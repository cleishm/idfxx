// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Tests for idfxx_log's sinks: the text helpers on their own, adding and removing sinks, and the
// lines that a sink added without all_lines receives. The sinks added with all_lines are tested in
// all_lines_test.cpp.

#include "../src/line_text.hpp"
#include "qemu_log_cache.hpp"
#include "recording_sink.hpp"
#include "sdkconfig.h"
#include "test_cleanup.hpp"
#include "unity.h"

#include <idfxx/log>

#include <array>
#include <atomic>
#include <cstdint>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace lg = idfxx::log;
using idfxx::log::level;
using sink_test::recorder;

// =============================================================================
// Compile-time tests
// =============================================================================

// A sink is an interface. Classes derive from it, and it is never instantiated itself.
static_assert(std::is_abstract_v<lg::sink>);
static_assert(std::has_virtual_destructor_v<lg::sink>);
static_assert(std::is_base_of_v<lg::sink, recorder>);

static_assert(noexcept(std::declval<const lg::sink&>().wants(level::info, "")));
static_assert(noexcept(std::declval<lg::sink&>().write(level::info, "", "")));
static_assert(noexcept(lg::remove_sink(std::declval<lg::sink&>())));
static_assert(
    std::is_same_v<decltype(lg::try_add_sink(std::declval<lg::sink&>())), idfxx::result<void>>
);

#ifdef CONFIG_IDFXX_LOG_ALL_LINES
// The tag must be named, and can't be constructed from `{}`, so that add_sink({}, s) doesn't
// select the all_lines overload.
template<typename T>
concept from_empty_braces = requires(void (*f)(T)) { f({}); };
static_assert(!from_empty_braces<lg::all_lines_t>);
static_assert(std::is_same_v<decltype(lg::all_lines), const lg::all_lines_t>);
static_assert(std::is_same_v<
              decltype(lg::try_add_sink(lg::all_lines, std::declval<lg::sink&>())),
              idfxx::result<void>>);
#endif

namespace {

template<typename T>
bool fails_with(const idfxx::result<T>& r, idfxx::errc e) {
    return !r.has_value() && r.error() == e;
}

// =============================================================================
// Sinks that the tests add, removed after each test
// =============================================================================

recorder s_rec{level::warn};
recorder s_other{level::warn};
recorder s_all_rec{level::warn};
std::array<recorder, CONFIG_IDFXX_LOG_MAX_SINKS + 1> s_many;

void remove_test_sinks() {
    lg::remove_sink(s_rec);
    lg::remove_sink(s_other);
    lg::remove_sink(s_all_rec);
    for (auto& r : s_many) {
        lg::remove_sink(r);
    }
    s_rec.clear();
    s_other.clear();
    s_all_rec.clear();
    for (auto& r : s_many) {
        r.clear();
    }
}

// Adds `s` for idfxx::log's lines. The sink is removed after the test.
void add_checked(lg::sink& s) {
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_test_sinks);
    TEST_ASSERT_TRUE(lg::try_add_sink(s).has_value());
}

} // namespace

// =============================================================================
// Text helpers
// =============================================================================

TEST_CASE("log sink message_start cuts through the first tag and colon", "[idfxx][log]") {
    using lg::detail::message_start;
    // Log v1's prefix, with and without colors.
    TEST_ASSERT_EQUAL(15, message_start("W (1234) wifi: hello\n", "wifi"));
    TEST_ASSERT_EQUAL(22, message_start("\x1b[0;33mW (1234) wifi: hello\x1b[0m\n", "wifi"));
    // The Wi-Fi libraries' prefix, without a space after the colon.
    TEST_ASSERT_EQUAL(13, message_start("W (123) wifi:", "wifi"));
    // OpenThread's prefix, without a space before the timestamp.
    TEST_ASSERT_EQUAL(15, message_start("\x1b[0;31mE(5) ot:m", "ot"));
    // Only the first "<tag>:" counts, and only one space after it is skipped.
    TEST_ASSERT_EQUAL(11, message_start("I (1) net: net: x", "net"));
    TEST_ASSERT_EQUAL(11, message_start("I (1) net:  x", "net"));
    // The tag must be followed by its colon. An earlier mention of the tag without a colon is
    // skipped.
    TEST_ASSERT_EQUAL(18, message_start("net up I (1) net: x", "net"));
    // With no "<tag>:" at all, all of the text is the message.
    TEST_ASSERT_EQUAL(0, message_start("plain text\n", "net"));
    TEST_ASSERT_EQUAL(0, message_start("net down\n", "net"));
    TEST_ASSERT_EQUAL(0, message_start("", "net"));
    // With no tag, nothing is removed.
    TEST_ASSERT_EQUAL(0, message_start("x: y", ""));
}

