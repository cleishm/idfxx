// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/radio/types>
 * @file types.hpp
 * @brief Chip-agnostic LoRa modulation and packet types.
 * @ingroup idfxx_radio
 */

#include <chrono>
#include <cstdint>
#include <electro/decibel>
#include <frequency/frequency>

/**
 * @headerfile <idfxx/radio/types>
 * @brief LoRa radio types and driver classes.
 */
namespace idfxx::radio {

/**
 * @headerfile <idfxx/radio/types>
 * @brief Top-level radio operating mode.
 *
 * Drivers expose the high-level mode their chip is in. Chip-specific
 * sub-modes (e.g. SX126x's `stdby_rc` vs `stdby_xosc`) are internal to each
 * driver.
 */
enum class chip_mode : uint8_t {
    sleep, ///< Sleep / low-power state.
    stdby, ///< Standby (radio off, registers active).
    tx,    ///< Transmitting.
    rx,    ///< Receiving.
    cad,   ///< Channel-activity detection.
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief LoRa spreading factor.
 *
 * Values are the LoRa spec spreading-factor numbers. Each driver maps these
 * to its chip's register encoding.
 */
enum class spreading_factor : uint8_t {
    sf5 = 5,
    sf6 = 6,
    sf7 = 7,
    sf8 = 8,
    sf9 = 9,
    sf10 = 10,
    sf11 = 11,
    sf12 = 12,
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief LoRa channel bandwidth.
 *
 * Named by the bandwidth in kHz (with underscores for fractional values, so
 * `bw_7_8` is 7.8 kHz). Drivers translate these to chip-specific register
 * values.
 */
enum class bandwidth : uint8_t {
    bw_7_8,   ///< 7.8 kHz
    bw_10_4,  ///< 10.4 kHz
    bw_15_6,  ///< 15.6 kHz
    bw_20_8,  ///< 20.8 kHz
    bw_31_25, ///< 31.25 kHz
    bw_41_7,  ///< 41.7 kHz
    bw_62_5,  ///< 62.5 kHz
    bw_125,   ///< 125 kHz
    bw_250,   ///< 250 kHz
    bw_500,   ///< 500 kHz
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief LoRa forward-error-correction coding rate.
 *
 * The underlying value is the denominator of the 4/N rate (e.g.
 * `cr_4_8` = 8 = "4/8 coding rate").
 */
enum class coding_rate : uint8_t {
    cr_4_5 = 5, ///< 4/5
    cr_4_6 = 6, ///< 4/6
    cr_4_7 = 7, ///< 4/7
    cr_4_8 = 8, ///< 4/8
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief LoRa network selection.
 *
 * Selects the sync word senders and receivers must share to hear each other.
 * Drivers map each value to their chip's native sync-word encoding.
 */
enum class lora_network : uint8_t {
    private_network, ///< Private / ad-hoc point-to-point links.
    public_network,  ///< Public LoRaWAN networks.
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief LoRa packet header mode.
 */
enum class header_type : uint8_t {
    variable, ///< Explicit (variable-length) header.
    fixed,    ///< Implicit (fixed-length) header.
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief Power-amplifier ramp-time bucket.
 *
 * Drivers map each value to the closest supported chip option.
 */
enum class ramp_time : uint8_t {
    us_10,   ///< ~10 µs ramp-up.
    us_40,   ///< ~40 µs ramp-up.
    us_200,  ///< ~200 µs ramp-up.
    us_800,  ///< ~800 µs ramp-up.
    us_3400, ///< ~3.4 ms ramp-up.
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief LoRa modulation parameters.
 */
struct lora_modulation {
    spreading_factor sf = spreading_factor::sf7; ///< Spreading factor.
    bandwidth bw = bandwidth::bw_125;            ///< Channel bandwidth.
    coding_rate cr = coding_rate::cr_4_5;        ///< Forward-error-correction coding rate.
    bool low_data_rate_optimize = false;         ///< Enable low-data-rate optimization (recommended at SF11/SF12 on
                                                 ///< narrow bandwidths).

