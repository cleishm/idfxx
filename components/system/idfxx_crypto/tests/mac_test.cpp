// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx crypto message authentication codes
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "crypto_test_utils.hpp"
#include "idfxx/crypto/mac"
#include "idfxx/hex"
#include "unity.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace idfxx;
using namespace idfxx_crypto_test;

namespace {

// A known-answer test: the tag (lowercase hex) of a message under a key.
struct kat {
    std::vector<uint8_t> key;
    std::vector<uint8_t> data;
    std::string tag;
};

std::vector<uint8_t> repeat(uint8_t b, size_t n) {
    return std::vector<uint8_t>(n, b);
}

std::vector<uint8_t> text(std::string_view s) {
    return {s.begin(), s.end()};
}

std::string_view as_text(const std::vector<uint8_t>& v) {
    return {reinterpret_cast<const char*>(v.data()), v.size()};
}

std::string to_hex(std::span<const std::byte> data) {
    return idfxx::to_string(idfxx::hex(data));
}

// Checks that an authenticator can't be created with a key: make() fails with the expected error
// and, with exceptions enabled, the constructor throws it.
template<typename M>
void rejects_key(std::error_code expected, const crypto::key& k) {
    auto r = M::make(k);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL_MESSAGE(expected.value(), r.error().value(), r.error().message().c_str());
    TEST_ASSERT_EQUAL_PTR(&expected.category(), &r.error().category());
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    bool threw = false;
    try {
        M m{k};
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == expected);
    }
    TEST_ASSERT_TRUE(threw);
#endif
}

// The inputs of RFC 4231 test cases 1-4, 6 and 7 (case 5 checks a truncated tag), with the
// expected tags.
std::vector<kat> rfc4231(const char* const (&tags)[6]) {
    std::vector<uint8_t> key4;
    for (uint8_t b = 0x01; b <= 0x19; ++b) {
        key4.push_back(b);
    }
    return {
        {repeat(0x0b, 20), text("Hi There"), tags[0]},
        {text("Jefe"), text("what do ya want for nothing?"), tags[1]},
        {repeat(0xaa, 20), repeat(0xdd, 50), tags[2]},
        {key4, repeat(0xcd, 50), tags[3]},
        {repeat(0xaa, 131),
         text("Test Using Larger Than Block-Size Key - Hash Key First"),
         tags[4]},
        {repeat(0xaa, 131),
         text("This is a test using a larger than block-size key and a larger than block-size "
              "data. The key needs to be hashed before being used by the HMAC algorithm."),
         tags[5]},
    };
}

// The inputs of RFC 2202 test cases 1-4, 6 and 7, with the expected tags. Cases 1 and 3 use a key
// of the hash's digest size.
std::vector<kat> rfc2202(size_t key_size, const char* const (&tags)[6]) {
    std::vector<uint8_t> key4;
    for (uint8_t b = 0x01; b <= 0x19; ++b) {
        key4.push_back(b);
    }
    return {
        {repeat(0x0b, key_size), text("Hi There"), tags[0]},
        {text("Jefe"), text("what do ya want for nothing?"), tags[1]},
        {repeat(0xaa, key_size), repeat(0xdd, 50), tags[2]},
        {key4, repeat(0xcd, 50), tags[3]},
        {repeat(0xaa, 80),
         text("Test Using Larger Than Block-Size Key - Hash Key First"),
         tags[4]},
        {repeat(0xaa, 80),
         text("Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data"),
         tags[5]},
    };
}

#if defined(PSA_WANT_ALG_CMAC) && defined(PSA_WANT_KEY_TYPE_AES)
// The AES-CMAC examples of RFC 4493 (AES-128) and NIST SP 800-38B (AES-192, AES-256): the first 0,
// 16, 40 and 64 bytes of one message under a key, with the expected tags.
std::vector<kat> cmac_examples(std::string_view key_hex, const char* const (&tags)[4]) {
    auto msg = from_hex(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710"
    );
    auto key = from_hex(key_hex);
    return {
        {key, {}, tags[0]},
        {key, {msg.begin(), msg.begin() + 16}, tags[1]},
        {key, {msg.begin(), msg.begin() + 40}, tags[2]},
        {key, msg, tags[3]},
    };
}
#endif

// Known-answer tests with each tag truncated to its leftmost `tag_size` bytes.
std::vector<kat> truncated(std::vector<kat> kats, size_t tag_size) {
    for (auto& v : kats) {
        v.tag.resize(tag_size * 2);
    }
    return kats;
}

// The algorithm keys for an authenticator are permitted: its MAC algorithm, truncated to its tag
// size.
template<typename M>
constexpr crypto::key_algorithm key_algorithm_of = crypto::key_algorithm{M::algorithm, M::tag_size};

// The longest message of a set of known-answer tests.
const kat& longest(const std::vector<kat>& kats) {
    return *std::max_element(kats.begin(), kats.end(), [](const kat& a, const kat& b) {
        return a.data.size() < b.data.size();
    });
}

std::vector<std::byte> copy_of(std::span<const std::byte> data) {
    return {data.begin(), data.end()};
}

// What mac::operation takes from a key: the algorithm, the key type and the tag length. The helpers
// that test mac::operation take these at runtime, so that one copy serves every algorithm and tag
// length. The helpers that test an authenticator's own API are templates: the full set runs only on
// a few representative authenticators, and the others are checked against their known answers.
struct mac_params {
    crypto::mac::algorithm algorithm;
    crypto::key_type key_type;
    size_t tag_size;
};

template<typename M>
constexpr mac_params params_of{
    M::algorithm, crypto::detail::mac_key_type(M::algorithm), M::tag_size
};

// A key for p's algorithm and tag length, permitted the given usage.
crypto::key import_key(
    const mac_params& p, const std::vector<uint8_t>& material,
    flags<crypto::key_usage> usage = sign_verify
) {
    return ok(crypto::key::try_import(
        {.type = p.key_type,
         .usage = usage,
         .algorithm = ok(crypto::key_algorithm::try_truncated(p.algorithm, p.tag_size))},
        material
    ));
}

// Each known answer through the authenticator's own API: compute, compare, and verify and finish
// incrementally. For the authenticators that don't run the full set of helpers below.
template<typename M>
void check_tags(const std::vector<kat>& kats) {
    for (const auto& v : kats) {
        auto k = ok(M::try_import_key(v.key));
        auto good = from_hex(v.tag);
        auto flipped = good;
        flipped.back() ^= 0x01;
        TEST_ASSERT_TAG(v.tag, ok(M::try_compute(k, v.data)));
        TEST_ASSERT_TRUE(ok(M::try_compare(k, v.data, good)));
        TEST_ASSERT_FALSE(ok(M::try_compare(k, v.data, flipped)));
        auto m = ok(M::make(k));
        ok(m.try_update(v.data));
        TEST_ASSERT_TRUE(ok(m.try_verify(good)));
        ok(m.try_update(v.data));
        TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));
    }
}

template<typename M>
void check_known_answers(const std::vector<kat>& kats) {
    for (const auto& v : kats) {
        auto k = ok(M::try_import_key(v.key));
        auto empty_tag = to_hex(ok(M::try_compute(k, std::string_view{})));
        auto good = from_hex(v.tag);

        // One-shot, each overload and byte_view source
        TEST_ASSERT_TAG(v.tag, ok(M::try_compute(k, v.data)));
        TEST_ASSERT_TAG(v.tag, ok(M::try_compute(k, bytes_of(v.data))));
        TEST_ASSERT_TAG(v.tag, ok(M::try_compute(k, {v.data.data(), v.data.size()})));
        TEST_ASSERT_TAG(v.tag, ok(M::try_compute(k, as_text(v.data))));
        TEST_ASSERT_TRUE(ok(M::try_compare(k, v.data, good)));
        TEST_ASSERT_TRUE(ok(M::try_compare(k, as_text(v.data), bytes_from_hex(v.tag))));

        // Incremental, one update per message, each overload
        auto m = ok(M::make(k));
        ok(m.try_update(v.data));
        TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));
        ok(m.try_update(as_text(v.data)));
        TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));
        ok(m.try_update(bytes_of(v.data)));
        TEST_ASSERT_TRUE(ok(m.try_verify(good)));

        // Reuse after finish and verify: finishing again authenticates the empty message
        TEST_ASSERT_TAG(empty_tag, ok(m.try_finish()));
        TEST_ASSERT_TRUE(ok(m.try_verify(from_hex(empty_tag))));
    }

    // A tag can itself be authenticated (tag_type is a span of std::byte)
    auto k = ok(M::try_import_key(kats[0].key));
    auto t = ok(M::try_compute(k, kats[0].data));
    auto tt = ok(M::try_compute(k, t));
    TEST_ASSERT_TAG(to_hex(tt), ok(M::try_compute(k, bytes_from_hex(kats[0].tag))));
}

template<typename M>
void check_split_updates(const std::vector<kat>& kats) {
    const kat& v = longest(kats);
    std::span<const uint8_t> all{v.data};
    auto good = from_hex(v.tag);
    auto k = ok(M::try_import_key(v.key));
    auto m = ok(M::make(k));

    // One byte at a time
    for (size_t i = 0; i < all.size(); ++i) {
        ok(m.try_update(all.subspan(i, 1)));
    }
    TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));

    // Chunks of varying size, including empty updates (and null spans)
    const size_t sizes[] = {0, 1, 7, 0, 64, 3, 128, 0};
    size_t pos = 0;
    for (size_t i = 0; pos < all.size(); ++i) {
        size_t n = std::min(sizes[i % std::size(sizes)], all.size() - pos);
        ok(m.try_update(std::as_bytes(all.subspan(pos, n))));
        pos += n;
    }
    ok(m.try_update(std::span<const uint8_t>{}));
    ok(m.try_update(std::span<const std::byte>{}));
    ok(m.try_update(std::string_view{}));
    TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));

    // Split in two at a range of positions, as bytes and as text
    for (size_t split : {size_t{0}, size_t{1}, size_t{15}, size_t{16}, size_t{17}, all.size() - 1,
                         all.size()}) {
        ok(m.try_update(all.first(split)));
        ok(m.try_update(all.subspan(split)));
        TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));
        ok(m.try_update(as_text(v.data).substr(0, split)));
        ok(m.try_update(as_text(v.data).substr(split)));
        TEST_ASSERT_TRUE(ok(m.try_verify(good)));
    }
}

// The longest message fed to mac::operation in pieces: one byte at a time, in chunks of varying
// size and split in two at a range of positions.
void check_split_operation(const mac_params& p, const std::vector<kat>& kats) {
    const kat& v = longest(kats);
    std::span<const uint8_t> all{v.data};
    std::array<std::byte, 80> buf; // larger than any tag
    auto k = import_key(p, v.key);
    auto op = ok(crypto::mac::operation::make(k));

    // One byte at a time
    for (size_t i = 0; i < all.size(); ++i) {
        ok(op.try_update(all.subspan(i, 1)));
    }
    TEST_ASSERT_TAG(v.tag, ok(op.try_finish(buf)));

    // Chunks of varying size, including empty updates (and null spans)
    const size_t sizes[] = {0, 1, 7, 0, 64, 3, 128, 0};
    size_t pos = 0;
    for (size_t i = 0; pos < all.size(); ++i) {
        size_t n = std::min(sizes[i % std::size(sizes)], all.size() - pos);
        ok(op.try_update(std::as_bytes(all.subspan(pos, n))));
        pos += n;
    }
    ok(op.try_update(std::span<const uint8_t>{}));
    ok(op.try_update(std::span<const std::byte>{}));
    ok(op.try_update(std::string_view{}));
    TEST_ASSERT_TAG(v.tag, ok(op.try_finish(buf)));

    // Split in two at a range of positions, as bytes and as text
    for (size_t split : {size_t{0}, size_t{1}, size_t{15}, size_t{16}, size_t{17}, all.size() - 1,
                         all.size()}) {
        ok(op.try_update(all.first(split)));
        ok(op.try_update(all.subspan(split)));
        TEST_ASSERT_TAG(v.tag, ok(op.try_finish(buf)));
        ok(op.try_update(as_text(v.data).substr(0, split)));
        ok(op.try_update(as_text(v.data).substr(split)));
        TEST_ASSERT_TRUE(ok(op.try_verify(from_hex(v.tag))));
    }
}

