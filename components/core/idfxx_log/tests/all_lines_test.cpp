// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Tests for the sinks added with all_lines. These sinks receive ESP-IDF's log lines, both complete
// lines and lines joined from several calls.

#include "sdkconfig.h"

#ifdef CONFIG_IDFXX_LOG_ALL_LINES

#include "qemu_log_cache.hpp"
#include "recording_sink.hpp"
#include "test_cleanup.hpp"
#include "unity.h"

#include <idfxx/log>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>
#include <string_view>

// The real esp_log, which the wrapper passes calls on to.
extern "C" void __real_esp_log(esp_log_config_t config, const char* tag, const char* format, ...);

namespace lg = idfxx::log;
using idfxx::log::level;
using sink_test::recorder;

namespace {

// =============================================================================
// Helpers
// =============================================================================

// These sinks record warnings and errors.
recorder s_all{level::warn};
recorder s_untagged{level::warn, true};
recorder s_direct{level::warn};

void remove_all_lines_sinks() {
    lg::remove_sink(s_all);
    lg::remove_sink(s_untagged);
    lg::remove_sink(s_direct);
    s_all.clear();
    s_untagged.clear();
    s_direct.clear();
}

// Adds `s` with all_lines. The sink is removed after the test.
void add_all_lines(lg::sink& s) {
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_all_lines_sinks);
    TEST_ASSERT_TRUE(lg::try_add_sink(lg::all_lines, s).has_value());
}

// Returns `lead` ASCII bytes, followed by 3-byte UTF-8 characters until the text is longer than
// `size` bytes.
std::string three_byte_text(size_t lead, size_t size) {
    std::string text(lead, 'a');
    while (text.size() <= size) {
        text += "\xE2\x82\xAC";
    }
    return text;
}

// A vprintf installed in place of the console's, which counts its calls and prints nothing.
vprintf_like_t s_other_previous = nullptr;
bool s_other_installed = false;
int s_other_calls = 0;

int silent_vprintf(const char*, va_list) {
    ++s_other_calls;
    return 0;
}

void restore_vprintf() {
    if (s_other_installed) {
        esp_log_set_vprintf(s_other_previous);
        s_other_installed = false;
    }
}

void install_silent_vprintf() {
    idfxx::test::run_after_each_test(restore_vprintf);
    s_other_previous = esp_log_set_vprintf(silent_vprintf);
    s_other_installed = true;
    s_other_calls = 0;
}

// A sink that records, when it receives a line, how many calls the silent vprintf has received so
// far.
class console_order_sink final : public lg::sink {
public:
    std::atomic<int> printed_before{-1};

private:
    bool do_wants(level, std::string_view tag) const noexcept override {
        return tag == "sk_order";
    }
    void do_write(level, std::string_view, std::string_view) noexcept override {
        printed_before = s_other_calls;
    }
};

console_order_sink s_order_all;
console_order_sink s_order_direct;

void remove_order_sinks() {
    lg::remove_sink(s_order_all);
    lg::remove_sink(s_order_direct);
}

// A task that runs `step` once, then waits until it is told to end, so that its handle isn't reused
// before then.
struct held_task {
    TaskHandle_t handle = nullptr;
    TaskHandle_t waiter = nullptr;
    void (*step)(int) = nullptr;
    int arg = 0;
};

void held_task_main(void* p) {
    auto* h = static_cast<held_task*>(p);
    h->step(h->arg);
    xTaskNotifyGive(h->waiter);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vTaskDelete(nullptr);
}

// Runs `step(arg)` on a new task, and returns when it has run. The task then waits for end_held().
void run_held(held_task& h, void (*step)(int), int arg) {
    h.waiter = xTaskGetCurrentTaskHandle();
    h.step = step;
    h.arg = arg;
    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(held_task_main, "sk_held", 4096, &h, uxTaskPriorityGet(nullptr), &h.handle)
    );
    TEST_ASSERT_EQUAL(1, ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)));
}

void end_held(held_task& h) {
    xTaskNotifyGive(h.handle);
}

void leave_partial(int i) {
    char tag[16];
    std::snprintf(tag, sizeof tag, "sk_p%d", i);
    esp_log_write(ESP_LOG_WARN, tag, "%c (%d) %s:", 'W', 100 + i, tag);
    esp_log_write(ESP_LOG_WARN, tag, "%s", "sk partial");
}

void finish_line(int i) {
    esp_log_write(ESP_LOG_WARN, "sk_last", "%c (%d) %s:", 'W', 100 + i, "sk_last");
    esp_log_write(ESP_LOG_WARN, "sk_last", "%s", "sk finished");
    esp_log_write(ESP_LOG_WARN, "sk_last", "%s", "\n");
}

void leave_stale(int) {
    esp_log_write(ESP_LOG_WARN, "sk_stale", "%c (%d) %s:", 'W', 1, "sk_stale");
    esp_log_write(ESP_LOG_WARN, "sk_stale", "%s", "sk stale partial");
}

// A task that waits to be told to log a line, then waits to be deleted.
struct waiting_task {
    TaskHandle_t handle = nullptr;
    TaskHandle_t waiter = nullptr;
};

void waiting_task_main(void* p) {
    auto* w = static_cast<waiting_task*>(p);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    esp_log_write(ESP_LOG_WARN, "sk_new", "W (%d) %s: sk new task's line\n", 2, "sk_new");
    xTaskNotifyGive(w->waiter);
    vTaskSuspend(nullptr);
}

