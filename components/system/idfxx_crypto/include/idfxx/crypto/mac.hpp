// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/crypto/mac>
 * @file mac.hpp
 * @brief Message authentication codes (MACs).
 *
 * @defgroup idfxx_crypto_mac Message Authentication
 * @ingroup idfxx_crypto
 * @brief One-shot and incremental message authentication codes: HMAC (with each available hash
 *        algorithm) and AES-CMAC.
 *
 * Each algorithm has an `authenticator` alias (`idfxx::crypto::mac::hmac_sha256`,
 * `idfxx::crypto::mac::cmac`, ...). Its static `import_key()` and `generate_key()` create a key for
 * it. Its static `compute()` and `compare()` authenticate a complete message in one call. Its
 * instances authenticate a message supplied in pieces with `update()`. The aliases produce
 * full-length tags. For truncated tags, name the `authenticator` template with a tag size
 * (`mac::authenticator<mac::algorithm::cmac, 4>`). Use these when the algorithm is known at compile
 * time.
 *
 * `idfxx::crypto::mac::operation` offers the same operations with an algorithm chosen at runtime.
 * It takes the algorithm and tag size from a key's policy, and writes tags into a
 * caller-supplied buffer. `mac::tag_size()` gives an algorithm's full tag size, and is also
 * usable in constant expressions.
 *
 * An algorithm (its `mac::algorithm` enumerator and its alias) is available only when it is
 * enabled in the mbedTLS configuration (`Component config` → `mbedTLS` in menuconfig). HMAC is
 * available for every enabled hash algorithm (see @ref idfxx_crypto_hash). AES-CMAC is enabled
 * with `CONFIG_MBEDTLS_CMAC_C`.
 * @{
 */

#include <idfxx/byte_view>
#include <idfxx/crypto/error>
#include <idfxx/crypto/key>
#include <idfxx/error>

#include <array>
#include <cstddef>
#include <memory>
#include <psa/crypto.h>
#include <span>
#include <string_view>
#include <utility>

namespace idfxx::crypto::mac {

/**
 * @headerfile <idfxx/crypto/mac>
 * @brief MAC algorithms.
 *
 * Each enumerator is present only when the algorithm is enabled in the mbedTLS configuration.
 */
enum class algorithm : psa_algorithm_t {
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_MD5)
    /// HMAC (RFC 2104) with MD5, 16-byte tag.
    hmac_md5 = PSA_ALG_HMAC(PSA_ALG_MD5),
#endif
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_1)
    /// HMAC (RFC 2104) with SHA-1, 20-byte tag.
    hmac_sha1 = PSA_ALG_HMAC(PSA_ALG_SHA_1),
#endif
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_224)
    /// HMAC (RFC 2104) with SHA-224, 28-byte tag.
    hmac_sha224 = PSA_ALG_HMAC(PSA_ALG_SHA_224),
#endif
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_256)
    /// HMAC (RFC 2104) with SHA-256, 32-byte tag.
    hmac_sha256 = PSA_ALG_HMAC(PSA_ALG_SHA_256),
#endif
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_384)
    /// HMAC (RFC 2104) with SHA-384, 48-byte tag.
    hmac_sha384 = PSA_ALG_HMAC(PSA_ALG_SHA_384),
#endif
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_512)
    /// HMAC (RFC 2104) with SHA-512, 64-byte tag.
    hmac_sha512 = PSA_ALG_HMAC(PSA_ALG_SHA_512),
#endif
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA3_224)
    /// HMAC (RFC 2104) with SHA3-224, 28-byte tag.
    hmac_sha3_224 = PSA_ALG_HMAC(PSA_ALG_SHA3_224),
#endif
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA3_256)
    /// HMAC (RFC 2104) with SHA3-256, 32-byte tag.
    hmac_sha3_256 = PSA_ALG_HMAC(PSA_ALG_SHA3_256),
#endif
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA3_384)
    /// HMAC (RFC 2104) with SHA3-384, 48-byte tag.
    hmac_sha3_384 = PSA_ALG_HMAC(PSA_ALG_SHA3_384),
#endif
#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA3_512)
    /// HMAC (RFC 2104) with SHA3-512, 64-byte tag.
    hmac_sha3_512 = PSA_ALG_HMAC(PSA_ALG_SHA3_512),
#endif
#if defined(PSA_WANT_ALG_CMAC) && defined(PSA_WANT_KEY_TYPE_AES)
    /// AES-CMAC (NIST SP 800-38B, RFC 4493), 16-byte tag. Takes a 128-, 192- or 256-bit AES key.
    cmac = PSA_ALG_CMAC,
#endif
};

/**
 * @headerfile <idfxx/crypto/mac>
 * @brief Returns the full tag size of a MAC algorithm.
 *
 * Usable in constant expressions, for example to size a buffer for tags. The size is known for
 * AES-CMAC and for HMAC with every hash algorithm of the PSA Certified Crypto API, whether or not
 * the algorithm is enabled in the mbedTLS configuration. A key that permits truncated tags
 * (`key_algorithm{alg, tag_size}`) gives shorter tags. See `mac::operation::tag_size()`.
 *
 * @code
 * static_assert(idfxx::crypto::mac::tag_size(idfxx::crypto::mac::algorithm::hmac_sha256) == 32);
 * @endcode
 *
 * @param alg The MAC algorithm.
 * @return The size of an untruncated tag, in bytes, or 0 if `alg` is not a MAC algorithm.
 */
[[nodiscard]] constexpr std::size_t tag_size(algorithm alg) noexcept {
    const auto a = static_cast<psa_algorithm_t>(alg);
    return PSA_ALG_IS_MAC(a) ? detail::mac_full_tag_size(a) : 0;
}

} // namespace idfxx::crypto::mac