template<typename M>
void check_compare_and_verify(const std::vector<kat>& kats) {
    const kat& v = kats[1];
    auto k = ok(M::try_import_key(v.key));
    auto good = from_hex(v.tag);
    TEST_ASSERT_EQUAL(M::tag_size, good.size());
    auto good_bytes = bytes_from_hex(v.tag);
    auto flipped = good;
    flipped[flipped.size() / 2] ^= 0x01;
    auto flipped_bytes = good_bytes;
    flipped_bytes[0] ^= std::byte{0x80};
    auto too_short = std::vector<uint8_t>(good.begin(), good.end() - 1);
    auto too_long = good;
    too_long.push_back(0);
    auto other = text("another message");

    // Expected tags of each supported form resolve to an overload unambiguously
    typename M::tag_type tag = ok(M::try_compute(k, v.data));
    std::array<uint8_t, M::tag_size> u8_array;
    std::memcpy(u8_array.data(), good.data(), good.size());

    // One-shot
    TEST_ASSERT_TRUE(ok(M::try_compare(k, v.data, good)));
    TEST_ASSERT_TRUE(ok(M::try_compare(k, v.data, good_bytes)));
    TEST_ASSERT_TRUE(ok(M::try_compare(k, v.data, tag)));
    TEST_ASSERT_TRUE(ok(M::try_compare(k, v.data, u8_array)));
    TEST_ASSERT_TRUE(ok(M::try_compare(k, bytes_of(v.data), good)));
    TEST_ASSERT_TRUE(ok(M::try_compare(k, as_text(v.data), good)));

    TEST_ASSERT_FALSE(ok(M::try_compare(k, v.data, flipped)));
    TEST_ASSERT_FALSE(ok(M::try_compare(k, v.data, flipped_bytes)));
    TEST_ASSERT_FALSE(ok(M::try_compare(k, as_text(v.data), flipped)));
    TEST_ASSERT_FALSE(ok(M::try_compare(k, v.data, too_short)));
    TEST_ASSERT_FALSE(ok(M::try_compare(k, v.data, too_long)));
    TEST_ASSERT_FALSE(ok(M::try_compare(k, v.data, std::span<const uint8_t>{})));
    TEST_ASSERT_FALSE(ok(M::try_compare(k, other, good)));

    // Incremental; verify leaves the authenticator ready for a new message whatever the outcome
    auto empty_tag = to_hex(ok(M::try_compute(k, std::string_view{})));
    auto m = ok(M::make(k));
    TEST_ASSERT_TRUE(ok(m.try_verify(from_hex(empty_tag))));
    ok(m.try_update(v.data));
    TEST_ASSERT_TRUE(ok(m.try_verify(good)));
    TEST_ASSERT_TAG(empty_tag, ok(m.try_finish()));
    ok(m.try_update(v.data));
    TEST_ASSERT_TRUE(ok(m.try_verify(good_bytes)));
    ok(m.try_update(v.data));
    TEST_ASSERT_TRUE(ok(m.try_verify(tag)));
    ok(m.try_update(v.data));
    TEST_ASSERT_TRUE(ok(m.try_verify(u8_array)));
    ok(m.try_update(v.data));
    TEST_ASSERT_FALSE(ok(m.try_verify(flipped)));
    TEST_ASSERT_TAG(empty_tag, ok(m.try_finish()));
    ok(m.try_update(v.data));
    TEST_ASSERT_FALSE(ok(m.try_verify(flipped_bytes)));
    TEST_ASSERT_TAG(empty_tag, ok(m.try_finish()));
    ok(m.try_update(v.data));
    TEST_ASSERT_FALSE(ok(m.try_verify(too_short)));
    TEST_ASSERT_TAG(empty_tag, ok(m.try_finish()));
    ok(m.try_update(v.data));
    TEST_ASSERT_FALSE(ok(m.try_verify(too_long)));
    TEST_ASSERT_TAG(empty_tag, ok(m.try_finish()));
    ok(m.try_update(v.data));
    TEST_ASSERT_FALSE(ok(m.try_verify(std::span<const std::byte>{})));
    ok(m.try_update(other));
    TEST_ASSERT_FALSE(ok(m.try_verify(good)));
    ok(m.try_update(v.data));
    TEST_ASSERT_TRUE(ok(m.try_verify(good)));
}

template<typename M>
void check_lifecycle(const std::vector<kat>& kats) {
    const kat& v = longest(kats);
    std::span<const uint8_t> all{v.data};
    auto prefix = all.first(all.size() / 2);
    auto rest = all.subspan(all.size() / 2);
    auto k = ok(M::try_import_key(v.key));
    auto empty_tag = to_hex(ok(M::try_compute(k, std::string_view{})));
    auto good = from_hex(v.tag);

    // Reset mid-message, and on a fresh authenticator
    {
        auto m = ok(M::make(k));
        m.reset();
        ok(m.try_update(prefix));
        m.reset();
        TEST_ASSERT_TAG(empty_tag, ok(m.try_finish()));
        ok(m.try_update(prefix));
        m.reset();
        ok(m.try_update(all));
        TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));
    }

    // Destroy mid-message, repeatedly, then authenticate again
    for (int i = 0; i < 16; ++i) {
        auto m = ok(M::make(k));
        ok(m.try_update(prefix));
    }
    {
        auto m = ok(M::make(k));
        ok(m.try_update(all));
        TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));
    }

    // Move construction carries the partial message
    {
        auto a = ok(M::make(k));
        ok(a.try_update(prefix));
        M b{std::move(a)};
        ok(b.try_update(rest));
        TEST_ASSERT_TAG(v.tag, ok(b.try_finish()));
    }

    // Move assignment discards the target's message and takes over the source's
    {
        auto x = ok(M::make(k));
        auto y = ok(M::make(k));
        ok(y.try_update(prefix));
        x = std::move(y);
        ok(x.try_update(rest));
        TEST_ASSERT_TAG(v.tag, ok(x.try_finish()));
    }
    {
        auto other_key = ok(M::try_import_key(kats[0].key));
        auto x = ok(M::make(other_key));
        ok(x.try_update(kats[0].data));
        x.reset();
        auto y = ok(M::make(k));
        x = std::move(y);
        ok(x.try_update(all));
        TEST_ASSERT_TAG(v.tag, ok(x.try_finish()));
    }

    // Several authenticators can share a key
    {
        auto a = ok(M::make(k));
        auto b = ok(M::make(k));
        ok(a.try_update(all));
        TEST_ASSERT_TAG(v.tag, ok(a.try_finish()));
        ok(b.try_update(all));
        TEST_ASSERT_TRUE(ok(b.try_verify(good)));
    }

    // The key can be moved, mid-message and between messages, while an authenticator uses it
    {
        auto original = ok(M::try_import_key(v.key));
        auto m = ok(M::make(original));
        ok(m.try_update(prefix));
        crypto::key moved{std::move(original)};
        ok(m.try_update(rest));
        TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));
        std::vector<crypto::key> keys;
        keys.push_back(std::move(moved));
        for (int i = 0; i < 8; ++i) {
            keys.push_back(ok(M::try_import_key(v.key))); // reallocates
        }
        ok(m.try_update(all));
        TEST_ASSERT_TRUE(ok(m.try_verify(good)));
    }
}

template<typename M>
void check_independence(const std::vector<kat>& kats) {
    const kat& v = longest(kats);
    std::span<const uint8_t> all{v.data};
    auto prefix = all.first(all.size() / 2);
    auto rest = all.subspan(all.size() / 2);
    auto k = ok(M::try_import_key(v.key));
    const kat& w = kats[1];
    auto k2 = ok(M::try_import_key(w.key));

    // Two authenticators, with different keys, interleaved
    {
        auto a = ok(M::make(k));
        auto b = ok(M::make(k2));
        ok(a.try_update(prefix));
        ok(b.try_update(as_text(w.data).substr(0, 4)));
        ok(a.try_update(rest));
        ok(b.try_update(as_text(w.data).substr(4)));
        TEST_ASSERT_TAG(v.tag, ok(a.try_finish()));
        TEST_ASSERT_TAG(w.tag, ok(b.try_finish()));
    }

    // One-shot calls while an incremental message is in progress don't disturb it
    {
        auto m = ok(M::make(k));
        ok(m.try_update(prefix));
        TEST_ASSERT_TAG(w.tag, ok(M::try_compute(k2, w.data)));
        TEST_ASSERT_TRUE(ok(M::try_compare(k2, w.data, from_hex(w.tag))));
        ok(m.try_update(rest));
        TEST_ASSERT_TAG(v.tag, ok(m.try_finish()));
    }
}

// Two mac::operations with different keys, interleaved, and one-shot calls while a message is in
// progress.
void check_operation_independence(const mac_params& p, const std::vector<kat>& kats) {
    using crypto::mac::operation;
    const kat& v = longest(kats);
    std::span<const uint8_t> all{v.data};
    auto prefix = all.first(all.size() / 2);
    auto rest = all.subspan(all.size() / 2);
    std::array<std::byte, 80> buf; // larger than any tag
    auto k = import_key(p, v.key);
    const kat& w = kats[1];
    auto k2 = import_key(p, w.key);

    // Two operations, with different keys, interleaved
    {
        auto a = ok(operation::make(k));
        auto b = ok(operation::make(k2));
        ok(a.try_update(prefix));
        ok(b.try_update(as_text(w.data).substr(0, 4)));
        ok(a.try_update(rest));
        ok(b.try_update(as_text(w.data).substr(4)));
        TEST_ASSERT_TAG(v.tag, ok(a.try_finish(buf)));
        TEST_ASSERT_TAG(w.tag, ok(b.try_finish(buf)));
    }

    // One-shot calls while an incremental message is in progress don't disturb it
    {
        auto op = ok(operation::make(k));
        ok(op.try_update(prefix));
        TEST_ASSERT_TAG(w.tag, ok(operation::try_compute(k2, w.data, buf)));
        TEST_ASSERT_TRUE(ok(operation::try_compare(k2, w.data, from_hex(w.tag))));
        ok(op.try_update(rest));
        TEST_ASSERT_TAG(v.tag, ok(op.try_finish(buf)));
    }
}

// Generated keys: an HMAC key is as long as the hash's output, whatever the authenticator's tag
// size.
template<typename M>
void check_generated_keys() {
    constexpr bool is_hmac = crypto::detail::mac_key_type(M::algorithm) == crypto::key_type::hmac;
    constexpr size_t full_tag_size = crypto::mac::tag_size(M::algorithm);
    auto a = ok(M::try_generate_key());
    auto b = ok(M::try_generate_key());
    TEST_ASSERT_EQUAL(
        std::to_underlying(crypto::detail::mac_key_type(M::algorithm)), std::to_underlying(a.type())
    );
    TEST_ASSERT_EQUAL_INT32(is_hmac ? full_tag_size * 8 : 128, static_cast<int32_t>(a.bits()));
    TEST_ASSERT_TRUE(a.usage() == sign_verify);
    TEST_ASSERT_TRUE(a.algorithm() == key_algorithm_of<M>);

    auto tag_a = ok(M::try_compute(a, "message"));
    auto tag_b = ok(M::try_compute(b, "message"));
    TEST_ASSERT_TRUE(tag_a != tag_b);
    TEST_ASSERT_TRUE(ok(M::try_compare(a, "message", tag_a)));
    TEST_ASSERT_FALSE(ok(M::try_compare(b, "message", tag_a)));

    // A chosen size
    std::size_t bits = is_hmac ? full_tag_size * 16 : 256;
    auto c = ok(M::try_generate_key(bits));
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(bits), static_cast<int32_t>(c.bits()));
    TEST_ASSERT_TRUE(c.usage() == sign_verify);
    TEST_ASSERT_TRUE(c.algorithm() == key_algorithm_of<M>);
    auto tag_c = ok(M::try_compute(c, "message"));
    TEST_ASSERT_TRUE(ok(M::try_compare(c, "message", tag_c)));
    fails_with(crypto::errc::invalid_argument, M::try_generate_key(is_hmac ? 12 : 100));
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    TEST_ASSERT_EQUAL_INT32(
        static_cast<int32_t>(bits), static_cast<int32_t>(M::generate_key(bits).bits())
    );
#endif
}

// Every combination of the usages an authenticator uses, with each operation. A key that permits
// computing tags gives an authenticator that computes them (and can't verify unless the key also
// permits that); a key that permits only checking tags gives an authenticator that can only verify.
template<typename M>
void check_policy(const std::vector<uint8_t>& material) {
    auto make = [&](flags<crypto::key_usage> usage) {
        return ok(crypto::key::try_import(
            {.type = crypto::detail::mac_key_type(M::algorithm),
             .usage = usage,
             .algorithm = key_algorithm_of<M>},
            material
        ));
    };
    auto both = make(sign_verify);
    auto sign_only = make(crypto::key_usage::sign_message);
    auto verify_only = make(crypto::key_usage::verify_message);
    auto neither = make(crypto::key_usage::none);
    auto export_only = make(crypto::key_usage::export_key);

    std::string_view msg = "policy";
    auto good = ok(M::try_compute(both, msg));
    auto empty_tag = ok(M::try_compute(both, std::string_view{}));
    auto flipped = good;
    flipped[0] ^= std::byte{0x01};
    auto too_short = std::span<const std::byte>{good}.first(M::tag_size - 1);

    // Both: everything is permitted
    {
        TEST_ASSERT_TRUE(ok(M::try_compare(both, msg, good)));
        auto m = ok(M::make(both));
        ok(m.try_update(msg));
        TEST_ASSERT_TAG(to_hex(good), ok(m.try_finish()));
        ok(m.try_update(msg));
        TEST_ASSERT_TRUE(ok(m.try_verify(good)));
        ok(m.try_update(msg));
        TEST_ASSERT_FALSE(ok(m.try_verify(flipped)));
        TEST_ASSERT_TRUE(ok(m.try_verify(empty_tag)));
    }

    // Sign only: compute and finish
    {
        TEST_ASSERT_TAG(to_hex(good), ok(M::try_compute(sign_only, msg)));
        fails_with(crypto::errc::not_permitted, M::try_compare(sign_only, msg, good));
        fails_with(crypto::errc::not_permitted, M::try_compare(sign_only, msg, too_short));
        auto m = ok(M::make(sign_only));
        ok(m.try_update(msg));
        TEST_ASSERT_TAG(to_hex(good), ok(m.try_finish()));
        ok(m.try_update(msg));
        fails_with(crypto::errc::not_permitted, m.try_verify(good));
        fails_with(crypto::errc::not_permitted, m.try_verify(too_short));
        // The failed verify discarded the message
        TEST_ASSERT_TAG(to_hex(empty_tag), ok(m.try_finish()));
        ok(m.try_update(msg));
        TEST_ASSERT_TAG(to_hex(good), ok(m.try_finish()));
    }

    // Verify only: compare and verify
    {
        fails_with(crypto::errc::not_permitted, M::try_compute(verify_only, msg));
        TEST_ASSERT_TRUE(ok(M::try_compare(verify_only, msg, good)));
        TEST_ASSERT_FALSE(ok(M::try_compare(verify_only, msg, flipped)));
        TEST_ASSERT_FALSE(ok(M::try_compare(verify_only, msg, too_short)));
        auto m = ok(M::make(verify_only));
        ok(m.try_update(msg));
        fails_with(crypto::errc::not_permitted, m.try_finish());
        fails_with(crypto::errc::not_permitted, m.try_finish());
        // The failed finish discarded the message
        TEST_ASSERT_TRUE(ok(m.try_verify(empty_tag)));
        ok(m.try_update(msg));
        TEST_ASSERT_TRUE(ok(m.try_verify(good)));
        ok(m.try_update(msg));
        TEST_ASSERT_FALSE(ok(m.try_verify(flipped)));
        ok(m.try_update(msg));
        TEST_ASSERT_FALSE(ok(m.try_verify(too_short)));
        ok(m.try_update(as_text(text("pol"))));
        ok(m.try_update(as_text(text("icy"))));
        TEST_ASSERT_TRUE(ok(m.try_verify(good)));
    }

    // Neither (with or without other usages): nothing is permitted, and no authenticator can be
    // created
    for (const crypto::key* k : {&neither, &export_only}) {
        fails_with(crypto::errc::not_permitted, M::try_compute(*k, msg));
        fails_with(crypto::errc::not_permitted, M::try_compare(*k, msg, good));
        fails_with(crypto::errc::not_permitted, M::try_compare(*k, msg, too_short));
        rejects_key<M>(crypto::errc::not_permitted, *k);
    }
}

