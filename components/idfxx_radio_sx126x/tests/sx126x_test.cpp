// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_radio_sx126x
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "../src/sx126x_codec.hpp"
#include "../src/sx126x_internal.hpp"
#include "idfxx/radio/sx126x"
#include "unity.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <type_traits>

using namespace electro_literals;
using namespace idfxx::radio;
namespace codec = idfxx::radio::sx126x_internal;

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

// sx126x is a concrete subclass of the abstract transceiver base. This doubles as a
// regression guard: every pure-virtual do_* hook on transceiver — including the
// future-returning do_start_transmit, do_start_receive, and
// do_start_channel_scan — must be overridden with a matching signature, or
// sx126x would remain abstract and this would fail.
static_assert(std::is_base_of_v<transceiver, sx126x>);
static_assert(!std::is_abstract_v<sx126x>);

// sx126x is move-only.
static_assert(!std::is_copy_constructible_v<sx126x>);
static_assert(!std::is_copy_assignable_v<sx126x>);
static_assert(std::is_move_constructible_v<sx126x>);
static_assert(std::is_move_assignable_v<sx126x>);

// config is default-constructible (every field has a default).
static_assert(std::is_default_constructible_v<sx126x::config>);
static_assert(std::is_default_constructible_v<sx126x::cad_params>);
static_assert(std::is_default_constructible_v<sx126x::tcxo_config>);

// dio1_mask carries the full IRQ bitfield type so any flag combination can be
// routed, and warm-start recovery exposes the adopted packet's rx_info.
static_assert(std::is_same_v<decltype(sx126x::config::dio1_mask), std::optional<idfxx::flags<sx126x::irq_flag>>>);
static_assert(std::is_same_v<decltype(&sx126x::try_adopt_pending), idfxx::result<std::optional<rx_info>> (sx126x::*)()>
);

// irq_flag has the documented bit layout.
static_assert(std::to_underlying(sx126x::irq_flag::tx_done) == (1 << 0));
static_assert(std::to_underlying(sx126x::irq_flag::rx_done) == (1 << 1));
static_assert(std::to_underlying(sx126x::irq_flag::crc_err) == (1 << 6));
static_assert(std::to_underlying(sx126x::irq_flag::timeout) == (1 << 9));

// =============================================================================
// Pure register-encoder tests (src/sx126x_codec.hpp)
// =============================================================================

// Bandwidth bytes (DS table 13-47).
static_assert(codec::bandwidth_byte(bandwidth::bw_7_8) == 0x00);
static_assert(codec::bandwidth_byte(bandwidth::bw_125) == 0x04);
static_assert(codec::bandwidth_byte(bandwidth::bw_250) == 0x05);
static_assert(codec::bandwidth_byte(bandwidth::bw_500) == 0x06);

// Spreading factor: the chip encoding is the SF number itself.
static_assert(codec::spreading_factor_byte(spreading_factor::sf7) == 7);
static_assert(codec::spreading_factor_byte(spreading_factor::sf12) == 12);

// Coding rate: 1..4 for cr_4_5..cr_4_8.
static_assert(codec::coding_rate_byte(coding_rate::cr_4_5) == 1);
static_assert(codec::coding_rate_byte(coding_rate::cr_4_8) == 4);

// Ramp-time buckets.
static_assert(codec::ramp_time_byte(ramp_time::us_10) == 0x00);
static_assert(codec::ramp_time_byte(ramp_time::us_200) == 0x04);
static_assert(codec::ramp_time_byte(ramp_time::us_3400) == 0x07);

// SetRfFrequency register value: reg = hz * 2^25 / 32 MHz.
// 915 MHz -> 0x39300000 (915.2 MHz would be 0x39333000; this is exactly 915.0).
static_assert(codec::freq_to_register(915'000'000) == 0x39300000u);
static_assert(codec::freq_to_register(868'000'000) == 0x36400000u);
static_assert(codec::freq_to_register(433'000'000) == 0x1B100000u);

// SetPacketParams packing: preamble MSB/LSB, header, payload len, CRC, IQ.
static_assert(
    codec::pack_packet_params(
        lora_packet_params{
            .preamble_length = 8,
            .header = header_type::variable,
            .payload_length = 16,
            .crc_on = true,
            .invert_iq = false,
        }
    ) == std::array<uint8_t, 6>{0x00, 0x08, 0x00, 16, 0x01, 0x00}
);
static_assert(
    codec::pack_packet_params(
        lora_packet_params{
            .preamble_length = 0x1234,
            .header = header_type::fixed,
            .payload_length = 255,
            .crc_on = false,
            .invert_iq = true,
        }
    ) == std::array<uint8_t, 6>{0x12, 0x34, 0x01, 255, 0x00, 0x01}
);

