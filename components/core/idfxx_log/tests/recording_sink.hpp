// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// A sink that records the lines it receives, and cleanup for the tests that add sinks.

#pragma once

#include "sdkconfig.h"
#include "test_cleanup.hpp"

#include <idfxx/log>

#include <cstddef>
#include <esp_log.h>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sink_test {

using idfxx::log::level;

struct line {
    level lvl;
    std::string tag;
    std::string message;
};

// Records the lines at `threshold` or more severe whose tag starts with "sk_". When constructed
// with `untagged`, it also records the lines without a tag. The tag check keeps out the lines that
// other tasks log.
class recorder final : public idfxx::log::sink {
public:
    explicit recorder(level threshold = level::verbose, bool untagged = false) noexcept
        : _threshold(threshold)
        , _untagged(untagged) {}

    std::vector<line> lines() const {
        std::lock_guard lock(_mutex);
        return _lines;
    }

    size_t size() const {
        std::lock_guard lock(_mutex);
        return _lines.size();
    }

    void clear() {
        std::lock_guard lock(_mutex);
        _lines.clear();
    }

    // The number of lines received at `lvl` with `tag` and `message`.
    size_t count(level lvl, std::string_view tag, std::string_view message) const {
        std::lock_guard lock(_mutex);
        size_t n = 0;
        for (const auto& l : _lines) {
            if (l.lvl == lvl && l.tag == tag && l.message == message) {
                ++n;
            }
        }
        return n;
    }

    bool has(level lvl, std::string_view tag, std::string_view message) const {
        return count(lvl, tag, message) > 0;
    }

    // Whether any line's message contains `text`.
    bool containing(std::string_view text) const {
        std::lock_guard lock(_mutex);
        for (const auto& l : _lines) {
            if (l.message.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // The message of the newest line with `tag`, or empty if there is none.
    std::string last_message(std::string_view tag) const {
        std::lock_guard lock(_mutex);
        for (auto it = _lines.rbegin(); it != _lines.rend(); ++it) {
            if (it->tag == tag) {
                return it->message;
            }
        }
        return {};
    }

private:
    bool do_wants(level lvl, std::string_view tag) const noexcept override {
        if (lvl == level::none || lvl > _threshold) {
            return false;
        }
        return tag.starts_with("sk_") || (_untagged && tag.empty());
    }

    void do_write(level lvl, std::string_view tag, std::string_view message) noexcept override {
        std::lock_guard lock(_mutex);
        _lines.push_back({lvl, std::string(tag), std::string(message)});
    }

    level _threshold;
    bool _untagged;
    mutable std::mutex _mutex;
    std::vector<line> _lines;
};

// The tags whose console level a test changed. Their levels are restored after the test.
inline std::vector<std::string> s_changed_tags;

inline void restore_console_levels() {
    for (const auto& tag : s_changed_tags) {
        esp_log_level_set(tag.c_str(), static_cast<esp_log_level_t>(CONFIG_LOG_DEFAULT_LEVEL));
    }
    s_changed_tags.clear();
}

// Sets the console's level for `tag`, until the test ends.
inline void set_console_level(const char* tag, esp_log_level_t lvl) {
    idfxx::test::run_after_each_test(restore_console_levels);
    s_changed_tags.emplace_back(tag);
    esp_log_level_set(tag, lvl);
}

// A value that counts each time it is formatted, so that a test can tell whether idfxx::log
// formatted a line.
inline int s_formatted = 0;
struct counted {};

} // namespace sink_test

template<>
struct std::formatter<sink_test::counted> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const sink_test::counted&, FormatContext& ctx) const {
        ++sink_test::s_formatted;
        return std::format_to(ctx.out(), "counted");
    }
};
