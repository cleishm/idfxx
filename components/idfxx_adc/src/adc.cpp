// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/adc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_continuous.h>
#include <esp_adc/adc_oneshot.h>
#include <memory>
#include <sdkconfig.h>
#include <soc/soc_caps.h>
#include <utility>

namespace idfxx::adc {

// Verify enum values match ESP-IDF constants.
static_assert(std::to_underlying(attenuation::db_0) == ADC_ATTEN_DB_0);
static_assert(std::to_underlying(attenuation::db_2_5) == ADC_ATTEN_DB_2_5);
static_assert(std::to_underlying(attenuation::db_6) == ADC_ATTEN_DB_6);
static_assert(std::to_underlying(attenuation::db_12) == ADC_ATTEN_DB_12);

// The sampler's parse loop strides the DMA buffer by result size; the header's
// blocking try_read passes the maximum uint32_t value as "wait forever".
static_assert(sizeof(adc_digi_output_data_t) == SOC_ADC_DIGI_RESULT_BYTES);
static_assert(ADC_MAX_DELAY == std::numeric_limits<uint32_t>::max());

namespace {

// The enumerators map one-to-one onto adc_atten_t, as the static_asserts above enforce.
adc_atten_t to_idf(attenuation a) {
    return static_cast<adc_atten_t>(a);
}

// Creates a best-effort factory-calibration handle for a channel, or returns nullptr when the chip
// provides no usable calibration scheme. `channel` is only used by curve fitting; line fitting
// calibrates per-unit and ignores it.
adc_cali_handle_t create_calibration(
    adc_unit_t unit,
    [[maybe_unused]] adc_channel_t channel,
    adc_atten_t atten,
    adc_bitwidth_t bitwidth
) {
    adc_cali_handle_t cali = nullptr;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg{};
    cfg.unit_id = unit;
    cfg.chan = channel;
    cfg.atten = atten;
    cfg.bitwidth = bitwidth;
    (void)adc_cali_create_scheme_curve_fitting(&cfg, &cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cfg{};
    cfg.unit_id = unit;
    cfg.atten = atten;
    cfg.bitwidth = bitwidth;
    (void)adc_cali_create_scheme_line_fitting(&cfg, &cali);
#endif
    return cali;
}

// Releases a handle from create_calibration(); no-ops on nullptr.
void delete_calibration([[maybe_unused]] adc_cali_handle_t cali) {
    if (!cali) {
        return;
    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    (void)adc_cali_delete_scheme_curve_fitting(cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    (void)adc_cali_delete_scheme_line_fitting(cali);
#endif
}

} // namespace

struct input::state {
    config cfg{};
    adc_channel_t channel = ADC_CHANNEL_0;
    adc_oneshot_unit_handle_t handle = nullptr;
    adc_cali_handle_t cali = nullptr;

    ~state() {
        delete_calibration(cali);
        if (handle) {
            (void)adc_oneshot_del_unit(handle);
        }
    }
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
input::input(config cfg)
    : input(unwrap(make(std::move(cfg)))) {}
#endif

input::input(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

result<input> input::make(config cfg) {
    if (!cfg.pin.is_connected()) {
        return error(errc::invalid_arg);
    }

    auto s = std::make_unique<state>();
    s->cfg = cfg;

    adc_unit_t unit = ADC_UNIT_1;
    if (auto e = wrap(adc_oneshot_io_to_channel(cfg.pin.num(), &unit, &s->channel)); !e) {
        return error(errc::invalid_arg);
    }

    adc_oneshot_unit_init_cfg_t unit_cfg{};
    unit_cfg.unit_id = unit;
    if (auto e = wrap(adc_oneshot_new_unit(&unit_cfg, &s->handle)); !e) {
        return error(e.error());
    }

    adc_oneshot_chan_cfg_t chan_cfg{};
    chan_cfg.atten = to_idf(cfg.attenuation);
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (auto e = wrap(adc_oneshot_config_channel(s->handle, s->channel, &chan_cfg)); !e) {
        return error(e.error());
    }

    // Factory calibration: best effort — reads still work uncalibrated.
    s->cali = create_calibration(unit, s->channel, chan_cfg.atten, ADC_BITWIDTH_DEFAULT);

    return input{std::move(s)};
}

input::~input() = default;
input::input(input&& other) noexcept = default;
input& input::operator=(input&& other) noexcept = default;

idfxx::gpio input::pin() const noexcept {
    return _state ? _state->cfg.pin : gpio::nc();
}

bool input::calibrated() const noexcept {
    return _state && _state->cali != nullptr;
}

result<int> input::try_read_raw() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    int raw = 0;
    if (auto e = wrap(adc_oneshot_read(_state->handle, _state->channel, &raw)); !e) {
        return error(e.error());
    }
    return raw;
}

result<electro::millivolts> input::try_read_voltage() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    if (!_state->cali) {
        return error(errc::not_supported);
    }
    auto raw = try_read_raw();
    if (!raw) {
        return error(raw.error());
    }
    int mv = 0;
    if (auto e = wrap(adc_cali_raw_to_voltage(_state->cali, *raw, &mv)); !e) {
        return error(e.error());
    }
    return electro::millivolts{mv};
}

struct sampler::state {
    config cfg{};
    adc_continuous_handle_t handle = nullptr;
    bool running = false;
    size_t overruns = 0;
    // Channels for cfg.pins, in the same order.
    std::vector<adc_channel_t> channels{};
    // Reverse map used when parsing: channel number -> configured pin (nc = unconfigured).
    std::array<idfxx::gpio, SOC_ADC_MAX_CHANNEL_NUM> chan_pin{};
    // Calibration handle per channel. With curve fitting each configured channel owns
    // its own handle; with line fitting every configured channel aliases one per-unit handle.
    std::array<adc_cali_handle_t, SOC_ADC_MAX_CHANNEL_NUM> chan_cali{};
    // Staging buffer for driver reads, parsed into caller-provided samples.
    std::vector<uint8_t> staging{};
    // Reused scratch for the fused voltage read: holds raw samples between read and conversion.
    std::vector<sample> read_scratch{};

    ~state() {
        if (handle && running) {
            (void)adc_continuous_stop(handle);
        }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        // Curve fitting owns one handle per channel: delete each.
        for (auto cali : chan_cali) {
            delete_calibration(cali);
        }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        // Every configured channel aliases the same per-unit handle: delete it once.
        auto it = std::ranges::find_if(chan_cali, [](auto cali) { return cali != nullptr; });
        if (it != chan_cali.end()) {
            delete_calibration(*it);
        }
#endif
        if (handle) {
            (void)adc_continuous_deinit(handle);
        }
    }
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
sampler::sampler(config cfg)
    : sampler(unwrap(make(std::move(cfg)))) {}
#endif

sampler::sampler(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

result<sampler> sampler::make(config cfg) {
    if (cfg.pins.empty() || cfg.pins.size() > SOC_ADC_PATT_LEN_MAX) {
        return error(errc::invalid_arg);
    }
    for (auto pin : cfg.pins) {
        if (!pin.is_connected()) {
            return error(errc::invalid_arg);
        }
    }
    if (cfg.frame_samples < 1 || cfg.buffer_samples < cfg.frame_samples) {
        return error(errc::invalid_arg);
    }
    if (cfg.sample_rate.count() <= 0) {
        return error(errc::invalid_arg);
    }

    auto s = std::make_unique<state>();
    s->cfg = std::move(cfg);
    s->chan_pin.fill(gpio::nc());

    // The digital (continuous) controller only serves ADC1 on the supported targets.
    for (auto pin : s->cfg.pins) {
        adc_unit_t unit = ADC_UNIT_1;
        adc_channel_t channel = ADC_CHANNEL_0;
        if (adc_continuous_io_to_channel(pin.num(), &unit, &channel) != ESP_OK || unit != ADC_UNIT_1) {
            return error(errc::invalid_arg);
        }
        // Each ADC1 channel maps to a single pin, so an already-populated slot means a duplicate pin.
        if (s->chan_pin[channel].is_connected()) {
            return error(errc::invalid_arg);
        }
        s->channels.push_back(channel);
        s->chan_pin[channel] = pin;
    }

    // The driver requires the frame size to be a multiple of one conversion unit; round up.
    constexpr size_t conv = SOC_ADC_DIGI_DATA_BYTES_PER_CONV;
    size_t frame_bytes = (s->cfg.frame_samples * SOC_ADC_DIGI_RESULT_BYTES + conv - 1) / conv * conv;
    size_t store_bytes = std::max(s->cfg.buffer_samples * SOC_ADC_DIGI_RESULT_BYTES, frame_bytes);
    s->staging.resize(frame_bytes);

    adc_continuous_handle_cfg_t handle_cfg{};
    handle_cfg.max_store_buf_size = store_bytes;
    handle_cfg.conv_frame_size = frame_bytes;
    if (auto e = wrap(adc_continuous_new_handle(&handle_cfg, &s->handle)); !e) {
        return error(e.error());
    }

    std::array<adc_digi_pattern_config_t, SOC_ADC_PATT_LEN_MAX> pattern{};
    for (size_t i = 0; i < s->channels.size(); ++i) {
        pattern[i].atten = to_idf(s->cfg.attenuation);
        pattern[i].channel = s->channels[i];
        pattern[i].unit = ADC_UNIT_1;
        pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }

    adc_continuous_config_t dig_cfg{};
    dig_cfg.pattern_num = s->channels.size();
    dig_cfg.adc_pattern = pattern.data();
    dig_cfg.sample_freq_hz = static_cast<uint32_t>(s->cfg.sample_rate.count());
    dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    if (auto e = wrap(adc_continuous_config(s->handle, &dig_cfg)); !e) {
        return error(e.error());
    }

    // Factory calibration: best effort — reads still work uncalibrated.
    [[maybe_unused]] const auto atten = to_idf(s->cfg.attenuation);
    [[maybe_unused]] const auto bitwidth = static_cast<adc_bitwidth_t>(SOC_ADC_DIGI_MAX_BITWIDTH);
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    // Curve fitting calibrates per channel: one handle each.
    for (auto channel : s->channels) {
        s->chan_cali[channel] = create_calibration(ADC_UNIT_1, channel, atten, bitwidth);
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    // Line fitting calibrates per unit: create one handle and alias it across channels.
    adc_cali_handle_t cali = create_calibration(ADC_UNIT_1, s->channels.front(), atten, bitwidth);
    for (auto channel : s->channels) {
        s->chan_cali[channel] = cali;
    }
#endif

    return sampler{std::move(s)};
}

sampler::~sampler() = default;
sampler::sampler(sampler&& other) noexcept = default;
sampler& sampler::operator=(sampler&& other) noexcept = default;

std::span<const idfxx::gpio> sampler::pins() const noexcept {
    return _state ? std::span<const idfxx::gpio>{_state->cfg.pins} : std::span<const idfxx::gpio>{};
}

freq::hertz sampler::sample_rate() const noexcept {
    return _state ? _state->cfg.sample_rate : freq::hertz{0};
}

bool sampler::running() const noexcept {
    return _state && _state->running;
}

bool sampler::calibrated() const noexcept {
    return _state && std::ranges::all_of(_state->channels, [this](auto channel) { return _state->chan_cali[channel]; });
}

size_t sampler::overruns() const noexcept {
    return _state ? _state->overruns : 0;
}

result<void> sampler::try_start() {
    if (!_state || _state->running) {
        return error(errc::invalid_state);
    }
    if (auto e = wrap(adc_continuous_start(_state->handle)); !e) {
        return error(e.error());
    }
    _state->overruns = 0;
    _state->running = true;
    return {};
}

result<void> sampler::try_stop() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    if (!_state->running) {
        return {};
    }
    if (auto e = wrap(adc_continuous_stop(_state->handle)); !e) {
        return error(e.error());
    }
    _state->running = false;
    return {};
}

result<size_t> sampler::_try_read(std::span<sample> out, uint32_t timeout_ms) {
    if (!_state || !_state->running) {
        return error(errc::invalid_state);
    }
    if (out.empty()) {
        return error(errc::invalid_arg);
    }

    const bool forever = timeout_ms == ADC_MAX_DELAY;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
    const size_t want_bytes = std::min(out.size() * SOC_ADC_DIGI_RESULT_BYTES, _state->staging.size());

    // A driver read can return only arbiter garbage (entries for channels we never
    // configured), so loop against the deadline until at least one valid sample lands.
    size_t count = 0;
    uint32_t remaining_ms = timeout_ms;
    while (count == 0) {
        uint32_t out_len = 0;
        esp_err_t err = adc_continuous_read(_state->handle, _state->staging.data(), want_bytes, &out_len, remaining_ms);
        if (err == ESP_ERR_TIMEOUT) {
            return error(errc::timeout);
        }
        if (err == ESP_ERR_INVALID_STATE) {
            // The internal pool overflowed and samples were dropped; the bytes
            // returned by this read are still valid.
            ++_state->overruns;
        } else if (err != ESP_OK) {
            return error(err);
        }

        const uint8_t* buf = _state->staging.data();
        for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= out_len && count < out.size();
             i += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t data;
            std::memcpy(&data, buf + i, sizeof(data));
#if CONFIG_IDF_TARGET_ESP32
            const uint32_t channel = data.type1.channel;
            const int raw = data.type1.data;
#else
            const uint32_t channel = data.type2.channel;
            const int raw = data.type2.data;
#endif
            const idfxx::gpio pin = channel < SOC_ADC_MAX_CHANNEL_NUM ? _state->chan_pin[channel] : gpio::nc();
            if (!pin.is_connected()) {
                continue;
            }
            out[count++] = {.pin = pin, .raw = raw};
        }

        if (count == 0 && !forever) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return error(errc::timeout);
            }
            auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
            remaining_ms = left > ADC_MAX_DELAY - 1 ? ADC_MAX_DELAY - 1 : static_cast<uint32_t>(left);
        }
    }
    return count;
}

result<electro::millivolts> sampler::try_to_voltage(const sample& s) const {
    electro::millivolts mv{0};
    if (auto e = try_to_voltage(std::span{&s, 1}, std::span{&mv, 1}); !e) {
        return error(e.error());
    }
    return mv;
}

result<void> sampler::try_to_voltage(std::span<const sample> in, std::span<electro::millivolts> out) const {
    if (!_state) {
        return error(errc::invalid_state);
    }
    if (out.size() < in.size()) {
        return error(errc::invalid_arg);
    }

    // Samples arrive in pin-runs (a single pin, or round-robin), so cache the last pin's handle
    // and re-resolve only when the pin changes.
    idfxx::gpio cached_pin = gpio::nc();
    adc_cali_handle_t cached_cali = nullptr;
    for (size_t i = 0; i < in.size(); ++i) {
        if (i == 0 || in[i].pin != cached_pin) {
            auto it = std::ranges::find(_state->cfg.pins, in[i].pin);
            if (it == _state->cfg.pins.end()) {
                return error(errc::invalid_arg);
            }
            cached_cali = _state->chan_cali[_state->channels[static_cast<size_t>(it - _state->cfg.pins.begin())]];
            cached_pin = in[i].pin;
        }
        if (!cached_cali) {
            return error(errc::not_supported);
        }
        int mv = 0;
        if (auto e = wrap(adc_cali_raw_to_voltage(cached_cali, in[i].raw, &mv)); !e) {
            return error(e.error());
        }
        out[i] = electro::millivolts{mv};
    }
    return {};
}

result<size_t> sampler::_read_voltage(std::span<electro::millivolts> out, uint32_t timeout_ms) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    if (_state->read_scratch.size() < out.size()) {
        _state->read_scratch.resize(out.size());
    }
    std::span<sample> scratch{_state->read_scratch.data(), out.size()};

    auto n = _try_read(scratch, timeout_ms);
    if (!n) {
        return error(n.error());
    }
    if (auto e = try_to_voltage(scratch.first(*n), out.first(*n)); !e) {
        return error(e.error());
    }
    return *n;
}

} // namespace idfxx::adc
