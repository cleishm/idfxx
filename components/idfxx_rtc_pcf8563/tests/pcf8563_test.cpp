// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_rtc_pcf8563
// Uses ESP-IDF Unity test framework with compile-time static_asserts.
// QEMU has no PCF8563, so most of these cover the register encoding only; the
// [hw] tests need a PCF8563 on TEST_SDA/TEST_SCL and are skipped if none responds.

#include "../src/pcf8563_codec.hpp"
#include "idfxx/error"
#include "idfxx/i2c/master"
#include "idfxx/rtc/clock"
#include "idfxx/rtc/pcf8563"
#include "unity.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#ifdef CONFIG_IDFXX_STD_FORMAT
#include <format>
#endif

// `clock` is also the C library's ::clock(), so the rtc namespace is never imported whole.
using idfxx::rtc::pcf8563;
namespace pcf8563_detail = idfxx::rtc::pcf8563_detail;
namespace pcf8563_internal = idfxx::rtc::pcf8563_internal;
using namespace std::chrono_literals;
using namespace quanta::frequency_literals;
using std::chrono::sys_days;
using std::chrono::sys_seconds;

namespace {

// SDA/SCL pins of the PCF8563 used by the [hw] tests.
#ifdef CONFIG_IDF_TARGET_ESP32C2
inline constexpr auto TEST_SDA = idfxx::gpio_4;
#else
inline constexpr auto TEST_SDA = idfxx::gpio_21;
#endif
inline constexpr auto TEST_SCL = idfxx::gpio_9;

using timer_duration = pcf8563::timer_duration;
using timer_source = pcf8563::timer_source;
using clkout_frequency = pcf8563::clkout_frequency;

constexpr sys_seconds make_time(int y, unsigned mo, unsigned d, int h, int mi, int s) {
    return sys_days{std::chrono::year{y} / std::chrono::month{mo} / std::chrono::day{d}} + std::chrono::hours{h}
        + std::chrono::minutes{mi} + std::chrono::seconds{s};
}

// 2026-11-09T12:34:56, a Monday
constexpr sys_seconds sample_time = make_time(2026, 11, 9, 12, 34, 56);
constexpr pcf8563_internal::time_registers sample_block{0x56, 0x34, 0x12, 0x09, 0x01, 0x11, 0x26};

bool is_error(const auto& r, idfxx::errc e) {
    return !r.has_value() && r.error() == e;
}

void assert_round_trip(sys_seconds t) {
    auto regs = pcf8563_internal::encode_time(t);
    TEST_ASSERT_TRUE(regs.has_value());
    auto back = pcf8563_internal::decode_time(*regs);
    TEST_ASSERT_TRUE(back.has_value());
    TEST_ASSERT_TRUE(*back == t);
}

// Converts a period and selects its source, as try_start_timer() does.
template<typename Rep, typename Period>
idfxx::result<pcf8563_internal::timer_setting> select_timer(const std::chrono::duration<Rep, Period>& period) {
    const auto ticks = pcf8563_detail::timer_ticks(period);
    if (!ticks) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    return pcf8563_internal::select_timer_source(timer_duration{*ticks});
}

template<typename Rep, typename Period>
void assert_timer_setting(const std::chrono::duration<Rep, Period>& period, timer_source source, int count) {
    auto s = select_timer(period);
    TEST_ASSERT_TRUE(s.has_value());
    TEST_ASSERT_EQUAL_INT32(std::to_underlying(source), std::to_underlying(s->source));
    TEST_ASSERT_EQUAL_INT32(count, s->count);
}

template<typename Rep, typename Period>
void assert_timer_rejected(const std::chrono::duration<Rep, Period>& period) {
    TEST_ASSERT_TRUE(is_error(select_timer(period), idfxx::errc::invalid_arg));
}

// A clock register block (00h–08h) with the given Control_status_1 and the sample time.
constexpr pcf8563_internal::clock_registers make_clock_block(uint8_t control1) {
    pcf8563_internal::clock_registers regs{control1, 0x00};
    for (size_t i = 0; i < sample_block.size(); ++i) {
        regs[2 + i] = sample_block[i];
    }
    return regs;
}

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// pcf8563 is a final, move-only rtc::clock
static_assert(std::is_base_of_v<idfxx::rtc::clock, pcf8563>);
static_assert(std::is_final_v<pcf8563>);
static_assert(!std::is_abstract_v<pcf8563>);
static_assert(!std::is_copy_constructible_v<pcf8563>);
static_assert(!std::is_copy_assignable_v<pcf8563>);
static_assert(std::is_move_constructible_v<pcf8563>);
static_assert(std::is_move_assignable_v<pcf8563>);
static_assert(!std::is_default_constructible_v<pcf8563>);

// Configuration defaults
static_assert(pcf8563::config{}.address == 0x51);
static_assert(!pcf8563::config{}.scl_speed.has_value());
static_assert(pcf8563::timer_config{}.interrupt == pcf8563::timer_interrupt::none);
static_assert(!pcf8563::alarm_config{}.minute && !pcf8563::alarm_config{}.hour);
static_assert(!pcf8563::alarm_config{}.day && !pcf8563::alarm_config{}.weekday);
static_assert(!pcf8563::alarm_config{}.interrupt);

// timer_source values match the TD field; clkout_frequency values match the FD field
static_assert(std::to_underlying(timer_source::hz_4096) == 0b00);
static_assert(std::to_underlying(timer_source::hz_64) == 0b01);
static_assert(std::to_underlying(timer_source::hz_1) == 0b10);
static_assert(std::to_underlying(timer_source::hz_1_60) == 0b11);
static_assert(std::to_underlying(clkout_frequency::hz_32768) == 0b00);
static_assert(std::to_underlying(clkout_frequency::hz_1024) == 0b01);
static_assert(std::to_underlying(clkout_frequency::hz_32) == 0b10);
static_assert(std::to_underlying(clkout_frequency::hz_1) == 0b11);

// Register addresses (datasheet Table 4)
static_assert(pcf8563_internal::reg_control1 == 0x00);
static_assert(pcf8563_internal::reg_control2 == 0x01);
static_assert(pcf8563_internal::reg_seconds == 0x02);
static_assert(pcf8563_internal::reg_minutes == 0x03);
static_assert(pcf8563_internal::reg_hours == 0x04);
static_assert(pcf8563_internal::reg_days == 0x05);
static_assert(pcf8563_internal::reg_weekdays == 0x06);
static_assert(pcf8563_internal::reg_months == 0x07);
static_assert(pcf8563_internal::reg_years == 0x08);
static_assert(pcf8563_internal::reg_minute_alarm == 0x09);
static_assert(pcf8563_internal::reg_hour_alarm == 0x0A);
static_assert(pcf8563_internal::reg_day_alarm == 0x0B);
static_assert(pcf8563_internal::reg_weekday_alarm == 0x0C);
static_assert(pcf8563_internal::reg_clkout == 0x0D);
static_assert(pcf8563_internal::reg_timer_control == 0x0E);
static_assert(pcf8563_internal::reg_timer == 0x0F);
static_assert(pcf8563_internal::reg_seconds + pcf8563_internal::time_size == pcf8563_internal::reg_minute_alarm);
static_assert(pcf8563_internal::reg_control1 + pcf8563_internal::clock_size == pcf8563_internal::reg_minute_alarm);
static_assert(pcf8563_internal::reg_minute_alarm + pcf8563_internal::alarm_size == pcf8563_internal::reg_clkout);

// Control and field bits
static_assert(pcf8563_internal::control1_test1 == 0x80);
static_assert(pcf8563_internal::control1_stop == 0x20);
static_assert(pcf8563_internal::control1_testc == 0x08);
static_assert(pcf8563_internal::control2_ti_tp == 0x10);
static_assert(pcf8563_internal::control2_af == 0x08);
static_assert(pcf8563_internal::control2_tf == 0x04);
static_assert(pcf8563_internal::control2_aie == 0x02);
static_assert(pcf8563_internal::control2_tie == 0x01);
static_assert(pcf8563_internal::seconds_vl == 0x80);
static_assert(pcf8563_internal::months_century == 0x80);
static_assert(pcf8563_internal::alarm_disable == 0x80);
static_assert(pcf8563_internal::clkout_fe == 0x80);
static_assert(pcf8563_internal::timer_te == 0x80);
static_assert(pcf8563_internal::timer_td_low_power == 0x03);
static_assert(pcf8563_internal::max_scl_speed.count() == 400'000);

// The supported time range
static_assert(pcf8563_internal::min_time == make_time(2000, 1, 1, 0, 0, 0));
static_assert(pcf8563_internal::max_time == make_time(2099, 12, 31, 23, 59, 59));

// timer_duration holds every timer period exactly
static_assert(std::is_same_v<timer_duration::rep, uint32_t>);
static_assert(std::is_same_v<timer_duration::period, std::ratio<1, 4096>>);
static_assert(timer_duration{30s}.count() == 122'880);
static_assert(timer_duration{255min}.count() == 62'668'800);
static_assert(pcf8563_detail::max_timer_ticks == timer_duration{255min}.count());

// Periods convert to an exact tick count, or not at all
static_assert(pcf8563_detail::timer_ticks(30s) == 122'880u);
static_assert(pcf8563_detail::timer_ticks(250ms) == 1024u);
static_assert(pcf8563_detail::timer_ticks(203'125us) == 832u);
static_assert(pcf8563_detail::timer_ticks(255min) == 62'668'800u);
static_assert(pcf8563_detail::timer_ticks(timer_duration{1}) == 1u);
static_assert(!pcf8563_detail::timer_ticks(100ms));
static_assert(!pcf8563_detail::timer_ticks(256min));
static_assert(!pcf8563_detail::timer_ticks(1'048'606s)); // 2^32 / 4096 + 30 s: wraps to 30 s in uint32
static_assert(!pcf8563_detail::timer_ticks(0s));
static_assert(!pcf8563_detail::timer_ticks(-1s));
static_assert(!pcf8563_detail::timer_ticks(std::chrono::hours::max()));
static_assert(!pcf8563_detail::timer_ticks(-std::chrono::hours::max()));
static_assert(!pcf8563_detail::timer_ticks(std::chrono::nanoseconds::max()));
static_assert(!pcf8563_detail::timer_ticks(std::chrono::duration<int64_t, std::exa>{1}));
static_assert(!pcf8563_detail::timer_ticks(std::chrono::duration<int64_t, std::atto>{1}));
static_assert(pcf8563_detail::timer_ticks(std::chrono::duration<int64_t, std::atto>{244'140'625'000'000}) == 1u);
static_assert(!pcf8563_detail::timer_ticks(std::chrono::duration<int16_t, std::nano>{32767}));
static_assert(pcf8563_detail::timer_ticks(std::chrono::duration<int16_t, std::milli>{250}) == 1024u);
static_assert(pcf8563_detail::timer_ticks(std::chrono::duration<uint8_t, std::ratio<60>>{255}) == 62'668'800u);

// Periods must have an integer rep
template<typename D>
concept timer_period = requires(pcf8563& rtc, D d) { rtc.try_start_timer(d); };
static_assert(timer_period<std::chrono::seconds>);
static_assert(timer_period<std::chrono::duration<uint8_t, std::milli>>);
static_assert(!timer_period<std::chrono::duration<double>>);
static_assert(!timer_period<std::chrono::duration<float, std::milli>>);
static_assert(pcf8563_internal::timer_tick(timer_source::hz_4096) == timer_duration{1});
static_assert(pcf8563_internal::timer_tick(timer_source::hz_64) == timer_duration{64});
static_assert(pcf8563_internal::timer_tick(timer_source::hz_1) == 1s);
static_assert(pcf8563_internal::timer_tick(timer_source::hz_1_60) == 1min);

// BCD
static_assert(pcf8563_internal::bcd_encode(0) == 0x00);
static_assert(pcf8563_internal::bcd_encode(59) == 0x59);
static_assert(pcf8563_internal::bcd_encode(99) == 0x99);
static_assert(pcf8563_internal::bcd_decode(0x42) == 42);
static_assert(!pcf8563_internal::bcd_decode(0x4A));
static_assert(!pcf8563_internal::bcd_decode(0xA4));

// Control_status_1 preserves TESTC and writes TEST1 and the unused bits as 0
static_assert(pcf8563_internal::make_control1(0xFF, false) == 0x08);
static_assert(pcf8563_internal::make_control1(0xFF, true) == 0x28);
static_assert(pcf8563_internal::make_control1(0x00, true) == 0x20);
static_assert(pcf8563_internal::make_control1(0x20, false) == 0x00);

// Control_status_2 leaves AF and TF set unless cleared
static_assert(pcf8563_internal::make_control2(0x00, {.alarm_flag = false, .timer_flag = false}) == 0x0C);
static_assert(pcf8563_internal::make_control2(0xFF, {.alarm_flag = false, .timer_flag = false}) == 0x1F);
static_assert(pcf8563_internal::make_control2(0xFF, {.alarm_flag = true, .timer_flag = false}) == 0x17);
static_assert(pcf8563_internal::make_control2(0xFF, {.alarm_flag = false, .timer_flag = true}) == 0x1B);
static_assert(pcf8563_internal::make_control2(0x00, {.alarm_flag = true, .timer_flag = true}) == 0x00);

// Timer interrupt modes and Timer_control
static_assert(pcf8563_internal::timer_interrupt_bits(pcf8563::timer_interrupt::none) == 0x00);
static_assert(pcf8563_internal::timer_interrupt_bits(pcf8563::timer_interrupt::level) == 0x01);
static_assert(pcf8563_internal::timer_interrupt_bits(pcf8563::timer_interrupt::pulse) == 0x11);
static_assert(pcf8563_internal::make_timer_control(false, timer_source::hz_1) == 0x02);
static_assert(pcf8563_internal::make_timer_control(true, timer_source::hz_1_60) == 0x83);
static_assert(pcf8563_internal::make_timer_control(true, timer_source::hz_4096) == 0x80);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("pcf8563 BCD round-trips 0-99", "[idfxx][rtc][pcf8563]") {
    for (int v = 0; v <= 99; ++v) {
        const uint8_t bcd = pcf8563_internal::bcd_encode(static_cast<uint8_t>(v));
        TEST_ASSERT_EQUAL_INT32(v / 10, bcd >> 4);
        TEST_ASSERT_EQUAL_INT32(v % 10, bcd & 0x0F);
        auto back = pcf8563_internal::bcd_decode(bcd);
        TEST_ASSERT_TRUE(back.has_value());
        TEST_ASSERT_EQUAL_INT32(v, *back);
    }
}

TEST_CASE("pcf8563 BCD rejects invalid nibbles", "[idfxx][rtc][pcf8563]") {
    int rejected = 0;
    for (int b = 0; b <= 0xFF; ++b) {
        const bool valid = (b >> 4) <= 9 && (b & 0x0F) <= 9;
        auto v = pcf8563_internal::bcd_decode(static_cast<uint8_t>(b));
        TEST_ASSERT_EQUAL(valid, v.has_value());
        if (!valid) {
            ++rejected;
        }
    }
    TEST_ASSERT_EQUAL_INT32(256 - 100, rejected);
}

TEST_CASE("pcf8563 decodes a sample time block", "[idfxx][rtc][pcf8563]") {
    auto t = pcf8563_internal::decode_time(sample_block);
    TEST_ASSERT_TRUE(t.has_value());
    TEST_ASSERT_TRUE(*t == sample_time);
}

TEST_CASE("pcf8563 decode ignores unused bits and the weekday", "[idfxx][rtc][pcf8563]") {
    auto regs = sample_block;
    regs[1] |= 0x80; // minutes b7
    regs[2] |= 0xC0; // hours b7-6
    regs[3] |= 0xC0; // days b7-6
    regs[4] = 0xFF;  // weekday register (ignored entirely, even out of range)
    regs[5] |= 0x60; // months b6-5
    auto t = pcf8563_internal::decode_time(regs);
    TEST_ASSERT_TRUE(t.has_value());
    TEST_ASSERT_TRUE(*t == sample_time);
}

TEST_CASE("pcf8563 decode reports VL as not valid", "[idfxx][rtc][pcf8563]") {
    auto regs = sample_block;
    regs[0] |= pcf8563_internal::seconds_vl;
    TEST_ASSERT_FALSE(pcf8563_internal::decode_time(regs).has_value());

    // Combined with the century bit and a corrupt field
    regs[5] |= pcf8563_internal::months_century;
    regs[6] = 0xAA;
    TEST_ASSERT_FALSE(pcf8563_internal::decode_time(regs).has_value());
}

TEST_CASE("pcf8563 clock block decode", "[idfxx][rtc][pcf8563]") {
    // Running clock: the time decodes; TESTC and Control_status_2 are ignored
    for (uint8_t control1 : {uint8_t{0x00}, pcf8563_internal::control1_testc}) {
        auto t = pcf8563_internal::decode_clock(make_clock_block(control1));
        TEST_ASSERT_TRUE(t.has_value());
        TEST_ASSERT_TRUE(*t == sample_time);
    }
    auto regs = make_clock_block(0x00);
    regs[1] = 0xFF;
    TEST_ASSERT_TRUE(pcf8563_internal::decode_clock(regs).has_value());

    // STOP set: not valid, even with a valid time
    TEST_ASSERT_FALSE(pcf8563_internal::decode_clock(make_clock_block(pcf8563_internal::control1_stop)).has_value());
    regs = make_clock_block(pcf8563_internal::control1_stop | pcf8563_internal::control1_testc);
    TEST_ASSERT_FALSE(pcf8563_internal::decode_clock(regs).has_value());

    // VL, the century bit and a corrupt field in the time registers are reported the same way
    regs = make_clock_block(0x00);
    regs[2] |= pcf8563_internal::seconds_vl;
    TEST_ASSERT_FALSE(pcf8563_internal::decode_clock(regs).has_value());
    regs = make_clock_block(0x00);
    regs[7] |= pcf8563_internal::months_century;
    TEST_ASSERT_FALSE(pcf8563_internal::decode_clock(regs).has_value());
    regs = make_clock_block(0x00);
    regs[8] = 0xA0;
    TEST_ASSERT_FALSE(pcf8563_internal::decode_clock(regs).has_value());
}

TEST_CASE("pcf8563 decode rejects a corrupt nibble in each field", "[idfxx][rtc][pcf8563]") {
    // Field index and a corrupt value for each of seconds, minutes, hours, days, months, years
    constexpr std::array<std::pair<int, uint8_t>, 6> corrupt{{
        {0, 0x5A},
        {1, 0x3B},
        {2, 0x1C},
        {3, 0x0D},
        {5, 0x0E},
        {6, 0xA0},
    }};
    for (const auto& [index, value] : corrupt) {
        auto regs = sample_block;
        regs[index] = value;
        TEST_ASSERT_FALSE(pcf8563_internal::decode_time(regs).has_value());
    }
}

TEST_CASE("pcf8563 decode rejects out-of-range fields", "[idfxx][rtc][pcf8563]") {
    constexpr std::array<std::pair<int, uint8_t>, 7> bad{{
        {0, 0x60}, // second 60
        {1, 0x60}, // minute 60
        {2, 0x24}, // hour 24
        {3, 0x00}, // day 0
        {3, 0x32}, // day 32
        {5, 0x00}, // month 0
        {5, 0x13}, // month 13
    }};
    for (const auto& [index, value] : bad) {
        auto regs = sample_block;
        regs[index] = value;
        TEST_ASSERT_FALSE(pcf8563_internal::decode_time(regs).has_value());
    }
}

TEST_CASE("pcf8563 decode rejects impossible dates", "[idfxx][rtc][pcf8563]") {
    // 31 September 2026
    pcf8563_internal::time_registers sep31{0x00, 0x00, 0x00, 0x31, 0x00, 0x09, 0x26};
    TEST_ASSERT_FALSE(pcf8563_internal::decode_time(sep31).has_value());

    // 29 February 2001 (not a leap year)
    pcf8563_internal::time_registers feb29_2001{0x00, 0x00, 0x00, 0x29, 0x00, 0x02, 0x01};
    TEST_ASSERT_FALSE(pcf8563_internal::decode_time(feb29_2001).has_value());

    // 29 February 2000 is a leap day
    pcf8563_internal::time_registers feb29_2000{0x00, 0x00, 0x00, 0x29, 0x00, 0x02, 0x00};
    auto t = pcf8563_internal::decode_time(feb29_2000);
    TEST_ASSERT_TRUE(t.has_value());
    TEST_ASSERT_TRUE(*t == make_time(2000, 2, 29, 0, 0, 0));
}

TEST_CASE("pcf8563 decode rejects the century bit", "[idfxx][rtc][pcf8563]") {
    auto regs = sample_block;
    regs[5] |= pcf8563_internal::months_century;
    TEST_ASSERT_FALSE(pcf8563_internal::decode_time(regs).has_value());
}

TEST_CASE("pcf8563 encode matches the sample block", "[idfxx][rtc][pcf8563]") {
    auto regs = pcf8563_internal::encode_time(sample_time);
    TEST_ASSERT_TRUE(regs.has_value());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sample_block.data(), regs->data(), sample_block.size());
}

TEST_CASE("pcf8563 encode clears VL and the century bit", "[idfxx][rtc][pcf8563]") {
    for (auto t : {pcf8563_internal::min_time, sample_time, pcf8563_internal::max_time}) {
        auto regs = pcf8563_internal::encode_time(t);
        TEST_ASSERT_TRUE(regs.has_value());
        TEST_ASSERT_EQUAL_HEX8(0, (*regs)[0] & pcf8563_internal::seconds_vl);
        TEST_ASSERT_EQUAL_HEX8(0, (*regs)[5] & pcf8563_internal::months_century);
    }
}

TEST_CASE("pcf8563 encode computes the weekday from the date", "[idfxx][rtc][pcf8563]") {
    struct case_t {
        sys_seconds time;
        uint8_t weekday;
    };
    constexpr std::array<case_t, 5> cases{{
        {make_time(2000, 1, 1, 0, 0, 0), 6},    // Saturday
        {make_time(2000, 1, 2, 0, 0, 0), 0},    // Sunday
        {make_time(2026, 11, 9, 12, 34, 56), 1}, // Monday
        {make_time(2024, 2, 29, 0, 0, 0), 4},   // Thursday
        {make_time(2099, 12, 31, 23, 59, 59), 4}, // Thursday
    }};
    for (const auto& c : cases) {
        auto regs = pcf8563_internal::encode_time(c.time);
        TEST_ASSERT_TRUE(regs.has_value());
        TEST_ASSERT_EQUAL_INT32(c.weekday, (*regs)[4]);
    }
}

TEST_CASE("pcf8563 encode and decode round-trip", "[idfxx][rtc][pcf8563]") {
    assert_round_trip(pcf8563_internal::min_time);
    assert_round_trip(sample_time);
    assert_round_trip(make_time(2024, 2, 28, 23, 59, 59));
    assert_round_trip(make_time(2024, 2, 29, 0, 0, 0));
    assert_round_trip(make_time(2024, 2, 29, 23, 59, 59));
    assert_round_trip(make_time(2024, 3, 1, 0, 0, 0));
    assert_round_trip(make_time(2025, 12, 31, 23, 59, 59));
    assert_round_trip(make_time(2026, 1, 1, 0, 0, 0));
    assert_round_trip(pcf8563_internal::max_time);

    // 2099-12-31T23:59:59 encodes to the expected registers
    auto regs = pcf8563_internal::encode_time(pcf8563_internal::max_time);
    TEST_ASSERT_TRUE(regs.has_value());
    const pcf8563_internal::time_registers expected{0x59, 0x59, 0x23, 0x31, 0x04, 0x12, 0x99};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), regs->data(), expected.size());
}

TEST_CASE("pcf8563 encode refuses times outside 2000-2099", "[idfxx][rtc][pcf8563]") {
    constexpr auto invalid_arg = idfxx::errc::invalid_arg;
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_time(make_time(1999, 12, 31, 23, 59, 59)), invalid_arg));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_time(make_time(1999, 12, 31, 0, 0, 0)), invalid_arg));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_time(make_time(2100, 1, 1, 0, 0, 0)), invalid_arg));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_time(sys_seconds{}), invalid_arg));
}

