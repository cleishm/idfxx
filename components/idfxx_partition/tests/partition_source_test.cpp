// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx partition_source
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/partition"
#include "unity.h"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// partition_source models byte_source
static_assert(byte_source<partition_source>);

// partition_source is explicitly constructed from a partition
static_assert(!std::is_default_constructible_v<partition_source>);
static_assert(std::is_constructible_v<partition_source, const partition&>);
static_assert(std::is_constructible_v<partition_source, partition&>);
static_assert(!std::is_convertible_v<const partition&, partition_source>);

// It keeps its own copy of the partition, so a temporary is fine
static_assert(std::is_nothrow_constructible_v<partition_source, partition>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_constructible_v<partition_source, partition, size_t, size_t>);
#endif

// partition_source is copyable
static_assert(std::is_copy_constructible_v<partition_source>);
static_assert(std::is_copy_assignable_v<partition_source>);

// =============================================================================
// Helpers
// =============================================================================

namespace {

partition find_test_partition() {
    auto result = partition::try_find(partition::type::app, partition::subtype::app_ota_0);
    TEST_ASSERT_TRUE(result.has_value());
    return *result;
}

std::vector<uint8_t> read_direct(const partition& part, size_t offset, size_t length) {
    std::vector<uint8_t> out(length);
    TEST_ASSERT_TRUE(part.try_read(offset, std::span<uint8_t>{out}).has_value());
    return out;
}

} // namespace

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("partition_source covers the whole partition", "[idfxx][partition]") {
    auto part = find_test_partition();
    partition_source src{part};
    TEST_ASSERT_EQUAL(part.size(), src.remaining());

    std::array<uint8_t, 32> buf{};
    auto n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(buf.size(), *n);
    TEST_ASSERT_EQUAL(part.size() - buf.size(), src.remaining());

    auto expected = read_direct(part, 0, buf.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), buf.data(), buf.size());
}

TEST_CASE("partition_source window reads in short chunks then reports end of stream", "[idfxx][partition]") {
    auto part = find_test_partition();
    constexpr size_t offset = 5;
    constexpr size_t length = 23;
    auto expected = read_direct(part, offset, length);

    auto src = partition_source::make(part, offset, length);
    TEST_ASSERT_TRUE(src.has_value());
    TEST_ASSERT_EQUAL(length, src->remaining());

    // Odd-sized chunks: 7 + 7 + 7 + 2
    std::vector<uint8_t> out;
    std::array<uint8_t, 7> buf{};
    while (true) {
        auto n = src->try_read(buf);
        TEST_ASSERT_TRUE(n.has_value());
        TEST_ASSERT_TRUE(*n <= buf.size());
        if (*n == 0) {
            break;
        }
        out.insert(out.end(), buf.begin(), buf.begin() + *n);
    }
    TEST_ASSERT_EQUAL(length, out.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), out.data(), length);
    TEST_ASSERT_EQUAL(0, src->remaining());

    // Stays at end of stream
    auto n = src->try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(0, *n);
}

TEST_CASE("partition_source empty buffer reads nothing and does not advance", "[idfxx][partition]") {
    auto part = find_test_partition();
    auto src = partition_source::make(part, 0, 16);
    TEST_ASSERT_TRUE(src.has_value());

    auto n = src->try_read(std::span<uint8_t>{});
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(0, *n);
    TEST_ASSERT_EQUAL(16, src->remaining());
}

TEST_CASE("partition_source window ending at the partition end", "[idfxx][partition]") {
    auto part = find_test_partition();
    const size_t offset = part.size() - 8;
    auto expected = read_direct(part, offset, 8);

    auto src = partition_source::make(part, offset, 8);
    TEST_ASSERT_TRUE(src.has_value());

    std::array<uint8_t, 16> buf{};
    auto n = src->try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(8, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), buf.data(), 8);

    n = src->try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(0, *n);
}

