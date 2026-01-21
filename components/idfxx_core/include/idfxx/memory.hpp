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
 * Provides allocators for internal DRAM, IRAM, and DMA-capable memory
 * that can be used with standard containers.
 * @{
 */

#include <cstddef>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <new>

namespace idfxx {

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

/** @} */ // end of idfxx_core_memory
/** @} */ // end of idfxx_core

} // namespace idfxx
