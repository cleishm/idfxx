// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx crypto keys
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "crypto_test_utils.hpp"
#include "idfxx/crypto/key"
#include "idfxx/crypto/mac"
#include "unity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <psa/crypto.h>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace idfxx;
using namespace idfxx_crypto_test;

namespace {

// n bytes of key material: 1, 2, 3, ... by default
std::vector<uint8_t> pattern(size_t n, uint8_t first = 1) {
    return patterned_message(n, first);
}

bool key_exists(psa_key_id_t id) {
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_status_t status = psa_get_key_attributes(id, &attributes);
    psa_reset_key_attributes(&attributes);
    return status == PSA_SUCCESS;
}

// The algorithm AES keys are created for: AES-CMAC where it is enabled.
#ifdef PSA_WANT_ALG_CMAC
constexpr crypto::key_algorithm aes_algorithm = crypto::mac::algorithm::cmac;
#else
constexpr crypto::key_algorithm aes_algorithm{};
#endif

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

#ifndef PSA_WANT_KEY_TYPE_RAW_DATA
#error "test app must enable raw data keys"
#endif
#ifndef PSA_WANT_KEY_TYPE_HMAC
#error "test app must enable hmac keys"
#endif
#ifndef PSA_WANT_KEY_TYPE_DERIVE
#error "test app must enable derive keys"
#endif
#ifndef PSA_WANT_KEY_TYPE_AES
#error "test app must enable aes keys"
#endif
// The runtime tests create HMAC keys for HMAC-SHA-256.
#if !defined(PSA_WANT_ALG_HMAC) || !defined(PSA_WANT_ALG_SHA_256)
#error "test app must enable hmac_sha256"
#endif

static_assert(std::to_underlying(crypto::key_type::raw_data) == PSA_KEY_TYPE_RAW_DATA);
static_assert(std::to_underlying(crypto::key_type::hmac) == PSA_KEY_TYPE_HMAC);
static_assert(std::to_underlying(crypto::key_type::derive) == PSA_KEY_TYPE_DERIVE);
static_assert(std::to_underlying(crypto::key_type::aes) == PSA_KEY_TYPE_AES);

static_assert(std::to_underlying(crypto::key_usage::none) == 0);
static_assert(std::to_underlying(crypto::key_usage::export_key) == PSA_KEY_USAGE_EXPORT);
static_assert(std::to_underlying(crypto::key_usage::copy) == PSA_KEY_USAGE_COPY);
static_assert(std::to_underlying(crypto::key_usage::encrypt) == PSA_KEY_USAGE_ENCRYPT);
static_assert(std::to_underlying(crypto::key_usage::decrypt) == PSA_KEY_USAGE_DECRYPT);
static_assert(std::to_underlying(crypto::key_usage::sign_message) == PSA_KEY_USAGE_SIGN_MESSAGE);
static_assert(
    std::to_underlying(crypto::key_usage::verify_message) == PSA_KEY_USAGE_VERIFY_MESSAGE
);
static_assert(std::to_underlying(crypto::key_usage::sign_hash) == PSA_KEY_USAGE_SIGN_HASH);
static_assert(std::to_underlying(crypto::key_usage::verify_hash) == PSA_KEY_USAGE_VERIFY_HASH);
static_assert(std::to_underlying(crypto::key_usage::derive) == PSA_KEY_USAGE_DERIVE);
static_assert(
    std::to_underlying(crypto::key_usage::verify_derivation) == PSA_KEY_USAGE_VERIFY_DERIVATION
);

// Usages combine into flags
static_assert(
    to_underlying(sign_verify) == (PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE)
);
static_assert(sign_verify.contains(crypto::key_usage::sign_message));
static_assert(!sign_verify.contains(crypto::key_usage::export_key));

// key_algorithm: none by default, implicitly constructed from an algorithm enumerator
static_assert(crypto::key_algorithm{}.idf_algorithm() == PSA_ALG_NONE);
static_assert(crypto::key_algorithm{} == crypto::key_algorithm{});
static_assert(
    crypto::key_algorithm{crypto::mac::algorithm::hmac_sha256}.idf_algorithm() ==
    PSA_ALG_HMAC(PSA_ALG_SHA_256)
);
static_assert(std::is_convertible_v<crypto::mac::algorithm, crypto::key_algorithm>);
static_assert(
    crypto::key_algorithm{crypto::mac::algorithm::hmac_sha256} != crypto::key_algorithm{}
);
static_assert(std::is_nothrow_copy_constructible_v<crypto::key_algorithm>);

// key_algorithm for a MAC with truncated tags: constructed explicitly from the MAC algorithm and
// a constant tag length (an invalid one is a compile error), the PSA truncated algorithm below the
// full length and the plain one at it
namespace {
template<typename T>
concept copy_list_initializable_from_mac_and_length =
    requires(void (*f)(T)) { f({crypto::mac::algorithm{}, std::size_t{}}); };
} // namespace
static_assert(
    std::is_nothrow_constructible_v<crypto::key_algorithm, crypto::mac::algorithm, std::size_t>
);
static_assert(!copy_list_initializable_from_mac_and_length<crypto::key_algorithm>);
constexpr auto hmac_sha256 = crypto::mac::algorithm::hmac_sha256;
static_assert(crypto::key_algorithm{hmac_sha256, 16}.idf_algorithm() ==
              PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), 16));
