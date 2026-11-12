// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx nvs
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/nvs"
#include "idfxx/partition"
#include "unity.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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

// nvs is move-only
static_assert(std::is_move_constructible_v<nvs>);
static_assert(std::is_move_assignable_v<nvs>);

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

// Typed blob test object
namespace {
struct blob_cfg {
    uint32_t id;
    int16_t offset;
    uint8_t flags[2];
};

struct no_default_ctor {
    explicit no_default_ctor(int v) : value(v) {}
    int value;
};

// Overload routing probes
template<typename N, typename A>
concept can_set_blob = requires(N& n, A a) { n.try_set_blob("k", a); };
template<typename N, typename A>
concept can_set_typed_blob = requires(N& n, A a) { n.template try_set_blob<A>("k", a); };
template<typename N, typename A>
concept can_get_blob_into = requires(N& n, A& a) { n.try_get_blob("k", a); };
} // namespace

// nvs_blob_value accepts trivially copyable objects
static_assert(nvs_blob_value<blob_cfg>);
static_assert(nvs_blob_value<no_default_ctor>);
static_assert(nvs_blob_value<uint32_t>);

// nvs_blob_value rejects pointers, arrays and ranges (byte buffers go through byte_view)
static_assert(!nvs_blob_value<std::array<uint8_t, 4>>);
static_assert(!nvs_blob_value<std::span<const uint8_t>>);
static_assert(!nvs_blob_value<std::vector<uint8_t>>);
static_assert(!nvs_blob_value<byte_view>);
static_assert(!nvs_blob_value<byte_span>);
static_assert(!nvs_blob_value<std::string_view>);
static_assert(!nvs_blob_value<uint8_t[4]>);
static_assert(!nvs_blob_value<const char*>);
static_assert(!nvs_blob_value<void*>);
static_assert(!nvs_blob_value<std::nullptr_t>);

// Objects take the typed overload; byte buffers can't
static_assert(can_set_blob<nvs, blob_cfg>);
static_assert(can_set_typed_blob<nvs, blob_cfg>);
static_assert(!can_set_typed_blob<nvs, std::array<uint8_t, 4>>);
static_assert(!can_set_typed_blob<nvs, std::span<const uint8_t>>);

// Byte buffers take the byte_view overload
static_assert(can_set_blob<nvs, std::array<uint8_t, 4>>);
static_assert(can_set_blob<nvs, std::array<std::byte, 4>>);
static_assert(can_set_blob<nvs, std::vector<uint8_t>>);
static_assert(can_set_blob<nvs, std::span<const uint8_t>>);
static_assert(can_set_blob<nvs, byte_view>);

// Text and non-byte ranges are rejected (use std::as_bytes)
static_assert(!can_set_blob<nvs, std::string>);
static_assert(!can_set_blob<nvs, std::string_view>);
static_assert(!can_set_blob<nvs, const char*>);
static_assert(!can_set_blob<nvs, std::array<uint32_t, 2>>);

// Read-into takes writable byte buffers only
static_assert(can_get_blob_into<nvs, std::array<uint8_t, 4>>);
static_assert(can_get_blob_into<nvs, std::vector<std::byte>>);
static_assert(!can_get_blob_into<nvs, const std::array<uint8_t, 4>>);
static_assert(!can_get_blob_into<nvs, blob_cfg>);

// get_blob forms
static_assert(std::is_same_v<decltype(std::declval<nvs&>().try_get_blob("k")),
                             result<std::vector<std::byte>>>);
static_assert(std::is_same_v<decltype(std::declval<nvs&>().try_get_blob<blob_cfg>("k")),
                             result<blob_cfg>>);
static_assert(std::is_same_v<decltype(std::declval<nvs&>().try_get_blob(
                                 "k", std::declval<std::array<uint8_t, 4>&>())),
                             result<size_t>>);
static_assert(std::is_same_v<decltype(std::declval<nvs&>().try_blob_size("k")), result<size_t>>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_same_v<decltype(std::declval<nvs&>().get_blob("k")), std::vector<std::byte>>);
static_assert(std::is_same_v<decltype(std::declval<nvs&>().get_blob<blob_cfg>("k")), blob_cfg>);
static_assert(std::is_same_v<decltype(std::declval<nvs&>().get_blob(
                                 "k", std::declval<std::array<uint8_t, 4>&>())),
                             size_t>);
