// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/pwm>

#include <driver/ledc.h>
#include <esp_log.h>
#include <mutex>
#include <utility>

// Verify enum values match ESP-IDF constants
using namespace idfxx::pwm;

static_assert(std::to_underlying(channel::ch_0) == LEDC_CHANNEL_0);
static_assert(std::to_underlying(channel::ch_1) == LEDC_CHANNEL_1);
static_assert(std::to_underlying(channel::ch_2) == LEDC_CHANNEL_2);
static_assert(std::to_underlying(channel::ch_3) == LEDC_CHANNEL_3);
static_assert(std::to_underlying(channel::ch_4) == LEDC_CHANNEL_4);
static_assert(std::to_underlying(channel::ch_5) == LEDC_CHANNEL_5);
#if SOC_LEDC_CHANNEL_NUM > 6
static_assert(std::to_underlying(channel::ch_6) == LEDC_CHANNEL_6);
#endif
#if SOC_LEDC_CHANNEL_NUM > 7
static_assert(std::to_underlying(channel::ch_7) == LEDC_CHANNEL_7);
#endif

static_assert(std::to_underlying(fade_mode::no_wait) == LEDC_FADE_NO_WAIT);
static_assert(std::to_underlying(fade_mode::wait_done) == LEDC_FADE_WAIT_DONE);

namespace {

const char* TAG = "idfxx::pwm";

// Per-timer state protected by a mutex.
// Tracks configuration for auto-allocation matching.
struct timer_state {
    std::mutex mutex;
    uint8_t resolution_bits = 0; // 0 = unconfigured
    uint32_t frequency_hz = 0;
    int8_t clk_source = 0;
};
timer_state timers[LEDC_SPEED_MODE_MAX][SOC_LEDC_TIMER_NUM];

// Per-channel state protected by a mutex.
// Tracks ownership (generation counter) and active status.
struct channel_state {
    std::mutex mutex;
    uint32_t gen = 0;
    bool active = false;
    int timer_num = 0;
};
channel_state channels[LEDC_SPEED_MODE_MAX][SOC_LEDC_CHANNEL_NUM];

// Serializes auto-allocation searches across all timers and channels.
std::mutex allocation_mutex;

ledc_mode_t to_idf(enum speed_mode mode) {
    switch (mode) {
#if SOC_LEDC_SUPPORT_HS_MODE
    case speed_mode::high_speed:
        return LEDC_HIGH_SPEED_MODE;
#endif
    default:
        return LEDC_LOW_SPEED_MODE;
    }
}

ledc_clk_cfg_t to_idf(enum clk_source src) {
    switch (src) {
#if SOC_LEDC_SUPPORT_APB_CLOCK
    case clk_source::apb:
        return LEDC_USE_APB_CLK;
#endif
#if SOC_LEDC_SUPPORT_REF_TICK
    case clk_source::ref_tick:
        return LEDC_USE_REF_TICK;
#endif
#if SOC_LEDC_SUPPORT_PLL_DIV_CLOCK
    case clk_source::pll_div:
        return LEDC_USE_PLL_DIV_CLK;
#endif
#if SOC_LEDC_SUPPORT_XTAL_CLOCK
    case clk_source::xtal:
        return LEDC_USE_XTAL_CLK;
#endif
    default:
        return LEDC_AUTO_CLK;
    }
}

} // namespace

