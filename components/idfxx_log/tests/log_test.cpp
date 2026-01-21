// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx log
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/log>
#include <unity.h>

#include <esp_log.h>
#include <string>
#include <type_traits>

using namespace idfxx::log;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// level enum values match ESP-IDF constants
static_assert(std::to_underlying(level::none) == ESP_LOG_NONE);
static_assert(std::to_underlying(level::error) == ESP_LOG_ERROR);
static_assert(std::to_underlying(level::warn) == ESP_LOG_WARN);
static_assert(std::to_underlying(level::info) == ESP_LOG_INFO);
static_assert(std::to_underlying(level::debug) == ESP_LOG_DEBUG);
static_assert(std::to_underlying(level::verbose) == ESP_LOG_VERBOSE);

// logger is constexpr constructible
static_assert([] {
    constexpr logger log{"test"};
    return log.tag()[0] == 't';
}());

// logger is copyable
static_assert(std::is_copy_constructible_v<logger>);
static_assert(std::is_copy_assignable_v<logger>);

// logger is movable
static_assert(std::is_move_constructible_v<logger>);
static_assert(std::is_move_assignable_v<logger>);

// logger is not default constructible
static_assert(!std::is_default_constructible_v<logger>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("logger construction and tag access", "[idfxx][log]") {
    constexpr logger log{"test_tag"};
    TEST_ASSERT_EQUAL_STRING("test_tag", log.tag());
}

TEST_CASE("logger copy preserves tag", "[idfxx][log]") {
    constexpr logger log1{"copy_tag"};
    logger log2 = log1;
    TEST_ASSERT_EQUAL_STRING("copy_tag", log2.tag());
}

TEST_CASE("logger::error logs without crashing", "[idfxx][log]") {
    constexpr logger log{"test_error"};
    log.error("error message: {}", 42);
}

TEST_CASE("logger::warn logs without crashing", "[idfxx][log]") {
    constexpr logger log{"test_warn"};
    log.warn("warn message: {}", "warning");
}

TEST_CASE("logger::info logs without crashing", "[idfxx][log]") {
    constexpr logger log{"test_info"};
    log.info("info message: {}", 3.14);
}

TEST_CASE("logger::debug logs without crashing", "[idfxx][log]") {
    constexpr logger log{"test_debug"};
    log.debug("debug message: {} {}", "a", "b");
}

TEST_CASE("logger::verbose logs without crashing", "[idfxx][log]") {
    constexpr logger log{"test_verbose"};
    log.verbose("verbose message");
}

TEST_CASE("logger::log with explicit level", "[idfxx][log]") {
    constexpr logger log{"test_log"};
    log.log(level::info, "explicit level message: {}", 99);
}

TEST_CASE("free function error logs without crashing", "[idfxx][log]") {
    idfxx::log::error("free_error", "error: {}", 1);
}

TEST_CASE("free function warn logs without crashing", "[idfxx][log]") {
    idfxx::log::warn("free_warn", "warn: {}", 2);
}

TEST_CASE("free function info logs without crashing", "[idfxx][log]") {
    idfxx::log::info("free_info", "info: {}", 3);
}

TEST_CASE("free function debug logs without crashing", "[idfxx][log]") {
    idfxx::log::debug("free_debug", "debug: {}", 4);
}

TEST_CASE("free function verbose logs without crashing", "[idfxx][log]") {
    idfxx::log::verbose("free_verbose", "verbose: {}", 5);
}

TEST_CASE("free function log with explicit level", "[idfxx][log]") {
    idfxx::log::log(level::warn, "free_log", "explicit: {}", "test");
}

TEST_CASE("set_level suppresses messages below threshold", "[idfxx][log]") {
    constexpr logger log{"test_set_level"};

    // Set level to error only
    log.set_level(level::error);

    // These should not crash (they are suppressed at runtime)
    log.info("this should be suppressed");
    log.warn("this should be suppressed");
    log.error("this should still appear");

    // Restore to verbose for other tests
    log.set_level(level::verbose);
}

TEST_CASE("set_default_level does not crash", "[idfxx][log]") {
    set_default_level(level::info);
    // Restore
    set_default_level(level::verbose);
}

TEST_CASE("free function set_level does not crash", "[idfxx][log]") {
    set_level("free_level_test", level::warn);
    // Log at warn should work
    idfxx::log::warn("free_level_test", "this should appear");
    // Log at info should be suppressed
    idfxx::log::info("free_level_test", "this should be suppressed");
}

TEST_CASE("log with multiple argument types", "[idfxx][log]") {
    constexpr logger log{"test_types"};
    int i = 42;
    double d = 3.14;
    std::string s = "hello";
    const char* c = "world";
    log.info("int={} double={:.2f} string={} cstr={}", i, d, s, c);
}

TEST_CASE("log with no format arguments", "[idfxx][log]") {
    constexpr logger log{"test_noargs"};
    log.info("simple message with no arguments");
}

TEST_CASE("IDFXX_LOGI macro logs without crashing", "[idfxx][log]") {
    IDFXX_LOGI("test_macro", "macro info: {}", 42);
}

TEST_CASE("IDFXX_LOGE macro logs without crashing", "[idfxx][log]") {
    IDFXX_LOGE("test_macro", "macro error: {}", "err");
}

TEST_CASE("IDFXX_LOGW macro logs without crashing", "[idfxx][log]") {
    IDFXX_LOGW("test_macro", "macro warn");
}

TEST_CASE("IDFXX_LOGD macro logs without crashing", "[idfxx][log]") {
    IDFXX_LOGD("test_macro", "macro debug: {}", 1.5);
}

TEST_CASE("IDFXX_LOGV macro logs without crashing", "[idfxx][log]") {
    IDFXX_LOGV("test_macro", "macro verbose: {} {}", "a", "b");
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
static const uint8_t test_buffer[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x57,
                                      0x6F, 0x72, 0x6C, 0x64, 0x21, 0x00, 0xFF, 0x80};

TEST_CASE("free function buffer_hex logs without crashing", "[idfxx][log]") {
    idfxx::log::buffer_hex(level::info, "test_buf", test_buffer, sizeof(test_buffer));
}

TEST_CASE("free function buffer_char logs without crashing", "[idfxx][log]") {
    idfxx::log::buffer_char(level::info, "test_buf", test_buffer, sizeof(test_buffer));
}

TEST_CASE("free function buffer_hex_dump logs without crashing", "[idfxx][log]") {
    idfxx::log::buffer_hex_dump(level::info, "test_buf", test_buffer, sizeof(test_buffer));
}

TEST_CASE("logger::buffer_hex logs without crashing", "[idfxx][log]") {
    constexpr logger log{"test_buf_logger"};
    log.buffer_hex(level::debug, test_buffer, sizeof(test_buffer));
}

TEST_CASE("logger::buffer_char logs without crashing", "[idfxx][log]") {
    constexpr logger log{"test_buf_logger"};
    log.buffer_char(level::debug, test_buffer, sizeof(test_buffer));
}

TEST_CASE("logger::buffer_hex_dump logs without crashing", "[idfxx][log]") {
    constexpr logger log{"test_buf_logger"};
    log.buffer_hex_dump(level::debug, test_buffer, sizeof(test_buffer));
}

// =============================================================================
// to_string tests
// =============================================================================

TEST_CASE("to_string(level) outputs correct names", "[idfxx][log]") {
    TEST_ASSERT_EQUAL_STRING("NONE", idfxx::to_string(level::none).c_str());
    TEST_ASSERT_EQUAL_STRING("ERROR", idfxx::to_string(level::error).c_str());
    TEST_ASSERT_EQUAL_STRING("WARN", idfxx::to_string(level::warn).c_str());
    TEST_ASSERT_EQUAL_STRING("INFO", idfxx::to_string(level::info).c_str());
    TEST_ASSERT_EQUAL_STRING("DEBUG", idfxx::to_string(level::debug).c_str());
    TEST_ASSERT_EQUAL_STRING("VERBOSE", idfxx::to_string(level::verbose).c_str());
}

TEST_CASE("to_string(level) handles unknown values", "[idfxx][log]") {
    auto unknown = static_cast<level>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", idfxx::to_string(unknown).c_str());
}

// =============================================================================
// Formatter tests
// =============================================================================

static_assert(std::formattable<level, char>);

TEST_CASE("level formatter outputs correct names", "[idfxx][log]") {
    TEST_ASSERT_EQUAL_STRING("NONE", std::format("{}", level::none).c_str());
    TEST_ASSERT_EQUAL_STRING("ERROR", std::format("{}", level::error).c_str());
    TEST_ASSERT_EQUAL_STRING("WARN", std::format("{}", level::warn).c_str());
    TEST_ASSERT_EQUAL_STRING("INFO", std::format("{}", level::info).c_str());
    TEST_ASSERT_EQUAL_STRING("DEBUG", std::format("{}", level::debug).c_str());
    TEST_ASSERT_EQUAL_STRING("VERBOSE", std::format("{}", level::verbose).c_str());
}

TEST_CASE("level formatter handles unknown values", "[idfxx][log]") {
    auto unknown = static_cast<level>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", std::format("{}", unknown).c_str());
}