// A sink added with all_lines that wants one tag's lines and records only their size, so that it
// uses little stack itself.
class sizing_sink final : public lg::sink {
public:
    std::atomic<size_t> last{0};

private:
    bool do_wants(level, std::string_view tag) const noexcept override {
        return tag == "sk_stack";
    }
    void do_write(level, std::string_view, std::string_view message) noexcept override {
        last = message.size();
    }
};

sizing_sink s_sizing;

void remove_sizing() {
    lg::remove_sink(s_sizing);
}

// A task that logs one long warning and records its stack's high-water mark.
struct stack_probe {
    TaskHandle_t waiter = nullptr;
    const char* message = nullptr;
    bool real = false;
    size_t free_bytes = 0;
};

void stack_probe_main(void* p) {
    auto* probe = static_cast<stack_probe*>(p);
    if (probe->real) {
        // The same as ESP_LOGW's expansion, but calling the real esp_log instead of the wrapper.
#if ESP_LOG_VERSION == 2
        __real_esp_log(
            ESP_LOG_CONFIG_INIT(ESP_LOG_WARN | ESP_LOG_CONFIGS_DEFAULT),
            "sk_stack",
            "%s",
            probe->message
        );
#else
        __real_esp_log(
            ESP_LOG_CONFIG_INIT(ESP_LOG_WARN),
            "sk_stack",
            LOG_FORMAT(W, "%s"),
            esp_log_timestamp(),
            "sk_stack",
            probe->message
        );
#endif
    } else {
        ESP_LOGW("sk_stack", "%s", probe->message);
    }
    probe->free_bytes = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
    xTaskNotifyGive(probe->waiter);
    vTaskSuspend(nullptr);
}

// The least stack that logging one long warning uses, measured over a few new tasks.
size_t stack_to_log(const char* message, bool real) {
    constexpr uint32_t stack_size = 4096;
    size_t most_free = 0;
    for (int i = 0; i < 4; ++i) {
        stack_probe probe{.waiter = xTaskGetCurrentTaskHandle(), .message = message, .real = real};
        TaskHandle_t task = nullptr;
        TEST_ASSERT_EQUAL(
            pdPASS,
            xTaskCreate(
                stack_probe_main, "sk_stack", stack_size, &probe, uxTaskPriorityGet(nullptr), &task
            )
        );
        TEST_ASSERT_EQUAL(1, ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)));
        vTaskDelete(task);
        most_free = std::max(most_free, probe.free_bytes);
    }
    return stack_size - most_free;
}

} // namespace

// =============================================================================
// Complete lines
// =============================================================================

TEST_CASE("log all-lines sink takes ESP_LOGx lines it wants, whole", "[idfxx][log]") {
    add_all_lines(s_all);
    ESP_LOGW("sk_cap", "warned %d", 42);
    ESP_LOGE("sk_cap", "failed: %s", "badly");
    ESP_LOGI("sk_cap", "informed");
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_cap", "warned 42"));
    TEST_ASSERT_TRUE(s_all.has(level::error, "sk_cap", "failed: badly"));
    TEST_ASSERT_FALSE(s_all.containing("informed"));
    // ESP-IDF's own timestamp and colors are removed.
    TEST_ASSERT_FALSE(s_all.containing("("));
    TEST_ASSERT_FALSE(s_all.containing("\x1b"));
}

TEST_CASE("log all-lines sink takes idfxx::log's lines", "[idfxx][log]") {
    add_all_lines(s_all);
    lg::warn("sk_fmt", "formatted {} and {}", 42, "more");
    lg::info("sk_fmt", "not taken {}", 1);
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_fmt", "formatted 42 and more"));
    TEST_ASSERT_FALSE(s_all.containing("not taken"));
}

TEST_CASE("log line reaches each sink once with sinks of both kinds added", "[idfxx][log]") {
    add_all_lines(s_all);
    TEST_ASSERT_TRUE(lg::try_add_sink(s_direct).has_value());
    lg::warn("sk_both", "once {}", 1);
    lg::warn("sk_both", std::string_view{"once as a string"});
    ESP_LOGW("sk_both", "esp line");
    TEST_ASSERT_EQUAL(1, s_all.count(level::warn, "sk_both", "once 1"));
    TEST_ASSERT_EQUAL(1, s_direct.count(level::warn, "sk_both", "once 1"));
    TEST_ASSERT_EQUAL(1, s_all.count(level::warn, "sk_both", "once as a string"));
    TEST_ASSERT_EQUAL(1, s_direct.count(level::warn, "sk_both", "once as a string"));
    TEST_ASSERT_EQUAL(1, s_all.count(level::warn, "sk_both", "esp line"));
    TEST_ASSERT_EQUAL(3, s_all.size());
    TEST_ASSERT_EQUAL(2, s_direct.size());
}

TEST_CASE("log all-lines sink is given an idfxx::log line whole, however long", "[idfxx][log]") {
    add_all_lines(s_all);
    constexpr size_t most = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
    const std::string body = "sk " + std::string(2 * most, 'x');
    lg::warn("sk_whole_fx", "{}", body);
    lg::warn("sk_whole_fx", std::string_view{body});
    // An ESP_LOGx line of the same length is shortened.
    ESP_LOGW("sk_whole_esp", "%s", body.c_str());
    TEST_ASSERT_EQUAL(2, s_all.count(level::warn, "sk_whole_fx", body));
    TEST_ASSERT_TRUE(s_all.last_message("sk_whole_esp").size() < body.size());
    TEST_ASSERT_EQUAL(3, s_all.size());
}