static_assert(std::is_same_v<decltype(std::declval<nvs&>().blob_size("k")), size_t>);
#endif

// Encryption keys are opaque bytes
static_assert(std::is_same_v<decltype(nvs::flash::secure_config::eky),
                             std::array<std::byte, nvs::flash::key_size>>);
static_assert(std::is_same_v<decltype(nvs::flash::secure_config::tky),
                             std::array<std::byte, nvs::flash::key_size>>);

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
        TEST_ASSERT_TRUE(nvs::flash::try_erase().has_value());
        result = nvs::flash::try_init();
    }
    TEST_ASSERT_TRUE(result.has_value());
}

TEST_CASE("nvs::make with valid namespace succeeds", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto result = nvs::make("test_ns");
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->is_writeable());
}

TEST_CASE("nvs::make with read_only flag", "[idfxx][nvs]") {
    ensure_nvs_init();

    // First create the namespace by opening it read-write
    {
        auto rw = nvs::make("test_ro");
        TEST_ASSERT_TRUE(rw.has_value());
        TEST_ASSERT_TRUE(rw->is_writeable());
    }

    // Now open read-only
    auto ro = nvs::make("test_ro", true);
    TEST_ASSERT_TRUE(ro.has_value());
    TEST_ASSERT_FALSE(ro->is_writeable());
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
    auto& nvs = *nvs_handle;

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
    auto& nvs = *nvs_handle;

    auto result = nvs.try_get_string("nonexistent_key");
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), result.error().value());
}

TEST_CASE("nvs set and get blob", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_blob");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

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

TEST_CASE("nvs set and get blob with pointer and length", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_blobp");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    // Set a blob from a pointer and length (only the first 3 bytes)
    uint8_t data[]{0xAA, 0xBB, 0xCC, 0xDD};
    auto set_result = nvs.try_set_blob("blob2", {data, 3});
    TEST_ASSERT_TRUE(set_result.has_value());

    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Get it back
    auto get_result = nvs.try_get_blob("blob2");
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_EQUAL(3, get_result.value().size());
    TEST_ASSERT_EQUAL_HEX8(0xAA, std::to_integer<uint8_t>(get_result.value()[0]));
    TEST_ASSERT_EQUAL_HEX8(0xBB, std::to_integer<uint8_t>(get_result.value()[1]));
    TEST_ASSERT_EQUAL_HEX8(0xCC, std::to_integer<uint8_t>(get_result.value()[2]));
}

TEST_CASE("nvs blob_size returns stored length", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_bsize");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    std::array<std::byte, 7> data{};
    TEST_ASSERT_TRUE(nvs.try_set_blob("b7", data).has_value());
    TEST_ASSERT_TRUE(nvs.try_set_blob("b0", byte_view{}).has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    auto size7 = nvs.try_blob_size("b7");
    TEST_ASSERT_TRUE(size7.has_value());
    TEST_ASSERT_EQUAL(7, *size7);

    auto size0 = nvs.try_blob_size("b0");
    TEST_ASSERT_TRUE(size0.has_value());
    TEST_ASSERT_EQUAL(0, *size0);

    auto missing = nvs.try_blob_size("missing");
    TEST_ASSERT_FALSE(missing.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), missing.error().value());
}

TEST_CASE("nvs get_blob into buffer", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_binto");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    std::array<uint8_t, 4> data{0x10, 0x20, 0x30, 0x40};
    TEST_ASSERT_TRUE(nvs.try_set_blob("blob", data).has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Exact-size buffer
    std::array<uint8_t, 4> exact{};
    auto exact_result = nvs.try_get_blob("blob", exact);
    TEST_ASSERT_TRUE(exact_result.has_value());
    TEST_ASSERT_EQUAL(4, *exact_result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), exact.data(), 4);

    // Larger buffer: blob goes to the front, the rest is untouched
    std::array<uint8_t, 8> larger{};
    larger.fill(0xEE);
    auto larger_result = nvs.try_get_blob("blob", larger);
    TEST_ASSERT_TRUE(larger_result.has_value());
    TEST_ASSERT_EQUAL(4, *larger_result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), larger.data(), 4);
    TEST_ASSERT_EQUAL_HEX8(0xEE, larger[4]);

    // std::byte buffer
    std::vector<std::byte> bytes(6);
    auto bytes_result = nvs.try_get_blob("blob", bytes);
    TEST_ASSERT_TRUE(bytes_result.has_value());
    TEST_ASSERT_EQUAL(4, *bytes_result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), bytes.data(), 4);

    // Too-small buffer
    std::array<uint8_t, 3> small{};
    auto small_result = nvs.try_get_blob("blob", small);
    TEST_ASSERT_FALSE(small_result.has_value());
    TEST_ASSERT_TRUE(small_result.error() == idfxx::errc::invalid_size);

    // Empty buffer against a non-empty blob
    auto empty_result = nvs.try_get_blob("blob", byte_span{});
    TEST_ASSERT_FALSE(empty_result.has_value());
    TEST_ASSERT_TRUE(empty_result.error() == idfxx::errc::invalid_size);

    // Missing key
    auto missing = nvs.try_get_blob("missing", exact);
    TEST_ASSERT_FALSE(missing.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), missing.error().value());
}

