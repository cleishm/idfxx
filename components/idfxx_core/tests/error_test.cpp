// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx error handling
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/error"
#include "unity.h"

#include <esp_err.h>
#include <system_error>
#include <type_traits>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// errc is an enum class with int as underlying type
static_assert(std::is_enum_v<errc>);
static_assert(std::is_same_v<std::underlying_type_t<errc>, int>);

// errc is registered as an error code enum
static_assert(std::is_error_code_enum_v<errc>);

// result<T> is std::expected<T, std::error_code>
static_assert(std::is_same_v<result<int>, std::expected<int, std::error_code>>);
static_assert(std::is_same_v<result<void>, std::expected<void, std::error_code>>);

// Error codes have expected values (matching ESP-IDF)
static_assert(std::to_underlying(errc::fail) == ESP_FAIL);
static_assert(std::to_underlying(errc::no_mem) == ESP_ERR_NO_MEM);
static_assert(std::to_underlying(errc::invalid_arg) == ESP_ERR_INVALID_ARG);
static_assert(std::to_underlying(errc::invalid_state) == ESP_ERR_INVALID_STATE);
static_assert(std::to_underlying(errc::invalid_size) == ESP_ERR_INVALID_SIZE);
static_assert(std::to_underlying(errc::not_found) == ESP_ERR_NOT_FOUND);
static_assert(std::to_underlying(errc::not_supported) == ESP_ERR_NOT_SUPPORTED);
static_assert(std::to_underlying(errc::timeout) == ESP_ERR_TIMEOUT);
static_assert(std::to_underlying(errc::invalid_response) == ESP_ERR_INVALID_RESPONSE);
static_assert(std::to_underlying(errc::invalid_crc) == ESP_ERR_INVALID_CRC);
static_assert(std::to_underlying(errc::invalid_version) == ESP_ERR_INVALID_VERSION);
static_assert(std::to_underlying(errc::invalid_mac) == ESP_ERR_INVALID_MAC);
static_assert(std::to_underlying(errc::not_finished) == ESP_ERR_NOT_FINISHED);
static_assert(std::to_underlying(errc::not_allowed) == ESP_ERR_NOT_ALLOWED);

// Note: make_error_code is constexpr for errc, but std::error_code::value()
// is not constexpr in the C++ standard library (proposed in P1195, not yet adopted),
// so we verify this at runtime instead (see "make_error_code from errc" test).

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("error_category has correct name", "[idfxx][error]") {
    const auto& cat = default_category();
    TEST_ASSERT_EQUAL_STRING("idfxx::Error", cat.name());
}

TEST_CASE("error_category messages are not empty", "[idfxx][error]") {
    const auto& cat = default_category();

    // Test all defined error codes have non-empty messages
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::fail)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::no_mem)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::invalid_arg)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::invalid_state)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::invalid_size)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::not_found)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::not_supported)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::timeout)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::invalid_response)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::invalid_crc)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::invalid_version)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::invalid_mac)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::not_finished)).empty());
    TEST_ASSERT_FALSE(cat.message(std::to_underlying(errc::not_allowed)).empty());
}

TEST_CASE("error_category message content is correct", "[idfxx][error]") {
    const auto& cat = default_category();

    TEST_ASSERT_EQUAL_STRING("Generic failure", cat.message(std::to_underlying(errc::fail)).c_str());
    TEST_ASSERT_EQUAL_STRING("Out of memory", cat.message(std::to_underlying(errc::no_mem)).c_str());
    TEST_ASSERT_EQUAL_STRING("Invalid argument", cat.message(std::to_underlying(errc::invalid_arg)).c_str());
    TEST_ASSERT_EQUAL_STRING("Operation timed out", cat.message(std::to_underlying(errc::timeout)).c_str());
}

TEST_CASE("make_error_code from errc", "[idfxx][error]") {
    std::error_code ec = make_error_code(errc::no_mem);

    TEST_ASSERT_EQUAL(std::to_underlying(errc::no_mem), ec.value());
    TEST_ASSERT_EQUAL_STRING("idfxx::Error", ec.category().name());
    TEST_ASSERT_EQUAL_STRING("Out of memory", ec.message().c_str());
}

