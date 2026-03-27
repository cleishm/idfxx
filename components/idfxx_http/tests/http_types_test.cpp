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
    auto s = to_string(method::get);
    TEST_ASSERT_EQUAL_STRING("GET", s.c_str());
    s = to_string(method::post);
    TEST_ASSERT_EQUAL_STRING("POST", s.c_str());
    s = to_string(method::put);
    TEST_ASSERT_EQUAL_STRING("PUT", s.c_str());
    s = to_string(method::patch);
    TEST_ASSERT_EQUAL_STRING("PATCH", s.c_str());
    s = to_string(method::delete_);
    TEST_ASSERT_EQUAL_STRING("DELETE", s.c_str());
    s = to_string(method::head);
    TEST_ASSERT_EQUAL_STRING("HEAD", s.c_str());
    s = to_string(method::options);
    TEST_ASSERT_EQUAL_STRING("OPTIONS", s.c_str());
    s = to_string(method::propfind);
    TEST_ASSERT_EQUAL_STRING("PROPFIND", s.c_str());
    s = to_string(method::report);
    TEST_ASSERT_EQUAL_STRING("REPORT", s.c_str());
}

TEST_CASE("to_string(method) handles unknown values", "[idfxx][http]") {
    auto unknown = static_cast<method>(99);
    auto s = to_string(unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}

TEST_CASE("to_string(auth_type) outputs correct names", "[idfxx][http]") {
    auto s = to_string(auth_type::none);
    TEST_ASSERT_EQUAL_STRING("NONE", s.c_str());
    s = to_string(auth_type::basic);
    TEST_ASSERT_EQUAL_STRING("BASIC", s.c_str());
    s = to_string(auth_type::digest);
    TEST_ASSERT_EQUAL_STRING("DIGEST", s.c_str());
}

TEST_CASE("to_string(auth_type) handles unknown values", "[idfxx][http]") {
    auto unknown = static_cast<auth_type>(99);
    auto s = to_string(unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}

TEST_CASE("to_string(transport) outputs correct names", "[idfxx][http]") {
    auto s = to_string(transport::unknown);
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", s.c_str());
    s = to_string(transport::tcp);
    TEST_ASSERT_EQUAL_STRING("TCP", s.c_str());
    s = to_string(transport::ssl);
    TEST_ASSERT_EQUAL_STRING("SSL", s.c_str());
}

TEST_CASE("to_string(transport) handles unknown values", "[idfxx][http]") {
    auto unknown = static_cast<transport>(99);
    auto s = to_string(unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}

// -- Formatter tests ---------------------------------------------------------

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<method, char>);
static_assert(std::formattable<auth_type, char>);
static_assert(std::formattable<transport, char>);

TEST_CASE("method formatter outputs correct names", "[idfxx][http]") {
    auto s = std::format("{}", method::get);
    TEST_ASSERT_EQUAL_STRING("GET", s.c_str());
    s = std::format("{}", method::post);
    TEST_ASSERT_EQUAL_STRING("POST", s.c_str());
    s = std::format("{}", method::delete_);
    TEST_ASSERT_EQUAL_STRING("DELETE", s.c_str());
}

TEST_CASE("auth_type formatter outputs correct names", "[idfxx][http]") {
    auto s = std::format("{}", auth_type::none);
    TEST_ASSERT_EQUAL_STRING("NONE", s.c_str());
    s = std::format("{}", auth_type::basic);
    TEST_ASSERT_EQUAL_STRING("BASIC", s.c_str());
    s = std::format("{}", auth_type::digest);
    TEST_ASSERT_EQUAL_STRING("DIGEST", s.c_str());
}

TEST_CASE("transport formatter outputs correct names", "[idfxx][http]") {
    auto s = std::format("{}", transport::unknown);
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", s.c_str());
    s = std::format("{}", transport::tcp);
    TEST_ASSERT_EQUAL_STRING("TCP", s.c_str());
    s = std::format("{}", transport::ssl);
    TEST_ASSERT_EQUAL_STRING("SSL", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
