// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/error>
 * @file error.hpp
 * @brief IDFXX error handling.
 *
 * @defgroup idfxx_core Core Utilities
 * @brief Core utilities for the idfxx component family.
 *
 * Provides foundational error handling, memory allocators, and chrono utilities
 * for modern C++ ESP-IDF development.
 *
 * @defgroup idfxx_core_error Error Handling
 * @ingroup idfxx_core
 * @brief Result-based error handling with ESP-IDF integration.
 *
 * Provides `idfxx::result<T>` (C++23 `std::expected`) for fallible operations
 * and `idfxx::errc` enum compatible with ESP-IDF error codes.
 * @{
 */

#include <expected>
#include <functional>
#include <string>
#include <system_error>
#include <utility>

typedef int esp_err_t;

namespace idfxx {

/**
 * @headerfile <idfxx/error>
 * @brief IDFXX error codes.
 *
 * These error codes are compatible with ESP-IDF error codes and can be used
 * with std::error_code through the IDFXX error category.
 */
enum class errc : esp_err_t {
    // clang-format off
    fail                = -1,    /**< Generic failure. */
    no_mem              = 0x101, /**< Out of memory */
    invalid_arg         = 0x102, /**< Invalid argument */
    invalid_state       = 0x103, /**< Invalid state */
    invalid_size        = 0x104, /**< Invalid size */
    not_found           = 0x105, /**< Requested resource not found */
    not_supported       = 0x106, /**< Operation or feature not supported */
    timeout             = 0x107, /**< Operation timed out */
    invalid_response    = 0x108, /**< Received response was invalid */
    invalid_crc         = 0x109, /**< CRC or checksum was invalid */
    invalid_version     = 0x10A, /**< Version was invalid */
    invalid_mac         = 0x10B, /**< MAC address was invalid */
    not_finished        = 0x10C, /**< Operation has not fully completed */
    not_allowed         = 0x10D, /**< Operation is not allowed */
    // clang-format on
};

/**
 * @headerfile <idfxx/error>
 * @brief Error category for IDFXX and ESP-IDF error codes.
 *
 * This category provides human-readable error messages for IDFXX and ESP-IDF
 * error codes, enabling integration with the standard C++ error handling
 * mechanisms.
 */
class error_category : public std::error_category {
public:
    /**
     * @brief Returns the name of the error category.
     *
     * @return The category name.
     */
    [[nodiscard]] const char* name() const noexcept override final;

    /**
     * @brief Returns a human-readable message for the given error code.
     *
     * @param ec The error code value.
     *
     * @return A descriptive error message.
     */
    [[nodiscard]] std::string message(int ec) const override final;
};

/**
 * @brief Returns a reference to the IDFXX error category singleton.
 *
 * This follows the standard library pattern (like std::generic_category()).
 *
 * @return Reference to the singleton error_category instance.
 */
[[nodiscard]] const error_category& default_category() noexcept;

/** @brief Creates a std::error_code from an idfxx::errc value. */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), default_category()};
}

/**
 * @brief Creates a std::error_code from a general esp_err_t value.
 *
 * Maps common ESP-IDF error codes to corresponding idfxx::errc values, or
 * returns an std::error_code with idfxx::errc::fail for unknown ESP-IDF
 * error codes.
 */
[[nodiscard]] std::error_code make_error_code(esp_err_t e) noexcept;

/**
 * @brief result type wrapping a value or error code.
 *
 * @tparam T The success value type.
 */
template<typename T>
using result = std::expected<T, std::error_code>;

/**
 * @brief Wraps an esp_err_t into a result<void>.
 *
 * @param e The esp_err_t value to wrap.
 *
 * @return A result representing success or failure.
 */
[[nodiscard]] inline result<void> wrap(esp_err_t e) {
    if (e) {
        return std::unexpected(make_error_code(e));
    }
    return {};
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Throws a std::system_error if the result is an error.
 *
 * @tparam T The success value type.
 *
 * @param result The result to check.
 *
 * @return The success value.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error if the result is an error.
 */
template<typename T>
[[nodiscard]] inline T unwrap(result<T> result) {
    return result.transform_error([](auto ec) -> std::error_code { throw std::system_error(ec); }).value();
}

// Specialization for void
template<>
inline void unwrap(result<void> result) {
    result.transform_error([](auto ec) -> std::error_code { throw std::system_error(ec); });
}
#endif

template<typename T>
inline void abort_on_error(result<T> result, std::move_only_function<void(std::error_code)> on_error = nullptr) {
    result.transform_error([&](auto ec) {
        if (on_error) {
            on_error(ec);
        }
        abort();
        return ec;
    });
}

} // namespace idfxx

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::errc> : true_type {};
} // namespace std
/** @endcond */

/** @} */ // end of idfxx_core_error
