// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_persistent_log: the prefix stamped on each stored line, and shortening text
// so that it ends at the start of a UTF-8 character.

#include "../src/log_line.hpp"
#include "unity.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

using namespace idfxx::persistent_log;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

std::string prefix(
    uint32_t boot,
    uint32_t wake,
    milliseconds uptime,
    std::optional<system_clock::time_point> wall
) {
    std::array<char, max_prefix_size> buf;
    size_t n = format_prefix(buf, boot, wake, uptime, wall);
    return std::string(buf.data(), n);
}

void assert_same(std::string_view expected, const std::string& actual) {
    TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
    TEST_ASSERT_TRUE(expected == actual);
}

} // namespace

TEST_CASE(
    "format_prefix stamps the boot, wake, time since boot and wall time",
    "[persistent_log]"
) {
    auto wall = sys_days{2026y / November / 20} + 10h + 31min + 2s + 789ms;
    assert_same("12.345 +1234 2026-11-20T10:31:02Z ", prefix(12, 345, 1234ms, wall));
    assert_same("0.0 +0 1970-01-01T00:00:00Z ", prefix(0, 0, 0ms, system_clock::time_point{}));
    assert_same(
        "1.2 +3 1999-01-02T03:04:05Z ",
        prefix(1, 2, 3ms, sys_days{1999y / January / 2} + 3h + 4min + 5s)
    );
}

TEST_CASE("format_prefix writes a dash without a wall time", "[persistent_log]") {
    assert_same("12.345 +1234 - ", prefix(12, 345, 1234ms, std::nullopt));
}

TEST_CASE("format_prefix fits its largest stamp and refuses a small buffer", "[persistent_log]") {
    // The latest second that system_clock can hold in 64-bit nanoseconds is in 2262.
    auto wall = sys_days{2262y / January / 1} + 23h + 59min + 59s;
    std::string big = prefix(
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max(),
        milliseconds::min(),
        wall
    );
    TEST_ASSERT_GREATER_THAN_size_t(0, big.size());
    TEST_ASSERT_LESS_OR_EQUAL_size_t(max_prefix_size, big.size());
    TEST_ASSERT_TRUE(big.ends_with(" 2262-01-01T23:59:59Z "));

    std::array<char, max_prefix_size - 1> small;
    TEST_ASSERT_EQUAL_size_t(0, format_prefix(small, 1, 2, 3ms, std::nullopt));
}

namespace {

std::string utf8_cut(std::string_view s, size_t size) {
    return std::string(detail::utf8_prefix(s, size));
}

} // namespace

TEST_CASE("utf8_prefix keeps whole characters and ASCII as they are", "[persistent_log]") {
    // "é", "€" and U+1F600: 2, 3 and 4 bytes, as complete characters.
    const std::string_view whole = "a\xC3\xA9"
                                   "b\xE2\x82\xAC"
                                   "c\xF0\x9F\x98\x80";
    assert_same(whole, utf8_cut(whole, whole.size()));
    assert_same(whole, utf8_cut(whole, 100));
    assert_same("plain ascii", utf8_cut("plain ascii text", 11));
    assert_same("", utf8_cut("abc", 0));
}

TEST_CASE("utf8_prefix trims a character the cut ends partway", "[persistent_log]") {
    assert_same("a", utf8_cut("a\xC3\xA9", 2));
    assert_same("b", utf8_cut("b\xE2\x82\xAC", 2));
    assert_same("b", utf8_cut("b\xE2\x82\xAC", 3));
    assert_same("c", utf8_cut("c\xF0\x9F\x98\x80", 2));
    assert_same("c", utf8_cut("c\xF0\x9F\x98\x80", 3));
    assert_same("c", utf8_cut("c\xF0\x9F\x98\x80", 4));
}

TEST_CASE(
    "utf8_prefix trims text that fits but ends partway through a character",
    "[persistent_log]"
) {
    assert_same("x", utf8_cut("x\xE2\x82", 10));
    assert_same("x", utf8_cut("x\xF0", 10));
}

TEST_CASE("utf8_prefix keeps a run of continuation bytes rather than eat it", "[persistent_log]") {
    const std::string run(8, '\x80');
    assert_same(run, utf8_cut(run, run.size()));
    assert_same("a" + run, utf8_cut("a" + run, 9));
}