    /**
     * @brief Compares two modulation-parameter sets for equality.
     */
    [[nodiscard]] constexpr bool operator==(const lora_modulation&) const noexcept = default;
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief LoRa packet framing parameters.
 */
struct lora_packet_params {
    uint16_t preamble_length = 8;               ///< Preamble length in symbols.
    header_type header = header_type::variable; ///< Header mode (variable or fixed).
    uint8_t payload_length = 0xFF;              ///< Payload length, used only in fixed-header mode.
    bool crc_on = true;                         ///< Enable CRC.
    bool invert_iq = false;                     ///< Invert I/Q (for downlink / gateway use).

    /**
     * @brief Compares two packet-framing-parameter sets for equality.
     */
    [[nodiscard]] constexpr bool operator==(const lora_packet_params&) const noexcept = default;
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief Complete LoRa link configuration.
 *
 * Bundles the settings both ends of a link must agree on — frequency,
 * modulation, packet framing, and network — plus this end's transmit power,
 * so they can be applied with a single `lora_transceiver::configure` call.
 */
struct lora_link {
    freq::hertz frequency{0};                            ///< RF carrier frequency (required; no meaningful default).
    electro::dbm output_power{14};                       ///< Transmit output power.
    ramp_time ramp = ramp_time::us_200;                  ///< Output-power ramp-up time.
    lora_modulation modulation{};                        ///< Modulation parameters.
    lora_packet_params packet_params{};                  ///< Packet framing parameters.
    lora_network network = lora_network::public_network; ///< Network (sync word) selection.

    /**
     * @brief Compares two link configurations for equality.
     */
    [[nodiscard]] constexpr bool operator==(const lora_link&) const noexcept = default;
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief Listen/sleep windows for duty-cycled receive.
 *
 * Describes one period of a duty-cycled receive: the radio listens for
 * @ref rx_period, sleeps for @ref sleep_period, and repeats until a packet
 * arrives or the mode is changed. Pass to `lora_transceiver::start_listening`, either
 * built directly or computed from the modulation with @ref rx_duty_cycle_for
 * (`<idfxx/radio/duty_cycle>`).
 */
struct rx_duty_cycle {
    std::chrono::microseconds rx_period;    ///< Time to listen in each cycle.
    std::chrono::microseconds sleep_period; ///< Time to sleep in each cycle.

    /**
     * @brief Compares two listen/sleep window pairs for equality.
     */
    [[nodiscard]] constexpr bool operator==(const rx_duty_cycle&) const noexcept = default;
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief Information about a received packet.
 */
struct rx_info {
    uint8_t length = 0;           ///< Number of bytes received into the caller's buffer.
    electro::centi_dbm rssi{};    ///< Received-signal-strength indicator.
    electro::centidecibels snr{}; ///< Signal-to-noise ratio.

    /**
     * @brief Compares two received-packet descriptions for equality.
     */
    [[nodiscard]] constexpr bool operator==(const rx_info&) const noexcept = default;
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief Result of a channel-activity-detection operation.
 */
struct cad_info {
    bool detected = false; ///< true if LoRa activity was detected on the channel.

    /**
     * @brief Compares two channel-scan results for equality.
     */
    [[nodiscard]] constexpr bool operator==(const cad_info&) const noexcept = default;
};

/**
 * @headerfile <idfxx/radio/types>
 * @brief Detailed status for the most recent packet.
 */
struct packet_status {
    electro::centi_dbm rssi{};    ///< RSSI of the received packet.
    electro::centidecibels snr{}; ///< Signal-to-noise ratio.

    /**
     * @brief Compares two packet-status records for equality.
     */
    [[nodiscard]] constexpr bool operator==(const packet_status&) const noexcept = default;
};

} // namespace idfxx::radio
