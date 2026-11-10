// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal: how erases are split into commands. Shared with the tests.

#include <cstddef>

namespace idfxx::sdcard::internal {

// Erases are issued in chunks that start and end on multiples of this many sectors (1 GiB with 512-byte sectors).
// sdmmc_erase_sectors() computes the erase timeout from the range's size with 32-bit arithmetic, which overflows on
// larger ranges: the byte count does at 2^23 sectors, and so can the card-estimated timeout in milliseconds at 2^22
// sectors (an SD card can report up to 63 s per 16 KiB allocation unit).
inline constexpr size_t erase_chunk_sectors = size_t{1} << 21;

} // namespace idfxx::sdcard::internal
