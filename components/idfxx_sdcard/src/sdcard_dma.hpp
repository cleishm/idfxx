// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal: classifies the buffers of raw sector I/O on an SDMMC host slot.
// Kept free of driver dependencies so it can be unit-tested (in QEMU) against
// real addresses.

#include <cstddef>
#include <cstdint>
#include <esp_memory_utils.h>

namespace idfxx::sdcard::internal {

// The SDMMC host's DMA reaches internal DMA-capable RAM, and ESP-IDF handles external RAM (PSRAM) itself: it copies
// a PSRAM buffer through a DMA-capable one, except on targets whose SDMMC DMA reaches PSRAM. ESP-IDF also copies
// misaligned buffers. Any other buffer (in flash, IRAM or RTC memory, for example) ESP-IDF hands to the DMA as it
// is, which the DMA can't reach.
inline bool sdmmc_dma_handles(const void* p) {
    return esp_ptr_dma_capable(p) || esp_ptr_external_ram(p);
}

// True if a buffer of size bytes (more than zero) at p must be copied through a DMA-capable buffer before ESP-IDF's
// SDMMC sector reads and writes can be given it. Both ends are checked, so that a buffer that runs past the end of
// a memory region is copied too.
inline bool sdmmc_needs_dma_copy(const void* p, size_t size) {
    const auto* first = static_cast<const uint8_t*>(p);
    return !sdmmc_dma_handles(first) || !sdmmc_dma_handles(first + (size - 1));
}

} // namespace idfxx::sdcard::internal
