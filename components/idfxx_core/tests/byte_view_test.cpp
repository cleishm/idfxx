// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx byte views
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/byte_view.hpp"
#include "unity.h"

#include <array>
#include <cstring>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// byte_view accepts contiguous ranges of std::byte and uint8_t, const or not
static_assert(std::is_convertible_v<std::array<std::byte, 4>&, byte_view>);
static_assert(std::is_convertible_v<const std::array<std::byte, 4>&, byte_view>);
static_assert(std::is_convertible_v<std::array<uint8_t, 4>&, byte_view>);
static_assert(std::is_convertible_v<const std::array<uint8_t, 4>&, byte_view>);
static_assert(std::is_convertible_v<std::vector<std::byte>&, byte_view>);
static_assert(std::is_convertible_v<const std::vector<std::byte>&, byte_view>);
static_assert(std::is_convertible_v<std::vector<uint8_t>&, byte_view>);
static_assert(std::is_convertible_v<const std::vector<uint8_t>&, byte_view>);
static_assert(std::is_convertible_v<std::span<std::byte>, byte_view>);
static_assert(std::is_convertible_v<std::span<const std::byte>, byte_view>);
static_assert(std::is_convertible_v<std::span<uint8_t>, byte_view>);
static_assert(std::is_convertible_v<std::span<const uint8_t>, byte_view>);
static_assert(std::is_convertible_v<std::byte (&)[4], byte_view>);
static_assert(std::is_convertible_v<const std::byte (&)[4], byte_view>);
static_assert(std::is_convertible_v<uint8_t (&)[4], byte_view>);
static_assert(std::is_convertible_v<const uint8_t (&)[4], byte_view>);
static_assert(std::is_convertible_v<byte_span, byte_view>);
// A braced list would dangle once stored, so views reject it
template<typename V>
concept takes_braced_bytes = requires(void (*f)(V)) { f({uint8_t{0x01}, uint8_t{0x02}}); };
static_assert(!takes_braced_bytes<byte_view>);
static_assert(!takes_braced_bytes<byte_span>);

// byte_view rejects text and non-byte element types
static_assert(!std::is_constructible_v<byte_view, std::string&>);
static_assert(!std::is_constructible_v<byte_view, const std::string&>);
static_assert(!std::is_constructible_v<byte_view, std::string_view>);
static_assert(!std::is_constructible_v<byte_view, const char (&)[4]>);
static_assert(!std::is_constructible_v<byte_view, std::span<const char>>);
static_assert(!std::is_constructible_v<byte_view, std::span<const int>>);
static_assert(!std::is_constructible_v<byte_view, std::vector<int>&>);
static_assert(!std::is_constructible_v<byte_view, std::span<const int8_t>>);

// byte_span accepts lvalue containers and spans of non-const bytes
static_assert(std::is_convertible_v<std::array<std::byte, 4>&, byte_span>);
static_assert(std::is_convertible_v<std::array<uint8_t, 4>&, byte_span>);
static_assert(std::is_convertible_v<std::vector<std::byte>&, byte_span>);
static_assert(std::is_convertible_v<std::vector<uint8_t>&, byte_span>);
static_assert(std::is_convertible_v<std::span<std::byte>, byte_span>);
static_assert(std::is_convertible_v<std::span<uint8_t>, byte_span>);
static_assert(std::is_convertible_v<std::byte (&)[4], byte_span>);
static_assert(std::is_convertible_v<uint8_t (&)[4], byte_span>);

// byte_span rejects const ranges, temporary containers, text and a read-only view
static_assert(!std::is_constructible_v<byte_span, const std::array<uint8_t, 4>&>);
static_assert(!std::is_constructible_v<byte_span, const std::vector<std::byte>&>);
static_assert(!std::is_constructible_v<byte_span, std::span<const std::byte>>);
static_assert(!std::is_constructible_v<byte_span, std::span<const uint8_t>>);
static_assert(!std::is_constructible_v<byte_span, const uint8_t (&)[4]>);
static_assert(!std::is_constructible_v<byte_span, std::vector<uint8_t>>);
static_assert(!std::is_constructible_v<byte_span, std::vector<std::byte>&&>);
static_assert(!std::is_constructible_v<byte_span, std::array<uint8_t, 4>>);
static_assert(!std::is_constructible_v<byte_span, std::string&>);
static_assert(!std::is_constructible_v<byte_span, std::span<char>>);
static_assert(!std::is_constructible_v<byte_span, std::span<int>>);
static_assert(!std::is_constructible_v<byte_span, byte_view>);
static_assert(!std::is_convertible_v<byte_view, byte_span>);

