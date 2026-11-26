// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "entry_buffer.hpp"
#include "idfxx/persistent_log.hpp"
#include "log_line.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace idfxx::persistent_log {

namespace {

using namespace std::chrono;

// Reads the stamp at the start of a line, field by field. Each read returns false if the field is
// missing or not valid.
class stamp_parser {
public:
    explicit stamp_parser(std::string_view s)
        : _s(s) {}

    // One or more decimal digits, with a value that fits in T.
    template<typename T>
    bool number(T& out) {
        if (_at >= _s.size() || _s[_at] < '0' || _s[_at] > '9') {
            return false;
        }
        const char* begin = _s.data() + _at;
        const auto r = std::from_chars(begin, _s.data() + _s.size(), out);
        if (r.ec != std::errc{}) {
            return false;
        }
        _at += static_cast<size_t>(r.ptr - begin);
        return true;
    }

    // Exactly `width` decimal digits.
    bool digits(int width, unsigned& out) {
        out = 0;
        for (int i = 0; i < width; ++i) {
            if (_at >= _s.size() || _s[_at] < '0' || _s[_at] > '9') {
                return false;
            }
            out = out * 10 + static_cast<unsigned>(_s[_at++] - '0');
        }
        return true;
    }

    bool literal(char c) {
        if (_at >= _s.size() || _s[_at] != c) {
            return false;
        }
        ++_at;
        return true;
    }

    // The wall-clock time as format_prefix() writes it (`2026-11-20T10:31:02Z`), or `-`.
    bool wall(std::optional<sys_seconds>& out) {
        if (literal('-')) {
            out.reset();
            return true;
        }
        unsigned y, mo, d, h, mi, s;
        if (!digits(4, y) || !literal('-') || !digits(2, mo) || !literal('-') || !digits(2, d) ||
            !literal('T') || !digits(2, h) || !literal(':') || !digits(2, mi) || !literal(':') ||
            !digits(2, s) || !literal('Z')) {
            return false;
        }
        const year_month_day ymd{year{static_cast<int>(y)}, month{mo}, day{d}};
        if (!ymd.ok() || h > 23 || mi > 59 || s > 59) {
            return false;
        }
        const sys_seconds secs = sys_days{ymd} + hours{h} + minutes{mi} + seconds{s};
        // A time that the system clock can't hold is not valid.
        using clock_duration = system_clock::duration;
        if (secs.time_since_epoch() > duration_cast<seconds>(clock_duration::max()) ||
            secs.time_since_epoch() < duration_cast<seconds>(clock_duration::min())) {
            return false;
        }
        out = secs;
        return true;
    }

    [[nodiscard]] size_t at() const { return _at; }

private:
    std::string_view _s;
    size_t _at = 0;
};

// Where the entry at the start of some text ends. The text doesn't start with an empty line.
struct entry_end {
    // The size of the entry's stored text, without its final newline.
    size_t stored = 0;
    // The number of bytes that the entry takes, with its final newline.
    size_t taken = 0;
    // The text holds the whole entry. Otherwise, `stored` and `taken` cover the whole lines at the
    // start of the entry, and are 0 when there are none.
    bool whole = false;
};

// Finds the end of the entry at the start of `text`. The entry ends at the first newline that
// isn't followed by a tab. When `final` is true, the text ends where the source ends, so the entry
// also ends at a newline at the end of the text, or at the end of the text.
entry_end find_entry_end(std::string_view text, bool final) {
    size_t from = 0;
    for (;;) {
        const size_t nl = text.find('\n', from);
        if (nl == std::string_view::npos) {
            if (final) {
                return {.stored = text.size(), .taken = text.size(), .whole = true};
            }
            return {.stored = from == 0 ? 0 : from - 1, .taken = from};
        }
        if (nl + 1 == text.size()) {
            // Whether the next line continues the entry isn't known until its first byte is.
            return {.stored = nl, .taken = nl + 1, .whole = final};
        }
        if (text[nl + 1] != '\t') {
            return {.stored = nl, .taken = nl + 1, .whole = true};
        }
        from = nl + 1;
    }
}

} // namespace

entry detail::entry_access::parsed(std::string_view stored) noexcept {
    entry e;
    e._stored = stored;
    stamp_parser p(stored);
    uint32_t boot = 0;
    uint32_t wake = 0;
    milliseconds::rep ms = 0;
    std::optional<sys_seconds> wall;
    // A stamp longer than format_prefix() writes, such as one with leading zeros in damaged text,
    // is not valid.
    if (p.number(boot) && p.literal('.') && p.number(wake) && p.literal(' ') && p.literal('+') &&
        p.number(ms) && p.literal(' ') && p.wall(wall) && p.literal(' ') &&
        p.at() <= max_prefix_size) {
        e._boot = boot;
        e._wake = wake;
        e._uptime = milliseconds{ms};
        e._wall = wall;
        e._stamp_size = static_cast<uint8_t>(p.at());
    }
    return e;
}

bool next_entry(std::string_view& text, entry& out) noexcept {
    const size_t first = text.find_first_not_of('\n');
    if (first == std::string_view::npos) {
        text = {};
        return false;
    }
    text.remove_prefix(first);
    const entry_end e = find_entry_end(text, true);
    out = detail::entry_access::parsed(text.substr(0, e.stored));
    text.remove_prefix(e.taken);
    return true;
}

bool entry_buffer::_take(entry& out) noexcept {
    char* const data = _data.get();
    if (_in_line) {
        // The rest of a line that didn't fit in the buffer. Each piece of it is an entry that isn't
        // stamped.
        const std::string_view text(data + _begin, _end - _begin);
        const size_t nl = text.find('\n');
        if (nl == std::string_view::npos) {
            if (text.empty() || (!_done && text.size() < _size)) {
                return false;
            }
            out = detail::entry_access::unstamped(text);
            _begin = _end;
            _in_line = !_done;
            return true;
        }
        _in_line = false;
        _begin += nl + 1;
        if (nl > 0) {
            out = detail::entry_access::unstamped(text.substr(0, nl));
            return true;
        }
    }
    // Empty lines are skipped.
    while (_begin < _end && data[_begin] == '\n') {
        ++_begin;
    }
    const std::string_view text(data + _begin, _end - _begin);
    if (text.empty()) {
        return false;
    }
    const bool full = text.size() == _size;
    const entry_end e = find_entry_end(text, _done);
    if (!e.whole && !full) {
        return false;
    }
    if (e.whole || e.taken > 0) {
        // A whole entry, or the whole lines of an entry that doesn't fit in the buffer.
        out = detail::entry_access::parsed(text.substr(0, e.stored));
        _begin += e.taken;
        return true;
    }
    // A line that doesn't fit in the buffer.
    out = detail::entry_access::unstamped(text);
    _begin = _end;
    _in_line = true;
    return true;
}

} // namespace idfxx::persistent_log