/** @cond INTERNAL */
namespace idfxx::crypto::detail {

// The type of key that a MAC algorithm takes.
[[nodiscard]] constexpr key_type mac_key_type(mac::algorithm alg) noexcept {
    return static_cast<key_type>(mac_key_type_of(std::to_underlying(alg)));
}

} // namespace idfxx::crypto::detail
/** @endcond */

namespace idfxx::crypto::mac {

/**
 * @headerfile <idfxx/crypto/mac>
 * @brief Computes message authentication codes (MACs) with the MAC algorithm that a key permits.
 *
 * Use `mac::operation` when the algorithm is only known at runtime. It uses the algorithm and tag
 * size of the key's policy (`key::algorithm()`), for example of a key whose algorithm comes from
 * configuration. When the algorithm is known at compile time, use its `authenticator` alias
 * (`hmac_sha256`, `cmac`, ...) instead: it checks that a key is for that algorithm, and its tags
 * are fixed-size arrays.
 *
 * The static `compute()` and `compare()` authenticate a complete message in one call. An instance
 * authenticates a message supplied in pieces: `update()` adds data, and `finish()` (or `verify()`)
 * completes the message. After `finish()` or `verify()`, the operation is ready for a new message.
 * A failed `update()`, `finish()` or `verify()` discards the partially authenticated message, and
 * the operation is ready for a new message. The exception is `finish()` failing with
 * `errc::buffer_too_small`, which leaves the message unchanged.
 *
 * `compute()` and `finish()` need `key_usage::sign_message`, and `compare()` and `verify()` need
 * `key_usage::verify_message`. Without that usage, they fail with `errc::not_permitted`.
 * `compute()` and `compare()` refuse a key that permits no MAC algorithm with
 * `errc::not_permitted`, and creating an operation with such a key fails in the same way. An
 * operation also can't be created with a key that permits neither usage. A key's type always
 * matches its algorithm, because key creation refuses a type that doesn't. But the crypto library
 * knows whether it can use the key's size only when it sets up a message. A size that it can't use
 * (such as a 192-bit AES key on a chip whose AES engine doesn't support that size) fails with
 * `errc::not_supported` from `compute()` and `compare()`, or from an operation's first `update()`,
 * `finish()` or `verify()` of a message.
 *
 * This type is non-copyable and move-only. A moved-from object must not be used: any operation
 * other than destruction or assignment is undefined behavior. Instances cannot be cloned.
 *
 * @code
 * // The algorithm comes from configuration, for example HMAC-SHA-256 or HMAC-SHA-512
 * auto k = idfxx::crypto::key::import(
 *     {.type = idfxx::crypto::key_type::hmac,
 *      .usage = idfxx::crypto::key_usage::sign_message | idfxx::crypto::key_usage::verify_message,
 *      .algorithm = settings.algorithm},
 *     key_material
 * );
 *
 * // One-shot: the tag is the first mac::tag_size(settings.algorithm) bytes of buf
 * std::array<std::byte, 64> buf; // large enough for any tag
 * auto tag = idfxx::crypto::mac::operation::compute(k, "hello, world", buf);
 *
 * // Incremental
 * idfxx::crypto::mac::operation op{k};
 * op.update(header);
 * op.update(body);
 * if (!op.verify(received_tag)) {
 *     // the message was altered, or was not sent by a key holder
 * }
 * @endcode
 */
class operation {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Computes the tag of a message.
     *
     * @param k The key. Its usage must include `key_usage::sign_message`.
     * @param data The message.
     * @param tag The buffer to write the tag to, at least the tag size that the key permits:
     *            `mac::tag_size()` of its algorithm, or less for truncated tags.
     * @return The filled prefix of `tag`: its first tag-size bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_permitted` if the key permits no MAC algorithm or
     *         does not permit computing tags, `errc::not_supported` if the key's size is not
     *         supported for its algorithm, `errc::buffer_too_small` if `tag` is too small, or
     *         another code on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static byte_span compute(const key& k, byte_view data, byte_span tag) {
        return unwrap(try_compute(k, data, tag));
    }

    /**
     * @brief Computes the tag of a message.
     *
     * @param k The key. Its usage must include `key_usage::sign_message`.
     * @param data The message, as its bytes.
     * @param tag The buffer to write the tag to, at least the tag size that the key permits:
     *            `mac::tag_size()` of its algorithm, or less for truncated tags.
     * @return The filled prefix of `tag`: its first tag-size bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_permitted` if the key permits no MAC algorithm or
     *         does not permit computing tags, `errc::not_supported` if the key's size is not
     *         supported for its algorithm, `errc::buffer_too_small` if `tag` is too small, or
     *         another code on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static byte_span compute(const key& k, std::string_view data, byte_span tag) {
        return unwrap(try_compute(k, data, tag));
    }
#endif

