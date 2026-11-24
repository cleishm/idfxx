// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>

#include <cstddef>
#include <cstdio>
#include <deque>
#include <esp_log.h>
#include <mutex>
#include <string>
#include <string_view>

// Stores the most recent warnings and errors, so that they can be read after a problem occurs,
// even if the console did not print them.
class recent_problems final : public idfxx::log::sink {
public:
    // Prints the stored lines, oldest first.
    void print() const {
        std::lock_guard lock(_mutex);
        for (const entry& e : _lines) {
            std::printf(
                "%-5s %s: %s\n", idfxx::to_string(e.lvl).c_str(), e.tag.c_str(), e.message.c_str()
            );
        }
    }

private:
    static constexpr size_t kept = 8;

    struct entry {
        idfxx::log::level lvl;
        std::string tag;
        std::string message;
    };

    bool do_wants(idfxx::log::level lvl, std::string_view) const noexcept override {
        return lvl == idfxx::log::level::error || lvl == idfxx::log::level::warn;
    }

    // The tag and message are valid only during the call, so they are copied.
    void do_write(
        idfxx::log::level lvl,
        std::string_view tag,
        std::string_view message
    ) noexcept override {
        // This is called from the task that logged the line. Several tasks can call it at the
        // same time.
        std::lock_guard lock(_mutex);
        if (_lines.size() == kept) {
            _lines.pop_front();
        }
        _lines.push_back({lvl, std::string(tag), std::string(message)});
    }

    mutable std::mutex _mutex;
    std::deque<entry> _lines;
};

static recent_problems problems;

// An example of a library that logs through ESP-IDF's log directly, with its own tag.
static void another_library_retry() {
    ESP_LOGW("other_lib", "Retrying after a timeout (attempt %d)", 2);
}

extern "C" void app_main() {
    // Add the sink for every log line, so that it also stores warnings and errors from libraries
    // that don't log through idfxx::log.
    idfxx::log::add_sink(idfxx::log::all_lines, problems);

    // Limit the console to errors for this tag. The sink still receives the tag's warnings,
    // because a sink's choice of lines is independent of the console's log levels.
    idfxx::log::set_level("sensor", idfxx::log::level::error);

    constexpr idfxx::log::logger logger{"example"};
    logger.info("Starting up");
    logger.warn("Battery at {}%", 15);
    idfxx::log::warn("sensor", "Reading took {} ms", 120);
    another_library_retry();
    logger.error("Upload failed: {}", "no network");

    std::printf("Recent problems:\n");
    problems.print();

    // After this returns, no task is inside the sink, so the sink could be destroyed safely.
    idfxx::log::remove_sink(problems);
}