static_assert(crypto::key_algorithm{hmac_sha256, 4}.idf_algorithm() ==
              PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), 4));
static_assert(crypto::key_algorithm{hmac_sha256, 31}.idf_algorithm() ==
              PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), 31));
static_assert(crypto::key_algorithm{hmac_sha256, 32} == crypto::key_algorithm{hmac_sha256});
static_assert(crypto::key_algorithm{hmac_sha256, 32}.idf_algorithm() ==
              PSA_ALG_HMAC(PSA_ALG_SHA_256));
static_assert(crypto::key_algorithm{hmac_sha256, 16} != crypto::key_algorithm{hmac_sha256});
static_assert(crypto::key_algorithm{hmac_sha256, 16} != crypto::key_algorithm{hmac_sha256, 17});
// A constant tag length works in constant expressions too
constexpr crypto::key_algorithm hmac_sha256_128{hmac_sha256, 16};
static_assert(hmac_sha256_128.idf_algorithm() ==
              PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), 16));
// A tag length known only at runtime goes through truncated() / try_truncated()
static_assert(std::is_same_v<
              decltype(crypto::key_algorithm::try_truncated(hmac_sha256, std::size_t{})),
              result<crypto::key_algorithm>>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_same_v<
              decltype(crypto::key_algorithm::truncated(hmac_sha256, std::size_t{})),
              crypto::key_algorithm>);
#endif
#ifdef PSA_WANT_ALG_SHA_512
static_assert(crypto::key_algorithm{crypto::mac::algorithm::hmac_sha512, 63}.idf_algorithm() ==
              PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_512), 63));
static_assert(crypto::key_algorithm{crypto::mac::algorithm::hmac_sha512, 64} ==
              crypto::key_algorithm{crypto::mac::algorithm::hmac_sha512});
#endif
#ifdef PSA_WANT_ALG_CMAC
static_assert(crypto::key_algorithm{crypto::mac::algorithm::cmac, 4}.idf_algorithm() ==
              PSA_ALG_TRUNCATED_MAC(PSA_ALG_CMAC, 4));
static_assert(crypto::key_algorithm{crypto::mac::algorithm::cmac, 16} ==
              crypto::key_algorithm{crypto::mac::algorithm::cmac});
#endif

// key: move-only, not default constructible
static_assert(!std::is_default_constructible_v<crypto::key>);
static_assert(!std::is_copy_constructible_v<crypto::key>);
static_assert(!std::is_copy_assignable_v<crypto::key>);
static_assert(std::is_nothrow_move_constructible_v<crypto::key>);
static_assert(std::is_nothrow_move_assignable_v<crypto::key>);
static_assert(std::is_nothrow_destructible_v<crypto::key>);