TEST_CASE("pcf8563 alarm encode and decode", "[idfxx][rtc][pcf8563]") {
    // Minute and hour enabled
    {
        auto regs = pcf8563_internal::encode_alarm({.minute = 30min, .hour = 7h});
        TEST_ASSERT_TRUE(regs.has_value());
        const pcf8563_internal::alarm_registers expected{0x30, 0x07, 0x80, 0x80};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), regs->data(), expected.size());

        auto back = pcf8563_internal::decode_alarm(*regs, pcf8563_internal::control2_aie);
        TEST_ASSERT_TRUE(back.has_value());
        TEST_ASSERT_TRUE(back->minute == 30min);
        TEST_ASSERT_TRUE(back->hour == 7h);
        TEST_ASSERT_FALSE(back->day.has_value());
        TEST_ASSERT_FALSE(back->weekday.has_value());
        TEST_ASSERT_TRUE(back->interrupt);
    }

    // Day and weekday enabled
    {
        auto regs = pcf8563_internal::encode_alarm({.day = std::chrono::day{15}, .weekday = std::chrono::Sunday});
        TEST_ASSERT_TRUE(regs.has_value());
        const pcf8563_internal::alarm_registers expected{0x80, 0x80, 0x15, 0x00};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), regs->data(), expected.size());

        auto back = pcf8563_internal::decode_alarm(*regs, 0x00);
        TEST_ASSERT_TRUE(back.has_value());
        TEST_ASSERT_FALSE(back->minute.has_value());
        TEST_ASSERT_FALSE(back->hour.has_value());
        TEST_ASSERT_TRUE(back->day == std::chrono::day{15});
        TEST_ASSERT_TRUE(back->weekday == std::chrono::Sunday);
        TEST_ASSERT_FALSE(back->interrupt);
    }

    // Every field enabled, at their upper bounds
    {
        auto regs = pcf8563_internal::encode_alarm(
            {.minute = 59min, .hour = 23h, .day = std::chrono::day{31}, .weekday = std::chrono::Saturday}
        );
        TEST_ASSERT_TRUE(regs.has_value());
        const pcf8563_internal::alarm_registers expected{0x59, 0x23, 0x31, 0x06};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), regs->data(), expected.size());
    }

    // All empty disables every field
    {
        auto regs = pcf8563_internal::encode_alarm({});
        TEST_ASSERT_TRUE(regs.has_value());
        const pcf8563_internal::alarm_registers expected{0x80, 0x80, 0x80, 0x80};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), regs->data(), expected.size());

        auto back = pcf8563_internal::decode_alarm(*regs, 0x00);
        TEST_ASSERT_TRUE(back.has_value());
        TEST_ASSERT_FALSE(back->minute || back->hour || back->day || back->weekday);
    }

    // Disabled fields are ignored whatever their value bits hold
    {
        auto back = pcf8563_internal::decode_alarm({0xFF, 0xFF, 0xFF, 0xFF}, 0x00);
        TEST_ASSERT_TRUE(back.has_value());
        TEST_ASSERT_FALSE(back->minute || back->hour || back->day || back->weekday);
    }
}