TEST_CASE("nvs get_blob into buffer with empty blob", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_binto0");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    TEST_ASSERT_TRUE(nvs.try_set_blob("empty", byte_view{}).has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    auto empty_buf = nvs.try_get_blob("empty", byte_span{});
    TEST_ASSERT_TRUE(empty_buf.has_value());
    TEST_ASSERT_EQUAL(0, *empty_buf);

    std::array<uint8_t, 4> buf{};
    auto with_buf = nvs.try_get_blob("empty", buf);
    TEST_ASSERT_TRUE(with_buf.has_value());
    TEST_ASSERT_EQUAL(0, *with_buf);

    auto missing = nvs.try_get_blob("missing", byte_span{});
    TEST_ASSERT_FALSE(missing.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), missing.error().value());
}

TEST_CASE("nvs typed blob round-trip", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_btyped");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    blob_cfg cfg{};
    cfg.id = 0xCAFEF00D;
    cfg.offset = -1234;
    cfg.flags[0] = 0x5A;
    cfg.flags[1] = 0xA5;
    TEST_ASSERT_TRUE(nvs.try_set_blob("cfg", cfg).has_value());
    TEST_ASSERT_TRUE(nvs.try_set_blob("nodef", no_default_ctor{42}).has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Stored as the object's bytes
    auto size = nvs.try_blob_size("cfg");
    TEST_ASSERT_TRUE(size.has_value());
    TEST_ASSERT_EQUAL(sizeof(blob_cfg), *size);

    auto result = nvs.try_get_blob<blob_cfg>("cfg");
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL_HEX32(0xCAFEF00D, result->id);
    TEST_ASSERT_EQUAL_INT16(-1234, result->offset);
    TEST_ASSERT_EQUAL_HEX8(0x5A, result->flags[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, result->flags[1]);

    // Types without a default constructor
    auto nodef = nvs.try_get_blob<no_default_ctor>("nodef");
    TEST_ASSERT_TRUE(nodef.has_value());
    TEST_ASSERT_EQUAL(42, nodef->value);

    // Missing key
    auto missing = nvs.try_get_blob<blob_cfg>("missing");
    TEST_ASSERT_FALSE(missing.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), missing.error().value());
}

TEST_CASE("nvs typed blob size mismatch returns invalid_size", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_btmm");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    // Stored blob smaller than the type
    std::array<uint8_t, sizeof(blob_cfg) - 1> shorter{};
    TEST_ASSERT_TRUE(nvs.try_set_blob("short", shorter).has_value());

    // Stored blob larger than the type
    std::array<uint8_t, sizeof(blob_cfg) + 1> longer{};
    TEST_ASSERT_TRUE(nvs.try_set_blob("long", longer).has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    auto short_result = nvs.try_get_blob<blob_cfg>("short");
    TEST_ASSERT_FALSE(short_result.has_value());
    TEST_ASSERT_TRUE(short_result.error() == idfxx::errc::invalid_size);

    auto long_result = nvs.try_get_blob<blob_cfg>("long");
    TEST_ASSERT_FALSE(long_result.has_value());
    TEST_ASSERT_TRUE(long_result.error() == idfxx::errc::invalid_size);
}

TEST_CASE("nvs set_blob routes byte buffers and objects", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_broute");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    // A span stores the bytes it views, not the span object itself
    std::vector<uint8_t> data{1, 2, 3, 4, 5};
    static_assert(sizeof(std::span<const uint8_t>) != 3);
    TEST_ASSERT_TRUE(nvs.try_set_blob("span", std::span<const uint8_t>{data}.first(3)).has_value());

    // A built-in array stores its elements
    uint8_t raw[5]{};
    TEST_ASSERT_TRUE(nvs.try_set_blob("raw", raw).has_value());

    // A vector stores its elements
    TEST_ASSERT_TRUE(nvs.try_set_blob("vec", data).has_value());

    // An object stores its object representation
    TEST_ASSERT_TRUE(nvs.try_set_blob("obj", blob_cfg{}).has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    auto span_size = nvs.try_blob_size("span");
    TEST_ASSERT_TRUE(span_size.has_value());
    TEST_ASSERT_EQUAL(3, *span_size);

    auto raw_size = nvs.try_blob_size("raw");
    TEST_ASSERT_TRUE(raw_size.has_value());
    TEST_ASSERT_EQUAL(5, *raw_size);

    auto vec_size = nvs.try_blob_size("vec");
    TEST_ASSERT_TRUE(vec_size.has_value());
    TEST_ASSERT_EQUAL(5, *vec_size);

    auto obj_size = nvs.try_blob_size("obj");
    TEST_ASSERT_TRUE(obj_size.has_value());
    TEST_ASSERT_EQUAL(sizeof(blob_cfg), *obj_size);
}

TEST_CASE("nvs set and get integer values", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_int");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
TEST_CASE("nvs set and get float and double values", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_fp");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    TEST_ASSERT_TRUE(nvs.try_set_value<float>("f", 3.25f).has_value());
    auto f_result = nvs.try_get_value<float>("f");
    TEST_ASSERT_TRUE(f_result.has_value());
    TEST_ASSERT_EQUAL_FLOAT(3.25f, f_result.value());

    TEST_ASSERT_TRUE(nvs.try_set_value<double>("d", -1234567.890123).has_value());
    auto d_result = nvs.try_get_value<double>("d");
    TEST_ASSERT_TRUE(d_result.has_value());
    TEST_ASSERT_EQUAL_DOUBLE(-1234567.890123, d_result.value());

    // Infinity is a valid IEEE 754 value
    TEST_ASSERT_TRUE(nvs.try_set_value<double>("inf", std::numeric_limits<double>::infinity()).has_value());
    auto inf_result = nvs.try_get_value<double>("inf");
    TEST_ASSERT_TRUE(inf_result.has_value());
    TEST_ASSERT_TRUE(std::isinf(inf_result.value()));

    TEST_ASSERT_TRUE(nvs.try_commit().has_value());
}

TEST_CASE("nvs rejects NaN for float and double", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_nan");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    auto f_result = nvs.try_set_value<float>("f", std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT_FALSE(f_result.has_value());
    TEST_ASSERT_TRUE(f_result.error() == idfxx::errc::invalid_arg);

    auto d_result = nvs.try_set_value<double>("d", std::numeric_limits<double>::quiet_NaN());
    TEST_ASSERT_FALSE(d_result.has_value());
    TEST_ASSERT_TRUE(d_result.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("nvs float and double type mismatch", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_fpmm");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

    TEST_ASSERT_TRUE(nvs.try_set_value<float>("f", 1.5f).has_value());
    TEST_ASSERT_FALSE(nvs.try_get_value<double>("f").has_value());
    TEST_ASSERT_FALSE(nvs.try_get_value<uint32_t>("f").has_value());
}
#endif

TEST_CASE("nvs erase key", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_erase");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

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
    auto& nvs = *nvs_handle;

    auto result = nvs.try_erase("does_not_exist");
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), result.error().value());
}

TEST_CASE("nvs erase_all", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_erall");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

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
        TEST_ASSERT_TRUE(rw->try_set_string("key", "value").has_value());
        TEST_ASSERT_TRUE(rw->try_commit().has_value());
    }

    // Open read-only
    auto ro = nvs::make("test_ro_w", true);
    TEST_ASSERT_TRUE(ro.has_value());
    auto& nvs = *ro;

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
    auto& nvs = *nvs_handle;

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
    auto& nvs = *nvs_handle;

    // NVS key max length is 15 characters
    auto result = nvs.try_set_string("this_key_name_is_definitely_too_long", "value");
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::key_too_long), result.error().value());
}

TEST_CASE("nvs empty string value", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_empty");
    TEST_ASSERT_TRUE(nvs_handle.has_value());
    auto& nvs = *nvs_handle;

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
    auto& nvs = *nvs_handle;

    // Set empty blob
    std::vector<uint8_t> empty_data;
    TEST_ASSERT_TRUE(nvs.try_set_blob("empty", empty_data).has_value());
    TEST_ASSERT_TRUE(nvs.try_commit().has_value());

    // Get it back
    auto result = nvs.try_get_blob("empty");
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(0, result.value().size());
}

TEST_CASE("nvs read-only handle cannot write typed blob", "[idfxx][nvs]") {
    ensure_nvs_init();

    { auto rw = nvs::make("test_ro_tb"); TEST_ASSERT_TRUE(rw.has_value()); }

    auto ro = nvs::make("test_ro_tb", true);
    TEST_ASSERT_TRUE(ro.has_value());

    auto result = ro->try_set_blob("cfg", blob_cfg{});
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::read_only), result.error().value());
}

