// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "idfxx/max31865"

#include <idfxx/sched>

#include <algorithm>
#include <array>
#include <cmath>
#include <esp_log.h>
#include <span>
#include <utility>

namespace {
const char* TAG = "idfxx::max31865";
}

namespace idfxx::max31865 {

using quanta::electro::milliohms;
using quanta::thermo::millicelsius;
using std::chrono::microseconds;
using std::chrono::steady_clock;

// =============================================================================
// Callendar–Van Dusen math
// =============================================================================

namespace detail {

namespace {

// Full-scale code span: R = code × RREF / 2^15.
constexpr int64_t code_span = 32768;

constexpr double to_ohms(milliohms r) noexcept {
    return static_cast<double>(r.count()) / 1000.0;
}

uint16_t saturate_code(double code) noexcept {
    if (!(code > 0.0)) {
        return 0;
    }
    if (code >= static_cast<double>(max_code)) {
        return max_code;
    }
    return static_cast<uint16_t>(std::lround(code));
}

} // namespace

double resistance_at(millicelsius t, milliohms r0, const cvd_coefficients& k) noexcept {
    return cvd_resistance(static_cast<double>(t.count()) / 1000.0, to_ohms(r0), k);
}

double rref_ohms(milliohms rref) noexcept {
    return to_ohms(rref);
}

uint16_t ohms_to_code(double ohms, milliohms rref) noexcept {
    if (rref.count() <= 0) {
        return 0;
    }
    return saturate_code(ohms * static_cast<double>(code_span) / to_ohms(rref));
}

double cvd_resistance(double celsius, double r0_ohms, const cvd_coefficients& k) noexcept {
    const double t = celsius;
    double ratio = 1.0 + k.a * t + k.b * t * t;
    if (t < 0.0) {
        ratio += k.c * (t - 100.0) * t * t * t;
    }
    return r0_ohms * ratio;
}

double cvd_temperature(double ohms, double r0_ohms, const cvd_coefficients& k) noexcept {
    // Quadratic root of R0(1 + aT + bT²) = R, exact for T ≥ 0. Written as
    // 2z / (a + sqrt(a² + 4bz)) with z = R/R0 − 1, which avoids the
    // cancellation of the textbook (−a + sqrt(...)) / 2b form.
    const double z = ohms / r0_ohms - 1.0;
    const double disc = std::max(k.a * k.a + 4.0 * k.b * z, 0.0);
    double t = 2.0 * z / (k.a + std::sqrt(disc));
    if (ohms >= r0_ohms) {
        return t;
    }

    // Below 0 °C the quartic c term applies: refine the quadratic estimate
    // with Newton's method on the full equation.
    for (int i = 0; i < 16; ++i) {
        const double f = cvd_resistance(t, r0_ohms, k) - ohms;
        const double df = r0_ohms * (k.a + 2.0 * k.b * t + k.c * (4.0 * t * t * t - 300.0 * t * t));
        if (df == 0.0) {
            break;
        }
        const double step = f / df;
        t -= step;
        if (std::fabs(step) < 1e-9) {
            break;
        }
    }
    return t;
}

milliohms code_to_resistance(uint16_t code, milliohms rref) noexcept {
    return milliohms{(static_cast<int64_t>(code) * rref.count() + code_span / 2) / code_span};
}

uint16_t resistance_to_code(milliohms r, milliohms rref) noexcept {
    if (r.count() <= 0 || rref.count() <= 0) {
        return 0;
    }
    if (r >= rref) {
        return max_code;
    }
    const int64_t code = (r.count() * code_span + rref.count() / 2) / rref.count();
    return static_cast<uint16_t>(std::min<int64_t>(code, max_code));
}

millicelsius code_to_temperature(uint16_t code, milliohms rref, milliohms r0, const cvd_coefficients& k) noexcept {
    const double r = static_cast<double>(code) * to_ohms(rref) / static_cast<double>(code_span);
    const double t = cvd_temperature(r, to_ohms(r0), k);
    return millicelsius{std::llround(t * 1000.0)};
}

uint16_t temperature_to_code(millicelsius t, milliohms rref, milliohms r0, const cvd_coefficients& k) noexcept {
    return ohms_to_code(resistance_at(t, r0, k), rref);
}

} // namespace detail

// =============================================================================
// Device state and register access
// =============================================================================

namespace {

// Margin on top of the datasheet maximum before declaring a timeout.
constexpr microseconds drdy_timeout_margin{10'000};
constexpr microseconds fault_cycle_timeout_margin{1'000};
constexpr microseconds fault_cycle_poll_interval{50};

uint8_t initial_config_byte(const device::config& cfg) {
    uint8_t b = 0;
    if (*cfg.wiring == wiring::three_wire) {
        b |= detail::cfg_three_wire;
    }
    if (*cfg.notch == notch_filter::hz_50) {
        b |= detail::cfg_notch_50hz;
    }
    return b;
}

} // namespace

struct device::state {
    state(spi::master_device d, config c)
        : dev(std::move(d))
        , cfg(std::move(c))
        , config_byte(initial_config_byte(cfg)) {}

    spi::master_device dev;
    config cfg;

    // Intended register contents. config_byte holds only the persistent bits
    // (bias, conversion mode, wiring, notch), never the self-clearing ones.
    uint8_t config_byte;
    uint16_t low_threshold = detail::por_low_threshold;
    uint16_t high_threshold = detail::por_high_threshold;

    // Whether the bias was on before start_continuous(), restored by stop_continuous().
    bool bias_before_continuous = false;

    // When the bias was last turned on, and when the input filter settles
    // after a fault detection cycle.
    steady_clock::time_point bias_on_since{};
    steady_clock::time_point settled_at{};

    [[nodiscard]] bool bias_on() const noexcept { return (config_byte & detail::cfg_bias) != 0; }
    [[nodiscard]] bool continuous() const noexcept { return (config_byte & detail::cfg_continuous) != 0; }

    [[nodiscard]] result<void> write(uint8_t reg, std::span<const uint8_t> data) {
        spi::transaction t{
            .addr = static_cast<uint64_t>(reg | detail::write_bit),
            .tx_buffer = data,
            .length = data.size() * 8,
        };
        return dev.try_polling_transmit(t);
    }

    [[nodiscard]] result<void> read(uint8_t reg, std::span<uint8_t> data) {
        // Half duplex: address phase, then a receive-only data phase. Reads
        // auto-increment, so multi-byte registers come back in one transaction.
        spi::transaction t{
            .addr = reg,
            .rx_buffer = data,
            .rx_length = data.size() * 8,
        };
        return dev.try_polling_transmit(t);
    }

    [[nodiscard]] result<void> write_config(uint8_t value) {
        alignas(4) const std::array<uint8_t, 1> buf{value};
        return write(detail::reg_config, buf);
    }

    [[nodiscard]] result<uint8_t> read_config() {
        alignas(4) std::array<uint8_t, 1> buf{};
        if (auto r = read(detail::reg_config, buf); !r) {
            return error(r.error());
        }
        return buf[0];
    }

    [[nodiscard]] result<void> write_thresholds(uint16_t low, uint16_t high) {
        // Threshold registers hold the code in D15:D1 (D0 is don't-care),
        // high threshold first (03h–06h), written in one auto-incrementing burst.
        const uint16_t high_reg = static_cast<uint16_t>(high << 1);
        const uint16_t low_reg = static_cast<uint16_t>(low << 1);
        alignas(4) const std::array<uint8_t, 4> buf{
            static_cast<uint8_t>(high_reg >> 8),
            static_cast<uint8_t>(high_reg & 0xFF),
            static_cast<uint8_t>(low_reg >> 8),
            static_cast<uint8_t>(low_reg & 0xFF),
        };
        return write(detail::reg_high_threshold_msb, buf);
    }

    [[nodiscard]] result<std::pair<uint16_t, uint16_t>> read_thresholds() {
        alignas(4) std::array<uint8_t, 4> buf{};
        if (auto r = read(detail::reg_high_threshold_msb, buf); !r) {
            return error(r.error());
        }
        const uint16_t high = static_cast<uint16_t>(((buf[0] << 8) | buf[1]) >> 1);
        const uint16_t low = static_cast<uint16_t>(((buf[2] << 8) | buf[3]) >> 1);
        return std::pair{low, high};
    }

    [[nodiscard]] result<sample> read_rtd() {
        alignas(4) std::array<uint8_t, 2> buf{};
        if (auto r = read(detail::reg_rtd_msb, buf); !r) {
            return error(r.error());
        }
        const uint16_t raw = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
        return sample{.code = static_cast<uint16_t>(raw >> 1), .fault = (raw & 0x01) != 0};
    }

    // Writes the intended configuration and thresholds.
    [[nodiscard]] result<void> write_all() {
        if (auto r = write_config(config_byte); !r) {
            return r;
        }
        return write_thresholds(low_threshold, high_threshold);
    }

    // Compares the chip's configuration and thresholds against the intended ones.
    [[nodiscard]] result<bool> matches() {
        auto cfg_reg = read_config();
        if (!cfg_reg) {
            return error(cfg_reg.error());
        }
        if ((*cfg_reg & ~detail::cfg_self_clearing) != config_byte) {
            ESP_LOGD(TAG, "config register 0x%02X, expected 0x%02X", *cfg_reg, config_byte);
            return false;
        }
        auto thresholds = read_thresholds();
        if (!thresholds) {
            return error(thresholds.error());
        }
        if (thresholds->first != low_threshold || thresholds->second != high_threshold) {
            ESP_LOGD(
                TAG,
                "thresholds %u/%u, expected %u/%u",
                thresholds->first,
                thresholds->second,
                low_threshold,
                high_threshold
            );
            return false;
        }
        return true;
    }

    // Waits until the input filter has settled for a conversion: the bias has
    // been on for 10.5 time constants plus 1 ms, and any fault cycle has settled.
    void wait_for_settling() const {
        const auto bias_settled = bias_on_since + detail::bias_settle_time(*cfg.filter_time_constant);
        idfxx::delay_until(std::max(bias_settled, settled_at));
    }

    // Polls the fault detection cycle bits (D3:D2) until they read 00.
    [[nodiscard]] result<void> wait_fault_cycle() {
        const auto deadline = steady_clock::now() + detail::auto_fault_cycle_time + fault_cycle_timeout_margin;
        while (true) {
            auto cfg_reg = read_config();
            if (!cfg_reg) {
                return error(cfg_reg.error());
            }
            if ((*cfg_reg & detail::cfg_fault_cycle_mask) == 0) {
                return {};
            }
            if (steady_clock::now() > deadline) {
                ESP_LOGW(TAG, "fault detection cycle did not complete");
                return error(errc::timeout);
            }
            idfxx::delay(fault_cycle_poll_interval);
        }
    }

    // Waits for a one-shot conversion to complete.
    [[nodiscard]] result<void> wait_conversion() {
        const auto conversion_time = detail::max_conversion_time(*cfg.notch);
        if (!cfg.drdy.is_connected()) {
            idfxx::delay(conversion_time);
            return {};
        }
        const auto deadline = steady_clock::now() + conversion_time + drdy_timeout_margin;
        while (cfg.drdy.get_level() != gpio::level::low) {
            if (steady_clock::now() > deadline) {
                ESP_LOGW(TAG, "DRDY did not go low");
                return error(errc::timeout);
            }
            idfxx::delay(idfxx::next_tick);
        }
        return {};
    }

    [[nodiscard]] result<sample> one_shot() {
        if (continuous()) {
            return error(errc::invalid_state);
        }
        const uint8_t biased = config_byte | detail::cfg_bias;
        if (!bias_on()) {
            if (auto r = write_config(biased); !r) {
                return error(r.error());
            }
            bias_on_since = steady_clock::now();
        }
        wait_for_settling();

        // An unread result holds DRDY low; read it so DRDY tracks this conversion.
        if (cfg.drdy.is_connected() && cfg.drdy.get_level() == gpio::level::low) {
            if (auto r = read_rtd(); !r) {
                return error(r.error());
            }
        }

        // The conversion starts when CS rises after the 1-shot bit is written.
        if (auto r = write_config(biased | detail::cfg_one_shot); !r) {
            return error(r.error());
        }
        if (auto r = wait_conversion(); !r) {
            return error(r.error());
        }
        return read_rtd();
    }
};

// =============================================================================
// Construction
// =============================================================================

device::device(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

result<device> device::make(spi::master_bus& bus, config cfg) {
    if (!cfg.cs->is_connected()) {
        ESP_LOGE(TAG, "cs must be set");
        return error(errc::invalid_arg);
    }
    if (cfg.clock_speed.count() <= 0 || cfg.clock_speed > detail::max_clock_speed) {
        ESP_LOGE(TAG, "clock_speed must be above 0 and at most 5 MHz");
        return error(errc::invalid_arg);
    }
    if (cfg.r0->count() <= 0 || cfg.rref->count() <= 0) {
        ESP_LOGE(TAG, "r0 and rref must be positive");
        return error(errc::invalid_arg);
    }
    if (cfg.filter_time_constant->count() < 0) {
        ESP_LOGE(TAG, "filter_time_constant must not be negative");
        return error(errc::invalid_arg);
    }

    if (cfg.drdy.is_connected()) {
        auto drdy = cfg.drdy;
        if (auto r = drdy.try_set_direction(gpio::mode::input); !r) {
            return error(r.error());
        }
    }

    // SPI mode 1, half duplex: an 8-bit address phase carries the register
    // address, followed by either a transmit or a receive data phase. CS is
    // asserted ahead of the first clock edge to meet the 400 ns tCC setup time.
    const spi::master_device::config dev_cfg{
        .address_bits = 8,
        .mode = 1,
        .cs_ena_pretrans = detail::cs_pretrans_cycles(cfg.clock_speed),
        .clock_speed = cfg.clock_speed,
        .cs = *cfg.cs,
        .flags = spi::device_flags::halfduplex,
    };
    auto dev = spi::master_device::make(bus, dev_cfg);
    if (!dev) {
        return error(dev.error());
    }

    auto s = std::make_unique<state>(std::move(*dev), std::move(cfg));

    if (auto r = s->write_all(); !r) {
        return error(r.error());
    }
    auto ok = s->matches();
    if (!ok) {
        return error(ok.error());
    }
    if (!*ok) {
        ESP_LOGE(TAG, "no MAX31865 responding (configuration did not read back)");
        return error(errc::not_found);
    }

    return device{std::move(s)};
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
device::device(spi::master_bus& bus, config cfg)
    : device(unwrap(make(bus, std::move(cfg)))) {}
#endif

device::device(device&& other) noexcept = default;
device& device::operator=(device&& other) noexcept = default;
device::~device() = default;

// =============================================================================
// Accessors
// =============================================================================

spi::master_device& device::spi() noexcept {
    return _state->dev;
}

// =============================================================================
// Conversions
// =============================================================================

result<sample> device::try_measure() {
    auto& s = *_state;
    const bool bias_was_on = s.bias_on();
    auto result = s.one_shot();
    if (!bias_was_on) {
        // Return the bias to off even if the conversion failed.
        auto off = s.write_config(s.config_byte);
        if (result && !off) {
            return error(off.error());
        }
    }
    return result;
}

result<void> device::try_start_continuous() {
    auto& s = *_state;
    if (s.continuous()) {
        return {};
    }
    const bool bias_was_on = s.bias_on();
    const uint8_t prior = s.config_byte;
    const uint8_t biased = prior | detail::cfg_bias;
    if (!bias_was_on) {
        // Bias on, conversions still off, so the input filter can settle first.
        if (auto r = s.write_config(biased); !r) {
            return r;
        }
        s.bias_on_since = steady_clock::now();
        s.config_byte = biased;
    }
    s.wait_for_settling();

    const uint8_t next = biased | detail::cfg_continuous;
    if (auto r = s.write_config(next); !r) {
        if (!bias_was_on) {
            // Best effort: return the bias to off, as it was before.
            if (s.write_config(prior)) {
                s.config_byte = prior;
            }
        }
        return r;
    }
    s.bias_before_continuous = bias_was_on;
    s.config_byte = next;
    return {};
}

result<void> device::try_stop_continuous() {
    auto& s = *_state;
    if (!s.continuous()) {
        return {};
    }
    uint8_t next = s.config_byte & ~detail::cfg_continuous;
    if (!s.bias_before_continuous) {
        next &= ~detail::cfg_bias;
    }
    if (auto r = s.write_config(next); !r) {
        return r;
    }
    s.config_byte = next;
    return {};
}

result<sample> device::try_read() {
    return _state->read_rtd();
}

result<millicelsius> device::try_read_temperature() {
    auto s = try_read();
    if (!s) {
        return error(s.error());
    }
    if (s->fault) {
        return error(errc::invalid_response);
    }
    return to_temperature(s->code);
}

// =============================================================================
// Bias
// =============================================================================

result<void> device::try_enable_bias() {
    auto& s = *_state;
    if (s.continuous()) {
        // The bias is already on; keep it on once continuous conversion stops.
        s.bias_before_continuous = true;
        return {};
    }
    if (s.bias_on()) {
        return {};
    }
    const uint8_t next = s.config_byte | detail::cfg_bias;
    if (auto r = s.write_config(next); !r) {
        return r;
    }
    s.config_byte = next;
    s.bias_on_since = steady_clock::now();
    return {};
}

result<void> device::try_disable_bias() {
    auto& s = *_state;
    if (s.continuous()) {
        return error(errc::invalid_state);
    }
    if (!s.bias_on()) {
        return {};
    }
    const uint8_t next = s.config_byte & ~detail::cfg_bias;
    if (auto r = s.write_config(next); !r) {
        return r;
    }
    s.config_byte = next;
    return {};
}

// =============================================================================
// Unit conversion
// =============================================================================

milliohms device::to_resistance(uint16_t code) const noexcept {
    return detail::code_to_resistance(code, *_state->cfg.rref);
}

millicelsius device::to_temperature(uint16_t code) const noexcept {
    const auto& c = _state->cfg;
    return detail::code_to_temperature(code, *c.rref, *c.r0, c.coefficients);
}

uint16_t device::to_code(milliohms resistance) const noexcept {
    return detail::resistance_to_code(resistance, *_state->cfg.rref);
}

uint16_t device::to_code(millicelsius temperature) const noexcept {
    const auto& c = _state->cfg;
    return detail::temperature_to_code(temperature, *c.rref, *c.r0, c.coefficients);
}

// =============================================================================
// Faults
// =============================================================================

result<flags<fault>> device::try_read_faults() {
    alignas(4) std::array<uint8_t, 1> buf{};
    if (auto r = _state->read(detail::reg_fault_status, buf); !r) {
        return error(r.error());
    }
    return flags<fault>{static_cast<uint8_t>(buf[0] & detail::fault_mask)};
}

result<void> device::try_clear_faults() {
    auto& s = *_state;
    // D1 set with D5 and D3:D2 clear; config_byte never holds those bits.
    return s.write_config(s.config_byte | detail::cfg_fault_clear);
}

result<flags<fault>> device::try_detect_faults() {
    auto& s = *_state;
    const auto tau = *s.cfg.filter_time_constant;
    // 100X0X0Xb base: bias on, conversions off, keep the wiring and notch bits.
    const uint8_t base =
        detail::cfg_bias | static_cast<uint8_t>(s.config_byte & (detail::cfg_three_wire | detail::cfg_notch_50hz));

    auto run_cycle = [&]() -> result<void> {
        // Clear faults latched earlier, so the result reflects only this cycle
        // (and any fault that persists, such as an over/under-voltage).
        if (auto r = s.write_config(s.config_byte | detail::cfg_fault_clear); !r) {
            return r;
        }

        // The bias must have been on for 5 time constants, and any earlier
        // fault cycle must have settled, before the first cycle write.
        if (!s.bias_on()) {
            if (auto r = s.write_config(base); !r) {
                return r;
            }
            s.bias_on_since = steady_clock::now();
        }
        idfxx::delay_until(std::max(s.bias_on_since + tau * 5, s.settled_at));

        if (tau <= detail::auto_fault_cycle_max_tau) {
            // Automatic: the chip inserts its own delays between the checks.
            if (auto r = s.write_config(base | detail::cfg_fault_auto); !r) {
                return r;
            }
            return s.wait_fault_cycle();
        }

        // Manual: wait 5 time constants between the two steps.
        if (auto r = s.write_config(base | detail::cfg_fault_manual_1); !r) {
            return r;
        }
        idfxx::delay(tau * 5);
        if (auto r = s.write_config(base | detail::cfg_fault_manual_2); !r) {
            return r;
        }
        return s.wait_fault_cycle();
    };

    auto faults = run_cycle().and_then([this]() { return try_read_faults(); });

    // The cycle disturbs the input filter: the datasheet asks for 5 time
    // constants plus 1 ms of settling before the next conversion.
    s.settled_at = steady_clock::now() + tau * 5 + microseconds{1'000};

    // Restore the intended configuration even if the cycle failed, resuming
    // continuous conversion (once settled) if it was running.
    if (s.continuous()) {
        idfxx::delay_until(s.settled_at);
    }
    auto restored = s.write_config(s.config_byte);
    if (faults && !restored) {
        return error(restored.error());
    }
    return faults;
}

// =============================================================================
// Fault thresholds
// =============================================================================

result<void> device::try_set_fault_thresholds(uint16_t low, uint16_t high) {
    if (low > detail::max_code || high > detail::max_code || low > high) {
        return error(errc::invalid_arg);
    }
    auto& s = *_state;
    if (auto r = s.write_thresholds(low, high); !r) {
        return r;
    }
    s.low_threshold = low;
    s.high_threshold = high;
    return {};
}

result<void> device::try_set_fault_thresholds(milliohms low, milliohms high) {
    const auto rref = *_state->cfg.rref;
    auto in_range = [&](milliohms r) { return r.count() >= 0 && r < rref; };
    if (!in_range(low) || !in_range(high) || low > high) {
        return error(errc::invalid_arg);
    }
    return try_set_fault_thresholds(to_code(low), to_code(high));
}

result<void> device::try_set_fault_thresholds(millicelsius low, millicelsius high) {
    if (low > high) {
        return error(errc::invalid_arg);
    }
    const auto& c = _state->cfg;
    const double rref = detail::rref_ohms(*c.rref);
    const double r_low = detail::resistance_at(low, *c.r0, c.coefficients);
    const double r_high = detail::resistance_at(high, *c.r0, c.coefficients);
    auto in_range = [&](double r) { return r >= 0.0 && r < rref; };
    if (!in_range(r_low) || !in_range(r_high)) {
        return error(errc::invalid_arg);
    }
    return try_set_fault_thresholds(detail::ohms_to_code(r_low, *c.rref), detail::ohms_to_code(r_high, *c.rref));
}

result<std::pair<uint16_t, uint16_t>> device::try_read_fault_thresholds() {
    return _state->read_thresholds();
}

// =============================================================================
// Verification
// =============================================================================

result<bool> device::try_verify() {
    return _state->matches();
}

result<void> device::try_restore() {
    auto& s = *_state;
    if (auto r = s.write_all(); !r) {
        return r;
    }
    if (s.bias_on()) {
        // A reset may have turned the bias off; let it settle again before measuring.
        s.bias_on_since = steady_clock::now();
    }
    return {};
}

} // namespace idfxx::max31865

// =============================================================================
// String conversion
// =============================================================================

namespace idfxx {

namespace {

std::string hex_byte(uint8_t v) {
    static constexpr char hex[] = "0123456789ABCDEF";
    return std::string("unknown(0x") + hex[v >> 4] + hex[v & 0xF] + ')';
}

} // namespace

std::string to_string(max31865::fault f) {
    switch (f) {
    case max31865::fault::high_threshold:
        return "high_threshold";
    case max31865::fault::low_threshold:
        return "low_threshold";
    case max31865::fault::refin_high:
        return "refin_high";
    case max31865::fault::refin_low:
        return "refin_low";
    case max31865::fault::rtdin_low:
        return "rtdin_low";
    case max31865::fault::over_under_voltage:
        return "over_under_voltage";
    default:
        return hex_byte(std::to_underlying(f));
    }
}

std::string to_string(flags<max31865::fault> faults) {
    if (faults.empty()) {
        return "none";
    }
    static constexpr max31865::fault all[] = {
        max31865::fault::high_threshold,
        max31865::fault::low_threshold,
        max31865::fault::refin_high,
        max31865::fault::refin_low,
        max31865::fault::rtdin_low,
        max31865::fault::over_under_voltage,
    };
    std::string s;
    uint8_t known = 0;
    for (auto f : all) {
        known |= std::to_underlying(f);
        if (faults.contains(f)) {
            if (!s.empty()) {
                s += '|';
            }
            s += to_string(f);
        }
    }
    if (const auto unknown = static_cast<uint8_t>(to_underlying(faults) & ~known); unknown != 0) {
        if (!s.empty()) {
            s += '|';
        }
        s += hex_byte(unknown);
    }
    return s;
}

} // namespace idfxx
