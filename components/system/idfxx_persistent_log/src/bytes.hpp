// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Little-endian fields and CRCs for the layouts that the component stores in flash. This header is
// private to the component's sources.

#include "sector_store.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <esp_rom_crc.h>
#include <span>

namespace idfxx::persistent_log::detail {

inline constexpr std::byte erased{0xFF};

inline uint32_t crc32(uint32_t crc, std::span<const std::byte> bytes) {
    return esp_rom_crc32_le(
        crc, reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size())
    );
}

inline void put_u16(std::byte* p, uint16_t v) {
    p[0] = std::byte(v & 0xFF);
    p[1] = std::byte(v >> 8);
}

inline void put_u32(std::byte* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = std::byte((v >> (8 * i)) & 0xFF);
    }
}

inline void put_u64(std::byte* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = std::byte((v >> (8 * i)) & 0xFF);
    }
}

inline uint16_t get_u16(const std::byte* p) {
    return static_cast<uint16_t>(
        std::to_integer<unsigned>(p[0]) | std::to_integer<unsigned>(p[1]) << 8
    );
}

inline uint32_t get_u32(const std::byte* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= std::to_integer<uint32_t>(p[i]) << (8 * i);
    }
    return v;
}

inline uint64_t get_u64(const std::byte* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= std::to_integer<uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

inline bool all_erased(std::span<const std::byte> bytes) {
    return std::ranges::all_of(bytes, [](std::byte b) { return b == erased; });
}

// `n` rounded up to a whole number of blocks.
inline constexpr size_t round_up_block(size_t n) {
    return (n + block_size - 1) / block_size * block_size;
}

// True when the store can take the complete, aligned blocks that the log and the fault store write.
// That is the case when its sector size is a multiple of the block size, and its write size divides
// the block size.
inline bool takes_blocks(const sector_store& store) {
    size_t unit = store.write_size();
    return store.sector_size() % block_size == 0 && unit != 0 && block_size % unit == 0;
}

// True when the bytes begin with a 4-character magic.
inline bool has_magic(std::span<const std::byte> bytes, const char (&magic)[5]) {
    for (size_t i = 0; i < 4; ++i) {
        if (bytes[i] != std::byte(magic[i])) {
            return false;
        }
    }
    return true;
}

inline void put_magic(std::byte* p, const char (&magic)[5]) {
    for (size_t i = 0; i < 4; ++i) {
        p[i] = std::byte(magic[i]);
    }
}

} // namespace idfxx::persistent_log::detail