// =============================================================================
// Exception-based API tests
// =============================================================================

// =============================================================================
// Flash init/deinit tests
// =============================================================================

TEST_CASE("nvs::flash::try_deinit and reinit cycle", "[idfxx][nvs]") {
    ensure_nvs_init();

    // Deinit should succeed
    auto deinit_result = nvs::flash::try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());

    // After deinit, making an nvs handle should fail
    auto nvs_result = nvs::make("test_dinit");
    TEST_ASSERT_FALSE(nvs_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_initialized), nvs_result.error().value());

    // Re-init should succeed
    auto init_result = nvs::flash::try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    // Now making a handle should work again
    auto nvs_result2 = nvs::make("test_dinit");
    TEST_ASSERT_TRUE(nvs_result2.has_value());
}

TEST_CASE("nvs::flash::try_erase and reinit", "[idfxx][nvs]") {
    ensure_nvs_init();

    // Write something first
    {
        auto ns = nvs::make("test_erase");
        TEST_ASSERT_TRUE(ns.has_value());
        TEST_ASSERT_TRUE(ns->try_set_string("key", "value").has_value());
        TEST_ASSERT_TRUE(ns->try_commit().has_value());
    }

    // Deinit before erase
    auto deinit_result = nvs::flash::try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());

    // Erase
    auto erase_result = nvs::flash::try_erase();
    TEST_ASSERT_TRUE(erase_result.has_value());

    // Re-init
    auto init_result = nvs::flash::try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    // Data should be gone after erase
    {
        auto ns = nvs::make("test_erase");
        TEST_ASSERT_TRUE(ns.has_value());
        auto get_result = ns->try_get_string("key");
        TEST_ASSERT_FALSE(get_result.has_value());
        TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), get_result.error().value());
    }
}