// Pointer and length: only void, std::byte and uint8_t pointees
static_assert(std::is_constructible_v<byte_view, const void*, size_t>);
static_assert(std::is_constructible_v<byte_view, const std::byte*, size_t>);
static_assert(std::is_constructible_v<byte_view, const uint8_t*, size_t>);
static_assert(std::is_constructible_v<byte_view, uint8_t*, size_t>);
static_assert(!std::is_constructible_v<byte_view, const char*, size_t>);
static_assert(!std::is_constructible_v<byte_view, const int*, size_t>);
static_assert(!std::is_constructible_v<byte_view, const volatile uint8_t*, size_t>);
static_assert(std::is_constructible_v<byte_span, void*, size_t>);
static_assert(std::is_constructible_v<byte_span, std::byte*, size_t>);
static_assert(std::is_constructible_v<byte_span, uint8_t*, size_t>);
static_assert(!std::is_constructible_v<byte_span, const void*, size_t>);
static_assert(!std::is_constructible_v<byte_span, const uint8_t*, size_t>);
static_assert(!std::is_constructible_v<byte_span, char*, size_t>);
static_assert(!std::is_constructible_v<byte_span, int*, size_t>);

// Conversions to spans
static_assert(std::is_convertible_v<byte_view, std::span<const std::byte>>);
static_assert(std::is_convertible_v<byte_view, std::span<const uint8_t>>);
static_assert(!std::is_convertible_v<byte_view, std::span<std::byte>>);
static_assert(!std::is_convertible_v<byte_view, std::span<uint8_t>>);
static_assert(std::is_convertible_v<byte_span, std::span<std::byte>>);
static_assert(std::is_convertible_v<byte_span, std::span<uint8_t>>);
static_assert(std::is_convertible_v<byte_span, std::span<const std::byte>>);
static_assert(std::is_convertible_v<byte_span, std::span<const uint8_t>>);

// Range properties
static_assert(std::ranges::contiguous_range<byte_view>);
static_assert(std::ranges::sized_range<byte_view>);
static_assert(std::ranges::view<byte_view>);
static_assert(std::ranges::borrowed_range<byte_view>);
static_assert(std::ranges::contiguous_range<byte_span>);
static_assert(std::ranges::sized_range<byte_span>);
static_assert(std::ranges::view<byte_span>);
static_assert(std::ranges::borrowed_range<byte_span>);
static_assert(std::is_same_v<std::ranges::range_value_t<byte_view>, std::byte>);
static_assert(std::is_same_v<std::ranges::range_reference_t<byte_view>, const std::byte&>);
static_assert(std::is_same_v<std::ranges::range_reference_t<byte_span>, std::byte&>);

// Cheap value types
static_assert(std::is_trivially_copyable_v<byte_view>);
static_assert(std::is_trivially_copyable_v<byte_span>);
static_assert(std::is_nothrow_default_constructible_v<byte_view>);
static_assert(std::is_nothrow_default_constructible_v<byte_span>);

// =============================================================================
// Helpers
// =============================================================================

namespace {

struct reading {
    uint32_t time;
    int16_t value;
};
static_assert(!std::is_constructible_v<byte_view, const reading*, size_t>);

const std::byte* data_of(byte_view v) {
    return v.data();
}

size_t size_of(byte_view v) {
    return v.size();
}

size_t sum_of(byte_view v) {
    size_t sum = 0;
    for (std::byte b : v) {
        sum += std::to_integer<size_t>(b);
    }
    return sum;
}

std::byte* mut_data_of(byte_span v) {
    return v.data();
}

size_t mut_size_of(byte_span v) {
    return v.size();
}

void fill_with(byte_span v, uint8_t value) {
    for (auto& b : v) {
        b = std::byte{value};
    }
}

// Overload sets: text goes to string_view, bytes go to byte_view
std::string_view kind(byte_view) {
    return "bytes";
}

std::string_view kind(std::string_view) {
    return "text";
}

} // namespace

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("byte_view default is empty", "[idfxx][byte_view]") {
    byte_view v;
    TEST_ASSERT_EQUAL(0, v.size());
    TEST_ASSERT_TRUE(v.empty());
    TEST_ASSERT_NULL(v.data());

    byte_span m;
    TEST_ASSERT_EQUAL(0, m.size());
    TEST_ASSERT_TRUE(m.empty());
    TEST_ASSERT_NULL(m.data());
}

