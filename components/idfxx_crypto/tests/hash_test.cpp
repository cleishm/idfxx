// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx crypto hashing
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/crypto/hash"
#include "idfxx/hex"
#include "unity.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace idfxx;

namespace {

// Expected digests (lowercase hex) of the test messages, generated with Python's hashlib. The "abc"
// digests of every algorithm, and the 448-bit message's SHA-1, SHA-256 and SHA3-256 digests, were
// cross-checked against published vectors (RFC 1321, FIPS 180-4 and FIPS 202 examples).
struct vectors {
    const char* empty;   // ""
    const char* abc;     // "abc"
    const char* m448;    // msg_448
    const char* multi;   // patterned_message(1000)
    const char* chunked; // patterned_message(3 * 4096 + 7)
};

constexpr std::string_view msg_abc = "abc";
constexpr std::string_view msg_448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

// n bytes: 0, 1, ..., 255, 0, 1, ...
std::vector<uint8_t> patterned_message(std::size_t n) {
    std::vector<uint8_t> msg(n);
    for (size_t i = 0; i < msg.size(); ++i) {
        msg[i] = static_cast<uint8_t>(i & 0xff);
    }
    return msg;
}

// "a" repeated n times
std::string msg_all_a(size_t n) {
    return std::string(n, 'a');
}

std::span<const uint8_t> u8_of(std::string_view s) {
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

std::span<const std::byte> bytes_of(std::string_view s) {
    return std::as_bytes(std::span<const char>{s.data(), s.size()});
}

std::vector<uint8_t> from_hex(std::string_view hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return out;
}

std::vector<std::byte> bytes_from_hex(std::string_view hex) {
    auto u8 = from_hex(hex);
    std::vector<std::byte> out(u8.size());
    std::memcpy(out.data(), u8.data(), u8.size());
    return out;
}

template<typename T>
T ok(result<T> r) {
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), r ? "" : r.error().message().c_str());
    return std::move(*r);
}

void ok(result<void> r) {
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), r ? "" : r.error().message().c_str());
}

#define TEST_ASSERT_DIGEST(expected_hex, digest) \
    TEST_ASSERT_EQUAL_STRING((expected_hex), idfxx::to_string(idfxx::hex(digest)).c_str())

template<typename H>
void check_known_answers(const vectors& v) {
    auto msg = patterned_message(1000); // several blocks of every algorithm
    auto msg_bytes = std::as_bytes(std::span<const uint8_t>{msg});

    // One-shot, each overload and byte_view source
    TEST_ASSERT_DIGEST(v.empty, ok(H::try_compute(std::string_view{})));
    TEST_ASSERT_DIGEST(v.empty, ok(H::try_compute(std::span<const uint8_t>{})));
    TEST_ASSERT_DIGEST(v.empty, ok(H::try_compute(std::span<const std::byte>{})));
    TEST_ASSERT_DIGEST(v.empty, ok(H::try_compute(idfxx::byte_view{})));
    TEST_ASSERT_DIGEST(v.multi, ok(H::try_compute({msg.data(), msg.size()})));
    TEST_ASSERT_DIGEST(v.abc, ok(H::try_compute(msg_abc)));
    TEST_ASSERT_DIGEST(v.abc, ok(H::try_compute(u8_of(msg_abc))));
    TEST_ASSERT_DIGEST(v.abc, ok(H::try_compute(bytes_of(msg_abc))));
    TEST_ASSERT_DIGEST(v.m448, ok(H::try_compute(msg_448)));
    TEST_ASSERT_DIGEST(v.multi, ok(H::try_compute(msg)));
    TEST_ASSERT_DIGEST(v.multi, ok(H::try_compute(msg_bytes)));

    // Incremental, one update per message, each overload
    H h;
    TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_DIGEST(v.abc, ok(h.try_finish()));
    ok(h.try_update(u8_of(msg_448)));
    TEST_ASSERT_DIGEST(v.m448, ok(h.try_finish()));
    ok(h.try_update(msg_bytes));
    TEST_ASSERT_DIGEST(v.multi, ok(h.try_finish()));
    ok(h.try_update(msg));
    TEST_ASSERT_DIGEST(v.multi, ok(h.try_finish()));

    // Empty updates (including null spans) don't change the message
    ok(h.try_update(std::span<const uint8_t>{}));
    ok(h.try_update(std::span<const std::byte>{}));
    TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));
    ok(h.try_update(std::string_view{}));
    ok(h.try_update(msg_abc));
    ok(h.try_update(std::span<const std::byte>{}));
    TEST_ASSERT_DIGEST(v.abc, ok(h.try_finish()));

    // Reuse after finish: finishing again digests the empty message
    TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));

    // A digest can itself be hashed (digest_type is a span of std::byte)
    auto d = ok(H::try_compute(msg_abc));
    auto dd = ok(H::try_compute(d));
    TEST_ASSERT_DIGEST(
        idfxx::to_string(idfxx::hex(dd)).c_str(), ok(H::try_compute(bytes_from_hex(v.abc)))
    );
}

template<typename H>
void check_split_updates(const vectors& v) {
    auto msg = patterned_message(1000); // several blocks of every algorithm
    std::span<const uint8_t> all{msg};
    H h;

    // One byte at a time
    for (size_t i = 0; i < all.size(); ++i) {
        ok(h.try_update(all.subspan(i, 1)));
    }
    TEST_ASSERT_DIGEST(v.multi, ok(h.try_finish()));

    // Chunks that end just before, on and just after block boundaries
    constexpr size_t b = H::block_size;
    const size_t sizes[] = {1, b - 2, 3, b, b + 1, 7, 2 * b - 1};
    size_t pos = 0;
    for (size_t i = 0; pos < all.size(); ++i) {
        size_t n = std::min(sizes[i % std::size(sizes)], all.size() - pos);
        ok(h.try_update(std::as_bytes(all.subspan(pos, n))));
        pos += n;
    }
    TEST_ASSERT_DIGEST(v.multi, ok(h.try_finish()));

    // Split exactly on a block boundary
    ok(h.try_update(all.first(b)));
    ok(h.try_update(all.subspan(b)));
    TEST_ASSERT_DIGEST(v.multi, ok(h.try_finish()));

    // Text split mid-message
    ok(h.try_update(msg_448.substr(0, 5)));
    ok(h.try_update(msg_448.substr(5)));
    TEST_ASSERT_DIGEST(v.m448, ok(h.try_finish()));
}

