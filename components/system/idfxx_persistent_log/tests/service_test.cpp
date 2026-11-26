// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Tests for idfxx_persistent_log's service. The first tests cover capture's level filter on its
// own. The rest start the log on the test app's "log" partition, where it captures log lines.

#include "sdkconfig.h"

#ifdef CONFIG_IDFXX_STD_FORMAT
#include "../src/capture.hpp"
#endif
#include "../src/flash_log.hpp"
#include "../src/log_line.hpp"
#include "../src/rtc.hpp"
#include "../src/sector_store.hpp"
#include "../src/service.hpp"
#include "entry_helpers.hpp"
#include "idfxx/persistent_log"
#include "qemu_log_cache.hpp"
#include "sdkconfig.h"
#include "test_cleanup.hpp"
#include "unity.h"

#ifdef CONFIG_IDFXX_STD_FORMAT
#include <idfxx/log>
#endif
#include <idfxx/partition>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <format>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <string>
#include <string_view>
#include <sys/time.h>
#include <system_error>
#include <vector>

#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
// The esp_log that the wrapper passes calls on to.
extern "C" void __real_esp_log(esp_log_config_t config, const char* tag, const char* format, ...);
#endif

namespace pl = idfxx::persistent_log;
namespace internal = idfxx::persistent_log::internal;
#ifdef CONFIG_IDFXX_STD_FORMAT
using idfxx::log::level;
#endif

namespace {

// =============================================================================
// Helpers
// =============================================================================

template<typename T>
bool fails_with(const idfxx::result<T>& r, idfxx::errc e) {
    return !r.has_value() && r.error() == e;
}

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

size_t occurrences(std::string_view hay, std::string_view needle) {
    size_t n = 0;
    for (size_t at = 0; (at = hay.find(needle, at)) != std::string_view::npos; ++at) {
        ++n;
    }
    return n;
}

// The complete line of `text` that holds `marker`, without its newline. Empty if no line holds
// `marker`.
std::string line_with(std::string_view text, std::string_view marker) {
    const size_t at = text.rfind(marker);
    if (at == std::string_view::npos) {
        return {};
    }
    const size_t begin = text.rfind('\n', at);
    const size_t from = begin == std::string_view::npos ? 0 : begin + 1;
    const size_t end = text.find('\n', at);
    return std::string(
        text.substr(from, end == std::string_view::npos ? std::string_view::npos : end - from)
    );
}

std::string ring_text() {
    std::string s(4096, '\0');
    s.resize(pl::copy_newest_in_memory(s).size());
    return s;
}

// The newest entries in flash, read without a flush, so that a test can see what is in flash.
std::string flash_newest() {
    std::string s(8192, '\0');
    auto n = internal::copy_newest_in_flash(s);
    TEST_ASSERT_TRUE(n.has_value());
    s.resize(*n);
    return s;
}

std::string read_all(pl::text_reader& r) {
    std::string all;
    std::array<char, 100> chunk;
    while (true) {
        auto n = r.try_next(chunk);
        TEST_ASSERT_TRUE(n.has_value());
        if (n->empty()) {
            return all;
        }
        all.append(*n);
    }
}

std::string read_all() {
    auto r = pl::try_read_text();
    TEST_ASSERT_TRUE(r.has_value());
    return read_all(*r);
}

bool wait_for_flash(std::string_view marker, int ms) {
    for (int waited = 0; waited < ms; waited += 10) {
        if (contains(flash_newest(), marker)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return contains(flash_newest(), marker);
}

void stop_log() {
    (void)pl::try_stop();
}

// A configuration with no writer, boot 7, no wall clock, and the default capture.
pl::config test_config() {
    pl::config c;
    c.boot_count = 7;
    c.wall_clock = [] { return std::optional<std::chrono::system_clock::time_point>{}; };
    c.writer = std::nullopt;
    return c;
}

// Starts the log, and empties it so that a test sees only its own lines.
void start_clean(pl::config c = test_config()) {
    pl::testing::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(stop_log);
    auto started = pl::try_start(std::move(c));
    TEST_ASSERT_TRUE_MESSAGE(started.has_value(), "try_start failed");
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
}

void stop_checked() {
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

// The ring index `by` bytes past `index`.
uint32_t ring_index(uint32_t index, size_t by) {
    return static_cast<uint32_t>((uint64_t{index} + by) % internal::ring_type::modulus);
}

// The bytes appended to the ring since its head was `before`.
size_t appended_since(uint32_t before) {
    const auto& ring = internal::rtc_ring();
    return static_cast<size_t>(
        (uint64_t{ring.head} + internal::ring_type::modulus - before) % internal::ring_type::modulus
    );
}

// The text appended to the ring since its head was `before`.
std::string text_added_since(uint32_t before) {
    const size_t added = appended_since(before);
    const std::string all = ring_text();
    TEST_ASSERT_TRUE(added <= all.size());
    return all.substr(all.size() - added);
}

// `lead` ASCII bytes, followed by 3-byte UTF-8 characters until the text is longer than `size`
// bytes.
std::string three_byte_text(size_t lead, size_t size) {
    std::string text(lead, 'a');
    while (text.size() <= size) {
        text += "\xE2\x82\xAC";
    }
    return text;
}

// The test configuration with a writer.
pl::config writer_config_of(pl::writer_config w) {
    auto c = test_config();
    c.writer = w;
    return c;
}

// The test configuration without capture, so that the ring holds only the test's own lines.
pl::config quiet_config() {
    auto c = test_config();
#ifdef CONFIG_IDFXX_STD_FORMAT
    c.capture = std::nullopt;
#endif
    return c;
}

// A fixed wall clock.
std::optional<std::chrono::system_clock::time_point> fixed_clock() {
    using namespace std::chrono;
    return sys_days{year{2026} / 1 / 2} + hours{3} + minutes{4} + seconds{5};
}

#ifdef CONFIG_IDFXX_STD_FORMAT
// A vprintf installed in place of the console's, which counts its calls and prints nothing.
vprintf_like_t s_other_previous = nullptr;
bool s_other_installed = false;
int s_other_calls = 0;

int silent_vprintf(const char*, va_list) {
    ++s_other_calls;
    return 0;
}

// Tag levels that a test set, which are restored after the test.
bool s_levels_changed = false;

void restore_levels() {
    if (s_levels_changed) {
        esp_log_level_set("pl_rt", ESP_LOG_INFO);
        s_levels_changed = false;
    }
}
#endif

// The system time, which is restored after a test that sets it.
timeval s_saved_time{};
bool s_time_changed = false;

void restore_time() {
    if (s_time_changed) {
        settimeofday(&s_saved_time, nullptr);
        s_time_changed = false;
    }
}

#ifdef CONFIG_IDFXX_STD_FORMAT
void restore_vprintf() {
    if (s_other_installed) {
        esp_log_set_vprintf(s_other_previous);
        s_other_installed = false;
    }
}

// A sink that wants no lines, for filling idfxx::log's sink slots.
class idle_sink final : public idfxx::log::sink {
    bool do_wants(level, std::string_view) const noexcept override { return false; }
    void do_write(level, std::string_view, std::string_view) noexcept override {}
};

idle_sink s_idle[CONFIG_IDFXX_LOG_MAX_SINKS];

void remove_idle_sinks() {
    for (auto& s : s_idle) {
        idfxx::log::remove_sink(s);
    }
}

// Adds idle sinks until idfxx::log holds as many as it can, and returns how many were added.
size_t fill_sinks() {
    idfxx::test::run_after_each_test(remove_idle_sinks);
    size_t added = 0;
    for (auto& s : s_idle) {
        if (idfxx::log::try_add_sink(s).has_value()) {
            ++added;
        }
    }
    return added;
}

// A sink that wants only one tag's lines and keeps only their size, so that it uses little stack
// itself. With this sink, the stack measured is what idfxx::log uses for a sink, without what
// persistent_log's sink uses.
class sizing_sink final : public idfxx::log::sink {
public:
    std::atomic<size_t> last{0};

private:
    bool do_wants(level, std::string_view tag) const noexcept override {
        return tag == "pl_stack";
    }
    void do_write(level, std::string_view, std::string_view message) noexcept override {
        last = message.size();
    }
};

sizing_sink s_sizing;

void remove_sizing() {
    idfxx::log::remove_sink(s_sizing);
}
#endif

#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
// A store that passes each call on to the test app's "log" partition, and logs a warning each time
// it is written.
class logging_store final : public pl::sector_store {
public:
    explicit logging_store(pl::partition_sector_store whole)
        : sector_store(whole.size(), whole.sector_size(), whole.write_size())
        , _whole(std::move(whole)) {}

private:
    idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
        return _whole.try_read(offset, dst);
    }
    idfxx::result<void> do_read_raw(size_t offset, idfxx::byte_span dst) const override {
        return _whole.try_read_raw(offset, dst);
    }
    idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
        ESP_LOGW("pl_store", "pl store wrote %u bytes", static_cast<unsigned>(src.size()));
        return _whole.try_write(offset, src);
    }
    idfxx::result<void> do_erase_sector(size_t index) override {
        return _whole.try_erase_sector(index);
    }

    pl::partition_sector_store _whole;
};
#endif

#ifdef CONFIG_IDFXX_STD_FORMAT
// How a stack probe logs its warning.
enum class logged_with {
    // ESP_LOGW. With the wrapper, this calls the wrapper.
    esp_logw,
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
    // What ESP_LOGW expands to, but calling esp_log itself rather than the wrapper.
    real_esp_log,
#endif
    // idfxx::log::warn with the message as a string, so that no formatting adds to the stack
    // measured.
    idfxx_log,
};

// A task that logs one long warning and records its stack's high-water mark.
struct stack_probe {
    TaskHandle_t waiter = nullptr;
    const char* message = nullptr;
    logged_with how = logged_with::esp_logw;
    size_t free_bytes = 0;
};

void stack_probe_main(void* p) {
    auto* probe = static_cast<stack_probe*>(p);
    switch (probe->how) {
    case logged_with::esp_logw:
        ESP_LOGW("pl_stack", "%s", probe->message);
        break;
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
    case logged_with::real_esp_log:
#if ESP_LOG_VERSION == 2
        __real_esp_log(
            ESP_LOG_CONFIG_INIT(ESP_LOG_WARN | ESP_LOG_CONFIGS_DEFAULT),
            "pl_stack",
            "%s",
            probe->message
        );
#else
        __real_esp_log(
            ESP_LOG_CONFIG_INIT(ESP_LOG_WARN),
            "pl_stack",
            LOG_FORMAT(W, "%s"),
            esp_log_timestamp(),
            "pl_stack",
            probe->message
        );
#endif
        break;
#endif
    case logged_with::idfxx_log:
        idfxx::log::warn("pl_stack", std::string_view{probe->message});
        break;
    }
    probe->free_bytes = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
    xTaskNotifyGive(probe->waiter);
    vTaskSuspend(nullptr);
}

// Logs one long warning on each of a few new tasks, and returns the least stack that any of them
// used.
size_t stack_to_log(const char* message, logged_with how) {
    constexpr uint32_t stack_size = 4096;
    size_t most_free = 0;
    for (int i = 0; i < 4; ++i) {
        stack_probe probe{.waiter = xTaskGetCurrentTaskHandle(), .message = message, .how = how};
        TaskHandle_t task = nullptr;
        TEST_ASSERT_EQUAL(
            pdPASS,
            xTaskCreate(
                stack_probe_main, "pl_stack", stack_size, &probe, uxTaskPriorityGet(nullptr), &task
            )
        );
        TEST_ASSERT_EQUAL(1, ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)));
        vTaskDelete(task);
        most_free = std::max(most_free, probe.free_bytes);
    }
    return stack_size - most_free;
}