TEST_CASE("byte_view views each kind of byte source", "[idfxx][byte_view]") {
    std::array<std::byte, 4> ab{};
    std::array<uint8_t, 5> au{};
    const std::array<std::byte, 6> cab{};
    const std::array<uint8_t, 7> cau{};
    std::vector<std::byte> vb(8);
    std::vector<uint8_t> vu(9);
    const std::vector<uint8_t> cvu(10);
    std::byte bb[3] = {};
    uint8_t bu[2] = {};
    const uint8_t cbu[1] = {};

    TEST_ASSERT_EQUAL(4, size_of(ab));
    TEST_ASSERT_TRUE(data_of(ab) == ab.data());
    TEST_ASSERT_EQUAL(5, size_of(au));
    TEST_ASSERT_TRUE(data_of(au) == reinterpret_cast<const std::byte*>(au.data()));
    TEST_ASSERT_EQUAL(6, size_of(cab));
    TEST_ASSERT_TRUE(data_of(cab) == cab.data());
    TEST_ASSERT_EQUAL(7, size_of(cau));
    TEST_ASSERT_TRUE(data_of(cau) == reinterpret_cast<const std::byte*>(cau.data()));
    TEST_ASSERT_EQUAL(8, size_of(vb));
    TEST_ASSERT_TRUE(data_of(vb) == vb.data());
    TEST_ASSERT_EQUAL(9, size_of(vu));
    TEST_ASSERT_TRUE(data_of(vu) == reinterpret_cast<const std::byte*>(vu.data()));
    TEST_ASSERT_EQUAL(10, size_of(cvu));
    TEST_ASSERT_TRUE(data_of(cvu) == reinterpret_cast<const std::byte*>(cvu.data()));
    TEST_ASSERT_EQUAL(3, size_of(bb));
    TEST_ASSERT_TRUE(data_of(bb) == bb);
    TEST_ASSERT_EQUAL(2, size_of(bu));
    TEST_ASSERT_TRUE(data_of(bu) == reinterpret_cast<const std::byte*>(bu));
    TEST_ASSERT_EQUAL(1, size_of(cbu));
    TEST_ASSERT_TRUE(data_of(cbu) == reinterpret_cast<const std::byte*>(cbu));
}

TEST_CASE("byte_view views spans", "[idfxx][byte_view]") {
    std::array<uint8_t, 8> buf{};
    TEST_ASSERT_EQUAL(8, size_of(std::span{buf}));
    TEST_ASSERT_TRUE(data_of(std::span{buf}) == reinterpret_cast<const std::byte*>(buf.data()));
    TEST_ASSERT_EQUAL(3, size_of(std::span<const uint8_t>{buf.data(), 3}));
    TEST_ASSERT_EQUAL(8, size_of(std::as_bytes(std::span{buf})));
    TEST_ASSERT_EQUAL(8, size_of(std::as_writable_bytes(std::span{buf})));
}

TEST_CASE("byte_view views the bytes of an object", "[idfxx][byte_view]") {
    reading r{1, 2};
    byte_view v = std::as_bytes(std::span{&r, 1});
    TEST_ASSERT_EQUAL(sizeof(reading), v.size());
    TEST_ASSERT_TRUE(v.data() == reinterpret_cast<const std::byte*>(&r));

    byte_span m = std::as_writable_bytes(std::span{&r, 1});
    TEST_ASSERT_EQUAL(sizeof(reading), m.size());
    TEST_ASSERT_TRUE(m.data() == reinterpret_cast<std::byte*>(&r));
}

TEST_CASE("byte_view views a pointer and length", "[idfxx][byte_view]") {
    std::array<uint8_t, 8> buf{};
    const uint8_t* cp = buf.data() + 2;
    TEST_ASSERT_EQUAL(4, size_of({cp, 4}));
    TEST_ASSERT_TRUE(data_of({cp, 4}) == reinterpret_cast<const std::byte*>(cp));
    TEST_ASSERT_EQUAL(4, size_of({buf.data() + 2, 4}));
    TEST_ASSERT_TRUE(data_of({buf.data() + 2, 4}) == reinterpret_cast<const std::byte*>(cp));

    reading r{};
    const void* vp = &r;
    TEST_ASSERT_EQUAL(sizeof(r), size_of({vp, sizeof(r)}));
    TEST_ASSERT_TRUE(data_of({vp, sizeof(r)}) == reinterpret_cast<const std::byte*>(&r));
}

