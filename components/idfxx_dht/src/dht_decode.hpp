// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal: pure decoding of a DHT reply frame captured as RMT symbols.
// Kept free of hardware dependencies so it can be unit-tested (in QEMU)
// against canned captures.

#include <idfxx/dht.hpp>
#include <idfxx/error>

#include <array>
#include <cstdint>
#include <driver/rmt_types.h>
#include <span>

namespace idfxx::dht::internal {

// Symbol durations are interpreted in microseconds: captures must be made
// with a 1 MHz RMT resolution.
inline constexpr uint32_t rmt_resolution_hz = 1'000'000;

// Extracts the 5 frame bytes from a capture.
//
// The capture may include leading symbols from the host's own start pulse
// (the RMT input sees the shared line): decoding starts at the sensor's
// response preamble (~80 us low, ~80 us high) and takes the 40 bit symbols
// that follow (~50 us low, then ~27 us high = 0 / ~70 us high = 1).
//
// Returns errc::timeout when no response preamble is found (the sensor never
// answered the start pulse), errc::invalid_response when a preamble is found
// but fewer than 40 well-formed bit symbols follow it, and errc::invalid_crc
// when the checksum byte does not match.
[[nodiscard]] result<std::array<uint8_t, 5>> decode_frame(std::span<const rmt_symbol_word_t> symbols);

// Converts the 5 frame bytes to a reading using the model's encoding.
[[nodiscard]] reading to_reading(const std::array<uint8_t, 5>& bytes, model m);

} // namespace idfxx::dht::internal