// A value that counts each time it is formatted, to show whether idfxx::log formatted a line.
int s_formatted = 0;
struct counted {};
#endif

} // namespace

#ifdef CONFIG_IDFXX_STD_FORMAT
template<>
struct std::formatter<counted> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const counted&, FormatContext& ctx) const {
        ++s_formatted;
        return std::format_to(ctx.out(), "counted");
    }
};
#endif

// =============================================================================
// Filter
// =============================================================================

#ifdef CONFIG_IDFXX_STD_FORMAT
TEST_CASE("persistent_log level_filter applies tag levels over the level", "[persistent_log]") {
    internal::level_filter f(
        level::warn, {{"loud", level::debug}, {"quiet", level::error}, {"quiet", level::none}}
    );
    TEST_ASSERT_TRUE(f.keeps(level::error, "any"));
    TEST_ASSERT_TRUE(f.keeps(level::warn, "any"));
    TEST_ASSERT_FALSE(f.keeps(level::info, "any"));
    TEST_ASSERT_TRUE(f.keeps(level::debug, "loud"));
    TEST_ASSERT_FALSE(f.keeps(level::verbose, "loud"));
    // The last entry for a tag takes effect, so "quiet" keeps nothing.
    TEST_ASSERT_FALSE(f.keeps(level::error, "quiet"));
    // A prefix of a tag is not the tag.
    TEST_ASSERT_FALSE(f.keeps(level::debug, "lou"));
    TEST_ASSERT_FALSE(f.keeps(level::none, "loud"));

    internal::level_filter off;
    TEST_ASSERT_FALSE(off.keeps(level::error, "any"));
}
#endif

// =============================================================================
// The service
// =============================================================================

template<typename C>
concept has_capture = requires(C c) { c.capture; };

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(has_capture<pl::config>);
#else
// Without capture, the configuration has no member that selects capture.
static_assert(!has_capture<pl::config>);

TEST_CASE("persistent_log without capture keeps only the appended lines", "[persistent_log]") {
    start_clean();
    ESP_LOGW("pl_off", "pl not captured %d", 1);
    esp_log_write(ESP_LOG_WARN, "pl_off", "W (%d) %s: pl not captured %d\n", 1, "pl_off", 2);
    pl::append("pl written without capture");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    const std::string all = read_all();
    TEST_ASSERT_TRUE(contains(all, " - pl written without capture\n"));
    TEST_ASSERT_FALSE(contains(all, "pl not captured"));
    stop_checked();
}
#endif

TEST_CASE("persistent_log is idle before start", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    (void)pl::try_stop();
    std::array<char, 64> buf;
    TEST_ASSERT_TRUE(fails_with(pl::try_stop(), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_flush(), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_flush_if_pending(1), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_read_text(), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_read(), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_read_newest(1), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_copy_newest(buf), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_clear(), idfxx::errc::invalid_state));
    pl::append("pl dropped before start");
    TEST_ASSERT_FALSE(contains(ring_text(), "pl dropped before start"));
}

TEST_CASE("persistent_log start rejects a bad configuration", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    auto c = test_config();
    c.storage = std::string("pl-nope");
    TEST_ASSERT_TRUE(fails_with(pl::try_start(std::move(c)), idfxx::errc::not_found));

    c = test_config();
    c.writer = pl::writer_config{.interval = std::chrono::milliseconds{0}};
    TEST_ASSERT_TRUE(fails_with(pl::try_start(std::move(c)), idfxx::errc::invalid_arg));

    c = test_config();
    c.faults = pl::fault_config{.slot_count = 0};
    TEST_ASSERT_TRUE(fails_with(pl::try_start(std::move(c)), idfxx::errc::invalid_arg));

    // The partition is 64 KiB, so slots that take all of it leave the log no room.
    c = test_config();
    c.faults = pl::fault_config{.slot_size = 16 * 1024, .slot_count = 4};
    TEST_ASSERT_TRUE(fails_with(pl::try_start(std::move(c)), idfxx::errc::invalid_arg));

    // Nothing was started.
    TEST_ASSERT_TRUE(fails_with(pl::try_stop(), idfxx::errc::invalid_state));
}

TEST_CASE("persistent_log a second start fails; stop and start cycle", "[persistent_log]") {
    start_clean();
    TEST_ASSERT_TRUE(fails_with(pl::try_start(test_config()), idfxx::errc::invalid_state));
    stop_checked();
    TEST_ASSERT_TRUE(fails_with(pl::try_stop(), idfxx::errc::invalid_state));

    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(pl::try_start(test_config()).has_value());
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
        ESP_LOGW("pl_cycle", "captured in cycle %d", i);
#endif
        pl::append("pl written in cycle " + std::to_string(i));
        stop_checked();
    }

    TEST_ASSERT_TRUE(pl::try_start(test_config()).has_value());
    const std::string all = read_all();
    for (int i = 0; i < 3; ++i) {
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
        // Each line appears exactly once, however many times the log was started.
        TEST_ASSERT_EQUAL(
            1, occurrences(all, "W pl_cycle: captured in cycle " + std::to_string(i) + "\n")
        );
#endif
        TEST_ASSERT_EQUAL(1, occurrences(all, "pl written in cycle " + std::to_string(i) + "\n"));
    }
    stop_checked();
}

#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
TEST_CASE("persistent_log captures warnings and errors, not info", "[persistent_log]") {
    start_clean();
    ESP_LOGW("pl_cap", "warned %d", 42);
    ESP_LOGE("pl_cap", "failed: %s", "badly");
    ESP_LOGI("pl_cap", "informed");
    const std::string ring = ring_text();
    const std::string w = line_with(ring, "W pl_cap: warned 42");
    const std::string e = line_with(ring, "E pl_cap: failed: badly");
    TEST_ASSERT_FALSE(w.empty());
    TEST_ASSERT_FALSE(e.empty());
    TEST_ASSERT_FALSE(contains(ring, "informed"));
    // The line is stamped "<boot>.<wake> +<ms> <wall> ", with the wall clock unset (before 2020).
    TEST_ASSERT_TRUE(w.starts_with("7."));
    TEST_ASSERT_TRUE(w.ends_with(" - W pl_cap: warned 42"));
    TEST_ASSERT_TRUE(contains(w, " +"));
    // ESP-IDF's own timestamp and colors are gone.
    TEST_ASSERT_FALSE(contains(w, "("));
    TEST_ASSERT_FALSE(contains(w, "\x1b"));
    stop_checked();
}

TEST_CASE("persistent_log capture takes tag levels", "[persistent_log]") {
    auto c = test_config();
    c.capture = pl::capture_config{
        .level = level::warn,
        .tag_levels = {{"pl_loud", level::info}, {"pl_quiet", level::error}},
    };
    start_clean(std::move(c));
    ESP_LOGI("pl_loud", "loud info");
    ESP_LOGW("pl_quiet", "quiet warning");
    ESP_LOGE("pl_quiet", "quiet error");
    ESP_LOGI("pl_other", "other info");
    ESP_LOGW("pl_other", "other warning");
    const std::string ring = ring_text();
    TEST_ASSERT_TRUE(contains(ring, "I pl_loud: loud info\n"));
    TEST_ASSERT_FALSE(contains(ring, "quiet warning"));
    TEST_ASSERT_TRUE(contains(ring, "E pl_quiet: quiet error\n"));
    TEST_ASSERT_FALSE(contains(ring, "other info"));
    TEST_ASSERT_TRUE(contains(ring, "W pl_other: other warning\n"));
    stop_checked();
}
#endif

#ifdef CONFIG_IDFXX_STD_FORMAT
TEST_CASE("persistent_log captures idfxx::log lines", "[persistent_log]") {
    start_clean();
    idfxx::log::warn("pl_fmt", "formatted {} and {}", 42, "more");
    idfxx::log::info("pl_fmt", "not kept {}", 1);
    const std::string ring = ring_text();
    TEST_ASSERT_TRUE(contains(ring, "W pl_fmt: formatted 42 and more\n"));
    TEST_ASSERT_FALSE(contains(ring, "not kept"));
    stop_checked();
}

TEST_CASE("persistent_log keeps an idfxx::log line complete", "[persistent_log]") {
    constexpr size_t most = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
    constexpr std::string_view head = "W pl_whole: ";
    // The longest message that an entry holds complete after its head. idfxx::log passes its own
    // lines complete, whatever the limit for ESP-IDF's lines is (CONFIG_IDFXX_LOG_SINK_MAX_LINE,
    // with the wrapper), so only the entry's own limit applies.
    const std::string body(most - head.size(), 'y');
    start_clean();
    auto& ring = internal::rtc_ring();
    uint32_t before = ring.head;
    idfxx::log::warn("pl_whole", "{}", body);
    std::string entry = text_added_since(before);
    TEST_ASSERT_TRUE(entry.starts_with("7."));
    TEST_ASSERT_TRUE(entry.ends_with(" - W pl_whole: " + body + "\n"));
    TEST_ASSERT_EQUAL(1, occurrences(entry, "\n"));
    // A logger's error, which is above the threshold, and the macros' warning, which is at it, are
    // kept the same way. A logger's line below the threshold is not kept.
    const idfxx::log::logger logger{"pl_whole"};
    before = ring.head;
    logger.error("pl logger {}", 1);
    IDFXX_LOGW("pl_whole", "pl macro {}", 2);
    logger.info("pl logger info");
    entry = text_added_since(before);
    TEST_ASSERT_TRUE(contains(entry, " - E pl_whole: pl logger 1\n7."));
    TEST_ASSERT_TRUE(entry.ends_with(" - W pl_whole: pl macro 2\n"));
    TEST_ASSERT_FALSE(contains(entry, "pl logger info"));
    stop_checked();
}
#endif

#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
TEST_CASE("persistent_log keeps a line given in several calls as one entry", "[persistent_log]") {
    start_clean();
    // A line as a Wi-Fi library logs it, in three calls: prefix, message and newline. The sink
    // receives it from idfxx::log as one complete line.
    esp_log_write(ESP_LOG_WARN, "pl_wifi", "%c (%d) %s:", 'W', 123, "pl_wifi");
    esp_log_write(ESP_LOG_WARN, "pl_wifi", "%s", "pl beacon: timeout");
    esp_log_write(ESP_LOG_WARN, "pl_wifi", "%s", "\n");
    const std::string ring = ring_text();
    TEST_ASSERT_EQUAL(1, occurrences(ring, "pl beacon"));
    TEST_ASSERT_TRUE(contains(ring, " - W pl_wifi: pl beacon: timeout\n"));
    stop_checked();
}

TEST_CASE(
    "persistent_log keeps a line without a tag as its level and message",
    "[persistent_log]"
) {
    start_clean();
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_WARN | ESP_LOG_CONFIG_REQUIRE_FORMATTING),
        nullptr,
        "pl null v2 %d",
        3
    );
    // Log v1 passes the tag to the console's "%s". ESP-IDF 5.5's newlib vprintf faults on a null
    // tag, and 6.x's picolibc prints "(null)".