template<typename H>
void check_compare_and_verify(const vectors& v) {
    auto good = from_hex(v.abc);
    TEST_ASSERT_EQUAL(H::digest_size, good.size());
    auto good_bytes = bytes_from_hex(v.abc);
    auto flipped = good;
    flipped[flipped.size() / 2] ^= 0x01;
    auto flipped_bytes = good_bytes;
    flipped_bytes[0] ^= std::byte{0x80};
    auto too_short = std::vector<uint8_t>(good.begin(), good.end() - 1);
    auto too_long = good;
    too_long.push_back(0);
    auto too_long_bytes = good_bytes;
    too_long_bytes.push_back(std::byte{0});

    // Expected digests of each supported form resolve to an overload unambiguously
    typename H::digest_type digest = ok(H::try_compute(msg_abc));
    std::array<uint8_t, H::digest_size> u8_array;
    std::memcpy(u8_array.data(), good.data(), good.size());
    uint8_t u8_c_array[H::digest_size];
    std::memcpy(u8_c_array, good.data(), good.size());

    // One-shot: every data form with every expected form
    TEST_ASSERT_TRUE(ok(H::try_compare(msg_abc, good)));
    TEST_ASSERT_TRUE(ok(H::try_compare(msg_abc, good_bytes)));
    TEST_ASSERT_TRUE(ok(H::try_compare(u8_of(msg_abc), good)));
    TEST_ASSERT_TRUE(ok(H::try_compare(u8_of(msg_abc), good_bytes)));
    TEST_ASSERT_TRUE(ok(H::try_compare(bytes_of(msg_abc), good)));
    TEST_ASSERT_TRUE(ok(H::try_compare(bytes_of(msg_abc), good_bytes)));
    TEST_ASSERT_TRUE(ok(H::try_compare(msg_abc, digest)));
    TEST_ASSERT_TRUE(ok(H::try_compare(msg_abc, u8_array)));
    TEST_ASSERT_TRUE(ok(H::try_compare(msg_abc, u8_c_array)));
    TEST_ASSERT_TRUE(ok(H::try_compare(std::string_view{}, from_hex(v.empty))));

    TEST_ASSERT_FALSE(ok(H::try_compare(msg_abc, flipped)));
    TEST_ASSERT_FALSE(ok(H::try_compare(bytes_of(msg_abc), flipped_bytes)));
    TEST_ASSERT_FALSE(ok(H::try_compare(u8_of(msg_abc), flipped)));
    TEST_ASSERT_FALSE(ok(H::try_compare(msg_abc, too_short)));
    TEST_ASSERT_FALSE(ok(H::try_compare(msg_abc, too_long)));
    TEST_ASSERT_FALSE(ok(H::try_compare(msg_abc, too_long_bytes)));
    TEST_ASSERT_FALSE(ok(H::try_compare(msg_abc, std::span<const uint8_t>{})));
    TEST_ASSERT_FALSE(ok(H::try_compare(msg_abc, std::span<const std::byte>{})));
    TEST_ASSERT_FALSE(ok(H::try_compare(msg_448, good)));

    // Incremental; verify leaves the hasher ready for a new message whatever the outcome
    H h;
    TEST_ASSERT_TRUE(ok(h.try_verify(from_hex(v.empty))));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_TRUE(ok(h.try_verify(good)));
    TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_TRUE(ok(h.try_verify(good_bytes)));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_TRUE(ok(h.try_verify(digest)));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_TRUE(ok(h.try_verify(u8_array)));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_TRUE(ok(h.try_verify(u8_c_array)));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_FALSE(ok(h.try_verify(flipped)));
    TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_FALSE(ok(h.try_verify(flipped_bytes)));
    TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_FALSE(ok(h.try_verify(too_short)));
    TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_FALSE(ok(h.try_verify(too_long)));
    TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_FALSE(ok(h.try_verify(too_long_bytes)));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_FALSE(ok(h.try_verify(std::span<const std::byte>{})));
    ok(h.try_update(msg_abc));
    TEST_ASSERT_TRUE(ok(h.try_verify(good)));
}

template<typename H>
void check_lifecycle(const vectors& v) {
    auto msg = patterned_message(1000); // several blocks of every algorithm
    std::span<const uint8_t> all{msg};
    auto prefix = all.first(300);

    // Reset mid-message, and on a fresh hasher
    {
        H h;
        h.reset();
        ok(h.try_update(prefix));
        h.reset();
        TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));
        ok(h.try_update(prefix));
        h.reset();
        ok(h.try_update(msg_abc));
        TEST_ASSERT_DIGEST(v.abc, ok(h.try_finish()));
    }

    // Destroy mid-message, repeatedly, then hash again
    for (int i = 0; i < 16; ++i) {
        H h;
        ok(h.try_update(prefix));
    }
    {
        H h;
        ok(h.try_update(all));
        TEST_ASSERT_DIGEST(v.multi, ok(h.try_finish()));
    }

    // Move construction carries the partial message
    {
        H a;
        ok(a.try_update(msg_abc.substr(0, 1)));
        H b{std::move(a)};
        ok(b.try_update(msg_abc.substr(1)));
        TEST_ASSERT_DIGEST(v.abc, ok(b.try_finish()));
    }

    // Move assignment discards the target's message and takes over the source's
    {
        H x;
        ok(x.try_update(prefix));
        H y;
        ok(y.try_update(msg_abc.substr(0, 1)));
        x = std::move(y);
        ok(x.try_update(msg_abc.substr(1)));
        TEST_ASSERT_DIGEST(v.abc, ok(x.try_finish()));
    }

    // Clone before any update, and after finish: a fresh hasher
    {
        H h;
        H c = ok(h.try_clone());
        ok(c.try_update(msg_abc));
        TEST_ASSERT_DIGEST(v.abc, ok(c.try_finish()));
        TEST_ASSERT_DIGEST(v.empty, ok(h.try_finish()));
        H c2 = ok(h.try_clone());
        TEST_ASSERT_DIGEST(v.empty, ok(c2.try_finish()));
    }
}

