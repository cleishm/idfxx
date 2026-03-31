// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/pwm>

#include <driver/ledc.h>
#include <esp_idf_version.h>
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
    int8_t clk_source = 0;
};
timer_state timers[LEDC_SPEED_MODE_MAX][SOC_LEDC_TIMER_NUM];

// Per-channel state protected by a mutex.
// Tracks ownership (generation counter) and active status.
struct channel_state {
    std::mutex mutex;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    bool configured = false; // true once ledc_channel_config has been called
#endif
    uint32_t gen = 0;
    bool active = false;
    idfxx::gpio gpio;
    ledc_timer_t ledc_timer = LEDC_TIMER_0;
};
channel_state channels[LEDC_SPEED_MODE_MAX][SOC_LEDC_CHANNEL_NUM];

// Serializes auto-allocation searches across all timers and channels.
std::mutex allocation_mutex;

ledc_mode_t to_ledc(enum speed_mode mode) {
    switch (mode) {
#if SOC_LEDC_SUPPORT_HS_MODE
    case speed_mode::high_speed:
        return LEDC_HIGH_SPEED_MODE;
#endif
    default:
        return LEDC_LOW_SPEED_MODE;
    }
}

ledc_clk_cfg_t to_ledc(enum clk_source src) {
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

ledc_channel_t to_ledc(enum channel ch) {
    return static_cast<ledc_channel_t>(std::to_underlying(ch));
}

struct configure_result {
    uint32_t gen;
    uint32_t duty_ticks;
};

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
        .speed_mode = to_ledc(_speed_mode),
        .duty_resolution = static_cast<ledc_timer_bit_t>(cfg.resolution_bits),
        .timer_num = static_cast<ledc_timer_t>(_num),
        .freq_hz = static_cast<uint32_t>(cfg.frequency.count()),
        .clk_cfg = to_ledc(cfg.clk_source),
        .deconfigure = false,
    };

    if (auto err = ledc_timer_config(&timer_cfg); err != ESP_OK) {
        ESP_LOGD(
            TAG,
            "Failed to configure LEDC timer %u (Frequency: %d Hz, Resolution: %d bits): %s",
            _num,
            static_cast<int>(cfg.frequency.count()),
            cfg.resolution_bits,
            esp_err_to_name(err)
        );
        return error(err);
    }

    {
        auto& state = timers[to_ledc(_speed_mode)][_num];
        std::scoped_lock lock(state.mutex);
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
    auto& state = timers[to_ledc(_speed_mode)][_num];
    std::scoped_lock lock(state.mutex);
    return state.resolution_bits > 0;
}

uint8_t timer::resolution_bits() const noexcept {
    auto& state = timers[to_ledc(_speed_mode)][_num];
    std::scoped_lock lock(state.mutex);
    return state.resolution_bits;
}

uint32_t timer::ticks_max() const noexcept {
    auto& state = timers[to_ledc(_speed_mode)][_num];
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
        ledc_set_freq(to_ledc(_speed_mode), static_cast<ledc_timer_t>(_num), static_cast<uint32_t>(frequency.count()))
    );
}

freq::hertz timer::frequency() const {
    if (!is_configured()) {
        return freq::hertz{0};
    }
    return freq::hertz{static_cast<int64_t>(ledc_get_freq(to_ledc(_speed_mode), static_cast<ledc_timer_t>(_num)))};
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
    return wrap(ledc_timer_pause(to_ledc(_speed_mode), static_cast<ledc_timer_t>(_num)));
}

result<void> timer::try_resume() {
    return wrap(ledc_timer_resume(to_ledc(_speed_mode), static_cast<ledc_timer_t>(_num)));
}

result<void> timer::try_reset() {
    return wrap(ledc_timer_rst(to_ledc(_speed_mode), static_cast<ledc_timer_t>(_num)));
}

// ======================================================================
// output
// ======================================================================