    /**
     * @brief Computes the tag of a message.
     *
     * @param k The key. Its usage must include `key_usage::sign_message`.
     * @param data The message.
     * @param tag The buffer to write the tag to, at least the tag size that the key permits:
     *            `mac::tag_size()` of its algorithm, or less for truncated tags.
     * @return The filled prefix of `tag` (its first tag-size bytes), or an error.
     * @retval errc::not_permitted The key permits no MAC algorithm, or does not permit computing
     *         tags.
     * @retval errc::not_supported The key's size is not supported for its algorithm.
     * @retval errc::buffer_too_small `tag` is smaller than the key's tag size.
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] static result<byte_span> try_compute(const key& k, byte_view data, byte_span tag);

    /**
     * @brief Computes the tag of a message.
     *
     * @param k The key. Its usage must include `key_usage::sign_message`.
     * @param data The message, as its bytes.
     * @param tag The buffer to write the tag to, at least the tag size that the key permits:
     *            `mac::tag_size()` of its algorithm, or less for truncated tags.
     * @return The filled prefix of `tag` (its first tag-size bytes), or an error.
     * @retval errc::not_permitted The key permits no MAC algorithm, or does not permit computing
     *         tags.
     * @retval errc::not_supported The key's size is not supported for its algorithm.
     * @retval errc::buffer_too_small `tag` is smaller than the key's tag size.
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] static result<byte_span>
    try_compute(const key& k, std::string_view data, byte_span tag) {
        return try_compute(k, std::as_bytes(std::span{data}), tag);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Checks whether a message has the expected tag.
     *
     * The tags are compared in constant time.
     *
     * @param k The key. Its usage must include `key_usage::verify_message`.
     * @param data The message.
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_permitted` if the key permits no MAC algorithm or
     *         does not permit checking tags, `errc::not_supported` if the key's size is not
     *         supported for its algorithm, or another code on failure (for example
     *         `errc::hardware_failure`).
     */
    [[nodiscard]] static bool compare(const key& k, byte_view data, byte_view expected) {
        return unwrap(try_compare(k, data, expected));
    }

    /**
     * @brief Checks whether a message has the expected tag.
     *
     * The tags are compared in constant time.
     *
     * @param k The key. Its usage must include `key_usage::verify_message`.
     * @param data The message, as its bytes.
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_permitted` if the key permits no MAC algorithm or
     *         does not permit checking tags, `errc::not_supported` if the key's size is not
     *         supported for its algorithm, or another code on failure (for example
     *         `errc::hardware_failure`).
     */
    [[nodiscard]] static bool compare(const key& k, std::string_view data, byte_view expected) {
        return unwrap(try_compare(k, data, expected));
    }
#endif

    /**
     * @brief Checks whether a message has the expected tag.
     *
     * The tags are compared in constant time.
     *
     * @param k The key. Its usage must include `key_usage::verify_message`.
     * @param data The message.
     * @param expected The expected tag. A tag of the wrong length never matches. A key that can't
     *                 check tags is reported as an error, whatever the expected tag.
     * @return True if the message's tag equals `expected`, false if it does not, or an error.
     * @retval errc::not_permitted The key permits no MAC algorithm, or does not permit checking
     *         tags.
     * @retval errc::not_supported The key's size is not supported for its algorithm (not
     *         reported for an expected tag of the wrong length).
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] static result<bool> try_compare(const key& k, byte_view data, byte_view expected);

    /**
     * @brief Checks whether a message has the expected tag.
     *
     * The tags are compared in constant time.
     *
     * @param k The key. Its usage must include `key_usage::verify_message`.
     * @param data The message, as its bytes.
     * @param expected The expected tag. A tag of the wrong length never matches. A key that can't
     *                 check tags is reported as an error, whatever the expected tag.
     * @return True if the message's tag equals `expected`, false if it does not, or an error.
     * @retval errc::not_permitted The key permits no MAC algorithm, or does not permit checking
     *         tags.
     * @retval errc::not_supported The key's size is not supported for its algorithm (not
     *         reported for an expected tag of the wrong length).
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] static result<bool>
    try_compare(const key& k, std::string_view data, byte_view expected) {
        return try_compare(k, std::as_bytes(std::span{data}), expected);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates an operation ready for a new message, authenticated with a key.
     *
     * Checks that the key permits a MAC algorithm and computing or checking tags. It sets nothing
     * up in the crypto library. So a failure that only the crypto library can detect, such as
     * `errc::not_supported` for a key size that it can't use, is reported by the first `update()`,
     * `finish()` or `verify()` of a message. Allocation failure is treated as fatal
     * (`std::bad_alloc`).
     *
     * @param k The key, whose policy gives the MAC algorithm and tag size. Its usage must
     *          include `key_usage::sign_message` (to complete messages with `finish()`),
     *          `key_usage::verify_message` (to check them with `verify()`), or both. Must outlive
     *          the operation. While the operation uses the key, the key may be moved, but not
     *          destroyed or assigned over.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_permitted` if the key permits no MAC algorithm or
     *         its usage includes neither `key_usage::sign_message` nor
     *         `key_usage::verify_message`.
     */
    [[nodiscard]] explicit operation(const key& k)
        : operation(unwrap(make(k))) {}
#endif

    /**
     * @brief Creates an operation ready for a new message, authenticated with a key.
     *
     * Checks that the key permits a MAC algorithm and computing or checking tags. It sets nothing
     * up in the crypto library. So a failure that only the crypto library can detect, such as
     * `errc::not_supported` for a key size that it can't use, is reported by the first
     * `try_update()`, `try_finish()` or `try_verify()` of a message. Allocation failure is treated
     * as fatal (`std::bad_alloc`, or abort when exceptions are disabled).
     *
     * @param k The key, whose policy gives the MAC algorithm and tag size. Its usage must
     *          include `key_usage::sign_message` (to complete messages with `try_finish()`),
     *          `key_usage::verify_message` (to check them with `try_verify()`), or both. Must
     *          outlive the operation. While the operation uses the key, the key may be moved, but
     *          not destroyed or assigned over.
     * @return The operation, or an error.
     * @retval errc::not_permitted The key permits no MAC algorithm, or its usage includes neither
     *         `key_usage::sign_message` nor `key_usage::verify_message`.
     */
    [[nodiscard]] static result<operation> make(const key& k);

    /**
     * @brief Destroys the operation, discarding any partially authenticated message.
     */
    ~operation();

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;

    /**
     * @brief Move constructor. Transfers the key and the partially authenticated message, if any.
     *
     * @param other The operation to move from.
     */
    operation(operation&& other) noexcept;