// =============================================================================
// Exception-based API tests
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("nvs constructor succeeds", "[idfxx][nvs]") {
    ensure_nvs_init();

    nvs ns("test_exc");
    TEST_ASSERT_TRUE(ns.is_writeable());
}

TEST_CASE("nvs set and get string via exception API", "[idfxx][nvs]") {
    ensure_nvs_init();

    nvs ns("test_exc2");
    ns.set_string("greeting", "hello");
    ns.commit();

    auto val = ns.get_string("greeting");
    TEST_ASSERT_EQUAL_STRING("hello", val.c_str());
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
TEST_CASE("nvs float and double via exception API", "[idfxx][nvs]") {
    ensure_nvs_init();

    nvs ns("test_exc_fp");
    ns.set_value<float>("f", 0.5f);
    ns.set_value<double>("d", 2.5);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, ns.get_value<float>("f"));
    TEST_ASSERT_EQUAL_DOUBLE(2.5, ns.get_value<double>("d"));

    bool threw = false;
    try {
        ns.set_value<float>("n", std::numeric_limits<float>::quiet_NaN());
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == idfxx::errc::invalid_arg);
    }
    TEST_ASSERT_TRUE(threw);
}
#endif

TEST_CASE("nvs get non-existent key throws", "[idfxx][nvs]") {
    ensure_nvs_init();

    nvs ns("test_exc3");

    bool threw = false;
    try {
        (void)ns.get_string("nonexistent");
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("nvs set_value and get_value via exception API", "[idfxx][nvs]") {
    ensure_nvs_init();

    nvs ns("test_exc4");
    ns.set_value<int32_t>("count", 42);
    ns.commit();

    auto val = ns.get_value<int32_t>("count");
    TEST_ASSERT_EQUAL(42, val);
}

TEST_CASE("nvs blobs via exception API", "[idfxx][nvs]") {
    ensure_nvs_init();

    nvs ns("test_exc_blob");

    std::array<uint8_t, 3> data{0x01, 0x02, 0x03};
    ns.set_blob("bytes", data);

    blob_cfg cfg{};
    cfg.id = 7;
    cfg.offset = -7;
    ns.set_blob("cfg", cfg);
    ns.commit();

    TEST_ASSERT_EQUAL(3, ns.blob_size("bytes"));

    auto vec = ns.get_blob("bytes");
    TEST_ASSERT_EQUAL(3, vec.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), vec.data(), 3);

    std::array<uint8_t, 8> buf{};
    TEST_ASSERT_EQUAL(3, ns.get_blob("bytes", buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), buf.data(), 3);

    auto read_cfg = ns.get_blob<blob_cfg>("cfg");
    TEST_ASSERT_EQUAL_UINT32(7, read_cfg.id);
    TEST_ASSERT_EQUAL_INT16(-7, read_cfg.offset);

    bool threw = false;
    try {
        std::array<uint8_t, 2> small{};
        (void)ns.get_blob("bytes", small);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == idfxx::errc::invalid_size);
    }
    TEST_ASSERT_TRUE(threw);

    threw = false;
    try {
        (void)ns.get_blob<blob_cfg>("bytes");
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == idfxx::errc::invalid_size);
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("nvs read-only write throws", "[idfxx][nvs]") {
    ensure_nvs_init();

    // Create namespace first
    { nvs ns("test_ro_exc"); }

    nvs ns("test_ro_exc", true);
    TEST_ASSERT_FALSE(ns.is_writeable());

    bool threw = false;
    try {
        ns.set_string("key", "value");
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::read_only), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS

// =============================================================================
// Moved-from state tests
// =============================================================================

TEST_CASE("moved-from nvs returns invalid_handle", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto nvs_handle = nvs::make("test_move");
    TEST_ASSERT_TRUE(nvs_handle.has_value());

    auto moved = std::move(*nvs_handle);

    // moved-from object should return invalid_handle
    auto set_result = nvs_handle->try_set_string("key", "value");
    TEST_ASSERT_FALSE(set_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::invalid_handle), set_result.error().value());

    auto get_result = nvs_handle->try_get_string("key");
    TEST_ASSERT_FALSE(get_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::invalid_handle), get_result.error().value());

    auto commit_result = nvs_handle->try_commit();
    TEST_ASSERT_FALSE(commit_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::invalid_handle), commit_result.error().value());

    // Moved-to object should work normally
    TEST_ASSERT_TRUE(moved.try_set_string("key", "value").has_value());
}