// The same as check_policy, through mac::operation with keys for p's algorithm and tag length.
void check_operation_policy(const mac_params& p, const std::vector<uint8_t>& material) {
    using crypto::mac::operation;
    auto both = import_key(p, material);
    auto sign_only = import_key(p, material, crypto::key_usage::sign_message);
    auto verify_only = import_key(p, material, crypto::key_usage::verify_message);
    auto neither = import_key(p, material, crypto::key_usage::none);
    auto export_only = import_key(p, material, crypto::key_usage::export_key);

    std::string_view msg = "policy";
    std::array<std::byte, 80> buf; // larger than any tag
    auto small = std::span{buf}.first(p.tag_size - 1);
    auto good = copy_of(ok(operation::try_compute(both, msg, buf)));
    auto empty_tag = copy_of(ok(operation::try_compute(both, std::string_view{}, buf)));
    auto flipped = good;
    flipped[0] ^= std::byte{0x01};
    auto too_short = std::span<const std::byte>{good}.first(p.tag_size - 1);
    {
        TEST_ASSERT_TAG(to_hex(good), ok(operation::try_compute(both, msg, buf)));
        TEST_ASSERT_TRUE(ok(operation::try_compare(both, msg, good)));
        auto op = ok(operation::make(both));
        ok(op.try_update(msg));
        TEST_ASSERT_TAG(to_hex(good), ok(op.try_finish(buf)));
        ok(op.try_update(msg));
        TEST_ASSERT_TRUE(ok(op.try_verify(good)));
        ok(op.try_update(msg));
        TEST_ASSERT_FALSE(ok(op.try_verify(flipped)));
        TEST_ASSERT_TRUE(ok(op.try_verify(empty_tag)));
    }
    {
        TEST_ASSERT_TAG(to_hex(good), ok(operation::try_compute(sign_only, msg, buf)));
        fails_with(crypto::errc::not_permitted, operation::try_compare(sign_only, msg, good));
        fails_with(
            crypto::errc::not_permitted, operation::try_compare(sign_only, msg, too_short)
        );
        auto op = ok(operation::make(sign_only));
        ok(op.try_update(msg));
        TEST_ASSERT_TAG(to_hex(good), ok(op.try_finish(buf)));
        ok(op.try_update(msg));
        fails_with(crypto::errc::not_permitted, op.try_verify(good));
        fails_with(crypto::errc::not_permitted, op.try_verify(too_short));
        // The failed verify discarded the message
        TEST_ASSERT_TAG(to_hex(empty_tag), ok(op.try_finish(buf)));
    }
    {
        fails_with(crypto::errc::not_permitted, operation::try_compute(verify_only, msg, buf));
        // A key that can't compute tags is reported in preference to the buffer size
        fails_with(
            crypto::errc::not_permitted, operation::try_compute(verify_only, msg, small)
        );
        TEST_ASSERT_TRUE(ok(operation::try_compare(verify_only, msg, good)));
        TEST_ASSERT_FALSE(ok(operation::try_compare(verify_only, msg, flipped)));
        TEST_ASSERT_FALSE(ok(operation::try_compare(verify_only, msg, too_short)));
        auto op = ok(operation::make(verify_only));
        ok(op.try_update(msg));
        // The key's usage is checked before the buffer size, and the message is discarded
        fails_with(crypto::errc::not_permitted, op.try_finish(small));
        TEST_ASSERT_TRUE(ok(op.try_verify(empty_tag)));
        ok(op.try_update(msg));
        fails_with(crypto::errc::not_permitted, op.try_finish(buf));
        TEST_ASSERT_TRUE(ok(op.try_verify(empty_tag)));
        ok(op.try_update(msg));
        TEST_ASSERT_TRUE(ok(op.try_verify(good)));
        ok(op.try_update(msg));
        TEST_ASSERT_FALSE(ok(op.try_verify(flipped)));
    }
    for (const crypto::key* k : {&neither, &export_only}) {
        fails_with(crypto::errc::not_permitted, operation::try_compute(*k, msg, buf));
        fails_with(crypto::errc::not_permitted, operation::try_compute(*k, msg, small));
        fails_with(crypto::errc::not_permitted, operation::try_compare(*k, msg, good));
        fails_with(crypto::errc::not_permitted, operation::try_compare(*k, msg, too_short));
        rejects_key<operation>(crypto::errc::not_permitted, *k);
    }
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
template<typename M>
void check_throwing_api(const std::vector<kat>& kats) {
    const kat& v = kats[1];
    auto good = from_hex(v.tag);
    auto flipped = good;
    flipped[0] ^= 0x80;

    auto k = M::import_key(v.key);
    TEST_ASSERT_TAG(v.tag, M::compute(k, v.data));
    TEST_ASSERT_TAG(v.tag, M::compute(k, as_text(v.data)));
    TEST_ASSERT_TRUE(M::compare(k, v.data, good));
    TEST_ASSERT_TRUE(M::compare(k, as_text(v.data), good));
    TEST_ASSERT_FALSE(M::compare(k, v.data, flipped));
    TEST_ASSERT_FALSE(M::compare(k, as_text(v.data), std::span<const uint8_t>{good}.first(1)));

    M m{k};
    m.update(as_text(v.data).substr(0, 1));
    m.update(std::span<const uint8_t>{v.data}.subspan(1));
    TEST_ASSERT_TAG(v.tag, m.finish());
    m.update(v.data);
    TEST_ASSERT_TRUE(m.verify(good));
    m.update(v.data);
    TEST_ASSERT_FALSE(m.verify(flipped));

    auto g = M::generate_key();
    TEST_ASSERT_TRUE(M::compare(g, "abc", M::compute(g, "abc")));

    // Errors are thrown as std::system_error
    auto verify_only = crypto::key::import(
        {.type = crypto::detail::mac_key_type(M::algorithm),
         .usage = crypto::key_usage::verify_message,
         .algorithm = key_algorithm_of<M>},
        v.key
    );
    bool threw = false;
    try {
        (void)M::compute(verify_only, v.data);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::not_permitted);
    }
    TEST_ASSERT_TRUE(threw);

    threw = false;
    M vm{verify_only};
    try {
        vm.update(v.data);
        (void)vm.finish();
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::not_permitted);
    }
    TEST_ASSERT_TRUE(threw);

    auto unusable = crypto::key::import(
        {.type = crypto::detail::mac_key_type(M::algorithm),
         .usage = crypto::key_usage::none,
         .algorithm = key_algorithm_of<M>},
        v.key
    );
    rejects_key<M>(crypto::errc::not_permitted, unusable);

    threw = false;
    try {
        (void)M::import_key(byte_view{});
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::invalid_argument);
    }
    TEST_ASSERT_TRUE(threw);
}

// The exception-based API of mac::operation, with keys for p's algorithm and tag length.
void check_throwing_operation(const mac_params& p, const std::vector<kat>& kats) {
    using crypto::mac::operation;
    const kat& v = kats[1];
    auto good = from_hex(v.tag);
    auto flipped = good;
    flipped[0] ^= 0x80;
    auto key_for = [&](flags<crypto::key_usage> usage) {
        return crypto::key::import(
            {.type = p.key_type,
             .usage = usage,
             .algorithm = crypto::key_algorithm::truncated(p.algorithm, p.tag_size)},
            v.key
        );
    };

    auto k = key_for(sign_verify);
    std::array<std::byte, 80> buf; // larger than any tag
    auto small = std::span{buf}.first(p.tag_size - 1);
    TEST_ASSERT_TAG(v.tag, operation::compute(k, v.data, buf));
    TEST_ASSERT_TAG(v.tag, operation::compute(k, as_text(v.data), buf));
    TEST_ASSERT_TRUE(operation::compare(k, v.data, good));
    TEST_ASSERT_TRUE(operation::compare(k, as_text(v.data), good));
    TEST_ASSERT_FALSE(operation::compare(k, v.data, flipped));
    TEST_ASSERT_FALSE(
        operation::compare(k, as_text(v.data), std::span<const uint8_t>{good}.first(1))
    );
    operation op{k};
    TEST_ASSERT_TRUE(op.algorithm() == p.algorithm);
    TEST_ASSERT_EQUAL(p.tag_size, op.tag_size());
    op.update(as_text(v.data).substr(0, 1));
    op.update(std::span<const uint8_t>{v.data}.subspan(1));
    TEST_ASSERT_TAG(v.tag, op.finish(buf));
    op.update(v.data);
    TEST_ASSERT_TRUE(op.verify(good));
    op.update(v.data);
    TEST_ASSERT_FALSE(op.verify(flipped));

    // Errors are thrown as std::system_error
    bool threw = false;
    try {
        (void)operation::compute(k, v.data, small);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::buffer_too_small);
    }
    TEST_ASSERT_TRUE(threw);

    threw = false;
    op.update(v.data);
    try {
        (void)op.finish(small);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::buffer_too_small);
    }
    TEST_ASSERT_TRUE(threw);
    // The message survived the failed finish
    TEST_ASSERT_TAG(v.tag, op.finish(buf));

    auto verify_only = key_for(crypto::key_usage::verify_message);
    threw = false;
    try {
        (void)operation::compute(verify_only, v.data, buf);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::not_permitted);
    }
    TEST_ASSERT_TRUE(threw);

    threw = false;
    operation vop{verify_only};
    try {
        vop.update(v.data);
        (void)vop.finish(buf);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == crypto::errc::not_permitted);
    }
    TEST_ASSERT_TRUE(threw);
    rejects_key<operation>(crypto::errc::not_permitted, key_for(crypto::key_usage::none));
}
#endif

// Checks a truncated authenticator against full-length known answers: its tags are the leftmost
// bytes of the full tags, and a full-length tag never matches.
template<typename M>
void check_full_length_tags(const std::vector<kat>& full) {
    for (const auto& v : full) {
        auto k = ok(M::try_import_key(v.key));
        auto full_tag = from_hex(v.tag);
        TEST_ASSERT_TRUE(full_tag.size() > M::tag_size);
        auto prefix = std::span<const uint8_t>{full_tag}.first(M::tag_size);
        auto tag = ok(M::try_compute(k, v.data));
        TEST_ASSERT_EQUAL_MEMORY(prefix.data(), tag.data(), M::tag_size);

        TEST_ASSERT_TRUE(ok(M::try_compare(k, v.data, prefix)));
        TEST_ASSERT_FALSE(ok(M::try_compare(k, v.data, full_tag)));
        auto m = ok(M::make(k));
        ok(m.try_update(v.data));
        TEST_ASSERT_FALSE(ok(m.try_verify(full_tag)));
        ok(m.try_update(v.data));
        TEST_ASSERT_TRUE(ok(m.try_verify(prefix)));
    }
}

// Checks that the tag length is part of a key's policy: the truncated authenticator M refuses a
// full-length key and a key for another truncated length, and the authenticators for those lengths
// refuse M's keys. Each key works with its own authenticator, and the tags agree.
template<typename M>
void check_tag_length_policy(const std::vector<uint8_t>& material) {
    using full_mac = crypto::mac::authenticator<M::algorithm>;
    using other_mac = crypto::mac::authenticator<M::algorithm, M::tag_size + 1>;
    static_assert(M::tag_size < full_mac::tag_size);
    std::string_view msg = "message";

    auto full_key = ok(full_mac::try_import_key(material));
    auto truncated_key = ok(M::try_import_key(material));
    auto other_key = ok(other_mac::try_import_key(material));
    TEST_ASSERT_TRUE(full_key.algorithm() == M::algorithm);
    TEST_ASSERT_TRUE(truncated_key.algorithm() == crypto::key_algorithm(M::algorithm, M::tag_size));
    TEST_ASSERT_TRUE(other_key.algorithm() == key_algorithm_of<other_mac>);
    TEST_ASSERT_TRUE(truncated_key.algorithm() != full_key.algorithm());

    // Expected tags of the right length, so that compare() reaches the key's policy
    typename M::tag_type tag{};
    typename full_mac::tag_type full_tag{};
    typename other_mac::tag_type other_tag{};
    for (const crypto::key* k : {&full_key, &other_key}) {
        fails_with(crypto::errc::not_permitted, M::try_compute(*k, msg));
        fails_with(crypto::errc::not_permitted, M::try_compare(*k, msg, tag));
        rejects_key<M>(crypto::errc::not_permitted, *k);
    }
    fails_with(crypto::errc::not_permitted, full_mac::try_compute(truncated_key, msg));
    fails_with(crypto::errc::not_permitted, full_mac::try_compare(truncated_key, msg, full_tag));
    rejects_key<full_mac>(crypto::errc::not_permitted, truncated_key);
    fails_with(crypto::errc::not_permitted, other_mac::try_compute(truncated_key, msg));
    fails_with(crypto::errc::not_permitted, other_mac::try_compare(truncated_key, msg, other_tag));
    rejects_key<other_mac>(crypto::errc::not_permitted, truncated_key);

    // A key imported with the truncated algorithm suits M
    auto imported = ok(crypto::key::try_import(
        {.type = crypto::detail::mac_key_type(M::algorithm),
         .usage = sign_verify,
         .algorithm = crypto::key_algorithm{M::algorithm, M::tag_size}},
        material
    ));
    TEST_ASSERT_TRUE(imported.algorithm() == truncated_key.algorithm());

    // The truncated tags are prefixes of the full tag
    auto full = ok(full_mac::try_compute(full_key, msg));
    auto truncated_tag = ok(M::try_compute(truncated_key, msg));
    auto other = ok(other_mac::try_compute(other_key, msg));
    TEST_ASSERT_EQUAL_MEMORY(full.data(), truncated_tag.data(), M::tag_size);
    TEST_ASSERT_EQUAL_MEMORY(full.data(), other.data(), other_mac::tag_size);
    TEST_ASSERT_TAG(to_hex(truncated_tag), ok(M::try_compute(imported, msg)));
    TEST_ASSERT_TRUE(ok(M::try_compare(imported, msg, truncated_tag)));
}

