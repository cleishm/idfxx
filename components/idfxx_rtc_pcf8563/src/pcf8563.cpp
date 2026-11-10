// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "idfxx/rtc/pcf8563"

#include "pcf8563_codec.hpp"

#include <array>
#include <esp_log.h>
#include <span>
#include <utility>

namespace {
const char* TAG = "idfxx::rtc::pcf8563";
}

namespace idfxx::rtc {

using std::chrono::sys_days;
using std::chrono::sys_seconds;

// =============================================================================
// Register encoding
// =============================================================================

namespace pcf8563_internal {

namespace {

// Decodes a masked BCD field and checks it lies in [lo, hi].
std::optional<uint8_t> decode_field(uint8_t reg, uint8_t mask, uint8_t lo, uint8_t hi) noexcept {
    auto v = bcd_decode(reg & mask);
    if (!v || *v < lo || *v > hi) {
        return std::nullopt;
    }
    return v;
}

// An alarm field's register value: AE set if @p value is empty, otherwise the
// BCD of a value that must lie in [lo, hi]. invalid_arg if it doesn't.
result<uint8_t>
encode_alarm_field(std::optional<long long> value, long long lo, long long hi) noexcept {
    if (!value) {
        return alarm_disable;
    }
    if (*value < lo || *value > hi) {
        return error(errc::invalid_arg);
    }
    return bcd_encode(static_cast<uint8_t>(*value));
}

// An alarm field's value: std::nullopt if AE disables it, otherwise its masked
// BCD value, which must lie in [lo, hi]. invalid_state if it doesn't.
result<std::optional<uint8_t>>
decode_alarm_field(uint8_t reg, uint8_t mask, uint8_t lo, uint8_t hi) noexcept {
    if ((reg & alarm_disable) != 0) {
        return std::optional<uint8_t>{};
    }
    const auto v = decode_field(reg, mask, lo, hi);
    if (!v) {
        return error(errc::invalid_state);
    }
    return v;
}

} // namespace

result<time_registers> encode_time(sys_seconds t) {
    if (t < min_time || t > max_time) {
        return error(errc::invalid_arg);
    }
    const auto days = std::chrono::floor<std::chrono::days>(t);
    const std::chrono::year_month_day ymd{days};
    const std::chrono::hh_mm_ss hms{t - days};
    const std::chrono::weekday wd{days};

    return time_registers{
        bcd_encode(static_cast<uint8_t>(hms.seconds().count())), // VL = 0
        bcd_encode(static_cast<uint8_t>(hms.minutes().count())),
        bcd_encode(static_cast<uint8_t>(hms.hours().count())),
        bcd_encode(static_cast<uint8_t>(static_cast<unsigned>(ymd.day()))),
        static_cast<uint8_t>(wd.c_encoding()),
        bcd_encode(static_cast<uint8_t>(static_cast<unsigned>(ymd.month()))), // C = 0
        bcd_encode(static_cast<uint8_t>(static_cast<int>(ymd.year()) - 2000)),
    };
}

std::optional<sys_seconds> decode_time(std::span<const uint8_t, time_size> regs) {
    if ((regs[0] & seconds_vl) != 0 || (regs[5] & months_century) != 0) {
        return std::nullopt;
    }
    const auto sec = decode_field(regs[0], seconds_mask, 0, 59);
    const auto min = decode_field(regs[1], minutes_mask, 0, 59);
    const auto hour = decode_field(regs[2], hours_mask, 0, 23);
    const auto day = decode_field(regs[3], days_mask, 1, 31);
    const auto month = decode_field(regs[5], months_mask, 1, 12);
    const auto year = decode_field(regs[6], years_mask, 0, 99);
    if (!sec || !min || !hour || !day || !month || !year) {
        return std::nullopt;
    }
    const std::chrono::year_month_day ymd{
        std::chrono::year{2000 + *year}, std::chrono::month{*month}, std::chrono::day{*day}
    };
    if (!ymd.ok()) {
        return std::nullopt;
    }
    return sys_days{ymd} + std::chrono::hours{*hour} + std::chrono::minutes{*min} +
        std::chrono::seconds{*sec};
}

std::optional<sys_seconds> decode_clock(const clock_registers& regs) {
    if ((regs[reg_control1] & control1_stop) != 0) {
        return std::nullopt;
    }
    return decode_time(std::span{regs}.subspan<reg_seconds, time_size>());
}

result<alarm_registers> encode_alarm(const pcf8563::alarm_config& alarm) {
    const auto minute =
        encode_alarm_field(alarm.minute.transform([](auto m) { return m.count(); }), 0, 59);
    const auto hour =
        encode_alarm_field(alarm.hour.transform([](auto h) { return h.count(); }), 0, 23);
    const auto day = encode_alarm_field(
        alarm.day.transform([](auto d) {
            return static_cast<long long>(static_cast<unsigned>(d));
        }),
        1,
        31
    );
    const auto weekday = encode_alarm_field(
        alarm.weekday.transform([](auto wd) { return static_cast<long long>(wd.c_encoding()); }),
        0,
        6
    );
    if (!minute || !hour || !day || !weekday) {
        return error(errc::invalid_arg);
    }
    return alarm_registers{*minute, *hour, *day, *weekday};
}

result<pcf8563::alarm_config> decode_alarm(const alarm_registers& regs, uint8_t control2) {
    const auto minute = decode_alarm_field(regs[0], minute_alarm_mask, 0, 59);
    const auto hour = decode_alarm_field(regs[1], hour_alarm_mask, 0, 23);
    const auto day = decode_alarm_field(regs[2], day_alarm_mask, 1, 31);
    // The weekday is a plain 0-6 value, which is also its BCD encoding.
    const auto weekday = decode_alarm_field(regs[3], weekday_alarm_mask, 0, 6);
    if (!minute || !hour || !day || !weekday) {
        return error(errc::invalid_state);
    }
    return pcf8563::alarm_config{
        .minute = minute->transform([](uint8_t m) { return std::chrono::minutes{m}; }),
        .hour = hour->transform([](uint8_t h) { return std::chrono::hours{h}; }),
        .day = day->transform([](uint8_t d) { return std::chrono::day{d}; }),
        .weekday = weekday->transform([](uint8_t wd) { return std::chrono::weekday{wd}; }),
        .interrupt = (control2 & control2_aie) != 0,
    };
}

result<timer_setting> select_timer_source(pcf8563::timer_duration period) {
    using enum pcf8563::timer_source;
    for (const auto source : {hz_4096, hz_64, hz_1, hz_1_60}) {
        const auto tick = timer_tick(source).count();
        const auto ticks = period.count();
        if (ticks % tick == 0 && ticks / tick >= 1 && ticks / tick <= 255) {
            return timer_setting{.source = source, .count = static_cast<uint8_t>(ticks / tick)};
        }
    }
    return error(errc::invalid_arg);
}

} // namespace pcf8563_internal

// =============================================================================
// Driver state and register access
// =============================================================================

namespace {

// Attempts at two consecutive matching reads of the live countdown value.
constexpr int timer_read_attempts = 4;

// Whether an enumerator lies within its enumeration's values, which run from 0 to @p last.
template<typename E>
constexpr bool valid(E v, E last) noexcept {
    return std::to_underlying(v) <= std::to_underlying(last);
}

} // namespace

struct pcf8563::state {
    i2c::master_device dev;