TEST_CASE(
    "log sinks of both kinds are given an idfxx::log line once it is printed",
    "[idfxx][log]"
) {
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_order_sinks);
    s_order_all.printed_before = -1;
    s_order_direct.printed_before = -1;
    TEST_ASSERT_TRUE(lg::try_add_sink(lg::all_lines, s_order_all).has_value());
    TEST_ASSERT_TRUE(lg::try_add_sink(s_order_direct).has_value());
    install_silent_vprintf();
    lg::warn("sk_order", "sk printed first {}", 1);
    const int printed = s_other_calls;
    restore_vprintf();
    remove_order_sinks();
    TEST_ASSERT_GREATER_THAN(0, printed);
    TEST_ASSERT_EQUAL(printed, s_order_all.printed_before.load());
    TEST_ASSERT_EQUAL(printed, s_order_direct.printed_before.load());
}

TEST_CASE("log all-lines sink is given each line once across adds and removes", "[idfxx][log]") {
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_all_lines_sinks);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(lg::try_add_sink(lg::all_lines, s_all).has_value());
        ESP_LOGW("sk_cycle", "taken in cycle %d", i);
        lg::remove_sink(s_all);
        ESP_LOGW("sk_cycle", "removed in cycle %d", i);
    }
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL(
            1, s_all.count(level::warn, "sk_cycle", "taken in cycle " + std::to_string(i))
        );
    }
    TEST_ASSERT_EQUAL(3, s_all.size());
}

// =============================================================================
// Lines in several calls
// =============================================================================