    /**
     * @brief Move assignment. Discards this operation's partially authenticated message, if any,
     *        and takes over `other`'s key and message.
     *
     * @param other The operation to move from.
     * @return A reference to this operation.
     */
    operation& operator=(operation&& other) noexcept;

    /**
     * @brief Returns the MAC algorithm.
     *
     * @return The MAC algorithm that the key permits, at its full tag size. `tag_size()` gives a
     *         truncated tag size.
     */
    [[nodiscard]] mac::algorithm algorithm() const noexcept;

    /**
     * @brief Returns the size of a tag.
     *
     * @return The size of a tag, in bytes: the truncated size that the key permits, or
     *         `mac::tag_size(algorithm())` for a key that permits full-length tags.
     */
    [[nodiscard]] std::size_t tag_size() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially authenticated message is discarded and the operation is ready for
     * a new message.
     *
     * @param data The data to add.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_supported` if the key's size is not
     *         supported for its algorithm (reported when a message starts), or another code (for
     *         example `errc::hardware_failure`).
     */
    void update(byte_view data) { unwrap(try_update(data)); }

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially authenticated message is discarded and the operation is ready for
     * a new message.
     *
     * @param data The text to add, as its bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_supported` if the key's size is not
     *         supported for its algorithm (reported when a message starts), or another code (for
     *         example `errc::hardware_failure`).
     */
    void update(std::string_view data) { unwrap(try_update(data)); }
#endif

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially authenticated message is discarded and the operation is ready for
     * a new message.
     *
     * @param data The data to add.
     * @return Success, or an error.
     * @retval errc::not_supported The key's size is not supported for its algorithm (reported
     *         when a message starts).
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] result<void> try_update(byte_view data);

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially authenticated message is discarded and the operation is ready for
     * a new message.
     *
     * @param data The text to add, as its bytes.
     * @return Success, or an error.
     * @retval errc::not_supported The key's size is not supported for its algorithm (reported
     *         when a message starts).
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] result<void> try_update(std::string_view data) {
        return try_update(std::as_bytes(std::span{data}));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Completes the message and writes its tag.
     *
     * Without any preceding `update()`, writes the tag of the empty message. Afterward, the
     * operation is ready for a new message. A failure with `errc::buffer_too_small` leaves the
     * message unchanged, so it can be finished again with a larger buffer. Any other failure
     * (including `errc::not_permitted`, which is reported first) discards it.
     *
     * @param tag The buffer to write the tag to, at least `tag_size()` bytes.
     * @return The filled prefix of `tag`: its first `tag_size()` bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_permitted` if the key's usage does not include
     *         `key_usage::sign_message`, `errc::buffer_too_small` if `tag` is too small,
     *         `errc::not_supported` if the key's size is not supported for its algorithm, or
     *         another code on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] byte_span finish(byte_span tag) { return unwrap(try_finish(tag)); }
#endif

    /**
     * @brief Completes the message and writes its tag.
     *
     * Without any preceding `try_update()`, writes the tag of the empty message. Afterward, the
     * operation is ready for a new message. A failure with `errc::buffer_too_small` leaves the
     * message unchanged, so it can be finished again with a larger buffer. Any other failure
     * (including `errc::not_permitted`, which is reported first) discards it.
     *
     * @param tag The buffer to write the tag to, at least `tag_size()` bytes.
     * @return The filled prefix of `tag` (its first `tag_size()` bytes), or an error.
     * @retval errc::not_permitted The key's usage does not include `key_usage::sign_message`.
     * @retval errc::buffer_too_small `tag` is smaller than `tag_size()`.
     * @retval errc::not_supported The key's size is not supported for its algorithm.
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] result<byte_span> try_finish(byte_span tag);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Completes the message and checks whether it has the expected tag.
     *
     * The tags are compared in constant time. Afterward, the operation is ready for a new message.
     *
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_permitted` if the key's usage does not include
     *         `key_usage::verify_message`, `errc::not_supported` if the key's size is not supported
     *         for its algorithm, or another code on failure (for example
     *         `errc::hardware_failure`).
     */
    [[nodiscard]] bool verify(byte_view expected) { return unwrap(try_verify(expected)); }
#endif

    /**
     * @brief Completes the message and checks whether it has the expected tag.
     *
     * The tags are compared in constant time. Afterward, whether or not it succeeded, the
     * operation is ready for a new message.
     *
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false if it does not, or an error.
     * @retval errc::not_permitted The key's usage does not include `key_usage::verify_message`.
     * @retval errc::not_supported The key's size is not supported for its algorithm (not reported
     *         for an expected tag of the wrong length).
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] result<bool> try_verify(byte_view expected);

    /**
     * @brief Discards the partially authenticated message, if any, leaving the operation ready for
     *        a new message.
     */
    void reset() noexcept;

private:
    /** @cond INTERNAL */
    template<mac::algorithm Alg, std::size_t TagSize>
    friend class authenticator;
    /** @endcond */

    struct state;

    explicit operation(std::unique_ptr<state> s) noexcept;

    // An authenticator's operations, defined out of line. They are the same as make(),
    // try_compute() and try_compare(), but refuse with errc::not_permitted a key that permits any
    // algorithm other than `alg`.
    [[nodiscard]] static result<operation> _make_as(key_algorithm alg, const key& k);
    [[nodiscard]] static result<byte_span>
    _try_compute_as(key_algorithm alg, const key& k, byte_view data, byte_span tag);
    [[nodiscard]] static result<bool>
    _try_compare_as(key_algorithm alg, const key& k, byte_view data, byte_view expected);

    std::unique_ptr<state> _state;
};