#if ESP_LOG_VERSION == 2 || ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    ESP_LOGW(nullptr, "pl no tag %d", 4);
#endif
    const std::string ring = ring_text();
    TEST_ASSERT_TRUE(contains(ring, " - W pl null v2 3\n"));
#if ESP_LOG_VERSION == 2 || ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    TEST_ASSERT_TRUE(contains(ring, " - W pl no tag 4\n"));
#endif
    stop_checked();
}

namespace {

// Returns the message that follows `marker` in the newest line of the ring that holds `marker`.
std::string message_after(std::string_view marker) {
    const std::string stored = line_with(ring_text(), marker);
    const size_t at = stored.find(marker);
    TEST_ASSERT_TRUE(at != std::string::npos);
    return stored.substr(at + marker.size());
}

} // namespace

TEST_CASE("persistent_log cuts a captured entry at the longest kept", "[persistent_log]") {
    start_clean();
    constexpr size_t most = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
    constexpr size_t given = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
    constexpr std::string_view head = "W pl_long: ";
    const std::string body(2 * std::max(most, given), 'x');
    // idfxx::log shortens the formatted line to `given` bytes, including its 15-byte prefix
    // "W (1) pl_long: ", and then removes the prefix before the sink receives the line. The entry,
    // which is the 11-byte head "W pl_long: " followed by the message, is then shortened to `most`
    // bytes. So the message kept is the shorter of `given` - 15 and `most` - 11.
    esp_log_write(ESP_LOG_WARN, "pl_long", "W (%d) %s: %s\n", 1, "pl_long", body.c_str());
    std::string message = message_after(head);
    TEST_ASSERT_EQUAL(std::min(given - 15, most - head.size()), message.size());
    TEST_ASSERT_TRUE(message == std::string(message.size(), 'x'));
    // A v2 line has no prefix. The sink receives `given` bytes of it, and then the entry's own
    // limit applies, which keeps the shorter of `given` and `most` - 11.
    esp_log(
        ESP_LOG_CONFIG_INIT(ESP_LOG_WARN | ESP_LOG_CONFIG_REQUIRE_FORMATTING),
        "pl_long",
        "%s",
        body.c_str()
    );
    message = message_after(head);
    TEST_ASSERT_EQUAL(std::min(given, most - head.size()), message.size());
    TEST_ASSERT_TRUE(message == std::string(message.size(), 'x'));
    // The sink receives idfxx::log's own line complete, so only the entry's limit applies, which
    // keeps `most` - 11 bytes.
    idfxx::log::warn("pl_long", "{}", body);
    message = message_after(head);
    TEST_ASSERT_EQUAL(most - head.size(), message.size());
    TEST_ASSERT_TRUE(message == std::string(message.size(), 'x'));
    stop_checked();
}

TEST_CASE(
    "persistent_log cuts a captured entry at the start of a UTF-8 character",
    "[persistent_log]"
) {
    start_clean();
    constexpr size_t most = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
    constexpr size_t given = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
    // For a line of `lead` ASCII bytes followed by 3-byte characters, the size of its first `size`
    // bytes once the cut is moved back to the start of a character.
    const auto whole_characters = [](size_t lead, size_t size) {
        return lead + (size - lead) / 3 * 3;
    };
    // Each lead puts the cuts at a different byte of a character.
    for (size_t lead = 0; lead < 3; ++lead) {
        const std::string body = three_byte_text(lead, std::max(most, given));
        // idfxx::log shortens the formatted line to `given` bytes, including its 15-byte prefix
        // "W (1) pl_utfa: ", and then removes the prefix. The entry is shortened again if the
        // 11-byte head "W pl_utfa: " and the message together are longer than `most`.
        esp_log_write(ESP_LOG_WARN, "pl_utfa", "W (%d) %s: %s\n", 1, "pl_utfa", body.c_str());
        const size_t given_a = whole_characters(lead, given - 15);
        const size_t kept_a = given_a + 11 <= most ? given_a : whole_characters(lead, most - 11);
        TEST_ASSERT_EQUAL_STRING(
            body.substr(0, kept_a).c_str(), message_after("W pl_utfa: ").c_str()
        );
        // Without a prefix, the sink receives all `given` bytes, and the entry is shortened to
        // make room for the 11-byte head "W pl_utfb: ".
        esp_log_write(ESP_LOG_WARN, "pl_utfb", "%s\n", body.c_str());
        const size_t given_b = whole_characters(lead, given);
        const size_t kept_b = given_b + 11 <= most ? given_b : whole_characters(lead, most - 11);
        TEST_ASSERT_EQUAL_STRING(
            body.substr(0, kept_b).c_str(), message_after("W pl_utfb: ").c_str()
        );
    }
    stop_checked();
}

TEST_CASE("persistent_log doesn't capture what it logs itself", "[persistent_log]") {
    // The log asks the wall clock for each line's stamp, and this wall clock logs a line. The
    // clock's line isn't captured, so it doesn't cause another capture that calls the clock again.
    auto c = test_config();
    c.wall_clock = [] {
        ESP_LOGW("pl_clock", "pl clock asked");
        return std::optional<std::chrono::system_clock::time_point>{};
    };
    start_clean(std::move(c));
    ESP_LOGW("pl_re", "pl reentrant line");
    pl::append("pl reentrant append");
    const std::string ring = ring_text();
    TEST_ASSERT_EQUAL(1, occurrences(ring, "W pl_re: pl reentrant line\n"));
    TEST_ASSERT_EQUAL(1, occurrences(ring, "pl reentrant append\n"));
    TEST_ASSERT_FALSE(contains(ring, "pl clock asked"));
    stop_checked();
}

TEST_CASE(
    "persistent_log doesn't capture what its store logs, from the writer or a flush",
    "[persistent_log]"
) {
    auto part = idfxx::partition::try_find("log");
    TEST_ASSERT_TRUE(part.has_value());
    auto whole = pl::partition_sector_store::make(*part);
    TEST_ASSERT_TRUE(whole.has_value());
    auto c = writer_config_of({.interval = std::chrono::milliseconds{50}});
    c.storage = std::make_unique<logging_store>(std::move(*whole));
    start_clean(std::move(c));
    // A line with the store's tag is still captured when code outside the log logs it. This task
    // logs it here, and it also runs the flush below.
    ESP_LOGW("pl_store", "pl store control");
    // One line is flushed by the writer task, and the next by this task. The store logs each time
    // it writes.
    pl::append("pl through the writer");
    TEST_ASSERT_TRUE(wait_for_flash("pl through the writer\n", 2000));
    pl::append("pl through a flush");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    const std::string all = read_all();
    TEST_ASSERT_TRUE(contains(all, " - W pl_store: pl store control\n"));
    TEST_ASSERT_TRUE(contains(all, "pl through a flush\n"));
    TEST_ASSERT_FALSE(contains(all, "pl store wrote"));
    TEST_ASSERT_FALSE(contains(ring_text(), "pl store wrote"));
    stop_checked();
}
#endif

TEST_CASE("persistent_log append stamps its entry and ends it", "[persistent_log]") {
    auto c = test_config();
    c.wall_clock = fixed_clock;
#ifdef CONFIG_IDFXX_STD_FORMAT
    c.capture = std::nullopt;
#endif
    start_clean(std::move(c));
    pl::append("pl hello");
    pl::append("pl first\npl second");
    pl::append("pl ended\n");
    pl::append("");
    ESP_LOGW("pl_nocap", "not captured without capture");
    const std::string ring = ring_text();

    const std::string hello = line_with(ring, "pl hello");
    TEST_ASSERT_TRUE(hello.starts_with("7."));
    TEST_ASSERT_TRUE(hello.ends_with(" 2026-01-02T03:04:05Z pl hello"));
    TEST_ASSERT_TRUE(line_with(ring, "pl first").ends_with(" 2026-01-02T03:04:05Z pl first"));
    // A later line continues the entry, behind a tab.
    TEST_ASSERT_EQUAL_STRING("\tpl second", line_with(ring, "pl second").c_str());
    // A newline at the end adds no empty line.
    TEST_ASSERT_TRUE(ring.ends_with(" 2026-01-02T03:04:05Z pl ended\n"));
    TEST_ASSERT_FALSE(contains(ring, "not captured"));

    // A line longer than the most that is kept is shortened.
    pl::append("pl long " + std::string(2 * CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE, 'x'));
    const std::string longest = line_with(ring_text(), "pl long ");
    TEST_ASSERT_EQUAL(
        CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE, longest.size() - longest.find("pl long ")
    );
    stop_checked();
}

#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
TEST_CASE(
    "persistent_log keeps a captured message's later lines behind a tab",
    "[persistent_log]"
) {
    start_clean();
    ESP_LOGW("pl_nl1", "pl first\npl second");
    // A leading newline leaves the first line's message empty, but the line is still kept.
    ESP_LOGW("pl_nl2", "\npl after leading");
    // Trailing and doubled newlines add no empty lines.
    ESP_LOGW("pl_nl3", "pl double\n\npl end\n");
    // Text made to look like a line of the log is kept behind a tab, as part of the entry before
    // it.
    ESP_LOGW("pl_forge", "ok\n12.0 +0 - E evil: x");
    const std::string ring = ring_text();

    TEST_ASSERT_TRUE(contains(ring, " - W pl_nl1: pl first\n\tpl second\n"));
    TEST_ASSERT_TRUE(contains(ring, " - W pl_nl2: \n\tpl after leading\n"));
    TEST_ASSERT_EQUAL(1, occurrences(ring, "pl_nl3"));
    TEST_ASSERT_TRUE(contains(ring, " - W pl_nl3: pl double\n\tpl end\n7."));
    TEST_ASSERT_TRUE(contains(ring, " - W pl_forge: ok\n\t12.0 +0 - E evil: x\n"));
    TEST_ASSERT_EQUAL_STRING("\t12.0 +0 - E evil: x", line_with(ring, "E evil: x").c_str());
    // Every line starts an entry with a stamp, or continues one behind a tab.
    TEST_ASSERT_TRUE(ring.starts_with("7."));
    TEST_ASSERT_EQUAL(
        occurrences(ring, "\n"), occurrences(ring, "\n7.") + occurrences(ring, "\n\t") + 1
    );
    stop_checked();
}
#endif

TEST_CASE(
    "persistent_log append keeps its text as one entry, skipping empty lines",
    "[persistent_log]"
) {
    start_clean(quiet_config());
    pl::append("\npl w1\n\npl w2\n\n");
    pl::append("\n");
    pl::append("ok\n12.0 +0 - E evil: x");
    const std::string ring = ring_text();
    // Two entries of two lines each. The first non-empty line of each entry is stamped, and the
    // next is behind a tab.
    TEST_ASSERT_EQUAL(4, occurrences(ring, "\n"));
    TEST_ASSERT_TRUE(ring.starts_with("7."));
    TEST_ASSERT_EQUAL(1, occurrences(ring, "\n7."));
    TEST_ASSERT_EQUAL(2, occurrences(ring, "\n\t"));
    TEST_ASSERT_TRUE(contains(ring, " - pl w1\n\tpl w2\n7."));
    TEST_ASSERT_TRUE(ring.ends_with(" - ok\n\t12.0 +0 - E evil: x\n"));
    TEST_ASSERT_EQUAL_STRING("\t12.0 +0 - E evil: x", line_with(ring, "E evil: x").c_str());
    stop_checked();
}