TEST_CASE("log all-lines sink is given a line joined from several calls", "[idfxx][log]") {
    add_all_lines(s_all);
    // A Wi-Fi library's line: prefix, message, newline.
    esp_log_write(ESP_LOG_WARN, "sk_wifi", "%c (%d) %s:", 'W', 123, "sk_wifi");
    esp_log_write(ESP_LOG_WARN, "sk_wifi", "%s", "sk beacon: timeout");
    esp_log_write(ESP_LOG_WARN, "sk_wifi", "%s", "\n");
    // An info line is dropped, and the sink still receives the next line.
    esp_log_write(ESP_LOG_INFO, "sk_wifi", "%c (%d) %s:", 'I', 124, "sk_wifi");
    esp_log_write(ESP_LOG_INFO, "sk_wifi", "sk dropped %s", "wifi info");
    esp_log_write(ESP_LOG_INFO, "sk_wifi", "%s", "\n");
    // A log v2 line arrives complete, and its format is the caller's own.
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_ERROR | ESP_LOG_CONFIG_REQUIRE_FORMATTING),
        "sk_v2",
        "v2 message %d",
        7
    );
    // A dropped Wi-Fi debug line, then a warning immediately after it.
    esp_log_write(ESP_LOG_DEBUG, "sk_v2", "%c (%d) %s:", 'D', 126, "sk_v2");
    esp_log_write(ESP_LOG_DEBUG, "sk_v2", "sk dropped %s", "wifi debug");
    esp_log_write(ESP_LOG_DEBUG, "sk_v2", "%s", "\n");
    esp_log_write(ESP_LOG_WARN, "sk_v2", "%c (%d) %s:", 'W', 127, "sk_v2");
    esp_log_write(ESP_LOG_WARN, "sk_v2", "%s", "after the dropped one");
    esp_log_write(ESP_LOG_WARN, "sk_v2", "%s", "\n");
    // A v1 prefix in its own call, then the message, then the color reset and newline, as the BLE
    // mesh library logs them.
    esp_log_write(ESP_LOG_WARN, "sk_v1s", "\x1b[0;33mW (%lu) %s: ", 128ul, "sk_v1s");
    esp_log_write(ESP_LOG_WARN, "sk_v1s", "mesh message %d", 9);
    esp_log_write(ESP_LOG_WARN, "sk_v1s", "\x1b[0m\n");
    // An OpenThread line: a prefix with no space after its tag, then the message and the end.
    esp_log_write(ESP_LOG_WARN, "sk_ot", "\x1b[0;33mW(%lu) %s:", 129ul, "sk_ot");
    esp_log_write(ESP_LOG_WARN, "sk_ot", "ot message %d", 10);
    esp_log_write(ESP_LOG_WARN, "sk_ot", "\x1b[0m\n");

    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_wifi", "sk beacon: timeout"));
    TEST_ASSERT_TRUE(s_all.has(level::error, "sk_v2", "v2 message 7"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_v2", "after the dropped one"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_v1s", "mesh message 9"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_ot", "ot message 10"));
    TEST_ASSERT_FALSE(s_all.containing("sk dropped"));
    TEST_ASSERT_FALSE(s_all.containing("\x1b"));
    TEST_ASSERT_EQUAL(5, s_all.size());
}

TEST_CASE("log all-lines sink is given a joined line at its first call's level", "[idfxx][log]") {
    add_all_lines(s_all);
    // The line starts at warning level. Its later calls, with the same tag, are at error level.
    esp_log_write(ESP_LOG_WARN, "sk_join", "%c (%d) %s:", 'W', 1, "sk_join");
    esp_log_write(ESP_LOG_ERROR, "sk_join", "%s", "sk joined");
    esp_log_write(ESP_LOG_ERROR, "sk_join", "%s", "\n");
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_join", "sk joined"));
    TEST_ASSERT_EQUAL(1, s_all.size());
}

TEST_CASE("log all-lines sink is given a joined line with its first call's tag", "[idfxx][log]") {
    add_all_lines(s_all);
    // A line ended by a call without a tag.
    esp_log_write(ESP_LOG_WARN, "sk_tag1", "%c (%d) %s:", 'W', 1, "sk_tag1");
    esp_log_write(ESP_LOG_WARN, "sk_tag1", "%s", "sk null end");
    esp_log_write(ESP_LOG_WARN, nullptr, "\n");
    // A line continued and ended with a tag that the sink doesn't want. The sink still receives the
    // line, with its first call's tag.
    esp_log_write(ESP_LOG_WARN, "sk_tag2", "%c (%d) %s:", 'W', 2, "sk_tag2");
    esp_log_write(ESP_LOG_WARN, "other", "%s", "sk other end");
    esp_log_write(ESP_LOG_WARN, "other", "%s", "\n");
    TEST_ASSERT_EQUAL(1, s_all.count(level::warn, "sk_tag1", "sk null end"));
    TEST_ASSERT_EQUAL(1, s_all.count(level::warn, "sk_tag2", "sk other end"));
    TEST_ASSERT_EQUAL(2, s_all.size());
}

TEST_CASE(
    "log all-lines sink: a joined line's tag counts toward the longest line",
    "[idfxx][log]"
) {
    add_all_lines(s_all);
    constexpr size_t most = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
    // A tag longer than half of the maximum line length is shortened to that half. The message
    // gets the rest.
    const std::string tag = "sk_" + std::string(most, 't');
    const std::string body(2 * most, 'x');
    esp_log_write(ESP_LOG_WARN, tag.c_str(), "%s", "sk long ");
    esp_log_write(ESP_LOG_WARN, tag.c_str(), "%s", body.c_str());
    esp_log_write(ESP_LOG_WARN, tag.c_str(), "%s", "\n");
    const auto lines = s_all.lines();
    TEST_ASSERT_EQUAL(1, lines.size());
    TEST_ASSERT_EQUAL_STRING(tag.substr(0, most / 2).c_str(), lines[0].tag.c_str());
    const std::string message = "sk long " + body.substr(0, most - most / 2 - 8);
    TEST_ASSERT_EQUAL_STRING(message.c_str(), lines[0].message.c_str());
}

TEST_CASE("log all-lines sink finds the end of an unwanted line from its text", "[idfxx][log]") {
    add_all_lines(s_all);
    // A line in one call, with its newline in an argument. The sink still receives the next line.
    const std::string text = "sk dropped whole\n";
    esp_log_write(
        ESP_LOG_INFO,
        "sk_end",
        "I (%d) %s: %.*s",
        1,
        "sk_end",
        static_cast<int>(text.size()),
        text.c_str()
    );
    esp_log_write(ESP_LOG_WARN, "sk_end", "W (%d) %s: sk after whole\n", 2, "sk_end");
    TEST_ASSERT_FALSE(s_all.containing("sk dropped"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_end", "sk after whole"));
    TEST_ASSERT_EQUAL(1, s_all.size());
}

TEST_CASE(
    "log all-lines sink finds the end of an unwanted line in several calls from its text",
    "[idfxx][log]"
) {
    add_all_lines(s_all);
    // The line's newline is in an argument of the last call. The sink still receives the next line.
    esp_log_write(ESP_LOG_INFO, "sk_end", "%c (%d) %s:", 'I', 3, "sk_end");
    esp_log_write(ESP_LOG_INFO, "sk_end", "%s%c", "sk dropped rest", '\n');
    esp_log_write(ESP_LOG_WARN, "sk_end", "W (%d) %s: sk after joined\n", 4, "sk_end");
    TEST_ASSERT_FALSE(s_all.containing("sk dropped"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_end", "sk after joined"));
    TEST_ASSERT_EQUAL(1, s_all.size());
}

TEST_CASE(
    "log all-lines sink finds the end of a line past the longest from its text",
    "[idfxx][log]"
) {
    add_all_lines(s_all);
    constexpr size_t most = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
    // The line is shortened to the maximum length, and its newline is in an argument of a later
    // call. The sink receives the line, and still receives the next line.
    const std::string body(2 * most, 'x');
    esp_log_write(ESP_LOG_WARN, "sk_end", "%c (%d) %s:", 'W', 5, "sk_end");
    esp_log_write(ESP_LOG_WARN, "sk_end", "%s", body.c_str());
    esp_log_write(ESP_LOG_WARN, "sk_end", "%s%c", "sk past the end", '\n');
    esp_log_write(ESP_LOG_WARN, "sk_end", "W (%d) %s: sk after full\n", 6, "sk_end");
    // Its tag, "sk_end", and the message share the maximum line length.
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_end", std::string(most - 6, 'x')));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_end", "sk after full"));
    TEST_ASSERT_EQUAL(2, s_all.size());
}

TEST_CASE("log all-lines sink takes a v2 line's format as its message", "[idfxx][log]") {
    add_all_lines(s_all);
    // A v2 message is the caller's own. Nothing in it is removed, including its tag and colon.
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_ERROR | ESP_LOG_CONFIG_REQUIRE_FORMATTING),
        "sk_v2m",
        "W (%d) sk_v2m: %s",
        1,
        "x"
    );
    // A newline that ends the message isn't removed either, because ESP-IDF adds the line's own
    // newline.
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_ERROR | ESP_LOG_CONFIG_REQUIRE_FORMATTING),
        "sk_v2e",
        "sk v2 ends in a newline %d\n",
        2
    );
    // A line with a binary-mode config is not text, so the sink doesn't receive it.
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_ERROR | ESP_LOG_CONFIG_BINARY_MODE),
        "sk_v2m",
        "sk dropped binary"
    );
    // The next esp_log_write call starts a new line.
    esp_log_write(ESP_LOG_WARN, "sk_v1n", "W (%d) %s: %s\n", 3, "sk_v1n", "next");

    TEST_ASSERT_TRUE(s_all.has(level::error, "sk_v2m", "W (1) sk_v2m: x"));
    TEST_ASSERT_TRUE(s_all.has(level::error, "sk_v2e", "sk v2 ends in a newline 2\n"));
    TEST_ASSERT_FALSE(s_all.containing("sk dropped"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_v1n", "next"));
}

TEST_CASE("log all-lines sink cuts a line's first call through its tag", "[idfxx][log]") {
    add_all_lines(s_all);
    // Only the first "<tag>:" is removed. One in the message stays.
    esp_log_write(ESP_LOG_WARN, "sk_first", "W (%d) %s: %s\n", 1, "sk_first", "sk_first: again");
    // A first call without "<tag>:" is kept in full.
    esp_log_write(ESP_LOG_ERROR, "sk_raw", "sk raw text %d\n", 2);
    // In a line without a prefix, whose message contains "<tag>:", the text before the tag is
    // removed.
    esp_log_write(ESP_LOG_ERROR, "sk_lose", "sk lost sk_lose: sk rest\n");
    // Nothing is removed from a call in the middle of a line.
    esp_log_write(ESP_LOG_WARN, "sk_mid", "%c (%d) %s:", 'W', 3, "sk_mid");
    esp_log_write(ESP_LOG_WARN, "sk_mid", "%s", "sk_mid: inside");
    esp_log_write(ESP_LOG_WARN, "sk_mid", "%s", "\n");
    // A message whose newline comes in a separate call.
    esp_log_write(ESP_LOG_WARN, "sk_nl", "W (%d) %s: sk first part", 4, "sk_nl");
    esp_log_write(ESP_LOG_WARN, "sk_nl", " and %s\n", "the rest");

    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_first", "sk_first: again"));
    TEST_ASSERT_TRUE(s_all.has(level::error, "sk_raw", "sk raw text 2"));
    TEST_ASSERT_TRUE(s_all.has(level::error, "sk_lose", "sk rest"));
    TEST_ASSERT_FALSE(s_all.containing("sk lost"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_mid", "sk_mid: inside"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_nl", "sk first part and the rest"));
}

TEST_CASE(
    "log all-lines sink drops a line unwanted at its start with its later calls",
    "[idfxx][log]"
) {
    add_all_lines(s_all);
    // An info line in three calls. Its later calls are at warning level, and would be wanted on
    // their own.
    esp_log_write(ESP_LOG_INFO, "sk_drop", "%c (%d) %s:", 'I', 1, "sk_drop");
    esp_log_write(ESP_LOG_WARN, "sk_drop", "sk dropped %s", "middle");
    esp_log_write(ESP_LOG_WARN, "sk_drop", "%s", "\n");
    // A v1 prefix in its own call at info level, followed by its message and end.
    esp_log_write(ESP_LOG_INFO, "sk_drop", "I (%lu) %s: ", 2ul, "sk_drop");
    esp_log_write(ESP_LOG_INFO, "sk_drop", "sk dropped %d", 2);
    esp_log_write(ESP_LOG_INFO, "sk_drop", "\n");
    // The next line is handled as a new line, and is kept.
    esp_log_write(ESP_LOG_WARN, "sk_drop", "W (%d) %s: sk kept after\n", 3, "sk_drop");

    TEST_ASSERT_FALSE(s_all.containing("sk dropped"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_drop", "sk kept after"));
    TEST_ASSERT_EQUAL(1, s_all.size());
}

TEST_CASE("log all-lines sink: a v2 line replaces a line left partway", "[idfxx][log]") {
    add_all_lines(s_all);
    esp_log_write(ESP_LOG_WARN, "sk_part", "%c (%d) %s:", 'W', 1, "sk_part");
    esp_log_write(ESP_LOG_WARN, "sk_part", "%s", "sk never ended");
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_WARN | ESP_LOG_CONFIG_REQUIRE_FORMATTING),
        "sk_v2r",
        "sk v2 whole %d",
        1
    );
    // The next esp_log_write call starts a new line.
    esp_log_write(ESP_LOG_WARN, "sk_part", "W (%d) %s: sk after\n", 2, "sk_part");

    TEST_ASSERT_FALSE(s_all.containing("sk never ended"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_v2r", "sk v2 whole 1"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_part", "sk after"));
}

TEST_CASE("log all-lines sink: an esp_log line replaces a line left partway", "[idfxx][log]") {
    add_all_lines(s_all);
    // An esp_log call (as ESP_LOGx makes under log v1) is a complete line. It never continues a
    // line that an esp_log_write call left unfinished, whether that line was being kept or dropped.
    esp_log_write(ESP_LOG_WARN, "sk_part", "%c (%d) %s:", 'W', 1, "sk_part");
    esp_log_write(ESP_LOG_WARN, "sk_part", "%s", "sk never ended");
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_WARN),
        "sk_whole",
        "W (%lu) %s: sk whole %d\n",
        2ul,
        "sk_whole",
        1
    );
    esp_log_write(ESP_LOG_INFO, "sk_part", "%c (%d) %s:", 'I', 3, "sk_part");
    esp_log_write(ESP_LOG_INFO, "sk_part", "%s", "sk dropped partway");
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_ERROR),
        "sk_whole",
        "E (%lu) %s: sk whole %d\n",
        4ul,
        "sk_whole",
        2
    );

    TEST_ASSERT_FALSE(s_all.containing("sk never ended"));
    TEST_ASSERT_FALSE(s_all.containing("sk dropped"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_whole", "sk whole 1"));
    TEST_ASSERT_TRUE(s_all.has(level::error, "sk_whole", "sk whole 2"));
}

TEST_CASE("log all-lines sink is given an esp_log call as a line, newline or not", "[idfxx][log]") {
    add_all_lines(s_all);
    // A direct call to esp_log whose format doesn't end in a newline still ends its line. The
    // esp_log_write call after it starts a new line.
    esp_log(ESP_LOG_CONFIG_INIT(ESP_LOG_WARN), "sk_open", "W (%d) %s: sk no newline", 1, "sk_open");
    esp_log_write(ESP_LOG_WARN, "sk_next", "W (%d) %s: sk next line\n", 2, "sk_next");
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_open", "sk no newline"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_next", "sk next line"));
    TEST_ASSERT_EQUAL(2, s_all.size());
}