result<output> try_start(idfxx::gpio gpio, const timer& tmr, enum channel ch) {
    return try_start(gpio, tmr, ch, output_config{});
}

result<output> try_start(idfxx::gpio gpio, const timer& tmr, enum channel ch, const output_config& cfg) {
    auto& state = channels[to_ledc(tmr.speed_mode())][std::to_underlying(ch)];
    std::scoped_lock lock(state.mutex);
    return output::_make(gpio, tmr, ch, cfg);
}

result<output> try_start(idfxx::gpio gpio, const timer::config& cfg) {
    return try_start(gpio, cfg, output_config{});
}

result<output> try_start(idfxx::gpio gpio, const timer::config& cfg, const output_config& out_cfg) {
    if (!gpio.is_connected()) {
        ESP_LOGD(TAG, "Field 'gpio' has an invalid value");
        return error(errc::invalid_arg);
    }

    constexpr auto mode = speed_mode::low_speed;
    const auto ledc_mode = to_ledc(mode);

    std::scoped_lock lock(allocation_mutex);

    // Step 1: Find a matching or free timer
    int matched_timer = -1;
    int free_timer = -1;

    for (int t = 0; t < SOC_LEDC_TIMER_NUM; ++t) {
        auto& state = timers[ledc_mode][t];
        std::scoped_lock tmr_lock(state.mutex);
        if (state.resolution_bits == 0) {
            if (free_timer < 0) {
                free_timer = t;
            }
            continue;
        }
        if (ledc_get_freq(ledc_mode, static_cast<ledc_timer_t>(t)) == static_cast<uint32_t>(cfg.frequency.count()) &&
            state.resolution_bits == cfg.resolution_bits &&
            state.clk_source == static_cast<int8_t>(std::to_underlying(cfg.clk_source))) {
            matched_timer = t;
            break;
        }
    }

    unsigned int timer_num;
    if (matched_timer >= 0) {
        timer_num = matched_timer;
        ESP_LOGD(TAG, "Auto-allocate: reusing timer %u", timer_num);
    } else if (free_timer >= 0) {
        timer_num = free_timer;
        timer tmr{timer_num, mode};
        auto r = tmr.try_configure(cfg);
        if (!r) {
            return error(r.error());
        }
        ESP_LOGD(TAG, "Auto-allocate: configured new timer %u", timer_num);
    } else {
        ESP_LOGD(TAG, "Auto-allocate: no free timer available");
        return error(errc::not_found);
    }

    timer tmr{timer_num, mode};

    // Step 2: Find a free channel
    for (int c = 0; c < SOC_LEDC_CHANNEL_NUM; ++c) {
        auto& state = channels[ledc_mode][c];
        std::scoped_lock ch_lock(state.mutex);
        if (!state.active) {
            return output::_make(gpio, tmr, static_cast<enum channel>(c), out_cfg);
        }
    }

    ESP_LOGD(TAG, "Auto-allocate: no free channel available");
    return error(errc::not_found);
}