// mac::operation with keys for p's algorithm and tag length: the known tags, written into a
// caller-supplied buffer.
void check_operation(const mac_params& p, const std::vector<kat>& kats) {
    using crypto::mac::operation;
    std::array<std::byte, 80> buf; // larger than any tag
    auto small = std::span{buf}.first(p.tag_size - 1);
    std::vector<std::byte> exact(p.tag_size);

    for (const auto& v : kats) {
        auto k = import_key(p, v.key);
        auto good = from_hex(v.tag);
        auto flipped = good;
        flipped[0] ^= 0x01;

        // One-shot: the tag is the filled prefix of the buffer
        auto t = ok(operation::try_compute(k, v.data, buf));
        TEST_ASSERT_EQUAL_PTR(buf.data(), t.data());
        TEST_ASSERT_EQUAL(p.tag_size, t.size());
        TEST_ASSERT_TAG(v.tag, t);
        TEST_ASSERT_TAG(v.tag, ok(operation::try_compute(k, bytes_of(v.data), exact)));
        TEST_ASSERT_TAG(v.tag, ok(operation::try_compute(k, as_text(v.data), buf)));
        TEST_ASSERT_TRUE(ok(operation::try_compare(k, v.data, good)));
        TEST_ASSERT_TRUE(ok(operation::try_compare(k, as_text(v.data), bytes_from_hex(v.tag))));
        TEST_ASSERT_FALSE(ok(operation::try_compare(k, v.data, flipped)));

        // Incremental
        auto op = ok(operation::make(k));
        TEST_ASSERT_TRUE(op.algorithm() == p.algorithm);
        TEST_ASSERT_EQUAL(p.tag_size, op.tag_size());
        ok(op.try_update(v.data));
        auto f = ok(op.try_finish(buf));
        TEST_ASSERT_EQUAL_PTR(buf.data(), f.data());
        TEST_ASSERT_EQUAL(p.tag_size, f.size());
        TEST_ASSERT_TAG(v.tag, f);
        ok(op.try_update(as_text(v.data)));
        TEST_ASSERT_TAG(v.tag, ok(op.try_finish(exact)));
        ok(op.try_update(bytes_of(v.data)));
        TEST_ASSERT_TRUE(ok(op.try_verify(good)));
        ok(op.try_update(v.data));
        TEST_ASSERT_FALSE(ok(op.try_verify(flipped)));
    }

    const kat& v = longest(kats);
    std::span<const uint8_t> all{v.data};
    auto prefix = all.first(all.size() / 2);
    auto rest = all.subspan(all.size() / 2);
    auto k = import_key(p, v.key);
    auto good = from_hex(v.tag);
    auto too_short = std::span<const uint8_t>{good}.first(p.tag_size - 1);
    auto too_long = good;
    too_long.push_back(0);
    auto empty_tag = to_hex(ok(operation::try_compute(k, std::string_view{}, buf)));

    // A tag of the wrong length never matches; a buffer too small for the tag is an error
    TEST_ASSERT_FALSE(ok(operation::try_compare(k, v.data, too_short)));
    TEST_ASSERT_FALSE(ok(operation::try_compare(k, v.data, too_long)));
    TEST_ASSERT_FALSE(ok(operation::try_compare(k, v.data, byte_view{})));
    fails_with(crypto::errc::buffer_too_small, operation::try_compute(k, v.data, small));
    fails_with(crypto::errc::buffer_too_small, operation::try_compute(k, v.data, byte_span{}));

    auto op = ok(operation::make(k));

    // Finishing into a buffer too small leaves the message unchanged
    ok(op.try_update(prefix));
    fails_with(crypto::errc::buffer_too_small, op.try_finish(small));
    fails_with(crypto::errc::buffer_too_small, op.try_finish(byte_span{}));
    ok(op.try_update(rest));
    TEST_ASSERT_TAG(v.tag, ok(op.try_finish(buf)));

    // Finishing without an update gives the empty message's tag
    TEST_ASSERT_TAG(empty_tag, ok(op.try_finish(buf)));

    // A wrong-length verify doesn't match, and leaves the operation ready for a new message
    ok(op.try_update(all));
    TEST_ASSERT_FALSE(ok(op.try_verify(too_short)));
    TEST_ASSERT_TAG(empty_tag, ok(op.try_finish(buf)));
    ok(op.try_update(all));
    TEST_ASSERT_FALSE(ok(op.try_verify(too_long)));
    TEST_ASSERT_TAG(empty_tag, ok(op.try_finish(buf)));

    // Reset mid-message
    ok(op.try_update(prefix));
    op.reset();
    ok(op.try_update(all));
    TEST_ASSERT_TAG(v.tag, ok(op.try_finish(buf)));

    // Pieces of varying size, including empty ones
    const size_t sizes[] = {0, 1, 7, 0, 64, 3, 128};
    size_t pos = 0;
    for (size_t i = 0; pos < all.size(); ++i) {
        size_t n = std::min(sizes[i % std::size(sizes)], all.size() - pos);
        ok(op.try_update(all.subspan(pos, n)));
        pos += n;
    }
    TEST_ASSERT_TRUE(ok(op.try_verify(good)));

    // Move construction carries the partial message
    ok(op.try_update(prefix));
    operation moved{std::move(op)};
    TEST_ASSERT_TRUE(moved.algorithm() == p.algorithm);
    TEST_ASSERT_EQUAL(p.tag_size, moved.tag_size());
    ok(moved.try_update(rest));
    TEST_ASSERT_TAG(v.tag, ok(moved.try_finish(buf)));

    // Move assignment takes over the source's message, and discards the target's
    {
        auto source = ok(operation::make(k));
        ok(source.try_update(prefix));
        moved = std::move(source);
        ok(moved.try_update(rest));
        TEST_ASSERT_TAG(v.tag, ok(moved.try_finish(buf)));
    }
    {
        auto idle = ok(operation::make(k));
        ok(moved.try_update(prefix));
        moved = std::move(idle);
        ok(moved.try_update(all));
        TEST_ASSERT_TAG(v.tag, ok(moved.try_finish(buf)));
    }

    // The key can be moved while an operation uses it
    {
        auto original = import_key(p, v.key);
        auto o = ok(operation::make(original));
        ok(o.try_update(prefix));
        crypto::key moved_key{std::move(original)};
        ok(o.try_update(rest));
        TEST_ASSERT_TAG(v.tag, ok(o.try_finish(buf)));
    }
}

// Input longer than the chunk size, whole and in pieces that straddle chunk boundaries, through an
// authenticator. `full_tag` is the full-length tag of patterned_message(3 * 4096 + 7) under
// `material`.
template<typename M>
void check_chunked_input(const std::vector<uint8_t>& material, std::string_view full_tag) {
    auto msg = patterned_message(3 * 4096 + 7);
    std::span<const uint8_t> all{msg};
    std::string tag{full_tag.substr(0, M::tag_size * 2)};
    auto good = from_hex(tag);
    auto flipped = good;
    flipped.back() ^= 0x01;
    const size_t sizes[] = {4095, 4098, 1, 8191};
    auto k = ok(M::try_import_key(material));

    TEST_ASSERT_TAG(tag, ok(M::try_compute(k, all)));
    TEST_ASSERT_TRUE(ok(M::try_compare(k, all, good)));
    TEST_ASSERT_FALSE(ok(M::try_compare(k, all, flipped)));

    auto m = ok(M::make(k));
    ok(m.try_update(all));
    TEST_ASSERT_TAG(tag, ok(m.try_finish()));
    ok(m.try_update(all));
    TEST_ASSERT_TRUE(ok(m.try_verify(good)));
    ok(m.try_update(all));
    TEST_ASSERT_FALSE(ok(m.try_verify(flipped)));
    size_t pos = 0;
    for (size_t i = 0; pos < all.size(); ++i) {
        size_t n = std::min(sizes[i % std::size(sizes)], all.size() - pos);
        ok(m.try_update(all.subspan(pos, n)));
        pos += n;
    }
    TEST_ASSERT_TAG(tag, ok(m.try_finish()));
}

// The same as check_chunked_input, through mac::operation with keys for p's algorithm and tag
// length.
void check_chunked_operation(
    const mac_params& p, const std::vector<uint8_t>& material, std::string_view full_tag
) {
    using crypto::mac::operation;
    auto msg = patterned_message(3 * 4096 + 7);
    std::span<const uint8_t> all{msg};
    std::string tag{full_tag.substr(0, p.tag_size * 2)};
    auto good = from_hex(tag);
    auto flipped = good;
    flipped.back() ^= 0x01;
    const size_t sizes[] = {4095, 4098, 1, 8191};
    auto k = import_key(p, material);

    std::array<std::byte, 64> buf;
    TEST_ASSERT_TAG(tag, ok(operation::try_compute(k, all, buf)));
    TEST_ASSERT_TRUE(ok(operation::try_compare(k, all, good)));
    TEST_ASSERT_FALSE(ok(operation::try_compare(k, all, flipped)));
    auto op = ok(operation::make(k));
    ok(op.try_update(all));
    TEST_ASSERT_TAG(tag, ok(op.try_finish(buf)));
    size_t pos = 0;
    for (size_t i = 0; pos < all.size(); ++i) {
        size_t n = std::min(sizes[i % std::size(sizes)], all.size() - pos);
        ok(op.try_update(all.subspan(pos, n)));
        pos += n;
    }
    TEST_ASSERT_TRUE(ok(op.try_verify(good)));

    // A key that may only check tags: its operations are set up to check one
    auto verify_only = import_key(p, material, crypto::key_usage::verify_message);
    auto checker = ok(operation::make(verify_only));
    ok(checker.try_update(all));
    TEST_ASSERT_TRUE(ok(checker.try_verify(good)));
    ok(checker.try_update(all));
    TEST_ASSERT_FALSE(ok(checker.try_verify(flipped)));
}

template<typename M>
constexpr bool has_authenticator_traits() {
    // The constructor from a key throws, so it exists only with exceptions enabled
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    constexpr bool has_key_ctor = true;
#else
    constexpr bool has_key_ctor = false;
#endif
    return !std::is_default_constructible_v<M> &&
           std::is_constructible_v<M, const crypto::key&> == has_key_ctor &&
           std::is_same_v<decltype(M::make(std::declval<const crypto::key&>())), result<M>> &&
           !std::is_convertible_v<const crypto::key&, M> && !std::is_copy_constructible_v<M> &&
           !std::is_copy_assignable_v<M> && std::is_nothrow_move_constructible_v<M> &&
           std::is_nothrow_move_assignable_v<M> && std::is_nothrow_destructible_v<M> &&
           std::is_same_v<typename M::tag_type, std::array<std::byte, M::tag_size>>;
}

// An authenticator's tag size is the crypto library's tag length for its (truncated) algorithm
template<typename M>
constexpr bool tag_size_matches_psa() {
    constexpr auto type = crypto::detail::mac_key_type(M::algorithm);
    return M::tag_size ==
           PSA_MAC_LENGTH(
               std::to_underlying(type),
               type == crypto::key_type::hmac ? 0 : 128,
               crypto::detail::truncated_mac_algorithm(M::algorithm, M::tag_size)
           );
}

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// The test app enables HMAC and every hash algorithm it can (see hash_test.cpp), and AES-CMAC
// (sdkconfig.defaults / sdkconfig.qemu). AES-CMAC is not required here so that the tests also build
// in a configuration without it.
#ifndef PSA_WANT_ALG_HMAC
#error "test app must enable hmac"
#endif

// mac::tag_size: the full tag length, in constant expressions, 0 for a value that is not a MAC
// algorithm. A truncated algorithm's full length is that of the algorithm it truncates.
static_assert(crypto::mac::tag_size(static_cast<crypto::mac::algorithm>(PSA_ALG_NONE)) == 0);
static_assert(crypto::mac::tag_size(static_cast<crypto::mac::algorithm>(PSA_ALG_SHA_256)) == 0);
static_assert(
    crypto::mac::tag_size(static_cast<crypto::mac::algorithm>(PSA_ALG_HMAC(PSA_ALG_SHA_256))) == 32
);
static_assert(
    crypto::mac::tag_size(static_cast<crypto::mac::algorithm>(
        PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), 16)
    )) == 32
);
static_assert(crypto::mac::tag_size(static_cast<crypto::mac::algorithm>(PSA_ALG_CMAC)) == 16);

// mac::operation: created from a key (throwing constructor only with exceptions), move-only
static_assert(!std::is_default_constructible_v<crypto::mac::operation>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_constructible_v<crypto::mac::operation, const crypto::key&>);
#else
static_assert(!std::is_constructible_v<crypto::mac::operation, const crypto::key&>);
#endif
static_assert(!std::is_convertible_v<const crypto::key&, crypto::mac::operation>);
static_assert(std::is_same_v<
              decltype(crypto::mac::operation::make(std::declval<const crypto::key&>())),
              result<crypto::mac::operation>>);
static_assert(!std::is_copy_constructible_v<crypto::mac::operation>);
static_assert(!std::is_copy_assignable_v<crypto::mac::operation>);
static_assert(std::is_nothrow_move_constructible_v<crypto::mac::operation>);
static_assert(std::is_nothrow_move_assignable_v<crypto::mac::operation>);
static_assert(std::is_nothrow_destructible_v<crypto::mac::operation>);
static_assert(std::is_same_v<
              decltype(crypto::mac::operation::try_compute(
                  std::declval<const crypto::key&>(), byte_view{}, byte_span{}
              )),
              result<byte_span>>);
static_assert(std::is_same_v<
              decltype(std::declval<crypto::mac::operation&>().try_finish(byte_span{})),
              result<byte_span>>);