TEST_CASE("log sink without_line_end drops the newline, then a color reset", "[idfxx][log]") {
    using lg::detail::without_line_end;
    TEST_ASSERT_TRUE(without_line_end("hello\n") == "hello");
    TEST_ASSERT_TRUE(without_line_end("hello\x1b[0m\n") == "hello");
    TEST_ASSERT_TRUE(without_line_end("hello\x1b[0m") == "hello");
    TEST_ASSERT_TRUE(without_line_end("hello") == "hello");
    // Only one newline is removed, and the reset is removed only at the very end.
    TEST_ASSERT_TRUE(without_line_end("hello\n\n") == "hello\n");
    TEST_ASSERT_TRUE(without_line_end("\x1b[0;31mhello\n") == "\x1b[0;31mhello");
    TEST_ASSERT_TRUE(without_line_end("hello\n\x1b[0m") == "hello\n");
}

namespace {

std::string utf8_cut(std::string_view s, size_t size) {
    return std::string(lg::detail::utf8_prefix(s, size));
}

void assert_same(std::string_view expected, const std::string& actual) {
    TEST_ASSERT_EQUAL_STRING(std::string(expected).c_str(), actual.c_str());
}

} // namespace

TEST_CASE("log sink utf8_prefix keeps whole characters and ASCII as they are", "[idfxx][log]") {
    // "é", "€" and U+1F600: 2, 3 and 4 bytes, as complete characters.
    const std::string_view whole = "a\xC3\xA9"
                                   "b\xE2\x82\xAC"
                                   "c\xF0\x9F\x98\x80";
    assert_same(whole, utf8_cut(whole, whole.size()));
    assert_same(whole, utf8_cut(whole, 100));
    assert_same("plain ascii", utf8_cut("plain ascii text", 11));
    assert_same("", utf8_cut("abc", 0));
}

TEST_CASE("log sink utf8_prefix trims a character the cut ends partway", "[idfxx][log]") {
    assert_same("a", utf8_cut("a\xC3\xA9", 2));
    assert_same("b", utf8_cut("b\xE2\x82\xAC", 2));
    assert_same("b", utf8_cut("b\xE2\x82\xAC", 3));
    assert_same("c", utf8_cut("c\xF0\x9F\x98\x80", 2));
    assert_same("c", utf8_cut("c\xF0\x9F\x98\x80", 3));
    assert_same("c", utf8_cut("c\xF0\x9F\x98\x80", 4));
}

TEST_CASE(
    "log sink utf8_prefix trims text that fits but ends partway through a character",
    "[idfxx][log]"
) {
    assert_same("x", utf8_cut("x\xE2\x82", 10));
    assert_same("x", utf8_cut("x\xF0", 10));
}

TEST_CASE(
    "log sink utf8_prefix keeps a run of continuation bytes rather than eat it",
    "[idfxx][log]"
) {
    const std::string run(8, '\x80');
    assert_same(run, utf8_cut(run, run.size()));
    assert_same("a" + run, utf8_cut("a" + run, 9));
}

// =============================================================================
// Adding and removing
// =============================================================================

TEST_CASE("log sink can't be added twice", "[idfxx][log]") {
    add_checked(s_rec);
    TEST_ASSERT_TRUE(fails_with(lg::try_add_sink(s_rec), idfxx::errc::invalid_arg));
#ifdef CONFIG_IDFXX_LOG_ALL_LINES
    // A sink also can't be added in the other mode, in either order.
    TEST_ASSERT_TRUE(fails_with(lg::try_add_sink(lg::all_lines, s_rec), idfxx::errc::invalid_arg));
    TEST_ASSERT_TRUE(lg::try_add_sink(lg::all_lines, s_all_rec).has_value());
    TEST_ASSERT_TRUE(
        fails_with(lg::try_add_sink(lg::all_lines, s_all_rec), idfxx::errc::invalid_arg)
    );
    TEST_ASSERT_TRUE(fails_with(lg::try_add_sink(s_all_rec), idfxx::errc::invalid_arg));
#endif
    // The sink is still added only once, so it receives a line once.
    lg::warn("sk_twice", "once {}", 1);
    TEST_ASSERT_EQUAL(1, s_rec.count(level::warn, "sk_twice", "once 1"));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("log sink add_sink throws when the sink is added already", "[idfxx][log]") {
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_test_sinks);
    lg::add_sink(s_rec);
    bool threw = false;
    try {
        lg::add_sink(s_rec);
    } catch (const std::system_error& e) {
        threw = e.code() == idfxx::errc::invalid_arg;
    }
    TEST_ASSERT_TRUE(threw);
#ifdef CONFIG_IDFXX_LOG_ALL_LINES
    threw = false;
    try {
        lg::add_sink(lg::all_lines, s_rec);
    } catch (const std::system_error& e) {
        threw = e.code() == idfxx::errc::invalid_arg;
    }
    TEST_ASSERT_TRUE(threw);
#endif
}
#endif