TEST_CASE("pcf8563 alarm refuses out-of-range fields", "[idfxx][rtc][pcf8563]") {
    using idfxx::errc;
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_alarm({.minute = 60min}), errc::invalid_arg));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_alarm({.minute = -1min}), errc::invalid_arg));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_alarm({.hour = 24h}), errc::invalid_arg));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_alarm({.hour = -1h}), errc::invalid_arg));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_alarm({.day = std::chrono::day{0}}), errc::invalid_arg));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_alarm({.day = std::chrono::day{32}}), errc::invalid_arg));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::encode_alarm({.weekday = std::chrono::weekday{8}}), errc::invalid_arg));

    // Impossible values in enabled fields are invalid states
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::decode_alarm({0x5A, 0x80, 0x80, 0x80}, 0), errc::invalid_state));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::decode_alarm({0x60, 0x80, 0x80, 0x80}, 0), errc::invalid_state));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::decode_alarm({0x80, 0x24, 0x80, 0x80}, 0), errc::invalid_state));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::decode_alarm({0x80, 0x80, 0x00, 0x80}, 0), errc::invalid_state));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::decode_alarm({0x80, 0x80, 0x32, 0x80}, 0), errc::invalid_state));
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::decode_alarm({0x80, 0x80, 0x80, 0x07}, 0), errc::invalid_state));
}

