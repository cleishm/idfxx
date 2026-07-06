// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_dht
// The frame decoder is pure (no hardware), so these run everywhere,
// including QEMU, against canned RMT captures.

#include "../src/dht_decode.hpp"
#include "idfxx/dht"
#include "unity.h"

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

using namespace idfxx::dht;
namespace decode = idfxx::dht::internal;

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

// sensor is move-only.
static_assert(!std::is_copy_constructible_v<sensor>);
static_assert(!std::is_copy_assignable_v<sensor>);
static_assert(std::is_move_constructible_v<sensor>);
static_assert(std::is_move_assignable_v<sensor>);

// config is default-constructible (every field has a default).
static_assert(std::is_default_constructible_v<sensor::config>);

// Captures are decoded at microsecond resolution.
static_assert(decode::rmt_resolution_hz == 1'000'000);

// =============================================================================
// Canned-capture helpers
// =============================================================================

namespace {

rmt_symbol_word_t sym(uint16_t low_us, uint16_t high_us, uint16_t level1 = 1) {
    rmt_symbol_word_t s{};
    s.duration0 = low_us;
    s.level0 = 0;
    s.duration1 = high_us;
    s.level1 = level1;
    return s;
}

// Builds a full capture as the driver sees it: the tail of the host's own
// start pulse, the sensor's response preamble, 40 data bits (MSB first), and
// the sensor's closing low.
std::vector<rmt_symbol_word_t> frame_symbols(const std::array<uint8_t, 5>& bytes, bool include_start = true) {
    std::vector<rmt_symbol_word_t> v;
    if (include_start) {
        v.push_back(sym(15, 35)); // remainder of start pulse, release high
    }
    v.push_back(sym(82, 84)); // response preamble
    for (int i = 0; i < 40; ++i) {
        const bool one = (bytes[i / 8] >> (7 - i % 8)) & 1;
        v.push_back(sym(52, one ? 70 : 26));
    }
    v.push_back(sym(48, 0, 0)); // closing low, line then idles high
    return v;
}

constexpr std::array<uint8_t, 5> checksummed(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    return {b0, b1, b2, b3, static_cast<uint8_t>(b0 + b1 + b2 + b3)};
}

// The same frame with the opposite capture phase, as observed on the XIAO
// ESP32-S3 (gate bring-up, 2026-07-05): RMT RX started while the host's
// start pulse still held the line low, so every symbol is a (high, low)
// pair — e.g. the response preamble's 80 us high lands in the same symbol
// as the first bit's 50 us low.
std::vector<rmt_symbol_word_t> frame_symbols_high_first(const std::array<uint8_t, 5>& bytes) {
    auto hl = [](uint16_t high_us, uint16_t low_us, uint16_t level1 = 0) {
        rmt_symbol_word_t s{};
        s.duration0 = high_us;
        s.level0 = 1;
        s.duration1 = low_us;
        s.level1 = level1;
        return s;
    };
    std::vector<rmt_symbol_word_t> v;
    v.push_back(hl(15, 78)); // release-high tail, then response low
    uint16_t prev_high = 81; // response high pairs with the first bit's low
    for (int i = 0; i < 40; ++i) {
        const bool one = (bytes[i / 8] >> (7 - i % 8)) & 1;
        v.push_back(hl(prev_high, 50));
        prev_high = one ? 70 : 25;
    }
    v.push_back(hl(prev_high, 48)); // last bit's high, closing low
    return v;
}

} // namespace

// =============================================================================
// Frame decoding
// =============================================================================

TEST_CASE("dht decode_frame extracts bytes from a clean capture", "[idfxx][dht]") {
    // 65.2 %RH, 23.7 C as a DHT22 frame.
    const auto bytes = checksummed(0x02, 0x8C, 0x00, 0xED);
    auto capture = frame_symbols(bytes);
    auto decoded = decode::decode_frame(capture);
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), decoded->data(), 5);
}