TEST_CASE(
    "log sink adding past the most sinks fails; a removed one's place is reused",
    "[idfxx][log]"
) {
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_test_sinks);
    for (size_t i = 0; i < CONFIG_IDFXX_LOG_MAX_SINKS; ++i) {
        TEST_ASSERT_TRUE(lg::try_add_sink(s_many[i]).has_value());
    }
    recorder& extra = s_many[CONFIG_IDFXX_LOG_MAX_SINKS];
    TEST_ASSERT_TRUE(fails_with(lg::try_add_sink(extra), idfxx::errc::invalid_state));
#ifdef CONFIG_IDFXX_LOG_ALL_LINES
    TEST_ASSERT_TRUE(
        fails_with(lg::try_add_sink(lg::all_lines, extra), idfxx::errc::invalid_state)
    );
#endif
    // Removing one that isn't added changes nothing.
    lg::remove_sink(extra);
    TEST_ASSERT_TRUE(fails_with(lg::try_add_sink(extra), idfxx::errc::invalid_state));

    lg::remove_sink(s_many[0]);
    TEST_ASSERT_TRUE(lg::try_add_sink(extra).has_value());
    lg::warn("sk_reuse", "reused {}", 1);
    TEST_ASSERT_EQUAL(0, s_many[0].size());
    for (size_t i = 1; i <= CONFIG_IDFXX_LOG_MAX_SINKS; ++i) {
        TEST_ASSERT_EQUAL(1, s_many[i].count(level::warn, "sk_reuse", "reused 1"));
    }
}

TEST_CASE("log sink removing one that isn't added does nothing", "[idfxx][log]") {
    add_checked(s_rec);
    lg::remove_sink(s_other);
    lg::remove_sink(s_other);
    lg::warn("sk_noop", "still {}", "added");
    TEST_ASSERT_TRUE(s_rec.has(level::warn, "sk_noop", "still added"));
    TEST_ASSERT_EQUAL(0, s_other.size());
}

// =============================================================================
// The lines that a sink added without all_lines receives
// =============================================================================

TEST_CASE("log sink is given idfxx::log's lines whole, not ESP-IDF's", "[idfxx][log]") {
    add_checked(s_rec);
    constexpr lg::logger logger{"sk_logger"};
    lg::warn("sk_free", "formatted {} and {}", 42, "more");
    lg::log(level::error, "sk_free", "explicit {}", 3);
    lg::warn("sk_free", std::string_view{"pre-formatted"});
    lg::warn("sk_free", "two\nlines");
    logger.error("from a logger {}", 1);
    IDFXX_LOGW("sk_macro", "from a macro {}", 2);
    // Below the sink's level, so the sink doesn't receive it.
    lg::info("sk_free", "informed {}", 4);
    // These are not idfxx::log's lines.
    ESP_LOGW("sk_free", "esp line %d", 5);
    esp_log_write(ESP_LOG_WARN, "sk_free", "written %d\n", 6);
    const uint8_t bytes[] = {0x01, 0x02};
    lg::buffer_hex(level::warn, "sk_free", bytes);
    logger.buffer_hex_dump(level::warn, bytes);

    TEST_ASSERT_TRUE(s_rec.has(level::warn, "sk_free", "formatted 42 and more"));
    TEST_ASSERT_TRUE(s_rec.has(level::error, "sk_free", "explicit 3"));
    TEST_ASSERT_TRUE(s_rec.has(level::warn, "sk_free", "pre-formatted"));
    TEST_ASSERT_TRUE(s_rec.has(level::warn, "sk_free", "two\nlines"));
    TEST_ASSERT_TRUE(s_rec.has(level::error, "sk_logger", "from a logger 1"));
    TEST_ASSERT_TRUE(s_rec.has(level::warn, "sk_macro", "from a macro 2"));
    TEST_ASSERT_EQUAL(6, s_rec.size());
}