TEST_CASE(
    "persistent_log keeps a message of many lines in half again its size and a stamp",
    "[persistent_log]"
) {
    constexpr size_t most = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
    [[maybe_unused]] constexpr std::string_view head = "W pl_amp: ";
    // A message of as many lines as fit in a captured line, leaving room for the line's level,
    // timestamp, tag and color.
    std::string message;
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
    for (size_t i = 0; i < (most - 64) / 2; ++i) {
        message += "x\n";
    }
    start_clean();
    auto& ring = internal::rtc_ring();
    uint32_t before = ring.head;
    ESP_LOGW("pl_amp", "%s", message.c_str());
    size_t added = appended_since(before);
    // Every line is kept.
    TEST_ASSERT_EQUAL(message.size() / 2 - 1, occurrences(ring_text(), "\tx\n"));
    TEST_ASSERT_TRUE(added <= message.size() * 3 / 2 + pl::max_prefix_size + head.size());
    stop_checked();
#else
    auto& ring = internal::rtc_ring();
    uint32_t before = 0;
    size_t added = 0;
#endif

    // Text for append() of the most bytes that are kept, all in one-byte lines. This is the most
    // that an entry takes in the ring.
    message.clear();
    for (size_t i = 0; i < most / 2; ++i) {
        message += "x\n";
    }
    start_clean(quiet_config());
    before = ring.head;
    pl::append(message);
    added = appended_since(before);
    TEST_ASSERT_EQUAL(most / 2 - 1, occurrences(ring_text(), "\tx\n"));
    TEST_ASSERT_TRUE(added <= pl::max_prefix_size + most + most / 2 + 1);
    stop_checked();
}

TEST_CASE("persistent_log adds each entry to the ring in one piece", "[persistent_log]") {
    auto& ring = internal::rtc_ring();
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
    start_clean();
    uint32_t before = ring.head;
    ESP_LOGW("pl_one", "pl a\npl b\n\npl c");
    // The bytes added are the entry alone: its stamp and its lines, with nothing between them.
    std::string entry = text_added_since(before);
    TEST_ASSERT_TRUE(entry.starts_with("7."));
    TEST_ASSERT_TRUE(entry.ends_with(" - W pl_one: pl a\n\tpl b\n\tpl c\n"));
    TEST_ASSERT_EQUAL(3, occurrences(entry, "\n"));
    stop_checked();
#else
    uint32_t before = 0;
    std::string entry;
#endif

    start_clean(quiet_config());
    before = ring.head;
    pl::append("pl d\n\npl e\npl f\n");
    entry = text_added_since(before);
    TEST_ASSERT_TRUE(entry.starts_with("7."));
    TEST_ASSERT_TRUE(entry.ends_with(" - pl d\n\tpl e\n\tpl f\n"));
    TEST_ASSERT_EQUAL(3, occurrences(entry, "\n"));
    stop_checked();
}

TEST_CASE(
    "persistent_log append keeps the most kept bytes of its text in all",
    "[persistent_log]"
) {
    constexpr size_t most = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
    std::string text;
    for (int i = 0; text.size() < 2 * most; ++i) {
        text += "pl cap " + std::to_string(i) + "\n";
    }
    start_clean(quiet_config());
    auto& ring = internal::rtc_ring();
    const uint32_t before = ring.head;
    pl::append(text);
    const std::string entry = text_added_since(before);
    TEST_ASSERT_TRUE(entry.starts_with("7."));
    const size_t at = entry.find(" - ");
    TEST_ASSERT_TRUE(at != std::string::npos);
    // The entry holds the text's first `most` bytes, which end in the middle of a line, with each
    // line after the first behind a tab.
    std::string first = text.substr(0, most);
    while (first.ends_with('\n')) {
        first.pop_back();
    }
    std::string expected;
    for (char c : first) {
        expected += c;
        if (c == '\n') {
            expected += '\t';
        }
    }
    expected += '\n';
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), entry.substr(at + 3).c_str());
    // The number of text bytes kept, not counting the stamp, tabs and newlines.
    const std::string_view body = std::string_view(entry).substr(at + 3);
    const size_t kept = body.size() - occurrences(body, "\t") - occurrences(body, "\n");
    TEST_ASSERT_EQUAL(most - occurrences(text.substr(0, most), "\n"), kept);
    stop_checked();
}

TEST_CASE(
    "persistent_log append cuts its text at the start of a UTF-8 character",
    "[persistent_log]"
) {
    constexpr size_t most = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
    start_clean(quiet_config());
    // Each lead puts the cut at a different byte of a character.
    for (size_t lead = 0; lead < 3; ++lead) {
        const std::string text = three_byte_text(lead, most);
        const uint32_t before = internal::rtc_ring().head;
        pl::append(text);
        const std::string entry = text_added_since(before);
        const size_t at = entry.find(" - ");
        TEST_ASSERT_TRUE(at != std::string::npos);
        // As many complete characters as fit.
        const std::string expected = text.substr(0, lead + (most - lead) / 3 * 3) + "\n";
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), entry.substr(at + 3).c_str());
    }
    stop_checked();
}

#ifdef CONFIG_IDFXX_STD_FORMAT
TEST_CASE("persistent_log append formats its text", "[persistent_log]") {
    start_clean(quiet_config());
    const uint32_t before = internal::rtc_ring().head;
    pl::append("pl fmt {} of {} {}", 3, 4u, std::string_view("done"));
    const std::string entry = text_added_since(before);
    TEST_ASSERT_TRUE(entry.starts_with("7."));
    TEST_ASSERT_TRUE(entry.ends_with(" - pl fmt 3 of 4 done\n"));
    TEST_ASSERT_EQUAL(1, occurrences(entry, "\n"));
    stop_checked();
}

TEST_CASE(
    "persistent_log formatted append cuts its text as the plain append does",
    "[persistent_log]"
) {
    constexpr size_t most = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
    start_clean(quiet_config());
    // Each lead puts the cut at a different byte of a character. The text is much longer than the
    // formatting buffer, so formatting stops partway through it.
    for (size_t lead = 0; lead < 3; ++lead) {
        const std::string text = three_byte_text(lead, 2 * most);
        uint32_t before = internal::rtc_ring().head;
        pl::append(text);
        const std::string plain = text_added_since(before);
        before = internal::rtc_ring().head;
        pl::append("{}", text);
        const std::string formatted = text_added_since(before);
        const size_t plain_at = plain.find(" - ");
        const size_t formatted_at = formatted.find(" - ");
        TEST_ASSERT_TRUE(plain_at != std::string::npos);
        TEST_ASSERT_TRUE(formatted_at != std::string::npos);
        // As many complete characters as fit, the same as the plain append keeps.
        const std::string expected = text.substr(0, lead + (most - lead) / 3 * 3) + "\n";
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), plain.substr(plain_at + 3).c_str());
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), formatted.substr(formatted_at + 3).c_str());
    }
    stop_checked();
}
#endif

TEST_CASE("persistent_log append without arguments keeps braces as text", "[persistent_log]") {
    start_clean(quiet_config());
    const uint32_t before = internal::rtc_ring().head;
    pl::append("pl a {} b {0} c");
    const std::string entry = text_added_since(before);
    TEST_ASSERT_TRUE(entry.ends_with(" - pl a {} b {0} c\n"));
    stop_checked();
}

TEST_CASE("persistent_log default wall clock is unset before 2020", "[persistent_log]") {
    idfxx::test::run_after_each_test(restore_time);
    gettimeofday(&s_saved_time, nullptr);
    s_time_changed = true;
    auto c = test_config();
    c.wall_clock = {};
    start_clean(std::move(c));

    timeval before{.tv_sec = 1559347200, .tv_usec = 0}; // 2019-06-01T00:00:00Z
    settimeofday(&before, nullptr);
    pl::append("pl clock before");
    timeval after{.tv_sec = 1795305600, .tv_usec = 0}; // 2026-11-22T00:00:00Z
    settimeofday(&after, nullptr);
    pl::append("pl clock after");
    restore_time();

    const std::string ring = ring_text();
    TEST_ASSERT_TRUE(line_with(ring, "pl clock before").ends_with(" - pl clock before"));
    TEST_ASSERT_TRUE(contains(line_with(ring, "pl clock after"), " 2026-11-22T00:00:0"));
    stop_checked();
}

TEST_CASE("persistent_log flush, read and copy_newest", "[persistent_log]") {
    start_clean();
    pl::append("pl flush one");
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
    ESP_LOGW("pl_rd", "pl flush two");
#else
    pl::append("W pl_rd: pl flush two");
#endif
    TEST_ASSERT_FALSE(contains(flash_newest(), "pl flush one"));
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    const std::string newest = flash_newest();
    TEST_ASSERT_TRUE(contains(newest, "pl flush one\n"));
    TEST_ASSERT_TRUE(newest.ends_with("W pl_rd: pl flush two\n"));

    // copy_newest() flushes first, and returns a view of the front of its buffer.
    pl::append("pl flush copied");
    TEST_ASSERT_FALSE(contains(flash_newest(), "pl flush copied"));
    std::string buffer(8192, '\0');
    auto copied = pl::try_copy_newest(buffer);
    TEST_ASSERT_TRUE(copied.has_value());
    TEST_ASSERT_TRUE(copied->data() == buffer.data());
    TEST_ASSERT_TRUE(copied->ends_with(" pl flush copied\n"));
    TEST_ASSERT_TRUE(*copied == flash_newest());
    TEST_ASSERT_EQUAL_size_t(0, internal::rtc_ring().unflushed());

    // read_text() flushes first.
    pl::append("pl flush three");
    auto r = pl::try_read_text();
    TEST_ASSERT_TRUE(r.has_value());
    // Lines added after the reader was made are not read.
    pl::append("pl flush four");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    const std::string all = read_all(*r);
    TEST_ASSERT_TRUE(contains(all, "pl flush one\n"));
    TEST_ASSERT_TRUE(contains(all, "pl flush two\n"));
    TEST_ASSERT_TRUE(all.ends_with("pl flush three\n"));
    TEST_ASSERT_FALSE(contains(all, "pl flush four"));
    TEST_ASSERT_TRUE(contains(read_all(), "pl flush four\n"));

    // A reader reads no more once the log is stopped.
    auto kept = pl::try_read_text();
    TEST_ASSERT_TRUE(kept.has_value());
    stop_checked();
    std::array<char, 16> chunk;
    TEST_ASSERT_TRUE(fails_with(kept->try_next(chunk), idfxx::errc::invalid_state));
}

namespace {

using pl::testing::last_of;
using pl::testing::read_newest;
using pl::testing::stored_entries;

std::vector<std::string> read_entries() {
    auto r = pl::try_read();
    TEST_ASSERT_TRUE(r.has_value());
    return stored_entries(*r);
}

// The one entry stored as `text`.
pl::entry parsed(const std::string& text) {
    std::string_view rest = text;
    pl::entry e;
    TEST_ASSERT_TRUE(pl::next_entry(rest, e));
    TEST_ASSERT_TRUE(rest.empty());
    return e;
}

// The entries as a text reader returns them, each followed by a newline.
std::string joined(const std::vector<std::string>& entries) {
    std::string s;
    for (const auto& e : entries) {
        s += e;
        s += '\n';
    }
    return s;
}

} // namespace