#ifdef PSA_WANT_ALG_MD5
static_assert(std::to_underlying(crypto::mac::algorithm::hmac_md5) == PSA_ALG_HMAC(PSA_ALG_MD5));
static_assert(crypto::mac::hmac_md5::algorithm == crypto::mac::algorithm::hmac_md5);
static_assert(crypto::mac::hmac_md5::tag_size == 16);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_md5) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_md5>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_md5>());
#endif

#ifdef PSA_WANT_ALG_SHA_1
static_assert(std::to_underlying(crypto::mac::algorithm::hmac_sha1) == PSA_ALG_HMAC(PSA_ALG_SHA_1));
static_assert(crypto::mac::hmac_sha1::algorithm == crypto::mac::algorithm::hmac_sha1);
static_assert(crypto::mac::hmac_sha1::tag_size == 20);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_sha1) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_sha1>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_sha1>());
#endif

#ifdef PSA_WANT_ALG_SHA_224
static_assert(
    std::to_underlying(crypto::mac::algorithm::hmac_sha224) == PSA_ALG_HMAC(PSA_ALG_SHA_224)
);
static_assert(crypto::mac::hmac_sha224::algorithm == crypto::mac::algorithm::hmac_sha224);
static_assert(crypto::mac::hmac_sha224::tag_size == 28);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_sha224) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_sha224>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_sha224>());
#endif

#ifdef PSA_WANT_ALG_SHA_256
static_assert(
    std::to_underlying(crypto::mac::algorithm::hmac_sha256) == PSA_ALG_HMAC(PSA_ALG_SHA_256)
);
static_assert(crypto::mac::hmac_sha256::algorithm == crypto::mac::algorithm::hmac_sha256);
static_assert(crypto::mac::hmac_sha256::tag_size == 32);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_sha256) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_sha256>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_sha256>());
#endif

#ifdef PSA_WANT_ALG_SHA_384
static_assert(
    std::to_underlying(crypto::mac::algorithm::hmac_sha384) == PSA_ALG_HMAC(PSA_ALG_SHA_384)
);
static_assert(crypto::mac::hmac_sha384::algorithm == crypto::mac::algorithm::hmac_sha384);
static_assert(crypto::mac::hmac_sha384::tag_size == 48);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_sha384) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_sha384>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_sha384>());
#endif

#ifdef PSA_WANT_ALG_SHA_512
static_assert(
    std::to_underlying(crypto::mac::algorithm::hmac_sha512) == PSA_ALG_HMAC(PSA_ALG_SHA_512)
);
static_assert(crypto::mac::hmac_sha512::algorithm == crypto::mac::algorithm::hmac_sha512);
static_assert(crypto::mac::hmac_sha512::tag_size == 64);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_sha512) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_sha512>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_sha512>());
#endif

#ifdef PSA_WANT_ALG_SHA3_224
static_assert(
    std::to_underlying(crypto::mac::algorithm::hmac_sha3_224) == PSA_ALG_HMAC(PSA_ALG_SHA3_224)
);
static_assert(crypto::mac::hmac_sha3_224::algorithm == crypto::mac::algorithm::hmac_sha3_224);
static_assert(crypto::mac::hmac_sha3_224::tag_size == 28);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_sha3_224) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_sha3_224>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_sha3_224>());
#endif

#ifdef PSA_WANT_ALG_SHA3_256
static_assert(
    std::to_underlying(crypto::mac::algorithm::hmac_sha3_256) == PSA_ALG_HMAC(PSA_ALG_SHA3_256)
);
static_assert(crypto::mac::hmac_sha3_256::algorithm == crypto::mac::algorithm::hmac_sha3_256);
static_assert(crypto::mac::hmac_sha3_256::tag_size == 32);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_sha3_256) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_sha3_256>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_sha3_256>());
#endif

#ifdef PSA_WANT_ALG_SHA3_384
static_assert(
    std::to_underlying(crypto::mac::algorithm::hmac_sha3_384) == PSA_ALG_HMAC(PSA_ALG_SHA3_384)
);
static_assert(crypto::mac::hmac_sha3_384::algorithm == crypto::mac::algorithm::hmac_sha3_384);
static_assert(crypto::mac::hmac_sha3_384::tag_size == 48);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_sha3_384) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_sha3_384>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_sha3_384>());
#endif

#ifdef PSA_WANT_ALG_SHA3_512
static_assert(
    std::to_underlying(crypto::mac::algorithm::hmac_sha3_512) == PSA_ALG_HMAC(PSA_ALG_SHA3_512)
);
static_assert(crypto::mac::hmac_sha3_512::algorithm == crypto::mac::algorithm::hmac_sha3_512);
static_assert(crypto::mac::hmac_sha3_512::tag_size == 64);
static_assert(
    crypto::detail::mac_key_type(crypto::mac::algorithm::hmac_sha3_512) == crypto::key_type::hmac
);
static_assert(has_authenticator_traits<crypto::mac::hmac_sha3_512>());
static_assert(tag_size_matches_psa<crypto::mac::hmac_sha3_512>());
#endif

#if defined(PSA_WANT_ALG_CMAC) && defined(PSA_WANT_KEY_TYPE_AES)
static_assert(std::to_underlying(crypto::mac::algorithm::cmac) == PSA_ALG_CMAC);
static_assert(crypto::mac::cmac::algorithm == crypto::mac::algorithm::cmac);
static_assert(crypto::mac::cmac::tag_size == 16);
static_assert(crypto::detail::mac_key_type(crypto::mac::algorithm::cmac) == crypto::key_type::aes);
static_assert(has_authenticator_traits<crypto::mac::cmac>());
static_assert(tag_size_matches_psa<crypto::mac::cmac>());
#endif

// Truncated tags: the tag size is part of the type, and the full length is the same type as the
// alias. A shorter tag is the PSA truncated algorithm; the full length is the plain algorithm.
namespace {
#ifdef PSA_WANT_ALG_SHA_256
using hmac_sha256_128 = crypto::mac::authenticator<crypto::mac::algorithm::hmac_sha256, 16>;
using hmac_sha256_32 = crypto::mac::authenticator<crypto::mac::algorithm::hmac_sha256, 4>;
#endif
#ifdef PSA_WANT_ALG_SHA_1
using hmac_sha1_96 = crypto::mac::authenticator<crypto::mac::algorithm::hmac_sha1, 12>;
#endif
#if defined(PSA_WANT_ALG_CMAC) && defined(PSA_WANT_KEY_TYPE_AES)
using cmac_32 = crypto::mac::authenticator<crypto::mac::algorithm::cmac, 4>;
#endif
} // namespace

#ifdef PSA_WANT_ALG_SHA_256
static_assert(hmac_sha256_128::algorithm == crypto::mac::algorithm::hmac_sha256);
static_assert(hmac_sha256_128::tag_size == 16);
static_assert(has_authenticator_traits<hmac_sha256_128>());
static_assert(tag_size_matches_psa<hmac_sha256_128>());
static_assert(hmac_sha256_32::tag_size == 4);
static_assert(has_authenticator_traits<hmac_sha256_32>());
static_assert(tag_size_matches_psa<hmac_sha256_32>());
static_assert(std::is_same_v<
              crypto::mac::authenticator<crypto::mac::algorithm::hmac_sha256, 32>,
              crypto::mac::hmac_sha256>);
static_assert(!std::is_same_v<hmac_sha256_128, crypto::mac::hmac_sha256>);
static_assert(
    crypto::detail::truncated_mac_algorithm(crypto::mac::algorithm::hmac_sha256, 16) ==
    PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), 16)
);
static_assert(
    crypto::detail::truncated_mac_algorithm(crypto::mac::algorithm::hmac_sha256, 4) ==
    PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), 4)
);
static_assert(
    crypto::detail::truncated_mac_algorithm(crypto::mac::algorithm::hmac_sha256, 32) ==
    PSA_ALG_HMAC(PSA_ALG_SHA_256)
);
static_assert(key_algorithm_of<hmac_sha256_128>.idf_algorithm() ==
              PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), 16));
static_assert(key_algorithm_of<crypto::mac::hmac_sha256> == crypto::mac::algorithm::hmac_sha256);
#endif

#ifdef PSA_WANT_ALG_SHA_1
static_assert(hmac_sha1_96::tag_size == 12);
static_assert(has_authenticator_traits<hmac_sha1_96>());
static_assert(tag_size_matches_psa<hmac_sha1_96>());
static_assert(std::is_same_v<
              crypto::mac::authenticator<crypto::mac::algorithm::hmac_sha1, 20>,
              crypto::mac::hmac_sha1>);
static_assert(
    crypto::detail::truncated_mac_algorithm(crypto::mac::algorithm::hmac_sha1, 12) ==
    PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_1), 12)
);
#endif

#ifdef PSA_WANT_ALG_SHA_512
// The longest truncation: PSA encodes a truncated length in 6 bits
static_assert(crypto::mac::authenticator<crypto::mac::algorithm::hmac_sha512, 63>::tag_size == 63);
static_assert(
    tag_size_matches_psa<crypto::mac::authenticator<crypto::mac::algorithm::hmac_sha512, 63>>()
);
static_assert(
    crypto::detail::truncated_mac_algorithm(crypto::mac::algorithm::hmac_sha512, 63) ==
    PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_512), 63)
);
static_assert(std::is_same_v<
              crypto::mac::authenticator<crypto::mac::algorithm::hmac_sha512, 64>,
              crypto::mac::hmac_sha512>);
#endif

#if defined(PSA_WANT_ALG_CMAC) && defined(PSA_WANT_KEY_TYPE_AES)
static_assert(cmac_32::algorithm == crypto::mac::algorithm::cmac);
static_assert(cmac_32::tag_size == 4);
static_assert(has_authenticator_traits<cmac_32>());
static_assert(tag_size_matches_psa<cmac_32>());
static_assert(
    std::is_same_v<crypto::mac::authenticator<crypto::mac::algorithm::cmac, 16>, crypto::mac::cmac>
);
static_assert(
    crypto::detail::truncated_mac_algorithm(crypto::mac::algorithm::cmac, 4) ==
    PSA_ALG_TRUNCATED_MAC(PSA_ALG_CMAC, 4)
);
static_assert(
    crypto::detail::truncated_mac_algorithm(crypto::mac::algorithm::cmac, 16) == PSA_ALG_CMAC
);
#endif

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

#ifdef PSA_WANT_ALG_MD5

