// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Pure, side-effect-free SX126x register encoders. This header intentionally
// depends only on the chip-agnostic LoRa types — no SPI, task, or chip state —
// so the encodings can be unit-tested in isolation (the test #includes this
// header by relative path). All register encodings are taken from the Semtech
// DS_SX1261-2 datasheet.

#include <idfxx/radio/types.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <electro/decibel>
#include <electro/electro>
#include <span>
#include <utility>

namespace idfxx::radio::sx126x_internal {

// =============================================================================
// LoRa modulation register-byte encodings (DS table 13-47)
// =============================================================================

/// Bandwidth byte values written to SetModulationParams (param 2).
[[nodiscard]] constexpr uint8_t bandwidth_byte(bandwidth bw) noexcept {
    switch (bw) {
    case bandwidth::bw_7_8:
        return 0x00;
    case bandwidth::bw_10_4:
        return 0x08;
    case bandwidth::bw_15_6:
        return 0x01;
    case bandwidth::bw_20_8:
        return 0x09;
    case bandwidth::bw_31_25:
        return 0x02;
    case bandwidth::bw_41_7:
        return 0x0A;
    case bandwidth::bw_62_5:
        return 0x03;
    case bandwidth::bw_125:
        return 0x04;
    case bandwidth::bw_250:
        return 0x05;
    case bandwidth::bw_500:
        return 0x06;
    }
    return 0x04; // safe default = 125 kHz
}

/// Spreading-factor byte values (the LoRa SF number itself, which happens to
/// match the chip's register encoding for SX126x: 0x05–0x0C).
[[nodiscard]] constexpr uint8_t spreading_factor_byte(spreading_factor sf) noexcept {
    return static_cast<uint8_t>(sf);
}

/// Coding-rate byte values (1..4 corresponds to cr_4_5..cr_4_8).
[[nodiscard]] constexpr uint8_t coding_rate_byte(coding_rate cr) noexcept {
    return static_cast<uint8_t>(std::to_underlying(cr) - 4);
}

/// Ramp-time byte values. Drivers round semantic buckets to the closest
/// supported value.
[[nodiscard]] constexpr uint8_t ramp_time_byte(ramp_time r) noexcept {
    switch (r) {
    case ramp_time::us_10:
        return 0x00;
    case ramp_time::us_40:
        return 0x02;
    case ramp_time::us_200:
        return 0x04;
    case ramp_time::us_800:
        return 0x06;
    case ramp_time::us_3400:
        return 0x07;
    }
    return 0x04;
}

// =============================================================================
// LoRa packet-parameter packing (DS SetPacketParams, opcode 0x8C)
// =============================================================================

/// Packs LoRa packet framing into the six SetPacketParams bytes:
/// preamble MSB/LSB, header type, payload length, CRC on, IQ inverted.
[[nodiscard]] constexpr std::array<uint8_t, 6> pack_packet_params(const lora_packet_params& p) noexcept {
    return {
        static_cast<uint8_t>(p.preamble_length >> 8),
        static_cast<uint8_t>(p.preamble_length),
        static_cast<uint8_t>(p.header == header_type::fixed ? 0x01 : 0x00),
        p.payload_length,
        static_cast<uint8_t>(p.crc_on ? 0x01 : 0x00),
        static_cast<uint8_t>(p.invert_iq ? 0x01 : 0x00),
    };
}

// =============================================================================
// TCXO control (DS SetDio3AsTcxoCtrl, opcode 0x97)
// =============================================================================

/// SetDio3AsTcxoCtrl voltage byte for a TCXO supply voltage in mV: the index
/// of the highest supported voltage (1.6/1.7/1.8/2.2/2.4/2.7/3.0/3.3 V) not
/// above @p mv. Voltages below 1.6 V clamp to the 1.6 V byte.
[[nodiscard]] constexpr uint8_t tcxo_voltage_byte(uint16_t mv) noexcept {
    constexpr std::array<uint16_t, 8> supported_mv{1600, 1700, 1800, 2200, 2400, 2700, 3000, 3300};
    uint8_t byte = 0;
    for (uint8_t i = 0; i < supported_mv.size(); ++i) {
        if (mv >= supported_mv[i]) {
            byte = i;
        }
    }
    return byte;
}

// =============================================================================
// LoRa sync word (register 0x0740, 16-bit, MSB first)
// =============================================================================

/// Packs a 16-bit LoRa sync word into the two reg_lora_sync_word_msb bytes.
[[nodiscard]] constexpr std::array<uint8_t, 2> pack_sync_word(uint16_t sync_word) noexcept {
    return {
        static_cast<uint8_t>(sync_word >> 8),
        static_cast<uint8_t>(sync_word),
    };
}

// =============================================================================
// Packet-status decoding (DS GetPacketStatus / GetRssiInst)
// =============================================================================

/// Decodes a GetPacketStatus / GetRssiInst RSSI byte (half-dBm steps: -raw/2 dBm).
[[nodiscard]] constexpr electro::centi_dbm decode_rssi_byte(uint8_t raw) noexcept {
    return electro::centi_dbm(-static_cast<int64_t>(raw) * 50);
}

/// Decodes the three GetPacketStatus response bytes (rssi_pkt, snr_pkt,
/// signal_rssi_pkt; the chip-specific signal_rssi_pkt byte is not surfaced).
[[nodiscard]] constexpr packet_status decode_packet_status(std::span<const uint8_t, 3> r) noexcept {
    return {
        .rssi = decode_rssi_byte(r[0]),
        .snr = electro::centidecibels(static_cast<int8_t>(r[1]) * 25),
    };
}

// =============================================================================
// Duty-cycled receive (DS SetRxDutyCycle, opcode 0x94)
// =============================================================================

/// Converts a duration to SetRxDutyCycle 15.625 µs steps, saturating at the
/// 24-bit field maximum (0xFFFFFF ≈ 262 s). steps = µs / 15.625 = µs · 64 / 1000
/// (the same step math as the DIO3 TCXO timeout). Non-positive durations yield 0.
[[nodiscard]] constexpr uint32_t duty_cycle_steps(std::chrono::microseconds d) noexcept {
    if (d.count() <= 0) {
        return 0;
    }
    const uint64_t steps = static_cast<uint64_t>(d.count()) * 64 / 1000;
    constexpr uint64_t max_steps = 0xFFFFFF;
    return static_cast<uint32_t>(steps > max_steps ? max_steps : steps);
}

/// Packs the six SetRxDutyCycle bytes: rxPeriod (MSB..LSB) then sleepPeriod
/// (MSB..LSB), each a 24-bit count of 15.625 µs steps.
[[nodiscard]] constexpr std::array<uint8_t, 6>
pack_rx_duty_cycle(std::chrono::microseconds rx, std::chrono::microseconds sleep) noexcept {
    const uint32_t rx_steps = duty_cycle_steps(rx);
    const uint32_t sleep_steps = duty_cycle_steps(sleep);
    return {
        static_cast<uint8_t>(rx_steps >> 16),
        static_cast<uint8_t>(rx_steps >> 8),
        static_cast<uint8_t>(rx_steps),
        static_cast<uint8_t>(sleep_steps >> 16),
        static_cast<uint8_t>(sleep_steps >> 8),
        static_cast<uint8_t>(sleep_steps),
    };
}

/// Packs the four SetDio3AsTcxoCtrl bytes: the voltage byte followed by the
/// startup timeout as a 24-bit count of the same 15.625 µs steps SetRxDutyCycle
/// uses (MSB..LSB).
[[nodiscard]] constexpr std::array<uint8_t, 4>
pack_tcxo_params(electro::millivolts voltage, std::chrono::microseconds startup) noexcept {
    const uint32_t timeout_steps = duty_cycle_steps(startup);
    return {
        tcxo_voltage_byte(static_cast<uint16_t>(voltage.count())),
        static_cast<uint8_t>(timeout_steps >> 16),
        static_cast<uint8_t>(timeout_steps >> 8),
        static_cast<uint8_t>(timeout_steps),
    };
}

// =============================================================================
// Frequency math
// =============================================================================

/// SX126x XTAL frequency.
inline constexpr uint64_t xtal_hz = 32'000'000;
/// SetRfFrequency divisor (2^25).
inline constexpr uint64_t freq_divisor = 1ULL << 25;

/// Converts an RF frequency in Hz to the four-byte SetRfFrequency argument.
/// reg = round_down(hz * 2^25 / 32 MHz). E.g. 915 MHz -> 0x39300000.
[[nodiscard]] constexpr uint32_t freq_to_register(uint64_t hz) noexcept {
    return static_cast<uint32_t>((hz * freq_divisor) / xtal_hz);
}

// =============================================================================
// Image calibration band table (DS_SX1261-2 §9.2.1, CalibrateImage)
// =============================================================================

/// The two frequency-band bytes passed to CalibrateImage (opcode 0x98).
struct image_cal_band {
    uint8_t f1;
    uint8_t f2;

    [[nodiscard]] constexpr bool operator==(const image_cal_band&) const noexcept = default;
};

/// Returns the CalibrateImage band bytes for a carrier frequency. The datasheet
/// tabulates exact bytes per ISM band; frequencies outside a named band snap to
/// the nearest documented band (CalibrateImage only needs to bracket the
/// carrier). Bytes are from DS_SX1261-2 §9.2.1.
[[nodiscard]] constexpr image_cal_band calibrate_image_bytes(uint64_t hz) noexcept {
    const uint64_t mhz = hz / 1'000'000;
    if (mhz < 446) {
        return {0x6B, 0x6F}; // 430–440 MHz
    }
    if (mhz < 734) {
        return {0x75, 0x81}; // 470–510 MHz
    }
    if (mhz < 838) {
        return {0xC1, 0xC5}; // 779–787 MHz
    }
    if (mhz < 894) {
        return {0xD7, 0xDB}; // 863–870 MHz
    }
    return {0xE1, 0xE9}; // 902–928 MHz
}

} // namespace idfxx::radio::sx126x_internal