template<typename H>
void check_independence(const vectors& v) {
    auto msg = patterned_message(1000); // several blocks of every algorithm
    std::span<const uint8_t> all{msg};
    auto prefix = all.first(300);
    auto rest = all.subspan(300);
    auto prefix_digest = idfxx::to_string(idfxx::hex(ok(H::try_compute(prefix))));

    // Two hashers interleaved
    {
        auto other = all.subspan(500);
        auto other_digest = idfxx::to_string(idfxx::hex(ok(H::try_compute(other))));
        H a;
        H b;
        ok(a.try_update(prefix));
        ok(b.try_update(other.first(250)));
        ok(a.try_update(rest));
        ok(b.try_update(other.subspan(250)));
        TEST_ASSERT_DIGEST(v.multi, ok(a.try_finish()));
        TEST_ASSERT_DIGEST(other_digest.c_str(), ok(b.try_finish()));
    }

    // One-shot calls, of this and other algorithms, while an incremental message is in progress
    // don't disturb it
    {
        H h;
        ok(h.try_update(prefix));
        TEST_ASSERT_DIGEST(v.abc, ok(H::try_compute(msg_abc)));
        TEST_ASSERT_TRUE(ok(H::try_compare(msg_abc, from_hex(v.abc))));
        TEST_ASSERT_DIGEST(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            ok(crypto::hash::sha256::try_compute(msg_abc))
        );
        TEST_ASSERT_DIGEST(
            "e61cfffe0d9195a525fc6cf06ca2d77119c24a40",
            ok(crypto::hash::sha1::try_compute(msg_all_a(200)))
        );
        ok(h.try_update(rest));
        TEST_ASSERT_DIGEST(v.multi, ok(h.try_finish()));
    }

    // Clone mid-message: the copies continue independently
    {
        H h;
        ok(h.try_update(prefix));
        H c = ok(h.try_clone());
        ok(c.try_update(rest));
        TEST_ASSERT_DIGEST(v.multi, ok(c.try_finish()));
        TEST_ASSERT_DIGEST(prefix_digest.c_str(), ok(h.try_finish()));
    }
    {
        H h;
        ok(h.try_update(prefix));
        H c = ok(h.try_clone());
        ok(h.try_update(rest));
        TEST_ASSERT_DIGEST(v.multi, ok(h.try_finish()));
        TEST_ASSERT_DIGEST(prefix_digest.c_str(), ok(c.try_finish()));
    }
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
template<typename H>
void check_throwing_api(const vectors& v) {
    auto msg = patterned_message(1000); // several blocks of every algorithm
    auto good = from_hex(v.abc);
    auto good_bytes = bytes_from_hex(v.abc);
    auto flipped = good;
    flipped[0] ^= 0x80;

    TEST_ASSERT_DIGEST(v.abc, H::compute(msg_abc));
    TEST_ASSERT_DIGEST(v.abc, H::compute(u8_of(msg_abc)));
    TEST_ASSERT_DIGEST(v.abc, H::compute(bytes_of(msg_abc)));
    TEST_ASSERT_DIGEST(v.multi, H::compute(msg));

    TEST_ASSERT_TRUE(H::compare(msg_abc, good));
    TEST_ASSERT_TRUE(H::compare(msg_abc, good_bytes));
    TEST_ASSERT_TRUE(H::compare(u8_of(msg_abc), good));
    TEST_ASSERT_TRUE(H::compare(u8_of(msg_abc), good_bytes));
    TEST_ASSERT_TRUE(H::compare(bytes_of(msg_abc), good));
    TEST_ASSERT_TRUE(H::compare(bytes_of(msg_abc), good_bytes));
    TEST_ASSERT_TRUE(H::compare(msg_abc, H::compute(msg_abc)));
    TEST_ASSERT_FALSE(H::compare(msg_abc, flipped));
    TEST_ASSERT_FALSE(H::compare(u8_of(msg_abc), std::span<const uint8_t>{good}.first(1)));

    H h;
    h.update(msg_abc.substr(0, 1));
    h.update(u8_of(msg_abc.substr(1, 1)));
    h.update(bytes_of(msg_abc.substr(2)));
    H c = h.clone();
    H c2 = h.clone();
    TEST_ASSERT_DIGEST(v.abc, h.finish());
    TEST_ASSERT_TRUE(c.verify(good));
    TEST_ASSERT_TRUE(c2.verify(good_bytes));
    h.update(msg_abc);
    TEST_ASSERT_FALSE(h.verify(flipped));
    TEST_ASSERT_DIGEST(v.empty, h.finish());
}
#endif

template<typename T>
void check_error(result<T> r, crypto::errc expected) {
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL_INT(std::to_underlying(expected), r.error().value());
    TEST_ASSERT_TRUE(r.error() == expected);
}

// Input longer than the chunk size, whole and in pieces that straddle chunk boundaries
template<typename H>
void check_chunked_input(const vectors& v) {
    // Several of the 4096-byte chunks in which input is fed to PSA where it would otherwise copy
    // the whole input to the heap (ESP-IDF 5.5), plus a remainder
    auto msg = patterned_message(3 * 4096 + 7);
    std::span<const uint8_t> all{msg};
    auto good = from_hex(v.chunked);
    auto flipped = good;
    flipped.back() ^= 0x01;

    TEST_ASSERT_DIGEST(v.chunked, ok(H::try_compute(all)));
    TEST_ASSERT_TRUE(ok(H::try_compare(all, good)));
    TEST_ASSERT_FALSE(ok(H::try_compare(all, flipped)));

    H h;
    ok(h.try_update(all));
    TEST_ASSERT_DIGEST(v.chunked, ok(h.try_finish()));
    ok(h.try_update(all));
    TEST_ASSERT_TRUE(ok(h.try_verify(good)));
    ok(h.try_update(all));
    TEST_ASSERT_FALSE(ok(h.try_verify(flipped)));

    const size_t sizes[] = {4095, 4098, 1, 8191};
    size_t pos = 0;
    for (size_t i = 0; pos < all.size(); ++i) {
        size_t n = std::min(sizes[i % std::size(sizes)], all.size() - pos);
        ok(h.try_update(all.subspan(pos, n)));
        pos += n;
    }
    TEST_ASSERT_DIGEST(v.chunked, ok(h.try_finish()));

    // The same through hash::operation
    std::array<std::byte, 64> buf;
    TEST_ASSERT_DIGEST(v.chunked, ok(crypto::hash::operation::try_compute(H::algorithm, all, buf)));
    TEST_ASSERT_TRUE(ok(crypto::hash::operation::try_compare(H::algorithm, all, good)));
    TEST_ASSERT_FALSE(ok(crypto::hash::operation::try_compare(H::algorithm, all, flipped)));
    auto op = ok(crypto::hash::operation::make(H::algorithm));
    ok(op.try_update(all));
    TEST_ASSERT_DIGEST(v.chunked, ok(op.try_finish(buf)));
    ok(op.try_update(all));
    TEST_ASSERT_TRUE(ok(op.try_verify(good)));
}

// hash::operation with the algorithm given at runtime: the same results as the hasher alias
void check_operation(crypto::hash::algorithm alg, const vectors& v) {
    using crypto::hash::operation;
    auto msg = patterned_message(1000); // several blocks of every algorithm
    std::span<const uint8_t> all{msg};
    const size_t size = crypto::hash::digest_size(alg);
    std::array<std::byte, 80> buf; // larger than any digest
    std::vector<std::byte> exact(size);
    auto small = std::span{buf}.first(size - 1);
    auto good = from_hex(v.abc);
    auto flipped = good;
    flipped[0] ^= 0x01;
    auto too_short = std::span<const uint8_t>{good}.first(size - 1);

    // One-shot: the digest is the filled prefix of the buffer
    auto d = ok(operation::try_compute(alg, msg_abc, buf));
    TEST_ASSERT_EQUAL_PTR(buf.data(), d.data());
    TEST_ASSERT_EQUAL(size, d.size());
    TEST_ASSERT_DIGEST(v.abc, d);
    TEST_ASSERT_DIGEST(v.abc, ok(operation::try_compute(alg, u8_of(msg_abc), exact)));
    TEST_ASSERT_DIGEST(v.abc, ok(operation::try_compute(alg, bytes_of(msg_abc), buf)));
    TEST_ASSERT_DIGEST(v.empty, ok(operation::try_compute(alg, std::string_view{}, buf)));
    TEST_ASSERT_DIGEST(v.empty, ok(operation::try_compute(alg, idfxx::byte_view{}, buf)));
    TEST_ASSERT_DIGEST(v.m448, ok(operation::try_compute(alg, msg_448, buf)));
    TEST_ASSERT_DIGEST(v.multi, ok(operation::try_compute(alg, msg, buf)));
    check_error(operation::try_compute(alg, msg_abc, small), crypto::errc::buffer_too_small);
    check_error(
        operation::try_compute(alg, msg_abc, idfxx::byte_span{}),
        crypto::errc::buffer_too_small
    );

    TEST_ASSERT_TRUE(ok(operation::try_compare(alg, msg_abc, good)));
    TEST_ASSERT_TRUE(ok(operation::try_compare(alg, u8_of(msg_abc), good)));
    TEST_ASSERT_TRUE(ok(operation::try_compare(alg, bytes_of(msg_abc), exact)));
    TEST_ASSERT_TRUE(ok(operation::try_compare(alg, std::string_view{}, from_hex(v.empty))));
    TEST_ASSERT_FALSE(ok(operation::try_compare(alg, msg_abc, flipped)));
    TEST_ASSERT_FALSE(ok(operation::try_compare(alg, msg_abc, too_short)));
    TEST_ASSERT_FALSE(ok(operation::try_compare(alg, msg_abc, std::span<const std::byte>{})));
    TEST_ASSERT_FALSE(ok(operation::try_compare(alg, msg_448, good)));

    // Incremental
    auto op = ok(operation::make(alg));
    TEST_ASSERT_TRUE(op.algorithm() == alg);
    TEST_ASSERT_EQUAL(size, op.digest_size());
    TEST_ASSERT_EQUAL(crypto::hash::block_size(alg), op.block_size());
    TEST_ASSERT_DIGEST(v.empty, ok(op.try_finish(buf)));
    ok(op.try_update(msg_abc.substr(0, 1)));
    ok(op.try_update(u8_of(msg_abc.substr(1, 1))));
    ok(op.try_update(bytes_of(msg_abc.substr(2))));
    auto f = ok(op.try_finish(buf));
    TEST_ASSERT_EQUAL_PTR(buf.data(), f.data());
    TEST_ASSERT_EQUAL(size, f.size());
    TEST_ASSERT_DIGEST(v.abc, f);
    for (size_t pos = 0; pos < all.size(); pos += 77) {
        ok(op.try_update(all.subspan(pos, std::min<size_t>(77, all.size() - pos))));
    }
    TEST_ASSERT_DIGEST(v.multi, ok(op.try_finish(exact)));

    // Finishing into a buffer that is too small leaves the message unchanged
    ok(op.try_update(msg_abc));
    check_error(op.try_finish(small), crypto::errc::buffer_too_small);
    check_error(op.try_finish(idfxx::byte_span{}), crypto::errc::buffer_too_small);
    TEST_ASSERT_DIGEST(v.abc, ok(op.try_finish(buf)));

    // Verify leaves the operation ready for a new message whatever the outcome
    TEST_ASSERT_TRUE(ok(op.try_verify(from_hex(v.empty))));
    ok(op.try_update(msg_abc));
    TEST_ASSERT_TRUE(ok(op.try_verify(good)));
    ok(op.try_update(msg_abc));
    TEST_ASSERT_FALSE(ok(op.try_verify(flipped)));
    TEST_ASSERT_DIGEST(v.empty, ok(op.try_finish(buf)));
    ok(op.try_update(msg_abc));
    TEST_ASSERT_FALSE(ok(op.try_verify(too_short)));
    TEST_ASSERT_DIGEST(v.empty, ok(op.try_finish(buf)));

    // Reset, and moves carry the partial message
    ok(op.try_update(msg_448));
    op.reset();
    TEST_ASSERT_DIGEST(v.empty, ok(op.try_finish(buf)));
    ok(op.try_update(msg_abc.substr(0, 1)));
    operation moved{std::move(op)};
    ok(moved.try_update(msg_abc.substr(1)));
    TEST_ASSERT_DIGEST(v.abc, ok(moved.try_finish(buf)));
    auto other = ok(operation::make(alg));
    ok(other.try_update(msg_448));
    ok(moved.try_update(msg_abc));
    other = std::move(moved);
    TEST_ASSERT_DIGEST(v.abc, ok(other.try_finish(buf)));
}

template<typename H>
constexpr bool has_hasher_traits() {
    return std::is_default_constructible_v<H> && !std::is_copy_constructible_v<H> &&
           !std::is_copy_assignable_v<H> && std::is_nothrow_move_constructible_v<H> &&
           std::is_nothrow_move_assignable_v<H> && std::is_nothrow_destructible_v<H> &&
           std::is_same_v<typename H::digest_type, std::array<std::byte, H::digest_size>>;
}

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// The test app enables every algorithm (sdkconfig.defaults / sdkconfig.qemu).
#ifndef PSA_WANT_ALG_MD5
#error "test app must enable md5"
#endif
#ifndef PSA_WANT_ALG_SHA_1
#error "test app must enable sha1"
#endif
#ifndef PSA_WANT_ALG_SHA_224
#error "test app must enable sha224"
#endif
#ifndef PSA_WANT_ALG_SHA_256
#error "test app must enable sha256"
#endif
#ifndef PSA_WANT_ALG_SHA_384
#error "test app must enable sha384"
#endif
#ifndef PSA_WANT_ALG_SHA_512
#error "test app must enable sha512"
#endif
#ifndef PSA_WANT_ALG_SHA3_224
#error "test app must enable sha3_224"
#endif
#ifndef PSA_WANT_ALG_SHA3_256
#error "test app must enable sha3_256"
#endif
#ifndef PSA_WANT_ALG_SHA3_384
#error "test app must enable sha3_384"
#endif
#ifndef PSA_WANT_ALG_SHA3_512
#error "test app must enable sha3_512"
#endif

static_assert(std::to_underlying(crypto::hash::algorithm::md5) == PSA_ALG_MD5);
static_assert(crypto::hash::md5::algorithm == crypto::hash::algorithm::md5);
static_assert(crypto::hash::md5::digest_size == 16);
static_assert(crypto::hash::md5::block_size == 64);
static_assert(has_hasher_traits<crypto::hash::md5>());

static_assert(std::to_underlying(crypto::hash::algorithm::sha1) == PSA_ALG_SHA_1);
static_assert(crypto::hash::sha1::algorithm == crypto::hash::algorithm::sha1);
static_assert(crypto::hash::sha1::digest_size == 20);
static_assert(crypto::hash::sha1::block_size == 64);
static_assert(has_hasher_traits<crypto::hash::sha1>());

static_assert(std::to_underlying(crypto::hash::algorithm::sha224) == PSA_ALG_SHA_224);
static_assert(crypto::hash::sha224::algorithm == crypto::hash::algorithm::sha224);
static_assert(crypto::hash::sha224::digest_size == 28);
static_assert(crypto::hash::sha224::block_size == 64);
static_assert(has_hasher_traits<crypto::hash::sha224>());

static_assert(std::to_underlying(crypto::hash::algorithm::sha256) == PSA_ALG_SHA_256);
static_assert(crypto::hash::sha256::algorithm == crypto::hash::algorithm::sha256);
static_assert(crypto::hash::sha256::digest_size == 32);
static_assert(crypto::hash::sha256::block_size == 64);
static_assert(has_hasher_traits<crypto::hash::sha256>());

static_assert(std::to_underlying(crypto::hash::algorithm::sha384) == PSA_ALG_SHA_384);
static_assert(crypto::hash::sha384::algorithm == crypto::hash::algorithm::sha384);
static_assert(crypto::hash::sha384::digest_size == 48);
static_assert(crypto::hash::sha384::block_size == 128);
static_assert(has_hasher_traits<crypto::hash::sha384>());

static_assert(std::to_underlying(crypto::hash::algorithm::sha512) == PSA_ALG_SHA_512);
static_assert(crypto::hash::sha512::algorithm == crypto::hash::algorithm::sha512);
static_assert(crypto::hash::sha512::digest_size == 64);
static_assert(crypto::hash::sha512::block_size == 128);
static_assert(has_hasher_traits<crypto::hash::sha512>());

static_assert(std::to_underlying(crypto::hash::algorithm::sha3_224) == PSA_ALG_SHA3_224);
static_assert(crypto::hash::sha3_224::algorithm == crypto::hash::algorithm::sha3_224);
static_assert(crypto::hash::sha3_224::digest_size == 28);
static_assert(crypto::hash::sha3_224::block_size == 144);
static_assert(has_hasher_traits<crypto::hash::sha3_224>());

static_assert(std::to_underlying(crypto::hash::algorithm::sha3_256) == PSA_ALG_SHA3_256);
static_assert(crypto::hash::sha3_256::algorithm == crypto::hash::algorithm::sha3_256);
static_assert(crypto::hash::sha3_256::digest_size == 32);
static_assert(crypto::hash::sha3_256::block_size == 136);
static_assert(has_hasher_traits<crypto::hash::sha3_256>());

static_assert(std::to_underlying(crypto::hash::algorithm::sha3_384) == PSA_ALG_SHA3_384);
static_assert(crypto::hash::sha3_384::algorithm == crypto::hash::algorithm::sha3_384);
static_assert(crypto::hash::sha3_384::digest_size == 48);
static_assert(crypto::hash::sha3_384::block_size == 104);
static_assert(has_hasher_traits<crypto::hash::sha3_384>());

static_assert(std::to_underlying(crypto::hash::algorithm::sha3_512) == PSA_ALG_SHA3_512);
static_assert(crypto::hash::sha3_512::algorithm == crypto::hash::algorithm::sha3_512);
static_assert(crypto::hash::sha3_512::digest_size == 64);
static_assert(crypto::hash::sha3_512::block_size == 72);
static_assert(has_hasher_traits<crypto::hash::sha3_512>());

namespace {
// Not hash algorithms
constexpr auto alg_none = static_cast<crypto::hash::algorithm>(PSA_ALG_NONE);
constexpr auto alg_hmac_sha256 =
    static_cast<crypto::hash::algorithm>(PSA_ALG_HMAC(PSA_ALG_SHA_256));
// A hash algorithm value that names no algorithm
constexpr auto alg_unassigned = static_cast<crypto::hash::algorithm>(PSA_ALG_CATEGORY_HASH | 0x7e);
// A known hash algorithm that idfxx_crypto has no enumerator for, and the test app doesn't enable
constexpr auto alg_ripemd160 = static_cast<crypto::hash::algorithm>(PSA_ALG_RIPEMD160);
} // namespace

static_assert(crypto::hash::digest_size(alg_none) == 0);
static_assert(crypto::hash::block_size(alg_none) == 0);
static_assert(crypto::hash::digest_size(alg_hmac_sha256) == 0);
static_assert(crypto::hash::block_size(alg_hmac_sha256) == 0);
static_assert(crypto::hash::digest_size(alg_unassigned) == 0);
static_assert(crypto::hash::block_size(alg_unassigned) == 0);
static_assert(crypto::hash::digest_size(alg_ripemd160) == 20);
static_assert(crypto::hash::block_size(alg_ripemd160) == 64);

static_assert(!std::is_default_constructible_v<crypto::hash::operation>);
static_assert(!std::is_copy_constructible_v<crypto::hash::operation>);
static_assert(!std::is_copy_assignable_v<crypto::hash::operation>);
static_assert(std::is_nothrow_move_constructible_v<crypto::hash::operation>);
static_assert(std::is_nothrow_move_assignable_v<crypto::hash::operation>);
static_assert(std::is_nothrow_destructible_v<crypto::hash::operation>);
static_assert(!std::is_convertible_v<crypto::hash::algorithm, crypto::hash::operation>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_constructible_v<crypto::hash::operation, crypto::hash::algorithm>);
#else
static_assert(!std::is_constructible_v<crypto::hash::operation, crypto::hash::algorithm>);
#endif

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

namespace {
const vectors md5_vectors{
    "d41d8cd98f00b204e9800998ecf8427e",
    "900150983cd24fb0d6963f7d28e17f72",
    "8215ef0796a20bcaaae116d3876c664a",
    "cbecbdb0fdd5cec1e242493b6008cc79",
    "9c08fe5954084b3f3b026ccfcd0a90ff",
};
} // namespace

TEST_CASE("crypto md5 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::md5>(md5_vectors);
}

TEST_CASE("crypto md5 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::md5>(md5_vectors);
}

TEST_CASE("crypto md5 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::md5>(md5_vectors);
}

TEST_CASE("crypto md5 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::md5>(md5_vectors);
}

TEST_CASE("crypto md5 interleaved and cloned hashers are independent", "[idfxx][crypto]") {
    check_independence<crypto::hash::md5>(md5_vectors);
}

TEST_CASE("crypto md5 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::md5>(md5_vectors);
}

TEST_CASE("crypto md5 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::md5, md5_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto md5 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::md5>(md5_vectors);
}
#endif


namespace {
const vectors sha1_vectors{
    "da39a3ee5e6b4b0d3255bfef95601890afd80709",
    "a9993e364706816aba3e25717850c26c9cd0d89d",
    "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
    "af0b191c2de46fe13fe0908f5a6a4e90e0cafc46",
    "04a1f3175caacc8b5fa21c7fcc08d97921d99d19",
};
} // namespace

TEST_CASE("crypto sha1 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::sha1>(sha1_vectors);
}

TEST_CASE("crypto sha1 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::sha1>(sha1_vectors);
}

TEST_CASE("crypto sha1 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::sha1>(sha1_vectors);
}

TEST_CASE("crypto sha1 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::sha1>(sha1_vectors);
}

TEST_CASE("crypto sha1 interleaved and cloned hashers are independent", "[idfxx][crypto]") {
    check_independence<crypto::hash::sha1>(sha1_vectors);
}

TEST_CASE("crypto sha1 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::sha1>(sha1_vectors);
}

TEST_CASE("crypto sha1 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::sha1, sha1_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto sha1 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::sha1>(sha1_vectors);
}
#endif


namespace {
const vectors sha224_vectors{
    "d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f",
    "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7",
    "75388b16512776cc5dba5da1fd890150b0c6455cb4f58b1952522525",
    "fd2f31945f10f2e0b559d19c56adc4cddfa4c68f38c77093a9cb8b0c",
    "83c3cbc92cb27c483efb833e07e8f5d4f37afd7eaf7731d9a5cbd491",
};
} // namespace

TEST_CASE("crypto sha224 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::sha224>(sha224_vectors);
}

TEST_CASE("crypto sha224 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::sha224>(sha224_vectors);
}

TEST_CASE("crypto sha224 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::sha224>(sha224_vectors);
}

TEST_CASE("crypto sha224 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::sha224>(sha224_vectors);
}

// QEMU's esp32s3 SHA peripheral model produces a wrong digest when a SHA-224 operation resumes
// after another SHA-224 operation has used the peripheral (ESP-IDF 5.5 and 6.x alike). ESP32-S3
// silicon gives the correct digests, so this runs on hardware only.

TEST_CASE("crypto sha224 interleaved and cloned hashers are independent", "[idfxx][crypto][hw]") {
    check_independence<crypto::hash::sha224>(sha224_vectors);
}

TEST_CASE("crypto sha224 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::sha224>(sha224_vectors);
}

TEST_CASE("crypto sha224 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::sha224, sha224_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto sha224 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::sha224>(sha224_vectors);
}
#endif


namespace {
const vectors sha256_vectors{
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
    "a8af099bf2e878609558dbf69d8f88f4a31040a8cf84b549a0cfa912f12ffc3f",
    "a8663d3afd009279950f853aed4c16ada995594511282e538274159965db9967",
};
} // namespace

TEST_CASE("crypto sha256 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::sha256>(sha256_vectors);
}

TEST_CASE("crypto sha256 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::sha256>(sha256_vectors);
}

TEST_CASE("crypto sha256 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::sha256>(sha256_vectors);
}

TEST_CASE("crypto sha256 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::sha256>(sha256_vectors);
}

TEST_CASE("crypto sha256 interleaved and cloned hashers are independent", "[idfxx][crypto]") {
    check_independence<crypto::hash::sha256>(sha256_vectors);
}

TEST_CASE("crypto sha256 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::sha256>(sha256_vectors);
}

TEST_CASE("crypto sha256 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::sha256, sha256_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto sha256 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::sha256>(sha256_vectors);
}
#endif


namespace {
const vectors sha384_vectors{
    "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b",
    "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7",
    "3391fdddfc8dc7393707a65b1b4709397cf8b1d162af05abfe8f450de5f36bc6b0455a8520bc4e6f5fe95b1fe3c8452b",
    "cfe84a17cb1c1c9d4e7d1b1f5e7aee4ba0fa7ccaafe00c80b20b94ef4250ecae24321940e3e66510732fe32f386e4cc7",
    "a1f37040dd08696b47261b74fb19faa375876b7f56d863e9b3007eb629a57818f24f063979ef517e62a7daf2c6f7c755",
};
} // namespace

TEST_CASE("crypto sha384 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::sha384>(sha384_vectors);
}

TEST_CASE("crypto sha384 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::sha384>(sha384_vectors);
}

TEST_CASE("crypto sha384 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::sha384>(sha384_vectors);
}

TEST_CASE("crypto sha384 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::sha384>(sha384_vectors);
}

TEST_CASE("crypto sha384 interleaved and cloned hashers are independent", "[idfxx][crypto]") {
    check_independence<crypto::hash::sha384>(sha384_vectors);
}

TEST_CASE("crypto sha384 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::sha384>(sha384_vectors);
}

TEST_CASE("crypto sha384 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::sha384, sha384_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto sha384 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::sha384>(sha384_vectors);
}
#endif


namespace {
const vectors sha512_vectors{
    "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e",
    "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
    "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445",
    "6cd2eda9bf9c0597129029b0054b81e433f6b8b7b499a75eb705efd74bac194149835b1d1a14c48be696e4d588456d512a22eae7aa1b57be2b56eae7d35e08cb",
    "423af5641b4fa811f235a93467615f2cf137736f1f13f058996bb01e6c08ef596db18b1ffdbb068c2e83659cd30f76874e4574c050989cdf65cb73f8d1775234",
};
} // namespace

TEST_CASE("crypto sha512 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::sha512>(sha512_vectors);
}

TEST_CASE("crypto sha512 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::sha512>(sha512_vectors);
}

TEST_CASE("crypto sha512 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::sha512>(sha512_vectors);
}

TEST_CASE("crypto sha512 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::sha512>(sha512_vectors);
}

TEST_CASE("crypto sha512 interleaved and cloned hashers are independent", "[idfxx][crypto]") {
    check_independence<crypto::hash::sha512>(sha512_vectors);
}

TEST_CASE("crypto sha512 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::sha512>(sha512_vectors);
}

TEST_CASE("crypto sha512 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::sha512, sha512_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto sha512 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::sha512>(sha512_vectors);
}
#endif


namespace {
const vectors sha3_224_vectors{
    "6b4e03423667dbb73b6e15454f0eb1abd4597f9a1b078e3f5b5a6bc7",
    "e642824c3f8cf24ad09234ee7d3c766fc9a3a5168d0c94ad73b46fdf",
    "8a24108b154ada21c9fd5574494479ba5c7e7ab76ef264ead0fcce33",
    "449b2acbbc0d2d133fd7a11157aafd2118a253f7a91091e5d3092efa",
    "841f26996b87554f946b0c2225edd85fc5e9d4b3cd0c9091bf80e0e4",
};
} // namespace

TEST_CASE("crypto sha3_224 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::sha3_224>(sha3_224_vectors);
}

TEST_CASE("crypto sha3_224 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::sha3_224>(sha3_224_vectors);
}

TEST_CASE("crypto sha3_224 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::sha3_224>(sha3_224_vectors);
}

TEST_CASE("crypto sha3_224 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::sha3_224>(sha3_224_vectors);
}

TEST_CASE("crypto sha3_224 interleaved and cloned hashers are independent", "[idfxx][crypto]") {
    check_independence<crypto::hash::sha3_224>(sha3_224_vectors);
}

TEST_CASE("crypto sha3_224 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::sha3_224>(sha3_224_vectors);
}

TEST_CASE("crypto sha3_224 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::sha3_224, sha3_224_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto sha3_224 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::sha3_224>(sha3_224_vectors);
}
#endif


namespace {
const vectors sha3_256_vectors{
    "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a",
    "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532",
    "41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376",
    "14e5de35911194ddad95ac1572e2b6ce054ed2146cd0562280fcab04ccfecbd8",
    "92ae05f36c039d26efcc2ca9fa29d64baab6b823ff9bb208c7b421a9b03fde58",
};
} // namespace

TEST_CASE("crypto sha3_256 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::sha3_256>(sha3_256_vectors);
}

TEST_CASE("crypto sha3_256 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::sha3_256>(sha3_256_vectors);
}

TEST_CASE("crypto sha3_256 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::sha3_256>(sha3_256_vectors);
}

TEST_CASE("crypto sha3_256 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::sha3_256>(sha3_256_vectors);
}

TEST_CASE("crypto sha3_256 interleaved and cloned hashers are independent", "[idfxx][crypto]") {
    check_independence<crypto::hash::sha3_256>(sha3_256_vectors);
}

TEST_CASE("crypto sha3_256 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::sha3_256>(sha3_256_vectors);
}

TEST_CASE("crypto sha3_256 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::sha3_256, sha3_256_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto sha3_256 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::sha3_256>(sha3_256_vectors);
}
#endif


namespace {
const vectors sha3_384_vectors{
    "0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2ac3713831264adb47fb6bd1e058d5f004",
    "ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b298d88cea927ac7f539f1edf228376d25",
    "991c665755eb3a4b6bbdfb75c78a492e8c56a22c5c4d7e429bfdbc32b9d4ad5aa04a1f076e62fea19eef51acd0657c22",
    "78361036d2bcf7cfc0d8004dd9f618ba2f1580022bd3127f639489776f1d11e3e61cc76d41f80421ee0a63b92a07ca51",
    "3f5f83fd4abd34463949006bf21f1842795a016943f1cd42ec61617d60f1c7dba9803ecba49dff706bbfb31ef81ffa2e",
};
} // namespace

TEST_CASE("crypto sha3_384 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::sha3_384>(sha3_384_vectors);
}

TEST_CASE("crypto sha3_384 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::sha3_384>(sha3_384_vectors);
}

TEST_CASE("crypto sha3_384 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::sha3_384>(sha3_384_vectors);
}

TEST_CASE("crypto sha3_384 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::sha3_384>(sha3_384_vectors);
}

TEST_CASE("crypto sha3_384 interleaved and cloned hashers are independent", "[idfxx][crypto]") {
    check_independence<crypto::hash::sha3_384>(sha3_384_vectors);
}

TEST_CASE("crypto sha3_384 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::sha3_384>(sha3_384_vectors);
}

TEST_CASE("crypto sha3_384 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::sha3_384, sha3_384_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto sha3_384 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::sha3_384>(sha3_384_vectors);
}
#endif


namespace {
const vectors sha3_512_vectors{
    "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26",
    "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0",
    "04a371e84ecfb5b8b77cb48610fca8182dd457ce6f326a0fd3d7ec2f1e91636dee691fbe0c985302ba1b0d8dc78c086346b533b49c030d99a27daf1139d6e75e",
    "0a96e7c099e956287a7d6c2516befb5089714c38f7c01ab158bcd131b50dd10c80a71ee8fe850a301fea39e88f9b3f58822b47925700c44efcd5a3ed333f5947",
    "74ab68679d8acbba4d149bc9ade68156048f764b6230a5c111e42bf0374fee59c705ac3b5bcc5ace7a01b7a7c98c40985c13af60a00aa748b66c8049bc19eed1",
};
} // namespace

TEST_CASE("crypto sha3_512 known-answer vectors", "[idfxx][crypto]") {
    check_known_answers<crypto::hash::sha3_512>(sha3_512_vectors);
}

TEST_CASE("crypto sha3_512 split updates match one-shot", "[idfxx][crypto]") {
    check_split_updates<crypto::hash::sha3_512>(sha3_512_vectors);
}

TEST_CASE("crypto sha3_512 compare and verify", "[idfxx][crypto]") {
    check_compare_and_verify<crypto::hash::sha3_512>(sha3_512_vectors);
}

TEST_CASE("crypto sha3_512 reset, destroy and move", "[idfxx][crypto]") {
    check_lifecycle<crypto::hash::sha3_512>(sha3_512_vectors);
}

TEST_CASE("crypto sha3_512 interleaved and cloned hashers are independent", "[idfxx][crypto]") {
    check_independence<crypto::hash::sha3_512>(sha3_512_vectors);
}

TEST_CASE("crypto sha3_512 input spanning several chunks", "[idfxx][crypto]") {
    check_chunked_input<crypto::hash::sha3_512>(sha3_512_vectors);
}

TEST_CASE("crypto sha3_512 hash::operation", "[idfxx][crypto]") {
    check_operation(crypto::hash::algorithm::sha3_512, sha3_512_vectors);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto sha3_512 exception-based API", "[idfxx][crypto]") {
    check_throwing_api<crypto::hash::sha3_512>(sha3_512_vectors);
}
#endif

TEST_CASE("crypto hash::operation rejects values it cannot use", "[idfxx][crypto]") {
    using crypto::errc;
    using crypto::hash::operation;
    struct bad_algorithm {
        crypto::hash::algorithm alg;
        errc code;
    };
    const bad_algorithm cases[] = {
        {alg_none, errc::invalid_argument},
        {alg_hmac_sha256, errc::invalid_argument},
        {alg_unassigned, errc::not_supported},
#ifndef PSA_WANT_ALG_RIPEMD160
        {alg_ripemd160, errc::not_supported},
#endif
    };
    std::array<std::byte, 64> buf{};
    auto twenty = std::span{buf}.first(20);

    for (const auto& c : cases) {
        check_error(operation::make(c.alg), c.code);
        check_error(operation::try_compute(c.alg, msg_abc, buf), c.code);
        check_error(operation::try_compute(c.alg, msg_abc, idfxx::byte_span{}), c.code);
        // An unusable algorithm is an error, not a mismatch, whatever the expected digest's length
        check_error(operation::try_compare(c.alg, msg_abc, buf), c.code);
        check_error(operation::try_compare(c.alg, msg_abc, twenty), c.code);
        check_error(operation::try_compare(c.alg, msg_abc, idfxx::byte_view{}), c.code);
    }
}

TEST_CASE("crypto hash::operation clones are independent", "[idfxx][crypto]") {
    const auto alg = crypto::hash::algorithm::sha256;
    const auto& v = sha256_vectors;
    auto msg = patterned_message(1000); // several blocks of every algorithm
    std::span<const uint8_t> all{msg};
    auto prefix = all.first(300);
    auto rest = all.subspan(300);
    auto prefix_digest =
        idfxx::to_string(idfxx::hex(ok(crypto::hash::sha256::try_compute(prefix))));
    std::array<std::byte, 32> buf;

    // Before any update: a fresh operation
    auto op = ok(crypto::hash::operation::make(alg));
    auto c = ok(op.try_clone());
    TEST_ASSERT_TRUE(c.algorithm() == alg);
    ok(c.try_update(msg_abc));
    TEST_ASSERT_DIGEST(v.abc, ok(c.try_finish(buf)));
    TEST_ASSERT_DIGEST(v.empty, ok(op.try_finish(buf)));

    // Mid-message: the copies continue independently
    ok(op.try_update(prefix));
    auto c2 = ok(op.try_clone());
    ok(c2.try_update(rest));
    TEST_ASSERT_DIGEST(v.multi, ok(c2.try_finish(buf)));
    TEST_ASSERT_DIGEST(prefix_digest.c_str(), ok(op.try_finish(buf)));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto hash::operation exception-based API", "[idfxx][crypto]") {
    using crypto::errc;
    using crypto::hash::operation;
    const auto alg = crypto::hash::algorithm::sha256;
    const auto& v = sha256_vectors;
    auto good = from_hex(v.abc);
    auto flipped = good;
    flipped[0] ^= 0x80;
    std::array<std::byte, 32> buf;
    auto small = std::span{buf}.first(31);

    TEST_ASSERT_DIGEST(v.abc, operation::compute(alg, msg_abc, buf));
    TEST_ASSERT_DIGEST(v.abc, operation::compute(alg, u8_of(msg_abc), buf));
    TEST_ASSERT_TRUE(operation::compare(alg, msg_abc, good));
    TEST_ASSERT_TRUE(operation::compare(alg, bytes_of(msg_abc), good));
    TEST_ASSERT_FALSE(operation::compare(alg, msg_abc, flipped));

    operation op{alg};
    op.update(msg_abc.substr(0, 1));
    op.update(u8_of(msg_abc.substr(1)));
    operation c = op.clone();
    TEST_ASSERT_DIGEST(v.abc, op.finish(buf));
    TEST_ASSERT_TRUE(c.verify(good));
    op.update(msg_abc);
    TEST_ASSERT_FALSE(op.verify(flipped));

    auto throws = [](auto&& f, errc expected) {
        try {
            f();
        } catch (const std::system_error& e) {
            return e.code() == expected;
        }
        return false;
    };
    TEST_ASSERT_TRUE(throws([] { operation bad{alg_none}; }, errc::invalid_argument));
    TEST_ASSERT_TRUE(throws([] { operation bad{alg_unassigned}; }, errc::not_supported));
    TEST_ASSERT_TRUE(
        throws([&] { (void)operation::compute(alg, msg_abc, small); }, errc::buffer_too_small)
    );
    TEST_ASSERT_TRUE(throws([&] { (void)op.finish(small); }, errc::buffer_too_small));
}
#endif
