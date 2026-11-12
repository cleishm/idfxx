// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx crypto error handling
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "../src/psa.hpp"
#include "idfxx/crypto/error"
#include "unity.h"

#include <algorithm>
#include <new>
#include <psa/crypto.h>
#include <string>
#include <system_error>
#include <utility>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(std::is_error_code_enum_v<crypto::errc>);

// Codes are small positive values
static_assert(std::to_underlying(crypto::errc::generic_error) == 1);
static_assert(std::to_underlying(crypto::errc::invalid_handle) == 20);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

namespace {

constexpr crypto::errc all_codes[] = {
    crypto::errc::generic_error,         crypto::errc::not_supported,
    crypto::errc::not_permitted,         crypto::errc::buffer_too_small,
    crypto::errc::already_exists,        crypto::errc::does_not_exist,
    crypto::errc::bad_state,             crypto::errc::invalid_argument,
    crypto::errc::insufficient_storage,  crypto::errc::communication_failure,
    crypto::errc::storage_failure,       crypto::errc::data_corrupt,
    crypto::errc::data_invalid,          crypto::errc::hardware_failure,
    crypto::errc::corruption_detected,   crypto::errc::insufficient_entropy,
    crypto::errc::invalid_signature,     crypto::errc::invalid_padding,
    crypto::errc::insufficient_data,     crypto::errc::invalid_handle,
};

} // namespace

TEST_CASE("crypto error category name", "[idfxx][crypto]") {
    TEST_ASSERT_EQUAL_STRING("crypto::Error", crypto_category().name());
    TEST_ASSERT_EQUAL_PTR(&crypto_category(), &crypto_category());
}

TEST_CASE("crypto error codes have messages", "[idfxx][crypto]") {
    for (auto code : all_codes) {
        std::error_code ec = code;
        TEST_ASSERT_EQUAL_PTR(&crypto_category(), &ec.category());
        auto msg = ec.message();
        TEST_ASSERT_FALSE(msg.empty());
        TEST_ASSERT_TRUE(msg.find("Unknown") == std::string::npos);
    }
    TEST_ASSERT_EQUAL_STRING(
        "Invalid signature", std::error_code(crypto::errc::invalid_signature).message().c_str()
    );
    TEST_ASSERT_TRUE(crypto_category().message(9999).find("Unknown") != std::string::npos);
}

TEST_CASE("crypto error codes map to std::errc conditions", "[idfxx][crypto]") {
    struct synonym {
        crypto::errc code;
        std::errc posix;
    };
    constexpr synonym synonyms[] = {
        {crypto::errc::not_supported, std::errc::not_supported},
        {crypto::errc::not_permitted, std::errc::operation_not_permitted},
        {crypto::errc::buffer_too_small, std::errc::value_too_large},
        {crypto::errc::already_exists, std::errc::file_exists},
        {crypto::errc::does_not_exist, std::errc::no_such_file_or_directory},
        {crypto::errc::invalid_argument, std::errc::invalid_argument},
        {crypto::errc::insufficient_storage, std::errc::no_space_on_device},
        {crypto::errc::storage_failure, std::errc::io_error},
        {crypto::errc::hardware_failure, std::errc::io_error},
        {crypto::errc::invalid_handle, std::errc::bad_file_descriptor},
    };
    for (auto [code, posix] : synonyms) {
        std::error_code ec = code;
        TEST_ASSERT_TRUE(ec == posix);
        TEST_ASSERT_TRUE(ec == code);
    }
    std::error_code not_supported = crypto::errc::not_supported;
    TEST_ASSERT_FALSE(not_supported == std::errc::invalid_argument);

    // Codes without a POSIX synonym canonicalize within the crypto category
    for (auto code : all_codes) {
        if (std::ranges::any_of(synonyms, [code](auto s) { return s.code == code; })) {
            continue;
        }
        auto cond = crypto_category().default_error_condition(std::to_underlying(code));
        TEST_ASSERT_EQUAL_PTR(&crypto_category(), &cond.category());
        TEST_ASSERT_EQUAL(std::to_underlying(code), cond.value());
    }
    TEST_ASSERT_FALSE(std::error_code(crypto::errc::bad_state) == std::errc::invalid_argument);
}

TEST_CASE("crypto error helper creates unexpected", "[idfxx][crypto]") {
    result<int> r = error(crypto::errc::hardware_failure);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == crypto::errc::hardware_failure);
}

TEST_CASE("crypto PSA status mapping", "[idfxx][crypto]") {
    using crypto::errc;
    using crypto::detail::psa_error_code;

    TEST_ASSERT_FALSE(psa_error_code(PSA_SUCCESS));

    const std::pair<psa_status_t, errc> mapping[] = {
        {PSA_ERROR_GENERIC_ERROR, errc::generic_error},
        {PSA_ERROR_NOT_SUPPORTED, errc::not_supported},
        {PSA_ERROR_NOT_PERMITTED, errc::not_permitted},
        {PSA_ERROR_BUFFER_TOO_SMALL, errc::buffer_too_small},
        {PSA_ERROR_ALREADY_EXISTS, errc::already_exists},
        {PSA_ERROR_DOES_NOT_EXIST, errc::does_not_exist},
        {PSA_ERROR_BAD_STATE, errc::bad_state},
        {PSA_ERROR_INVALID_ARGUMENT, errc::invalid_argument},
        {PSA_ERROR_INSUFFICIENT_STORAGE, errc::insufficient_storage},
        {PSA_ERROR_COMMUNICATION_FAILURE, errc::communication_failure},
        {PSA_ERROR_STORAGE_FAILURE, errc::storage_failure},
        {PSA_ERROR_DATA_CORRUPT, errc::data_corrupt},
        {PSA_ERROR_DATA_INVALID, errc::data_invalid},
        {PSA_ERROR_HARDWARE_FAILURE, errc::hardware_failure},
        {PSA_ERROR_CORRUPTION_DETECTED, errc::corruption_detected},
        {PSA_ERROR_INSUFFICIENT_ENTROPY, errc::insufficient_entropy},
        {PSA_ERROR_INVALID_SIGNATURE, errc::invalid_signature},
        {PSA_ERROR_INVALID_PADDING, errc::invalid_padding},
        {PSA_ERROR_INSUFFICIENT_DATA, errc::insufficient_data},
        {PSA_ERROR_INVALID_HANDLE, errc::invalid_handle},
    };
    for (auto [status, code] : mapping) {
        TEST_ASSERT_TRUE(psa_error_code(status) == code);
    }

    // Statuses without a specific code
    TEST_ASSERT_TRUE(psa_error_code(static_cast<psa_status_t>(-1)) == errc::generic_error);
    TEST_ASSERT_TRUE(psa_error_code(static_cast<psa_status_t>(-9999)) == errc::generic_error);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("crypto PSA out-of-memory raises bad_alloc", "[idfxx][crypto]") {
    bool thrown = false;
    try {
        (void)crypto::detail::psa_error_code(PSA_ERROR_INSUFFICIENT_MEMORY);
    } catch (const std::bad_alloc&) {
        thrown = true;
    }
    TEST_ASSERT_TRUE(thrown);
}
#endif

TEST_CASE("crypto PSA initialization succeeds and is repeatable", "[idfxx][crypto]") {
    TEST_ASSERT_TRUE(crypto::detail::psa_init().has_value());
    TEST_ASSERT_TRUE(crypto::detail::psa_init().has_value());
}