namespace {
// RFC 2202 test cases, cross-checked with Python's hmac module.
const char* const hmac_md5_tags[6] = {
    "9294727a3638bb1c13f48ef8158bfc9d",
    "750c783e6ab0b503eaa86e310a5db738",
    "56be34521d144c88dbb8c733f0e8b3f6",
    "697eaf0aca3a3aea3a75164746ffaa79",
    "6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd",
    "6f630fad67cda0ee1fb1f562db3aa53e",
};
const std::vector<kat> hmac_md5_vectors = rfc2202(16, hmac_md5_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_md5_chunked =
    "5c7fd4f096ac3b3ff569c2b83508a8bd";
} // namespace

TEST_CASE("crypto hmac_md5 known-answer vectors", "[idfxx][crypto]") {
    check_tags<crypto::mac::hmac_md5>(hmac_md5_vectors);
}

TEST_CASE("crypto hmac_md5 split updates match one-shot", "[idfxx][crypto]") {
    check_split_operation(params_of<crypto::mac::hmac_md5>, hmac_md5_vectors);
}

TEST_CASE("crypto hmac_md5 interleaved operations are independent", "[idfxx][crypto]") {
    check_operation_independence(params_of<crypto::mac::hmac_md5>, hmac_md5_vectors);
}

TEST_CASE("crypto hmac_md5 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_operation(params_of<crypto::mac::hmac_md5>, text("Jefe"), hmac_md5_chunked);
}

TEST_CASE("crypto hmac_md5 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_md5>, hmac_md5_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_md5 exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<crypto::mac::hmac_md5>, hmac_md5_vectors);
}
#endif

#endif // PSA_WANT_ALG_MD5

#ifdef PSA_WANT_ALG_SHA_1

namespace {
// RFC 2202 test cases, cross-checked with Python's hmac module.
const char* const hmac_sha1_tags[6] = {
    "b617318655057264e28bc0b6fb378c8ef146be00",
    "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79",
    "125d7342b9ac11cd91a39af48aa17b4f63f175d3",
    "4c9007f4026250c6bc8414f9bf50c86c2d7235da",
    "aa4ae5e15272d00e95705637ce8a3b55ed402112",
    "e8e99d0f45237d786d6bbaa7965c7808bbff1a91",
};
const std::vector<kat> hmac_sha1_vectors = rfc2202(20, hmac_sha1_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_sha1_chunked =
    "31a296e8e17a313fffad08c23675400d25fa77ab";
} // namespace

TEST_CASE("crypto hmac_sha1 known-answer vectors", "[idfxx][crypto]") {
    check_tags<crypto::mac::hmac_sha1>(hmac_sha1_vectors);
}

TEST_CASE("crypto hmac_sha1 split updates match one-shot", "[idfxx][crypto]") {
    check_split_operation(params_of<crypto::mac::hmac_sha1>, hmac_sha1_vectors);
}

TEST_CASE("crypto hmac_sha1 interleaved operations are independent", "[idfxx][crypto]") {
    check_operation_independence(params_of<crypto::mac::hmac_sha1>, hmac_sha1_vectors);
}

TEST_CASE("crypto hmac_sha1 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_operation(params_of<crypto::mac::hmac_sha1>, text("Jefe"), hmac_sha1_chunked);
}

TEST_CASE("crypto hmac_sha1 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_sha1>, hmac_sha1_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_sha1 exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<crypto::mac::hmac_sha1>, hmac_sha1_vectors);
}
#endif

#endif // PSA_WANT_ALG_SHA_1

#ifdef PSA_WANT_ALG_SHA_224

namespace {
// RFC 4231 test cases, cross-checked with Python's hmac module.
const char* const hmac_sha224_tags[6] = {
    "896fb1128abbdf196832107cd49df33f47b4b1169912ba4f53684b22",
    "a30e01098bc6dbbf45690f3a7e9e6d0f8bbea2a39e6148008fd05e44",
    "7fb3cb3588c6c1f6ffa9694d7d6ad2649365b0c1f65d69d1ec8333ea",
    "6c11506874013cac6a2abc1bb382627cec6a90d86efc012de7afec5a",
    "95e9a0db962095adaebe9b2d6f0dbce2d499f112f2d2b7273fa6870e",
    "3a854166ac5d9f023f54d517d0b39dbd946770db9c2b95c9f6f565d1",
};
const std::vector<kat> hmac_sha224_vectors = rfc4231(hmac_sha224_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_sha224_chunked =
    "d8e2a68184f48bc0abf695bb7bae1dbb85e1f265a8729d4f1c96d5e5";
} // namespace

TEST_CASE("crypto hmac_sha224 known-answer vectors", "[idfxx][crypto]") {
    check_tags<crypto::mac::hmac_sha224>(hmac_sha224_vectors);
}

TEST_CASE("crypto hmac_sha224 split updates match one-shot", "[idfxx][crypto]") {
    check_split_operation(params_of<crypto::mac::hmac_sha224>, hmac_sha224_vectors);
}

// QEMU's esp32s3 SHA peripheral model produces a wrong digest when a SHA-224 operation resumes
// after another SHA-224 operation has used the peripheral (see hash_test.cpp), so interleaved
// HMAC-SHA-224 operations run on hardware only.

TEST_CASE("crypto hmac_sha224 interleaved operations are independent", "[idfxx][crypto][hw]") {
    check_operation_independence(params_of<crypto::mac::hmac_sha224>, hmac_sha224_vectors);
}

TEST_CASE("crypto hmac_sha224 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_operation(params_of<crypto::mac::hmac_sha224>, text("Jefe"), hmac_sha224_chunked);
}

TEST_CASE("crypto hmac_sha224 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_sha224>, hmac_sha224_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_sha224 exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<crypto::mac::hmac_sha224>, hmac_sha224_vectors);
}
#endif

#endif // PSA_WANT_ALG_SHA_224

#ifdef PSA_WANT_ALG_SHA_256

namespace {
// RFC 4231 test cases, cross-checked with Python's hmac module.
const char* const hmac_sha256_tags[6] = {
    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
    "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
    "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
    "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
    "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
    "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
};
const std::vector<kat> hmac_sha256_vectors = rfc4231(hmac_sha256_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_sha256_chunked =
    "89d35b67a34670fbc3c54709d55a56595eb68b3cd80222d16147368327194e5f";
} // namespace

TEST_CASE("crypto hmac_sha256 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::mac::hmac_sha256>(hmac_sha256_vectors);
}

TEST_CASE("crypto hmac_sha256 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::mac::hmac_sha256>(hmac_sha256_vectors);
    check_split_operation(params_of<crypto::mac::hmac_sha256>, hmac_sha256_vectors);
}

TEST_CASE("crypto hmac_sha256 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::mac::hmac_sha256>(hmac_sha256_vectors);
}

TEST_CASE("crypto hmac_sha256 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::mac::hmac_sha256>(hmac_sha256_vectors);
}

TEST_CASE("crypto hmac_sha256 interleaved operations are independent", "[idfxx][crypto]") {
    check_independence<crypto::mac::hmac_sha256>(hmac_sha256_vectors);
    check_operation_independence(params_of<crypto::mac::hmac_sha256>, hmac_sha256_vectors);
}

TEST_CASE("crypto hmac_sha256 generated keys", "[idfxx][crypto]") {
    check_generated_keys<crypto::mac::hmac_sha256>();
}

TEST_CASE("crypto hmac_sha256 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::mac::hmac_sha256>(text("Jefe"), hmac_sha256_chunked);
    check_chunked_operation(params_of<crypto::mac::hmac_sha256>, text("Jefe"), hmac_sha256_chunked);
}

TEST_CASE("crypto hmac_sha256 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_sha256>, hmac_sha256_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_sha256 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::mac::hmac_sha256>(hmac_sha256_vectors);
    check_throwing_operation(params_of<crypto::mac::hmac_sha256>, hmac_sha256_vectors);
}
#endif

#endif // PSA_WANT_ALG_SHA_256

#ifdef PSA_WANT_ALG_SHA_384

namespace {
// RFC 4231 test cases, cross-checked with Python's hmac module.
const char* const hmac_sha384_tags[6] = {
    "afd03944d84895626b0825f4ab46907f15f9dadbe4101ec682aa034c7cebc59cfaea9ea9076ede7f4af152e8b2fa9cb6",
    "af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e8e2240ca5e69e2c78b3239ecfab21649",
    "88062608d3e6ad8a0aa2ace014c8a86f0aa635d947ac9febe83ef4e55966144b2a5ab39dc13814b94e3ab6e101a34f27",
    "3e8a69b7783c25851933ab6290af6ca77a9981480850009cc5577c6e1f573b4e6801dd23c4a7d679ccf8a386c674cffb",
    "4ece084485813e9088d2c63a041bc5b44f9ef1012a2b588f3cd11f05033ac4c60c2ef6ab4030fe8296248df163f44952",
    "6617178e941f020d351e2f254e8fd32c602420feb0b8fb9adccebb82461e99c5a678cc31e799176d3860e6110c46523e",
};
const std::vector<kat> hmac_sha384_vectors = rfc4231(hmac_sha384_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_sha384_chunked =
    "a981d07b9abbcafa8b7d21d0cab44220af05a84ee49c83207fcbdd35aeac32f9"
    "71c8805218018e905445cd12b531474c";
} // namespace

TEST_CASE("crypto hmac_sha384 known-answer vectors", "[idfxx][crypto]") {
    check_tags<crypto::mac::hmac_sha384>(hmac_sha384_vectors);
}

TEST_CASE("crypto hmac_sha384 split updates match one-shot", "[idfxx][crypto]") {
    check_split_operation(params_of<crypto::mac::hmac_sha384>, hmac_sha384_vectors);
}

TEST_CASE("crypto hmac_sha384 interleaved operations are independent", "[idfxx][crypto]") {
    check_operation_independence(params_of<crypto::mac::hmac_sha384>, hmac_sha384_vectors);
}

TEST_CASE("crypto hmac_sha384 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_operation(params_of<crypto::mac::hmac_sha384>, text("Jefe"), hmac_sha384_chunked);
}

TEST_CASE("crypto hmac_sha384 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_sha384>, hmac_sha384_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_sha384 exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<crypto::mac::hmac_sha384>, hmac_sha384_vectors);
}
#endif

#endif // PSA_WANT_ALG_SHA_384

#ifdef PSA_WANT_ALG_SHA_512

namespace {
// RFC 4231 test cases, cross-checked with Python's hmac module.
const char* const hmac_sha512_tags[6] = {
    "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854",
    "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737",
    "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb",
    "b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3dba91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd",
    "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598",
    "e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58",
};
const std::vector<kat> hmac_sha512_vectors = rfc4231(hmac_sha512_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_sha512_chunked =
    "18d49e05a7387edc7cadf65f24f27329558a517110597b259c164b15260ff3f4"
    "2473e99582f3e0dc8d15a1e3707fdb983aefa07bc1169cab581bfc060dce1dc0";
} // namespace

TEST_CASE("crypto hmac_sha512 known-answer vectors", "[idfxx][crypto]") {
    check_tags<crypto::mac::hmac_sha512>(hmac_sha512_vectors);
}

TEST_CASE("crypto hmac_sha512 split updates match one-shot", "[idfxx][crypto]") {
    check_split_operation(params_of<crypto::mac::hmac_sha512>, hmac_sha512_vectors);
}

TEST_CASE("crypto hmac_sha512 interleaved operations are independent", "[idfxx][crypto]") {
    check_operation_independence(params_of<crypto::mac::hmac_sha512>, hmac_sha512_vectors);
}

TEST_CASE("crypto hmac_sha512 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_operation(params_of<crypto::mac::hmac_sha512>, text("Jefe"), hmac_sha512_chunked);
}

TEST_CASE("crypto hmac_sha512 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_sha512>, hmac_sha512_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_sha512 exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<crypto::mac::hmac_sha512>, hmac_sha512_vectors);
}
#endif

#endif // PSA_WANT_ALG_SHA_512

#ifdef PSA_WANT_ALG_SHA3_224

namespace {
// Tags of the RFC 4231 test inputs, generated with Python's hmac module.
const char* const hmac_sha3_224_tags[6] = {
    "3b16546bbc7be2706a031dcafd56373d9884367641d8c59af3c860f7",
    "7fdb8dd88bd2f60d1b798634ad386811c2cfc85bfaf5d52bbace5e66",
    "676cfc7d16153638780390692be142d2df7ce924b909c0c08dbfdc1a",
    "a9d7685a19c4e0dbd9df2556cc8a7d2a7733b67625ce594c78270eeb",
    "b4a1f04c00287a9b7f6075b313d279b833bc8f75124352d05fb9995f",
    "05d8cd6d00faea8d1eb68ade28730bbd3cbab6929f0a086b29cd62a0",
};
const std::vector<kat> hmac_sha3_224_vectors = rfc4231(hmac_sha3_224_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_sha3_224_chunked =
    "53e230512a30cb3d7080814422bfd82e4ad99661564f78e8a9287871";
} // namespace

TEST_CASE("crypto hmac_sha3_224 known-answer vectors", "[idfxx][crypto]") {
    check_tags<crypto::mac::hmac_sha3_224>(hmac_sha3_224_vectors);
}

TEST_CASE("crypto hmac_sha3_224 split updates match one-shot", "[idfxx][crypto]") {
    check_split_operation(params_of<crypto::mac::hmac_sha3_224>, hmac_sha3_224_vectors);
}

TEST_CASE("crypto hmac_sha3_224 interleaved operations are independent", "[idfxx][crypto]") {
    check_operation_independence(params_of<crypto::mac::hmac_sha3_224>, hmac_sha3_224_vectors);
}

TEST_CASE("crypto hmac_sha3_224 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_operation(
        params_of<crypto::mac::hmac_sha3_224>, text("Jefe"), hmac_sha3_224_chunked
    );
}

TEST_CASE("crypto hmac_sha3_224 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_sha3_224>, hmac_sha3_224_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_sha3_224 exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<crypto::mac::hmac_sha3_224>, hmac_sha3_224_vectors);
}
#endif

#endif // PSA_WANT_ALG_SHA3_224

#ifdef PSA_WANT_ALG_SHA3_256

namespace {
// Tags of the RFC 4231 test inputs, generated with Python's hmac module.
const char* const hmac_sha3_256_tags[6] = {
    "ba85192310dffa96e2a3a40e69774351140bb7185e1202cdcc917589f95e16bb",
    "c7d4072e788877ae3596bbb0da73b887c9171f93095b294ae857fbe2645e1ba5",
    "84ec79124a27107865cedd8bd82da9965e5ed8c37b0ac98005a7f39ed58a4207",
    "57366a45e2305321a4bc5aa5fe2ef8a921f6af8273d7fe7be6cfedb3f0aea6d7",
    "ed73a374b96c005235f948032f09674a58c0ce555cfc1f223b02356560312c3b",
    "65c5b06d4c3de32a7aef8763261e49adb6e2293ec8e7c61e8de61701fc63e123",
};
const std::vector<kat> hmac_sha3_256_vectors = rfc4231(hmac_sha3_256_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_sha3_256_chunked =
    "45136283d48d8f475c7eb4f606499f02906d508d09d7d54682cfefc6654fd8ef";
} // namespace

TEST_CASE("crypto hmac_sha3_256 known-answer vectors", "[idfxx][crypto]") {
    check_tags<crypto::mac::hmac_sha3_256>(hmac_sha3_256_vectors);
}

TEST_CASE("crypto hmac_sha3_256 split updates match one-shot", "[idfxx][crypto]") {
    check_split_operation(params_of<crypto::mac::hmac_sha3_256>, hmac_sha3_256_vectors);
}

TEST_CASE("crypto hmac_sha3_256 interleaved operations are independent", "[idfxx][crypto]") {
    check_operation_independence(params_of<crypto::mac::hmac_sha3_256>, hmac_sha3_256_vectors);
}

TEST_CASE("crypto hmac_sha3_256 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_operation(
        params_of<crypto::mac::hmac_sha3_256>, text("Jefe"), hmac_sha3_256_chunked
    );
}

TEST_CASE("crypto hmac_sha3_256 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_sha3_256>, hmac_sha3_256_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_sha3_256 exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<crypto::mac::hmac_sha3_256>, hmac_sha3_256_vectors);
}
#endif

#endif // PSA_WANT_ALG_SHA3_256

#ifdef PSA_WANT_ALG_SHA3_384

namespace {
// Tags of the RFC 4231 test inputs, generated with Python's hmac module.
const char* const hmac_sha3_384_tags[6] = {
    "68d2dcf7fd4ddd0a2240c8a437305f61fb7334cfb5d0226e1bc27dc10a2e723a20d370b47743130e26ac7e3d532886bd",
    "f1101f8cbf9766fd6764d2ed61903f21ca9b18f57cf3e1a23ca13508a93243ce48c045dc007f26a21b3f5e0e9df4c20a",
    "275cd0e661bb8b151c64d288f1f782fb91a8abd56858d72babb2d476f0458373b41b6ab5bf174bec422e53fc3135ac6e",
    "3a5d7a879702c086bc96d1dd8aa15d9c46446b95521311c606fdc4e308f4b984da2d0f9449b3ba8425ec7fb8c31bc136",
    "0fc19513bf6bd878037016706a0e57bc528139836b9a42c3d419e498e0e1fb9616fd669138d33a1105e07c72b6953bcc",
    "026fdf6b50741e373899c9f7d5406d4eb09fc6665636fc1a530029ddf5cf3ca5a900edce01f5f61e2f408cdf2fd3e7e8",
};
const std::vector<kat> hmac_sha3_384_vectors = rfc4231(hmac_sha3_384_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_sha3_384_chunked =
    "f82a4360765a84892bd9669e7879428ee4dddaaa8737181cca978b27adff86cb"
    "5cc5c2ac9877e362ab5cfce6d4bb4cf4";
} // namespace

TEST_CASE("crypto hmac_sha3_384 known-answer vectors", "[idfxx][crypto]") {
    check_tags<crypto::mac::hmac_sha3_384>(hmac_sha3_384_vectors);
}

TEST_CASE("crypto hmac_sha3_384 split updates match one-shot", "[idfxx][crypto]") {
    check_split_operation(params_of<crypto::mac::hmac_sha3_384>, hmac_sha3_384_vectors);
}

TEST_CASE("crypto hmac_sha3_384 interleaved operations are independent", "[idfxx][crypto]") {
    check_operation_independence(params_of<crypto::mac::hmac_sha3_384>, hmac_sha3_384_vectors);
}

TEST_CASE("crypto hmac_sha3_384 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_operation(
        params_of<crypto::mac::hmac_sha3_384>, text("Jefe"), hmac_sha3_384_chunked
    );
}

TEST_CASE("crypto hmac_sha3_384 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_sha3_384>, hmac_sha3_384_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_sha3_384 exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<crypto::mac::hmac_sha3_384>, hmac_sha3_384_vectors);
}
#endif

#endif // PSA_WANT_ALG_SHA3_384

#ifdef PSA_WANT_ALG_SHA3_512

namespace {
// Tags of the RFC 4231 test inputs, generated with Python's hmac module.
const char* const hmac_sha3_512_tags[6] = {
    "eb3fbd4b2eaab8f5c504bd3a41465aacec15770a7cabac531e482f860b5ec7ba47ccb2c6f2afce8f88d22b6dc61380f23a668fd3888bb80537c0a0b86407689e",
    "5a4bfeab6166427c7a3647b747292b8384537cdb89afb3bf5665e4c5e709350b287baec921fd7ca0ee7a0c31d022a95e1fc92ba9d77df883960275beb4e62024",
    "309e99f9ec075ec6c6d475eda1180687fcf1531195802a99b5677449a8625182851cb332afb6a89c411325fbcbcd42afcb7b6e5aab7ea42c660f97fd8584bf03",
    "b27eab1d6e8d87461c29f7f5739dd58e98aa35f8e823ad38c5492a2088fa0281993bbfff9a0e9c6bf121ae9ec9bb09d84a5ebac817182ea974673fb133ca0d1d",
    "00f751a9e50695b090ed6911a4b65524951cdc15a73a5d58bb55215ea2cd839ac79d2b44a39bafab27e83fde9e11f6340b11d991b1b91bf2eee7fc872426c3a4",
    "38a456a004bd10d32c9ab8336684112862c3db61adcca31829355eaf46fd5c73d06a1f0d13fec9a652fb3811b577b1b1d1b9789f97ae5b83c6f44dfcf1d67eba",
};
const std::vector<kat> hmac_sha3_512_vectors = rfc4231(hmac_sha3_512_tags);
// The tag of patterned_message(3 * 4096 + 7) under the key "Jefe", from Python's hmac module.
constexpr std::string_view hmac_sha3_512_chunked =
    "f35f02b9f4d1ddc8a1e334b8256320c7fbbcb0960a77beb98bf9dd72d7b6595d"
    "af9a8fb1403c1883e84fd00f760f7a89a991e5bfd07d4f998532632028c75370";
} // namespace

TEST_CASE("crypto hmac_sha3_512 known-answer vectors", "[idfxx][crypto]") {
    check_tags<crypto::mac::hmac_sha3_512>(hmac_sha3_512_vectors);
}

TEST_CASE("crypto hmac_sha3_512 split updates match one-shot", "[idfxx][crypto]") {
    check_split_operation(params_of<crypto::mac::hmac_sha3_512>, hmac_sha3_512_vectors);
}

TEST_CASE("crypto hmac_sha3_512 interleaved operations are independent", "[idfxx][crypto]") {
    check_operation_independence(params_of<crypto::mac::hmac_sha3_512>, hmac_sha3_512_vectors);
}

TEST_CASE("crypto hmac_sha3_512 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_operation(
        params_of<crypto::mac::hmac_sha3_512>, text("Jefe"), hmac_sha3_512_chunked
    );
}

TEST_CASE("crypto hmac_sha3_512 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::hmac_sha3_512>, hmac_sha3_512_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hmac_sha3_512 exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<crypto::mac::hmac_sha3_512>, hmac_sha3_512_vectors);
}
#endif

#endif // PSA_WANT_ALG_SHA3_512

#if defined(PSA_WANT_ALG_CMAC) && defined(PSA_WANT_KEY_TYPE_AES)

namespace {
// RFC 4493 (AES-128) and NIST SP 800-38B (AES-192, AES-256) examples, cross-checked with OpenSSL
// and Python's cryptography package.
const char* const cmac_aes128_tags[4] = {
    "bb1d6929e95937287fa37d129b756746",
    "070a16b46b4d4144f79bdd9dd04a287c",
    "dfa66747de9ae63030ca32611497c827",
    "51f0bebf7e3b9d92fc49741779363cfe",
};
const char* const cmac_aes192_tags[4] = {
    "d17ddf46adaacde531cac483de7a9367",
    "9e99a7bf31e710900662f65e617c5184",
    "8a1de5be2eb31aad089a82e6ee908b0e",
    "a1d5df0eed790f794d77589659f39a11",
};
const char* const cmac_aes256_tags[4] = {
    "028962f61b7bf89efc6b551f4667d983",
    "28a7023f452e8f82bd4bf28d8c37c35c",
    "aaf3d8f1de5640c232f5b169b9c911e6",
    "e1992190549f6ed5696a2c056c315410",
};
constexpr std::string_view cmac_aes128_key = "2b7e151628aed2a6abf7158809cf4f3c";
constexpr std::string_view cmac_aes192_key = "8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b";
constexpr std::string_view cmac_aes256_key =
    "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4";

std::vector<kat> cmac_vectors() {
    auto v = cmac_examples(cmac_aes128_key, cmac_aes128_tags);
    auto v256 = cmac_examples(cmac_aes256_key, cmac_aes256_tags);
    v.insert(v.end(), v256.begin(), v256.end());
    return v;
}
const std::vector<kat> cmac_aes128_vectors = cmac_examples(cmac_aes128_key, cmac_aes128_tags);
const std::vector<kat> cmac_aes256_vectors = cmac_examples(cmac_aes256_key, cmac_aes256_tags);
// The tags of patterned_message(3 * 4096 + 7) under the AES-128 and AES-256 keys above, computed
// with Python's cryptography package.
constexpr std::string_view cmac_aes128_chunked = "3798b7216d59313564fc92a0e54c66e9";
constexpr std::string_view cmac_aes256_chunked = "878ddeb390f4852f20742c52c353cb43";
} // namespace

TEST_CASE("crypto cmac known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::mac::cmac>(cmac_vectors());
}

TEST_CASE("crypto cmac split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::mac::cmac>(cmac_aes128_vectors);
    check_split_updates<crypto::mac::cmac>(cmac_aes256_vectors);
    check_split_operation(params_of<crypto::mac::cmac>, cmac_aes128_vectors);
    check_split_operation(params_of<crypto::mac::cmac>, cmac_aes256_vectors);
}

TEST_CASE("crypto cmac compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::mac::cmac>(cmac_aes128_vectors);
    check_compare_and_verify<crypto::mac::cmac>(cmac_aes256_vectors);
}

TEST_CASE("crypto cmac reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::mac::cmac>(cmac_aes128_vectors);
}

TEST_CASE("crypto cmac interleaved operations are independent", "[idfxx][crypto]") {
    check_independence<crypto::mac::cmac>(cmac_aes128_vectors);
    check_independence<crypto::mac::cmac>(cmac_aes256_vectors);
    check_operation_independence(params_of<crypto::mac::cmac>, cmac_aes128_vectors);
    check_operation_independence(params_of<crypto::mac::cmac>, cmac_aes256_vectors);
}

TEST_CASE("crypto cmac generated keys", "[idfxx][crypto]") {
    check_generated_keys<crypto::mac::cmac>();
}

TEST_CASE("crypto cmac rejects keys of the wrong size", "[idfxx][crypto]") {
    for (size_t len : {1, 8, 15, 17, 20, 31, 33, 64}) {
        auto material = repeat(0x2b, len);
        fails_with(crypto::errc::invalid_argument, crypto::mac::cmac::try_import_key(material));
    }
    fails_with(crypto::errc::invalid_argument, crypto::mac::cmac::try_import_key(byte_view{}));
}

// AES-192 keys import on every target, but only the esp32 and esp32s2 AES engines take 192-bit
// keys. Elsewhere, with hardware AES, ESP-IDF 5.x fails with not_supported; on ESP-IDF 6.x the
// outcome is not established, so either an authenticator that computes correct tags or
// not_supported is accepted. The authenticator is created either way: the crypto library reports
// the key size only when a message starts.
TEST_CASE("crypto cmac with an AES-192 key", "[idfxx][crypto]") {
    auto vectors = cmac_examples(cmac_aes192_key, cmac_aes192_tags);
    auto k = ok(crypto::mac::cmac::try_import_key(vectors[0].key));
    TEST_ASSERT_EQUAL_INT32(192, static_cast<int32_t>(k.bits()));
    auto m = ok(crypto::mac::cmac::make(k));
    auto op = ok(crypto::mac::operation::make(k));
    auto tag = m.try_update(vectors[3].data).and_then([&] { return m.try_finish(); });
    if (!tag) {
        std::printf("AES-192 CMAC: %s\n", tag.error().message().c_str());
        fails_with(crypto::errc::not_supported, tag);
        // Each message reports it, through each API
        fails_with(crypto::errc::not_supported, m.try_finish());
        fails_with(crypto::errc::not_supported, m.try_verify(from_hex(vectors[3].tag)));
        fails_with(crypto::errc::not_supported, crypto::mac::cmac::try_compute(k, vectors[0].data));
        fails_with(
            crypto::errc::not_supported,
            crypto::mac::cmac::try_compare(k, vectors[0].data, from_hex(vectors[0].tag))
        );
        fails_with(crypto::errc::not_supported, op.try_update(vectors[0].data));
        // A tag of the wrong length doesn't match, without starting a message
        TEST_ASSERT_FALSE(ok(m.try_verify(byte_view{})));
        TEST_ASSERT_FALSE(ok(op.try_verify(byte_view{})));
        TEST_ASSERT_FALSE(ok(crypto::mac::cmac::try_compare(k, vectors[0].data, byte_view{})));
        return;
    }
    std::printf("AES-192 CMAC: supported\n");
    TEST_ASSERT_TAG(vectors[3].tag, *tag);
    check_known_answers<crypto::mac::cmac>(vectors);
}

TEST_CASE("crypto cmac input spanning several chunks", "[idfxx][crypto]") {
    constexpr auto p = params_of<crypto::mac::cmac>;
    check_chunked_input<crypto::mac::cmac>(from_hex(cmac_aes128_key), cmac_aes128_chunked);
    check_chunked_input<crypto::mac::cmac>(from_hex(cmac_aes256_key), cmac_aes256_chunked);
    check_chunked_operation(p, from_hex(cmac_aes128_key), cmac_aes128_chunked);
    check_chunked_operation(p, from_hex(cmac_aes256_key), cmac_aes256_chunked);
}

TEST_CASE("crypto cmac mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<crypto::mac::cmac>, cmac_vectors());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto cmac exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::mac::cmac>(cmac_aes128_vectors);
    check_throwing_operation(params_of<crypto::mac::cmac>, cmac_aes128_vectors);
    check_throwing_operation(params_of<crypto::mac::cmac>, cmac_aes256_vectors);
}
#endif

#endif // PSA_WANT_ALG_CMAC && PSA_WANT_KEY_TYPE_AES

// Key policies

#ifdef PSA_WANT_ALG_SHA_256

TEST_CASE("crypto hmac_sha256 key usage decides what an authenticator can do", "[idfxx][crypto]") {
    check_policy<crypto::mac::hmac_sha256>(repeat(0x0b, 32));
    check_operation_policy(params_of<crypto::mac::hmac_sha256>, repeat(0x0b, 32));
}

#endif // PSA_WANT_ALG_SHA_256

#if defined(PSA_WANT_ALG_CMAC) && defined(PSA_WANT_KEY_TYPE_AES)

TEST_CASE("crypto cmac key usage decides what an authenticator can do", "[idfxx][crypto]") {
    check_policy<crypto::mac::cmac>(from_hex(cmac_aes128_key));
    check_operation_policy(params_of<crypto::mac::cmac>, from_hex(cmac_aes128_key));
}

#endif // PSA_WANT_ALG_CMAC && PSA_WANT_KEY_TYPE_AES

#if defined(PSA_WANT_ALG_SHA_256) && defined(PSA_WANT_ALG_SHA_1)

TEST_CASE("crypto authenticator refuses a key permitted for another algorithm", "[idfxx][crypto]") {
    auto material = repeat(0x0b, 32);
    std::string_view msg = "message";
    std::array<std::byte, crypto::mac::hmac_sha1::tag_size> tag{};

    auto k = ok(crypto::mac::hmac_sha256::try_import_key(material));
    fails_with(crypto::errc::not_permitted, crypto::mac::hmac_sha1::try_compute(k, msg));
    fails_with(crypto::errc::not_permitted, crypto::mac::hmac_sha1::try_compare(k, msg, tag));
    rejects_key<crypto::mac::hmac_sha1>(crypto::errc::not_permitted, k);

    // A key permitted no algorithm
    auto none = ok(crypto::key::try_import(
        {.type = crypto::key_type::hmac, .usage = sign_verify}, material
    ));
    fails_with(crypto::errc::not_permitted, crypto::mac::hmac_sha256::try_compute(none, msg));
    rejects_key<crypto::mac::hmac_sha256>(crypto::errc::not_permitted, none);

    // The key still works with its own algorithm
    TEST_ASSERT_TRUE(ok(crypto::mac::hmac_sha256::try_compare(
        k, msg, ok(crypto::mac::hmac_sha256::try_compute(k, msg))
    )));
}

#endif // PSA_WANT_ALG_SHA_256 && PSA_WANT_ALG_SHA_1

#if defined(PSA_WANT_ALG_SHA_256) && defined(PSA_WANT_KEY_TYPE_AES)

// A key of the wrong type for a MAC algorithm can't be created, so no authenticator or
// mac::operation ever sees one.
TEST_CASE("crypto mac keys of the wrong type can't be created", "[idfxx][crypto]") {
    auto material = repeat(0x2b, 16);

    // An AES key permitted HMAC-SHA-256, at full length and truncated
    for (crypto::key_algorithm alg :
         {crypto::key_algorithm{crypto::mac::algorithm::hmac_sha256},
          key_algorithm_of<hmac_sha256_128>}) {
        fails_with(
            crypto::errc::invalid_argument,
            crypto::key::try_import(
                {.type = crypto::key_type::aes, .usage = sign_verify, .algorithm = alg}, material
            )
        );
        fails_with(
            crypto::errc::invalid_argument,
            crypto::key::try_generate(
                {.type = crypto::key_type::aes, .usage = sign_verify, .algorithm = alg}, 128
            )
        );
    }

#ifdef PSA_WANT_ALG_CMAC
    // An HMAC key permitted AES-CMAC, at full length and truncated
    for (crypto::key_algorithm alg :
         {crypto::key_algorithm{crypto::mac::algorithm::cmac}, key_algorithm_of<cmac_32>}) {
        fails_with(
            crypto::errc::invalid_argument,
            crypto::key::try_import(
                {.type = crypto::key_type::hmac, .usage = sign_verify, .algorithm = alg}, material
            )
        );
        fails_with(
            crypto::errc::invalid_argument,
            crypto::key::try_generate(
                {.type = crypto::key_type::hmac, .usage = sign_verify, .algorithm = alg}, 128
            )
        );
    }
#endif
}

// mac::operation takes its algorithm from the key: an unusable key is an error from every
// operation, never a mismatch, and is reported in preference to a buffer that is too small.
TEST_CASE("crypto mac::operation refuses keys it cannot use", "[idfxx][crypto]") {
    using crypto::mac::operation;
    std::string_view msg = "message";
    auto material = repeat(0x2b, 16);
    std::array<std::byte, 80> buf; // larger than any tag
    auto small = std::span{buf}.first(1);
    auto check = [&](crypto::errc expected, const crypto::key& k, size_t tag_size) {
        std::vector<std::byte> tag(tag_size);
        fails_with(expected, operation::try_compute(k, msg, buf));
        fails_with(expected, operation::try_compute(k, msg, small));
        fails_with(expected, operation::try_compute(k, msg, byte_span{}));
        fails_with(expected, operation::try_compare(k, msg, tag));
        fails_with(expected, operation::try_compare(k, msg, byte_view{}));
        rejects_key<operation>(expected, k);
    };

    // A key permitted no algorithm
    auto none = ok(crypto::key::try_import(
        {.type = crypto::key_type::hmac, .usage = sign_verify}, material
    ));
    check(crypto::errc::not_permitted, none, 32);

    // A key permitted HMAC with a hash the crypto library doesn't know, so of no known tag length
    const auto unknown_hmac = static_cast<crypto::mac::algorithm>(PSA_ALG_HMAC(0x020000fe));
    TEST_ASSERT_EQUAL(0, crypto::mac::tag_size(unknown_hmac));
    auto unknown = ok(crypto::key::try_import(
        {.type = crypto::key_type::hmac, .usage = sign_verify, .algorithm = unknown_hmac},
        material
    ));
    check(crypto::errc::not_permitted, unknown, 32);

    // A key permitted HMAC-SHA-256 that permits neither computing nor checking tags
    auto neither = ok(crypto::key::try_import(
        {.type = crypto::key_type::hmac,
         .usage = crypto::key_usage::export_key,
         .algorithm = crypto::mac::algorithm::hmac_sha256},
        material
    ));
    check(crypto::errc::not_permitted, neither, 32);

    // A key that suits its algorithm works
    auto good = ok(crypto::mac::hmac_sha256::try_import_key(material));
    auto tag = ok(operation::try_compute(good, msg, buf));
    TEST_ASSERT_TRUE(ok(operation::try_compare(good, msg, tag)));
    auto op = ok(operation::make(good));
    TEST_ASSERT_TRUE(op.algorithm() == crypto::mac::algorithm::hmac_sha256);
    TEST_ASSERT_EQUAL(32, op.tag_size());
}

#endif // PSA_WANT_ALG_SHA_256 && PSA_WANT_KEY_TYPE_AES

#ifndef PSA_WANT_ALG_RIPEMD160

// HMAC with a hash the crypto library knows but doesn't have enabled: a key for it can be created,
// and so can an operation (which sets nothing up), but each message that starts reports it. A tag
// of the wrong length doesn't match, without starting a message.
TEST_CASE("crypto mac::operation reports an unsupported hash on first use", "[idfxx][crypto]") {
    using crypto::mac::operation;
    const auto alg = static_cast<crypto::mac::algorithm>(PSA_ALG_HMAC(PSA_ALG_RIPEMD160));
    TEST_ASSERT_EQUAL(20, crypto::mac::tag_size(alg));
    auto k = ok(crypto::key::try_import(
        {.type = crypto::key_type::hmac, .usage = sign_verify, .algorithm = alg}, repeat(0x0b, 20)
    ));
    std::string_view msg = "message";
    std::array<std::byte, 20> tag{};

    auto op = ok(operation::make(k));
    TEST_ASSERT_EQUAL(20, op.tag_size());
    TEST_ASSERT_FALSE(ok(op.try_verify(byte_view{})));
    TEST_ASSERT_FALSE(ok(op.try_verify(std::span{tag}.first(19))));
    TEST_ASSERT_FALSE(ok(operation::try_compare(k, msg, byte_view{})));

    fails_with(crypto::errc::not_supported, op.try_update(msg));
    fails_with(crypto::errc::not_supported, op.try_finish(tag));
    fails_with(crypto::errc::not_supported, op.try_verify(tag));
    fails_with(crypto::errc::not_supported, operation::try_compute(k, msg, tag));
    fails_with(crypto::errc::not_supported, operation::try_compare(k, msg, tag));

    // A key that may only check tags gives an operation set up to check them, with the same outcome
    auto checker_key = ok(crypto::key::try_import(
        {.type = crypto::key_type::hmac,
         .usage = crypto::key_usage::verify_message,
         .algorithm = alg},
        repeat(0x0b, 20)
    ));
    auto checker = ok(operation::make(checker_key));
    TEST_ASSERT_FALSE(ok(checker.try_verify(byte_view{})));
    fails_with(crypto::errc::not_supported, checker.try_update(msg));
    fails_with(crypto::errc::not_supported, checker.try_verify(tag));
}

#endif // !PSA_WANT_ALG_RIPEMD160

// Truncated tags

namespace {
// RFC 4231 test case 5 and RFC 2202 test case 5 check truncated tags, of HMAC-SHA-256 (128 bits)
// and HMAC-SHA-1 (96 bits), of one message.
kat truncation_case(const char* tag) {
    return {repeat(0x0c, 20), text("Test With Truncation"), tag};
}
} // namespace

#ifdef PSA_WANT_ALG_SHA_256

namespace {
std::vector<kat> hmac_sha256_128_vectors() {
    auto v = truncated(hmac_sha256_vectors, 16);
    v.push_back(truncation_case("a3b6167473100ee06e0c796c2955552b"));
    return v;
}
} // namespace

TEST_CASE("crypto truncated hmac_sha256 (16 bytes) known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<hmac_sha256_128>(hmac_sha256_128_vectors());
    check_full_length_tags<hmac_sha256_128>(hmac_sha256_vectors);
}

TEST_CASE("crypto truncated hmac_sha256 (16 bytes) split updates", "[idfxx][crypto]") {
    check_split_updates<hmac_sha256_128>(hmac_sha256_128_vectors());
    check_split_operation(params_of<hmac_sha256_128>, hmac_sha256_128_vectors());
}

TEST_CASE("crypto truncated hmac_sha256 (16 bytes) compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<hmac_sha256_128>(hmac_sha256_128_vectors());
}

TEST_CASE("crypto truncated hmac_sha256 (16 bytes) generated keys", "[idfxx][crypto]") {
    check_generated_keys<hmac_sha256_128>();
}

TEST_CASE("crypto truncated hmac_sha256 (16 bytes) key usage", "[idfxx][crypto]") {
    check_policy<hmac_sha256_128>(repeat(0x0b, 32));
    check_operation_policy(params_of<hmac_sha256_128>, repeat(0x0b, 32));
}

TEST_CASE("crypto truncated hmac_sha256 (16 bytes) refuses other tag lengths", "[idfxx][crypto]") {
    check_tag_length_policy<hmac_sha256_128>(repeat(0x0b, 32));
}

TEST_CASE("crypto truncated hmac_sha256 (4 bytes) known-answer vectors", "[idfxx][crypto]") {
    check_full_length_tags<hmac_sha256_32>(hmac_sha256_vectors);
    check_tag_length_policy<hmac_sha256_32>(repeat(0x0b, 32));
}

TEST_CASE("crypto truncated hmac_sha256 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<hmac_sha256_128>(text("Jefe"), hmac_sha256_chunked);
    check_chunked_operation(params_of<hmac_sha256_128>, text("Jefe"), hmac_sha256_chunked);
    check_chunked_operation(params_of<hmac_sha256_32>, text("Jefe"), hmac_sha256_chunked);
}

TEST_CASE("crypto truncated hmac_sha256 mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<hmac_sha256_128>, hmac_sha256_128_vectors());
    check_operation(params_of<hmac_sha256_32>, truncated(hmac_sha256_vectors, 4));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto truncated hmac_sha256 (16 bytes) exception-based API", "[idfxx][crypto]") {
    check_throwing_api<hmac_sha256_128>(hmac_sha256_128_vectors());
    check_throwing_operation(params_of<hmac_sha256_128>, hmac_sha256_128_vectors());
    check_throwing_operation(params_of<hmac_sha256_32>, truncated(hmac_sha256_vectors, 4));
}
#endif

#endif // PSA_WANT_ALG_SHA_256

#ifdef PSA_WANT_ALG_SHA_1

namespace {
std::vector<kat> hmac_sha1_96_vectors() {
    auto v = truncated(hmac_sha1_vectors, 12);
    v.push_back(truncation_case("4c1a03424b55e07fe7f27be1"));
    return v;
}
} // namespace

TEST_CASE("crypto truncated hmac_sha1 (12 bytes) known-answer vectors", "[idfxx][crypto]") {
    check_full_length_tags<hmac_sha1_96>(hmac_sha1_vectors);
    check_full_length_tags<hmac_sha1_96>(
        {truncation_case("4c1a03424b55e07fe7f27be1d58bb9324a9a5a04")}
    );
}

TEST_CASE("crypto truncated hmac_sha1 (12 bytes) refuses other tag lengths", "[idfxx][crypto]") {
    check_tag_length_policy<hmac_sha1_96>(repeat(0x0b, 20));
}

TEST_CASE("crypto truncated hmac_sha1 (12 bytes) input spanning chunks", "[idfxx][crypto]") {
    check_chunked_operation(params_of<hmac_sha1_96>, text("Jefe"), hmac_sha1_chunked);
}

TEST_CASE("crypto truncated hmac_sha1 (12 bytes) mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<hmac_sha1_96>, hmac_sha1_96_vectors());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto truncated hmac_sha1 (12 bytes) exception-based API", "[idfxx][crypto]") {
    check_throwing_operation(params_of<hmac_sha1_96>, hmac_sha1_96_vectors());
}
#endif

#endif // PSA_WANT_ALG_SHA_1

#if defined(PSA_WANT_ALG_CMAC) && defined(PSA_WANT_KEY_TYPE_AES)

TEST_CASE("crypto truncated cmac (4 bytes) known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<cmac_32>(truncated(cmac_vectors(), 4));
    check_full_length_tags<cmac_32>(cmac_vectors());
}

TEST_CASE("crypto truncated cmac (4 bytes) split updates", "[idfxx][crypto]") {
    check_split_updates<cmac_32>(truncated(cmac_aes128_vectors, 4));
    check_split_operation(params_of<cmac_32>, truncated(cmac_aes128_vectors, 4));
}

TEST_CASE("crypto truncated cmac (4 bytes) compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<cmac_32>(truncated(cmac_aes128_vectors, 4));
    check_compare_and_verify<cmac_32>(truncated(cmac_aes256_vectors, 4));
}

TEST_CASE("crypto truncated cmac (4 bytes) generated keys", "[idfxx][crypto]") {
    check_generated_keys<cmac_32>();
}

TEST_CASE("crypto truncated cmac (4 bytes) key usage", "[idfxx][crypto]") {
    check_policy<cmac_32>(from_hex(cmac_aes128_key));
    check_operation_policy(params_of<cmac_32>, from_hex(cmac_aes128_key));
}

TEST_CASE("crypto truncated cmac (4 bytes) refuses other tag lengths", "[idfxx][crypto]") {
    check_tag_length_policy<cmac_32>(from_hex(cmac_aes128_key));
}

TEST_CASE("crypto truncated cmac (4 bytes) input spanning several chunks", "[idfxx][crypto]") {
    constexpr auto p = params_of<cmac_32>;
    check_chunked_input<cmac_32>(from_hex(cmac_aes128_key), cmac_aes128_chunked);
    check_chunked_input<cmac_32>(from_hex(cmac_aes256_key), cmac_aes256_chunked);
    check_chunked_operation(p, from_hex(cmac_aes128_key), cmac_aes128_chunked);
    check_chunked_operation(p, from_hex(cmac_aes256_key), cmac_aes256_chunked);
}

TEST_CASE("crypto truncated cmac (4 bytes) mac::operation", "[idfxx][crypto]") {
    check_operation(params_of<cmac_32>, truncated(cmac_vectors(), 4));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto truncated cmac (4 bytes) exception-based API", "[idfxx][crypto]") {
    check_throwing_api<cmac_32>(truncated(cmac_aes128_vectors, 4));
    check_throwing_operation(params_of<cmac_32>, truncated(cmac_aes128_vectors, 4));
}
#endif

#endif // PSA_WANT_ALG_CMAC && PSA_WANT_KEY_TYPE_AES