TEST_CASE("persistent_log read returns each entry with its stamp", "[persistent_log]") {
    auto c = quiet_config();
    c.wall_clock = fixed_clock;
    start_clean(std::move(c));
    const auto before = std::chrono::milliseconds(esp_timer_get_time() / 1000);
    pl::append("pl entry one");
    pl::append("pl entry two\npl entry three");
    const auto after = std::chrono::milliseconds(esp_timer_get_time() / 1000 + 1);
    const auto got = read_entries();
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    for (const auto& s : got) {
        const auto e = parsed(s);
        TEST_ASSERT_TRUE(e.stamped());
        TEST_ASSERT_EQUAL_UINT32(7, e.boot());
        TEST_ASSERT_EQUAL_UINT32(internal::rtc().wake.wake, e.wake());
        TEST_ASSERT_TRUE(e.uptime() >= before && e.uptime() <= after);
        TEST_ASSERT_TRUE(e.wall_time() == fixed_clock());
    }
    TEST_ASSERT_TRUE(parsed(got[0]).text() == "pl entry one");
    TEST_ASSERT_TRUE(parsed(got[1]).text() == "pl entry two\n\tpl entry three");
    // The entries are the text that read_text() returns.
    TEST_ASSERT_TRUE(joined(got) == read_all());
    stop_checked();
}

TEST_CASE("persistent_log read_newest returns the newest entries in flash", "[persistent_log]") {
    start_clean(quiet_config());
    for (int i = 0; i < 10; ++i) {
        pl::append("pl newest " + std::to_string(i) + (i % 3 == 0 ? "\nwith a second line" : ""));
    }
    const auto all = read_entries();
    TEST_ASSERT_EQUAL_size_t(10, all.size());
    for (size_t count : {size_t{0}, size_t{1}, size_t{3}, size_t{9}, size_t{10}, size_t{11}}) {
        TEST_ASSERT_TRUE(read_newest(count) == last_of(all, count));
    }
    TEST_ASSERT_TRUE(read_newest(SIZE_MAX) == all);
    // read_newest() flushes first, so the lines still in RTC memory are included.
    pl::append("pl newest waiting");
    TEST_ASSERT_FALSE(contains(flash_newest(), "pl newest waiting"));
    const auto newest = read_newest(2);
    TEST_ASSERT_EQUAL_size_t(2, newest.size());
    TEST_ASSERT_TRUE(newest[0] == all.back());
    TEST_ASSERT_TRUE(newest[1].ends_with(" pl newest waiting"));
    TEST_ASSERT_EQUAL_size_t(0, internal::rtc_ring().unflushed());
    stop_checked();
}

TEST_CASE(
    "persistent_log entry readers end where the log ended, and stop with the log",
    "[persistent_log]"
) {
    start_clean(quiet_config());
    pl::append("pl a");
    pl::append("pl b");
    auto r = pl::try_read();
    TEST_ASSERT_TRUE(r.has_value());
    auto newest = pl::try_read_newest(1);
    TEST_ASSERT_TRUE(newest.has_value());
    // Entries added after the readers were made are not read.
    pl::append("pl c");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    auto got = stored_entries(*r);
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(got[0].ends_with(" pl a") && got[1].ends_with(" pl b"));
    got = stored_entries(*newest);
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    TEST_ASSERT_TRUE(got[0].ends_with(" pl b"));

    // A reader reads no more once the log is stopped, even with entries left in its buffer.
    auto kept = pl::try_read();
    TEST_ASSERT_TRUE(kept.has_value());
    auto kept_newest = pl::try_read_newest(2);
    TEST_ASSERT_TRUE(kept_newest.has_value());
    pl::entry e;
    TEST_ASSERT_TRUE(kept->try_next(e).value_or(false));
    TEST_ASSERT_TRUE(kept_newest->try_next(e).value_or(false));
    stop_checked();
    TEST_ASSERT_TRUE(fails_with(kept->try_next(e), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(kept_newest->try_next(e), idfxx::errc::invalid_state));
    // After the log is started again, the readers still fail.
    TEST_ASSERT_TRUE(pl::try_start(quiet_config()).has_value());
    TEST_ASSERT_TRUE(fails_with(kept->try_next(e), idfxx::errc::invalid_state));
    stop_checked();
}

namespace {

// A store in RAM that fails every write and erase while its writes are set to fail. Its reads
// always succeed. It counts the writes and erases that it failed.
class write_failing_store final : public pl::sector_store {
public:
    write_failing_store(size_t sectors, size_t sector_size)
        : write_failing_store(std::make_unique<pl::ram_sector_store>(sectors, sector_size)) {}

    void fail_writes(bool on) { _failing = on; }

    [[nodiscard]] size_t failed() const { return _failed; }

private:
    explicit write_failing_store(std::unique_ptr<pl::ram_sector_store> ram)
        : sector_store(ram->size(), ram->sector_size(), ram->write_size())
        , _ram(std::move(ram)) {}

    idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
        return _ram->try_read(offset, dst);
    }
    idfxx::result<void> do_read_raw(size_t offset, idfxx::byte_span dst) const override {
        return _ram->try_read_raw(offset, dst);
    }
    idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
        if (_failing) {
            ++_failed;
            return idfxx::error(idfxx::errc::fail);
        }
        return _ram->try_write(offset, src);
    }
    idfxx::result<void> do_erase_sector(size_t index) override {
        if (_failing) {
            ++_failed;
            return idfxx::error(idfxx::errc::fail);
        }
        return _ram->try_erase_sector(index);
    }

    std::unique_ptr<pl::ram_sector_store> _ram;
    bool _failing = false;
    size_t _failed = 0;
};

} // namespace

TEST_CASE(
    "persistent_log reads and copy_newest read flash when their flush fails",
    "[persistent_log]"
) {
    auto store = std::make_unique<write_failing_store>(8, 1024);
    auto* raw = store.get();
    auto c = quiet_config();
    c.storage = std::move(store);
    c.faults = std::nullopt;
    start_clean(std::move(c));
    pl::append("pl stored");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    raw->fail_writes(true);

    // Each call's flush fails. The reader reads what is in flash, without the entry that the flush
    // couldn't write.
    pl::append("pl not stored");
    auto r = pl::try_read();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_GREATER_THAN_size_t(0, raw->failed());
    const auto got = stored_entries(*r);
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    TEST_ASSERT_TRUE(got[0].ends_with(" pl stored"));

    const size_t failed = raw->failed();
    pl::append("pl not stored either");
    auto t = pl::try_read_text();
    TEST_ASSERT_TRUE(t.has_value());
    TEST_ASSERT_GREATER_THAN_size_t(failed, raw->failed());
    const std::string all = read_all(*t);
    TEST_ASSERT_TRUE(all.ends_with(" pl stored\n"));
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, "\n"));

    const size_t failed_text = raw->failed();
    pl::append("pl not stored for read_newest");
    auto newest = pl::try_read_newest(5);
    TEST_ASSERT_TRUE(newest.has_value());
    TEST_ASSERT_GREATER_THAN_size_t(failed_text, raw->failed());
    const auto newest_got = stored_entries(*newest);
    TEST_ASSERT_EQUAL_size_t(1, newest_got.size());
    TEST_ASSERT_TRUE(newest_got[0].ends_with(" pl stored"));

    const size_t failed_newest = raw->failed();
    pl::append("pl not stored for copy_newest");
    std::string buffer(1024, '\0');
    auto copied = pl::try_copy_newest(buffer);
    TEST_ASSERT_TRUE(copied.has_value());
    TEST_ASSERT_GREATER_THAN_size_t(failed_newest, raw->failed());
    TEST_ASSERT_TRUE(copied->ends_with(" pl stored\n"));
    TEST_ASSERT_EQUAL_size_t(1, occurrences(*copied, "\n"));
    raw->fail_writes(false);
    stop_checked();
}