/**
 * @headerfile <idfxx/crypto/mac>
 * @brief Computes message authentication codes (MACs) with a fixed MAC algorithm.
 *
 * A MAC (or tag) authenticates a message. Only holders of the key can compute it, so a message
 * whose tag is correct came unaltered from one of them. The static `import_key()` and
 * `generate_key()` create a key for the algorithm. The key permits both computing and checking
 * tags.
 *
 * The static `compute()` and `compare()` authenticate a complete message in one call. An instance
 * authenticates a message supplied in pieces: `update()` adds data, and `finish()` (or `verify()`)
 * completes the message. After `finish()` or `verify()`, and after a failed `update()`, `finish()`
 * or `verify()`, the authenticator is ready for a new message. A failure discards the partially
 * authenticated message.
 *
 * For full-length tags, use one of the algorithm aliases (`hmac_sha256`, `cmac`, ...) rather than
 * naming the template directly. When the algorithm is only known at runtime, use `mac::operation`
 * instead.
 *
 * For a protocol that sends truncated tags, name the template with a `TagSize` shorter than the
 * algorithm's full tag size. A truncated tag is the leftmost `TagSize` bytes of the full tag. The
 * shorter the tag, the easier it is to forge by guessing.
 *
 * `compute()` and `finish()` need `key_usage::sign_message`, and `compare()` and `verify()` need
 * `key_usage::verify_message`. Without that usage, they fail with `errc::not_permitted`.
 * `compute()` and `compare()` refuse a key whose permitted algorithm is not this one with
 * `errc::not_permitted`, and creating an authenticator with such a key fails in the same way. An
 * authenticator also can't be created with a key that permits neither usage. A key's type always
 * matches its algorithm, because key creation refuses a type that doesn't. But the crypto library
 * knows whether it can use the key's size only when it sets up a message. A size that it can't use
 * (such as a 192-bit AES key on a chip whose AES engine doesn't support that size) fails with
 * `errc::not_supported` from `compute()` and `compare()`, or from an authenticator's first
 * `update()`, `finish()` or `verify()` of a message.
 *
 * The tag size is part of the algorithm that a key permits. A key made for one tag size (by
 * this type's `import_key()` or `generate_key()`, or with `key_algorithm{alg, tag_size}`) is
 * refused with `errc::not_permitted` by an authenticator of any other tag size, including the
 * full size.
 *
 * This type is non-copyable and move-only. A moved-from object must not be used: any operation
 * other than destruction or assignment is undefined behavior. Instances cannot be cloned.
 *
 * @code
 * auto k = idfxx::crypto::mac::hmac_sha256::import_key(key_material);
 *
 * // One-shot
 * auto tag = idfxx::crypto::mac::hmac_sha256::compute(k, "hello, world");
 *
 * // Incremental
 * idfxx::crypto::mac::hmac_sha256 hmac{k};
 * hmac.update(header);
 * hmac.update(body);
 * if (!hmac.verify(expected_tag)) {
 *     // the message was altered, or was not sent by a key holder
 * }
 * @endcode
 *
 * With 4-byte AES-CMAC tags:
 *
 * @code
 * using short_cmac = idfxx::crypto::mac::authenticator<idfxx::crypto::mac::algorithm::cmac, 4>;
 *
 * auto k = short_cmac::import_key(aes_key);
 * short_cmac::tag_type tag = short_cmac::compute(k, frame); // std::array<std::byte, 4>
 * @endcode
 *
 * @tparam Alg The MAC algorithm.
 * @tparam TagSize The tag size, in bytes: at least 4, and at most the algorithm's full tag
 *                 size (the default). A shorter size truncates each tag to its leftmost
 *                 `TagSize` bytes.
 */
template<algorithm Alg, std::size_t TagSize = tag_size(Alg)>
class authenticator {
    static_assert(
        mac::tag_size(Alg) > 0,
        "authenticator requires a MAC algorithm known to PSA Crypto"
    );
    // Only for a known algorithm, so that an unknown one fails just the assertion above
    static_assert(
        mac::tag_size(Alg) == 0 || detail::valid_mac_tag_size(Alg, TagSize),
        "TagSize must be at least 4 bytes and at most the algorithm's full tag length"
    );

public:
    /// The MAC algorithm.
    static constexpr mac::algorithm algorithm = Alg;

    /// The size of a tag, in bytes: `TagSize`.
    static constexpr std::size_t tag_size = TagSize;

    /// A tag.
    using tag_type = std::array<std::byte, tag_size>;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a key for this algorithm from key material.
     *
     * The key is permitted to compute and check tags (`key_usage::sign_message` and
     * `key_usage::verify_message`) with this algorithm and tag size only.
     *
     * @param material The key material. Must not be empty. For HMAC, any length. A key longer than
     *                 the hash's block size is hashed first, as HMAC specifies. A length of at
     *                 least the hash's digest size is recommended. For AES-CMAC, 16, 24 or 32
     *                 bytes.
     * @return The key.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::invalid_argument` if `material` is empty or not
     *         a valid size for the key type.
     */
    [[nodiscard]] static key import_key(byte_view material) {
        return unwrap(try_import_key(material));
    }
#endif

    /**
     * @brief Creates a key for this algorithm from key material.
     *
     * The key is permitted to compute and check tags (`key_usage::sign_message` and
     * `key_usage::verify_message`) with this algorithm and tag size only.
     *
     * @param material The key material. Must not be empty. For HMAC, any length. A key longer than
     *                 the hash's block size is hashed first, as HMAC specifies. A length of at
     *                 least the hash's digest size is recommended. For AES-CMAC, 16, 24 or 32
     *                 bytes.
     * @return The key, or an error.
     * @retval errc::invalid_argument `material` is empty or not a valid size for the key type.
     */
    [[nodiscard]] static result<key> try_import_key(byte_view material) {
        return key::try_import(_key_config(), material);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a random key for this algorithm.
     *
     * The key is permitted to compute and check tags (`key_usage::sign_message` and
     * `key_usage::verify_message`) with this algorithm and tag size only. An HMAC key is as long
     * as the hash's digest (the full tag size, whatever `tag_size`). This is the length that
     * RFC 2104 recommends. An AES-CMAC key is 128 bits.
     *
     * @return The key.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::insufficient_entropy`).
     */
    [[nodiscard]] static key generate_key() { return unwrap(try_generate_key()); }

    /**
     * @brief Creates a random key of a given size for this algorithm.
     *
     * The key is permitted to compute and check tags (`key_usage::sign_message` and
     * `key_usage::verify_message`) with this algorithm and tag size only.
     *
     * @param bits The key size, in bits: a multiple of 8 for HMAC (RFC 2104 recommends at least
     *             the hash's digest size), or 128, 192 or 256 for AES-CMAC.
     * @return The key.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::invalid_argument` if `bits` is not a valid size
     *         for the key type, `errc::not_supported` if the size is not supported, or
     *         `errc::insufficient_entropy` if the random generator failed.
     */
    [[nodiscard]] static key generate_key(std::size_t bits) {
        return unwrap(try_generate_key(bits));
    }
