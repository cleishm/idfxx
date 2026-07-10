// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/radio/airtime>
 * @file airtime.hpp
 * @brief LoRa time-on-air (air-time) calculation.
 * @ingroup idfxx_radio
 *
 * A pure, side-effect-free helper that computes how long a LoRa packet of a
 * given length will occupy the channel for a set of modulation and framing
 * parameters. It needs no chip and performs no I/O, so it is `constexpr` and
 * can size transmit timeouts, schedule duty cycles, or feed listen-before-talk
 * logic without ever touching hardware.
 */

#include <idfxx/radio/types.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace idfxx::radio {

/// @cond INTERNAL
namespace airtime_detail {

/// Channel bandwidth in Hz. The two fractional bandwidths return their exact
/// LoRa values — 15.625 kHz and 31.25 kHz — rather than the rounded kHz that
/// name the enumerators.
[[nodiscard]] constexpr uint32_t bandwidth_hz(bandwidth bw) noexcept {
    switch (bw) {
    case bandwidth::bw_7_8:
        return 7'800;
    case bandwidth::bw_10_4:
        return 10'400;
    case bandwidth::bw_15_6:
        return 15'625;
    case bandwidth::bw_20_8:
        return 20'800;
    case bandwidth::bw_31_25:
        return 31'250;
    case bandwidth::bw_41_7:
        return 41'700;
    case bandwidth::bw_62_5:
        return 62'500;
    case bandwidth::bw_125:
        return 125'000;
    case bandwidth::bw_250:
        return 250'000;
    case bandwidth::bw_500:
        return 500'000;
    }
    return 125'000; // safe default = 125 kHz
}

/// Number of LoRa symbols carrying the payload — the `8 + ...` term of the
/// Semtech air-time equation. Inputs map to the standard symbols: PL = payload
/// bytes, IH = implicit (fixed) header, CRC = CRC enabled, DE = low-data-rate
/// optimization, CR = coding-rate index 1..4 (cr_4_5..cr_4_8).
///
/// Exact for SF7–SF12. SF5/SF6 reuse the SF7–SF12 coefficients and are
/// therefore approximate (see @ref time_on_air).
[[nodiscard]] constexpr uint32_t payload_symbols(
    spreading_factor sf,
    coding_rate cr,
    header_type header,
    bool crc_on,
    bool low_data_rate_optimize,
    size_t payload_length
) noexcept {
    const int32_t sf_n = static_cast<int32_t>(std::to_underlying(sf));
    const int32_t cr_n = static_cast<int32_t>(std::to_underlying(cr)) - 4; // 1..4
    const int32_t crc = crc_on ? 1 : 0;
    const int32_t ih = (header == header_type::fixed) ? 1 : 0; // implicit header
    const int32_t de = low_data_rate_optimize ? 1 : 0;
    const int32_t pl = static_cast<int32_t>(payload_length);

    // numerator = 8·PL − 4·SF + 28 + 16·CRC − 20·IH. Clamping it to zero here is
    // equivalent to the classic `max(ceil(...)·(CR+4), 0)`: a non-positive
    // numerator contributes no extra symbols beyond the fixed eight.
    int32_t numerator = 8 * pl - 4 * sf_n + 28 + 16 * crc - 20 * ih;
    if (numerator < 0) {
        numerator = 0;
    }
    const int32_t denominator = 4 * (sf_n - 2 * de);
    const int32_t ceil_term = (numerator + denominator - 1) / denominator; // ceil of a non-negative ratio
    return 8u + static_cast<uint32_t>(ceil_term) * static_cast<uint32_t>(cr_n + 4);
}

} // namespace airtime_detail
/// @endcond

/**
 * @brief Computes the time-on-air of a LoRa packet.
 *
 * Returns how long the radio occupies the channel transmitting (or receiving)
 * a packet of @p payload_length bytes under the given modulation and framing,
 * using the classic Semtech air-time equation:
 *
 *     Ts        = 2^SF / BW
 *     nPayload  = 8 + max(ceil((8·PL − 4·SF + 28 + 16·CRC − 20·IH)
 *                              / (4·(SF − 2·DE))) · (CR + 4), 0)
 *     T         = (preamble_length + 4.25)·Ts + nPayload·Ts
 *
 * where DE is `mod.low_data_rate_optimize`, IH is set for a fixed
 * (implicit) header, CRC follows `pkt.crc_on`, and CR is the coding-rate
 * index. The result is rounded to the nearest microsecond. The computation is
 * exact integer arithmetic, so it is usable in `static_assert`.
 *
 * @param mod            Modulation parameters (spreading factor, bandwidth,
 *                       coding rate, low-data-rate optimization).
 * @param pkt            Packet framing (preamble length, header type, CRC).
 *                       `pkt.payload_length` is ignored — @p payload_length is
 *                       the byte count used.
 * @param payload_length Number of payload bytes the packet carries.
 * @return The packet's time-on-air.
 *
 * @note The equation is exact for spreading factors SF7–SF12. SF5 and SF6 reuse
 *       the SF7–SF12 coefficients and so are approximate.
 *       TODO: add the SF5/SF6 special-case coefficients for exact short-range air-time.
 *
 * @code
 * using namespace std::chrono_literals;
 * idfxx::radio::lora_modulation mod{.sf = idfxx::radio::spreading_factor::sf9};
 * idfxx::radio::lora_packet_params pkt{};
 * auto airtime = idfxx::radio::time_on_air(mod, pkt, payload.size());
 * radio.transmit(payload, airtime + 200ms);
 * @endcode
 */
[[nodiscard]] constexpr std::chrono::microseconds
time_on_air(const lora_modulation& mod, const lora_packet_params& pkt, size_t payload_length) noexcept {
    const uint64_t bw = airtime_detail::bandwidth_hz(mod.bw);
    const uint64_t sf = std::to_underlying(mod.sf);
    const uint32_t n_payload = airtime_detail::payload_symbols(
        mod.sf, mod.cr, pkt.header, pkt.crc_on, mod.low_data_rate_optimize, payload_length
    );

    // Total symbol count scaled ×4 so the 4.25-symbol preamble offset (×4 = 17)
    // stays integral.
    const uint64_t n_symbol_x4 = (static_cast<uint64_t>(pkt.preamble_length) + n_payload) * 4 + 17;

    // T = n_symbol · (2^SF / BW). Fold the ×4 of n_symbol_x4 into the divisor and
    // scale by 1e6 µs/s, then round to the nearest microsecond — all in integer
    // arithmetic so the result is deterministic and constexpr-evaluable.
    const uint64_t numerator = n_symbol_x4 * (uint64_t{1} << sf) * 1'000'000ull;
    const uint64_t denominator = 4ull * bw;
    const uint64_t us = (numerator + denominator / 2) / denominator;
    return std::chrono::microseconds{static_cast<int64_t>(us)};
}

} // namespace idfxx::radio