namespace idfxx::pwm {

// ======================================================================
// timer
// ======================================================================

result<void> timer::try_configure(const struct config& cfg) {
    if (cfg.frequency.count() <= 0) {
        ESP_LOGD(TAG, "Field 'frequency' has an invalid value");
        return error(errc::invalid_arg);
    }
    if (cfg.resolution_bits < 1 || cfg.resolution_bits > SOC_LEDC_TIMER_BIT_WIDTH) {
        ESP_LOGD(TAG, "Field 'resolution_bits' has an invalid value (%d)", cfg.resolution_bits);
        return error(errc::invalid_arg);
    }

    ledc_timer_config_t timer_cfg{
        .speed_mode = to_idf(_speed_mode),
        .duty_resolution = static_cast<ledc_timer_bit_t>(cfg.resolution_bits),
        .timer_num = static_cast<ledc_timer_t>(_num),
        .freq_hz = static_cast<uint32_t>(cfg.frequency.count()),
        .clk_cfg = to_idf(cfg.clk_source),
        .deconfigure = false,
    };

    if (auto err = ledc_timer_config(&timer_cfg); err != ESP_OK) {
        ESP_LOGD(
            TAG,
            "Failed to configure LEDC timer %d (Frequency: %d Hz, Resolution: %d bits): %s",
            _num,
            static_cast<int>(cfg.frequency.count()),
            cfg.resolution_bits,
            esp_err_to_name(err)
        );
        return error(err);
    }

    {
        auto& state = timers[to_idf(_speed_mode)][_num];
        std::scoped_lock lock(state.mutex);
        state.frequency_hz = static_cast<uint32_t>(cfg.frequency.count());
        state.clk_source = static_cast<int8_t>(std::to_underlying(cfg.clk_source));
        state.resolution_bits = cfg.resolution_bits;
    }

    ESP_LOGD(
        TAG,
        "LEDC timer %d configured (Frequency: %d Hz, Resolution: %d bits)",
        _num,
        static_cast<int>(cfg.frequency.count()),
        cfg.resolution_bits
    );
    return {};
}

bool timer::is_configured() const noexcept {
    auto& state = timers[to_idf(_speed_mode)][_num];
    std::scoped_lock lock(state.mutex);
    return state.resolution_bits > 0;
}

uint8_t timer::resolution_bits() const noexcept {
    auto& state = timers[to_idf(_speed_mode)][_num];
    std::scoped_lock lock(state.mutex);
    return state.resolution_bits;
}

uint32_t timer::ticks_max() const noexcept {
    auto& state = timers[to_idf(_speed_mode)][_num];
    std::scoped_lock lock(state.mutex);
    return 1u << state.resolution_bits;
}

result<void> timer::try_set_frequency(freq::hertz frequency) {
    if (!is_configured()) {
        return error(errc::invalid_state);
    }
    if (frequency.count() <= 0) {
        return error(errc::invalid_arg);
    }
    return wrap(
        ledc_set_freq(to_idf(_speed_mode), static_cast<ledc_timer_t>(_num), static_cast<uint32_t>(frequency.count()))
    );
}

freq::hertz timer::frequency() const {
    if (!is_configured()) {
        return freq::hertz{0};
    }
    return freq::hertz{static_cast<int64_t>(ledc_get_freq(to_idf(_speed_mode), static_cast<ledc_timer_t>(_num)))};
}

std::chrono::nanoseconds timer::period() const {
    auto freq = frequency();
    if (freq.count() <= 0) {
        return std::chrono::nanoseconds{0};
    }
    return std::chrono::nanoseconds{1'000'000'000 / freq.count()};
}

std::chrono::nanoseconds timer::tick_period() const {
    auto max = ticks_max();
    if (max == 0) {
        return std::chrono::nanoseconds{0};
    }
    return period() / max;
}

result<void> timer::try_pause() {
    return wrap(ledc_timer_pause(to_idf(_speed_mode), static_cast<ledc_timer_t>(_num)));
}

result<void> timer::try_resume() {
    return wrap(ledc_timer_resume(to_idf(_speed_mode), static_cast<ledc_timer_t>(_num)));
}

result<void> timer::try_reset() {
    return wrap(ledc_timer_rst(to_idf(_speed_mode), static_cast<ledc_timer_t>(_num)));
}

// ======================================================================
// output
// ======================================================================

output::output(class timer tmr, enum channel ch, idfxx::gpio gpio, uint32_t gen) noexcept
    : _timer(tmr)
    , _channel(ch)
    , _gpio(gpio)
    , _gen(gen) {}

static result<void> configure_channel(const timer& tmr, enum channel ch, idfxx::gpio gpio, const output_config& cfg) {
    if (!tmr.is_configured()) {
        ESP_LOGD(TAG, "Timer %d is not configured", tmr.idf_num());
        return error(errc::invalid_state);
    }
    if (!gpio.is_connected()) {
        ESP_LOGD(TAG, "Field 'gpio' has an invalid value");
        return error(errc::invalid_arg);
    }

    ledc_channel_config_t ch_cfg{
        .gpio_num = gpio.num(),
        .speed_mode = to_idf(tmr.speed_mode()),
        .channel = static_cast<ledc_channel_t>(std::to_underlying(ch)),
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = static_cast<ledc_timer_t>(tmr.idf_num()),
        .duty = static_cast<uint32_t>(std::clamp(cfg.duty, 0.0f, 1.0f) * static_cast<float>(tmr.ticks_max())),
        .hpoint = cfg.hpoint,
        .sleep_mode = static_cast<ledc_sleep_mode_t>(std::to_underlying(cfg.sleep_mode)),
        .flags = {.output_invert = cfg.output_invert ? 1u : 0u},
    };

    if (auto err = ledc_channel_config(&ch_cfg); err != ESP_OK) {
        ESP_LOGD(
            TAG,
            "Failed to configure LEDC channel %d (Timer: %d, GPIO: %d): %s",
            std::to_underlying(ch),
            tmr.idf_num(),
            gpio.num(),
            esp_err_to_name(err)
        );
        return error(err);
    }

    ESP_LOGD(
        TAG,
        "LEDC output created (Timer: %d, Channel: %d, GPIO: %d, Duty: %.1f%%)",
        tmr.idf_num(),
        std::to_underlying(ch),
        gpio.num(),
        static_cast<double>(cfg.duty * 100.0f)
    );
    return {};
}

result<output> output::make(const timer& tmr, enum channel ch, idfxx::gpio gpio, const output_config& cfg) {
    auto& state = channels[to_idf(tmr.speed_mode())][std::to_underlying(ch)];
    std::scoped_lock lock(state.mutex);
    return _make(tmr, ch, gpio, cfg);
}

result<output> output::make(const timer& tmr, enum channel ch, idfxx::gpio gpio) {
    return make(tmr, ch, gpio, output_config{});
}

result<output> output::_make(const timer& tmr, enum channel ch, idfxx::gpio gpio, const output_config& cfg) {
    auto r = configure_channel(tmr, ch, gpio, cfg);
    if (!r) {
        return error(r.error());
    }
    auto& state = channels[to_idf(tmr.speed_mode())][std::to_underlying(ch)];
    auto gen = ++state.gen;
    state.active = true;
    state.timer_num = tmr.idf_num();
    return output{tmr, ch, gpio, gen};
}

bool is_active(enum channel ch, enum speed_mode mode) {
    auto& state = channels[to_idf(mode)][std::to_underlying(ch)];
    std::scoped_lock lock(state.mutex);
    return state.active;
}

std::optional<timer> get_timer(enum channel ch, enum speed_mode mode) {
    auto& state = channels[to_idf(mode)][std::to_underlying(ch)];
    std::scoped_lock lock(state.mutex);
    if (!state.active) {
        return std::nullopt;
    }
    return timer{state.timer_num, mode};
}

result<output> try_start(idfxx::gpio gpio, const timer& tmr, enum channel ch, const output_config& cfg) {
    return output::make(tmr, ch, gpio, cfg);
}

result<output> try_start(idfxx::gpio gpio, const timer& tmr, enum channel ch) {
    return output::make(tmr, ch, gpio);
}

result<output> try_start(idfxx::gpio gpio, const timer::config& cfg, const output_config& out_cfg) {
    if (!gpio.is_connected()) {
        ESP_LOGD(TAG, "Field 'gpio' has an invalid value");
        return error(errc::invalid_arg);
    }

    constexpr auto mode = speed_mode::low_speed;
    const auto idf_mode = to_idf(mode);

    std::scoped_lock lock(allocation_mutex);

    // Step 1: Find a matching or free timer
    int matched_timer = -1;
    int free_timer = -1;

    for (int t = 0; t < SOC_LEDC_TIMER_NUM; ++t) {
        auto& state = timers[idf_mode][t];
        std::scoped_lock tmr_lock(state.mutex);
        if (state.resolution_bits == 0) {
            if (free_timer < 0) {
                free_timer = t;
            }
            continue;
        }
        if (state.frequency_hz == static_cast<uint32_t>(cfg.frequency.count()) &&
            state.resolution_bits == cfg.resolution_bits &&
            state.clk_source == static_cast<int8_t>(std::to_underlying(cfg.clk_source))) {
            matched_timer = t;
            break;
        }
    }

    int timer_num;
    if (matched_timer >= 0) {
        timer_num = matched_timer;
        ESP_LOGD(TAG, "Auto-allocate: reusing timer %d", timer_num);
    } else if (free_timer >= 0) {
        timer_num = free_timer;
        timer tmr{timer_num, mode};
        auto r = tmr.try_configure(cfg);
        if (!r) {
            return error(r.error());
        }
        ESP_LOGD(TAG, "Auto-allocate: configured new timer %d", timer_num);
    } else {
        ESP_LOGD(TAG, "Auto-allocate: no free timer available");
        return error(errc::not_found);
    }

    timer tmr{timer_num, mode};

    // Step 2: Find a free channel
    for (int c = 0; c < SOC_LEDC_CHANNEL_NUM; ++c) {
        auto& state = channels[idf_mode][c];
        std::scoped_lock ch_lock(state.mutex);
        if (!state.active) {
            return output::_make(tmr, static_cast<enum channel>(c), gpio, out_cfg);
        }
    }

    ESP_LOGD(TAG, "Auto-allocate: no free channel available");
    return error(errc::not_found);
}

result<output> try_start(idfxx::gpio gpio, const timer::config& cfg) {
    return try_start(gpio, cfg, output_config{});
}

result<void> try_stop(enum channel ch, enum speed_mode mode, idfxx::gpio::level idle_level) {
    auto& state = channels[to_idf(mode)][std::to_underlying(ch)];
    std::scoped_lock lock(state.mutex);
    if (!state.active) {
        return {};
    }
    auto err =
        ledc_stop(to_idf(mode), static_cast<ledc_channel_t>(std::to_underlying(ch)), static_cast<uint32_t>(idle_level));
    if (err != ESP_OK) {
        return error(err);
    }
    ++state.gen;
    state.active = false;
    return {};
}

output::output(output&& other) noexcept
    : _timer(other._timer)
    , _channel(other._channel)
    , _gpio(other._gpio)
    , _gen(std::exchange(other._gen, std::nullopt)) {}

output& output::operator=(output&& other) noexcept {
    if (this != &other) {
        _cleanup();
        _timer = other._timer;
        _channel = other._channel;
        _gpio = other._gpio;
        _gen = std::exchange(other._gen, std::nullopt);
    }
    return *this;
}

output::~output() {
    _cleanup();
}

void output::_cleanup() noexcept {
    if (_gen) {
        auto& state = channels[to_idf(_timer.speed_mode())][std::to_underlying(_channel)];
        std::scoped_lock lock(state.mutex);
        if (_gen == state.gen) {
            ledc_stop(to_idf(_timer.speed_mode()), static_cast<ledc_channel_t>(std::to_underlying(_channel)), 0);
            state.active = false;
        }
    }
}

uint32_t output::ticks_max() const noexcept {
    return _timer.ticks_max();
}

float output::duty() const {
    if (!_gen) {
        return 0.0f;
    }
    auto max = ticks_max();
    if (max == 0) {
        return 0.0f;
    }
    return static_cast<float>(duty_ticks()) / static_cast<float>(max);
}

uint32_t output::duty_ticks() const {
    if (!_gen) {
        return 0;
    }
    return ledc_get_duty(to_idf(_timer.speed_mode()), static_cast<ledc_channel_t>(std::to_underlying(_channel)));
}

result<void> output::try_set_duty(float duty) {
    return try_set_duty_ticks(static_cast<uint32_t>(std::clamp(duty, 0.0f, 1.0f) * static_cast<float>(ticks_max())));
}

result<void> output::try_set_duty_ticks(uint32_t duty) {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    return wrap(ledc_set_duty_and_update(
        to_idf(_timer.speed_mode()), static_cast<ledc_channel_t>(std::to_underlying(_channel)), duty, 0
    ));
}

std::chrono::nanoseconds output::pulse_width() const {
    auto tp = _timer.tick_period();
    return std::chrono::nanoseconds{static_cast<int64_t>(duty_ticks()) * tp.count()};
}

result<void> output::_try_set_pulse_width(std::chrono::nanoseconds width) {
    auto tp = _timer.tick_period();
    if (tp.count() <= 0) {
        return error(errc::invalid_state);
    }
    auto ticks = static_cast<uint32_t>(width.count() / tp.count());
    return try_set_duty_ticks(ticks);
}

result<void> output::try_set_duty_ticks(uint32_t duty, uint32_t hpoint) {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    return wrap(ledc_set_duty_and_update(
        to_idf(_timer.speed_mode()), static_cast<ledc_channel_t>(std::to_underlying(_channel)), duty, hpoint
    ));
}

result<void> output::try_stop(idfxx::gpio::level idle_level) {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    return wrap(ledc_stop(
        to_idf(_timer.speed_mode()),
        static_cast<ledc_channel_t>(std::to_underlying(_channel)),
        static_cast<uint32_t>(idle_level)
    ));
}

enum channel output::release() noexcept {
    _gen.reset();
    return _channel;
}

result<void> output::_try_fade_to_pulse_width(
    std::chrono::nanoseconds target_width,
    std::chrono::milliseconds duration,
    enum fade_mode mode
) {
    auto tp = _timer.tick_period();
    if (tp.count() <= 0) {
        return error(errc::invalid_state);
    }
    auto ticks = static_cast<uint32_t>(target_width.count() / tp.count());
    return _try_fade_to_duty(ticks, duration, mode);
}

result<void> output::_try_fade_to_duty(uint32_t target_duty, std::chrono::milliseconds duration, enum fade_mode mode) {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    return wrap(ledc_set_fade_time_and_start(
        to_idf(_timer.speed_mode()),
        static_cast<ledc_channel_t>(std::to_underlying(_channel)),
        target_duty,
        static_cast<uint32_t>(duration.count()),
        static_cast<ledc_fade_mode_t>(std::to_underlying(mode))
    ));
}

result<void> output::try_fade_with_step(uint32_t target_duty, uint32_t scale, uint32_t cycle_num, enum fade_mode mode) {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    return wrap(ledc_set_fade_step_and_start(
        to_idf(_timer.speed_mode()),
        static_cast<ledc_channel_t>(std::to_underlying(_channel)),
        target_duty,
        scale,
        cycle_num,
        static_cast<ledc_fade_mode_t>(std::to_underlying(mode))
    ));
}

#if SOC_LEDC_SUPPORT_FADE_STOP
result<void> output::try_fade_stop() {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    return wrap(ledc_fade_stop(to_idf(_timer.speed_mode()), static_cast<ledc_channel_t>(std::to_underlying(_channel))));
}
#endif

result<void> output::try_install_fade_service(idfxx::intr_levels levels, idfxx::flags<intr_flag> flags) {
    int raw = to_underlying(levels) | to_underlying(flags);
    return wrap(ledc_fade_func_install(raw));
}

void output::uninstall_fade_service() {
    ledc_fade_func_uninstall();
}

} // namespace idfxx::pwm