// CalibrateImage band table (DS §9.2.1).
static_assert(codec::calibrate_image_bytes(915'000'000).f1 == 0xE1);
static_assert(codec::calibrate_image_bytes(915'000'000).f2 == 0xE9);
static_assert(codec::calibrate_image_bytes(868'000'000).f1 == 0xD7);
static_assert(codec::calibrate_image_bytes(868'000'000).f2 == 0xDB);
static_assert(codec::calibrate_image_bytes(780'000'000).f1 == 0xC1);
static_assert(codec::calibrate_image_bytes(490'000'000).f1 == 0x75);
static_assert(codec::calibrate_image_bytes(434'000'000).f1 == 0x6B);

// SetDio3AsTcxoCtrl voltage byte: index of the highest supported voltage not
// above the supplied mV (0=1.6V … 7=3.3V); below 1.6 V clamps to 0.
static_assert(codec::tcxo_voltage_byte(1500) == 0);
static_assert(codec::tcxo_voltage_byte(1600) == 0);
static_assert(codec::tcxo_voltage_byte(1700) == 1);
static_assert(codec::tcxo_voltage_byte(1750) == 1);
static_assert(codec::tcxo_voltage_byte(1800) == 2);
static_assert(codec::tcxo_voltage_byte(2200) == 3);
static_assert(codec::tcxo_voltage_byte(2400) == 4);
static_assert(codec::tcxo_voltage_byte(2700) == 5);
static_assert(codec::tcxo_voltage_byte(3000) == 6);
static_assert(codec::tcxo_voltage_byte(3300) == 7);
static_assert(codec::tcxo_voltage_byte(5000) == 7);

// GetPacketStatus / GetRssiInst decoding: RSSI bytes are -raw/2 dBm, SNR
// bytes are a signed quarter-dB value.
static_assert(codec::decode_rssi_byte(160) == -80_dBm);
static_assert(codec::decode_rssi_byte(161) == electro::centi_dbm(-8050));
static_assert(codec::decode_rssi_byte(0) == 0_dBm);
static_assert(codec::decode_packet_status(std::array<uint8_t, 3>{160, 20, 180}).rssi == -80_dBm);
static_assert(codec::decode_packet_status(std::array<uint8_t, 3>{160, 20, 180}).snr == 5_dB);

// SetRxDutyCycle step conversion: one step = 15.625 µs (= µs · 64 / 1000).
static_assert(codec::duty_cycle_steps(std::chrono::microseconds{1000}) == 64);
static_assert(codec::duty_cycle_steps(std::chrono::microseconds{15625}) == 1000);
static_assert(codec::duty_cycle_steps(std::chrono::microseconds{0}) == 0);
static_assert(codec::duty_cycle_steps(std::chrono::microseconds{-5}) == 0);
// Saturates the 24-bit period field (0xFFFFFF steps ≈ 262 s).
static_assert(codec::duty_cycle_steps(std::chrono::microseconds{300'000'000}) == 0xFFFFFF);

// SetRxDutyCycle byte layout: rxPeriod (MSB..LSB) then sleepPeriod (MSB..LSB).
// rx = 1000 µs → 64 = 0x000040; sleep = 15625 µs → 1000 = 0x0003E8.
static_assert(
    codec::pack_rx_duty_cycle(std::chrono::microseconds{1000}, std::chrono::microseconds{15625}) ==
    std::array<uint8_t, 6>{0x00, 0x00, 0x40, 0x00, 0x03, 0xE8}
);

// Per-variant output-power limits (DS chapter 13.4.4, SetTxParams).
static_assert(codec::power_limits_for(sx126x::chip_variant::sx1261).min_dbm == -17);
static_assert(codec::power_limits_for(sx126x::chip_variant::sx1261).max_dbm == 15);
static_assert(codec::power_limits_for(sx126x::chip_variant::sx1262).min_dbm == -9);
static_assert(codec::power_limits_for(sx126x::chip_variant::sx1262).max_dbm == 22);
static_assert(codec::power_limits_for(sx126x::chip_variant::sx1268).max_dbm == 22);

