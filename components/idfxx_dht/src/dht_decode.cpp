// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "dht_decode.hpp"

namespace idfxx::dht::internal {

namespace {

// The capture's phase depends on the line level at the instant RMT RX starts:
// symbols arrive as (low, high) pairs when it started high, but as (high, low)
// pairs when it caught the tail of the host's start pulse (seen on the XIAO
// ESP32-S3). Decode from the flattened half-symbol sequence instead of
// assuming either pairing. A duration of 0 means the line held that level to
// the idle threshold (only ever the capture's final half).

struct half {
    uint8_t level;
    uint16_t duration;
};

constexpr half half_at(std::span<const rmt_symbol_word_t> symbols, size_t h) {
    const auto& s = symbols[h / 2];
    return (h % 2) == 0 ? half{static_cast<uint8_t>(s.level0), s.duration0}
                        : half{static_cast<uint8_t>(s.level1), s.duration1};
}

// Sensor response preamble: ~80 us low, then ~80 us high.
constexpr bool is_preamble(const half& lo, const half& hi) {
    return lo.level == 0 && hi.level == 1 && lo.duration >= 60 && lo.duration <= 100 && hi.duration >= 60 &&
        hi.duration <= 100;
}

// Data bit: ~50 us low, then ~26–28 us high (0) or ~70 us high (1).
constexpr bool is_bit(const half& lo, const half& hi) {
    return lo.level == 0 && hi.level == 1 && lo.duration >= 30 && lo.duration <= 70 && hi.duration >= 10 &&
        hi.duration <= 100;
}

} // namespace

result<std::array<uint8_t, 5>> decode_frame(std::span<const rmt_symbol_word_t> symbols) {
    const size_t halves = symbols.size() * 2;

    // Find the response preamble at any phase, skipping anything the capture
    // picked up from the host's own start pulse.
    size_t start = 0;
    while (start + 1 < halves && !is_preamble(half_at(symbols, start), half_at(symbols, start + 1))) {
        ++start;
    }
    if (start + 1 >= halves) {
        // No response preamble anywhere: the sensor never pulled the line low
        // to acknowledge the start pulse, i.e. it did not answer.
        return error(errc::timeout);
    }
    // Preamble + 40 bits = 82 halves from `start`.
    if (halves - start < 82) {
        // A preamble was seen but too few halves follow it to hold 40 bits.
        return error(errc::invalid_response);
    }

    std::array<uint8_t, 5> bytes{};
    for (size_t i = 0; i < 40; ++i) {
        const half lo = half_at(symbols, start + 2 + 2 * i);
        const half hi = half_at(symbols, start + 3 + 2 * i);
        if (!is_bit(lo, hi)) {
            return error(errc::invalid_response);
        }
        bytes[i / 8] = static_cast<uint8_t>((bytes[i / 8] << 1) | (hi.duration > 48 ? 1 : 0));
    }

    if (bytes[4] != static_cast<uint8_t>(bytes[0] + bytes[1] + bytes[2] + bytes[3])) {
        return error(errc::invalid_crc);
    }
    return bytes;
}

reading to_reading(const std::array<uint8_t, 5>& b, model m) {
    reading r{};
    int32_t temp_tenths = 0; // signed tenths of a degree Celsius
    if (m == model::dht22) {
        // 16-bit tenths, temperature in sign-magnitude form.
        const auto hum = static_cast<uint16_t>((b[0] << 8) | b[1]);
        const auto mag = static_cast<int32_t>(((b[2] & 0x7F) << 8) | b[3]);
        temp_tenths = (b[2] & 0x80) ? -mag : mag;
        r.humidity_pct = static_cast<float>(hum) / 10.0f;
    } else {
        // Integral degrees / percent, with tenths in the second byte on
        // DHT12-style parts (zero on a plain DHT11).
        const int32_t mag = b[2] * 10 + (b[3] & 0x0F);
        temp_tenths = (b[3] & 0x80) ? -mag : mag;
        r.humidity_pct = static_cast<float>(b[0]) + static_cast<float>(b[1]) / 10.0f;
    }
    r.temperature = thermo::millicelsius(static_cast<int64_t>(temp_tenths) * 100);
    return r;
}

} // namespace idfxx::dht::internal
