// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal: the PCF8563 register layout and the pure encoding and decoding of
// register contents. Kept free of I2C access so it can be unit-tested (in QEMU)
// without a chip.

#include <idfxx/error>
#include <idfxx/rtc/pcf8563>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <quanta/frequency>
#include <span>

namespace idfxx::rtc::pcf8563_internal {

// Register addresses (datasheet Table 4).
inline constexpr uint8_t reg_control1 = 0x00;
inline constexpr uint8_t reg_control2 = 0x01;
inline constexpr uint8_t reg_seconds = 0x02;
inline constexpr uint8_t reg_minutes = 0x03;
inline constexpr uint8_t reg_hours = 0x04;
inline constexpr uint8_t reg_days = 0x05;
inline constexpr uint8_t reg_weekdays = 0x06;
inline constexpr uint8_t reg_months = 0x07;
inline constexpr uint8_t reg_years = 0x08;
inline constexpr uint8_t reg_minute_alarm = 0x09;
inline constexpr uint8_t reg_hour_alarm = 0x0A;
inline constexpr uint8_t reg_day_alarm = 0x0B;
inline constexpr uint8_t reg_weekday_alarm = 0x0C;
inline constexpr uint8_t reg_clkout = 0x0D;
inline constexpr uint8_t reg_timer_control = 0x0E;
inline constexpr uint8_t reg_timer = 0x0F;

// Control_status_1 bits. All others read as unused and are written as 0.
inline constexpr uint8_t control1_test1 = 0x80;
inline constexpr uint8_t control1_stop = 0x20;
inline constexpr uint8_t control1_testc = 0x08;

// Control_status_2 bits. All others are unused and are written as 0. Writing
// 0 to AF or TF clears the flag; writing 1 leaves it unchanged.
inline constexpr uint8_t control2_ti_tp = 0x10;
inline constexpr uint8_t control2_af = 0x08;
inline constexpr uint8_t control2_tf = 0x04;
inline constexpr uint8_t control2_aie = 0x02;
inline constexpr uint8_t control2_tie = 0x01;
inline constexpr uint8_t control2_interrupt_mask = control2_ti_tp | control2_aie | control2_tie;

// Time register fields (02h–08h).
inline constexpr uint8_t seconds_vl = 0x80;
inline constexpr uint8_t seconds_mask = 0x7F;
inline constexpr uint8_t minutes_mask = 0x7F;
inline constexpr uint8_t hours_mask = 0x3F;
inline constexpr uint8_t days_mask = 0x3F;
inline constexpr uint8_t months_century = 0x80;
inline constexpr uint8_t months_mask = 0x1F;
inline constexpr uint8_t years_mask = 0xFF;

// Alarm register fields (09h–0Ch). AE = 1 disables the field.
inline constexpr uint8_t alarm_disable = 0x80;
inline constexpr uint8_t minute_alarm_mask = 0x7F;
inline constexpr uint8_t hour_alarm_mask = 0x3F;
inline constexpr uint8_t day_alarm_mask = 0x3F;
inline constexpr uint8_t weekday_alarm_mask = 0x07;

// CLKOUT_control fields (0Dh).
inline constexpr uint8_t clkout_fe = 0x80;
inline constexpr uint8_t clkout_fd_mask = 0x03;

// Timer_control fields (0Eh). TD = 11 is the low-power setting for an unused timer.
inline constexpr uint8_t timer_te = 0x80;
inline constexpr uint8_t timer_td_mask = 0x03;
inline constexpr uint8_t timer_td_low_power = 0x03;

// Register block sizes.
inline constexpr size_t time_size = 7;
inline constexpr size_t clock_size = 2 + time_size;
inline constexpr size_t alarm_size = 4;

// Maximum I2C clock (datasheet fSCL).
inline constexpr quanta::frequency::hertz max_scl_speed{400'000};

// The range of times the chip holds (century bit always 0).
inline constexpr std::chrono::sys_seconds min_time{std::chrono::sys_days{std::chrono::year{2000} / 1 / 1}};
inline constexpr std::chrono::sys_seconds max_time{
    std::chrono::sys_days{std::chrono::year{2099} / 12 / 31} + std::chrono::hours{23} + std::chrono::minutes{59} +
    std::chrono::seconds{59}
};

// The time registers 02h–08h, in address order.
using time_registers = std::array<uint8_t, time_size>;

// The control and time registers 00h–08h, in address order: Control_status_1,
// Control_status_2, then the time registers.
using clock_registers = std::array<uint8_t, clock_size>;

// The alarm registers 09h–0Ch, in address order.
using alarm_registers = std::array<uint8_t, alarm_size>;

// Flags to clear in a Control_status_2 write.
struct control2_clear {
    bool alarm_flag; // Clear AF.
    bool timer_flag; // Clear TF.
};

// A countdown timer source and count.
struct timer_setting {
    pcf8563::timer_source source;
    uint8_t count;
};

// BCD encoding of a value 0–99.
[[nodiscard]] constexpr uint8_t bcd_encode(uint8_t value) noexcept {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

// Decodes a BCD byte, or std::nullopt if either nibble is above 9.
[[nodiscard]] constexpr std::optional<uint8_t> bcd_decode(uint8_t bcd) noexcept {
    const uint8_t tens = bcd >> 4;
    const uint8_t units = bcd & 0x0F;
    if (tens > 9 || units > 9) {
        return std::nullopt;
    }
    return static_cast<uint8_t>(tens * 10 + units);
}

// Control_status_1 for a STOP setting: preserves TESTC from the current value,
// writes TEST1 and the unused bits as 0.
[[nodiscard]] constexpr uint8_t make_control1(uint8_t current, bool stop) noexcept {
    return static_cast<uint8_t>((current & control1_testc) | (stop ? control1_stop : 0));
}

// Control_status_2 with the given interrupt bits (TI_TP, AIE, TIE): AF and TF
// are written as 1 (unchanged) unless cleared, and the unused bits as 0.
[[nodiscard]] constexpr uint8_t make_control2(uint8_t interrupt_bits, control2_clear clear) noexcept {
    return static_cast<uint8_t>(
        (interrupt_bits & control2_interrupt_mask) | (clear.alarm_flag ? 0 : control2_af) |
        (clear.timer_flag ? 0 : control2_tf)
    );
}

// Control_status_2 interrupt bits (TIE, TI_TP) for a timer interrupt mode.
[[nodiscard]] constexpr uint8_t timer_interrupt_bits(pcf8563::timer_interrupt mode) noexcept {
    switch (mode) {
    case pcf8563::timer_interrupt::level:
        return control2_tie;
    case pcf8563::timer_interrupt::pulse:
        return control2_tie | control2_ti_tp;
    default:
        return 0;
    }
}

// Timer_control for a source, with the timer enabled or disabled.
[[nodiscard]] constexpr uint8_t make_timer_control(bool enable, pcf8563::timer_source source) noexcept {
    return static_cast<uint8_t>((enable ? timer_te : 0) | (std::to_underlying(source) & timer_td_mask));
}

// Duration of one tick of a timer source.
[[nodiscard]] constexpr pcf8563::timer_duration timer_tick(pcf8563::timer_source source) noexcept {
    switch (source) {
    case pcf8563::timer_source::hz_4096:
        return pcf8563::timer_duration{1};
    case pcf8563::timer_source::hz_64:
        return pcf8563::timer_duration{64};
    case pcf8563::timer_source::hz_1:
        return pcf8563::timer_duration{4096};
    default:
        return pcf8563::timer_duration{4096 * 60};
    }
}

// Time left in the countdown, from Timer_control and the countdown value:
// zero if the timer is disabled.
[[nodiscard]] constexpr pcf8563::timer_duration timer_remaining(uint8_t timer_control, uint8_t count) noexcept {
    if ((timer_control & timer_te) == 0) {
        return pcf8563::timer_duration::zero();
    }
    const auto source = static_cast<pcf8563::timer_source>(timer_control & timer_td_mask);
    return timer_tick(source) * count;
}

// CLKOUT_control for a frequency, or for the output off.
[[nodiscard]] constexpr uint8_t encode_clkout(std::optional<pcf8563::clkout_frequency> frequency) noexcept {
    if (!frequency) {
        return 0;
    }
    return static_cast<uint8_t>(clkout_fe | (std::to_underlying(*frequency) & clkout_fd_mask));
}

// The frequency set in CLKOUT_control, or std::nullopt if the output is off.
[[nodiscard]] constexpr std::optional<pcf8563::clkout_frequency> decode_clkout(uint8_t clkout_control) noexcept {
    if ((clkout_control & clkout_fe) == 0) {
        return std::nullopt;
    }
    return static_cast<pcf8563::clkout_frequency>(clkout_control & clkout_fd_mask);
}

// Encodes a time into the time registers, with VL and the century bit 0 and
// the weekday computed from the date. invalid_arg outside [min_time, max_time].
[[nodiscard]] result<time_registers> encode_time(std::chrono::sys_seconds t);

// Decodes the time registers, ignoring the weekday and unused bits.
// std::nullopt if the time is not valid: VL or the century bit is set, a BCD
// nibble is above 9, or the date or time is impossible.
[[nodiscard]] std::optional<std::chrono::sys_seconds> decode_time(std::span<const uint8_t, time_size> regs);

// Decodes the control and time registers: std::nullopt if STOP is set, or
// for any reason decode_time() gives.
[[nodiscard]] std::optional<std::chrono::sys_seconds> decode_clock(const clock_registers& regs);

// Encodes alarm settings into the alarm registers. invalid_arg if a field is
// out of range.
[[nodiscard]] result<alarm_registers> encode_alarm(const pcf8563::alarm_config& alarm);

// Decodes the alarm registers, with the interrupt enable taken from
// Control_status_2. invalid_state if an enabled field holds an impossible value.
[[nodiscard]] result<pcf8563::alarm_config> decode_alarm(const alarm_registers& regs, uint8_t control2);

// The fastest source that produces a period, given as an exact number of
// 1/4096 s ticks, as a count of 1–255 source ticks. invalid_arg if there is none.
[[nodiscard]] result<timer_setting> select_timer_source(pcf8563::timer_duration period);

} // namespace idfxx::rtc::pcf8563_internal
