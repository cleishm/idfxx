// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx partition
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/partition"
#include "unity.h"

#include <type_traits>
#include <utility>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// partition is not default constructible
static_assert(!std::is_default_constructible_v<partition>);

// partition is copyable (non-owning)
static_assert(std::is_copy_constructible_v<partition>);
static_assert(std::is_copy_assignable_v<partition>);

// partition is movable
static_assert(std::is_move_constructible_v<partition>);
static_assert(std::is_move_assignable_v<partition>);

// mmap_handle is default constructible
static_assert(std::is_default_constructible_v<partition::mmap_handle>);

// mmap_handle is move-only
static_assert(!std::is_copy_constructible_v<partition::mmap_handle>);
static_assert(!std::is_copy_assignable_v<partition::mmap_handle>);
static_assert(std::is_move_constructible_v<partition::mmap_handle>);
static_assert(std::is_move_assignable_v<partition::mmap_handle>);

// app_ota helper produces correct values
static_assert(app_ota(0) == partition::subtype::app_ota_0);
static_assert(app_ota(1) == partition::subtype::app_ota_1);
static_assert(app_ota(15) == partition::subtype::app_ota_15);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("partition find_all returns results", "[idfxx][partition]") {
    auto parts = partition::find_all();
    TEST_ASSERT_TRUE(parts.size() > 0);
}

TEST_CASE("partition find_all with type filter", "[idfxx][partition]") {
    auto app_parts = partition::find_all(partition::type::app);
    TEST_ASSERT_TRUE(app_parts.size() > 0);
    for (const auto& p : app_parts) {
        TEST_ASSERT_EQUAL(std::to_underlying(partition::type::app), std::to_underlying(p.type()));
    }
}

TEST_CASE("partition try_find ota_0 app", "[idfxx][partition]") {
    auto result = partition::try_find(partition::type::app, partition::subtype::app_ota_0);
    TEST_ASSERT_TRUE(result.has_value());

    auto& p = *result;
    TEST_ASSERT_NOT_NULL(p.idf_handle());
    TEST_ASSERT_EQUAL(std::to_underlying(partition::type::app), std::to_underlying(p.type()));
    TEST_ASSERT_EQUAL(std::to_underlying(partition::subtype::app_ota_0), std::to_underlying(p.subtype()));
    TEST_ASSERT_TRUE(p.size() > 0);
    TEST_ASSERT_TRUE(p.erase_size() > 0);
    TEST_ASSERT_FALSE(p.label().empty());
}

TEST_CASE("partition try_find nonexistent returns not_found", "[idfxx][partition]") {
    auto result = partition::try_find(partition::type::data, partition::subtype::any, "nonexistent_label_xyz");
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::not_found), result.error().value());
}

TEST_CASE("partition try_find by label", "[idfxx][partition]") {
    // The ota_0 app partition typically has a label
    auto all = partition::find_all(partition::type::app);
    TEST_ASSERT_TRUE(all.size() > 0);

    auto label = all[0].label();
    auto result = partition::try_find(label);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->label() == label);
}

TEST_CASE("partition copy semantics", "[idfxx][partition]") {
    auto result = partition::try_find(partition::type::app, partition::subtype::app_ota_0);
    TEST_ASSERT_TRUE(result.has_value());

    partition p1 = *result;
    partition p2 = p1;

    TEST_ASSERT_TRUE(p1 == p2);
    TEST_ASSERT_EQUAL(p1.idf_handle(), p2.idf_handle());
    TEST_ASSERT_TRUE(p1.label() == p2.label());
}

TEST_CASE("partition try_read on app partition", "[idfxx][partition]") {
    auto result = partition::try_find(partition::type::app, partition::subtype::app_ota_0);
    TEST_ASSERT_TRUE(result.has_value());

    // Read first 16 bytes
    uint8_t buf[16] = {};
    auto read_result = result->try_read(0, buf, sizeof(buf));
    TEST_ASSERT_TRUE(read_result.has_value());

    // At least some bytes should be non-zero (it's an app image)
    bool has_nonzero = false;
    for (auto b : buf) {
        if (b != 0) {
            has_nonzero = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(has_nonzero);
}

TEST_CASE("partition try_read with span", "[idfxx][partition]") {
    auto result = partition::try_find(partition::type::app, partition::subtype::app_ota_0);
    TEST_ASSERT_TRUE(result.has_value());

    std::array<uint8_t, 16> buf{};
    std::span<uint8_t> s{buf};
    auto read_result = result->try_read(0, s);
    TEST_ASSERT_TRUE(read_result.has_value());
}

TEST_CASE("partition check_identity with same partition returns true", "[idfxx][partition]") {
    auto result = partition::try_find(partition::type::app, partition::subtype::app_ota_0);
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->check_identity(*result));
}

TEST_CASE("partition mmap_handle default is invalid", "[idfxx][partition]") {
    partition::mmap_handle handle;
    TEST_ASSERT_FALSE(static_cast<bool>(handle));
    TEST_ASSERT_NULL(handle.data());
    TEST_ASSERT_EQUAL(0, handle.size());
}

TEST_CASE("partition try_sha256 on ota_0 app", "[idfxx][partition]") {
    auto result = partition::try_find(partition::type::app, partition::subtype::app_ota_0);
    TEST_ASSERT_TRUE(result.has_value());

    auto hash_result = result->try_sha256();
    TEST_ASSERT_TRUE(hash_result.has_value());

    // Hash should be non-zero for a valid app partition
    auto& hash = *hash_result;
    bool has_nonzero = false;
    for (auto b : hash) {
        if (b != 0) {
            has_nonzero = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(has_nonzero);
}

// =============================================================================
// Exception-based API tests
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("partition find ota_0 app succeeds", "[idfxx][partition]") {
    auto p = partition::find(partition::type::app, partition::subtype::app_ota_0);
    TEST_ASSERT_EQUAL(std::to_underlying(partition::type::app), std::to_underlying(p.type()));
}

TEST_CASE("partition find by label succeeds", "[idfxx][partition]") {
    auto all = partition::find_all(partition::type::app);
    TEST_ASSERT_TRUE(all.size() > 0);

    auto p = partition::find(all[0].label());
    TEST_ASSERT_NOT_NULL(p.idf_handle());
}

TEST_CASE("partition find nonexistent throws", "[idfxx][partition]") {
    bool threw = false;
    try {
        (void)partition::find(partition::type::data, partition::subtype::any, "nonexistent_xyz");
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::not_found), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("partition read on ota_0 app", "[idfxx][partition]") {
    auto p = partition::find(partition::type::app, partition::subtype::app_ota_0);
    uint8_t buf[16] = {};
    p.read(0, buf, sizeof(buf));

    bool has_nonzero = false;
    for (auto b : buf) {
        if (b != 0) {
            has_nonzero = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(has_nonzero);
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
