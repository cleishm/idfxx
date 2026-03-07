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
 * @defgroup idfxx_core_memory Memory
 * @brief Heap capability flags, heap info queries, and STL-compatible allocators.
 *
 * Provides composable memory capability flags via `memory_caps`, heap query
 * functions, heap walking and integrity checking, and allocators (including
 * aligned variants) for internal DRAM, external PSRAM, and DMA-capable
 * memory that can be used with standard containers.
 * @{
 */

#include <idfxx/error.hpp>
#include <idfxx/flags.hpp>

#include <cstddef>
#include <cstdint>
#include <esp_heap_caps.h>
#include <limits>
#include <type_traits>

namespace idfxx {

/**
 * @headerfile <idfxx/memory>
 * @brief Memory capability flags for heap allocations.
 *
 * Composable flags that describe properties of memory regions. Use the
 * bitwise operators provided by `idfxx::flags<memory_caps>` to combine
 * multiple capabilities.
 *
 * Used by components such as @ref idfxx_task (for stack allocation) and
 * @ref idfxx_queue (for queue storage) to control memory placement, and
 * by the heap query functions to filter heap statistics.
 *
 * @note `spiram` requires a device with external PSRAM and `CONFIG_SPIRAM` enabled.
 */
enum class memory_caps : uint32_t {
    exec = MALLOC_CAP_EXEC,                   ///< Executable memory
    access_32bit = MALLOC_CAP_32BIT,          ///< 32-bit aligned access
    access_8bit = MALLOC_CAP_8BIT,            ///< 8/16/32-bit access
    dma = MALLOC_CAP_DMA,                     ///< DMA-capable memory
    spiram = MALLOC_CAP_SPIRAM,               ///< External PSRAM
    internal = MALLOC_CAP_INTERNAL,           ///< Internal memory
    default_heap = MALLOC_CAP_DEFAULT,        ///< Default heap (same as malloc)
    iram = MALLOC_CAP_IRAM_8BIT,              ///< IRAM with unaligned access
    retention = MALLOC_CAP_RETENTION,         ///< Retention DMA accessible
    rtc = MALLOC_CAP_RTCRAM,                  ///< RTC fast memory
    dma_desc_ahb = MALLOC_CAP_DMA_DESC_AHB,   ///< AHB DMA descriptor capable
    dma_desc_axi = MALLOC_CAP_DMA_DESC_AXI,   ///< AXI DMA descriptor capable
    cache_aligned = MALLOC_CAP_CACHE_ALIGNED, ///< Cache-line aligned

    dram = internal | access_8bit, ///< Internal DRAM (8-bit accessible)
};

template<>
inline constexpr bool enable_flags_operators<memory_caps> = true;

/**
 * @headerfile <idfxx/memory>
 * @brief Heap region statistics.
 *
 * Aggregated information about heap regions matching a set of capability flags.
 * Returned by get_heap_info().
 */
struct heap_info {
    size_t total_free_bytes;      ///< Total free bytes across matching regions.
    size_t total_allocated_bytes; ///< Total allocated bytes across matching regions.
    size_t largest_free_block;    ///< Size of the largest contiguous free block.
    size_t minimum_free_bytes;    ///< Minimum free bytes since boot (high-water mark).
    size_t allocated_blocks;      ///< Number of allocated blocks.
    size_t free_blocks;           ///< Number of free blocks.
    size_t total_blocks;          ///< Total number of blocks (allocated + free).
};

/**
 * @headerfile <idfxx/memory>
 * @brief Returns the total size of heap regions matching the given capabilities.
 *
 * @param caps Capability flags to filter heap regions.
 * @return Total size in bytes of all matching heap regions.
 */
[[nodiscard]] inline size_t heap_total_size(flags<memory_caps> caps) noexcept {
    return heap_caps_get_total_size(to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief Returns the current free size of heap regions matching the given capabilities.
 *
 * @param caps Capability flags to filter heap regions.
 * @return Current free bytes across all matching heap regions.
 */
[[nodiscard]] inline size_t heap_free_size(flags<memory_caps> caps) noexcept {
    return heap_caps_get_free_size(to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief Returns the largest free block in heap regions matching the given capabilities.
 *
 * @param caps Capability flags to filter heap regions.
 * @return Size in bytes of the largest contiguous free block.
 */
[[nodiscard]] inline size_t heap_largest_free_block(flags<memory_caps> caps) noexcept {
    return heap_caps_get_largest_free_block(to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief Returns the minimum free size since boot for heap regions matching the given capabilities.
 *
 * This is the high-water mark of heap usage — the lowest amount of free memory
 * recorded since the system booted.
 *
 * @param caps Capability flags to filter heap regions.
 * @return Minimum free bytes since boot across all matching heap regions.
 */
[[nodiscard]] inline size_t heap_minimum_free_size(flags<memory_caps> caps) noexcept {
    return heap_caps_get_minimum_free_size(to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief Returns detailed heap statistics for regions matching the given capabilities.
 *
 * @param caps Capability flags to filter heap regions.
 * @return A heap_info struct containing aggregated statistics.
 */
[[nodiscard]] inline heap_info get_heap_info(flags<memory_caps> caps) noexcept {
    multi_heap_info_t info{};
    heap_caps_get_info(&info, to_underlying(caps));
    return {
        .total_free_bytes = info.total_free_bytes,
        .total_allocated_bytes = info.total_allocated_bytes,
        .largest_free_block = info.largest_free_block,
        .minimum_free_bytes = info.minimum_free_bytes,
        .allocated_blocks = info.allocated_blocks,
        .free_blocks = info.free_blocks,
        .total_blocks = info.total_blocks,
    };
}

// ---- Heap allocation --------------------------------------------------------

/**
 * @headerfile <idfxx/memory>
 * @brief Allocates memory from heap regions matching the given capabilities.
 *
 * @param size Number of bytes to allocate.
 * @param caps Capability flags to select heap regions.
 * @return Pointer to the allocated memory, or `nullptr` on failure.
 */
[[nodiscard]] inline void* heap_malloc(size_t size, flags<memory_caps> caps) noexcept {
    return heap_caps_malloc(size, to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief Allocates zero-initialized memory from heap regions matching the given capabilities.
 *
 * @param n Number of elements to allocate.
 * @param size Size of each element in bytes.
 * @param caps Capability flags to select heap regions.
 * @return Pointer to the zero-initialized memory, or `nullptr` on failure.
 */
[[nodiscard]] inline void* heap_calloc(size_t n, size_t size, flags<memory_caps> caps) noexcept {
    return heap_caps_calloc(n, size, to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief Reallocates memory from heap regions matching the given capabilities.
 *
 * If `ptr` is `nullptr`, behaves like heap_malloc(). The returned pointer may
 * differ from `ptr`.
 *
 * @param ptr Pointer to previously allocated memory, or `nullptr`.
 * @param size New size in bytes.
 * @param caps Capability flags to select heap regions.
 * @return Pointer to the reallocated memory, or `nullptr` on failure.
 */
[[nodiscard]] inline void* heap_realloc(void* ptr, size_t size, flags<memory_caps> caps) noexcept {
    return heap_caps_realloc(ptr, size, to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief Frees memory previously allocated by heap allocation functions.
 *
 * Accepts `nullptr` safely. Works for both regular and aligned allocations.
 *
 * @param ptr Pointer to the memory to free, or `nullptr`.
 */
inline void heap_free(void* ptr) noexcept {
    heap_caps_free(ptr);
}

/**
 * @headerfile <idfxx/memory>
 * @brief Allocates aligned memory from heap regions matching the given capabilities.
 *
 * @param alignment Alignment in bytes (must be a power of two).
 * @param size Number of bytes to allocate.
 * @param caps Capability flags to select heap regions.
 * @return Pointer to the aligned memory, or `nullptr` on failure.
 */
[[nodiscard]] inline void* heap_aligned_alloc(size_t alignment, size_t size, flags<memory_caps> caps) noexcept {
    return heap_caps_aligned_alloc(alignment, size, to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief Allocates aligned, zero-initialized memory from heap regions matching the given capabilities.
 *
 * @param alignment Alignment in bytes (must be a power of two).
 * @param n Number of elements to allocate.
 * @param size Size of each element in bytes.
 * @param caps Capability flags to select heap regions.
 * @return Pointer to the aligned, zero-initialized memory, or `nullptr` on failure.
 */
[[nodiscard]] inline void*
heap_aligned_calloc(size_t alignment, size_t n, size_t size, flags<memory_caps> caps) noexcept {
    return heap_caps_aligned_calloc(alignment, n, size, to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible allocator for capability-based memory regions.
 *
 * Allocates memory from heap regions matching the specified capability flags.
 * Can be used with standard containers to control memory placement.
 *
 * When `Alignment` is non-zero, allocations are guaranteed to be aligned to
 * the specified byte boundary. The alignment must be a power of two.
 *
 * @tparam T The type of object to allocate.
 * @tparam Caps The heap capability flags (e.g., `memory_caps::dram`).
 * @tparam Alignment Allocation alignment in bytes (0 = default alignment, must be power of two when non-zero).
 */
template<typename T, flags<memory_caps> Caps, size_t Alignment = 0>
struct caps_allocator {
    using value_type = T; /**< The type of object to allocate. */

    /**
     * @brief Rebind the allocator to a different type.
     *
     * @tparam U The new type to allocate.
     */
    template<typename U>
    struct rebind {
        using other = caps_allocator<U, Caps, Alignment>; /**< The rebound allocator type. */
    };

    /** @brief Default constructor. */
    caps_allocator() = default;

    /**
     * @brief Rebinding copy constructor.
     *
     * @tparam U The source allocator's value type.
     */
    template<typename U>
    constexpr caps_allocator(const caps_allocator<U, Caps, Alignment>&) noexcept {}

    /**
     * @brief Allocates memory for n objects of type T.
     *
     * When `Alignment` is non-zero, the returned pointer is guaranteed to be
     * aligned to `Alignment` bytes.
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
        void* p;
        if constexpr (Alignment == 0) {
            p = heap_caps_malloc(n * sizeof(T), to_underlying(Caps));
        } else {
            static_assert((Alignment & (Alignment - 1)) == 0, "Alignment must be a power of two");
            p = heap_caps_aligned_alloc(Alignment, n * sizeof(T), to_underlying(Caps));
        }
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
 * All caps_allocator instances with the same capability flags and alignment are considered equal.
 *
 * @return Always returns true.
 */
template<typename T, typename U, flags<memory_caps> Caps, size_t Alignment>
constexpr bool
operator==(const caps_allocator<T, Caps, Alignment>&, const caps_allocator<U, Caps, Alignment>&) noexcept {
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
using dram_allocator = caps_allocator<T, memory_caps::dram>;

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
using spiram_allocator = caps_allocator<T, memory_caps::spiram>;

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
using dma_allocator = caps_allocator<T, memory_caps::dma>;

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible aligned allocator for internal DRAM.
 *
 * Allocates from internal DRAM with a guaranteed byte alignment.
 *
 * @tparam T The type of object to allocate.
 * @tparam Alignment Alignment in bytes (must be a power of two).
 */
template<typename T, size_t Alignment>
using aligned_dram_allocator = caps_allocator<T, memory_caps::dram, Alignment>;

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible aligned allocator for external PSRAM (SPI RAM).
 *
 * Allocates from external PSRAM with a guaranteed byte alignment.
 *
 * @tparam T The type of object to allocate.
 * @tparam Alignment Alignment in bytes (must be a power of two).
 *
 * @note Requires a device with external PSRAM and `CONFIG_SPIRAM` enabled.
 */
template<typename T, size_t Alignment>
using aligned_spiram_allocator = caps_allocator<T, memory_caps::spiram, Alignment>;

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible aligned allocator for DMA-capable memory.
 *
 * Allocates DMA-capable memory with a guaranteed byte alignment.
 *
 * @tparam T The type of object to allocate.
 * @tparam Alignment Alignment in bytes (must be a power of two).
 */
template<typename T, size_t Alignment>
using aligned_dma_allocator = caps_allocator<T, memory_caps::dma, Alignment>;

// ---- Heap walking -----------------------------------------------------------

/**
 * @headerfile <idfxx/memory>
 * @brief Information about a heap region, passed to heap walk callbacks.
 */
struct heap_region {
    intptr_t start; ///< Start address of the heap region.
    intptr_t end;   ///< End address of the heap region.
};

/**
 * @headerfile <idfxx/memory>
 * @brief Information about a single heap block, passed to heap walk callbacks.
 */
struct heap_block {
    void* ptr;   ///< Pointer to the block data.
    size_t size; ///< Size of the block in bytes.
    bool used;   ///< True if allocated, false if free.
};

/**
 * @headerfile <idfxx/memory>
 * @brief Walk all heap blocks in regions matching the given capabilities.
 *
 * Iterates over every block in heaps that match `caps`, invoking `walker`
 * for each block. The walker receives a `heap_region` describing the
 * containing region and a `heap_block` describing the individual block.
 *
 * @tparam F Callable with signature `bool(heap_region, heap_block)`.
 * @param caps Capability flags to filter heap regions.
 * @param walker Callable invoked for each block. Return true to continue, false to stop.
 */
template<typename F>
void heap_walk(flags<memory_caps> caps, F&& walker) {
    auto cb = [](walker_heap_into_t heap_info, walker_block_info_t block_info, void* user_data) -> bool {
        auto& fn = *static_cast<std::remove_reference_t<F>*>(user_data);
        return fn(
            heap_region{heap_info.start, heap_info.end}, heap_block{block_info.ptr, block_info.size, block_info.used}
        );
    };
    heap_caps_walk(to_underlying(caps), cb, &walker);
}

/**
 * @headerfile <idfxx/memory>
 * @brief Walk all heap blocks across all heaps.
 *
 * Iterates over every block in all registered heaps, invoking `walker`
 * for each block.
 *
 * @tparam F Callable with signature `bool(heap_region, heap_block)`.
 * @param walker Callable invoked for each block. Return true to continue, false to stop.
 */
template<typename F>
void heap_walk_all(F&& walker) {
    auto cb = [](walker_heap_into_t heap_info, walker_block_info_t block_info, void* user_data) -> bool {
        auto& fn = *static_cast<std::remove_reference_t<F>*>(user_data);
        return fn(
            heap_region{heap_info.start, heap_info.end}, heap_block{block_info.ptr, block_info.size, block_info.used}
        );
    };
    heap_caps_walk_all(cb, &walker);
}

// ---- Heap integrity checking ------------------------------------------------

/**
 * @headerfile <idfxx/memory>
 * @brief Check integrity of all heaps with the given capabilities.
 *
 * @param caps Capability flags to filter heap regions.
 * @param print_errors Print specific errors if heap corruption is found.
 * @return True if all matching heaps are valid, false if at least one is corrupt.
 */
[[nodiscard]] inline bool heap_check_integrity(flags<memory_caps> caps, bool print_errors = false) noexcept {
    return heap_caps_check_integrity(to_underlying(caps), print_errors);
}

/**
 * @headerfile <idfxx/memory>
 * @brief Check integrity of all heaps.
 *
 * @param print_errors Print specific errors if heap corruption is found.
 * @return True if all heaps are valid, false if at least one is corrupt.
 */
[[nodiscard]] inline bool heap_check_integrity_all(bool print_errors = false) noexcept {
    return heap_caps_check_integrity_all(print_errors);
}

// ---- Heap dump --------------------------------------------------------------

/**
 * @headerfile <idfxx/memory>
 * @brief Dump the structure of all heaps with matching capabilities.
 *
 * Prints detailed information about every block in matching heaps to the serial console.
 *
 * @param caps Capability flags to filter heap regions.
 */
inline void heap_dump(flags<memory_caps> caps) noexcept {
    heap_caps_dump(to_underlying(caps));
}

/**
 * @headerfile <idfxx/memory>
 * @brief Dump the structure of all heaps.
 *
 * Prints detailed information about every block in all heaps to the serial console.
 */
inline void heap_dump_all() noexcept {
    heap_caps_dump_all();
}

/** @} */ // end of idfxx_core_memory
/** @} */ // end of idfxx_core

} // namespace idfxx