// config: the type is required, and the algorithm defaults to none
static_assert(!std::is_default_constructible_v<crypto::key::config>);
static_assert(crypto::key::config{.type = crypto::key_type::hmac, .usage = {}}.algorithm ==
              crypto::key_algorithm{});

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("crypto key import reports its attributes", "[idfxx][crypto]") {
    {
        auto material = pattern(20);
        auto k = ok(crypto::key::try_import(
            {.type = crypto::key_type::hmac,
             .usage = sign_verify,
             .algorithm = crypto::mac::algorithm::hmac_sha256},
            material
        ));
        TEST_ASSERT_EQUAL(std::to_underlying(crypto::key_type::hmac), std::to_underlying(k.type()));
        TEST_ASSERT_EQUAL_INT32(160, static_cast<int32_t>(k.bits()));
        TEST_ASSERT_TRUE(k.usage() == sign_verify);
        TEST_ASSERT_TRUE(k.algorithm() == crypto::mac::algorithm::hmac_sha256);
        TEST_ASSERT_NOT_EQUAL(PSA_KEY_ID_NULL, k.idf_handle());
    }
    {
        auto material = pattern(1);
        auto k = ok(crypto::key::try_import(
            {.type = crypto::key_type::hmac, .usage = crypto::key_usage::sign_message}, material
        ));
        TEST_ASSERT_EQUAL_INT32(8, static_cast<int32_t>(k.bits()));
        TEST_ASSERT_TRUE(k.usage() == crypto::key_usage::sign_message);
        TEST_ASSERT_TRUE(k.algorithm() == crypto::key_algorithm{});
    }
    for (size_t len : {16, 24, 32}) {
        auto material = pattern(len);
        auto k = ok(crypto::key::try_import(
            {.type = crypto::key_type::aes,
             .usage = sign_verify,
             .algorithm = aes_algorithm},
            material
        ));
        TEST_ASSERT_EQUAL(std::to_underlying(crypto::key_type::aes), std::to_underlying(k.type()));
        TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(len * 8), static_cast<int32_t>(k.bits()));
        TEST_ASSERT_TRUE(k.algorithm() == aes_algorithm);
    }
    {
        auto material = pattern(5);
        auto k = ok(crypto::key::try_import(
            {.type = crypto::key_type::raw_data, .usage = crypto::key_usage::export_key}, material
        ));
        TEST_ASSERT_EQUAL(
            std::to_underlying(crypto::key_type::raw_data), std::to_underlying(k.type())
        );
        TEST_ASSERT_EQUAL_INT32(40, static_cast<int32_t>(k.bits()));
        TEST_ASSERT_TRUE(k.usage() == crypto::key_usage::export_key);
    }
    {
        auto material = pattern(32);
        auto k = ok(crypto::key::try_import(
            {.type = crypto::key_type::derive, .usage = crypto::key_usage::derive}, material
        ));
        TEST_ASSERT_EQUAL(
            std::to_underlying(crypto::key_type::derive), std::to_underlying(k.type())
        );
        TEST_ASSERT_EQUAL_INT32(256, static_cast<int32_t>(k.bits()));
    }
}

TEST_CASE("crypto key usage includes implied usages", "[idfxx][crypto]") {
    auto material = pattern(32);
    auto k = ok(crypto::key::try_import(
        {.type = crypto::key_type::hmac,
         .usage = crypto::key_usage::sign_hash | crypto::key_usage::verify_hash,
         .algorithm = crypto::mac::algorithm::hmac_sha256},
        material
    ));
    TEST_ASSERT_TRUE(k.usage().contains(crypto::key_usage::sign_hash));
    TEST_ASSERT_TRUE(k.usage().contains(crypto::key_usage::verify_hash));
    TEST_ASSERT_TRUE(k.usage().contains(crypto::key_usage::sign_message));
    TEST_ASSERT_TRUE(k.usage().contains(crypto::key_usage::verify_message));
}

TEST_CASE("crypto key import rejects invalid material", "[idfxx][crypto]") {
    for (auto type : {crypto::key_type::hmac, crypto::key_type::aes, crypto::key_type::raw_data}) {
        fails_with(
            crypto::errc::invalid_argument,
            crypto::key::try_import({.type = type, .usage = sign_verify}, idfxx::byte_view{})
        );
        fails_with(
            crypto::errc::invalid_argument,
            crypto::key::try_import(
                {.type = type, .usage = sign_verify}, std::span<const uint8_t>{}
            )
        );
    }
    // AES keys are 16, 24 or 32 bytes
    for (size_t len : {1, 15, 17, 31, 33, 64}) {
        auto material = pattern(len);
        fails_with(
            crypto::errc::invalid_argument,
            crypto::key::try_import({.type = crypto::key_type::aes, .usage = sign_verify}, material)
        );
    }
}