    // The one place that writes Control_status_2. Replaces the interrupt bits
    // selected by @p mask with @p bits, keeps the others as read, and clears
    // only the flags named in @p clear: AF and TF are otherwise written as 1,
    // which leaves them unchanged. Writes nothing if the interrupt bits would
    // not change and no flag to clear is set.
    [[nodiscard]] result<void>
    modify_control2(uint8_t mask, uint8_t bits, pcf8563_internal::control2_clear clear) {
        auto current = dev.try_read_register(pcf8563_internal::reg_control2);
        if (!current) {
            return error(current.error());
        }
        const auto interrupts = static_cast<uint8_t>((*current & ~mask) | (bits & mask));
        const bool interrupts_change = (interrupts & pcf8563_internal::control2_interrupt_mask) !=
            (*current & pcf8563_internal::control2_interrupt_mask);
        const bool flag_to_clear =
            (clear.alarm_flag && (*current & pcf8563_internal::control2_af) != 0) ||
            (clear.timer_flag && (*current & pcf8563_internal::control2_tf) != 0);
        if (!interrupts_change && !flag_to_clear) {
            return {};
        }
        return dev.try_write_register(
            pcf8563_internal::reg_control2, pcf8563_internal::make_control2(interrupts, clear)
        );
    }

    // Sets or clears STOP in Control_status_1, writing nothing if the register
    // would not change.
    [[nodiscard]] result<void> set_stop(bool stop) {
        auto current = dev.try_read_register(pcf8563_internal::reg_control1);
        if (!current) {
            return error(current.error());
        }
        const auto control1 = pcf8563_internal::make_control1(*current, stop);
        if (control1 == *current) {
            return {};
        }
        return dev.try_write_register(pcf8563_internal::reg_control1, control1);
    }
};

// =============================================================================
// Construction
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
pcf8563::pcf8563(i2c::master_bus& bus)
    : pcf8563(unwrap(make(bus))) {}

pcf8563::pcf8563(i2c::master_bus& bus, const config& cfg)
    : pcf8563(unwrap(make(bus, cfg))) {}
#endif

pcf8563::pcf8563(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

result<pcf8563> pcf8563::make(i2c::master_bus& bus) {
    return make(bus, config{});
}

result<pcf8563> pcf8563::make(i2c::master_bus& bus, const config& cfg) {
    auto dev = i2c::master_device::make(bus, cfg.address, {.scl_speed = cfg.scl_speed});
    if (!dev) {
        return error(dev.error());
    }

    // Check the clock before anything is sent to the chip.
    if (dev->scl_speed() > pcf8563_internal::max_scl_speed) {
        ESP_LOGE(TAG, "I2C clock must be at most 400 kHz");
        return error(errc::invalid_arg);
    }

    // Probe the address; nothing is written to the chip.
    if (auto r = dev->try_probe(); !r) {
        if (r.error() == errc::not_found) {
            ESP_LOGE(TAG, "no PCF8563 responding at address 0x%02X", cfg.address);
        }
        return error(r.error());
    }

    return pcf8563{std::make_unique<state>(std::move(*dev))};
}

pcf8563::~pcf8563() = default;
pcf8563::pcf8563(pcf8563&& other) noexcept = default;
pcf8563& pcf8563::operator=(pcf8563&& other) noexcept = default;

// =============================================================================
// Time
// =============================================================================

result<std::optional<sys_seconds>> pcf8563::do_now() {
    // The control and time registers in one burst, so they are consistent.
    pcf8563_internal::clock_registers regs{};
    if (auto r = _state->dev.try_read_register(pcf8563_internal::reg_control1, regs); !r) {
        return error(r.error());
    }
    return pcf8563_internal::decode_clock(regs);
}

result<void> pcf8563::do_set_time(sys_seconds t) {
    auto regs = pcf8563_internal::encode_time(t);
    if (!regs) {
        ESP_LOGD(TAG, "time outside 2000-01-01T00:00:00..2099-12-31T23:59:59");
        return error(regs.error());
    }
    // One burst, which also clears VL in the seconds register.
    if (auto r = _state->dev.try_write_register(pcf8563_internal::reg_seconds, *regs); !r) {
        return error(r.error());
    }
    // Then restart a stopped clock, so the time is valid afterward. Only
    // Control_status_1 is touched, and only written if it changes: STOP is
    // cleared, TESTC kept. A clock stopped beforehand starts counting from
    // here, so stop() then set_time() sets the time on a precise second.
    return _state->set_stop(false);
}

// =============================================================================
// Oscillator
// =============================================================================

result<void> pcf8563::try_start() {
    return _state->set_stop(false);
}

result<void> pcf8563::try_stop() {
    return _state->set_stop(true);
}

result<bool> pcf8563::try_running() {
    return _state->dev.try_read_register(pcf8563_internal::reg_control1)
        .transform([](uint8_t control1) {
            return (control1 & pcf8563_internal::control1_stop) == 0;
        });
}

// =============================================================================
// Alarm
// =============================================================================

result<void> pcf8563::try_set_alarm(const alarm_config& alarm) {
    auto regs = pcf8563_internal::encode_alarm(alarm);
    if (!regs) {
        return error(regs.error());
    }
    // The alarm fields first, in one burst, then the interrupt enable.
    if (auto r = _state->dev.try_write_register(pcf8563_internal::reg_minute_alarm, *regs); !r) {
        return r;
    }
    return _state->modify_control2(
        pcf8563_internal::control2_aie,
        alarm.interrupt ? pcf8563_internal::control2_aie : 0,
        {.alarm_flag = false, .timer_flag = false}
    );
}

result<pcf8563::alarm_config> pcf8563::try_alarm() {
    pcf8563_internal::alarm_registers regs{};
    if (auto r = _state->dev.try_read_register(pcf8563_internal::reg_minute_alarm, regs); !r) {
        return error(r.error());
    }
    auto control2 = _state->dev.try_read_register(pcf8563_internal::reg_control2);
    if (!control2) {
        return error(control2.error());
    }
    return pcf8563_internal::decode_alarm(regs, *control2);
}

// =============================================================================
// Countdown timer
// =============================================================================

result<void> pcf8563::try_start_timer(timer_source source, uint8_t count) {
    return try_start_timer(source, count, timer_config{});
}

result<void> pcf8563::try_start_timer(timer_source source, uint8_t count, const timer_config& cfg) {
    if (count == 0 || !valid(source, timer_source::hz_1_60) ||
        !valid(cfg.interrupt, timer_interrupt::pulse)) {
        return error(errc::invalid_arg);
    }
    // Disable the timer with its new source and count in one burst (0Eh then
    // 0Fh), set the interrupt mode, then enable it.
    const std::array<uint8_t, 2> disabled{
        pcf8563_internal::make_timer_control(false, source), count
    };
    if (auto r = _state->dev.try_write_register(pcf8563_internal::reg_timer_control, disabled);
        !r) {
        return r;
    }
    if (auto r = _state->modify_control2(
            pcf8563_internal::control2_tie | pcf8563_internal::control2_ti_tp,
            pcf8563_internal::timer_interrupt_bits(cfg.interrupt),
            {.alarm_flag = false, .timer_flag = false}
        );
        !r) {
        return r;
    }
    return _state->dev.try_write_register(
        pcf8563_internal::reg_timer_control, pcf8563_internal::make_timer_control(true, source)
    );
}

result<void> pcf8563::_try_start_timer(timer_duration period, const timer_config& cfg) {
    auto setting = pcf8563_internal::select_timer_source(period);
    if (!setting) {
        return error(setting.error());
    }
    return try_start_timer(setting->source, setting->count, cfg);
}

result<pcf8563::timer_duration> pcf8563::try_timer_remaining() {
    // Timer_control and the countdown value in one burst. The countdown runs
    // while it is read, so accept it once two consecutive reads agree.
    std::array<uint8_t, 2> previous{};
    for (int attempt = 0; attempt < timer_read_attempts; ++attempt) {
        std::array<uint8_t, 2> current{};
        if (auto r = _state->dev.try_read_register(pcf8563_internal::reg_timer_control, current);
            !r) {
            return error(r.error());
        }
        if (attempt > 0 && current == previous) {
            break;
        }
        previous = current;
    }
    return pcf8563_internal::timer_remaining(previous[0], previous[1]);
}

result<void> pcf8563::try_stop_timer() {
    if (auto r = _state->dev.try_write_register(
            pcf8563_internal::reg_timer_control, pcf8563_internal::timer_td_low_power
        );
        !r) {
        return r;
    }
    return _state->modify_control2(
        pcf8563_internal::control2_tie, 0, {.alarm_flag = false, .timer_flag = false}
    );
}

// =============================================================================
// Events
// =============================================================================

result<flags<pcf8563::event>> pcf8563::try_events() {
    return _state->dev.try_read_register(pcf8563_internal::reg_control2)
        .transform([](uint8_t control2) {
            flags<event> events;
            if ((control2 & pcf8563_internal::control2_af) != 0) {
                events |= event::alarm;
            }
            if ((control2 & pcf8563_internal::control2_tf) != 0) {
                events |= event::timer;
            }
            return events;
        });
}

result<void> pcf8563::try_clear_events(flags<event> events) {
    return _state->modify_control2(
        0,
        0,
        {.alarm_flag = events.contains(event::alarm), .timer_flag = events.contains(event::timer)}
    );
}

// =============================================================================
// Clock output
// =============================================================================

result<void> pcf8563::try_set_clkout(std::optional<clkout_frequency> frequency) {
    if (frequency && !valid(*frequency, clkout_frequency::hz_1)) {
        return error(errc::invalid_arg);
    }
    return _state->dev.try_write_register(
        pcf8563_internal::reg_clkout, pcf8563_internal::encode_clkout(frequency)
    );
}

result<std::optional<pcf8563::clkout_frequency>> pcf8563::try_clkout() {
    return _state->dev.try_read_register(pcf8563_internal::reg_clkout)
        .transform(pcf8563_internal::decode_clkout);
}

} // namespace idfxx::rtc
