// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/radio/events>
 * @file events.hpp
 * @brief Chip-agnostic radio event base and typed events.
 * @ingroup idfxx_radio
 */

#include <idfxx/event>
#include <idfxx/radio/types.hpp>

#include <cstdint>

namespace idfxx::radio {

/**
 * @headerfile <idfxx/radio/events>
 * @brief Event IDs posted by a radio driver.
 */
enum class event_id : uint8_t {
    tx_done,           ///< Transmit complete.
    rx_done,           ///< Receive complete (carries @ref rx_info).
    crc_error,         ///< Received packet had a CRC error.
    cad_done,          ///< Channel scan finished (carries @ref cad_info).
    preamble_detected, ///< Preamble detected during continuous RX.
};

/**
 * @brief Event base for all radio events.
 *
 * Subscribe to this base on a concrete driver's event loop:
 *
 * @code
 * loop.listener_add(idfxx::radio::rx_done,
 *     [](const idfxx::radio::rx_info& info) {
 *         // ...
 *     });
 * @endcode
 */
IDFXX_EVENT_DEFINE_BASE(radio_events, event_id);

/** @brief Transmit-complete event. */
inline constexpr event<event_id> tx_done{event_id::tx_done};

/**
 * @brief Receive-complete event, carrying the packet's @ref rx_info.
 *
 * The payload describes the packet that raised the event, which suits
 * metadata-only listeners (e.g. signal monitoring). To read the packet's
 * bytes, call `lora_transceiver::read_received` and pair the bytes with the
 * @ref rx_info *it* returns — the receive cache holds only the most recent
 * packet, which may already be newer than the one that raised this event.
 */
inline constexpr event<event_id, rx_info> rx_done{event_id::rx_done};

/** @brief CRC-error event. */
inline constexpr event<event_id> crc_error{event_id::crc_error};

/** @brief Channel-scan-complete event, carrying @ref cad_info. */
inline constexpr event<event_id, cad_info> cad_done{event_id::cad_done};

/** @brief Preamble-detected event. */
inline constexpr event<event_id> preamble_detected{event_id::preamble_detected};

} // namespace idfxx::radio
