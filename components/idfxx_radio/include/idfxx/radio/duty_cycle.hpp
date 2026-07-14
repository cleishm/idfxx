// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/radio/duty_cycle>
 * @file duty_cycle.hpp
 * @brief Duty-cycled receive window calculation.
 * @ingroup idfxx_radio
 *
 * A pure, side-effect-free helper that computes listen/sleep windows for
 * duty-cycled receive from the modulation parameters and the preamble length
 * senders use. It needs no chip and performs no I/O, so it is `constexpr` and
 * the windows can be computed at compile time for a fixed link configuration.
 */

#include <idfxx/radio/airtime.hpp>
#include <idfxx/radio/types.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

namespace idfxx::radio {

/// Default shortest sleep window worth duty-cycling for. Below this the radio
/// spends the window transitioning in and out of sleep instead of saving
/// power; chips that restart an oscillator on each wake add that start-up
/// time (e.g. the TCXO delay on SX126x).
inline constexpr std::chrono::microseconds default_min_rx_sleep{1016};

/**
 * @brief Computes duty-cycle receive windows that cannot miss a packet.
 *
 * Chooses listen/sleep windows such that, no matter how a sender's
 * transmission aligns with the cycle, the radio is awake for at least
 * @p min_symbols of the preamble and can detect the packet:
 *
 *     sleep = Tsym · (sender_preamble − 2·min_symbols)
 *     rx    = max( (Tsym·(sender_preamble + 1) − (sleep − 1 ms)) / 2,
 *                  Tsym·(min_symbols + 1) )
 *
 * where `Tsym = 2^SF / BW` is the symbol duration. This is the algorithm
 * RadioLib's `startReceiveDutyCycleAuto` uses. The sleep window is rounded
 * down and the listen window up, so rounding only ever increases the time
 * spent listening.
 *
 * Returns `std::nullopt` when duty-cycling cannot work for the given
 * parameters — when the preamble is too short to sleep inside
 * (`2·min_symbols ≥ sender_preamble`) or the resulting sleep window is
 * shorter than @p min_sleep. The caller should then use continuous receive;
 * passing the result straight to `lora_transceiver::start_listening` does exactly that.
 *
 * @param mod             Modulation parameters (spreading factor and
 *                        bandwidth are used).
 * @param sender_preamble Preamble length, in symbols, that senders on this
 *                        link use (`lora_packet_params::preamble_length`).
 * @param min_symbols     Minimum preamble symbols the radio must observe to
 *                        detect a packet; must be at least 1. Increase for
 *                        margin at the cost of longer listen windows.
 * @param min_sleep       Shortest sleep window worth duty-cycling for.
 *                        Below this the radio spends the window transitioning
 *                        in and out of sleep instead of saving power; add the
 *                        oscillator start-up time (e.g. the TCXO delay on
 *                        SX126x) when the chip uses one.
 * @return The listen/sleep windows, or `std::nullopt` if duty-cycling cannot
 *         reliably catch packets and continuous receive should be used.
 *
 * @code
 * using namespace std::chrono_literals;
 * constexpr idfxx::radio::lora_modulation mod{.sf = idfxx::radio::spreading_factor::sf9};
 * constexpr idfxx::radio::lora_packet_params pkt{.preamble_length = 100};
 *
 * // Falls back to continuous receive automatically when nullopt.
 * radio.start_listening(idfxx::radio::rx_duty_cycle_for(mod, pkt.preamble_length));
 * @endcode
 */
[[nodiscard]] constexpr std::optional<rx_duty_cycle> rx_duty_cycle_for(
    const lora_modulation& mod,
    uint16_t sender_preamble,
    uint16_t min_symbols = 8,
    std::chrono::microseconds min_sleep = default_min_rx_sleep
) noexcept {
    if (min_symbols == 0 || 2 * static_cast<uint32_t>(min_symbols) >= sender_preamble) {
        return std::nullopt;
    }

    // Symbol duration Tsym = 2^SF / BW. Durations are computed as
    // n·2^SF·1e6 / BW in integer arithmetic, rounding the sleep window down
    // (wake early) and the listen window up (listen longer).
    const int64_t bw = airtime_detail::bandwidth_hz(mod.bw);
    const int64_t sym_x_1e6 = (int64_t{1} << std::to_underlying(mod.sf)) * 1'000'000;
    const auto symbols_floor = [&](int64_t n) { return n * sym_x_1e6 / bw; };
    const auto symbols_ceil = [&](int64_t n) { return (n * sym_x_1e6 + bw - 1) / bw; };

    const int64_t sleep_us = symbols_floor(sender_preamble - 2 * min_symbols);
    if (sleep_us < min_sleep.count()) {
        return std::nullopt;
    }

    constexpr int64_t margin_us = 1'000;
    const int64_t worst_case_us = (symbols_ceil(sender_preamble + 1) - (sleep_us - margin_us) + 1) / 2;
    const int64_t detect_us = symbols_ceil(min_symbols + 1);
    const int64_t rx_us = worst_case_us > detect_us ? worst_case_us : detect_us;

    return rx_duty_cycle{std::chrono::microseconds{rx_us}, std::chrono::microseconds{sleep_us}};
}

} // namespace idfxx::radio