#endif

    /**
     * @brief Creates a random key for this algorithm.
     *
     * The key is permitted to compute and check tags (`key_usage::sign_message` and
     * `key_usage::verify_message`) with this algorithm and tag size only. An HMAC key is as long
     * as the hash's digest (the full tag size, whatever `tag_size`). This is the length that
     * RFC 2104 recommends. An AES-CMAC key is 128 bits.
     *
     * @return The key, or an error (for example `errc::insufficient_entropy`).
     */
    [[nodiscard]] static result<key> try_generate_key() {
        return try_generate_key(
            detail::mac_key_type(Alg) == key_type::hmac ? mac::tag_size(Alg) * 8 : 128
        );
    }

    /**
     * @brief Creates a random key of a given size for this algorithm.
     *
     * The key is permitted to compute and check tags (`key_usage::sign_message` and
     * `key_usage::verify_message`) with this algorithm and tag size only.
     *
     * @param bits The key size, in bits: a multiple of 8 for HMAC (RFC 2104 recommends at least
     *             the hash's digest size), or 128, 192 or 256 for AES-CMAC.
     * @return The key, or an error.
     * @retval errc::invalid_argument `bits` is not a valid size for the key type.
     * @retval errc::not_supported The key size is not supported.
     * @retval errc::insufficient_entropy The random generator failed.
     */
    [[nodiscard]] static result<key> try_generate_key(std::size_t bits) {
        return key::try_generate(_key_config(), bits);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Computes the tag of a message.
     *
     * @param k The key. Its usage must include `key_usage::sign_message`.
     * @param data The message.
     * @return The tag.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_permitted` if the key does not permit
     *         computing tags with this algorithm, `errc::not_supported` if the key's size is not
     *         supported for this algorithm.
     */
    [[nodiscard]] static tag_type compute(const key& k, byte_view data) {
        return unwrap(try_compute(k, data));
    }

    /**
     * @brief Computes the tag of a message.
     *
     * @param k The key. Its usage must include `key_usage::sign_message`.
     * @param data The message, as its bytes.
     * @return The tag.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_permitted` if the key does not permit
     *         computing tags with this algorithm, `errc::not_supported` if the key's size is not
     *         supported for this algorithm.
     */
    [[nodiscard]] static tag_type compute(const key& k, std::string_view data) {
        return unwrap(try_compute(k, data));
    }
#endif

    /**
     * @brief Computes the tag of a message.
     *
     * @param k The key. Its usage must include `key_usage::sign_message`.
     * @param data The message.
     * @return The tag, or an error.
     * @retval errc::not_permitted The key does not permit computing tags with this algorithm.
     * @retval errc::not_supported The key's size is not supported for this algorithm.
     */
    [[nodiscard]] static result<tag_type> try_compute(const key& k, byte_view data) {
        tag_type tag;
        if (auto r = operation::_try_compute_as(_key_algorithm, k, data, tag); !r) {
            return error(r.error());
        }
        return tag;
    }

    /**
     * @brief Computes the tag of a message.
     *
     * @param k The key. Its usage must include `key_usage::sign_message`.
     * @param data The message, as its bytes.
     * @return The tag, or an error.
     * @retval errc::not_permitted The key does not permit computing tags with this algorithm.
     * @retval errc::not_supported The key's size is not supported for this algorithm.
     */
    [[nodiscard]] static result<tag_type> try_compute(const key& k, std::string_view data) {
        return try_compute(k, std::as_bytes(std::span{data}));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Checks whether a message has the expected tag.
     *
     * The tags are compared in constant time.
     *
     * @param k The key. Its usage must include `key_usage::verify_message`.
     * @param data The message.
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_permitted` if the key does not permit
     *         checking tags with this algorithm, `errc::not_supported` if the key's size is not
     *         supported for this algorithm.
     */
    [[nodiscard]] static bool compare(const key& k, byte_view data, byte_view expected) {
        return unwrap(try_compare(k, data, expected));
    }

    /**
     * @brief Checks whether a message has the expected tag.
     *
     * The tags are compared in constant time.
     *
     * @param k The key. Its usage must include `key_usage::verify_message`.
     * @param data The message, as its bytes.
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_permitted` if the key does not permit
     *         checking tags with this algorithm, `errc::not_supported` if the key's size is not
     *         supported for this algorithm.
     */
    [[nodiscard]] static bool compare(const key& k, std::string_view data, byte_view expected) {
        return unwrap(try_compare(k, data, expected));
    }
#endif

    /**
     * @brief Checks whether a message has the expected tag.
     *
     * The tags are compared in constant time.
     *
     * @param k The key. Its usage must include `key_usage::verify_message`.
     * @param data The message.
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false if it does not, or an error.
     * @retval errc::not_permitted The key does not permit checking tags with this algorithm.
     * @retval errc::not_supported The key's size is not supported for this algorithm (not
     *         reported for an expected tag of the wrong length).
     */
    [[nodiscard]] static result<bool>
    try_compare(const key& k, byte_view data, byte_view expected) {
        return operation::_try_compare_as(_key_algorithm, k, data, expected);
    }

    /**
     * @brief Checks whether a message has the expected tag.
     *
     * The tags are compared in constant time.
     *
     * @param k The key. Its usage must include `key_usage::verify_message`.
     * @param data The message, as its bytes.
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false if it does not, or an error.
     * @retval errc::not_permitted The key does not permit checking tags with this algorithm.
     * @retval errc::not_supported The key's size is not supported for this algorithm (not
     *         reported for an expected tag of the wrong length).
     */
    [[nodiscard]] static result<bool>
    try_compare(const key& k, std::string_view data, byte_view expected) {
        return try_compare(k, std::as_bytes(std::span{data}), expected);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates an authenticator ready for a new message, authenticated with a key.
     *
     * Checks that the key permits this algorithm and computing or checking tags. It sets nothing up
     * in the crypto library. So a failure that only the crypto library can detect, such as
     * `errc::not_supported` for a key size that it can't use, is reported by the first `update()`,
     * `finish()` or `verify()` of a message. Allocation failure is treated as fatal
     * (`std::bad_alloc`).
     *
     * @param k The key. Its usage must include `key_usage::sign_message` (to complete messages
     *          with `finish()`), `key_usage::verify_message` (to check them with `verify()`), or
     *          both. Must outlive the authenticator. While the authenticator uses the key, the key
     *          may be moved, but not destroyed or assigned over.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_permitted` if the key does not permit this
     *         algorithm or its usage includes neither `key_usage::sign_message` nor
     *         `key_usage::verify_message`.
     */
    explicit authenticator(const key& k)
        : authenticator(unwrap(make(k))) {}
#endif

    /**
     * @brief Creates an authenticator ready for a new message, authenticated with a key.
     *
     * Checks that the key permits this algorithm and computing or checking tags. It sets nothing up
     * in the crypto library. So a failure that only the crypto library can detect, such as
     * `errc::not_supported` for a key size that it can't use, is reported by the first
     * `try_update()`, `try_finish()` or `try_verify()` of a message. Allocation failure is treated
     * as fatal (`std::bad_alloc`, or abort when exceptions are disabled).
     *
     * @param k The key. Its usage must include `key_usage::sign_message` (to complete messages
     *          with `try_finish()`), `key_usage::verify_message` (to check them with
     *          `try_verify()`), or both. Must outlive the authenticator. While the authenticator
     *          uses the key, the key may be moved, but not destroyed or assigned over.
     * @return The authenticator, or an error.
     * @retval errc::not_permitted The key does not permit this algorithm, or its usage includes
     *         neither `key_usage::sign_message` nor `key_usage::verify_message`.
     */
    [[nodiscard]] static result<authenticator> make(const key& k) {
        return operation::_make_as(_key_algorithm, k).transform([](operation&& op) {
            return authenticator{std::move(op)};
        });
    }

    /**
     * @brief Destroys the authenticator, discarding any partially authenticated message.
     */
    ~authenticator() = default;

    authenticator(const authenticator&) = delete;
    authenticator& operator=(const authenticator&) = delete;

    /**
     * @brief Move constructor. Transfers the key and the partially authenticated message, if any.
     *
     * @param other The authenticator to move from.
     */
    authenticator(authenticator&& other) noexcept = default;

    /**
     * @brief Move assignment. Discards this authenticator's partially authenticated message, if
     *        any, and takes over `other`'s key and message.
     *
     * @param other The authenticator to move from.
     * @return A reference to this authenticator.
     */
    authenticator& operator=(authenticator&& other) noexcept = default;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially authenticated message is discarded and the authenticator is ready
     * for a new message.
     *
     * @param data The data to add.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_supported` if the key's size is not
     *         supported for its algorithm (reported when a message starts), or another code (for
     *         example `errc::hardware_failure`).
     */
    void update(byte_view data) { unwrap(try_update(data)); }

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially authenticated message is discarded and the authenticator is ready
     * for a new message.
     *
     * @param data The text to add, as its bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_supported` if the key's size is not
     *         supported for its algorithm (reported when a message starts), or another code (for
     *         example `errc::hardware_failure`).
     */
    void update(std::string_view data) { unwrap(try_update(data)); }
#endif

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially authenticated message is discarded and the authenticator is ready
     * for a new message.
     *
     * @param data The data to add.
     * @return Success, or an error.
     * @retval errc::not_supported The key's size is not supported for its algorithm (reported
     *         when a message starts).
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] result<void> try_update(byte_view data) { return _op.try_update(data); }

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially authenticated message is discarded and the authenticator is ready
     * for a new message.
     *
     * @param data The text to add, as its bytes.
     * @return Success, or an error.
     * @retval errc::not_supported The key's size is not supported for its algorithm (reported
     *         when a message starts).
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] result<void> try_update(std::string_view data) {
        return try_update(std::as_bytes(std::span{data}));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Completes the message and returns its tag.
     *
     * Without any preceding `update()`, returns the tag of the empty message. Afterward, the
     * authenticator is ready for a new message.
     *
     * @return The tag.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_permitted` if the key's usage does not
     *         include `key_usage::sign_message`, `errc::not_supported` if the key's size is not
     *         supported for this algorithm, or another error (for example
     *         `errc::hardware_failure`).
     */
    [[nodiscard]] tag_type finish() { return unwrap(try_finish()); }
#endif

    /**
     * @brief Completes the message and returns its tag.
     *
     * Without any preceding `try_update()`, returns the tag of the empty message. Afterward,
     * whether or not it succeeded, the authenticator is ready for a new message.
     *
     * @return The tag, or an error (for example `errc::hardware_failure`).
     * @retval errc::not_permitted The key's usage does not include `key_usage::sign_message`.
     * @retval errc::not_supported The key's size is not supported for this algorithm.
     */
    [[nodiscard]] result<tag_type> try_finish() {
        tag_type tag;
        if (auto r = _op.try_finish(tag); !r) {
            return error(r.error());
        }
        return tag;
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Completes the message and checks whether it has the expected tag.
     *
     * The tags are compared in constant time. Afterward, the authenticator is ready for a new
     * message.
     *
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_permitted` if the key's usage does not
     *         include `key_usage::verify_message`, `errc::not_supported` if the key's size is not
     *         supported for this algorithm, or another error (for example
     *         `errc::hardware_failure`).
     */
    [[nodiscard]] bool verify(byte_view expected) { return unwrap(try_verify(expected)); }
#endif

    /**
     * @brief Completes the message and checks whether it has the expected tag.
     *
     * The tags are compared in constant time. Afterward, whether or not it succeeded, the
     * authenticator is ready for a new message.
     *
     * @param expected The expected tag. A tag of the wrong length never matches.
     * @return True if the message's tag equals `expected`, false if it does not, or an error (for
     *         example `errc::hardware_failure`).
     * @retval errc::not_permitted The key's usage does not include `key_usage::verify_message`.
     * @retval errc::not_supported The key's size is not supported for this algorithm (not reported
     *         for an expected tag of the wrong length).
     */
    [[nodiscard]] result<bool> try_verify(byte_view expected) { return _op.try_verify(expected); }

    /**
     * @brief Discards the partially authenticated message, if any, leaving the authenticator ready
     *        for a new message.
     */
    void reset() noexcept { _op.reset(); }

private:
    explicit authenticator(operation op) noexcept
        : _op(std::move(op)) {}

    // The algorithm of this authenticator's keys: its own algorithm and tag size
    static constexpr key_algorithm _key_algorithm{Alg, TagSize};

    static key::config _key_config() noexcept {
        return {
            .type = detail::mac_key_type(Alg),
            .usage = key_usage::sign_message | key_usage::verify_message,
            .algorithm = _key_algorithm,
        };
    }

    operation _op;
};

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_MD5)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-MD5 (RFC 2104) authenticator, producing a 16-byte tag.
 *
 * @warning Use HMAC-MD5 only to interoperate with existing formats and protocols that require it.
 */
using hmac_md5 = authenticator<algorithm::hmac_md5>;
#endif

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_1)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-SHA-1 (RFC 2104) authenticator, producing a 20-byte tag.
 *
 * @warning Use HMAC-SHA-1 only to interoperate with existing formats and protocols that require
 *          it.
 */
using hmac_sha1 = authenticator<algorithm::hmac_sha1>;
#endif

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_224)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-SHA-224 (RFC 2104) authenticator, producing a 28-byte tag.
 */
using hmac_sha224 = authenticator<algorithm::hmac_sha224>;
#endif

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_256)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-SHA-256 (RFC 2104) authenticator, producing a 32-byte tag.
 */
using hmac_sha256 = authenticator<algorithm::hmac_sha256>;
#endif

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_384)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-SHA-384 (RFC 2104) authenticator, producing a 48-byte tag.
 */
using hmac_sha384 = authenticator<algorithm::hmac_sha384>;
#endif

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA_512)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-SHA-512 (RFC 2104) authenticator, producing a 64-byte tag.
 */
using hmac_sha512 = authenticator<algorithm::hmac_sha512>;
#endif

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA3_224)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-SHA3-224 (RFC 2104) authenticator, producing a 28-byte tag.
 */
using hmac_sha3_224 = authenticator<algorithm::hmac_sha3_224>;
#endif

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA3_256)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-SHA3-256 (RFC 2104) authenticator, producing a 32-byte tag.
 */
using hmac_sha3_256 = authenticator<algorithm::hmac_sha3_256>;
#endif

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA3_384)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-SHA3-384 (RFC 2104) authenticator, producing a 48-byte tag.
 */
using hmac_sha3_384 = authenticator<algorithm::hmac_sha3_384>;
#endif

#if defined(PSA_WANT_ALG_HMAC) && defined(PSA_WANT_ALG_SHA3_512)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief HMAC-SHA3-512 (RFC 2104) authenticator, producing a 64-byte tag.
 */
using hmac_sha3_512 = authenticator<algorithm::hmac_sha3_512>;
#endif

#if defined(PSA_WANT_ALG_CMAC) && defined(PSA_WANT_KEY_TYPE_AES)
/**
 * @headerfile <idfxx/crypto/mac>
 * @brief AES-CMAC (NIST SP 800-38B, RFC 4493) authenticator, producing a 16-byte tag. Takes a
 *        128-, 192- or 256-bit AES key.
 *
 * @note On ESP-IDF 5.5 with `CONFIG_MBEDTLS_HARDWARE_AES`, a 192-bit key fails with
 *       `errc::not_supported` on targets whose AES engine doesn't support 192-bit keys (all but
 *       the esp32 and esp32s2). Don't rely on 192-bit keys on those targets with ESP-IDF 6.x
 *       either. Use 128- or 256-bit keys.
 */
using cmac = authenticator<algorithm::cmac>;
#endif

} // namespace idfxx::crypto::mac

/** @} */ // end of idfxx_crypto_mac