#if ESP_LOG_VERSION == 2 || ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
namespace {
recorder s_untagged{level::warn, true};
void remove_untagged() {
    lg::remove_sink(s_untagged);
    s_untagged.clear();
}
} // namespace

// Log v1 passes the tag to the console's "%s". ESP-IDF 5.5's newlib vprintf faults on a null tag,
// and 6.x's picolibc prints "(null)".
TEST_CASE("log sink is given a line without a tag with an empty tag", "[idfxx][log]") {
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_untagged);
    TEST_ASSERT_TRUE(lg::try_add_sink(s_untagged).has_value());
    lg::log(level::warn, nullptr, std::string_view{"sk untagged"});
    TEST_ASSERT_TRUE(s_untagged.has(level::warn, "", "sk untagged"));
}
#endif

TEST_CASE("log sink takes a line the console's level skips, formatted only then", "[idfxx][log]") {
    add_checked(s_rec);
    sink_test::set_console_level("sk_rt", ESP_LOG_ERROR);
    sink_test::s_formatted = 0;
    // Below the console's level but at the sink's level, so the line is formatted and the sink
    // receives it.
    lg::warn("sk_rt", "kept {}", sink_test::counted{});
    TEST_ASSERT_EQUAL(1, sink_test::s_formatted);
    TEST_ASSERT_TRUE(s_rec.has(level::warn, "sk_rt", "kept counted"));
    // Below both levels, so the line is never formatted.
    lg::info("sk_rt", "unwanted {}", sink_test::counted{});
    TEST_ASSERT_EQUAL(1, sink_test::s_formatted);
    // After the sink is removed, it receives nothing, and the line that the console skips isn't
    // formatted.
    lg::remove_sink(s_rec);
    lg::warn("sk_rt", "removed {}", sink_test::counted{});
    TEST_ASSERT_EQUAL(1, sink_test::s_formatted);
    TEST_ASSERT_EQUAL(1, s_rec.size());
}

namespace {

// Logs with the flash cache reported to ESP-IDF's log as disabled, as when code runs from IRAM with
// the flash cache disabled. Then restores the check that was there before.
void log_with_cache_disabled() {
    sink_test::report_cache_disabled(true);
    lg::warn("sk_cache", "formatted {}", sink_test::counted{});
    lg::warn("sk_cache", std::string_view{"pre-formatted"});
    ESP_LOGW("sk_cache", "esp line");
    sink_test::report_cache_disabled(false);
}

} // namespace

TEST_CASE("log sink is given no line logged where the cache is disabled", "[idfxx][log]") {
    add_checked(s_rec);
#ifdef CONFIG_IDFXX_LOG_ALL_LINES
    TEST_ASSERT_TRUE(lg::try_add_sink(lg::all_lines, s_all_rec).has_value());
#endif
    sink_test::set_console_level("sk_cache", ESP_LOG_ERROR);
    sink_test::s_formatted = 0;
    log_with_cache_disabled();
    // The console skips the lines, and no sink wants them there, so they aren't formatted.
    TEST_ASSERT_EQUAL(0, sink_test::s_formatted);
    TEST_ASSERT_EQUAL(0, s_rec.size());
    TEST_ASSERT_EQUAL(0, s_all_rec.size());
    // With the cache enabled again, the sinks receive the same lines.
    lg::warn("sk_cache", "formatted {}", sink_test::counted{});
    TEST_ASSERT_EQUAL(1, sink_test::s_formatted);
    TEST_ASSERT_TRUE(s_rec.has(level::warn, "sk_cache", "formatted counted"));
#ifdef CONFIG_IDFXX_LOG_ALL_LINES
    TEST_ASSERT_TRUE(s_all_rec.has(level::warn, "sk_cache", "formatted counted"));
#endif
}

// =============================================================================
// Removing a sink while it is receiving a line
// =============================================================================