// SX1261 +15 dBm needs the high-duty-cycle PA row and a +14 SetTxParams byte
// (SetTxParams only accepts -17..+14 on the SX1261); +14 and below use the
// standard row and pass the dBm through.
static_assert(codec::tx_power_config_for(sx126x::chip_variant::sx1261, 15).pa.pa_duty_cycle == 0x06);
static_assert(codec::tx_power_config_for(sx126x::chip_variant::sx1261, 15).power_byte == 14);
static_assert(codec::tx_power_config_for(sx126x::chip_variant::sx1261, 14).pa.pa_duty_cycle == 0x04);
static_assert(codec::tx_power_config_for(sx126x::chip_variant::sx1261, 14).power_byte == 14);
static_assert(codec::tx_power_config_for(sx126x::chip_variant::sx1261, 14).pa.device_sel == 0x01);

// SX1262/SX1268 use the +22 dBm high-power row and trim via SetTxParams.
static_assert(codec::tx_power_config_for(sx126x::chip_variant::sx1262, 22).pa.hp_max == 0x07);
static_assert(codec::tx_power_config_for(sx126x::chip_variant::sx1262, 14).power_byte == 14);
static_assert(codec::tx_power_config_for(sx126x::chip_variant::sx1268, 22).pa.device_sel == 0x00);

// =============================================================================
// Runtime tests
// =============================================================================

TEST_CASE("sx126x config defaults", "[idfxx][radio][sx126x]") {
    sx126x::config cfg{};
    TEST_ASSERT_EQUAL(sx126x::chip_variant::sx1262, cfg.variant);
    TEST_ASSERT_FALSE(cfg.cs.is_connected());
    TEST_ASSERT_TRUE(cfg.dio2_as_rf_switch);
    TEST_ASSERT_FALSE(cfg.tcxo.has_value());
    TEST_ASSERT_EQUAL(sx126x::regulator::ldo, cfg.regulator);
    // Cold start with the driver's standard DIO1 routing unless overridden.
    TEST_ASSERT_FALSE(cfg.dio1_mask.has_value());
    TEST_ASSERT_FALSE(cfg.warm_start);
}

TEST_CASE("cad_params defaults match SX126x datasheet recommendations", "[idfxx][radio][sx126x]") {
    sx126x::cad_params p{};
    TEST_ASSERT_EQUAL(2, p.symbol_num);
    TEST_ASSERT_EQUAL(22, p.det_peak);
    TEST_ASSERT_EQUAL(10, p.det_min);
}

TEST_CASE("freq_to_register matches the datasheet formula", "[idfxx][radio][sx126x]") {
    TEST_ASSERT_EQUAL_HEX32(0x39300000u, codec::freq_to_register(915'000'000));
    TEST_ASSERT_EQUAL_HEX32(0x36400000u, codec::freq_to_register(868'000'000));
}

TEST_CASE("calibrate_image_bytes selects the correct band", "[idfxx][radio][sx126x]") {
    auto us915 = codec::calibrate_image_bytes(915'000'000);
    TEST_ASSERT_EQUAL_HEX8(0xE1, us915.f1);
    TEST_ASSERT_EQUAL_HEX8(0xE9, us915.f2);
    auto eu868 = codec::calibrate_image_bytes(868'000'000);
    TEST_ASSERT_EQUAL_HEX8(0xD7, eu868.f1);
    TEST_ASSERT_EQUAL_HEX8(0xDB, eu868.f2);
}

TEST_CASE("duty_cycle_steps converts to 15.625 us steps", "[idfxx][radio][sx126x]") {
    TEST_ASSERT_EQUAL_UINT32(64, codec::duty_cycle_steps(std::chrono::microseconds{1000}));
    TEST_ASSERT_EQUAL_UINT32(1000, codec::duty_cycle_steps(std::chrono::microseconds{15625}));
    TEST_ASSERT_EQUAL_UINT32(0, codec::duty_cycle_steps(std::chrono::microseconds{0}));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, codec::duty_cycle_steps(std::chrono::microseconds{300'000'000}));
}

TEST_CASE("pack_rx_duty_cycle lays out rx then sleep, MSB first", "[idfxx][radio][sx126x]") {
    auto p = codec::pack_rx_duty_cycle(std::chrono::microseconds{1000}, std::chrono::microseconds{15625});
    const std::array<uint8_t, 6> expected{0x00, 0x00, 0x40, 0x00, 0x03, 0xE8};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), p.data(), expected.size());
}
