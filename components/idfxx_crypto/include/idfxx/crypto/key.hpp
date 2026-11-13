// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/crypto/key>
 * @file key.hpp
 * @brief Cryptographic keys.
 *
 * @defgroup idfxx_crypto_key Keys
 * @ingroup idfxx_crypto
 * @brief Keys held by the crypto library, with a fixed type, size and usage policy.
 *
 * A `key` is created by importing key material (`key::import()`) or by generating a random key
 * (`key::generate()`). Its policy (the operations it permits and the algorithm it may be used
 * with) is fixed when it is created. Keys are volatile: they exist only in RAM, and are destroyed
 * with the `key` object (and at reboot).
 *
 * Algorithm families that take keys provide shortcuts that create a key with the right type and
 * policy, such as `mac::hmac_sha256::import_key()` in @ref idfxx_crypto_mac. A key's permitted
 * algorithm also selects the algorithm of operations that take it from the key, such as
 * `mac::operation`.
 *
 * Separate operations in different tasks can use the same key. That needs the mbedTLS threading
 * layer, so this header is a compile error without `CONFIG_MBEDTLS_THREADING_C=y` (on ESP-IDF 5.5,
 * also set `CONFIG_MBEDTLS_THREADING_PTHREAD=y` and clear `CONFIG_MBEDTLS_THREADING_ALT`).
 * @{
 */

#include <idfxx/byte_view>
#include <idfxx/crypto/error>
#include <idfxx/error>
#include <idfxx/flags>
#include <idfxx/required>

#include <cstddef>
#include <psa/crypto.h>
#include <sdkconfig.h>

// Without the threading layer, the crypto library's key table is unlocked, so keys created, used or
// destroyed from several tasks race. ESP-IDF 5.5 defaults to an alternate threading implementation
// that only the ROM port provides, so its locks fail: select the pthread implementation instead.
#ifndef CONFIG_MBEDTLS_THREADING_C
#error "idfxx_crypto keys require CONFIG_MBEDTLS_THREADING_C=y"
#error "on ESP-IDF 5.5, also CONFIG_MBEDTLS_THREADING_PTHREAD=y and CONFIG_MBEDTLS_THREADING_ALT=n"
#endif

namespace idfxx::crypto {

/** @cond INTERNAL */
// Defined in <idfxx/crypto/mac>; declared here so that a key_algorithm can be constructed from it.
namespace mac {
enum class algorithm : psa_algorithm_t;
} // namespace mac

namespace detail {

// The shortest tag a MAC algorithm can be truncated to, in bytes. PSA refuses shorter tags.
inline constexpr std::size_t min_mac_tag_size = 4;

// The PSA key type a PSA MAC algorithm takes, truncated or not: an HMAC key for HMAC, and an AES
// key for a block-cipher MAC (AES is the only block cipher). 0 for any other algorithm, including
// none. PSA doesn't check a key's permitted algorithm against its type when the key is created, so
// key creation refuses a key type other than this one.
[[nodiscard]] constexpr psa_key_type_t mac_key_type_of(psa_algorithm_t alg) noexcept {
    if (PSA_ALG_IS_HMAC(alg)) {
        return PSA_KEY_TYPE_HMAC;
    }
    if (PSA_ALG_IS_BLOCK_CIPHER_MAC(alg)) {
        return PSA_KEY_TYPE_AES;
    }
    return 0;
}

// The full tag length of a PSA MAC algorithm, truncated or not, in bytes.
[[nodiscard]] constexpr std::size_t mac_full_tag_size(psa_algorithm_t alg) noexcept {
    // Only a block-cipher MAC's length depends on the key type, which key creation fixes
    return PSA_MAC_LENGTH(mac_key_type_of(alg), 0, PSA_ALG_FULL_LENGTH_MAC(alg));
}

// Whether a MAC algorithm's tags can be truncated to `tag_size` bytes: at least min_mac_tag_size,
// and at most the full tag length.
[[nodiscard]] constexpr bool valid_mac_tag_size(mac::algorithm alg, std::size_t tag_size) noexcept {
    return tag_size >= min_mac_tag_size &&
        tag_size <= mac_full_tag_size(static_cast<psa_algorithm_t>(alg));
}

// The PSA algorithm of a MAC algorithm with its tags truncated to `tag_size` bytes, which must be
// valid (valid_mac_tag_size): the algorithm itself at its full tag length, and
// PSA_ALG_TRUNCATED_MAC for a shorter length.
[[nodiscard]] constexpr psa_algorithm_t
truncated_mac_algorithm(mac::algorithm alg, std::size_t tag_size) noexcept {
    auto full = static_cast<psa_algorithm_t>(alg);
    return tag_size == mac_full_tag_size(full) ? full : PSA_ALG_TRUNCATED_MAC(full, tag_size);
}

// Not constexpr: reaching it during constant evaluation makes a key_algorithm with an invalid tag
// length a compile error.
inline void mac_tag_size_out_of_range() noexcept {}

} // namespace detail
/** @endcond */

/**
 * @headerfile <idfxx/crypto/key>
 * @brief Key types.
 *
 * Each enumerator is present only when the key type is enabled in the mbedTLS configuration.
 */
enum class key_type : psa_key_type_t {
#ifdef PSA_WANT_KEY_TYPE_RAW_DATA
    /// Raw data, not usable with any cryptographic algorithm (for example, a stored secret).
    raw_data = PSA_KEY_TYPE_RAW_DATA,
#endif
#ifdef PSA_WANT_KEY_TYPE_HMAC
    /// An HMAC key. Any size, in whole bytes.
    hmac = PSA_KEY_TYPE_HMAC,
#endif
#ifdef PSA_WANT_KEY_TYPE_DERIVE
    /// Secret input for key derivation. Any size, in whole bytes.
    derive = PSA_KEY_TYPE_DERIVE,
#endif
#ifdef PSA_WANT_KEY_TYPE_AES
    /// An AES key: 128, 192 or 256 bits. Used for AES-CMAC.
    aes = PSA_KEY_TYPE_AES,
#endif
};

/**
 * @headerfile <idfxx/crypto/key>
 * @brief Operations a key permits.
 *
 * Combine values with `|` into a `flags<key_usage>`. A key can be used only for the operations
 * its usage permits; anything else fails with `errc::not_permitted`.
 */
enum class key_usage : psa_key_usage_t {
    /// No operations.
    none = 0,
    /// The key material can be read back with `key::export_to()`.
    export_key = PSA_KEY_USAGE_EXPORT,
    /// The key can be copied into a new key with `key::copy()`.
    copy = PSA_KEY_USAGE_COPY,
    /// The key can encrypt messages.
    encrypt = PSA_KEY_USAGE_ENCRYPT,
    /// The key can decrypt messages.
    decrypt = PSA_KEY_USAGE_DECRYPT,
    /// The key can compute a MAC or signature of a message.
    sign_message = PSA_KEY_USAGE_SIGN_MESSAGE,
    /// The key can check a MAC or signature of a message.
    verify_message = PSA_KEY_USAGE_VERIFY_MESSAGE,
    /// The key can sign a precomputed hash. Implies `sign_message`.
    sign_hash = PSA_KEY_USAGE_SIGN_HASH,
    /// The key can check a signature of a precomputed hash. Implies `verify_message`.
    verify_hash = PSA_KEY_USAGE_VERIFY_HASH,
    /// The key can be the secret input of a key derivation.
    derive = PSA_KEY_USAGE_DERIVE,
    /// The key can check the output of a key derivation.
    verify_derivation = PSA_KEY_USAGE_VERIFY_DERIVATION,
};

} // namespace idfxx::crypto

/** @cond INTERNAL */
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::crypto::key_usage> = true;
/** @endcond */

namespace idfxx::crypto {

/**
 * @headerfile <idfxx/crypto/key>
 * @brief The algorithm a key is permitted to be used with.
 *
 * Part of a key's policy: a key can be used only with the algorithm it was created for (or with
 * none, by default). Constructed implicitly from an algorithm enumerator, such as a
 * `mac::algorithm`; later algorithm families add constructors of their own. A MAC algorithm with
 * truncated tags is a different algorithm: a key for it is constructed explicitly, from the MAC
 * algorithm and a tag length known at compile time, or with `truncated()` or `try_truncated()`
 * from a tag length known only at runtime.
 *
 * @code
 * idfxx::crypto::key_algorithm alg = idfxx::crypto::mac::algorithm::hmac_sha256;
 * idfxx::crypto::key_algorithm short_tags{idfxx::crypto::mac::algorithm::hmac_sha256, 16};
 * auto configured = idfxx::crypto::key_algorithm::truncated(
 *     idfxx::crypto::mac::algorithm::hmac_sha256, tag_size
 * );
 * @endcode
 */
class key_algorithm {
public:
    /**
     * @brief Creates a key algorithm that permits no algorithm.
     */
    constexpr key_algorithm() noexcept = default;

    /**
     * @brief Creates a key algorithm that permits a MAC algorithm.
     *
     * @param alg The MAC algorithm.
     */
    constexpr key_algorithm(mac::algorithm alg) noexcept
        : _alg(static_cast<psa_algorithm_t>(alg)) {}

    /**
     * @brief Creates a key algorithm that permits a MAC algorithm with tags truncated to a given
     *        length, known at compile time.
     *
     * A truncated tag is the leftmost `tag_size` bytes of the full tag. A key permitted one tag
     * length can't be used with another, including the full length. This is the algorithm of the
     * keys that `mac::authenticator<alg, tag_size>::import_key()` and `generate_key()` create. For
     * a tag length known only at runtime, use `truncated()` or `try_truncated()`.
     *
     * @param alg The MAC algorithm.
     * @param tag_size The tag length, in bytes: at least 4, and at most the algorithm's full tag
     *                 length. Any other length is a compile error. The full length gives the same
     *                 key algorithm as the one-argument constructor.
     */
    explicit consteval key_algorithm(mac::algorithm alg, std::size_t tag_size) noexcept
        : _alg(detail::truncated_mac_algorithm(alg, tag_size)) {
        if (!detail::valid_mac_tag_size(alg, tag_size)) {
            detail::mac_tag_size_out_of_range();
        }
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a key algorithm that permits a MAC algorithm with tags truncated to a given
     *        length, known at runtime.
     *
     * A truncated tag is the leftmost `tag_size` bytes of the full tag. A key permitted one tag
     * length can't be used with another, including the full length. For a tag length known at
     * compile time, the two-argument constructor checks the length at compile time instead.
     *
     * @code
     * // tag_size came from, for example, configuration naming the protocol's tag length
     * auto alg = idfxx::crypto::key_algorithm::truncated(
     *     idfxx::crypto::mac::algorithm::hmac_sha256, tag_size
     * );
     * auto k = idfxx::crypto::key::import(
     *     {.type = idfxx::crypto::key_type::hmac,
     *      .usage = idfxx::crypto::key_usage::verify_message,
     *      .algorithm = alg},
     *     key_material
     * );
     * @endcode
     *
     * @param alg The MAC algorithm.
     * @param tag_size The tag length, in bytes: at least 4, and at most the algorithm's full tag
     *                 length. The full length gives the same key algorithm as the one-argument
     *                 constructor.
     * @return The key algorithm.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_argument` if `tag_size` is shorter than 4
     *         bytes or longer than the algorithm's full tag length, or `alg` is not a MAC
     *         algorithm.
     */
    [[nodiscard]] static key_algorithm truncated(mac::algorithm alg, std::size_t tag_size) {
        return unwrap(try_truncated(alg, tag_size));
    }
#endif

    /**
     * @brief Creates a key algorithm that permits a MAC algorithm with tags truncated to a given
     *        length, known at runtime.
     *
     * A truncated tag is the leftmost `tag_size` bytes of the full tag. A key permitted one tag
     * length can't be used with another, including the full length. For a tag length known at
     * compile time, the two-argument constructor checks the length at compile time instead.
     *
     * @param alg The MAC algorithm.
     * @param tag_size The tag length, in bytes: at least 4, and at most the algorithm's full tag
     *                 length. The full length gives the same key algorithm as the one-argument
     *                 constructor.
     * @return The key algorithm, or an error.
     * @retval errc::invalid_argument `tag_size` is shorter than 4 bytes or longer than the
     *         algorithm's full tag length, or `alg` is not a MAC algorithm.
     */
    [[nodiscard]] static result<key_algorithm>
    try_truncated(mac::algorithm alg, std::size_t tag_size) {
        if (!detail::valid_mac_tag_size(alg, tag_size)) {
            return error(errc::invalid_argument);
        }
        return key_algorithm{detail::truncated_mac_algorithm(alg, tag_size)};
    }

    /**
     * @brief Returns the underlying PSA algorithm identifier.
     *
     * For a MAC algorithm with truncated tags, this is the truncated algorithm
     * (`PSA_ALG_TRUNCATED_MAC`).
     *
     * @return The `psa_algorithm_t` value, or `PSA_ALG_NONE` if no algorithm is permitted.
     */
    [[nodiscard]] constexpr psa_algorithm_t idf_algorithm() const noexcept { return _alg; }

    /**
     * @brief Compares two key algorithms for equality.
     *
     * @param other The key algorithm to compare with.
     * @return True if both permit the same algorithm.
     */
    [[nodiscard]] constexpr bool operator==(const key_algorithm& other) const noexcept = default;

private:
    explicit constexpr key_algorithm(psa_algorithm_t alg) noexcept
        : _alg(alg) {}

    psa_algorithm_t _alg = PSA_ALG_NONE;
};

/**
 * @headerfile <idfxx/crypto/key>
 * @brief A cryptographic key, held by the crypto library.
 *
 * The key material stays inside the crypto library: operations take the key, not its bytes, and
 * the material can be read back only if the key's usage includes `key_usage::export_key`. The
 * key's type, size and policy are fixed when it is created.
 *
 * Keys are volatile: they exist only in RAM, and are destroyed with the `key` object (and at
 * reboot).
 *
 * This type is non-copyable and move-only. A moved-from object must not be used: any operation
 * other than destruction or assignment is undefined behavior.
 *
 * @code
 * auto k = idfxx::crypto::key::import(
 *     {.type = idfxx::crypto::key_type::hmac,
 *      .usage = idfxx::crypto::key_usage::sign_message | idfxx::crypto::key_usage::verify_message,
 *      .algorithm = idfxx::crypto::mac::algorithm::hmac_sha256},
 *     key_material
 * );
 * auto tag = idfxx::crypto::mac::hmac_sha256::compute(k, "message");
 * @endcode
 */
class key {
public:
    /**
     * @brief Key creation parameters.
     */
    struct config {
        /// The key type, which also determines the valid key sizes.
        idfxx::required<key_type> type;
        /// The operations the key permits.
        flags<key_usage> usage;
        /// The algorithm the key may be used with. Defaults to none. A MAC algorithm must take the
        /// key type: `key_type::hmac` for HMAC, `key_type::aes` for AES-CMAC.
        key_algorithm algorithm{};
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a key from key material.
     *
     * The key's size is taken from the material.
     *
     * @param cfg The key's type and policy.
     * @param material The key material. Must not be empty, and must be a valid size for the key
     *                 type (for example 16, 24 or 32 bytes for `key_type::aes`).
     * @return The key.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::invalid_argument` if `material` is empty or not
     *         a valid size for the key type, or if `cfg.algorithm` is a MAC algorithm that doesn't
     *         take the key type; `errc::not_supported` if the key type or size is not supported.
     */
    [[nodiscard]] static key import(const config& cfg, byte_view material) {
        return unwrap(try_import(cfg, material));
    }
#endif

    /**
     * @brief Creates a key from key material.
     *
     * The key's size is taken from the material.
     *
     * @param cfg The key's type and policy.
     * @param material The key material. Must not be empty, and must be a valid size for the key
     *                 type (for example 16, 24 or 32 bytes for `key_type::aes`).
     * @return The key, or an error.
     * @retval errc::invalid_argument `material` is empty or not a valid size for the key type, or
     *         `cfg.algorithm` is a MAC algorithm that doesn't take the key type.
     * @retval errc::not_supported The key type or size is not supported.
     */
    [[nodiscard]] static result<key> try_import(const config& cfg, byte_view material);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a key from random key material.
     *
     * @param cfg The key's type and policy.
     * @param bits The key size, in bits. Must be a valid size for the key type (for example 128,
     *             192 or 256 for `key_type::aes`, or a multiple of 8 for `key_type::hmac`).
     * @return The key.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::invalid_argument` or `errc::not_supported` if
     *         `bits` is not a valid size for the key type, `errc::invalid_argument` if
     *         `cfg.algorithm` is a MAC algorithm that doesn't take the key type,
     *         `errc::insufficient_entropy` if the random generator fails.
     */
    [[nodiscard]] static key generate(const config& cfg, std::size_t bits) {
        return unwrap(try_generate(cfg, bits));
    }
#endif

    /**
     * @brief Creates a key from random key material.
     *
     * @param cfg The key's type and policy.
     * @param bits The key size, in bits. Must be a valid size for the key type (for example 128,
     *             192 or 256 for `key_type::aes`, or a multiple of 8 for `key_type::hmac`).
     * @return The key, or an error.
     * @retval errc::invalid_argument `bits` is not a valid size for the key type, or
     *         `cfg.algorithm` is a MAC algorithm that doesn't take the key type.
     * @retval errc::not_supported The key type or size is not supported.
     * @retval errc::insufficient_entropy The random generator failed.
     */
    [[nodiscard]] static result<key> try_generate(const config& cfg, std::size_t bits);

    /**
     * @brief Destroys the key, erasing its key material.
     */
    ~key();

    key(const key&) = delete;
    key& operator=(const key&) = delete;

    /**
     * @brief Move constructor. Transfers the key; `other` is left moved-from.
     *
     * @param other The key to move from.
     */
    key(key&& other) noexcept;

    /**
     * @brief Move assignment. Destroys this key, if any, and takes over `other`'s; `other` is left
     *        moved-from.
     *
     * @param other The key to move from.
     * @return A reference to this key.
     */
    key& operator=(key&& other) noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new key with the same key material, permitting fewer operations.
     *
     * The new key has this key's type, size and algorithm, and permits the operations in `usage`
     * that this key also permits; the others are dropped. For example, a key that only checks
     * tags can be made from one that also computes them.
     *
     * @code
     * auto checker = k.copy(idfxx::crypto::key_usage::verify_message);
     * @endcode
     *
     * @param usage The operations the new key permits, among those this key permits.
     * @return The new key.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_permitted` if this key's usage does not
     *         include `key_usage::copy`.
     */
    [[nodiscard]] key copy(flags<key_usage> usage) const { return unwrap(try_copy(usage)); }
#endif

    /**
     * @brief Creates a new key with the same key material, permitting fewer operations.
     *
     * The new key has this key's type, size and algorithm, and permits the operations in `usage`
     * that this key also permits; the others are dropped.
     *
     * @param usage The operations the new key permits, among those this key permits.
     * @return The new key, or an error.
     * @retval errc::not_permitted This key's usage does not include `key_usage::copy`.
     */
    [[nodiscard]] result<key> try_copy(flags<key_usage> usage) const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Copies the key material into a buffer.
     *
     * @param out The buffer to fill. A buffer of `(bits() + 7) / 8` bytes is large enough. The
     *            contents of `out` past the bytes written are unspecified.
     * @return The filled prefix of `out`: the key material.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure: `errc::not_permitted` if the key's usage does not
     *         include `key_usage::export_key`, `errc::buffer_too_small` if `out` is too small.
     */
    [[nodiscard]] byte_span export_to(byte_span out) const { return unwrap(try_export_to(out)); }
#endif

    /**
     * @brief Copies the key material into a buffer.
     *
     * @param out The buffer to fill. A buffer of `(bits() + 7) / 8` bytes is large enough. The
     *            contents of `out` past the bytes written are unspecified.
     * @return The filled prefix of `out` (the key material), or an error.
     * @retval errc::not_permitted The key's usage does not include `key_usage::export_key`.
     * @retval errc::buffer_too_small `out` is too small for the key material.
     */
    [[nodiscard]] result<byte_span> try_export_to(byte_span out) const;

    /**
     * @brief Returns the key type.
     *
     * @return The key type.
     */
    [[nodiscard]] key_type type() const noexcept { return _type; }

    /**
     * @brief Returns the key size.
     *
     * @return The key size, in bits.
     */
    [[nodiscard]] std::size_t bits() const noexcept { return _bits; }

    /**
     * @brief Returns the operations the key permits.
     *
     * Includes the usages implied by those requested when the key was created (`sign_hash` implies
     * `sign_message`, and `verify_hash` implies `verify_message`).
     *
     * @return The key's usage flags.
     */
    [[nodiscard]] flags<key_usage> usage() const noexcept { return _usage; }

    /**
     * @brief Returns the algorithm the key may be used with.
     *
     * @return The key's permitted algorithm.
     */
    [[nodiscard]] key_algorithm algorithm() const noexcept { return _algorithm; }

    /**
     * @brief Returns the underlying PSA key identifier.
     *
     * The identifier remains owned by this object: do not destroy it.
     *
     * @return The `psa_key_id_t` identifying the key.
     */
    [[nodiscard]] psa_key_id_t idf_handle() const noexcept { return _id; }

private:
    key(psa_key_id_t id, key_type type, key_algorithm algorithm) noexcept;

    // Takes ownership of a key the crypto library created, given the status of its creation, and
    // reads back the size and usage the library settled on.
    [[nodiscard]] static result<key>
    _adopt(psa_status_t status, psa_key_id_t id, key_type type, key_algorithm algorithm);

    psa_key_id_t _id;
    key_type _type;
    std::size_t _bits;
    flags<key_usage> _usage;
    key_algorithm _algorithm;
};

} // namespace idfxx::crypto

/** @} */ // end of idfxx_crypto_key