TEST_CASE("log all-lines sink: an idfxx::log line leaves a line partway as it is", "[idfxx][log]") {
    add_all_lines(s_all);
    esp_log_write(ESP_LOG_WARN, "sk_part", "%c (%d) %s:", 'W', 1, "sk_part");
    esp_log_write(ESP_LOG_WARN, "sk_part", "%s", "sk begun ");
    // The sink receives the idfxx::log line complete and immediately, and the unfinished line
    // continues after it.
    lg::warn("sk_between", "sk in between {}", 1);
    esp_log_write(ESP_LOG_WARN, "sk_part", "%s", "and ended\n");
    const auto lines = s_all.lines();
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("sk in between 1", lines[0].message.c_str());
    TEST_ASSERT_EQUAL_STRING("sk_part", lines[1].tag.c_str());
    TEST_ASSERT_EQUAL_STRING("sk begun and ended", lines[1].message.c_str());
}

TEST_CASE("log all-lines sink is given a line without a tag", "[idfxx][log]") {
    add_all_lines(s_untagged);
    // For a line without a tag, the text up to and including "(null):" is removed, because a
    // missing tag is printed as "(null)" in its prefix. A line without "(null):" is kept in full.
    esp_log_write(ESP_LOG_WARN, nullptr, "W (%d) x: sk null write %d\n", 1, 2);
    esp_log_write(ESP_LOG_WARN, nullptr, "W (%d) %s: sk null prefix %d\n", 5, "(null)", 6);
    // With an empty tag, no prefix is found, so the sink receives the line with its prefix.
    esp_log_write(ESP_LOG_WARN, "", "W (%d) %s: sk empty tag %d\n", 7, "", 8);
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_WARN | ESP_LOG_CONFIG_REQUIRE_FORMATTING),
        nullptr,
        "sk null v2 %d",
        3
    );
    // Log v1 passes the tag to the console's "%s". ESP-IDF 5.5's newlib vprintf faults on a null
    // tag, and 6.x's picolibc prints "(null)".