TEST_CASE("crypto key generate reports its attributes", "[idfxx][crypto]") {
    {
        auto k = ok(crypto::key::try_generate(
            {.type = crypto::key_type::hmac,
             .usage = sign_verify,
             .algorithm = crypto::mac::algorithm::hmac_sha256},
            256
        ));
        TEST_ASSERT_EQUAL(std::to_underlying(crypto::key_type::hmac), std::to_underlying(k.type()));
        TEST_ASSERT_EQUAL_INT32(256, static_cast<int32_t>(k.bits()));
        TEST_ASSERT_TRUE(k.usage() == sign_verify);
        TEST_ASSERT_TRUE(k.algorithm() == crypto::mac::algorithm::hmac_sha256);
    }
    for (size_t bits : {128, 192, 256}) {
        auto k = ok(crypto::key::try_generate(
            {.type = crypto::key_type::aes,
             .usage = sign_verify,
             .algorithm = aes_algorithm},
            bits
        ));
        TEST_ASSERT_EQUAL(std::to_underlying(crypto::key_type::aes), std::to_underlying(k.type()));
        TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(bits), static_cast<int32_t>(k.bits()));
    }
    // AES keys are 128, 192 or 256 bits
    TEST_ASSERT_FALSE(crypto::key::try_generate({.type = crypto::key_type::aes, .usage = {}}, 100)
                          .has_value());
}

TEST_CASE("crypto key algorithm with truncated mac tags", "[idfxx][crypto]") {
    auto material = pattern(32);
    // Tag lengths from 4 to the full tag length, known only at runtime: the same algorithms as the
    // constant forms, and the full length is the plain algorithm
    for (size_t n : {4, 16, 31, 32}) {
        auto alg = ok(crypto::key_algorithm::try_truncated(hmac_sha256, n));
        TEST_ASSERT_EQUAL_UINT32(
            n == 32 ? PSA_ALG_HMAC(PSA_ALG_SHA_256)
                    : PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), n),
            alg.idf_algorithm()
        );
        auto k = ok(crypto::key::try_import(
            {.type = crypto::key_type::hmac, .usage = sign_verify, .algorithm = alg}, material
        ));
        TEST_ASSERT_TRUE(k.algorithm() == alg);
        auto g = ok(crypto::key::try_generate(
            {.type = crypto::key_type::hmac, .usage = sign_verify, .algorithm = alg}, 256
        ));
        TEST_ASSERT_TRUE(g.algorithm() == alg);
    }
    TEST_ASSERT_TRUE(
        ok(crypto::key_algorithm::try_truncated(hmac_sha256, 16)) ==
        crypto::key_algorithm(hmac_sha256, 16)
    );
    TEST_ASSERT_TRUE(
        ok(crypto::key_algorithm::try_truncated(hmac_sha256, 32)) ==
        crypto::key_algorithm{hmac_sha256}
    );
    // Too short or too long
    for (size_t n : {0, 1, 3, 33, 63, 64, 80, 1000}) {
        fails_with(
            crypto::errc::invalid_argument, crypto::key_algorithm::try_truncated(hmac_sha256, n)
        );
    }
    // Not a MAC algorithm
    fails_with(
        crypto::errc::invalid_argument,
        crypto::key_algorithm::try_truncated(static_cast<crypto::mac::algorithm>(PSA_ALG_NONE), 4)
    );
#ifdef PSA_WANT_ALG_CMAC
    auto aes_material = pattern(16);
    for (size_t n : {4, 15, 16}) {
        auto alg = ok(crypto::key_algorithm::try_truncated(crypto::mac::algorithm::cmac, n));
        auto k = ok(crypto::key::try_import(
            {.type = crypto::key_type::aes, .usage = sign_verify, .algorithm = alg}, aes_material
        ));
        TEST_ASSERT_TRUE(k.algorithm() == alg);
    }
    TEST_ASSERT_TRUE(
        ok(crypto::key_algorithm::try_truncated(crypto::mac::algorithm::cmac, 16)) ==
        crypto::mac::algorithm::cmac
    );
    for (size_t n : {0, 3, 17, 32}) {
        fails_with(
            crypto::errc::invalid_argument,
            crypto::key_algorithm::try_truncated(crypto::mac::algorithm::cmac, n)
        );
    }