TEST_CASE("nvs move assignment cleans up target", "[idfxx][nvs]") {
    ensure_nvs_init();

    auto r1 = nvs::make("test_mv1");
    TEST_ASSERT_TRUE(r1.has_value());

    auto r2 = nvs::make("test_mv2");
    TEST_ASSERT_TRUE(r2.has_value());

    // Move assignment should close the target handle
    *r1 = std::move(*r2);

    // r1 should now be usable, r2 should be moved-from
    TEST_ASSERT_TRUE(r1->try_set_string("k", "v").has_value());

    auto set_result = r2->try_set_string("k", "v");
    TEST_ASSERT_FALSE(set_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::invalid_handle), set_result.error().value());
}

// =============================================================================
// Partition-based API tests
// =============================================================================

static partition find_nvs_partition() {
    auto result = partition::try_find(partition::type::data, partition::subtype::data_nvs);
    TEST_ASSERT_TRUE(result.has_value());
    return *result;
}

TEST_CASE("nvs::flash::try_init with partition", "[idfxx][nvs]") {
    // Deinit first if already initialized
    nvs::flash::try_deinit();

    auto part = find_nvs_partition();

    auto result = nvs::flash::try_init(part);
    if (!result && (result.error() == nvs::errc::no_free_pages || result.error() == nvs::errc::new_version_found)) {
        TEST_ASSERT_TRUE(nvs::flash::try_erase(part).has_value());
        result = nvs::flash::try_init(part);
    }
    TEST_ASSERT_TRUE(result.has_value());

    // Should be able to open a namespace now
    auto nvs_result = nvs::make("test_pinit");
    TEST_ASSERT_TRUE(nvs_result.has_value());
}