namespace {

// A sink whose write, once entered, waits until the test releases it.
class blocking_sink final : public lg::sink {
public:
    SemaphoreHandle_t entered = nullptr;
    SemaphoreHandle_t go = nullptr;

private:
    bool do_wants(level, std::string_view tag) const noexcept override {
        return tag == "sk_block";
    }
    void do_write(level, std::string_view, std::string_view) noexcept override {
        xSemaphoreGive(entered);
        xSemaphoreTake(go, portMAX_DELAY);
    }
};

blocking_sink s_blocking;
SemaphoreHandle_t s_removed = nullptr;
SemaphoreHandle_t s_logged = nullptr;
SemaphoreHandle_t s_others_done = nullptr;
lg::sink* s_to_remove = nullptr;
bool s_other_added = false;

void make_semaphores() {
    if (s_blocking.entered == nullptr) {
        s_blocking.entered = xSemaphoreCreateBinary();
        s_blocking.go = xSemaphoreCreateBinary();
        // A counting semaphore, because two removals may finish.
        s_removed = xSemaphoreCreateCounting(2, 0);
        s_logged = xSemaphoreCreateBinary();
        s_others_done = xSemaphoreCreateBinary();
    }
    // Clears anything left from an earlier test.
    xSemaphoreTake(s_blocking.entered, 0);
    xSemaphoreTake(s_blocking.go, 0);
    while (xSemaphoreTake(s_removed, 0) == pdTRUE) {
    }
    xSemaphoreTake(s_logged, 0);
    xSemaphoreTake(s_others_done, 0);
}

void remove_blocking() {
    // Releases a write that is still waiting, so that the removal doesn't wait for it forever.
    xSemaphoreGive(s_blocking.go);
    lg::remove_sink(s_blocking);
}

void log_blocked(void*) {
    lg::warn("sk_block", "blocked {}", 1);
    xSemaphoreGive(s_logged);
    vTaskDelete(nullptr);
}

void remove_on_task(void*) {
    lg::remove_sink(*s_to_remove);
    xSemaphoreGive(s_removed);
    vTaskDelete(nullptr);
}

// Starts a task that logs a line that the blocking sink wants, and returns once the sink is writing
// it.
void start_blocked_write() {
    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(log_blocked, "sk_log", 4096, nullptr, uxTaskPriorityGet(nullptr), nullptr)
    );
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(s_blocking.entered, pdMS_TO_TICKS(2000)));
}

void start_removal(lg::sink& s) {
    s_to_remove = &s;
    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(remove_on_task, "sk_rm", 4096, nullptr, uxTaskPriorityGet(nullptr), nullptr)
    );
}

// Adds s_rec and removes s_other, on a separate task.
void add_and_remove_others(void*) {
    s_other_added = lg::try_add_sink(s_rec).has_value();
    lg::remove_sink(s_other);
    xSemaphoreGive(s_others_done);
    vTaskDelete(nullptr);
}

} // namespace

TEST_CASE("log sink removal waits for the sink's write in progress", "[idfxx][log]") {
    make_semaphores();
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_blocking);
    TEST_ASSERT_TRUE(lg::try_add_sink(s_blocking).has_value());
    start_blocked_write();
    start_removal(s_blocking);
    // The write is still waiting, so the removal can't have returned.
    const bool early = xSemaphoreTake(s_removed, pdMS_TO_TICKS(100)) == pdTRUE;
    xSemaphoreGive(s_blocking.go);
    const bool removed = xSemaphoreTake(s_removed, pdMS_TO_TICKS(2000)) == pdTRUE;
    const bool logged = xSemaphoreTake(s_logged, pdMS_TO_TICKS(2000)) == pdTRUE;
    TEST_ASSERT_FALSE(early);
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_TRUE(logged);
}

TEST_CASE("log sink removal doesn't wait for another sink's write", "[idfxx][log]") {
    make_semaphores();
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_blocking);
    TEST_ASSERT_TRUE(lg::try_add_sink(s_blocking).has_value());
    add_checked(s_other);
    start_blocked_write();
    start_removal(s_other);
    const bool removed = xSemaphoreTake(s_removed, pdMS_TO_TICKS(500)) == pdTRUE;
    xSemaphoreGive(s_blocking.go);
    const bool logged = xSemaphoreTake(s_logged, pdMS_TO_TICKS(2000)) == pdTRUE;
    if (!removed) {
        xSemaphoreTake(s_removed, pdMS_TO_TICKS(2000));
    }
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_TRUE(logged);
    // s_other was removed before the blocking sink let the line continue, so it never received the
    // line.
    TEST_ASSERT_EQUAL(0, s_other.size());
}