TEST_CASE("make_error_code from esp_err_t", "[idfxx][error]") {
    // Test mapping of ESP-IDF error codes
    std::error_code ec_no_mem = make_error_code(ESP_ERR_NO_MEM);
    TEST_ASSERT_EQUAL(std::to_underlying(errc::no_mem), ec_no_mem.value());

    std::error_code ec_invalid_arg = make_error_code(ESP_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), ec_invalid_arg.value());

    std::error_code ec_timeout = make_error_code(ESP_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), ec_timeout.value());

    std::error_code ec_not_found = make_error_code(ESP_ERR_NOT_FOUND);
    TEST_ASSERT_EQUAL(std::to_underlying(errc::not_found), ec_not_found.value());
}

TEST_CASE("make_error_code maps unknown esp_err_t to fail", "[idfxx][error]") {
    // Use an arbitrary unknown error code
    std::error_code ec = make_error_code(static_cast<esp_err_t>(0x9999));
    TEST_ASSERT_EQUAL(std::to_underlying(errc::fail), ec.value());
}

TEST_CASE("errc converts implicitly to std::error_code", "[idfxx][error]") {
    std::error_code ec = errc::invalid_state;

    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), ec.value());
    TEST_ASSERT_EQUAL_STRING("idfxx::Error", ec.category().name());
}

TEST_CASE("wrap with ESP_OK returns success", "[idfxx][error]") {
    result<void> r = wrap(ESP_OK);

    TEST_ASSERT_TRUE(r.has_value());
}

TEST_CASE("wrap with error returns unexpected", "[idfxx][error]") {
    result<void> r = wrap(ESP_ERR_NO_MEM);

    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::no_mem), r.error().value());
}

TEST_CASE("result with value", "[idfxx][error]") {
    result<int> r = 42;

    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL(42, r.value());
}

TEST_CASE("result with error", "[idfxx][error]") {
    result<int> r = error(errc::timeout);

    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), r.error().value());
}

TEST_CASE("result value_or returns default on error", "[idfxx][error]") {
    result<int> r = error(errc::fail);

    TEST_ASSERT_EQUAL(-1, r.value_or(-1));
}

TEST_CASE("error from errc returns unexpected with correct error code", "[idfxx][error]") {
    auto e = error(errc::no_mem);
    // Verify the error value matches
    result<int> r = e;
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::no_mem), r.error().value());
    TEST_ASSERT_EQUAL_STRING("idfxx::Error", r.error().category().name());
}

TEST_CASE("error from esp_err_t returns unexpected with correct error code", "[idfxx][error]") {
    auto e = error(ESP_ERR_TIMEOUT);
    result<void> r = e;
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), r.error().value());
}

TEST_CASE("result value_or returns value on success", "[idfxx][error]") {
    result<int> r = 100;

    TEST_ASSERT_EQUAL(100, r.value_or(-1));
}

TEST_CASE("error codes can be compared", "[idfxx][error]") {
    std::error_code ec1 = errc::no_mem;
    std::error_code ec2 = errc::no_mem;
    std::error_code ec3 = errc::timeout;

    TEST_ASSERT_TRUE(ec1 == ec2);
    TEST_ASSERT_FALSE(ec1 == ec3);
    TEST_ASSERT_TRUE(ec1 != ec3);
}

TEST_CASE("error from std::error_code preserves value and category", "[idfxx][error]") {
    std::error_code ec = errc::timeout;
    auto e = error(ec);
    result<int> r = e;
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), r.error().value());
    TEST_ASSERT_EQUAL_STRING("idfxx::Error", r.error().category().name());
}

TEST_CASE("error from std::error_code propagates between result types", "[idfxx][error]") {
    // Simulate propagating an error from result<void> to result<int>
    result<void> r1 = error(errc::no_mem);
    TEST_ASSERT_FALSE(r1.has_value());

    result<int> r2 = error(r1.error());
    TEST_ASSERT_FALSE(r2.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::no_mem), r2.error().value());
    TEST_ASSERT_EQUAL_STRING("idfxx::Error", r2.error().category().name());
}

TEST_CASE("error code evaluates to true when set", "[idfxx][error]") {
    std::error_code ec_error = errc::fail;
    std::error_code ec_success;

    TEST_ASSERT_TRUE(static_cast<bool>(ec_error));
    TEST_ASSERT_FALSE(static_cast<bool>(ec_success));
}