TEST_CASE("dht decode_frame works without leading start-pulse symbols", "[idfxx][dht]") {
    const auto bytes = checksummed(0x02, 0x8C, 0x00, 0xED);
    auto capture = frame_symbols(bytes, /*include_start=*/false);
    auto decoded = decode::decode_frame(capture);
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), decoded->data(), 5);
}

TEST_CASE("dht decode_frame handles a high-first capture phase", "[idfxx][dht]") {
    const auto bytes = checksummed(0x02, 0x8C, 0x00, 0xED);
    auto capture = frame_symbols_high_first(bytes);
    auto decoded = decode::decode_frame(capture);
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), decoded->data(), 5);
}

TEST_CASE("dht decode_frame rejects a bad checksum", "[idfxx][dht]") {
    auto bytes = checksummed(0x02, 0x8C, 0x00, 0xED);
    bytes[4] ^= 0x01;
    auto capture = frame_symbols(bytes);
    auto decoded = decode::decode_frame(capture);
    TEST_ASSERT_FALSE(decoded.has_value());
    TEST_ASSERT_TRUE(decoded.error() == idfxx::errc::invalid_crc);
}

TEST_CASE("dht decode_frame rejects a truncated frame", "[idfxx][dht]") {
    const auto bytes = checksummed(0x02, 0x8C, 0x00, 0xED);
    auto capture = frame_symbols(bytes);
    capture.resize(capture.size() - 10);
    auto decoded = decode::decode_frame(capture);
    TEST_ASSERT_FALSE(decoded.has_value());
    TEST_ASSERT_TRUE(decoded.error() == idfxx::errc::invalid_response);
}

TEST_CASE("dht decode_frame reports timeout when no preamble (sensor silent)", "[idfxx][dht]") {
    // No preamble anywhere means the sensor never answered the start pulse.
    std::vector<rmt_symbol_word_t> capture(45, sym(15, 20));
    auto decoded = decode::decode_frame(capture);
    TEST_ASSERT_FALSE(decoded.has_value());
    TEST_ASSERT_TRUE(decoded.error() == idfxx::errc::timeout);
}

TEST_CASE("dht decode_frame rejects a malformed bit symbol", "[idfxx][dht]") {
    const auto bytes = checksummed(0x02, 0x8C, 0x00, 0xED);
    auto capture = frame_symbols(bytes);
    capture[10] = sym(150, 150); // way out of bit range
    auto decoded = decode::decode_frame(capture);
    TEST_ASSERT_FALSE(decoded.has_value());
    TEST_ASSERT_TRUE(decoded.error() == idfxx::errc::invalid_response);
}

// =============================================================================
// Reading conversion
// =============================================================================

TEST_CASE("dht22 conversion: positive temperature", "[idfxx][dht]") {
    auto r = decode::to_reading(checksummed(0x02, 0x8C, 0x00, 0xED), model::dht22);
    TEST_ASSERT_EQUAL_INT32(23'700, static_cast<int32_t>(r.temperature.count()));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.2f, r.humidity_pct);
}

TEST_CASE("dht22 conversion: negative temperature is sign-magnitude", "[idfxx][dht]") {
    // -3.4 C: magnitude 34 with the sign bit set in the temperature MSB.
    auto r = decode::to_reading(checksummed(0x01, 0xF4, 0x80, 0x22), model::dht22);
    TEST_ASSERT_EQUAL_INT32(-3'400, static_cast<int32_t>(r.temperature.count()));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, r.humidity_pct);
}

TEST_CASE("dht11 conversion: integral degrees and percent", "[idfxx][dht]") {
    auto r = decode::to_reading(checksummed(45, 0, 21, 0), model::dht11);
    TEST_ASSERT_EQUAL_INT32(21'000, static_cast<int32_t>(r.temperature.count()));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, r.humidity_pct);
}

TEST_CASE("dht model to_string", "[idfxx][dht]") {
    TEST_ASSERT_EQUAL_STRING("DHT11", idfxx::to_string(model::dht11).c_str());
    TEST_ASSERT_EQUAL_STRING("DHT22", idfxx::to_string(model::dht22).c_str());
}
