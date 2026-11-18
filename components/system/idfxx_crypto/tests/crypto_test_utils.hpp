// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Helpers shared by the idfxx_crypto tests.

#pragma once

#include "idfxx/crypto/error"
#include "idfxx/error"
#include "idfxx/hex"
#include "unity.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sdkconfig.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Keys need the mbedTLS threading layer, which the hashing tests don't
#ifdef CONFIG_MBEDTLS_THREADING_C
#include "idfxx/crypto/key"
#endif

namespace idfxx_crypto_test {

template<typename T>
T ok(idfxx::result<T> r) {
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), r ? "" : r.error().message().c_str());
    return std::move(*r);
}

inline void ok(idfxx::result<void> r) {
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), r ? "" : r.error().message().c_str());
}

template<typename T>
void fails_with(idfxx::crypto::errc expected, const idfxx::result<T>& r) {
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL_MESSAGE(
        std::to_underlying(expected), r.error().value(), r.error().message().c_str()
    );
    TEST_ASSERT_EQUAL_PTR(&idfxx::crypto_category(), &r.error().category());
}

// n bytes: first, first + 1, ..., wrapping after 255 (by default 0, 1, ..., 255, 0, 1, ...)
inline std::vector<uint8_t> patterned_message(std::size_t n, uint8_t first = 0) {
    std::vector<uint8_t> msg(n);
    for (size_t i = 0; i < msg.size(); ++i) {
        msg[i] = static_cast<uint8_t>(first + i);
    }
    return msg;
}

inline std::span<const std::byte> bytes_of(std::string_view s) {
    return std::as_bytes(std::span<const char>{s.data(), s.size()});
}

inline std::span<const std::byte> bytes_of(const std::vector<uint8_t>& v) {
    return std::as_bytes(std::span<const uint8_t>{v});
}

inline std::vector<uint8_t> from_hex(std::string_view hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return out;
}

inline std::vector<std::byte> bytes_from_hex(std::string_view hex) {
    auto u8 = from_hex(hex);
    std::vector<std::byte> out(u8.size());
    std::memcpy(out.data(), u8.data(), u8.size());
    return out;
}

// Asserts that a digest or tag is the expected one (lowercase hex). A function, not inline in each
// assertion, to keep the many helper instantiations small.
inline void assert_hex(std::string_view expected_hex, std::span<const std::byte> actual, int line) {
    UnityAssertEqualString(
        std::string{expected_hex}.c_str(), idfxx::to_string(idfxx::hex(actual)).c_str(), nullptr,
        static_cast<UNITY_LINE_TYPE>(line)
    );
}

#ifdef CONFIG_MBEDTLS_THREADING_C
inline constexpr auto sign_verify =
    idfxx::crypto::key_usage::sign_message | idfxx::crypto::key_usage::verify_message;
#endif

} // namespace idfxx_crypto_test

#define TEST_ASSERT_DIGEST(expected_hex, digest) \
    ::idfxx_crypto_test::assert_hex((expected_hex), (digest), __LINE__)

#define TEST_ASSERT_TAG(expected_hex, tag) \
    ::idfxx_crypto_test::assert_hex((expected_hex), (tag), __LINE__)