TEST_CASE("persistent_log reads an entry of the longest text whole", "[persistent_log]") {
    start_clean(quiet_config());
    constexpr size_t max_line = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
    // The most lines that an entry's text can hold, each of one byte.
    std::string lines;
    std::string want = "a";
    while (lines.size() + 2 <= max_line) {
        lines += "a\n";
        if (lines.size() > 2) {
            want += "\n\ta";
        }
    }
    pl::append(lines);
    pl::append(std::string(max_line, 'x'));
    const auto got = read_entries();
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(parsed(got[0]).text() == want);
    TEST_ASSERT_TRUE(parsed(got[1]).text() == std::string(max_line, 'x'));
    TEST_ASSERT_TRUE(read_newest(1) == last_of(got, 1));
    TEST_ASSERT_TRUE(read_newest(2) == got);
    TEST_ASSERT_TRUE(joined(got) == read_all());
    stop_checked();
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
// The iterator holds only a pointer to its reader.
static_assert(sizeof(pl::entry_reader::iterator) == sizeof(void*));

namespace {

// The stored text of each entry that a range-based for loop over `r` returns.
std::vector<std::string> ranged(pl::entry_reader&& r) {
    std::vector<std::string> out;
    for (const auto& e : r) {
        out.emplace_back(pl::detail::entry_access::stored(e));
    }
    return out;
}

} // namespace

TEST_CASE("persistent_log entry readers are ranges of their entries", "[persistent_log]") {
    start_clean(quiet_config());
    // An empty log.
    TEST_ASSERT_TRUE(ranged(pl::read()).empty());
    TEST_ASSERT_TRUE(ranged(pl::read_newest(5)).empty());

    // The whole log, and its newest entries, are what the calls without ranges read.
    for (int i = 0; i < 8; ++i) {
        pl::append("pl ranged " + std::to_string(i) + (i % 3 == 0 ? "\nwith a second line" : ""));
    }
    const auto all = read_entries();
    TEST_ASSERT_EQUAL_size_t(8, all.size());
    TEST_ASSERT_TRUE(ranged(pl::read()) == all);
    for (size_t count : {size_t{0}, size_t{1}, size_t{3}, size_t{8}, size_t{9}}) {
        TEST_ASSERT_TRUE(ranged(pl::read_newest(count)) == read_newest(count));
    }
    // The loop in the form that the docs show.
    size_t n = 0;
    for (const auto& e : pl::read_newest(3)) {
        TEST_ASSERT_TRUE(pl::detail::entry_access::stored(e) == all[all.size() - 3 + n]);
        ++n;
    }
    TEST_ASSERT_EQUAL_size_t(3, n);
    // read_newest() flushes, so the range includes a line that is still in RTC memory.
    pl::append("pl ranged waiting");
    const auto newest = ranged(pl::read_newest(1));
    TEST_ASSERT_EQUAL_size_t(1, newest.size());
    TEST_ASSERT_TRUE(newest[0].ends_with(" pl ranged waiting"));

    // A second loop continues after the entry that the first loop ended at, and begin() on a
    // reader that has returned every entry is the end.
    auto r = pl::read();
    std::vector<std::string> got;
    for (const auto& e : r) {
        got.emplace_back(pl::detail::entry_access::stored(e));
        if (got.size() == 3) {
            break;
        }
    }
    pl::entry e;
    TEST_ASSERT_TRUE(r.next(e));
    got.emplace_back(pl::detail::entry_access::stored(e));
    for (const auto& rest : r) {
        got.emplace_back(pl::detail::entry_access::stored(rest));
    }
    TEST_ASSERT_TRUE(got == read_entries());
    TEST_ASSERT_TRUE(r.begin() == r.end());
    TEST_ASSERT_FALSE(r.next(e));

    // An iterator reads no more once the log is stopped.
    auto kept = pl::read();
    auto it = kept.begin();
    TEST_ASSERT_FALSE(it == kept.end());
    stop_checked();
    bool threw = false;
    try {
        ++it;
    } catch (const std::system_error& ex) {
        threw = ex.code() == idfxx::errc::invalid_state;
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("persistent_log reads throw what the try_ calls return", "[persistent_log]") {
    auto code_of = [](auto&& call) {
        try {
            call();
        } catch (const std::system_error& e) {
            return e.code();
        }
        return std::error_code();
    };
    stop_log();
    TEST_ASSERT_TRUE(code_of([] { (void)pl::read(); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([] { (void)pl::read_newest(1); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([] { (void)pl::read_text(); }) == idfxx::errc::invalid_state);

    start_clean(quiet_config());
    pl::append("pl thrown");
    auto r = pl::read();
    pl::entry e;
    TEST_ASSERT_TRUE(r.next(e));
    TEST_ASSERT_TRUE(e.text() == "pl thrown");
    TEST_ASSERT_FALSE(r.next(e));
    auto newest = pl::read_newest(1);
    TEST_ASSERT_TRUE(newest.next(e));
    TEST_ASSERT_TRUE(e.text() == "pl thrown");
    auto text = pl::read_text();
    std::array<char, 64> chunk;
    const std::string_view part = text.next(chunk);
    TEST_ASSERT_TRUE(part.data() == chunk.data());
    TEST_ASSERT_TRUE(part.ends_with(" pl thrown\n"));
    TEST_ASSERT_TRUE(text.next(chunk).empty());
    std::array<char, 256> buffer;
    const std::string_view copied = pl::copy_newest(buffer);
    TEST_ASSERT_TRUE(copied.data() == buffer.data());
    TEST_ASSERT_TRUE(copied.ends_with(" pl thrown\n"));
    stop_checked();
    TEST_ASSERT_TRUE(code_of([&] { (void)newest.next(e); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([&] { (void)text.next(chunk); }) == idfxx::errc::invalid_state);
}
#endif

TEST_CASE("persistent_log start ends a line left cut short in flash", "[persistent_log]") {
    auto c = test_config();
    c.faults = std::nullopt;
    start_clean(std::move(c));
    pl::append("pl before");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    stop_checked();

    // A line cut short, as a power cut leaves it when the cut tears the header of the sector that
    // the line continued into.
    {
        auto part = idfxx::partition::try_find("log");
        TEST_ASSERT_TRUE(part.has_value());
        auto store = pl::partition_sector_store::make(*part);
        TEST_ASSERT_TRUE(store.has_value());
        auto flash = pl::flash_log::try_open(*store);
        TEST_ASSERT_TRUE(flash.has_value());
        TEST_ASSERT_TRUE(flash->try_append("pl cut line").has_value());
    }

    c = test_config();
    c.faults = std::nullopt;
    TEST_ASSERT_TRUE(pl::try_start(std::move(c)).has_value());
    pl::append("pl after start");
    const std::string all = read_all();
    TEST_ASSERT_TRUE(contains(all, "pl before\n"));
    TEST_ASSERT_TRUE(contains(all, "\npl cut line\n"));
    const std::string after = line_with(all, "pl after start");
    TEST_ASSERT_TRUE(after.ends_with(" - pl after start"));
    TEST_ASSERT_TRUE(after[0] >= '0' && after[0] <= '9');
    stop_checked();
}

TEST_CASE("persistent_log clear empties flash and the waiting lines", "[persistent_log]") {
    start_clean();
    pl::append("pl clear flushed");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    pl::append("pl clear waiting");
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    pl::append("pl clear after");
    const std::string all = read_all();
    TEST_ASSERT_FALSE(contains(all, "pl clear flushed"));
    TEST_ASSERT_FALSE(contains(all, "pl clear waiting"));
    TEST_ASSERT_TRUE(contains(all, "pl clear after\n"));
    stop_checked();
}

TEST_CASE("persistent_log clear empties the lines in RTC memory", "[persistent_log]") {
    start_clean(quiet_config());
    pl::append("pl cleared flushed");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    pl::append("pl cleared waiting");
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    TEST_ASSERT_TRUE(ring_text().empty());
    pl::append("pl kept after clear");
    const std::string ring = ring_text();
    TEST_ASSERT_TRUE(ring.ends_with(" pl kept after clear\n"));
    TEST_ASSERT_EQUAL(1, occurrences(ring, "\n"));
    TEST_ASSERT_FALSE(contains(ring, "pl cleared"));
    stop_checked();
}

TEST_CASE("persistent_log flush_if_pending flushes only past its threshold", "[persistent_log]") {
    start_clean();
    pl::append("pl threshold line");
    TEST_ASSERT_TRUE(pl::try_flush_if_pending(1000).has_value());
    TEST_ASSERT_FALSE(contains(flash_newest(), "pl threshold line"));
    TEST_ASSERT_TRUE(pl::try_flush_if_pending(1).has_value());
    TEST_ASSERT_TRUE(contains(flash_newest(), "pl threshold line\n"));
    stop_checked();
}

TEST_CASE("persistent_log without a writer flushes only when asked", "[persistent_log]") {
    start_clean();
    pl::append("pl no writer");
    vTaskDelay(pdMS_TO_TICKS(200));
    TEST_ASSERT_FALSE(contains(flash_newest(), "pl no writer"));
    // stop() flushes.
    stop_checked();
    TEST_ASSERT_TRUE(pl::try_start(test_config()).has_value());
    TEST_ASSERT_TRUE(contains(flash_newest(), "pl no writer\n"));
    stop_checked();
}

TEST_CASE("persistent_log writer threshold must be inside the ring", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    constexpr size_t ring = CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE;
    TEST_ASSERT_TRUE(pl::writer_config{}.threshold < ring);

    TEST_ASSERT_TRUE(
        fails_with(pl::try_start(writer_config_of({.threshold = 0})), idfxx::errc::invalid_arg)
    );

    TEST_ASSERT_TRUE(
        fails_with(pl::try_start(writer_config_of({.threshold = ring})), idfxx::errc::invalid_arg)
    );

    TEST_ASSERT_TRUE(pl::try_start(writer_config_of({.threshold = ring - 1})).has_value());
    stop_checked();
}

TEST_CASE(
    "persistent_log writer interval must be at least a tick and not overflow",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    using std::chrono::milliseconds;
    constexpr int64_t one_tick_ms = portTICK_PERIOD_MS;
    constexpr int64_t max_ms = static_cast<int64_t>(UINT32_MAX / configTICK_RATE_HZ);

    if (one_tick_ms > 1) {
        TEST_ASSERT_TRUE(fails_with(
            pl::try_start(writer_config_of({.interval = milliseconds{one_tick_ms - 1}})),
            idfxx::errc::invalid_arg
        ));
    }

    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(writer_config_of({.interval = milliseconds{max_ms + 1}})),
        idfxx::errc::invalid_arg
    ));

    TEST_ASSERT_TRUE(
        pl::try_start(writer_config_of({.interval = milliseconds{one_tick_ms}})).has_value()
    );
    stop_checked();

    TEST_ASSERT_TRUE(
        pl::try_start(writer_config_of({.interval = milliseconds{max_ms}})).has_value()
    );
    stop_checked();
}

TEST_CASE(
    "persistent_log writer priority and stack must be ones FreeRTOS takes",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    constexpr unsigned priorities = configMAX_PRIORITIES;

    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(writer_config_of({.priority = priorities})), idfxx::errc::invalid_arg
    ));
    TEST_ASSERT_TRUE(fails_with(pl::try_read_text(), idfxx::errc::invalid_state));

    TEST_ASSERT_TRUE(
        fails_with(pl::try_start(writer_config_of({.stack_size = 0})), idfxx::errc::invalid_arg)
    );
    TEST_ASSERT_TRUE(fails_with(pl::try_read_text(), idfxx::errc::invalid_state));

    TEST_ASSERT_TRUE(pl::try_start(writer_config_of({.priority = priorities - 1})).has_value());
    stop_checked();
}

TEST_CASE("persistent_log writer flushes on its interval", "[persistent_log]") {
    auto c = test_config();
    c.writer = pl::writer_config{
        .interval = std::chrono::milliseconds{100},
        .threshold = CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE - 1
    };
    start_clean(std::move(c));
    pl::append("pl interval line");
    TEST_ASSERT_TRUE(wait_for_flash("pl interval line\n", 2000));
    stop_checked();
}

TEST_CASE("persistent_log writer flushes past its threshold", "[persistent_log]") {
    auto c = test_config();
    c.writer = pl::writer_config{.interval = std::chrono::hours{1}, .threshold = 512};
    start_clean(std::move(c));
    pl::append("pl below threshold");
    vTaskDelay(pdMS_TO_TICKS(200));
    TEST_ASSERT_FALSE(contains(flash_newest(), "pl below threshold"));

    // Enough lines to fill more than a sector, so that the writer starts (erases) sectors.
    const std::string filler(80, 'f');
    for (int i = 0; i < 64; ++i) {
        pl::append("pl filler " + std::to_string(i) + " " + filler);
        // A line and the four lines after it take the waiting bytes past the threshold, so by line
        // i the writer has flushed line i - 4.
        if (i % 8 == 7) {
            TEST_ASSERT_TRUE(wait_for_flash("pl filler " + std::to_string(i - 4) + " ", 2000));
        }
    }
    TEST_ASSERT_TRUE(contains(read_all(), "pl below threshold\n"));

    TaskHandle_t writer = xTaskGetHandle("persistent_log");
    TEST_ASSERT_NOT_NULL(writer);
    const size_t free_bytes = uxTaskGetStackHighWaterMark(writer) * sizeof(StackType_t);
    printf(
        "persistent_log writer stack high-water mark: %u of %u bytes free\n",
        static_cast<unsigned>(free_bytes),
        static_cast<unsigned>(pl::writer_config{}.stack_size)
    );
    TEST_ASSERT_GREATER_THAN(512, free_bytes);
    stop_checked();
}

#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
TEST_CASE("persistent_log captures a line below the console's runtime level", "[persistent_log]") {
    idfxx::test::run_after_each_test(restore_vprintf);
    idfxx::test::run_after_each_test(restore_levels);
    start_clean();
    s_other_previous = esp_log_set_vprintf(silent_vprintf);
    s_other_installed = true;
    esp_log_level_set("pl_rt", ESP_LOG_ERROR);
    s_levels_changed = true;
    s_other_calls = 0;
    ESP_LOGW("pl_rt", "pl below the console %d", 1);
    // The three calls that a Wi-Fi library makes for a line, below the console's level for the
    // tag.
    esp_log_write(ESP_LOG_WARN, "pl_rt", "%c (%d) %s:", 'W', 200, "pl_rt");
    esp_log_write(ESP_LOG_WARN, "pl_rt", "%s", "pl blob below the console");
    esp_log_write(ESP_LOG_WARN, "pl_rt", "%s", "\n");
    // idfxx::log's lines, whether formatted or passed as a string, are treated the same as
    // ESP_LOGx's lines.
    idfxx::log::warn("pl_rt", "pl idfxx formatted {}", 2);
    idfxx::log::warn("pl_rt", std::string_view{"pl idfxx string"});
    // The console printed none of them.
    TEST_ASSERT_EQUAL(0, s_other_calls);
    const std::string ring = ring_text();
    TEST_ASSERT_TRUE(contains(ring, " - W pl_rt: pl below the console 1\n"));
    TEST_ASSERT_TRUE(contains(ring, " - W pl_rt: pl blob below the console\n"));
    TEST_ASSERT_TRUE(contains(ring, " - W pl_rt: pl idfxx formatted 2\n"));
    TEST_ASSERT_TRUE(contains(ring, " - W pl_rt: pl idfxx string\n"));
    stop_checked();
}
#endif

#ifdef CONFIG_IDFXX_STD_FORMAT
TEST_CASE("persistent_log has idfxx::log format only the lines it keeps", "[persistent_log]") {
    idfxx::test::run_after_each_test(restore_levels);
    start_clean();
    esp_log_level_set("pl_rt", ESP_LOG_ERROR);
    s_levels_changed = true;
    s_formatted = 0;
    // A line below the console's level but at capture's level is formatted and kept.
    idfxx::log::warn("pl_rt", "pl idfxx kept {}", counted{});
    TEST_ASSERT_EQUAL(1, s_formatted);
    // A line below both levels is never formatted.
    idfxx::log::info("pl_rt", "pl idfxx unwanted {}", counted{});
    TEST_ASSERT_EQUAL(1, s_formatted);
    const std::string ring = ring_text();
    TEST_ASSERT_TRUE(contains(ring, " - W pl_rt: pl idfxx kept counted\n"));
    TEST_ASSERT_FALSE(contains(ring, "pl idfxx unwanted"));
    // Once the log is stopped, a line that the console skips is no longer formatted. The log has
    // also removed its sink, so idfxx::log accepts as many other sinks as it can hold.
    stop_checked();
    idfxx::log::warn("pl_rt", "pl idfxx stopped {}", counted{});
    TEST_ASSERT_EQUAL(1, s_formatted);
    TEST_ASSERT_EQUAL(CONFIG_IDFXX_LOG_MAX_SINKS, fill_sinks());
}

TEST_CASE("persistent_log has idfxx::log format no line logged inside append", "[persistent_log]") {
    idfxx::test::run_after_each_test(restore_levels);
    // Below the console's level, so that only capture would have the line formatted.
    esp_log_level_set("pl_rt", ESP_LOG_ERROR);
    s_levels_changed = true;
    // A wall clock that logs a line that capture would keep. append() asks the clock for the stamp.
    auto c = test_config();
    c.wall_clock = [] {
        idfxx::log::warn("pl_rt", "pl clock {}", counted{});
        return std::optional<std::chrono::system_clock::time_point>{};
    };
    start_clean(std::move(c));
    s_formatted = 0;
    pl::append("pl written");
    TEST_ASSERT_EQUAL(0, s_formatted);
    const std::string ring = ring_text();
    TEST_ASSERT_TRUE(contains(ring, " - pl written\n"));
    TEST_ASSERT_FALSE(contains(ring, "pl clock"));
    stop_checked();
}

TEST_CASE(
    "persistent_log a start that can't add its sink fails and leaves the log stopped",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    TEST_ASSERT_EQUAL(CONFIG_IDFXX_LOG_MAX_SINKS, fill_sinks());
    // With every sink slot taken, start fails after it has started the writer task.
    TEST_ASSERT_TRUE(fails_with(pl::try_start(writer_config_of({})), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_read_text(), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_stop(), idfxx::errc::invalid_state));
    // Without capture, the log adds no sink.
    TEST_ASSERT_TRUE(pl::try_start(quiet_config()).has_value());
    stop_checked();
    // With a sink slot free, the log captures, and removes its sink at stop.
    idfxx::log::remove_sink(s_idle[0]);
    start_clean();
    idfxx::log::warn("pl_roll", "pl captured with a sink free");
    TEST_ASSERT_TRUE(contains(ring_text(), " - W pl_roll: pl captured with a sink free\n"));
    stop_checked();
    TEST_ASSERT_TRUE(idfxx::log::try_add_sink(s_idle[0]).has_value());
}
#endif

#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
TEST_CASE(
    "persistent_log capture's stack cost stays within its documented bound",
    "[persistent_log]"
) {
    constexpr size_t given = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
    // A line nearly as long as the longest line that a sink receives from idfxx::log and that the
    // log keeps. The rest leaves room for the line's prefix with a long timestamp, and for the
    // stored head.
    constexpr size_t longest = std::min<size_t>(given, CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE);
    const std::string message(longest - 32, 'x');
    auto c = test_config();
    c.wall_clock = fixed_clock;
    start_clean(std::move(c));
    const size_t capturing = stack_to_log(message.c_str(), logged_with::esp_logw);
    TEST_ASSERT_TRUE(contains(ring_text(), "W pl_stack: " + message + "\n"));
    stop_checked();
    // A sink added with all_lines that keeps only a size, to measure the stack that idfxx::log
    // itself uses.
    idfxx::test::run_after_each_test(remove_sizing);
    s_sizing.last = 0;
    TEST_ASSERT_TRUE(idfxx::log::try_add_sink(idfxx::log::all_lines, s_sizing).has_value());
    const size_t sizing = stack_to_log(message.c_str(), logged_with::esp_logw);
    TEST_ASSERT_EQUAL(message.size(), s_sizing.last.load());
    idfxx::log::remove_sink(s_sizing);
    // With the log stopped and no sink added with all_lines, the wrapper only passes calls on.
    const size_t stopped = stack_to_log(message.c_str(), logged_with::esp_logw);
    // Calling esp_log itself, bypassing the wrapper.
    const size_t without = stack_to_log(message.c_str(), logged_with::real_esp_log);
    printf(
        "persistent_log stack to log a %u-byte line: %u without the wrapper, %u stopped, "
        "%u with a sink that keeps a size, %u capturing\n",
        static_cast<unsigned>(message.size()),
        static_cast<unsigned>(without),
        static_cast<unsigned>(stopped),
        static_cast<unsigned>(sizing),
        static_cast<unsigned>(capturing)
    );
    // On esp32s3 in QEMU with ESP-IDF 6.1.1, capturing measured 688 bytes more than with a sink
    // that keeps a size. The documented bound is idfxx::log's max_line + 64, plus up to about 768
    // more.
    TEST_ASSERT_LESS_OR_EQUAL(sizing + 768, capturing);
    TEST_ASSERT_LESS_OR_EQUAL(without + given + 64 + 768, capturing);
}
#endif

#if defined(CONFIG_IDFXX_STD_FORMAT) && !defined(CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES)
TEST_CASE("persistent_log without all_lines keeps only idfxx::log lines", "[persistent_log]") {
    start_clean();
    ESP_LOGW("pl_only", "pl esp warning %d", 1);
    esp_log_write(ESP_LOG_WARN, "pl_only", "W (%d) %s: pl esp write\n", 1, "pl_only");
    // A buffer dump is logged through ESP-IDF's log, so it is not kept either.
    idfxx::log::buffer_char(level::warn, "pl_only", std::string_view{"pl dump"});
    idfxx::log::warn("pl_only", "pl idfxx warning {}", 2);
    const std::string ring = ring_text();
    TEST_ASSERT_FALSE(contains(ring, "pl esp warning"));
    TEST_ASSERT_FALSE(contains(ring, "pl esp write"));
    TEST_ASSERT_FALSE(contains(ring, "pl dump"));
    TEST_ASSERT_TRUE(contains(ring, " - W pl_only: pl idfxx warning 2\n"));
    stop_checked();
}

TEST_CASE(
    "persistent_log capture's stack cost without all_lines stays within its documented bound",
    "[persistent_log]"
) {
    // A line nearly as long as the longest entry that the log keeps.
    const std::string message(CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE - 32, 'x');
    auto c = test_config();
    c.wall_clock = fixed_clock;
    start_clean(std::move(c));
    const size_t capturing = stack_to_log(message.c_str(), logged_with::idfxx_log);
    TEST_ASSERT_TRUE(contains(ring_text(), "W pl_stack: " + message + "\n"));
    stop_checked();
    // A sink that keeps only a size, to measure the stack that idfxx::log itself uses for a sink.
    idfxx::test::run_after_each_test(remove_sizing);
    s_sizing.last = 0;
    TEST_ASSERT_TRUE(idfxx::log::try_add_sink(s_sizing).has_value());
    const size_t sizing = stack_to_log(message.c_str(), logged_with::idfxx_log);
    TEST_ASSERT_EQUAL(message.size(), s_sizing.last.load());
    idfxx::log::remove_sink(s_sizing);
    // With the log stopped and no sink added, idfxx::log only prints the line.
    const size_t stopped = stack_to_log(message.c_str(), logged_with::idfxx_log);
    // ESP_LOGW, which nothing wraps without all_lines.
    const size_t esp_logw = stack_to_log(message.c_str(), logged_with::esp_logw);
    // The console prints the line before idfxx::log passes it to the sinks, and printing can use
    // more stack than capture does. So the same lines are measured again with a console that
    // prints nothing, where capture's own stack shows.
    idfxx::test::run_after_each_test(restore_vprintf);
    s_other_previous = esp_log_set_vprintf(silent_vprintf);
    s_other_installed = true;
    start_clean(test_config());
    const size_t capturing_quiet = stack_to_log(message.c_str(), logged_with::idfxx_log);
    TEST_ASSERT_TRUE(contains(ring_text(), "W pl_stack: " + message + "\n"));
    stop_checked();
    const size_t stopped_quiet = stack_to_log(message.c_str(), logged_with::idfxx_log);
    restore_vprintf();
    printf(
        "persistent_log stack to log a %u-byte line without all_lines: %u with ESP_LOGW, %u "
        "stopped, %u with a sink that keeps a size, %u capturing; with a silent console, %u "
        "stopped, %u capturing\n",
        static_cast<unsigned>(message.size()),
        static_cast<unsigned>(esp_logw),
        static_cast<unsigned>(stopped),
        static_cast<unsigned>(sizing),
        static_cast<unsigned>(capturing),
        static_cast<unsigned>(stopped_quiet),
        static_cast<unsigned>(capturing_quiet)
    );
    // On esp32s3 in QEMU with ESP-IDF 6.1.1, capturing measured 320 bytes more than stopped, and
    // 688 bytes more with a silent console. The documented bound is up to about 768 bytes more than
    // idfxx::log uses without capture.
    TEST_ASSERT_LESS_OR_EQUAL(stopped + 768, capturing);
    TEST_ASSERT_LESS_OR_EQUAL(stopped_quiet + 768, capturing_quiet);
}
#endif

TEST_CASE(
    "persistent_log copy_newest_in_memory reads RTC memory without a start",
    "[persistent_log]"
) {
    start_clean();
    pl::append("pl ring marker");
    stop_checked();
    TEST_ASSERT_TRUE(contains(ring_text(), "pl ring marker\n"));
    // A small buffer receives complete lines from the newest end.
    std::array<char, 40> small;
    const std::string_view got = pl::copy_newest_in_memory(small);
    TEST_ASSERT_TRUE(got.data() == small.data());
    TEST_ASSERT_TRUE(got.empty() || got.ends_with("pl ring marker\n"));
    std::array<char, 256> large;
    const std::string_view all = pl::copy_newest_in_memory(large);
    TEST_ASSERT_TRUE(all.data() == large.data());
    TEST_ASSERT_TRUE(all.ends_with("pl ring marker\n"));
}

TEST_CASE("persistent_log marks the bytes an overrun dropped", "[persistent_log]") {
    start_clean(quiet_config());
    auto& ring = internal::rtc_ring();
    const uint32_t before = ring.head;
    // Write twice what the ring holds, with no writer to flush it, so the oldest lines are
    // dropped.
    const std::string filler(60, 'o');
    constexpr int lines = 2 * CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE / 64;
    for (int i = 0; i < lines; ++i) {
        pl::append("pl overrun " + std::to_string(i) + " " + filler);
    }
    const size_t appended = appended_since(before);
    TEST_ASSERT_TRUE(appended > sizeof ring.data);
    const std::string all = read_all();

    // A stamped marker comes first, and counts all of the missing bytes.
    const size_t eol = all.find('\n');
    TEST_ASSERT_TRUE(eol != std::string::npos);
    const std::string marker = all.substr(0, eol + 1);
    TEST_ASSERT_TRUE(marker.starts_with("7."));
    constexpr std::string_view label = " - W persistent_log: dropped ";
    const size_t at = marker.find(label);
    TEST_ASSERT_TRUE(at != std::string::npos);
    TEST_ASSERT_TRUE(marker.ends_with(" bytes\n"));
    const size_t dropped = std::strtoul(marker.c_str() + at + label.size(), nullptr, 10);
    TEST_ASSERT_EQUAL(appended, dropped + all.size() - marker.size());

    // Only complete lines follow, up to the last line written.
    std::string_view rest = std::string_view(all).substr(marker.size());
    TEST_ASSERT_FALSE(rest.empty());
    while (!rest.empty()) {
        const size_t end = rest.find('\n');
        TEST_ASSERT_TRUE(end != std::string_view::npos);
        const std::string_view line = rest.substr(0, end);
        TEST_ASSERT_TRUE(line.starts_with("7."));
        TEST_ASSERT_TRUE(contains(line, " - pl overrun "));
        TEST_ASSERT_TRUE(line.ends_with(filler));
        rest.remove_prefix(end + 1);
    }
    TEST_ASSERT_TRUE(
        all.ends_with(" - pl overrun " + std::to_string(lines - 1) + " " + filler + "\n")
    );
    stop_checked();
}

TEST_CASE(
    "persistent_log leaves out the lines that continue an entry an overrun cut",
    "[persistent_log]"
) {
    // Write entries of a line and its continuations, more than the ring holds. The overrun cuts
    // the oldest entry left in the middle of a line that a continuation line follows, and that
    // continuation is left out with the cut line.
    constexpr size_t most = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
    const std::string filler(40, 'o');
    std::string text = "pl cut head";
    while (text.size() + 12 + filler.size() <= most) {
        text += "\npl orphan " + filler;
    }
    start_clean(quiet_config());
    auto& ring = internal::rtc_ring();
    const uint32_t before = ring.head;
    while (appended_since(before) <= sizeof ring.data) {
        pl::append(text);
    }
    // Write short entries to move the cut along until a continuation line follows the line that
    // the cut falls in.
    const auto cut_before_continuation = [&ring] {
        const auto p = ring.pending();
        std::string s(p.first.begin(), p.first.end());
        s.append(p.second.begin(), p.second.end());
        const size_t nl = s.find('\n');
        return p.lost > 0 && nl != std::string::npos && nl + 1 < s.size() && s[nl + 1] == '\t';
    };
    std::string last = text.substr(text.rfind('\n') + 1);
    for (int shifts = 0; !cut_before_continuation() && shifts < 32; ++shifts) {
        last = "pl shift " + std::to_string(shifts);
        pl::append(last);
    }
    TEST_ASSERT_TRUE(cut_before_continuation());
    const size_t appended = appended_since(before);
    const std::string all = read_all();

    // The marker counts every byte left out, including the continuation lines. Complete entries
    // follow it.
    constexpr std::string_view label = " - W persistent_log: dropped ";
    const size_t eol = all.find('\n');
    TEST_ASSERT_TRUE(eol != std::string::npos);
    const std::string marker = all.substr(0, eol + 1);
    const size_t at = marker.find(label);
    TEST_ASSERT_TRUE(at != std::string::npos);
    const size_t dropped = std::strtoul(marker.c_str() + at + label.size(), nullptr, 10);
    const std::string rest = all.substr(marker.size());
    TEST_ASSERT_TRUE(rest.starts_with("7."));
    const std::string first = rest.substr(0, rest.find('\n'));
    TEST_ASSERT_TRUE(first.ends_with(" - pl cut head") || contains(first, " - pl shift "));
    TEST_ASSERT_TRUE(rest.ends_with(last + "\n"));
    TEST_ASSERT_EQUAL(appended, dropped + rest.size());
    stop_checked();
}

TEST_CASE("persistent_log start resets a ring that isn't valid", "[persistent_log]") {
    start_clean(quiet_config());
    pl::append("pl before garbage");
    stop_checked();
    // Set the ring as a power-on can leave it, with the magic right by chance and the indices
    // spanning more than the ring.
    auto& ring = internal::rtc_ring();
    std::memset(ring.data, 'g', sizeof ring.data);
    ring.flushed = ring.begin;
    ring.head = ring_index(ring.begin, sizeof ring.data + 1);
    TEST_ASSERT_TRUE(pl::try_start(quiet_config()).has_value());
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    pl::append("pl after garbage");
    const std::string all = read_all();
    TEST_ASSERT_EQUAL(1, occurrences(all, "pl before garbage\n"));
    TEST_ASSERT_TRUE(all.ends_with("pl after garbage\n"));
    TEST_ASSERT_FALSE(contains(all, "ggg"));
    stop_checked();
}

TEST_CASE("persistent_log flush resets a ring that isn't valid", "[persistent_log]") {
    start_clean(quiet_config());
    pl::append("pl flushed once");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    // The ring holds a byte more than it can while the log runs.
    auto& ring = internal::rtc_ring();
    ring.flushed = ring.begin;
    ring.head = ring_index(ring.begin, sizeof ring.data + 1);
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    TEST_ASSERT_TRUE(ring.valid());
    TEST_ASSERT_TRUE(ring_text().empty());
    // What it held is counted as lost, up to the ring's size.
    TEST_ASSERT_EQUAL_UINT32(sizeof ring.data, ring.dropped);
    pl::append("pl after reset");
    const std::string all = read_all();
    TEST_ASSERT_TRUE(contains(
        all, " - W persistent_log: dropped " + std::to_string(sizeof ring.data) + " bytes\n"
    ));
    TEST_ASSERT_EQUAL(1, occurrences(all, "pl flushed once\n"));
    TEST_ASSERT_TRUE(all.ends_with("pl after reset\n"));
    stop_checked();
}

TEST_CASE("persistent_log start ends a line a reset cut short", "[persistent_log]") {
    start_clean(quiet_config());
    pl::append("pl whole line");
    stop_checked();
    // A reset between a line's appends leaves its stamp and text in the ring, but not its newline.
    internal::rtc_ring().append("7.0 +1 - pl torn line");
    TEST_ASSERT_TRUE(pl::try_start(quiet_config()).has_value());
    pl::append("pl next line");
    const std::string all = read_all();
    TEST_ASSERT_TRUE(contains(all, "pl whole line\n7.0 +1 - pl torn line\n"));
    const std::string next = line_with(all, "pl next line");
    TEST_ASSERT_TRUE(next.starts_with("7."));
    TEST_ASSERT_FALSE(contains(next, "torn"));
    stop_checked();
}

TEST_CASE("persistent_log checks each RTC record on its own", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    // Start once, so that start has counted this boot's wake and leaves the records to the test.
    // Write a line to flash, so that the hint holds a position.
    TEST_ASSERT_TRUE(pl::try_start(quiet_config()).has_value());
    pl::append("pl before the records");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    stop_checked();
    auto& rtc = internal::rtc();
    const internal::hint_record hint = rtc.hint;
    TEST_ASSERT_TRUE(internal::valid(hint));
    TEST_ASSERT_EQUAL_UINT32(1, hint.has_hint);
    internal::check_rtc(false);
    internal::check_rtc(true);
    internal::check_rtc(true);
    TEST_ASSERT_EQUAL_UINT32(2, rtc.wake.wake);
    rtc.faults.has_core_dump_crc = 1;
    rtc.faults.core_dump_crc = 0x12345678;
    internal::seal(rtc.faults);

    // A torn hint is dropped. The wake count continues from where it was, and the fault words
    // stay.
    rtc.hint.seq ^= 1;
    internal::check_rtc(true);
    TEST_ASSERT_TRUE(internal::valid(rtc.hint));
    TEST_ASSERT_EQUAL_UINT32(0, rtc.hint.has_hint);
    TEST_ASSERT_EQUAL_UINT32(3, rtc.wake.wake);
    TEST_ASSERT_EQUAL_UINT32(1, rtc.faults.has_core_dump_crc);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, rtc.faults.core_dump_crc);
    // Start finds the log without the hint, takes the wake count, and keeps a new hint.
    TEST_ASSERT_TRUE(pl::try_start(quiet_config()).has_value());
    pl::append("pl after a torn hint");
    TEST_ASSERT_TRUE(line_with(ring_text(), "pl after a torn hint").starts_with("7.3 "));
    stop_checked();
    TEST_ASSERT_TRUE(internal::valid(rtc.hint));
    TEST_ASSERT_EQUAL_UINT32(1, rtc.hint.has_hint);

    // A torn wake count starts again at 0, and the hint and the fault words stay.
    const internal::hint_record kept = rtc.hint;
    rtc.wake.wake ^= 0x100;
    internal::check_rtc(true);
    TEST_ASSERT_EQUAL_UINT32(0, rtc.wake.wake);
    TEST_ASSERT_EQUAL_MEMORY(&kept, &rtc.hint, sizeof kept);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, rtc.faults.core_dump_crc);

    // Torn fault words are zeroed, and the hint and the wake count stay.
    rtc.faults.core_dump_crc ^= 1;
    internal::check_rtc(true);
    TEST_ASSERT_TRUE(internal::valid(rtc.faults));
    TEST_ASSERT_EQUAL_UINT32(0, rtc.faults.has_core_dump_crc);
    TEST_ASSERT_EQUAL_UINT32(0, rtc.faults.core_dump_crc);
    TEST_ASSERT_EQUAL_UINT32(1, rtc.wake.wake);
    TEST_ASSERT_EQUAL_MEMORY(&kept, &rtc.hint, sizeof kept);

    // Leave the records as a boot other than a wake leaves them.
    internal::check_rtc(false);
}

TEST_CASE("persistent_log runs without fault slots", "[persistent_log]") {
    auto c = test_config();
    c.faults = std::nullopt;
    start_clean(std::move(c));
    pl::append("pl whole partition");
    TEST_ASSERT_TRUE(contains(read_all(), "pl whole partition\n"));
    stop_checked();
}