TEST_CASE("byte_view empty braces are an empty view", "[idfxx][byte_view]") {
    TEST_ASSERT_EQUAL(0, size_of({}));
    TEST_ASSERT_NULL(data_of({}));
}

TEST_CASE("byte_view first, last and subspan", "[idfxx][byte_view]") {
    std::array<uint8_t, 8> buf{0, 1, 2, 3, 4, 5, 6, 7};
    byte_view v{buf};
    const auto* base = reinterpret_cast<const std::byte*>(buf.data());

    TEST_ASSERT_EQUAL(3, v.first(3).size());
    TEST_ASSERT_TRUE(v.first(3).data() == base);

    TEST_ASSERT_EQUAL(3, v.last(3).size());
    TEST_ASSERT_TRUE(v.last(3).data() == base + 5);

    TEST_ASSERT_EQUAL(4, v.subspan(2, 4).size());
    TEST_ASSERT_TRUE(v.subspan(2, 4).data() == base + 2);

    // Default count runs to the end
    TEST_ASSERT_EQUAL(6, v.subspan(2).size());
    TEST_ASSERT_TRUE(v.subspan(2).data() == base + 2);
    TEST_ASSERT_EQUAL(8, v.subspan(0).size());
    TEST_ASSERT_EQUAL(0, v.subspan(8).size());

    TEST_ASSERT_EQUAL(0, v.first(0).size());
    TEST_ASSERT_EQUAL(0, v.last(0).size());
}

TEST_CASE("byte_view conversions to span preserve data and size", "[idfxx][byte_view]") {
    std::array<uint8_t, 6> buf{};
    byte_view v{buf};

    std::span<const std::byte> sb = v;
    TEST_ASSERT_TRUE(sb.data() == v.data());
    TEST_ASSERT_EQUAL(v.size(), sb.size());

    std::span<const uint8_t> su = v;
    TEST_ASSERT_TRUE(su.data() == buf.data());
    TEST_ASSERT_EQUAL(buf.size(), su.size());
}

TEST_CASE("byte_view copies and iterates", "[idfxx][byte_view]") {
    std::array<uint8_t, 4> buf{1, 2, 3, 4};
    byte_view a{buf};
    byte_view b = a;
    TEST_ASSERT_TRUE(a.data() == b.data());
    TEST_ASSERT_EQUAL(a.size(), b.size());
    TEST_ASSERT_EQUAL(10, sum_of(b));
    TEST_ASSERT_TRUE(b.front() == std::byte{1});
    TEST_ASSERT_TRUE(b.back() == std::byte{4});
    TEST_ASSERT_TRUE(b[2] == std::byte{3});
}

TEST_CASE("byte_span views each kind of byte sink", "[idfxx][byte_view]") {
    std::array<std::byte, 4> ab{};
    std::array<uint8_t, 5> au{};
    std::vector<std::byte> vb(6);
    std::vector<uint8_t> vu(7);
    std::byte bb[3] = {};
    uint8_t bu[2] = {};

    TEST_ASSERT_EQUAL(4, mut_size_of(ab));
    TEST_ASSERT_TRUE(mut_data_of(ab) == ab.data());
    TEST_ASSERT_EQUAL(5, mut_size_of(au));
    TEST_ASSERT_TRUE(mut_data_of(au) == reinterpret_cast<std::byte*>(au.data()));
    TEST_ASSERT_EQUAL(6, mut_size_of(vb));
    TEST_ASSERT_TRUE(mut_data_of(vb) == vb.data());
    TEST_ASSERT_EQUAL(7, mut_size_of(vu));
    TEST_ASSERT_TRUE(mut_data_of(vu) == reinterpret_cast<std::byte*>(vu.data()));
    TEST_ASSERT_EQUAL(3, mut_size_of(bb));
    TEST_ASSERT_TRUE(mut_data_of(bb) == bb);
    TEST_ASSERT_EQUAL(2, mut_size_of(bu));
    TEST_ASSERT_TRUE(mut_data_of(bu) == reinterpret_cast<std::byte*>(bu));

    TEST_ASSERT_EQUAL(5, mut_size_of(std::span{au}));
    TEST_ASSERT_EQUAL(2, mut_size_of(std::span<std::byte>{ab}.first(2)));
}

TEST_CASE("byte_span views a pointer and length", "[idfxx][byte_view]") {
    std::array<uint8_t, 8> buf{};
    TEST_ASSERT_EQUAL(4, mut_size_of({buf.data() + 2, 4}));
    TEST_ASSERT_TRUE(mut_data_of({buf.data() + 2, 4}) ==
                     reinterpret_cast<std::byte*>(buf.data() + 2));
    TEST_ASSERT_EQUAL(0, mut_size_of({}));
}