TEST_CASE(
    "log sink removal waiting for a write holds up no other sink's add or remove",
    "[idfxx][log]"
) {
    make_semaphores();
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_blocking);
    TEST_ASSERT_TRUE(lg::try_add_sink(s_blocking).has_value());
    add_checked(s_other);
    start_blocked_write();
    start_removal(s_blocking);
    const bool early = xSemaphoreTake(s_removed, pdMS_TO_TICKS(100)) == pdTRUE;
    s_other_added = false;
    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(
            add_and_remove_others, "sk_oth", 4096, nullptr, uxTaskPriorityGet(nullptr), nullptr
        )
    );
    const bool others_done = xSemaphoreTake(s_others_done, pdMS_TO_TICKS(500)) == pdTRUE;
    xSemaphoreGive(s_blocking.go);
    const bool removed = xSemaphoreTake(s_removed, pdMS_TO_TICKS(2000)) == pdTRUE;
    const bool logged = xSemaphoreTake(s_logged, pdMS_TO_TICKS(2000)) == pdTRUE;
    if (!others_done) {
        xSemaphoreTake(s_others_done, pdMS_TO_TICKS(2000));
    }
    TEST_ASSERT_FALSE(early);
    TEST_ASSERT_TRUE(others_done);
    TEST_ASSERT_TRUE(s_other_added);
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_TRUE(logged);
}

TEST_CASE("log sink removals of the same sink at once both wait for its write", "[idfxx][log]") {
    make_semaphores();
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_blocking);
    TEST_ASSERT_TRUE(lg::try_add_sink(s_blocking).has_value());
    start_blocked_write();
    start_removal(s_blocking);
    start_removal(s_blocking);
    // Neither returns while the write is waiting.
    const bool early = xSemaphoreTake(s_removed, pdMS_TO_TICKS(100)) == pdTRUE;
    xSemaphoreGive(s_blocking.go);
    const bool first = xSemaphoreTake(s_removed, pdMS_TO_TICKS(2000)) == pdTRUE;
    const bool second = xSemaphoreTake(s_removed, pdMS_TO_TICKS(2000)) == pdTRUE;
    const bool logged = xSemaphoreTake(s_logged, pdMS_TO_TICKS(2000)) == pdTRUE;
    TEST_ASSERT_FALSE(early);
    TEST_ASSERT_TRUE(first);
    TEST_ASSERT_TRUE(second);
    TEST_ASSERT_TRUE(logged);
    // Once removed, the sink can be added again.
    TEST_ASSERT_TRUE(lg::try_add_sink(s_blocking).has_value());
}

// =============================================================================
// Reentrancy
// =============================================================================

namespace {

// A sink that logs from its write. Its own lines are among the lines that it wants.
class reentrant_sink final : public lg::sink {
public:
    std::atomic<int> writes{0};

private:
    bool do_wants(level, std::string_view tag) const noexcept override { return tag == "sk_re"; }
    void do_write(level, std::string_view, std::string_view) noexcept override {
        ++writes;
        lg::warn("sk_re", "inner {}", sink_test::counted{});
        ESP_LOGW("sk_re", "inner esp line");
    }
};

reentrant_sink s_reentrant;

void remove_reentrant() {
    lg::remove_sink(s_reentrant);
}

} // namespace

TEST_CASE("log sink that logs isn't given its own lines", "[idfxx][log]") {
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_reentrant);
    s_reentrant.writes = 0;
    TEST_ASSERT_TRUE(lg::try_add_sink(s_reentrant).has_value());
    sink_test::set_console_level("sk_re", ESP_LOG_ERROR);
    sink_test::s_formatted = 0;
    lg::warn("sk_re", "outer {}", sink_test::counted{});
    TEST_ASSERT_EQUAL(1, s_reentrant.writes.load());
    // The outer line is formatted for the sink. The inner line is not formatted at all, because the
    // console skips it and it is logged from inside the sink.
    TEST_ASSERT_EQUAL(1, sink_test::s_formatted);
    lg::remove_sink(s_reentrant);

#ifdef CONFIG_IDFXX_LOG_ALL_LINES
    s_reentrant.writes = 0;
    TEST_ASSERT_TRUE(lg::try_add_sink(lg::all_lines, s_reentrant).has_value());
    ESP_LOGW("sk_re", "outer esp line");
    TEST_ASSERT_EQUAL(1, s_reentrant.writes.load());
    lg::warn("sk_re", "outer {}", 2);
    TEST_ASSERT_EQUAL(2, s_reentrant.writes.load());
#endif
}
