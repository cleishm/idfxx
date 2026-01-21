// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx nvs
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/nvs"
#include "unity.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <type_traits>
#include <utility>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// nvs is not default constructible (must use make() factory)
static_assert(!std::is_default_constructible_v<nvs>);

// nvs is not copyable (unique ownership of handle)
static_assert(!std::is_copy_constructible_v<nvs>);
static_assert(!std::is_copy_assignable_v<nvs>);

// nvs is not movable (fixed handle ownership)
static_assert(!std::is_move_constructible_v<nvs>);
static_assert(!std::is_move_assignable_v<nvs>);

// sized_integral concept accepts all expected types
static_assert(sized_integral<uint8_t>);
static_assert(sized_integral<int8_t>);
static_assert(sized_integral<uint16_t>);
static_assert(sized_integral<int16_t>);
static_assert(sized_integral<uint32_t>);
static_assert(sized_integral<int32_t>);
static_assert(sized_integral<uint64_t>);
static_assert(sized_integral<int64_t>);

// sized_integral concept rejects non-integral types
static_assert(!sized_integral<float>);
static_assert(!sized_integral<double>);
static_assert(!sized_integral<bool>);
static_assert(!sized_integral<char>);
static_assert(!sized_integral<void*>);

// nvs::errc is an error_code_enum
static_assert(std::is_error_code_enum_v<nvs::errc>);

// Error codes have expected values (matching ESP-IDF)
static_assert(std::to_underlying(nvs::errc::not_found) == ESP_ERR_NVS_NOT_FOUND);
static_assert(std::to_underlying(nvs::errc::type_mismatch) == ESP_ERR_NVS_TYPE_MISMATCH);
static_assert(std::to_underlying(nvs::errc::read_only) == ESP_ERR_NVS_READ_ONLY);
static_assert(std::to_underlying(nvs::errc::not_enough_space) == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
static_assert(std::to_underlying(nvs::errc::invalid_name) == ESP_ERR_NVS_INVALID_NAME);
static_assert(std::to_underlying(nvs::errc::invalid_handle) == ESP_ERR_NVS_INVALID_HANDLE);
static_assert(std::to_underlying(nvs::errc::remove_failed) == ESP_ERR_NVS_REMOVE_FAILED);
static_assert(std::to_underlying(nvs::errc::key_too_long) == ESP_ERR_NVS_KEY_TOO_LONG);
static_assert(std::to_underlying(nvs::errc::invalid_state) == ESP_ERR_NVS_INVALID_STATE);
static_assert(std::to_underlying(nvs::errc::invalid_length) == ESP_ERR_NVS_INVALID_LENGTH);
static_assert(std::to_underlying(nvs::errc::no_free_pages) == ESP_ERR_NVS_NO_FREE_PAGES);
static_assert(std::to_underlying(nvs::errc::value_too_long) == ESP_ERR_NVS_VALUE_TOO_LONG);
static_assert(std::to_underlying(nvs::errc::part_not_found) == ESP_ERR_NVS_PART_NOT_FOUND);

// nvs key is the correct size
static_assert(nvs::flash::key_size == NVS_KEY_SIZE);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("nvs error_category has correct name", "[idfxx][nvs]") {
    TEST_ASSERT_EQUAL_STRING("nvs::Error", nvs_category().name());
}

TEST_CASE("nvs error_category produces messages", "[idfxx][nvs]") {
    std::error_code ec = nvs::errc::not_found;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = nvs::errc::type_mismatch;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = nvs::errc::read_only;
    TEST_ASSERT_TRUE(ec.message().length() > 0);
}

TEST_CASE("make_error_code creates correct error codes", "[idfxx][nvs]") {
    std::error_code ec = make_error_code(nvs::errc::not_found);
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), ec.value());
    TEST_ASSERT_EQUAL_STRING("nvs::Error", ec.category().name());

    ec = make_error_code(nvs::errc::key_too_long);
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::key_too_long), ec.value());
}

// =============================================================================
// Hardware-dependent tests
// These tests interact with actual NVS flash storage
// =============================================================================

// Helper to ensure NVS is initialized before tests
static void ensure_nvs_init() {
    auto result = nvs::flash::try_init();
    if (!result && (result.error() == nvs::errc::no_free_pages ||
                    result.error() == nvs::errc::new_version_found)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs::flash::try_init();
    }
    TEST_ASSERT_TRUE(result.has_value());
}

TEST_CASE("nvs::make with valid namespace succeeds", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto result = nvs::make("test_ns");
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result.value()->is_writeable());
}

TEST_CASE("nvs::make with read_only flag", "[idfxx][nvs]") {
    ensure_nvs_init();

    // First create the namespace by opening it read-write
    {
        auto rw = nvs::make("test_ro");
        TEST_ASSERT_TRUE(rw.has_value());
        TEST_ASSERT_TRUE(rw.value()->is_writeable());
    }

    // Now open read-only
    auto ro = nvs::make("test_ro", true);
    TEST_ASSERT_TRUE(ro.has_value());
    TEST_ASSERT_FALSE(ro.value()->is_writeable());
}

