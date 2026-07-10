// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_radio
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/future"
#include "idfxx/radio/airtime"
#include "idfxx/radio/duty_cycle"
#include "idfxx/radio/events"
#include "idfxx/radio/transceiver"
#include "idfxx/radio/types"
#include "unity.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

using namespace idfxx::radio;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// The transceiver base is abstract — no concrete chip details leak in.
static_assert(std::is_abstract_v<transceiver>);

// The base is non-copyable so concrete drivers inherit that property.
static_assert(!std::is_copy_constructible_v<transceiver>);
static_assert(!std::is_copy_assignable_v<transceiver>);

// LoRa-spec semantics for spreading factor — drivers map to chip-specific
// register encodings internally, but the public values are the LoRa spec
// SF numbers themselves.
static_assert(std::to_underlying(spreading_factor::sf5) == 5);
static_assert(std::to_underlying(spreading_factor::sf7) == 7);
static_assert(std::to_underlying(spreading_factor::sf12) == 12);

// Coding-rate underlying value = denominator of the 4/N rate.
static_assert(std::to_underlying(coding_rate::cr_4_5) == 5);
static_assert(std::to_underlying(coding_rate::cr_4_8) == 8);

// Default-constructible value types.
static_assert(std::is_default_constructible_v<lora_modulation>);
static_assert(std::is_default_constructible_v<lora_packet_params>);
static_assert(std::is_default_constructible_v<rx_info>);
static_assert(std::is_default_constructible_v<cad_info>);
static_assert(std::is_default_constructible_v<packet_status>);

// Trivially copyable so they can flow through the event loop.
static_assert(std::is_trivially_copyable_v<rx_info>);
static_assert(std::is_trivially_copyable_v<cad_info>);
static_assert(std::is_trivially_copyable_v<packet_status>);

// Bandwidth enum has all the LoRa spec values.
static_assert(static_cast<int>(bandwidth::bw_500) > static_cast<int>(bandwidth::bw_125));

// One-shot async data-path operations return futures; packet streams
// (continuous / duty-cycled receive) stay event-based and return void.
static_assert(std::is_same_v<
              decltype(std::declval<transceiver&>().try_start_transmit(std::declval<std::span<const uint8_t>>())),
              idfxx::result<idfxx::future<void>>>);
static_assert(std::is_same_v<
              decltype(std::declval<transceiver&>().try_start_receive(std::declval<std::span<uint8_t>>())),
              idfxx::result<idfxx::future<rx_info>>>);
static_assert(std::is_same_v<
              decltype(std::declval<transceiver&>().try_start_channel_scan()),
              idfxx::result<idfxx::future<cad_info>>>);
static_assert(std::is_same_v<decltype(std::declval<transceiver&>().try_start_listening()), idfxx::result<void>>);
static_assert(
    std::is_same_v<decltype(std::declval<transceiver&>().try_start_listening(rx_duty_cycle{})), idfxx::result<void>>
);

// Blocking forms compose over the futures and keep their signatures.
static_assert(std::is_same_v<
              decltype(std::declval<transceiver&>()
                           .try_transmit(std::declval<std::span<const uint8_t>>(), std::chrono::milliseconds{1})),
              idfxx::result<void>>);
static_assert(std::is_same_v<
              decltype(std::declval<transceiver&>()
                           .try_receive(std::declval<std::span<uint8_t>>(), std::chrono::milliseconds{1})),
              idfxx::result<rx_info>>);
static_assert(std::is_same_v<decltype(std::declval<transceiver&>().try_scan_channel()), idfxx::result<cad_info>>);

// =============================================================================
// Time-on-air (airtime.hpp)
//
// Reference air-times in microseconds from the classic Semtech LoRa equation;
// these are exact for SF7–SF12 and match RadioLib's getTimeOnAir. Each uses an
// 8-symbol preamble, coding rate 4/5, explicit header and CRC on (the type
// defaults) unless noted. The result is exact integer math, so it is checked at
// compile time.
// =============================================================================

// SF7 / BW125, 16-byte payload — the baseline case.
static_assert(time_on_air({.sf = spreading_factor::sf7, .bw = bandwidth::bw_125}, {}, 16).count() == 51456);

