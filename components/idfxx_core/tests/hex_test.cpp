// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx hex formatting
// Uses ESP-IDF Unity test framework

#include "idfxx/hex.hpp"
#include "unity.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#ifdef CONFIG_IDFXX_STD_FORMAT
#include <format>
#endif

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

static_assert(hex_options{}.separator.empty());
static_assert(hex_options{}.letter_case == hex_case::lower);

// =============================================================================
// to_string
// =============================================================================

TEST_CASE("hex of an empty buffer is empty", "[idfxx][core][hex]") {
    std::vector<uint8_t> empty;
    TEST_ASSERT_EQUAL_STRING("", to_string(hex(empty)).c_str());
    TEST_ASSERT_EQUAL_STRING("", to_string(hex(empty, {.separator = ":"})).c_str());
}

TEST_CASE("hex of a single byte", "[idfxx][core][hex]") {
    std::array<uint8_t, 1> one{0x5A};
    TEST_ASSERT_EQUAL_STRING("5a", to_string(hex(one)).c_str());
    // No separator appears with only one byte
    TEST_ASSERT_EQUAL_STRING("5a", to_string(hex(one, {.separator = ":"})).c_str());
}

TEST_CASE("hex defaults to lowercase with no separator", "[idfxx][core][hex]") {
    std::array<uint8_t, 4> bytes{0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_STRING("deadbeef", to_string(hex(bytes)).c_str());
}

TEST_CASE("hex upper case", "[idfxx][core][hex]") {
    std::array<uint8_t, 4> bytes{0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_STRING(
        "DEADBEEF",
        to_string(hex(bytes, {.letter_case = hex_case::upper})).c_str()
    );
    TEST_ASSERT_EQUAL_STRING(
        "deadbeef",
        to_string(hex(bytes, {.letter_case = hex_case::lower})).c_str()
    );
}

TEST_CASE("hex with a single-character separator", "[idfxx][core][hex]") {
    std::array<uint8_t, 6> mac{0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_STRING(
        "AA:BB:CC:01:02:03",
        to_string(hex(mac, {.separator = ":", .letter_case = hex_case::upper})).c_str()
    );
    TEST_ASSERT_EQUAL_STRING("aa bb cc 01 02 03", to_string(hex(mac, {.separator = " "})).c_str());
}

TEST_CASE("hex with a multi-character separator", "[idfxx][core][hex]") {
    std::array<uint8_t, 3> bytes{0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_STRING("01, 02, 03", to_string(hex(bytes, {.separator = ", "})).c_str());
    TEST_ASSERT_EQUAL_STRING("01::02::03", to_string(hex(bytes, {.separator = "::"})).c_str());
}

TEST_CASE("hex separator is neither leading nor trailing", "[idfxx][core][hex]") {
    std::array<uint8_t, 2> bytes{0x12, 0x34};
    auto s = to_string(hex(bytes, {.separator = "--"}));
    TEST_ASSERT_EQUAL_STRING("12--34", s.c_str());
    TEST_ASSERT_EQUAL(6, s.size());
}

TEST_CASE("hex accepts std::byte and uint8_t sources", "[idfxx][core][hex]") {
    std::array<std::byte, 3> as_byte{std::byte{0x0A}, std::byte{0xB0}, std::byte{0xFF}};
    std::array<uint8_t, 3> as_u8{0x0A, 0xB0, 0xFF};
    TEST_ASSERT_EQUAL_STRING("0ab0ff", to_string(hex(as_byte)).c_str());
    TEST_ASSERT_EQUAL_STRING("0ab0ff", to_string(hex(as_u8)).c_str());

    const std::vector<uint8_t> vec{0x01, 0x23};
    TEST_ASSERT_EQUAL_STRING("0123", to_string(hex(vec)).c_str());
}

TEST_CASE("hex pads and covers the byte range extremes", "[idfxx][core][hex]") {
    std::array<uint8_t, 4> bytes{0x00, 0xFF, 0x0F, 0xF0};
    TEST_ASSERT_EQUAL_STRING("00ff0ff0", to_string(hex(bytes)).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "00:FF:0F:F0",
        to_string(hex(bytes, {.separator = ":", .letter_case = hex_case::upper})).c_str()
    );
}

TEST_CASE("hex accessors return construction arguments", "[idfxx][core][hex]") {
    std::array<uint8_t, 2> bytes{0x01, 0x02};
    hex h(bytes, {.separator = "-", .letter_case = hex_case::upper});
    TEST_ASSERT_EQUAL(2, h.bytes().size());
    TEST_ASSERT_TRUE(h.bytes().data() == reinterpret_cast<const std::byte*>(bytes.data()));
    TEST_ASSERT_EQUAL_STRING("-", std::string(h.options().separator).c_str());
    TEST_ASSERT_TRUE(h.options().letter_case == hex_case::upper);
}

// =============================================================================
// std::format
// =============================================================================

#ifdef CONFIG_IDFXX_STD_FORMAT

TEST_CASE("hex formats with std::format", "[idfxx][core][hex]") {
    std::array<uint8_t, 4> bytes{0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_STRING("deadbeef", std::format("{}", hex(bytes)).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "DE AD BE EF",
        std::format("{}", hex(bytes, {.separator = " ", .letter_case = hex_case::upper})).c_str()
    );
    TEST_ASSERT_EQUAL_STRING("", std::format("{}", hex(std::vector<uint8_t>{})).c_str());
}

TEST_CASE("hex format honors fill/align/width spec", "[idfxx][core][hex]") {
    std::array<uint8_t, 4> bytes{0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_STRING("[    deadbeef]", std::format("[{:>12}]", hex(bytes)).c_str());
    TEST_ASSERT_EQUAL_STRING("[deadbeef    ]", std::format("[{:<12}]", hex(bytes)).c_str());
    TEST_ASSERT_EQUAL_STRING("[**deadbeef**]", std::format("[{:*^12}]", hex(bytes)).c_str());
    // Width smaller than the text does not truncate
    TEST_ASSERT_EQUAL_STRING("deadbeef", std::format("{:4}", hex(bytes)).c_str());
    // '.' and '?' are accepted as fill characters
    TEST_ASSERT_EQUAL_STRING("[..deadbeef..]", std::format("[{:.^12}]", hex(bytes)).c_str());
    TEST_ASSERT_EQUAL_STRING("[??deadbeef]", std::format("[{:?>10}]", hex(bytes)).c_str());
    // A multi-byte fill character
    TEST_ASSERT_EQUAL_STRING(
        "[\u00b7\u00b7deadbeef]", std::format("[{:\u00b7>10}]", hex(bytes)).c_str()
    );
    // Nested width argument
    auto h = hex(bytes);
    int width = 10;
    TEST_ASSERT_EQUAL_STRING(
        "[deadbeef  ]", std::vformat("[{:{}}]", std::make_format_args(h, width)).c_str()
    );
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("hex format rejects specs other than fill, align and width", "[idfxx][core][hex]") {
    std::array<uint8_t, 2> bytes{0xDE, 0xAD};
    auto h = hex(bytes);
    int width = 8;
    int precision = 3;
    auto rejects = [](std::string_view fmt, auto&&... args) {
        try {
            (void)std::vformat(fmt, std::make_format_args(args...));
        } catch (const std::format_error&) {
            return true;
        }
        return false;
    };
    TEST_ASSERT_TRUE(rejects("{:.3}", h));
    TEST_ASSERT_TRUE(rejects("{:?}", h));
    TEST_ASSERT_TRUE(rejects("{:s}", h));
    TEST_ASSERT_TRUE(rejects("{:>8x}", h));
    TEST_ASSERT_TRUE(rejects("{:{}.{}}", h, width, precision));
}
#endif // CONFIG_COMPILER_CXX_EXCEPTIONS

#endif // CONFIG_IDFXX_STD_FORMAT
