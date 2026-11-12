// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/crypto/hash>
 * @file hash.hpp
 * @brief Message digests (hash functions).
 *
 * @defgroup idfxx_crypto_hash Hashing
 * @ingroup idfxx_crypto
 * @brief One-shot and incremental message digests: SHA-2, SHA-3, SHA-1 and MD5.
 *
 * Each algorithm has a `hasher` alias (`idfxx::crypto::hash::sha256`,
 * `idfxx::crypto::hash::sha3_256`, ...) whose static `compute()` and `compare()` digest a complete
 * message in one call, and whose instances digest a message supplied in pieces with `update()`. Use
 * these when the algorithm is known at compile time.
 *
 * `idfxx::crypto::hash::operation` offers the same operations with an algorithm chosen at runtime,
 * writing digests into a caller-supplied buffer. `hash::digest_size()` and `hash::block_size()`
 * give an algorithm's sizes, in constant expressions too.
 *
 * An algorithm (its `hash::algorithm` enumerator and its alias) is available only when it is
 * enabled in the mbedTLS configuration (`Component config` → `mbedTLS` in menuconfig). MD5, SHA-1
 * and the SHA-2 family are enabled by default. SHA-3 is enabled with `CONFIG_MBEDTLS_SHA3_C`.
 * @{
 */

#include <idfxx/byte_view>
#include <idfxx/crypto/error>
#include <idfxx/error>

#include <array>
#include <cstddef>
#include <memory>
#include <psa/crypto.h>
#include <span>
#include <string_view>
#include <utility>

namespace idfxx::crypto::hash {

/**
 * @headerfile <idfxx/crypto/hash>
 * @brief Hash algorithms.
 *
 * Each enumerator is present only when the algorithm is enabled in the mbedTLS configuration.
 */
enum class algorithm : psa_algorithm_t {
#ifdef PSA_WANT_ALG_MD5
    /// MD5 (RFC 1321), 16-byte digest. Not collision-resistant.
    md5 = PSA_ALG_MD5,
#endif
#ifdef PSA_WANT_ALG_SHA_1
    /// SHA-1 (FIPS 180-4), 20-byte digest. Not collision-resistant.
    sha1 = PSA_ALG_SHA_1,
#endif
#ifdef PSA_WANT_ALG_SHA_224
    /// SHA-224 (FIPS 180-4), 28-byte digest.
    sha224 = PSA_ALG_SHA_224,
#endif
#ifdef PSA_WANT_ALG_SHA_256
    /// SHA-256 (FIPS 180-4), 32-byte digest.
    sha256 = PSA_ALG_SHA_256,
#endif
#ifdef PSA_WANT_ALG_SHA_384
    /// SHA-384 (FIPS 180-4), 48-byte digest.
    sha384 = PSA_ALG_SHA_384,
#endif
#ifdef PSA_WANT_ALG_SHA_512
    /// SHA-512 (FIPS 180-4), 64-byte digest.
    sha512 = PSA_ALG_SHA_512,
#endif
#ifdef PSA_WANT_ALG_SHA3_224
    /// SHA3-224 (FIPS 202), 28-byte digest.
    sha3_224 = PSA_ALG_SHA3_224,
#endif
#ifdef PSA_WANT_ALG_SHA3_256
    /// SHA3-256 (FIPS 202), 32-byte digest.
    sha3_256 = PSA_ALG_SHA3_256,
#endif
#ifdef PSA_WANT_ALG_SHA3_384
    /// SHA3-384 (FIPS 202), 48-byte digest.
    sha3_384 = PSA_ALG_SHA3_384,
#endif
#ifdef PSA_WANT_ALG_SHA3_512
    /// SHA3-512 (FIPS 202), 64-byte digest.
    sha3_512 = PSA_ALG_SHA3_512,
#endif
};

/**
 * @headerfile <idfxx/crypto/hash>
 * @brief Returns the size of a hash algorithm's digest.
 *
 * Usable in constant expressions, for example to size a buffer for a runtime-chosen algorithm's
 * digest. The size is known for every hash algorithm of the PSA Certified Crypto API, whether or
 * not it is enabled in the mbedTLS configuration.
 *
 * @code
 * static_assert(idfxx::crypto::hash::digest_size(idfxx::crypto::hash::algorithm::sha256) == 32);
 * @endcode
 *
 * @param alg The hash algorithm.
 * @return The size of a digest, in bytes, or 0 if `alg` is not a known hash algorithm.
 */
[[nodiscard]] constexpr std::size_t digest_size(algorithm alg) noexcept {
    const auto a = static_cast<psa_algorithm_t>(alg);
    return PSA_ALG_IS_HASH(a) ? PSA_HASH_LENGTH(a) : 0;
}

/**
 * @headerfile <idfxx/crypto/hash>
 * @brief Returns the size of a hash algorithm's input block.
 *
 * Usable in constant expressions. The size is known for every hash algorithm of the PSA Certified
 * Crypto API, whether or not it is enabled in the mbedTLS configuration.
 *
 * @param alg The hash algorithm.
 * @return The size of an input block, in bytes, or 0 if `alg` is not a known hash algorithm.
 */
[[nodiscard]] constexpr std::size_t block_size(algorithm alg) noexcept {
    const auto a = static_cast<psa_algorithm_t>(alg);
    return PSA_ALG_IS_HASH(a) ? PSA_HASH_BLOCK_LENGTH(a) : 0;
}

template<algorithm Alg>
class hasher;

/**
 * @headerfile <idfxx/crypto/hash>
 * @brief Computes message digests with a hash algorithm chosen at runtime.
 *
 * Use `hash::operation` when the algorithm is only known at runtime, for example when a manifest or
 * protocol message names it. When the algorithm is known at compile time, use its `hasher` alias
 * (`sha256`, `sha3_256`, ...) instead: it cannot fail to construct, and its digests are fixed-size
 * arrays.
 *
 * The static `compute()` and `compare()` digest a complete message in one call. An instance
 * digests a message supplied in pieces: `update()` adds data, and `finish()` (or `verify()`)
 * completes the message. After `finish()` or `verify()`, the operation is ready for a new message.
 * A failed `update()`, `finish()` or `verify()` discards the partially digested message, leaving
 * the operation ready for a new message, except that `finish()` failing with
 * `errc::buffer_too_small` leaves the message unchanged. A failed `clone()` leaves the operation
 * unchanged.
 *
 * Creating an operation checks the algorithm: it fails with `errc::not_supported` for a hash
 * algorithm that is not supported (for example one not enabled in the mbedTLS configuration), and
 * with `errc::invalid_argument` for a value that is not a hash algorithm.
 *
 * This type is non-copyable and move-only. A moved-from object must not be used: any operation
 * other than destruction or assignment is undefined behavior. `clone()` copies a partially
 * digested message, for example to digest several messages that share a prefix.
 *
 * @code
 * // The manifest names the digest algorithm alongside the expected digest
 * std::optional<idfxx::crypto::hash::algorithm> algorithm_named(std::string_view name) {
 *     if (name == "sha256") {
 *         return idfxx::crypto::hash::algorithm::sha256;
 *     }
 *     if (name == "sha512") {
 *         return idfxx::crypto::hash::algorithm::sha512;
 *     }
 *     return std::nullopt;
 * }
 *
 * auto alg = algorithm_named(manifest.digest_algorithm);
 * if (!alg) {
 *     return; // unsupported algorithm
 * }
 * idfxx::crypto::hash::operation op{*alg};
 * for (auto piece : file_pieces) {
 *     op.update(piece);
 * }
 * if (!op.verify(manifest.digest)) {
 *     // the file was altered
 * }
 * @endcode
 */
class operation {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Computes the digest of a message.
     *
     * @param alg The hash algorithm.
     * @param data The message.
     * @param digest The buffer to write the digest to, at least `hash::digest_size(alg)` bytes.
     * @return The filled prefix of `digest`: its first `hash::digest_size(alg)` bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_supported` if `alg` is not supported,
     *         `errc::invalid_argument` if it is not a hash algorithm, `errc::buffer_too_small` if
     *         `digest` is too small, or another code on failure (for example
     *         `errc::hardware_failure`).
     */
    [[nodiscard]] static byte_span compute(hash::algorithm alg, byte_view data, byte_span digest) {
        return unwrap(try_compute(alg, data, digest));
    }

    /**
     * @brief Computes the digest of a message.
     *
     * @param alg The hash algorithm.
     * @param data The message, as its bytes.
     * @param digest The buffer to write the digest to, at least `hash::digest_size(alg)` bytes.
     * @return The filled prefix of `digest`: its first `hash::digest_size(alg)` bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_supported` if `alg` is not supported,
     *         `errc::invalid_argument` if it is not a hash algorithm, `errc::buffer_too_small` if
     *         `digest` is too small, or another code on failure (for example
     *         `errc::hardware_failure`).
     */
    [[nodiscard]] static byte_span
    compute(hash::algorithm alg, std::string_view data, byte_span digest) {
        return unwrap(try_compute(alg, data, digest));
    }
#endif

    /**
     * @brief Computes the digest of a message.
     *
     * @param alg The hash algorithm.
     * @param data The message.
     * @param digest The buffer to write the digest to, at least `hash::digest_size(alg)` bytes.
     * @return The filled prefix of `digest` (its first `hash::digest_size(alg)` bytes), or an
     *         error.
     * @retval errc::not_supported `alg` is not a supported hash algorithm, for example one not
     *         enabled in the mbedTLS configuration.
     * @retval errc::invalid_argument `alg` is not a hash algorithm.
     * @retval errc::buffer_too_small `digest` is smaller than `hash::digest_size(alg)`.
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] static result<byte_span>
    try_compute(hash::algorithm alg, byte_view data, byte_span digest);

    /**
     * @brief Computes the digest of a message.
     *
     * @param alg The hash algorithm.
     * @param data The message, as its bytes.
     * @param digest The buffer to write the digest to, at least `hash::digest_size(alg)` bytes.
     * @return The filled prefix of `digest` (its first `hash::digest_size(alg)` bytes), or an
     *         error.
     * @retval errc::not_supported `alg` is not a supported hash algorithm, for example one not
     *         enabled in the mbedTLS configuration.
     * @retval errc::invalid_argument `alg` is not a hash algorithm.
     * @retval errc::buffer_too_small `digest` is smaller than `hash::digest_size(alg)`.
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] static result<byte_span>
    try_compute(hash::algorithm alg, std::string_view data, byte_span digest) {
        return try_compute(alg, std::as_bytes(std::span{data}), digest);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Checks whether a message has the expected digest.
     *
     * The digests are compared in constant time.
     *
     * @param alg The hash algorithm.
     * @param data The message.
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_supported` if `alg` is not supported,
     *         `errc::invalid_argument` if it is not a hash algorithm, or another code on failure
     *         (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static bool compare(hash::algorithm alg, byte_view data, byte_view expected) {
        return unwrap(try_compare(alg, data, expected));
    }

    /**
     * @brief Checks whether a message has the expected digest.
     *
     * The digests are compared in constant time.
     *
     * @param alg The hash algorithm.
     * @param data The message, as its bytes.
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_supported` if `alg` is not supported,
     *         `errc::invalid_argument` if it is not a hash algorithm, or another code on failure
     *         (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static bool
    compare(hash::algorithm alg, std::string_view data, byte_view expected) {
        return unwrap(try_compare(alg, data, expected));
    }
#endif

    /**
     * @brief Checks whether a message has the expected digest.
     *
     * The digests are compared in constant time.
     *
     * @param alg The hash algorithm.
     * @param data The message.
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false if it does not, or an error.
     * @retval errc::not_supported `alg` is not a supported hash algorithm, for example one not
     *         enabled in the mbedTLS configuration.
     * @retval errc::invalid_argument `alg` is not a hash algorithm.
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] static result<bool>
    try_compare(hash::algorithm alg, byte_view data, byte_view expected);

    /**
     * @brief Checks whether a message has the expected digest.
     *
     * The digests are compared in constant time.
     *
     * @param alg The hash algorithm.
     * @param data The message, as its bytes.
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false if it does not, or an error.
     * @retval errc::not_supported `alg` is not a supported hash algorithm, for example one not
     *         enabled in the mbedTLS configuration.
     * @retval errc::invalid_argument `alg` is not a hash algorithm.
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] static result<bool>
    try_compare(hash::algorithm alg, std::string_view data, byte_view expected) {
        return try_compare(alg, std::as_bytes(std::span{data}), expected);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates an operation ready for a new message.
     *
     * @param alg The hash algorithm.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::not_supported` if `alg` is not supported,
     *         `errc::invalid_argument` if it is not a hash algorithm, or another code on failure
     *         (for example a PSA Crypto initialization failure).
     */
    [[nodiscard]] explicit operation(hash::algorithm alg)
        : operation(unwrap(make(alg))) {}
#endif

    /**
     * @brief Creates an operation ready for a new message.
     *
     * @param alg The hash algorithm.
     * @return The operation, or an error.
     * @retval errc::not_supported `alg` is not a supported hash algorithm, for example one not
     *         enabled in the mbedTLS configuration.
     * @retval errc::invalid_argument `alg` is not a hash algorithm.
     */
    [[nodiscard]] static result<operation> make(hash::algorithm alg);

    /**
     * @brief Destroys the operation, discarding any partially digested message.
     */
    ~operation();

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;

    /**
     * @brief Move constructor. Transfers the partially digested message, if any.
     *
     * @param other The operation to move from.
     */
    operation(operation&& other) noexcept;

    /**
     * @brief Move assignment. Discards this operation's partially digested message, if any, and
     *        takes over `other`'s.
     *
     * @param other The operation to move from.
     * @return A reference to this operation.
     */
    operation& operator=(operation&& other) noexcept;

    /**
     * @brief Returns the hash algorithm.
     *
     * @return The algorithm the operation was created with.
     */
    [[nodiscard]] hash::algorithm algorithm() const noexcept;

    /**
     * @brief Returns the size of a digest.
     *
     * @return The size of the algorithm's digest, in bytes: `hash::digest_size(algorithm())`.
     */
    [[nodiscard]] std::size_t digest_size() const noexcept {
        return hash::digest_size(algorithm());
    }

    /**
     * @brief Returns the size of the algorithm's input block.
     *
     * @return The size of an input block, in bytes: `hash::block_size(algorithm())`.
     */
    [[nodiscard]] std::size_t block_size() const noexcept { return hash::block_size(algorithm()); }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially digested message is discarded and the operation is ready for a
     * new message.
     *
     * @param data The data to add.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    void update(byte_view data) { unwrap(try_update(data)); }

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially digested message is discarded and the operation is ready for a
     * new message.
     *
     * @param data The text to add, as its bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    void update(std::string_view data) { unwrap(try_update(data)); }
#endif

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially digested message is discarded and the operation is ready for a
     * new message.
     *
     * @param data The data to add.
     * @return Success, or an error (for example `errc::hardware_failure`).
     */
    [[nodiscard]] result<void> try_update(byte_view data);

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially digested message is discarded and the operation is ready for a
     * new message.
     *
     * @param data The text to add, as its bytes.
     * @return Success, or an error (for example `errc::hardware_failure`).
     */
    [[nodiscard]] result<void> try_update(std::string_view data) {
        return try_update(std::as_bytes(std::span{data}));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Completes the message and writes its digest.
     *
     * Without any preceding `update()`, writes the digest of the empty message. Afterward, the
     * operation is ready for a new message. If `digest` is too small, the message is left
     * unchanged, so it can be finished again with a larger buffer; on any other failure it is
     * discarded.
     *
     * @param digest The buffer to write the digest to, at least `digest_size()` bytes.
     * @return The filled prefix of `digest`: its first `digest_size()` bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::buffer_too_small` if `digest` is too small, or another
     *         code on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] byte_span finish(byte_span digest) { return unwrap(try_finish(digest)); }
#endif

    /**
     * @brief Completes the message and writes its digest.
     *
     * Without any preceding `try_update()`, writes the digest of the empty message. Afterward,
     * the operation is ready for a new message. If `digest` is too small, the message is left
     * unchanged, so it can be finished again with a larger buffer; on any other failure it is
     * discarded.
     *
     * @param digest The buffer to write the digest to, at least `digest_size()` bytes.
     * @return The filled prefix of `digest` (its first `digest_size()` bytes), or an error.
     * @retval errc::buffer_too_small `digest` is smaller than `digest_size()`.
     * @retval errc::hardware_failure The hardware failed (or another code for another failure).
     */
    [[nodiscard]] result<byte_span> try_finish(byte_span digest);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Completes the message and checks whether it has the expected digest.
     *
     * The digests are compared in constant time. Afterward, the operation is ready for a new
     * message.
     *
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] bool verify(byte_view expected) { return unwrap(try_verify(expected)); }
#endif

    /**
     * @brief Completes the message and checks whether it has the expected digest.
     *
     * The digests are compared in constant time. Afterward, whether or not it succeeded, the
     * operation is ready for a new message.
     *
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false if it does not, or an error
     *         (for example `errc::hardware_failure`).
     */
    [[nodiscard]] result<bool> try_verify(byte_view expected);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates an independent copy of this operation, including its partially digested
     *        message.
     *
     * @return The copy.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`); this operation
     *         is unchanged.
     */
    [[nodiscard]] operation clone() const { return unwrap(try_clone()); }
#endif

    /**
     * @brief Creates an independent copy of this operation, including its partially digested
     *        message.
     *
     * @return The copy, or an error (for example `errc::hardware_failure`); on failure this
     *         operation is unchanged.
     */
    [[nodiscard]] result<operation> try_clone() const;

    /**
     * @brief Discards the partially digested message, if any, leaving the operation ready for a
     *        new message.
     */
    void reset() noexcept;

private:
    template<hash::algorithm Alg>
    friend class hasher;

    struct state;

    // Creates an operation without checking the algorithm, for `hasher`, whose algorithm is a
    // compile-time constant. An algorithm that is not supported fails on first use instead.
    struct unchecked_t {};
    operation(unchecked_t, hash::algorithm alg);

    std::unique_ptr<state> _state;
};

/**
 * @headerfile <idfxx/crypto/hash>
 * @brief Computes message digests with a fixed hash algorithm.
 *
 * The static `compute()` and `compare()` digest a complete message in one call. An instance
 * digests a message supplied in pieces: `update()` adds data, and `finish()` (or `verify()`)
 * completes the message. After `finish()` or `verify()`, and after a failed `update()`,
 * `finish()` or `verify()`, the hasher is ready for a new message. A failed `clone()` leaves the
 * hasher unchanged.
 *
 * Use one of the algorithm aliases (`sha256`, `sha3_256`, ...) rather than naming the template
 * directly. When the algorithm is only known at runtime, use `hash::operation` instead.
 *
 * This type is non-copyable and move-only. A moved-from object must not be used: any operation
 * other than destruction or assignment is undefined behavior. `clone()` copies a partially
 * digested message, for example to digest several messages that share a prefix.
 *
 * @code
 * // One-shot
 * auto digest = idfxx::crypto::hash::sha256::compute("abc");
 *
 * // Incremental
 * idfxx::crypto::hash::sha256 hasher;
 * hasher.update(header);
 * hasher.update(body);
 * if (!hasher.verify(expected_digest)) {
 *     // the message was altered
 * }
 * @endcode
 *
 * @tparam Alg The hash algorithm.
 */
template<algorithm Alg>
class hasher {
public:
    /// The hash algorithm.
    static constexpr hash::algorithm algorithm = Alg;

    /// The size of a digest, in bytes.
    static constexpr std::size_t digest_size = hash::digest_size(Alg);

    /// The size of the algorithm's input block, in bytes.
    static constexpr std::size_t block_size = hash::block_size(Alg);

    /// A digest.
    using digest_type = std::array<std::byte, digest_size>;

    /** @cond INTERNAL */
    static_assert(digest_size > 0, "hasher requires a hash algorithm known to PSA Crypto");
    /** @endcond */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Computes the digest of a message.
     *
     * @param data The message.
     * @return The digest.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static digest_type compute(byte_view data) { return unwrap(try_compute(data)); }

    /**
     * @brief Computes the digest of a message.
     *
     * @param data The message, as its bytes.
     * @return The digest.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static digest_type compute(std::string_view data) {
        return unwrap(try_compute(data));
    }
#endif

    /**
     * @brief Computes the digest of a message.
     *
     * @param data The message.
     * @return The digest, or an error (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static result<digest_type> try_compute(byte_view data) {
        digest_type digest;
        return operation::try_compute(Alg, data, digest).transform([&](byte_span) {
            return digest;
        });
    }

    /**
     * @brief Computes the digest of a message.
     *
     * @param data The message, as its bytes.
     * @return The digest, or an error (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static result<digest_type> try_compute(std::string_view data) {
        return try_compute(std::as_bytes(std::span{data}));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Checks whether a message has the expected digest.
     *
     * The digests are compared in constant time.
     *
     * @param data The message.
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static bool compare(byte_view data, byte_view expected) {
        return unwrap(try_compare(data, expected));
    }

    /**
     * @brief Checks whether a message has the expected digest.
     *
     * The digests are compared in constant time.
     *
     * @param data The message, as its bytes.
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static bool compare(std::string_view data, byte_view expected) {
        return unwrap(try_compare(data, expected));
    }
#endif

    /**
     * @brief Checks whether a message has the expected digest.
     *
     * The digests are compared in constant time.
     *
     * @param data The message.
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false if it does not, or an error
     *         (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static result<bool> try_compare(byte_view data, byte_view expected) {
        return operation::try_compare(Alg, data, expected);
    }

    /**
     * @brief Checks whether a message has the expected digest.
     *
     * The digests are compared in constant time.
     *
     * @param data The message, as its bytes.
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false if it does not, or an error
     *         (for example `errc::hardware_failure`).
     */
    [[nodiscard]] static result<bool> try_compare(std::string_view data, byte_view expected) {
        return try_compare(std::as_bytes(std::span{data}), expected);
    }

    /**
     * @brief Creates a hasher ready for a new message.
     *
     * Allocates the hasher's state; allocation failure is treated as fatal (`std::bad_alloc`, or
     * abort when exceptions are disabled). Available whether or not exceptions are enabled.
     */
    hasher()
        : _op(operation::unchecked_t{}, Alg) {}

    /**
     * @brief Destroys the hasher, discarding any partially digested message.
     */
    ~hasher() = default;

    hasher(const hasher&) = delete;
    hasher& operator=(const hasher&) = delete;

    /**
     * @brief Move constructor. Transfers the partially digested message, if any.
     *
     * @param other The hasher to move from.
     */
    hasher(hasher&& other) noexcept = default;

    /**
     * @brief Move assignment. Discards this hasher's partially digested message, if any, and
     *        takes over `other`'s.
     *
     * @param other The hasher to move from.
     * @return A reference to this hasher.
     */
    hasher& operator=(hasher&& other) noexcept = default;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially digested message is discarded and the hasher is ready for a new
     * message.
     *
     * @param data The data to add.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    void update(byte_view data) { unwrap(try_update(data)); }

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially digested message is discarded and the hasher is ready for a new
     * message.
     *
     * @param data The text to add, as its bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    void update(std::string_view data) { unwrap(try_update(data)); }
#endif

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially digested message is discarded and the hasher is ready for a new
     * message.
     *
     * @param data The data to add.
     * @return Success, or an error (for example `errc::hardware_failure`).
     */
    [[nodiscard]] result<void> try_update(byte_view data) { return _op.try_update(data); }

    /**
     * @brief Adds data to the message.
     *
     * On failure, the partially digested message is discarded and the hasher is ready for a new
     * message.
     *
     * @param data The text to add, as its bytes.
     * @return Success, or an error (for example `errc::hardware_failure`).
     */
    [[nodiscard]] result<void> try_update(std::string_view data) {
        return try_update(std::as_bytes(std::span{data}));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Completes the message and returns its digest.
     *
     * Without any preceding `update()`, returns the digest of the empty message. Afterward, the
     * hasher is ready for a new message.
     *
     * @return The digest.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] digest_type finish() { return unwrap(try_finish()); }
#endif

    /**
     * @brief Completes the message and returns its digest.
     *
     * Without any preceding `try_update()`, returns the digest of the empty message. Afterward,
     * whether or not it succeeded, the hasher is ready for a new message.
     *
     * @return The digest, or an error (for example `errc::hardware_failure`).
     */
    [[nodiscard]] result<digest_type> try_finish() {
        digest_type digest;
        return _op.try_finish(digest).transform([&](byte_span) { return digest; });
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Completes the message and checks whether it has the expected digest.
     *
     * The digests are compared in constant time. Afterward, the hasher is ready for a new
     * message.
     *
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false otherwise.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`).
     */
    [[nodiscard]] bool verify(byte_view expected) { return unwrap(try_verify(expected)); }
#endif

    /**
     * @brief Completes the message and checks whether it has the expected digest.
     *
     * The digests are compared in constant time. Afterward, whether or not it succeeded, the
     * hasher is ready for a new message.
     *
     * @param expected The expected digest. A digest of the wrong length never matches.
     * @return True if the message's digest equals `expected`, false if it does not, or an error
     *         (for example `errc::hardware_failure`).
     */
    [[nodiscard]] result<bool> try_verify(byte_view expected) { return _op.try_verify(expected); }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates an independent copy of this hasher, including its partially digested
     *        message.
     *
     * @return The copy.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (for example `errc::hardware_failure`); this hasher
     *         is unaffected.
     */
    [[nodiscard]] hasher clone() const { return unwrap(try_clone()); }
#endif

    /**
     * @brief Creates an independent copy of this hasher, including its partially digested
     *        message.
     *
     * @return The copy, or an error (for example `errc::hardware_failure`); on failure this
     *         hasher is unaffected.
     */
    [[nodiscard]] result<hasher> try_clone() const {
        return _op.try_clone().transform([](operation&& op) { return hasher{std::move(op)}; });
    }

    /**
     * @brief Discards the partially digested message, if any, leaving the hasher ready for a new
     *        message.
     */
    void reset() noexcept { _op.reset(); }

private:
    explicit hasher(operation op) noexcept
        : _op(std::move(op)) {}

    operation _op;
};

#ifdef PSA_WANT_ALG_MD5
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief MD5 (RFC 1321) hasher, producing a 16-byte digest.
 *
 * @warning MD5 is not collision-resistant. Use it only to interoperate with existing formats and
 *          protocols that require it, never for security.
 */
using md5 = hasher<algorithm::md5>;
#endif

#ifdef PSA_WANT_ALG_SHA_1
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief SHA-1 (FIPS 180-4) hasher, producing a 20-byte digest.
 *
 * @warning SHA-1 is not collision-resistant. Use it only to interoperate with existing formats
 *          and protocols that require it, never for security.
 */
using sha1 = hasher<algorithm::sha1>;
#endif

#ifdef PSA_WANT_ALG_SHA_224
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief SHA-224 (FIPS 180-4) hasher, producing a 28-byte digest.
 */
using sha224 = hasher<algorithm::sha224>;
#endif

#ifdef PSA_WANT_ALG_SHA_256
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief SHA-256 (FIPS 180-4) hasher, producing a 32-byte digest.
 */
using sha256 = hasher<algorithm::sha256>;
#endif

#ifdef PSA_WANT_ALG_SHA_384
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief SHA-384 (FIPS 180-4) hasher, producing a 48-byte digest.
 */
using sha384 = hasher<algorithm::sha384>;
#endif

#ifdef PSA_WANT_ALG_SHA_512
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief SHA-512 (FIPS 180-4) hasher, producing a 64-byte digest.
 */
using sha512 = hasher<algorithm::sha512>;
#endif

#ifdef PSA_WANT_ALG_SHA3_224
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief SHA3-224 (FIPS 202) hasher, producing a 28-byte digest.
 */
using sha3_224 = hasher<algorithm::sha3_224>;
#endif

#ifdef PSA_WANT_ALG_SHA3_256
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief SHA3-256 (FIPS 202) hasher, producing a 32-byte digest.
 */
using sha3_256 = hasher<algorithm::sha3_256>;
#endif

#ifdef PSA_WANT_ALG_SHA3_384
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief SHA3-384 (FIPS 202) hasher, producing a 48-byte digest.
 */
using sha3_384 = hasher<algorithm::sha3_384>;
#endif

#ifdef PSA_WANT_ALG_SHA3_512
/**
 * @headerfile <idfxx/crypto/hash>
 * @brief SHA3-512 (FIPS 202) hasher, producing a 64-byte digest.
 */
using sha3_512 = hasher<algorithm::sha3_512>;
#endif

} // namespace idfxx::crypto::hash

/** @} */ // end of idfxx_crypto_hash
