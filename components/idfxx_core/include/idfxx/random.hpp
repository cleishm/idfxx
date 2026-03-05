// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/random>
 * @file random.hpp
 * @brief Hardware random number generation.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_random Random Numbers
 * @ingroup idfxx_core
 * @brief True random number generation using the hardware RNG.
 *
 * Provides free functions and a C++ `UniformRandomBitGenerator` class
 * backed by the hardware random number generator.
 *
 * @code
 * uint32_t val = idfxx::random();
 *
 * idfxx::random_device rng;
 * std::uniform_int_distribution<int> dist(1, 100);
 * int n = dist(rng);
 * @endcode
 * @{
 */

#include <cstdint>
#include <esp_random.h>
#include <limits>
#include <span>

namespace idfxx {

/**
 * @brief Returns a hardware-generated random 32-bit value.
 *
 * Uses the true hardware random number generator when RF subsystem
 * is active, or a pseudo-random source otherwise.
 *
 * @return A random 32-bit unsigned integer.
 */
[[nodiscard]] inline uint32_t random() noexcept {
    return esp_random();
}

/**
 * @brief Fills a buffer with hardware-generated random bytes.
 *
 * @param buf The buffer to fill with random data.
 */
inline void fill_random(std::span<uint8_t> buf) noexcept {
    esp_fill_random(buf.data(), buf.size());
}

/**
 * @brief Fills a buffer with hardware-generated random bytes.
 *
 * @param buf The buffer to fill with random data.
 */
inline void fill_random(std::span<std::byte> buf) noexcept {
    esp_fill_random(buf.data(), buf.size());
}

/**
 * @headerfile <idfxx/random>
 * @brief Hardware random number generator satisfying UniformRandomBitGenerator.
 *
 * Can be used with standard library distributions and algorithms
 * that require a random bit source.
 *
 * @code
 * idfxx::random_device rng;
 * std::uniform_int_distribution<int> dist(1, 6);
 * int die_roll = dist(rng);
 * @endcode
 */
class random_device {
public:
    /** @brief The type of random values produced. */
    using result_type = uint32_t;

    /**
     * @brief Returns the minimum value that can be generated.
     *
     * @return 0.
     */
    static constexpr result_type min() noexcept { return std::numeric_limits<result_type>::min(); }

    /**
     * @brief Returns the maximum value that can be generated.
     *
     * @return The maximum uint32_t value.
     */
    static constexpr result_type max() noexcept { return std::numeric_limits<result_type>::max(); }

    /**
     * @brief Generates a random value.
     *
     * @return A hardware-generated random 32-bit value.
     */
    result_type operator()() noexcept { return esp_random(); }
};

/** @} */ // end of idfxx_core_random
/** @} */ // end of idfxx_core

} // namespace idfxx
