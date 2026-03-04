// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/memory>
 * @file memory.hpp
 * @brief IDFXX memory utilities.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_memory Memory Allocators
 * @brief STL-compatible allocators for ESP-IDF memory regions.
 *
 * Provides allocators for internal DRAM, external PSRAM, and DMA-capable
 * memory that can be used with standard containers.
 * @{
 */

#include <idfxx/error.hpp>

#include <cstddef>
#include <cstdint>
#include <esp_heap_caps.h>
#include <limits>

namespace idfxx {

/**
 * @headerfile <idfxx/memory>
 * @brief Memory region type for heap allocations.
 *
 * Controls where memory is allocated. Use `internal` for default internal
 * DRAM allocation, or `spiram` to allocate from external PSRAM (freeing
 * internal memory for DMA buffers and performance-critical data).
 *
 * Used by components such as @ref idfxx_task (for stack allocation) and
 * @ref idfxx_queue (for queue storage) to control memory placement.
 *
 * @note `spiram` requires a device with external PSRAM and `CONFIG_SPIRAM` enabled.
 */
enum class memory_type : uint32_t {
    internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, ///< Internal DRAM (default)
    spiram = MALLOC_CAP_SPIRAM,                       ///< External PSRAM
};

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible allocator for capability-based memory regions.
 *
 * This allocator uses the ESP-IDF heap_caps API to allocate memory with
 * the specified capability flags. It can be used with standard containers
 * to control memory placement.
 *
 * @tparam T The type of object to allocate.
 * @tparam Caps The ESP-IDF heap capability flags (e.g., `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`).
 */
template<typename T, uint32_t Caps>
struct caps_allocator {
    using value_type = T; /**< The type of object to allocate. */

    /**
     * @brief Rebind the allocator to a different type.
     *
     * @tparam U The new type to allocate.
     */
    template<typename U>
    struct rebind {
        using other = caps_allocator<U, Caps>; /**< The rebound allocator type. */
    };

    /** @brief Default constructor. */
    caps_allocator() = default;

    /**
     * @brief Rebinding copy constructor.
     *
     * @tparam U The source allocator's value type.
     */
    template<typename U>
    constexpr caps_allocator(const caps_allocator<U, Caps>&) noexcept {}

    /**
     * @brief Allocates memory for n objects of type T.
     *
     * @param n The number of objects to allocate space for.
     *
     * @return A pointer to the allocated memory.
     *
     * @note Throws std::bad_alloc only when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     *       When exceptions are disabled, calls abort() on failure.
     * @throws std::bad_alloc If allocation fails and exceptions are enabled.
     */
    [[nodiscard]] T* allocate(size_t n) {
        if (n > std::numeric_limits<size_t>::max() / sizeof(T)) {
            raise_no_mem();
        }
        void* p = heap_caps_malloc(n * sizeof(T), Caps);
        if (!p) {
            raise_no_mem();
        }
        return static_cast<T*>(p);
    }

    /**
     * @brief Deallocates memory previously allocated by this allocator.
     *
     * @param p Pointer to the memory to deallocate.
     */
    void deallocate(T* p, size_t) noexcept { heap_caps_free(p); }
};

/**
 * @brief Equality comparison for caps_allocator.
 *
 * All caps_allocator instances with the same capability flags are considered equal.
 *
 * @return Always returns true.
 */
template<typename T, typename U, uint32_t Caps>
constexpr bool operator==(const caps_allocator<T, Caps>&, const caps_allocator<U, Caps>&) noexcept {
    return true;
}

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible allocator for internal DRAM.
 *
 * Uses the ESP-IDF heap_caps API to allocate memory from internal DRAM
 * with 8-bit access capability. Can be used with standard containers to
 * ensure allocations come from internal memory rather than external PSRAM.
 *
 * @tparam T The type of object to allocate.
 */
template<typename T>
using dram_allocator = caps_allocator<T, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT>;

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible allocator for external PSRAM (SPI RAM).
 *
 * Uses the ESP-IDF heap_caps API to allocate memory from external PSRAM.
 * Can be used with standard containers to place large data structures in
 * PSRAM, freeing internal DRAM for performance-critical or DMA-capable
 * allocations.
 *
 * @tparam T The type of object to allocate.
 *
 * @note Requires a device with external PSRAM and `CONFIG_SPIRAM` enabled.
 */
template<typename T>
using spiram_allocator = caps_allocator<T, MALLOC_CAP_SPIRAM>;

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible allocator for DMA-capable memory.
 *
 * Uses the ESP-IDF heap_caps API to allocate memory that is suitable for
 * DMA transfers. Can be used with standard containers to create buffers
 * for SPI, I2S, LCD, and other DMA-capable peripherals.
 *
 * @tparam T The type of object to allocate.
 */
template<typename T>
using dma_allocator = caps_allocator<T, MALLOC_CAP_DMA>;

/** @} */ // end of idfxx_core_memory
/** @} */ // end of idfxx_core

} // namespace idfxx