TEST_CASE("partition_source empty window is immediately at end of stream", "[idfxx][partition]") {
    auto part = find_test_partition();
    for (size_t offset : {size_t{0}, size_t{100}, static_cast<size_t>(part.size())}) {
        auto src = partition_source::make(part, offset, 0);
        TEST_ASSERT_TRUE(src.has_value());
        TEST_ASSERT_EQUAL(0, src->remaining());

        std::array<uint8_t, 4> buf{};
        auto n = src->try_read(buf);
        TEST_ASSERT_TRUE(n.has_value());
        TEST_ASSERT_EQUAL(0, *n);
    }
}

TEST_CASE("partition_source make rejects out-of-range windows", "[idfxx][partition]") {
    auto part = find_test_partition();
    const size_t size = part.size();
    constexpr size_t max = std::numeric_limits<size_t>::max();

    struct window {
        size_t offset;
        size_t length;
    };
    const window cases[] = {
        {0, size + 1},    // one byte past the end
        {size - 8, 9},    // tail window one byte too long
        {size, 1},        // starts at the end
        {size + 1, 0},    // starts past the end
        {1, max},         // offset + length overflows
        {max, 2},         // offset + length overflows
    };
    for (const auto& c : cases) {
        auto src = partition_source::make(part, c.offset, c.length);
        TEST_ASSERT_FALSE(src.has_value());
        TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), src.error().value());
        TEST_ASSERT_TRUE(src.error().category() == default_category());
    }

    // The full partition is in range
    auto full = partition_source::make(part, 0, size);
    TEST_ASSERT_TRUE(full.has_value());
    TEST_ASSERT_EQUAL(size, full->remaining());
}

TEST_CASE("partition_source reads after the partition it was made from is gone", "[idfxx][partition]") {
    auto part = find_test_partition();
    auto expected = read_direct(part, 2, 6);

    // From a temporary
    partition_source whole{find_test_partition()};
    TEST_ASSERT_EQUAL(part.size(), whole.remaining());

    // From a partition that goes out of scope
    auto window = [] {
        auto scoped = find_test_partition();
        return partition_source::make(scoped, 2, 6);
    }();
    TEST_ASSERT_TRUE(window.has_value());

    std::array<uint8_t, 8> buf{};
    auto n = window->try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(6, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), buf.data(), 6);

    n = whole.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(buf.size(), *n);
}

TEST_CASE("partition_source copies read independently", "[idfxx][partition]") {
    auto part = find_test_partition();
    auto expected = read_direct(part, 0, 12);

    partition_source a{part};
    std::array<uint8_t, 4> buf{};
    TEST_ASSERT_TRUE(a.try_read(buf).has_value());

    partition_source b = a;
    TEST_ASSERT_TRUE(b.try_read(buf).has_value());
    TEST_ASSERT_EQUAL(part.size() - 4, a.remaining());
    TEST_ASSERT_EQUAL(part.size() - 8, b.remaining());

    // a resumes from where it was copied, not from where b got to
    auto n = a.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(4, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&expected[4], buf.data(), 4);
}

// =============================================================================
// Exception-based API tests
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("partition_source window constructor and read", "[idfxx][partition]") {
    auto part = find_test_partition();
    auto expected = read_direct(part, 3, 10);

    partition_source src{part, 3, 10};
    TEST_ASSERT_EQUAL(10, src.remaining());

    std::array<uint8_t, 6> buf{};
    TEST_ASSERT_EQUAL(6, src.read(buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&expected[0], buf.data(), 6);
    TEST_ASSERT_EQUAL(4, src.read(buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&expected[6], buf.data(), 4);
    TEST_ASSERT_EQUAL(0, src.read(buf));
}

TEST_CASE("partition_source window constructor throws on out-of-range window", "[idfxx][partition]") {
    auto part = find_test_partition();
    bool threw = false;
    try {
        [[maybe_unused]] partition_source src{part, part.size(), 1};
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
