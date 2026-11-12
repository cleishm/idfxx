// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/crypto/error>
 * @file error.hpp
 * @brief Error codes for cryptographic operations.
 *
 * @defgroup idfxx_crypto Cryptography
 * @brief Type-safe cryptographic primitives.
 *
 * Provides message digests (@ref idfxx_crypto_hash) in one-shot and incremental forms, with the
 * algorithm chosen at compile time or at runtime. Every operation is available both as a
 * `result<T>`-returning `try_*` function and, when exceptions are enabled, as a throwing function.
 *
 * Depends on @ref idfxx_core for error handling.
 *
 * @defgroup idfxx_crypto_error Error Handling
 * @ingroup idfxx_crypto
 * @brief Error codes and category for `idfxx_crypto`.
 *
 * The codes follow the vocabulary of the PSA Certified Crypto API. The category's
 * `default_error_condition()` maps the codes that have a standard POSIX synonym to the
 * corresponding `std::errc`, so callers can compare against either form.
 * @{
 */

#include <idfxx/error>

#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace idfxx::crypto {

/**
 * @headerfile <idfxx/crypto/error>
 * @brief Cryptography error codes.
 *
 * Codes with a standard POSIX synonym also compare equal to it (via `default_error_condition`):
 *
 * | `crypto::errc`                          | `std::errc`                 |
 * |-----------------------------------------|-----------------------------|
 * | `not_supported`                         | `not_supported`             |
 * | `not_permitted`                         | `operation_not_permitted`   |
 * | `buffer_too_small`                      | `value_too_large`           |
 * | `already_exists`                        | `file_exists`               |
 * | `does_not_exist`                        | `no_such_file_or_directory` |
 * | `invalid_argument`                      | `invalid_argument`          |
 * | `insufficient_storage`                  | `no_space_on_device`        |
 * | `storage_failure`, `hardware_failure`   | `io_error`                  |
 * | `invalid_handle`                        | `bad_file_descriptor`       |
 *
 * so for example
 * @code
 * ec == idfxx::crypto::errc::not_supported
 * ec == std::errc::not_supported
 * @endcode
 * are both true for the same underlying error. The other codes have no POSIX synonym and compare
 * equal only to themselves.
 */
enum class errc : esp_err_t {
    // clang-format off
    generic_error         = 1,  ///< An error that does not correspond to any more specific code.
    not_supported         = 2,  ///< The requested operation or a parameter is not supported.
    not_permitted         = 3,  ///< The requested action is denied by a policy.
    buffer_too_small      = 4,  ///< An output buffer is too small.
    already_exists        = 5,  ///< An item already exists.
    does_not_exist        = 6,  ///< An item does not exist.
    bad_state             = 7,  ///< The operation is not valid in the current state.
    invalid_argument      = 8,  ///< A parameter is invalid.
    insufficient_storage  = 9,  ///< There is not enough persistent storage.
    communication_failure = 10, ///< Communication with a cryptoprocessor failed.
    storage_failure       = 11, ///< Persistent storage failed.
    data_corrupt          = 12, ///< Stored data has been corrupted.
    data_invalid          = 13, ///< Data read from storage is not valid.
    hardware_failure      = 14, ///< A hardware failure was detected.
    corruption_detected   = 15, ///< A tampering attempt was detected.
    insufficient_entropy  = 16, ///< There is not enough entropy to generate random data.
    invalid_signature     = 17, ///< A signature, MAC or hash is incorrect.
    invalid_padding       = 18, ///< The decrypted padding is incorrect.
    insufficient_data     = 19, ///< Not enough data was supplied.
    invalid_handle        = 20, ///< A key identifier or handle is not valid.
    // clang-format on
};

/**
 * @headerfile <idfxx/crypto/error>
 * @brief Error category for `idfxx_crypto` errors.
 *
 * Returns human-readable names for `errc` values and maps the codes that have a standard POSIX
 * synonym to the corresponding `std::errc` via `default_error_condition()`.
 */
class error_category : public std::error_category {
public:
    /**
     * @brief Returns the name of the error category.
     *
     * @return The category name, `"crypto::Error"`.
     */
    [[nodiscard]] const char* name() const noexcept override final;

    /**
     * @brief Returns a human-readable message for an error code.
     *
     * @param ec The `errc` value, as an integer.
     * @return A description of the error, or a message naming the unknown value if `ec` is not an
     *         `errc` value.
     */
    [[nodiscard]] std::string message(int ec) const override final;

    /**
     * @brief Maps an `errc` value to its canonical portable condition.
     *
     * Maps each code with a standard POSIX synonym, as listed on
     * @ref idfxx::crypto::errc "crypto::errc", to that `std::errc`; every other code maps to a
     * condition in this category. Because `==`/`!=` comparisons
     * between an `std::error_code` and an `std::error_condition` go through this mapping, callers
     * may compare against either form:
     * @code
     * ec == idfxx::crypto::errc::not_permitted
     * ec == std::errc::operation_not_permitted
     * @endcode
     *
     * @param code The `errc` value to canonicalize.
     * @return The canonical `std::error_condition` for `code`.
     */
    [[nodiscard]] std::error_condition
    default_error_condition(int code) const noexcept override final;
};

} // namespace idfxx::crypto

namespace idfxx {

/**
 * @headerfile <idfxx/crypto/error>
 * @brief Returns a reference to the `idfxx_crypto` error category singleton.
 *
 * @return Reference to the singleton `crypto::error_category` instance.
 */
[[nodiscard]] const crypto::error_category& crypto_category() noexcept;

} // namespace idfxx

namespace idfxx::crypto {

/**
 * @headerfile <idfxx/crypto/error>
 * @brief Creates an error code from an `idfxx::crypto::errc` value.
 *
 * @param e The error code enumerator.
 * @return The corresponding `std::error_code`.
 */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), crypto_category()};
}

} // namespace idfxx::crypto

/// @cond INTERNAL
namespace std {
template<>
struct is_error_code_enum<idfxx::crypto::errc> : std::true_type {};
} // namespace std
/// @endcond

/** @} */ // end of idfxx_crypto_error