// Empty payload — still carries the 8 fixed payload symbols plus preamble.
static_assert(time_on_air({.sf = spreading_factor::sf7, .bw = bandwidth::bw_125}, {}, 0).count() == 25856);

// BW500 is exactly 4× faster than BW125 (symbol count is independent of bandwidth).
static_assert(time_on_air({.sf = spreading_factor::sf7, .bw = bandwidth::bw_500}, {}, 16).count() == 12864);
static_assert(
    time_on_air({.sf = spreading_factor::sf7, .bw = bandwidth::bw_500}, {}, 16).count() * 4 ==
    time_on_air({.sf = spreading_factor::sf7, .bw = bandwidth::bw_125}, {}, 16).count()
);

// SF9 / BW125, 11-byte payload — the ping_pong example's configuration.
static_assert(time_on_air({.sf = spreading_factor::sf9, .bw = bandwidth::bw_125}, {}, 11).count() == 144384);

// SF12 / BW125 climbs to nearly a second even for a 1-byte payload.
static_assert(time_on_air({.sf = spreading_factor::sf12, .bw = bandwidth::bw_125}, {}, 1).count() == 827392);

// Implicit (fixed) header drops 20 from the symbol-count numerator, shortening
// the packet relative to the explicit-header baseline (51456 µs).
static_assert(
    time_on_air({.sf = spreading_factor::sf7, .bw = bandwidth::bw_125}, {.header = header_type::fixed}, 16).count() ==
    46336
);

// Low-data-rate optimization adds redundancy, lengthening the packet.
static_assert(
    time_on_air({.sf = spreading_factor::sf7, .bw = bandwidth::bw_125, .low_data_rate_optimize = true}, {}, 16)
        .count() == 61696
);

// =============================================================================
// Duty-cycle windows (duty_cycle.hpp)
//
// Reference windows from the RadioLib startReceiveDutyCycleAuto algorithm:
// sleep = Tsym·(preamble − 2·min_symbols), rx = max((Tsym·(preamble+1) −
// (sleep − 1ms))/2, Tsym·(min_symbols+1)). Exact integer math, checked at
// compile time.
// =============================================================================

// Trivially copyable value type, like the other result/config structs.
static_assert(std::is_trivially_copyable_v<rx_duty_cycle>);

// SF9/BW125 (Tsym = 4096 µs), 100-symbol preamble, default min_symbols = 8:
// sleep = 84 symbols; the detection floor Tsym·9 wins for the listen window.
static_assert(
    rx_duty_cycle_for({.sf = spreading_factor::sf9, .bw = bandwidth::bw_125}, 100)->sleep_period.count() == 344064
);
static_assert(
    rx_duty_cycle_for({.sf = spreading_factor::sf9, .bw = bandwidth::bw_125}, 100)->rx_period.count() == 36864
);

// SF7/BW250 (Tsym = 512 µs < 1 ms margin): the worst-case term wins instead.
static_assert(
    rx_duty_cycle_for({.sf = spreading_factor::sf7, .bw = bandwidth::bw_250}, 100)->sleep_period.count() == 43008
);
static_assert(
    rx_duty_cycle_for({.sf = spreading_factor::sf7, .bw = bandwidth::bw_250}, 100)->rx_period.count() == 4852
);

// Preamble too short to sleep inside (2·min_symbols ≥ preamble) → continuous.
static_assert(!rx_duty_cycle_for({.sf = spreading_factor::sf9, .bw = bandwidth::bw_125}, 16).has_value());

// min_symbols must be at least 1.
static_assert(!rx_duty_cycle_for({.sf = spreading_factor::sf9, .bw = bandwidth::bw_125}, 100, 0).has_value());

// Sleep window below min_sleep → continuous. SF7/BW500: Tsym = 256 µs, a
// 20-symbol preamble leaves a 4-symbol (1024 µs) sleep — just above the
// default 1016 µs floor, below a TCXO-padded one.
static_assert(rx_duty_cycle_for({.sf = spreading_factor::sf7, .bw = bandwidth::bw_500}, 20).has_value());
static_assert(!rx_duty_cycle_for({.sf = spreading_factor::sf7, .bw = bandwidth::bw_500}, 19).has_value());
static_assert(
    !rx_duty_cycle_for({.sf = spreading_factor::sf7, .bw = bandwidth::bw_500}, 20, 8, std::chrono::microseconds{2000})
         .has_value()
);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("lora_modulation defaults", "[idfxx][radio]") {
    lora_modulation mod{};
    TEST_ASSERT_EQUAL(spreading_factor::sf7, mod.sf);
    TEST_ASSERT_EQUAL(bandwidth::bw_125, mod.bw);
    TEST_ASSERT_EQUAL(coding_rate::cr_4_5, mod.cr);
    TEST_ASSERT_FALSE(mod.low_data_rate_optimize);
}

