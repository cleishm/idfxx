// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// The pure functions used to pass log lines to sinks. They find a line's tag, where its message
// starts, its end, and where to shorten it. They hold no state, so the tests can call them
// directly.

#include <cstddef>
#include <string_view>

namespace idfxx::log::detail {

// A line's tag as a sink receives it. It is empty for a line without a tag.
[[nodiscard]] constexpr std::string_view tag_view(const char* tag) noexcept {
    return tag != nullptr ? std::string_view{tag} : std::string_view{};
}

// Where the message starts in the text of a line's first call. The caller formatted that text with
// its own prefix. The message starts just past the first `<tag>:`, and past one space after it if
// there is one. In a text without `<tag>:`, all of the text is the message, starting at 0.
[[nodiscard]] constexpr size_t message_start(std::string_view text, std::string_view tag) noexcept {
    if (tag.empty()) {
        return 0;
    }
    for (size_t at = text.find(tag); at != std::string_view::npos; at = text.find(tag, at + 1)) {
        size_t end = at + tag.size();
        if (end < text.size() && text[end] == ':') {
            ++end;
            if (end < text.size() && text[end] == ' ') {
                ++end;
            }
            return end;
        }
    }
    return 0;
}

// Removes the newline that ends a complete line, and then a color reset (`ESC [0m`) before it.
[[nodiscard]] constexpr std::string_view without_line_end(std::string_view line) noexcept {
    if (line.ends_with('\n')) {
        line.remove_suffix(1);
    }
    if (line.ends_with("\x1b[0m")) {
        line.remove_suffix(4);
    }
    return line;
}

// Returns the first `size` bytes of `s` (all of `s` when it is shorter), without a UTF-8 character
// that those bytes end partway through. The lead byte of such a character is one of the last three
// bytes, and it starts a longer sequence than the bytes from it to the end. Text that already ends
// partway through a character, because it was shortened before this call, is trimmed too. The
// function looks at no more than the last three bytes, so it keeps other malformed text as it is.
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

} // namespace idfxx::log::detail
