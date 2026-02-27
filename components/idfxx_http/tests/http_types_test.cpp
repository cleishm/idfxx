// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx http types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/http/types"
#include "unity.h"

#include <type_traits>
#include <utility>

using namespace idfxx;
using namespace idfxx::http;

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// Note: Enum values are verified against ESP-IDF constants in types.cpp
// =============================================================================

// -- to_string tests ---------------------------------------------------------

TEST_CASE("to_string(method) outputs correct names", "[idfxx][http]") {
    TEST_ASSERT_EQUAL_STRING("GET", to_string(method::get).c_str());
    TEST_ASSERT_EQUAL_STRING("POST", to_string(method::post).c_str());
    TEST_ASSERT_EQUAL_STRING("PUT", to_string(method::put).c_str());
    TEST_ASSERT_EQUAL_STRING("PATCH", to_string(method::patch).c_str());
    TEST_ASSERT_EQUAL_STRING("DELETE", to_string(method::delete_).c_str());
    TEST_ASSERT_EQUAL_STRING("HEAD", to_string(method::head).c_str());
    TEST_ASSERT_EQUAL_STRING("OPTIONS", to_string(method::options).c_str());
    TEST_ASSERT_EQUAL_STRING("PROPFIND", to_string(method::propfind).c_str());
    TEST_ASSERT_EQUAL_STRING("REPORT", to_string(method::report).c_str());
}

TEST_CASE("to_string(method) handles unknown values", "[idfxx][http]") {
    auto unknown = static_cast<method>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", to_string(unknown).c_str());
}

TEST_CASE("to_string(auth_type) outputs correct names", "[idfxx][http]") {
    TEST_ASSERT_EQUAL_STRING("NONE", to_string(auth_type::none).c_str());
    TEST_ASSERT_EQUAL_STRING("BASIC", to_string(auth_type::basic).c_str());
    TEST_ASSERT_EQUAL_STRING("DIGEST", to_string(auth_type::digest).c_str());
}

TEST_CASE("to_string(auth_type) handles unknown values", "[idfxx][http]") {
    auto unknown = static_cast<auth_type>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", to_string(unknown).c_str());
}

TEST_CASE("to_string(transport) outputs correct names", "[idfxx][http]") {
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", to_string(transport::unknown).c_str());
    TEST_ASSERT_EQUAL_STRING("TCP", to_string(transport::tcp).c_str());
    TEST_ASSERT_EQUAL_STRING("SSL", to_string(transport::ssl).c_str());
}

TEST_CASE("to_string(transport) handles unknown values", "[idfxx][http]") {
    auto unknown = static_cast<transport>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", to_string(unknown).c_str());
}

// -- Formatter tests ---------------------------------------------------------

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<method, char>);
static_assert(std::formattable<auth_type, char>);
static_assert(std::formattable<transport, char>);

TEST_CASE("method formatter outputs correct names", "[idfxx][http]") {
    TEST_ASSERT_EQUAL_STRING("GET", std::format("{}", method::get).c_str());
    TEST_ASSERT_EQUAL_STRING("POST", std::format("{}", method::post).c_str());
    TEST_ASSERT_EQUAL_STRING("DELETE", std::format("{}", method::delete_).c_str());
}

TEST_CASE("auth_type formatter outputs correct names", "[idfxx][http]") {
    TEST_ASSERT_EQUAL_STRING("NONE", std::format("{}", auth_type::none).c_str());
    TEST_ASSERT_EQUAL_STRING("BASIC", std::format("{}", auth_type::basic).c_str());
    TEST_ASSERT_EQUAL_STRING("DIGEST", std::format("{}", auth_type::digest).c_str());
}

TEST_CASE("transport formatter outputs correct names", "[idfxx][http]") {
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", std::format("{}", transport::unknown).c_str());
    TEST_ASSERT_EQUAL_STRING("TCP", std::format("{}", transport::tcp).c_str());
    TEST_ASSERT_EQUAL_STRING("SSL", std::format("{}", transport::ssl).c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