#endif
}

TEST_CASE("crypto key creation refuses a mac algorithm its type doesn't take", "[idfxx][crypto]") {
    auto material = pattern(16);
    // Every way of creating a key with this type and algorithm is refused
    auto refused = [&](crypto::key_type type, crypto::key_algorithm alg) {
        const crypto::key::config cfg{.type = type, .usage = sign_verify, .algorithm = alg};
        fails_with(crypto::errc::invalid_argument, crypto::key::try_import(cfg, material));
        fails_with(crypto::errc::invalid_argument, crypto::key::try_generate(cfg, 128));
    };

    // HMAC takes HMAC keys
    for (crypto::key_algorithm alg :
         {crypto::key_algorithm{hmac_sha256}, crypto::key_algorithm{hmac_sha256, 16}}) {
        refused(crypto::key_type::aes, alg);
        refused(crypto::key_type::raw_data, alg);
        refused(crypto::key_type::derive, alg);
        ok(crypto::key::try_import(
            {.type = crypto::key_type::hmac, .usage = sign_verify, .algorithm = alg}, material
        ));
    }

#ifdef PSA_WANT_ALG_CMAC
    // AES-CMAC takes AES keys
    for (crypto::key_algorithm alg :
         {crypto::key_algorithm{crypto::mac::algorithm::cmac},
          crypto::key_algorithm{crypto::mac::algorithm::cmac, 4}}) {
        refused(crypto::key_type::hmac, alg);
        refused(crypto::key_type::raw_data, alg);
        refused(crypto::key_type::derive, alg);
        ok(crypto::key::try_import(
            {.type = crypto::key_type::aes, .usage = sign_verify, .algorithm = alg}, material
        ));
    }
#endif

    // A key permitted no algorithm can have any type
    for (auto type : {crypto::key_type::hmac, crypto::key_type::aes, crypto::key_type::raw_data}) {
        auto k = ok(crypto::key::try_import(
            {.type = type, .usage = crypto::key_usage::export_key}, material
        ));
        TEST_ASSERT_TRUE(k.algorithm() == crypto::key_algorithm{});
    }
}

TEST_CASE("crypto generated keys differ", "[idfxx][crypto]") {
    auto a = ok(crypto::key::try_generate(
        {.type = crypto::key_type::hmac, .usage = crypto::key_usage::export_key}, 256
    ));
    auto b = ok(crypto::key::try_generate(
        {.type = crypto::key_type::hmac, .usage = crypto::key_usage::export_key}, 256
    ));
    std::array<std::byte, 32> ma;
    std::array<std::byte, 32> mb;
    TEST_ASSERT_EQUAL_INT32(32, static_cast<int32_t>(ok(a.try_export_to(ma)).size()));
    TEST_ASSERT_EQUAL_INT32(32, static_cast<int32_t>(ok(b.try_export_to(mb)).size()));
    TEST_ASSERT_TRUE(ma != mb);
    TEST_ASSERT_TRUE((ma != std::array<std::byte, 32>{}));
}

TEST_CASE("crypto key export", "[idfxx][crypto]") {
    auto material = pattern(20, 0xa0);

    // Exportable: the material round-trips, into an exact or a larger buffer
    {
        auto k = ok(crypto::key::try_import(
            {.type = crypto::key_type::hmac, .usage = crypto::key_usage::export_key}, material
        ));
        std::array<uint8_t, 20> exact{};
        auto exported = ok(k.try_export_to(exact));
        TEST_ASSERT_EQUAL_PTR(exact.data(), exported.data());
        TEST_ASSERT_EQUAL_INT32(20, static_cast<int32_t>(exported.size()));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(material.data(), exact.data(), 20);

        // The filled prefix of a larger buffer
        std::vector<std::byte> larger(64);
        exported = ok(k.try_export_to(larger));
        TEST_ASSERT_EQUAL_PTR(larger.data(), exported.data());
        TEST_ASSERT_EQUAL_INT32(20, static_cast<int32_t>(exported.size()));
        TEST_ASSERT_EQUAL_MEMORY(material.data(), exported.data(), 20);

        uint8_t raw[32];
        exported = ok(k.try_export_to({raw, sizeof(raw)}));
        TEST_ASSERT_EQUAL_PTR(raw, exported.data());
        TEST_ASSERT_EQUAL_INT32(20, static_cast<int32_t>(exported.size()));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(material.data(), raw, 20);

        // Too small
        std::array<uint8_t, 19> small{};
        fails_with(crypto::errc::buffer_too_small, k.try_export_to(small));
        fails_with(crypto::errc::buffer_too_small, k.try_export_to(idfxx::byte_span{}));
    }

    // Not exportable
    {
        auto k = ok(crypto::key::try_import(
            {.type = crypto::key_type::hmac, .usage = sign_verify}, material
        ));
        std::array<uint8_t, 20> out{};
        fails_with(crypto::errc::not_permitted, k.try_export_to(out));
    }
}