TEST_CASE("lora_packet_params defaults", "[idfxx][radio]") {
    lora_packet_params params{};
    TEST_ASSERT_EQUAL(8, params.preamble_length);
    TEST_ASSERT_EQUAL(header_type::variable, params.header);
    TEST_ASSERT_TRUE(params.crc_on);
    TEST_ASSERT_FALSE(params.invert_iq);
}

TEST_CASE("time_on_air matches Semtech reference air-times", "[idfxx][radio]") {
    // Modulation temporaries are passed inline; the static_asserts above verify
    // the same references at compile time. (CR4-5, explicit header, CRC on.)
    // Compared as 32-bit values: every reference air-time is far below
    // INT32_MAX µs, and Unity's 64-bit support is not enabled in every test
    // configuration.
    auto airtime_us = [](const lora_modulation& mod, const lora_packet_params& pkt, size_t len) {
        return static_cast<int32_t>(time_on_air(mod, pkt, len).count());
    };

    // Baseline SF7/BW125, 16-byte payload.
    TEST_ASSERT_EQUAL_INT32(51456, airtime_us({.sf = spreading_factor::sf7, .bw = bandwidth::bw_125}, {}, 16));
    // Empty payload still carries the 8 fixed payload symbols.
    TEST_ASSERT_EQUAL_INT32(25856, airtime_us({.sf = spreading_factor::sf7, .bw = bandwidth::bw_125}, {}, 0));

    // Higher spreading factor → exponentially longer air-time.
    TEST_ASSERT_EQUAL_INT32(827392, airtime_us({.sf = spreading_factor::sf12, .bw = bandwidth::bw_125}, {}, 1));

    // The example's SF9/BW125 config with an 11-byte payload.
    TEST_ASSERT_EQUAL_INT32(144384, airtime_us({.sf = spreading_factor::sf9, .bw = bandwidth::bw_125}, {}, 11));

    // Wider bandwidth shortens air-time proportionally (BW500 = 4 × BW125).
    TEST_ASSERT_INT32_WITHIN(2, 51456, airtime_us({.sf = spreading_factor::sf7, .bw = bandwidth::bw_500}, {}, 16) * 4);

    // Implicit header shortens, LDRO lengthens, relative to the 51456 µs baseline.
    TEST_ASSERT_LESS_THAN_INT32(
        51456, airtime_us({.sf = spreading_factor::sf7, .bw = bandwidth::bw_125}, {.header = header_type::fixed}, 16)
    );
    TEST_ASSERT_GREATER_THAN_INT32(
        51456,
        airtime_us({.sf = spreading_factor::sf7, .bw = bandwidth::bw_125, .low_data_rate_optimize = true}, {}, 16)
    );
}

TEST_CASE("rx_duty_cycle_for matches RadioLib reference windows", "[idfxx][radio]") {
    // SF9/BW125, 100-symbol preamble: sleep 84 symbols, listen at the
    // 9-symbol detection floor (the static_asserts above verify the same
    // references at compile time).
    auto cycle = rx_duty_cycle_for({.sf = spreading_factor::sf9, .bw = bandwidth::bw_125}, 100);
    TEST_ASSERT_TRUE(cycle.has_value());
    TEST_ASSERT_EQUAL_INT32(344064, static_cast<int32_t>(cycle->sleep_period.count()));
    TEST_ASSERT_EQUAL_INT32(36864, static_cast<int32_t>(cycle->rx_period.count()));

    // Short preambles cannot be duty-cycled — the caller falls back to
    // continuous receive.
    TEST_ASSERT_FALSE(rx_duty_cycle_for({.sf = spreading_factor::sf9, .bw = bandwidth::bw_125}, 16).has_value());
}