TEST_CASE("pcf8563 control2 builder never clears an unrequested flag", "[idfxx][rtc][pcf8563]") {
    constexpr std::array<pcf8563_internal::control2_clear, 4> requests{{
        {.alarm_flag = false, .timer_flag = false},
        {.alarm_flag = true, .timer_flag = false},
        {.alarm_flag = false, .timer_flag = true},
        {.alarm_flag = true, .timer_flag = true},
    }};
    for (const auto& clear : requests) {
        for (int bits = 0; bits <= 0xFF; ++bits) {
            const uint8_t b = pcf8563_internal::make_control2(static_cast<uint8_t>(bits), clear);
            TEST_ASSERT_EQUAL(!clear.alarm_flag, (b & pcf8563_internal::control2_af) != 0);
            TEST_ASSERT_EQUAL(!clear.timer_flag, (b & pcf8563_internal::control2_tf) != 0);
            TEST_ASSERT_EQUAL_HEX8(0, b & 0xE0); // unused bits b7-5
            constexpr uint8_t interrupt_mask = pcf8563_internal::control2_interrupt_mask;
            TEST_ASSERT_EQUAL_HEX8(bits & interrupt_mask, b & interrupt_mask);
        }
    }
}

TEST_CASE("pcf8563 timer source selection", "[idfxx][rtc][pcf8563]") {
    // Cases from the API documentation
    assert_timer_setting(30s, timer_source::hz_1, 30);
    assert_timer_setting(250ms, timer_source::hz_64, 16);
    assert_timer_setting(5min, timer_source::hz_1_60, 5);
    assert_timer_setting(203'125us, timer_source::hz_64, 13);
    assert_timer_setting(255min, timer_source::hz_1_60, 255);
    assert_timer_rejected(100ms);

    // The fastest exact source wins
    assert_timer_setting(timer_duration{1}, timer_source::hz_4096, 1);
    assert_timer_setting(1s, timer_source::hz_64, 64);
    assert_timer_setting(4min, timer_source::hz_1, 240);
    assert_timer_setting(300s, timer_source::hz_1_60, 5);

    // n = 255 boundaries for each source
    assert_timer_setting(timer_duration{255}, timer_source::hz_4096, 255);
    assert_timer_setting(timer_duration{256}, timer_source::hz_64, 4);
    assert_timer_setting(timer_duration{64 * 255}, timer_source::hz_64, 255);
    assert_timer_setting(255s, timer_source::hz_1, 255);
    assert_timer_rejected(timer_duration{257});
    assert_timer_rejected(256s);
    assert_timer_rejected(256min);
    TEST_ASSERT_TRUE(is_error(pcf8563_internal::select_timer_source(timer_duration::zero()), idfxx::errc::invalid_arg));
}

TEST_CASE("pcf8563 timer periods are never rounded or wrapped", "[idfxx][rtc][pcf8563]") {
    // Not a whole number of 1/4096 s ticks
    assert_timer_rejected(100ms);
    assert_timer_rejected(50ms);
    assert_timer_rejected(10ms);
    assert_timer_rejected(1ns);

    // Longer than 255 minutes, including counts that would wrap a uint32_t tick count
    assert_timer_rejected(256min);
    assert_timer_rejected(1'048'606s); // 30 s once wrapped
    assert_timer_rejected(std::chrono::hours::max());
    assert_timer_rejected(std::chrono::nanoseconds::max());

    // Not positive
    assert_timer_rejected(0s);
    assert_timer_rejected(-1s);
    assert_timer_rejected(-std::chrono::hours::max());
    assert_timer_rejected(std::chrono::hours::min());

    // Narrow and unsigned reps
    assert_timer_setting(std::chrono::duration<int16_t, std::milli>{250}, timer_source::hz_64, 16);
    assert_timer_setting(std::chrono::duration<uint8_t, std::ratio<60>>{255}, timer_source::hz_1_60, 255);
    assert_timer_setting(std::chrono::duration<int8_t>{30}, timer_source::hz_1, 30);
    assert_timer_rejected(std::chrono::duration<int16_t, std::nano>{32767});
    assert_timer_rejected(std::chrono::duration<int16_t, std::nano>{-32768});
    assert_timer_rejected(std::chrono::duration<int8_t, std::ratio<3600>>{5});
    assert_timer_rejected(std::chrono::duration<uint8_t>{0});
}

TEST_CASE("pcf8563 timer_remaining", "[idfxx][rtc][pcf8563]") {
    // Disabled timer: zero whatever the count
    TEST_ASSERT_TRUE(pcf8563_internal::timer_remaining(0x02, 30) == timer_duration::zero());
    TEST_ASSERT_TRUE(pcf8563_internal::timer_remaining(0x03, 255) == timer_duration::zero());

    TEST_ASSERT_TRUE(pcf8563_internal::timer_remaining(0x80, 100) == timer_duration{100});
    TEST_ASSERT_TRUE(pcf8563_internal::timer_remaining(0x81, 13) == timer_duration{832});
    TEST_ASSERT_TRUE(pcf8563_internal::timer_remaining(0x82, 30) == 30s);
    TEST_ASSERT_TRUE(pcf8563_internal::timer_remaining(0x83, 255) == 255min);
    TEST_ASSERT_TRUE(pcf8563_internal::timer_remaining(0x82, 0) == timer_duration::zero());

    // Unused bits are ignored
    TEST_ASSERT_TRUE(pcf8563_internal::timer_remaining(0xFE, 2) == 2s);
}

TEST_CASE("pcf8563 clkout encode and decode", "[idfxx][rtc][pcf8563]") {
    TEST_ASSERT_EQUAL_HEX8(0x00, pcf8563_internal::encode_clkout(std::nullopt));
    TEST_ASSERT_EQUAL_HEX8(0x80, pcf8563_internal::encode_clkout(clkout_frequency::hz_32768));
    TEST_ASSERT_EQUAL_HEX8(0x81, pcf8563_internal::encode_clkout(clkout_frequency::hz_1024));
    TEST_ASSERT_EQUAL_HEX8(0x82, pcf8563_internal::encode_clkout(clkout_frequency::hz_32));
    TEST_ASSERT_EQUAL_HEX8(0x83, pcf8563_internal::encode_clkout(clkout_frequency::hz_1));

    for (auto f : {clkout_frequency::hz_32768, clkout_frequency::hz_1024, clkout_frequency::hz_32, clkout_frequency::hz_1}) {
        auto back = pcf8563_internal::decode_clkout(pcf8563_internal::encode_clkout(f));
        TEST_ASSERT_TRUE(back == f);
    }
    TEST_ASSERT_FALSE(pcf8563_internal::decode_clkout(0x00).has_value());
    TEST_ASSERT_FALSE(pcf8563_internal::decode_clkout(0x03).has_value()); // FE = 0
    TEST_ASSERT_TRUE(pcf8563_internal::decode_clkout(0xFE) == clkout_frequency::hz_32); // unused bits ignored
}

TEST_CASE("pcf8563 enum to_string", "[idfxx][rtc][pcf8563]") {
    TEST_ASSERT_EQUAL_STRING("4096Hz", idfxx::to_string(timer_source::hz_4096).c_str());
    TEST_ASSERT_EQUAL_STRING("1/60Hz", idfxx::to_string(timer_source::hz_1_60).c_str());
    TEST_ASSERT_EQUAL_STRING("pulse", idfxx::to_string(pcf8563::timer_interrupt::pulse).c_str());
    TEST_ASSERT_EQUAL_STRING("32768Hz", idfxx::to_string(clkout_frequency::hz_32768).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(9)", idfxx::to_string(static_cast<clkout_frequency>(9)).c_str());
#ifdef CONFIG_IDFXX_STD_FORMAT
    TEST_ASSERT_EQUAL_STRING("64Hz", std::format("{}", timer_source::hz_64).c_str());
    TEST_ASSERT_EQUAL_STRING("level", std::format("{}", pcf8563::timer_interrupt::level).c_str());
    TEST_ASSERT_EQUAL_STRING("1Hz", std::format("{}", clkout_frequency::hz_1).c_str());
#endif
}

TEST_CASE("pcf8563 set_time restarts a stopped clock", "[idfxx][rtc][pcf8563][hw]") {
    auto bus = idfxx::i2c::master_bus::make(idfxx::i2c::port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
    TEST_ASSERT_TRUE_MESSAGE(bus.has_value(), "Failed to create I2C bus");
    auto rtc = pcf8563::make(*bus);
    if (is_error(rtc, idfxx::errc::not_found)) {
        TEST_IGNORE_MESSAGE("no PCF8563 attached");
    }
    TEST_ASSERT_TRUE(rtc.has_value());

    TEST_ASSERT_TRUE(rtc->try_stop().has_value());
    auto running = rtc->try_running();
    TEST_ASSERT_TRUE(running.has_value());
    TEST_ASSERT_FALSE(*running);
    auto now = rtc->try_now();
    TEST_ASSERT_TRUE(now.has_value());
    TEST_ASSERT_FALSE(now->has_value());

    TEST_ASSERT_TRUE(rtc->try_set_time(sample_time).has_value());
    running = rtc->try_running();
    TEST_ASSERT_TRUE(running.has_value());
    TEST_ASSERT_TRUE(*running);
    now = rtc->try_now();
    TEST_ASSERT_TRUE(now.has_value());
    TEST_ASSERT_TRUE(now->has_value());
}