#if ESP_LOG_VERSION == 2 || ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    ESP_LOGW(nullptr, "sk no tag %d", 4);
#endif
    TEST_ASSERT_TRUE(s_untagged.has(level::warn, "", "W (1) x: sk null write 2"));
    TEST_ASSERT_TRUE(s_untagged.has(level::warn, "", "sk null prefix 6"));
    TEST_ASSERT_TRUE(s_untagged.has(level::warn, "", "W (7) : sk empty tag 8"));
    TEST_ASSERT_TRUE(s_untagged.has(level::warn, "", "sk null v2 3"));
#if ESP_LOG_VERSION == 2 || ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    TEST_ASSERT_TRUE(s_untagged.has(level::warn, "", "sk no tag 4"));
#endif
}

TEST_CASE(
    "log all-lines sink: the least recent task's slot is taken when all are",
    "[idfxx][log]"
) {
    add_all_lines(s_all);
    // More tasks than slots, each leaving its line unfinished. The first tasks' slots are evicted.
    constexpr int tasks = CONFIG_IDFXX_LOG_JOIN_SLOTS + 2;
    held_task partial[tasks];
    for (int i = 0; i < tasks; ++i) {
        run_held(partial[i], leave_partial, i);
    }
    // One more task starts a line, and finishes it.
    held_task last;
    run_held(last, finish_line, 0);
    for (auto& h : partial) {
        end_held(h);
    }
    end_held(last);
    vTaskDelay(pdMS_TO_TICKS(20));

    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_last", "sk finished"));
    // The unfinished lines never ended.
    TEST_ASSERT_FALSE(s_all.containing("sk partial"));
}

