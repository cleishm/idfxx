// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx ota
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/ota"
#include "unity.h"

#include <type_traits>
#include <utility>

using namespace idfxx;
using namespace idfxx::ota;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// update is not default constructible (must use make() factory or constructor)
static_assert(!std::is_default_constructible_v<update>);

// update is not copyable (unique ownership of handle)
static_assert(!std::is_copy_constructible_v<update>);
static_assert(!std::is_copy_assignable_v<update>);

// update is move-only
static_assert(std::is_move_constructible_v<update>);
static_assert(std::is_move_assignable_v<update>);

// app_description is not default constructible (constructed only via try_partition_description)
static_assert(!std::is_default_constructible_v<app_description>);

// app_description is copyable
static_assert(std::is_copy_constructible_v<app_description>);
static_assert(std::is_copy_assignable_v<app_description>);

// ota::errc is an error_code_enum
static_assert(std::is_error_code_enum_v<ota::errc>);

// image_state enum values
static_assert(std::to_underlying(image_state::new_image) == 0x0);
static_assert(std::to_underlying(image_state::pending_verify) == 0x1);
static_assert(std::to_underlying(image_state::valid) == 0x2);
static_assert(std::to_underlying(image_state::invalid) == 0x3);
static_assert(std::to_underlying(image_state::aborted) == 0x4);
static_assert(std::to_underlying(image_state::undefined) == 0xFFFFFFFFU);

// sequential_erase_tag is an empty tag type
static_assert(std::is_empty_v<sequential_erase_tag>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("ota error_category has correct name", "[idfxx][ota]") {
    TEST_ASSERT_EQUAL_STRING("ota::Error", ota_category().name());
}

TEST_CASE("ota error_category produces messages", "[idfxx][ota]") {
    std::error_code ec = ota::errc::partition_conflict;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = ota::errc::validate_failed;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = ota::errc::rollback_failed;
    TEST_ASSERT_TRUE(ec.message().length() > 0);
}

TEST_CASE("make_error_code creates correct error codes", "[idfxx][ota]") {
    std::error_code ec = make_error_code(ota::errc::partition_conflict);
    TEST_ASSERT_EQUAL(std::to_underlying(ota::errc::partition_conflict), ec.value());
    TEST_ASSERT_EQUAL_STRING("ota::Error", ec.category().name());

    ec = make_error_code(ota::errc::rollback_invalid_state);
    TEST_ASSERT_EQUAL(std::to_underlying(ota::errc::rollback_invalid_state), ec.value());
}

// =============================================================================
// Hardware-dependent tests
// These tests interact with actual OTA partitions
// =============================================================================

TEST_CASE("ota running_partition returns a valid partition", "[idfxx][ota]") {
    auto result = try_running_partition();
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->idf_handle() != nullptr);
    TEST_ASSERT_EQUAL(static_cast<int>(partition::type::app), static_cast<int>(result->type()));
}

TEST_CASE("ota boot_partition returns a valid partition", "[idfxx][ota]") {
    auto result = try_boot_partition();
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->idf_handle() != nullptr);
}

TEST_CASE("ota partition_description for running partition", "[idfxx][ota]") {
    auto part_result = try_running_partition();
    TEST_ASSERT_TRUE(part_result.has_value());

    auto desc_result = try_partition_description(*part_result);
    TEST_ASSERT_TRUE(desc_result.has_value());
    TEST_ASSERT_TRUE(desc_result->version().length() > 0);
    TEST_ASSERT_TRUE(desc_result->project_name().length() > 0);
    TEST_ASSERT_TRUE(desc_result->idf_ver().length() > 0);
}

TEST_CASE("ota app_partition_count returns non-zero", "[idfxx][ota]") {
    size_t count = app_partition_count();
    TEST_ASSERT_GREATER_THAN(0, count);
}

TEST_CASE("ota rollback_possible returns a boolean", "[idfxx][ota]") {
    // Just verify it doesn't crash - result depends on partition layout
    (void)rollback_possible();
}

TEST_CASE("ota partition_state for running partition", "[idfxx][ota]") {
    auto part_result = try_running_partition();
    TEST_ASSERT_TRUE(part_result.has_value());

    auto state_result = try_partition_state(*part_result);
    // May fail if no OTA data partition, but should not crash
    if (state_result.has_value()) {
        auto state = *state_result;
        // State should be one of the valid values
        TEST_ASSERT_TRUE(state == image_state::new_image || state == image_state::pending_verify ||
                         state == image_state::valid || state == image_state::invalid ||
                         state == image_state::aborted || state == image_state::undefined);
    }
}

// =============================================================================
// Exception-based API tests
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("ota running_partition via exception API", "[idfxx][ota]") {
    auto part = running_partition();
    TEST_ASSERT_TRUE(part.idf_handle() != nullptr);
    TEST_ASSERT_EQUAL(static_cast<int>(partition::type::app), static_cast<int>(part.type()));
}

TEST_CASE("ota boot_partition via exception API", "[idfxx][ota]") {
    auto part = boot_partition();
    TEST_ASSERT_TRUE(part.idf_handle() != nullptr);
}

TEST_CASE("ota partition_description via exception API", "[idfxx][ota]") {
    auto part = running_partition();
    auto desc = partition_description(part);
    TEST_ASSERT_TRUE(desc.version().length() > 0);
    TEST_ASSERT_TRUE(desc.project_name().length() > 0);
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