TEST_CASE("nvs::flash::try_erase with partition", "[idfxx][nvs]") {
    auto part = find_nvs_partition();

    // Ensure initialized
    auto init_result = nvs::flash::try_init(part);
    if (!init_result && (init_result.error() == nvs::errc::no_free_pages ||
                         init_result.error() == nvs::errc::new_version_found)) {
        TEST_ASSERT_TRUE(nvs::flash::try_erase(part).has_value());
        init_result = nvs::flash::try_init(part);
    }
    TEST_ASSERT_TRUE(init_result.has_value());

    // Write something
    {
        auto ns = nvs::make("test_perase");
        TEST_ASSERT_TRUE(ns.has_value());
        TEST_ASSERT_TRUE(ns->try_set_string("key", "value").has_value());
        TEST_ASSERT_TRUE(ns->try_commit().has_value());
    }

    // Deinit, erase, reinit
    TEST_ASSERT_TRUE(nvs::flash::try_deinit().has_value());
    TEST_ASSERT_TRUE(nvs::flash::try_erase(part).has_value());
    TEST_ASSERT_TRUE(nvs::flash::try_init(part).has_value());

    // Data should be gone
    {
        auto ns = nvs::make("test_perase");
        TEST_ASSERT_TRUE(ns.has_value());
        auto get_result = ns->try_get_string("key");
        TEST_ASSERT_FALSE(get_result.has_value());
        TEST_ASSERT_EQUAL(std::to_underlying(nvs::errc::not_found), get_result.error().value());
    }
}

TEST_CASE("nvs::make with partition", "[idfxx][nvs]") {
    auto part = find_nvs_partition();
    ensure_nvs_init();

    // Deinit default, init via partition
    nvs::flash::try_deinit();
    auto init_result = nvs::flash::try_init(part);
    if (!init_result && (init_result.error() == nvs::errc::no_free_pages ||
                         init_result.error() == nvs::errc::new_version_found)) {
        TEST_ASSERT_TRUE(nvs::flash::try_erase(part).has_value());
        init_result = nvs::flash::try_init(part);
    }
    TEST_ASSERT_TRUE(init_result.has_value());

    auto nvs_result = nvs::make(part, "test_pmake");
    TEST_ASSERT_TRUE(nvs_result.has_value());
    TEST_ASSERT_TRUE(nvs_result->is_writeable());

    // Write and read back
    TEST_ASSERT_TRUE(nvs_result->try_set_string("pkey", "pvalue").has_value());
    TEST_ASSERT_TRUE(nvs_result->try_commit().has_value());

    auto get_result = nvs_result->try_get_string("pkey");
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_EQUAL_STRING("pvalue", get_result->c_str());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("nvs constructor with partition", "[idfxx][nvs]") {
    auto part = find_nvs_partition();

    // Deinit default, init via partition
    nvs::flash::try_deinit();
    auto init_result = nvs::flash::try_init(part);
    if (!init_result && (init_result.error() == nvs::errc::no_free_pages ||
                         init_result.error() == nvs::errc::new_version_found)) {
        nvs::flash::erase(part);
        nvs::flash::init(part);
    }

    nvs ns(part, "test_pexc");
    TEST_ASSERT_TRUE(ns.is_writeable());

    ns.set_string("greeting", "hello");
    ns.commit();

    auto val = ns.get_string("greeting");
    TEST_ASSERT_EQUAL_STRING("hello", val.c_str());
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