TEST_CASE(
    "log all-lines sink: a task made at a deleted task's handle starts its own line",
    "[idfxx][log]"
) {
    add_all_lines(s_all);
    held_task old;
    run_held(old, leave_stale, 0);
    const TaskHandle_t old_handle = old.handle;
    // The task is deleted from here while it waits, so that its memory is freed immediately.
    vTaskDelete(old_handle);
    // Creates new tasks until one is created at the deleted task's handle. Each task is kept until
    // the end, so that each new task is created somewhere else.
    constexpr int attempts = 16;
    waiting_task made[attempts];
    waiting_task* reused = nullptr;
    int count = 0;
    while (count < attempts && reused == nullptr) {
        waiting_task& w = made[count++];
        w.waiter = xTaskGetCurrentTaskHandle();
        TEST_ASSERT_EQUAL(
            pdPASS,
            xTaskCreate(
                waiting_task_main, "sk_held", 4096, &w, uxTaskPriorityGet(nullptr), &w.handle
            )
        );
        if (w.handle == old_handle) {
            reused = &w;
        }
    }
    bool logged = false;
    if (reused != nullptr) {
        xTaskNotifyGive(reused->handle);
        logged = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) == 1;
    }
    for (int i = 0; i < count; ++i) {
        vTaskDelete(made[i].handle);
    }
    if (reused == nullptr) {
        TEST_IGNORE_MESSAGE("no new task was made at the deleted task's handle");
    }
    TEST_ASSERT_TRUE(logged);
    TEST_ASSERT_EQUAL(1, s_all.count(level::warn, "sk_new", "sk new task's line"));
    TEST_ASSERT_FALSE(s_all.containing("sk stale"));
    TEST_ASSERT_EQUAL(1, s_all.size());
}

TEST_CASE(
    "log all-lines sink: a line partway isn't continued once the last sink is removed",
    "[idfxx][log]"
) {
    add_all_lines(s_all);
    esp_log_write(ESP_LOG_WARN, "sk_rel", "%c (%d) %s:", 'W', 1, "sk_rel");
    esp_log_write(ESP_LOG_WARN, "sk_rel", "%s", "sk left partway ");
    lg::remove_sink(s_all);
    TEST_ASSERT_TRUE(lg::try_add_sink(lg::all_lines, s_all).has_value());
    esp_log_write(ESP_LOG_WARN, "sk_rel", "%s", "sk rest\n");
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_rel", "sk rest"));
    TEST_ASSERT_FALSE(s_all.containing("sk left partway"));
}

// =============================================================================
// Shortening and line ends
// =============================================================================

TEST_CASE("log all-lines sink cuts a line at the longest given", "[idfxx][log]") {
    add_all_lines(s_all);
    constexpr size_t most = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
    const std::string body(2 * most, 'x');
    esp_log_write(ESP_LOG_WARN, "sk_long", "W (%d) %s: %s\n", 1, "sk_long", body.c_str());
    // The line is shortened to the maximum length as formatted, before its prefix,
    // "W (1) sk_long: " (15 bytes), is removed.
    const std::string message = s_all.last_message("sk_long");
    TEST_ASSERT_EQUAL(most - 15, message.size());
    TEST_ASSERT_TRUE(message == std::string(most - 15, 'x'));
}

TEST_CASE("log all-lines sink cuts a line at the start of a UTF-8 character", "[idfxx][log]") {
    add_all_lines(s_all);
    constexpr size_t most = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
    // Each lead length puts the limit at a different byte of a character.
    for (size_t lead = 0; lead < 3; ++lead) {
        const std::string body = three_byte_text(lead, most);
        // The line is shortened as formatted, including its prefix "W (1) sk_utfa: " (15 bytes),
        // and then the prefix is removed.
        esp_log_write(ESP_LOG_WARN, "sk_utfa", "W (%d) %s: %s\n", 1, "sk_utfa", body.c_str());
        const size_t formatted = lead + (most - 15 - lead) / 3 * 3;
        TEST_ASSERT_EQUAL_STRING(
            body.substr(0, formatted).c_str(), s_all.last_message("sk_utfa").c_str()
        );
        // Without a prefix, the message can use the full maximum length.
        esp_log_write(ESP_LOG_WARN, "sk_utfb", "%s\n", body.c_str());
        const size_t whole = lead + (most - lead) / 3 * 3;
        TEST_ASSERT_EQUAL_STRING(
            body.substr(0, whole).c_str(), s_all.last_message("sk_utfb").c_str()
        );
    }
}

TEST_CASE("log all-lines sink is given a message's own newlines", "[idfxx][log]") {
    add_all_lines(s_all);
    ESP_LOGW("sk_nl1", "sk first\nsk second");
    // A leading newline leaves the first line empty.
    ESP_LOGW("sk_nl2", "\nsk after leading");
    // Only the newline that ends the line is dropped.
    ESP_LOGW("sk_nl3", "sk double\n\nsk end\n");
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_nl1", "sk first\nsk second"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_nl2", "\nsk after leading"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_nl3", "sk double\n\nsk end\n"));
}

// =============================================================================
// The console and the lines that it skips
// =============================================================================

TEST_CASE("log all-lines sink takes lines whatever vprintf is installed", "[idfxx][log]") {
    add_all_lines(s_all);
    // A vprintf that prints nothing. The console's output goes only to this vprintf, and the sinks
    // are not part of the vprintf chain.
    install_silent_vprintf();
    ESP_LOGW("sk_chain", "under a silent vprintf");
    // Log v1 prints a line in one call to the vprintf. Log v2 prints it in three calls (prefix,
    // message, end).
    constexpr int console_calls = ESP_LOG_VERSION == 2 ? 3 : 1;
    TEST_ASSERT_EQUAL(console_calls, s_other_calls);
    TEST_ASSERT_EQUAL(1, s_all.count(level::warn, "sk_chain", "under a silent vprintf"));
    // After the sink is removed, the wrapper passes every call on, and the sink receives none.
    lg::remove_sink(s_all);
    s_other_calls = 0;
    ESP_LOGW("sk_chain", "while removed");
    TEST_ASSERT_EQUAL(console_calls, s_other_calls);
    TEST_ASSERT_FALSE(s_all.containing("while removed"));
    restore_vprintf();
}