TEST_CASE("nvs::make with invalid namespace name fails", "[idfxx][nvs]") {
    ensure_nvs_init();

    // Empty namespace name is invalid
    auto result = nvs::make("");
    TEST_ASSERT_FALSE(result.has_value());

    // Namespace name too long (max 15 chars)
    auto result2 = nvs::make("this_namespace_name_is_way_too_long");
    TEST_ASSERT_FALSE(result2.has_value());
}

TEST_CASE("nvs set and get string", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_str");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // Set a string
    auto set_result = nvs.try_set_string("key1", "hello world");
    TEST_ASSERT_TRUE(set_result.has_value());

    // Commit changes
    auto commit_result = nvs.try_commit();
    TEST_ASSERT_TRUE(commit_result.has_value());

    // Get the string back
    auto get_result = nvs.try_get_string("key1");
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_EQUAL_STRING("hello world", get_result.value().c_str());
}

TEST_CASE("nvs get non-existent key returns not_found", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_nf");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    auto result = nvs.try_get_string("nonexistent_key");
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), result.error().value());
}

TEST_CASE("nvs set and get blob", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_blob");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // Set a blob using vector
    std::vector<uint8_t> data{0x01, 0x02, 0x03, 0x04, 0x05};
    auto set_result = nvs.try_set_blob("blob1", data);
    TEST_ASSERT_TRUE(set_result.has_value());

    // Commit
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Get the blob back
    auto get_result = nvs.try_get_blob("blob1");
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_EQUAL(5, get_result.value().size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), get_result.value().data(), 5);
}

TEST_CASE("nvs set and get blob with raw pointer", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_blobp");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // Set a blob using raw pointer
    uint8_t data[]{0xAA, 0xBB, 0xCC};
    auto set_result = nvs.try_set_blob("blob2", data, sizeof(data));
    TEST_ASSERT_TRUE(set_result.has_value());

    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Get it back
    auto get_result = nvs.try_get_blob("blob2");
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_EQUAL(3, get_result.value().size());
    TEST_ASSERT_EQUAL_HEX8(0xAA, get_result.value()[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, get_result.value()[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, get_result.value()[2]);
}

TEST_CASE("nvs set and get integer values", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_int");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // uint8_t
    TEST_ASSERT_TRUE(nvs.try_set_value<uint8_t>("u8", 255).has_value());
    auto u8_result = nvs.try_get_value<uint8_t>("u8");
    TEST_ASSERT_TRUE(u8_result.has_value());
    TEST_ASSERT_EQUAL_UINT8(255, u8_result.value());

    // int8_t
    TEST_ASSERT_TRUE(nvs.try_set_value<int8_t>("i8", -128).has_value());
    auto i8_result = nvs.try_get_value<int8_t>("i8");
    TEST_ASSERT_TRUE(i8_result.has_value());
    TEST_ASSERT_EQUAL_INT8(-128, i8_result.value());

    // uint16_t
    TEST_ASSERT_TRUE(nvs.try_set_value<uint16_t>("u16", 65535).has_value());
    auto u16_result = nvs.try_get_value<uint16_t>("u16");
    TEST_ASSERT_TRUE(u16_result.has_value());
    TEST_ASSERT_EQUAL_UINT16(65535, u16_result.value());

    // int16_t
    TEST_ASSERT_TRUE(nvs.try_set_value<int16_t>("i16", -32768).has_value());
    auto i16_result = nvs.try_get_value<int16_t>("i16");
    TEST_ASSERT_TRUE(i16_result.has_value());
    TEST_ASSERT_EQUAL_INT16(-32768, i16_result.value());

    // uint32_t
    TEST_ASSERT_TRUE(nvs.try_set_value<uint32_t>("u32", 0xDEADBEEF).has_value());
    auto u32_result = nvs.try_get_value<uint32_t>("u32");
    TEST_ASSERT_TRUE(u32_result.has_value());
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, u32_result.value());

    // int32_t
    TEST_ASSERT_TRUE(nvs.try_set_value<int32_t>("i32", -2147483648).has_value());
    auto i32_result = nvs.try_get_value<int32_t>("i32");
    TEST_ASSERT_TRUE(i32_result.has_value());
    TEST_ASSERT_EQUAL_INT32(-2147483648, i32_result.value());

    // uint64_t
    TEST_ASSERT_TRUE(nvs.try_set_value<uint64_t>("u64", 0xDEADBEEFCAFEBABE).has_value());
    auto u64_result = nvs.try_get_value<uint64_t>("u64");
    TEST_ASSERT_TRUE(u64_result.has_value());
    TEST_ASSERT_EQUAL(0xDEADBEEFCAFEBABE, u64_result.value());

    // int64_t
    TEST_ASSERT_TRUE(nvs.try_set_value<int64_t>("i64", -9223372036854775807LL - 1).has_value());
    auto i64_result = nvs.try_get_value<int64_t>("i64");
    TEST_ASSERT_TRUE(i64_result.has_value());
    TEST_ASSERT_EQUAL(-9223372036854775807LL - 1, i64_result.value());

    TEST_ASSERT_TRUE(nvs.try_commit().has_value());
}

TEST_CASE("nvs erase key", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_erase");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // Set a value
    TEST_ASSERT_TRUE(nvs.try_set_string("to_erase", "value").has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Verify it exists
    TEST_ASSERT_TRUE(nvs.try_get_string("to_erase").has_value());

    // Erase it
    TEST_ASSERT_TRUE(nvs.try_erase("to_erase").has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Verify it's gone
    auto result = nvs.try_get_string("to_erase");
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), result.error().value());
}

TEST_CASE("nvs erase non-existent key returns not_found", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_ernf");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    auto result = nvs.try_erase("does_not_exist");
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), result.error().value());
}