TEST_CASE("crypto key copy", "[idfxx][crypto]") {
    auto material = pattern(32, 0x40);
    std::string_view msg = "message";
    auto source = ok(crypto::key::try_import(
        {.type = crypto::key_type::hmac,
         .usage = sign_verify | crypto::key_usage::copy | crypto::key_usage::export_key,
         .algorithm = crypto::key_algorithm{hmac_sha256, 16}},
        material
    ));
    using short_hmac = crypto::mac::authenticator<hmac_sha256, 16>;
    auto tag = ok(short_hmac::try_compute(source, msg));

    // The copy keeps the type, size and algorithm, and permits only what was asked for
    auto checker = ok(source.try_copy(crypto::key_usage::verify_message));
    TEST_ASSERT_EQUAL(
        std::to_underlying(crypto::key_type::hmac), std::to_underlying(checker.type())
    );
    TEST_ASSERT_EQUAL_INT32(256, static_cast<int32_t>(checker.bits()));
    TEST_ASSERT_TRUE(checker.algorithm() == source.algorithm());
    TEST_ASSERT_TRUE(checker.usage() == crypto::key_usage::verify_message);
    TEST_ASSERT_TRUE(checker.idf_handle() != source.idf_handle());
    TEST_ASSERT_TRUE(ok(short_hmac::try_compare(checker, msg, tag)));
    fails_with(crypto::errc::not_permitted, short_hmac::try_compute(checker, msg));

    // Usages this key doesn't permit are dropped
    auto narrowed =
        ok(source.try_copy(crypto::key_usage::sign_message | crypto::key_usage::derive));
    TEST_ASSERT_TRUE(narrowed.usage() == crypto::key_usage::sign_message);

    // The copy has its own key material: it outlives the source
    auto exportable =
        ok(source.try_copy(crypto::key_usage::export_key | crypto::key_usage::verify_message));
    source = ok(
        crypto::key::try_generate({.type = crypto::key_type::hmac, .usage = sign_verify}, 128)
    );
    std::array<uint8_t, 32> out{};
    TEST_ASSERT_EQUAL_INT32(32, static_cast<int32_t>(ok(exportable.try_export_to(out)).size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(material.data(), out.data(), 32);
    TEST_ASSERT_TRUE(ok(short_hmac::try_compare(checker, msg, tag)));

    // A key that doesn't permit copying, including a copy made without it, can't be copied
    fails_with(crypto::errc::not_permitted, source.try_copy(crypto::key_usage::sign_message));
    fails_with(crypto::errc::not_permitted, checker.try_copy(crypto::key_usage::verify_message));
}

TEST_CASE("crypto key destroy and move", "[idfxx][crypto]") {
    auto material = pattern(16);
    const crypto::key::config cfg{
        .type = crypto::key_type::aes, .usage = crypto::key_usage::export_key
    };

    // Destruction destroys the key
    psa_key_id_t id;
    {
        auto k = ok(crypto::key::try_import(cfg, material));
        id = k.idf_handle();
        TEST_ASSERT_TRUE(key_exists(id));
    }
    TEST_ASSERT_FALSE(key_exists(id));

    // Many keys created and destroyed in turn
    for (int i = 0; i < 64; ++i) {
        auto k = ok(crypto::key::try_import(cfg, material));
    }

    // Move construction transfers the key; the source can then be destroyed
    {
        auto a = ok(crypto::key::try_import(cfg, material));
        id = a.idf_handle();
        crypto::key b{std::move(a)};
        TEST_ASSERT_EQUAL(id, b.idf_handle());
        TEST_ASSERT_EQUAL_INT32(128, static_cast<int32_t>(b.bits()));
        TEST_ASSERT_TRUE(b.usage() == crypto::key_usage::export_key);
        std::array<uint8_t, 16> out{};
        TEST_ASSERT_EQUAL_INT32(16, static_cast<int32_t>(ok(b.try_export_to(out)).size()));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(material.data(), out.data(), 16);
    }
    TEST_ASSERT_FALSE(key_exists(id));

    // Move assignment destroys the target's key and takes over the source's
    {
        auto other = pattern(32, 0x40);
        auto x = ok(crypto::key::try_import(cfg, material));
        auto y = ok(crypto::key::try_import(cfg, other));
        psa_key_id_t x_id = x.idf_handle();
        psa_key_id_t y_id = y.idf_handle();
        x = std::move(y);
        TEST_ASSERT_FALSE(key_exists(x_id));
        TEST_ASSERT_EQUAL(y_id, x.idf_handle());
        TEST_ASSERT_EQUAL_INT32(256, static_cast<int32_t>(x.bits()));
        std::array<uint8_t, 32> out{};
        TEST_ASSERT_EQUAL_INT32(32, static_cast<int32_t>(ok(x.try_export_to(out)).size()));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(other.data(), out.data(), 32);
        id = y_id;
    }
    TEST_ASSERT_FALSE(key_exists(id));

    // A key returned in a result can be moved out of it
    {
        auto r = crypto::key::try_import(cfg, material);
        TEST_ASSERT_TRUE(r.has_value());
        crypto::key k = std::move(*r);
        TEST_ASSERT_TRUE(key_exists(k.idf_handle()));
    }
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto key exception-based API", "[idfxx][crypto]") {
    auto material = pattern(24);
    auto k = crypto::key::import(
        {.type = crypto::key_type::aes,
         .usage = crypto::key_usage::export_key | crypto::key_usage::sign_message,
         .algorithm = aes_algorithm},
        material
    );
    TEST_ASSERT_EQUAL_INT32(192, static_cast<int32_t>(k.bits()));
    std::array<uint8_t, 24> out{};
    auto exported = k.export_to(out);
    TEST_ASSERT_EQUAL_PTR(out.data(), exported.data());
    TEST_ASSERT_EQUAL_INT32(24, static_cast<int32_t>(exported.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(material.data(), out.data(), 24);

    auto g = crypto::key::generate({.type = crypto::key_type::hmac, .usage = sign_verify}, 160);
    TEST_ASSERT_EQUAL_INT32(160, static_cast<int32_t>(g.bits()));

    bool threw = false;
    try {
        (void)crypto::key::import({.type = crypto::key_type::hmac, .usage = {}}, byte_view{});
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::invalid_argument);
    }
    TEST_ASSERT_TRUE(threw);

    threw = false;
    try {
        (void)crypto::key::generate(
            {.type = crypto::key_type::aes,
             .usage = sign_verify,
             .algorithm = crypto::mac::algorithm::hmac_sha256},
            128
        );
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::invalid_argument);
    }
    TEST_ASSERT_TRUE(threw);

    TEST_ASSERT_TRUE(
        crypto::key_algorithm::truncated(crypto::mac::algorithm::hmac_sha256, 16) ==
        crypto::key_algorithm(crypto::mac::algorithm::hmac_sha256, 16)
    );
    threw = false;
    try {
        (void)crypto::key_algorithm::truncated(crypto::mac::algorithm::hmac_sha256, 3);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::invalid_argument);
    }
    TEST_ASSERT_TRUE(threw);

    auto copyable = crypto::key::generate(
        {.type = crypto::key_type::hmac, .usage = sign_verify | crypto::key_usage::copy}, 256
    );
    auto copied = copyable.copy(crypto::key_usage::verify_message);
    TEST_ASSERT_TRUE(copied.usage() == crypto::key_usage::verify_message);

    threw = false;
    try {
        (void)g.copy(sign_verify);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::not_permitted);
    }
    TEST_ASSERT_TRUE(threw);

    threw = false;
    try {
        (void)g.export_to(out);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::not_permitted);
        TEST_ASSERT_TRUE(e.code() == std::errc::operation_not_permitted);
    }
    TEST_ASSERT_TRUE(threw);
}
#endif