TEST_CASE("log all-lines sink takes a line below the console's runtime level", "[idfxx][log]") {
    add_all_lines(s_all);
    install_silent_vprintf();
    sink_test::set_console_level("sk_rt", ESP_LOG_ERROR);
    ESP_LOGW("sk_rt", "sk below the console %d", 1);
    // A Wi-Fi library's three calls, more verbose than the console's level for its tag.
    esp_log_write(ESP_LOG_WARN, "sk_rt", "%c (%d) %s:", 'W', 200, "sk_rt");
    esp_log_write(ESP_LOG_WARN, "sk_rt", "%s", "sk blob below the console");
    esp_log_write(ESP_LOG_WARN, "sk_rt", "%s", "\n");
    // idfxx::log's lines, formatted or passed as a string, are handled the same way as ESP_LOGx
    // lines.
    lg::warn("sk_rt", "sk idfxx formatted {}", 2);
    lg::warn("sk_rt", std::string_view{"sk idfxx string"});
    // The console printed none of them.
    const int printed = s_other_calls;
    restore_vprintf();
    TEST_ASSERT_EQUAL(0, printed);
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_rt", "sk below the console 1"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_rt", "sk blob below the console"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_rt", "sk idfxx formatted 2"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_rt", "sk idfxx string"));
}

TEST_CASE("log all-lines sink has idfxx::log format only the lines it wants", "[idfxx][log]") {
    add_all_lines(s_all);
    sink_test::set_console_level("sk_rt", ESP_LOG_ERROR);
    sink_test::s_formatted = 0;
    // More verbose than the console's level but at the sink's level, so the line is formatted and
    // the sink receives it.
    lg::warn("sk_rt", "sk idfxx kept {}", sink_test::counted{});
    TEST_ASSERT_EQUAL(1, sink_test::s_formatted);
    // More verbose than both levels, so the line is never formatted.
    lg::info("sk_rt", "sk idfxx unwanted {}", sink_test::counted{});
    TEST_ASSERT_EQUAL(1, sink_test::s_formatted);
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_rt", "sk idfxx kept counted"));
    TEST_ASSERT_FALSE(s_all.containing("sk idfxx unwanted"));
    // After the sink is removed, no line that the console skips is formatted.
    lg::remove_sink(s_all);
    lg::warn("sk_rt", "sk idfxx removed {}", sink_test::counted{});
    TEST_ASSERT_EQUAL(1, sink_test::s_formatted);
}

TEST_CASE("log all-lines sink takes buffer dumps, not DRAM or early lines", "[idfxx][log]") {
    add_all_lines(s_all);
    const uint8_t bytes[] = {0x01, 0x02, 0xab, 0xcd};
    ESP_LOG_BUFFER_HEX_LEVEL("sk_buf", bytes, sizeof bytes, ESP_LOG_WARN);
    lg::buffer_hex(level::warn, "sk_buf2", bytes);
    ESP_DRAM_LOGW("sk_dram", "sk dram line %d", 4);
    ESP_EARLY_LOGW("sk_early", "sk early line %d", 5);
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_buf", "01 02 ab cd"));
    TEST_ASSERT_TRUE(s_all.has(level::warn, "sk_buf2", "01 02 ab cd"));
    // Log v1 prints DRAM and early lines directly to the ROM console. Log v2 marks them as logged
    // where the flash cache may be disabled. The sink receives neither kind of line.
    TEST_ASSERT_FALSE(s_all.containing("sk dram line"));
    TEST_ASSERT_FALSE(s_all.containing("sk early line"));
}

TEST_CASE("log all-lines sinks' stack cost stays within its documented bound", "[idfxx][log]") {
    constexpr size_t max_line = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
    // A line nearly as long as the longest line that a sink receives, leaving room for its prefix
    // with a long timestamp.
    const std::string message(max_line - 32, 'x');
    sink_test::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(remove_sizing);
    s_sizing.last = 0;
    TEST_ASSERT_TRUE(lg::try_add_sink(lg::all_lines, s_sizing).has_value());
    const size_t taking = stack_to_log(message.c_str(), false);
    TEST_ASSERT_EQUAL(message.size(), s_sizing.last.load());
    lg::remove_sink(s_sizing);
    // With no sink added with all_lines, the wrapper passes calls on.
    const size_t passing = stack_to_log(message.c_str(), false);
    // Calling the real esp_log, bypassing the wrapper.
    const size_t without = stack_to_log(message.c_str(), true);
    printf(
        "log sinks' stack to log a %u-byte line: %u without the wrapper, %u with no sink, "
        "%u with a sink of every line\n",
        static_cast<unsigned>(message.size()),
        static_cast<unsigned>(without),
        static_cast<unsigned>(passing),
        static_cast<unsigned>(taking)
    );
    // Measured on esp32s3 in QEMU with ESP-IDF 6.1.1: 64 bytes more with no sink (the wrapper's
    // own frame), and max_line bytes more with a sink, for a max_line of 64, 256 and 1024. The
    // documented costs are about 64 and max_line + 64 bytes.
    TEST_ASSERT_LESS_OR_EQUAL(without + 128, passing);
    TEST_ASSERT_LESS_OR_EQUAL(without + max_line + 64, taking);
}

#endif // CONFIG_IDFXX_LOG_ALL_LINES