// requires channel_state for channel to be locked
static result<configure_result>
_configure_channel(enum channel ch, idfxx::gpio gpio, const timer& tmr, const output_config& cfg) {
    if (!tmr.is_configured()) {
        ESP_LOGD(TAG, "Timer %u is not configured", tmr.num());
        return error(errc::invalid_state);
    }
    if (!gpio.is_connected()) {
        ESP_LOGD(TAG, "Argument 'gpio' has an invalid value");
        return error(errc::invalid_arg);
    }

    auto ledc_mode = to_ledc(tmr.speed_mode());
    auto& state = channels[ledc_mode][std::to_underlying(ch)];
    auto ledc_timer = static_cast<ledc_timer_t>(tmr.num());
    auto ledc_ch = to_ledc(ch);
    auto duty = static_cast<uint32_t>(std::clamp(cfg.duty, 0.0f, 1.0f) * static_cast<float>(tmr.ticks_max()));

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    if (state.configured) {
        // Deconfigure the old channel to release GPIO pins and reservations.
        ledc_channel_config_t decfg{};
        decfg.speed_mode = ledc_mode;
        decfg.channel = ledc_ch;
        decfg.deconfigure = true;
        ledc_channel_config(&decfg);
        state.configured = false;
    }
#else
    state.gpio.reset();
#endif

    ledc_channel_config_t ch_cfg{
        .gpio_num = gpio.num(),
        .speed_mode = ledc_mode,
        .channel = ledc_ch,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = ledc_timer,
        .duty = duty,
        .hpoint = cfg.hpoint,
        .sleep_mode = static_cast<ledc_sleep_mode_t>(std::to_underlying(cfg.sleep_mode)),
        .flags = {.output_invert = cfg.output_invert ? 1u : 0u},
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .deconfigure = false,
#endif
    };

    if (auto err = ledc_channel_config(&ch_cfg); err != ESP_OK) {
        ESP_LOGD(
            TAG,
            "Failed to configure LEDC channel %d (Timer: %u, GPIO: %d): %s",
            std::to_underlying(ch),
            tmr.num(),
            gpio.num(),
            esp_err_to_name(err)
        );
        return error(err);
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    state.configured = true;
#endif

    auto gen = ++state.gen;
    state.active = true;
    state.gpio = gpio;
    state.ledc_timer = ledc_timer;
    return configure_result{gen, duty};
}

// requires channel_state for channel to be locked
static result<void> _deconfigure_channel(enum channel ch, enum speed_mode mode, idfxx::gpio::level idle_level) {
    auto ledc_mode = to_ledc(mode);
    auto& state = channels[ledc_mode][std::to_underlying(ch)];
    if (!state.active) {
        return {};
    }
    auto ledc_ch = to_ledc(ch);

    auto err = ledc_stop(ledc_mode, ledc_ch, static_cast<uint32_t>(idle_level));

    state.active = false;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    // On IDF 6.0+, properly deconfigure the channel to release GPIO pins and reservations.
    ledc_channel_config_t decfg{};
    decfg.speed_mode = ledc_mode;
    decfg.channel = ledc_ch;
    decfg.deconfigure = true;
    ledc_channel_config(&decfg);
    state.configured = false;
#else
    state.gpio.reset();
#endif
    state.gpio = idfxx::gpio_nc;

    if (err != ESP_OK) {
        return error(err);
    }
    return {};
}

// requires channel_state for channel to be locked
result<output> output::_make(idfxx::gpio gpio, const class timer& tmr, enum channel ch, const output_config& cfg) {
    auto r = _configure_channel(ch, gpio, tmr, cfg);
    if (!r) {
        return error(r.error());
    }
    auto out = output{tmr, ch, r->gen, r->duty_ticks};
    ESP_LOGD(
        TAG,
        "LEDC output created (GPIO: %d, Timer: %u, Channel: %d, Duty: %.1f%%)",
        gpio.num(),
        tmr.num(),
        std::to_underlying(ch),
        static_cast<double>(cfg.duty * 100.0f)
    );
    return out;
}

output::output(class timer tmr, enum channel ch, uint32_t gen, uint32_t duty_ticks) noexcept
    : _timer(tmr)
    , _channel(ch)
    , _gen(gen)
    , _duty_ticks(duty_ticks) {}

bool is_active(enum channel ch, enum speed_mode mode) {
    auto& state = channels[to_ledc(mode)][std::to_underlying(ch)];
    std::scoped_lock lock(state.mutex);
    return state.active;
}

std::optional<timer> get_timer(enum channel ch, enum speed_mode mode) {
    auto& state = channels[to_ledc(mode)][std::to_underlying(ch)];
    std::scoped_lock lock(state.mutex);
    if (!state.active) {
        return std::nullopt;
    }
    return timer{std::to_underlying(state.ledc_timer), mode};
}

result<void> try_stop(enum channel ch, enum speed_mode mode, idfxx::gpio::level idle_level) {
    auto& state = channels[to_ledc(mode)][std::to_underlying(ch)];
    std::scoped_lock lock(state.mutex);
    return _deconfigure_channel(ch, mode, idle_level);
}

output::output(output&& other) noexcept
    : _timer(other._timer)
    , _channel(other._channel)
    , _gen(std::exchange(other._gen, std::nullopt))
    , _duty_ticks(other._duty_ticks) {}

output& output::operator=(output&& other) noexcept {
    if (this != &other) {
        _delete();
        _timer = other._timer;
        _channel = other._channel;
        _gen = std::exchange(other._gen, std::nullopt);
        _duty_ticks = other._duty_ticks;
    }
    return *this;
}

output::~output() {
    _delete();
}

void output::_delete() noexcept {
    if (_gen) {
        auto& state = channels[to_ledc(_timer.speed_mode())][std::to_underlying(_channel)];
        std::scoped_lock lock(state.mutex);
        if (state.active && _gen == state.gen) {
            _deconfigure_channel(_channel, _timer.speed_mode(), idfxx::gpio::level::low);
        }
        _gen = std::nullopt;
    }
}

bool output::is_active() const noexcept {
    if (!_gen) {
        return false;
    }
    auto& state = channels[to_ledc(_timer.speed_mode())][std::to_underlying(_channel)];
    std::scoped_lock lock(state.mutex);
    return state.active && _gen == state.gen;
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
    return _duty_ticks;
}

result<void> output::try_set_duty(float duty) {
    return try_set_duty_ticks(static_cast<uint32_t>(std::clamp(duty, 0.0f, 1.0f) * static_cast<float>(ticks_max())));
}

result<void> output::try_set_duty_ticks(uint32_t duty) {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    auto ledc_mode = to_ledc(_timer.speed_mode());
    auto ledc_ch = to_ledc(_channel);
    auto r = wrap(ledc_set_duty(ledc_mode, ledc_ch, duty));
    if (!r) {
        return r;
    }
    r = wrap(ledc_update_duty(ledc_mode, ledc_ch));
    if (r) {
        _duty_ticks = duty;
    }
    return r;
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
    auto ledc_mode = to_ledc(_timer.speed_mode());
    auto ledc_ch = to_ledc(_channel);
    auto r = wrap(ledc_set_duty_with_hpoint(ledc_mode, ledc_ch, duty, hpoint));
    if (!r) {
        return r;
    }
    r = wrap(ledc_update_duty(ledc_mode, ledc_ch));
    if (r) {
        _duty_ticks = duty;
    }
    return r;
}

result<void> output::try_stop(idfxx::gpio::level idle_level) {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    return wrap(ledc_stop(to_ledc(_timer.speed_mode()), to_ledc(_channel), static_cast<uint32_t>(idle_level)));
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
    auto r = wrap(ledc_set_fade_time_and_start(
        to_ledc(_timer.speed_mode()),
        to_ledc(_channel),
        target_duty,
        static_cast<uint32_t>(duration.count()),
        static_cast<ledc_fade_mode_t>(std::to_underlying(mode))
    ));
    if (r) {
        _duty_ticks = target_duty;
    }
    return r;
}

result<void> output::try_fade_with_step(uint32_t target_duty, uint32_t scale, uint32_t cycle_num, enum fade_mode mode) {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    auto r = wrap(ledc_set_fade_step_and_start(
        to_ledc(_timer.speed_mode()),
        to_ledc(_channel),
        target_duty,
        scale,
        cycle_num,
        static_cast<ledc_fade_mode_t>(std::to_underlying(mode))
    ));
    if (r) {
        _duty_ticks = target_duty;
    }
    return r;
}

#if SOC_LEDC_SUPPORT_FADE_STOP
result<void> output::try_fade_stop() {
    if (!_gen) {
        return error(errc::invalid_state);
    }
    return wrap(ledc_fade_stop(to_ledc(_timer.speed_mode()), to_ledc(_channel)));
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