TEST_CASE("nvs erase_all", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_erall");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // Set multiple values
    TEST_ASSERT_TRUE(nvs.try_set_string("key1", "value1").has_value());
    TEST_ASSERT_TRUE(nvs.try_set_string("key2", "value2").has_value());
    TEST_ASSERT_TRUE(nvs.try_set_value<uint32_t>("key3", 42).has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Erase all
    TEST_ASSERT_TRUE(nvs.try_erase_all().has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Verify all are gone
    TEST_ASSERT_FALSE(nvs.try_get_string("key1").has_value());
    TEST_ASSERT_FALSE(nvs.try_get_string("key2").has_value());
    TEST_ASSERT_FALSE(nvs.try_get_value<uint32_t>("key3").has_value());
}

TEST_CASE("nvs read-only handle cannot write", "[idfxx][nvs]") {
    ensure_nvs_init();

    // First create the namespace
    {
        auto rw = nvs::make("test_ro_w");
        TEST_ASSERT_TRUE(rw.has_value());
        TEST_ASSERT_TRUE(rw.value()->try_set_string("key", "value").has_value());
        TEST_ASSERT_TRUE(rw.value()->try_commit().has_value());
    }

    // Open read-only
    auto ro = nvs::make("test_ro_w", true);
    TEST_ASSERT_TRUE(ro.has_value());
    auto& nvs = *ro.value();

    // Reading should work
    auto get_result = nvs.try_get_string("key");
    TEST_ASSERT_TRUE(get_result.has_value());

    // Writing should fail with read_only error
    auto set_result = nvs.try_set_string("key", "new_value");
    TEST_ASSERT_FALSE(set_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::read_only), set_result.error().value());

    // Commit should fail
    auto commit_result = nvs.try_commit();
    TEST_ASSERT_FALSE(commit_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::read_only), commit_result.error().value());

    // Erase should fail
    auto erase_result = nvs.try_erase("key");
    TEST_ASSERT_FALSE(erase_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::read_only), erase_result.error().value());

    // Erase all should fail
    auto erase_all_result = nvs.try_erase_all();
    TEST_ASSERT_FALSE(erase_all_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::read_only), erase_all_result.error().value());

    // Set blob should fail
    std::vector<uint8_t> data{1, 2, 3};
    auto blob_result = nvs.try_set_blob("blob", data);
    TEST_ASSERT_FALSE(blob_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::read_only), blob_result.error().value());

    // Set value should fail
    auto value_result = nvs.try_set_value<uint32_t>("val", 42);
    TEST_ASSERT_FALSE(value_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::read_only), value_result.error().value());
}

TEST_CASE("nvs type mismatch error", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_type");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // Ensure clean namespace by erasing any existing keys
    TEST_ASSERT_TRUE(nvs.try_erase_all().has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Store as string
    TEST_ASSERT_TRUE(nvs.try_set_string("mixed", "hello").has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Try to read as integer - should fail
    // ESP-IDF returns not_found when a key exists with a different type
    auto result = nvs.try_get_value<uint32_t>("mixed");
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), result.error().value());
}

TEST_CASE("nvs key too long error", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_keylen");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // NVS key max length is 15 characters
    auto result = nvs.try_set_string("this_key_name_is_definitely_too_long", "value");
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::key_too_long), result.error().value());
}

TEST_CASE("nvs empty string value", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_empty");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // Set empty string
    TEST_ASSERT_TRUE(nvs.try_set_string("empty", "").has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Get it back
    auto result = nvs.try_get_string("empty");
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL_STRING("", result.value().c_str());
    TEST_ASSERT_EQUAL(0, result.value().length());
}

TEST_CASE("nvs empty blob value", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_eblob");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle.value();

    // Set empty blob
    std::vector<uint8_t> empty_data;
    TEST_ASSERT_TRUE(nvs.try_set_blob("empty", empty_data).has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Get it back
    auto result = nvs.try_get_blob("empty");
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(0, result.value().size());
}
