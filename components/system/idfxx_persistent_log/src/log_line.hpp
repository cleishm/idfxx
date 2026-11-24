// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @file log_line.hpp
 * @brief The stamp on each stored log line.
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace idfxx::persistent_log {

/// The maximum number of bytes that @ref format_prefix writes.
inline constexpr size_t max_prefix_size = 72;

/**
 * @brief Writes the stamp that goes in front of a stored line.
 *
 * The stamp is `<boot>.<wake> +<ms> <wall> `, with a space at the end. It holds the boot and wake
 * counts, the milliseconds since the boot or the deep-sleep wake, and the wall-clock time in UTC to
 * the second (`2026-11-20T10:31:02Z`). Without a wall-clock time, `<wall>` is `-`.
 *
 * @param out    The buffer to write to, of at least @ref max_prefix_size bytes.
 * @param boot   The boot count.
 * @param wake   The wake count within the boot.
 * @param uptime The time since the boot or the deep-sleep wake.
 * @param wall   The wall-clock time, or `std::nullopt` when the clock isn't set.
 *
 * @return The number of bytes written to the start of @p out. Returns 0 when @p out is smaller than
 *         @ref max_prefix_size, or when the wall-clock time is outside the years 0 to 9999.
 */
[[nodiscard]] size_t format_prefix(
    std::span<char> out,
    uint32_t boot,
    uint32_t wake,
    std::chrono::milliseconds uptime,
    std::optional<std::chrono::system_clock::time_point> wall
);

/** @cond INTERNAL */
namespace detail {

// Writes a wall-clock time in UTC to the second, as `2026-11-20T10:31:02Z`, through `w`. The
// writer has `put(char)` and `padded(unsigned value, int width)`, which writes `value` as exactly
// `width` digits. Returns false, and writes nothing, when the year is outside 0 to 9999. It is
// always inlined, so that it adds no stack frame to the functions that write stamps.
template<typename Writer>
[[nodiscard, gnu::always_inline]] inline bool
put_utc(Writer& w, std::chrono::system_clock::time_point wall) {
    using namespace std::chrono;
    const auto secs = floor<seconds>(wall);
    const auto day = floor<days>(secs);
    const year_month_day ymd{day};
    const hh_mm_ss hms{secs - day};
    const int y = static_cast<int>(ymd.year());
    if (y < 0 || y > 9999) {
        return false;
    }
    w.padded(static_cast<unsigned>(y), 4);
    w.put('-');
    w.padded(static_cast<unsigned>(ymd.month()), 2);
    w.put('-');
    w.padded(static_cast<unsigned>(ymd.day()), 2);
    w.put('T');
    w.padded(static_cast<unsigned>(hms.hours().count()), 2);
    w.put(':');
    w.padded(static_cast<unsigned>(hms.minutes().count()), 2);
    w.put(':');
    w.padded(static_cast<unsigned>(hms.seconds().count()), 2);
    w.put('Z');
    return true;
}

// Returns the offset where the first entry between `from` and `end` begins. That is the first line
// start in that range whose first byte isn't a tab, or `end` if there is none. `at(i)` returns the
// byte at `i`. When `at_line` is true, `from` is treated as the start of a line. Otherwise, the
// function first skips the rest of the line that contains `from`.
template<typename At>
[[nodiscard]] constexpr size_t entry_start(At&& at, size_t from, size_t end, bool at_line) {
    while (from < end) {
        if (at_line && at(from) != '\t') {
            return from;
        }
        while (from < end && at(from) != '\n') {
            ++from;
        }
        if (from < end) {
            ++from;
        }
        at_line = true;
    }
    return end;
}

// Counts the entries in text that is passed in pieces. It splits the text into entries as
// next_entry() does, and takes the text to begin at the start of a line. An entry begins at each
// line that isn't empty and doesn't start with a tab. An entry also begins at a line that starts
// with a tab when no entry is open: at the start of the text, or after an empty line. An empty
// line ends the entry before it.
class entry_counter {
public:
    // Passes the next piece of the text. If entry number `wanted` (counting from 0) begins in
    // `piece`, returns its offset in `piece`, and counts only the entries before it. Otherwise,
    // counts the entries that begin in `piece` and returns `std::string_view::npos`.
    constexpr size_t add(std::string_view piece, size_t wanted = SIZE_MAX) noexcept {
        for (size_t i = 0; i < piece.size(); ++i) {
            const char c = piece[i];
            if (_at_line) {
                if (c == '\n') {
                    _in_entry = false;
                } else if (c != '\t' || !_in_entry) {
                    if (_count == wanted) {
                        return i;
                    }
                    ++_count;
                    _in_entry = true;
                }
            }
            _at_line = c == '\n';
        }
        return std::string_view::npos;
    }

    // The number of entries that have begun so far.
    [[nodiscard]] constexpr size_t count() const noexcept { return _count; }

private:
    size_t _count = 0;
    bool _at_line = true;
    bool _in_entry = false;
};

// Returns the first `size` bytes of `s`, or all of `s` when it is shorter, without an incomplete
// UTF-8 character at the end. A character is incomplete when its lead byte is among the last three
// bytes and starts a sequence longer than the bytes from the lead byte to the end. The function
// also trims an incomplete character at the end of `s` itself, as in text that was shortened before
// it was passed here. It checks no more than the last three bytes, so it leaves other malformed
// text unchanged.
[[nodiscard]] constexpr std::string_view utf8_prefix(std::string_view s, size_t size) noexcept {
    s = s.substr(0, size < s.size() ? size : s.size());
    for (size_t back = 1; back <= 3 && back <= s.size(); ++back) {
        const auto c = static_cast<unsigned char>(s[s.size() - back]);
        if ((c & 0xC0) == 0x80) {
            continue;
        }
        const size_t length = (c & 0xE0) == 0xC0 ? 2
            : (c & 0xF0) == 0xE0                 ? 3
            : (c & 0xF8) == 0xF0                 ? 4
                                                 : 1;
        return length > back ? s.substr(0, s.size() - back) : s;
    }
    return s;
}

} // namespace detail
/** @endcond */

} // namespace idfxx::persistent_log
