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

#include <cstddef>
#include <cstdint>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <new>

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
 * @brief STL-compatible allocator for internal DRAM.
 *
 * This allocator uses the ESP-IDF heap_caps API to allocate memory from
 * internal DRAM with 8-bit access capability. It can be used with standard
 * containers to ensure allocations come from internal memory rather than
 * external PSRAM.
 *
 * @tparam T The type of object to allocate.
 */
template<typename T>
struct dram_allocator {
    using value_type = T; /**< The type of object to allocate. */

    /** @brief Default constructor. */
    dram_allocator() = default;

    /**
     * @brief Rebinding copy constructor.
     *
     * @tparam U The source allocator's value type.
     */
    template<typename U>
    constexpr dram_allocator(const dram_allocator<U>&) noexcept {}

    /**
     * @brief Allocates memory for n objects of type T from internal DRAM.
     *
     * @param n The number of objects to allocate space for.
     *
     * @return A pointer to the allocated memory.
     *
     * @note Throws std::bad_alloc only when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     *       When exceptions are disabled, calls esp_system_abort() on failure.
     * @throws std::bad_alloc If allocation fails and exceptions are enabled.
     */
    [[nodiscard]] T* allocate(size_t n) {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!p) {
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
            throw std::bad_alloc();
#else
            esp_system_abort("dram_allocator: allocation failed");
#endif
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
 * @brief Equality comparison for dram_allocator.
 *
 * All dram_allocator instances are considered equal.
 *
 * @return Always returns true.
 */
template<typename T, typename U>
bool operator==(const dram_allocator<T>&, const dram_allocator<U>&) {
    return true;
}

/**
 * @brief Inequality comparison for dram_allocator.
 *
 * All dram_allocator instances are considered equal.
 *
 * @return Always returns false.
 */
template<typename T, typename U>
bool operator!=(const dram_allocator<T>&, const dram_allocator<U>&) {
    return false;
}

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible allocator for external PSRAM (SPI RAM).
 *
 * This allocator uses the ESP-IDF heap_caps API to allocate memory from
 * external PSRAM. It can be used with standard containers to place large
 * data structures in PSRAM, freeing internal DRAM for performance-critical
 * or DMA-capable allocations.
 *
 * @tparam T The type of object to allocate.
 *
 * @note Requires a device with external PSRAM and `CONFIG_SPIRAM` enabled.
 */
template<typename T>
struct spiram_allocator {
    using value_type = T; /**< The type of object to allocate. */

    /** @brief Default constructor. */
    spiram_allocator() = default;

    /**
     * @brief Rebinding copy constructor.
     *
     * @tparam U The source allocator's value type.
     */
    template<typename U>
    constexpr spiram_allocator(const spiram_allocator<U>&) noexcept {}

    /**
     * @brief Allocates memory for n objects of type T from external PSRAM.
     *
     * @param n The number of objects to allocate space for.
     *
     * @return A pointer to the allocated memory.
     *
     * @note Throws std::bad_alloc only when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     *       When exceptions are disabled, calls esp_system_abort() on failure.
     * @throws std::bad_alloc If allocation fails and exceptions are enabled.
     */
    [[nodiscard]] T* allocate(size_t n) {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
        if (!p) {
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
            throw std::bad_alloc();
#else
            esp_system_abort("spiram_allocator: allocation failed");
#endif
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
 * @brief Equality comparison for spiram_allocator.
 *
 * All spiram_allocator instances are considered equal.
 *
 * @return Always returns true.
 */
template<typename T, typename U>
bool operator==(const spiram_allocator<T>&, const spiram_allocator<U>&) {
    return true;
}

/**
 * @brief Inequality comparison for spiram_allocator.
 *
 * All spiram_allocator instances are considered equal.
 *
 * @return Always returns false.
 */
template<typename T, typename U>
bool operator!=(const spiram_allocator<T>&, const spiram_allocator<U>&) {
    return false;
}

/**
 * @headerfile <idfxx/memory>
 * @brief STL-compatible allocator for DMA-capable memory.
 *
 * This allocator uses the ESP-IDF heap_caps API to allocate memory that
 * is suitable for DMA transfers. It can be used with standard containers
 * to create buffers for SPI, I2S, LCD, and other DMA-capable peripherals.
 *
 * @tparam T The type of object to allocate.
 */
template<typename T>
struct dma_allocator {
    using value_type = T; /**< The type of object to allocate. */

    /** @brief Default constructor. */
    dma_allocator() = default;

    /**
     * @brief Rebinding copy constructor.
     *
     * @tparam U The source allocator's value type.
     */
    template<typename U>
    constexpr dma_allocator(const dma_allocator<U>&) noexcept {}

    /**
     * @brief Allocates DMA-capable memory for n objects of type T.
     *
     * @param n The number of objects to allocate space for.
     *
     * @return A pointer to the allocated memory.
     *
     * @note Throws std::bad_alloc only when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     *       When exceptions are disabled, calls esp_system_abort() on failure.
     * @throws std::bad_alloc If allocation fails and exceptions are enabled.
     */
    [[nodiscard]] T* allocate(size_t n) {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_DMA);
        if (!p) {
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
            throw std::bad_alloc();
#else
            esp_system_abort("dma_allocator: allocation failed");
#endif
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
 * @brief Equality comparison for dma_allocator.
 *
 * All dma_allocator instances are considered equal.
 *
 * @return Always returns true.
 */
template<typename T, typename U>
bool operator==(const dma_allocator<T>&, const dma_allocator<U>&) {
    return true;
}

/**
 * @brief Inequality comparison for dma_allocator.
 *
 * All dma_allocator instances are considered equal.
 *
 * @return Always returns false.
 */
template<typename T, typename U>
bool operator!=(const dma_allocator<T>&, const dma_allocator<U>&) {
    return false;
}

/** @} */ // end of idfxx_core_memory
/** @} */ // end of idfxx_core

} // namespace idfxx