TEST_CASE("byte_span writes land in the underlying buffer", "[idfxx][byte_view]") {
    std::array<uint8_t, 6> au{};
    fill_with(au, 0xA5);
    for (auto b : au) {
        TEST_ASSERT_EQUAL_HEX8(0xA5, b);
    }

    std::array<std::byte, 6> ab{};
    fill_with(ab, 0x5A);
    for (auto b : ab) {
        TEST_ASSERT_TRUE(b == std::byte{0x5A});
    }

    std::vector<uint8_t> vu(6);
    fill_with({vu.data() + 2, 3}, 0x11);
    TEST_ASSERT_EQUAL_HEX8(0x00, vu[1]);
    TEST_ASSERT_EQUAL_HEX8(0x11, vu[2]);
    TEST_ASSERT_EQUAL_HEX8(0x11, vu[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, vu[5]);

    reading r{1, 2};
    fill_with(std::as_writable_bytes(std::span{&r, 1}), 0);
    TEST_ASSERT_EQUAL(0, r.time);
    TEST_ASSERT_EQUAL(0, r.value);
}

TEST_CASE("byte_span first, last and subspan", "[idfxx][byte_view]") {
    std::array<uint8_t, 8> buf{};
    byte_span m{buf};
    auto* base = reinterpret_cast<std::byte*>(buf.data());

    TEST_ASSERT_TRUE(m.first(3).data() == base);
    TEST_ASSERT_EQUAL(3, m.first(3).size());
    TEST_ASSERT_TRUE(m.last(3).data() == base + 5);
    TEST_ASSERT_EQUAL(3, m.last(3).size());
    TEST_ASSERT_TRUE(m.subspan(2, 4).data() == base + 2);
    TEST_ASSERT_EQUAL(4, m.subspan(2, 4).size());
    TEST_ASSERT_TRUE(m.subspan(2).data() == base + 2);
    TEST_ASSERT_EQUAL(6, m.subspan(2).size());

    fill_with(m.subspan(6), 0xFF);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[7]);
}

TEST_CASE("byte_span converts to views and spans", "[idfxx][byte_view]") {
    std::array<uint8_t, 6> buf{1, 2, 3, 4, 5, 6};
    byte_span m{buf};

    byte_view v = m;
    TEST_ASSERT_TRUE(v.data() == m.data());
    TEST_ASSERT_EQUAL(m.size(), v.size());
    TEST_ASSERT_EQUAL(21, sum_of(m));

    std::span<std::byte> sb = m;
    TEST_ASSERT_TRUE(sb.data() == m.data());
    TEST_ASSERT_EQUAL(m.size(), sb.size());

    std::span<uint8_t> su = m;
    TEST_ASSERT_TRUE(su.data() == buf.data());
    TEST_ASSERT_EQUAL(buf.size(), su.size());

    std::span<const std::byte> csb = m;
    TEST_ASSERT_TRUE(csb.data() == m.data());

    std::span<const uint8_t> csu = m;
    TEST_ASSERT_TRUE(csu.data() == buf.data());
    TEST_ASSERT_EQUAL(buf.size(), csu.size());
}

TEST_CASE("byte_view overloads resolve against string_view", "[idfxx][byte_view]") {
    std::string text = "abc";
    std::array<uint8_t, 3> au{};
    std::array<std::byte, 3> ab{};
    std::vector<uint8_t> vu(3);
    std::span<const std::byte> sp{ab};

    TEST_ASSERT_EQUAL_STRING("text", kind(text).data());
    TEST_ASSERT_EQUAL_STRING("text", kind("abc").data());
    TEST_ASSERT_EQUAL_STRING("text", kind(std::string_view{"abc"}).data());
    TEST_ASSERT_EQUAL_STRING("text", kind({text.data(), 2}).data());

    TEST_ASSERT_EQUAL_STRING("bytes", kind(au).data());
    TEST_ASSERT_EQUAL_STRING("bytes", kind(ab).data());
    TEST_ASSERT_EQUAL_STRING("bytes", kind(vu).data());
    TEST_ASSERT_EQUAL_STRING("bytes", kind(sp).data());
    TEST_ASSERT_EQUAL_STRING("bytes", kind(std::as_bytes(std::span{text})).data());
    TEST_ASSERT_EQUAL_STRING("bytes", kind({au.data(), 2}).data());
}
