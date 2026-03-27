// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx http client
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/http/client"
#include "unity.h"

#include <type_traits>
#include <utility>

using namespace idfxx;
using namespace idfxx::http;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// Note: Enum values are verified against ESP-IDF constants in client.cpp
// =============================================================================

// client is not default constructible (must use make() factory)
static_assert(!std::is_default_constructible_v<client>);

// client is not copyable (unique ownership of handle)
static_assert(!std::is_copy_constructible_v<client>);
static_assert(!std::is_copy_assignable_v<client>);

// client is move-only
static_assert(std::is_move_constructible_v<client>);
static_assert(std::is_move_assignable_v<client>);

// client::errc is an error_code_enum
static_assert(std::is_error_code_enum_v<client::errc>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// -- Error category tests ----------------------------------------------------

TEST_CASE("http client error_category has correct name", "[idfxx][http]") {
    TEST_ASSERT_EQUAL_STRING("http::client::Error", http_client_category().name());
}

TEST_CASE("http client error_category produces messages", "[idfxx][http]") {
    std::error_code ec = client::errc::max_redirect;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = client::errc::connect;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = client::errc::connection_closed;
    TEST_ASSERT_TRUE(ec.message().length() > 0);
}

TEST_CASE("make_error_code creates correct error codes", "[idfxx][http]") {
    std::error_code ec = make_error_code(client::errc::max_redirect);
    TEST_ASSERT_EQUAL(std::to_underlying(client::errc::max_redirect), ec.value());
    TEST_ASSERT_EQUAL_STRING("http::client::Error", ec.category().name());

    ec = make_error_code(client::errc::eagain);
    TEST_ASSERT_EQUAL(std::to_underlying(client::errc::eagain), ec.value());
}

// -- to_string tests ---------------------------------------------------------

TEST_CASE("to_string(event_id) outputs correct names", "[idfxx][http]") {
    auto s = to_string(event_id::error);
    TEST_ASSERT_EQUAL_STRING("ERROR", s.c_str());
    s = to_string(event_id::on_connected);
    TEST_ASSERT_EQUAL_STRING("ON_CONNECTED", s.c_str());
    s = to_string(event_id::headers_sent);
    TEST_ASSERT_EQUAL_STRING("HEADERS_SENT", s.c_str());
    s = to_string(event_id::on_header);
    TEST_ASSERT_EQUAL_STRING("ON_HEADER", s.c_str());
    s = to_string(event_id::on_data);
    TEST_ASSERT_EQUAL_STRING("ON_DATA", s.c_str());
    s = to_string(event_id::on_finish);
    TEST_ASSERT_EQUAL_STRING("ON_FINISH", s.c_str());
    s = to_string(event_id::disconnected);
    TEST_ASSERT_EQUAL_STRING("DISCONNECTED", s.c_str());
    s = to_string(event_id::redirect);
    TEST_ASSERT_EQUAL_STRING("REDIRECT", s.c_str());
}

TEST_CASE("to_string(event_id) handles unknown values", "[idfxx][http]") {
    auto unknown = static_cast<event_id>(99);
    auto s = to_string(unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}

// -- Formatter tests ---------------------------------------------------------

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<event_id, char>);

TEST_CASE("event_id formatter outputs correct names", "[idfxx][http]") {
    auto s = std::format("{}", event_id::error);
    TEST_ASSERT_EQUAL_STRING("ERROR", s.c_str());
    s = std::format("{}", event_id::on_connected);
    TEST_ASSERT_EQUAL_STRING("ON_CONNECTED", s.c_str());
    s = std::format("{}", event_id::redirect);
    TEST_ASSERT_EQUAL_STRING("REDIRECT", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
