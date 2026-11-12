// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/utf8>
 * @file utf8.hpp
 * @brief UTF-8 decoding.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_utf8 UTF-8 Decoding
 * @ingroup idfxx_core
 * @brief Iterating UTF-8 text as Unicode code points.
 *
 * Provides @ref idfxx::utf8_view, a range over the code points of UTF-8
 * text that replaces malformed input with U+FFFD rather than failing.
 * @{
 */

#include <cstddef>
#include <iterator>
#include <ranges>
#include <string_view>

namespace idfxx {

/**
 * @headerfile <idfxx/utf8>
 * @brief A view of UTF-8 text as a sequence of Unicode code points.
 *
 * Iterating yields one `char32_t` per encoded character. Malformed input
 * never stops the iteration: each maximal ill-formed subsequence (a stray
 * continuation byte, a truncated sequence, an overlong or surrogate
 * encoding, or a value above U+10FFFF) yields one U+FFFD REPLACEMENT
 * CHARACTER, following the Unicode recommended practice, and decoding
 * resumes at the next byte that could start a character.
 *
 * @code
 * size_t count = 0;
 * for (char32_t cp : idfxx::utf8_view("café")) {
 *     ++count; // 4 code points from 5 bytes
 * }
 * @endcode
 *
 * The view does not own the text; the caller must keep it alive while
 * iterating. Iterators refer to the text directly, not to the view, so
 * they remain valid after the view is destroyed (the view is a borrowed
 * range).
 */
class utf8_view : public std::ranges::view_interface<utf8_view> {
public:
    /** @brief The code point yielded for malformed input. */
    static constexpr char32_t replacement_character = U'\uFFFD';

    /** @brief Forward iterator over the decoded code points. */
    class iterator {
    public:
        /** @brief The decoded value type. */
        using value_type = char32_t;
        /** @brief Distance type (required by the iterator concepts). */
        using difference_type = std::ptrdiff_t;

        /** @brief Creates an end-of-text iterator. */
        constexpr iterator() noexcept = default;

        /** @brief Returns the code point at the current position. */
        [[nodiscard]] constexpr char32_t operator*() const noexcept { return _cp; }

        /** @brief Advances to the next code point. */
        constexpr iterator& operator++() noexcept {
            _pos += _len;
            _decode();
            return *this;
        }

        /** @brief Advances to the next code point, returning the previous position. */
        constexpr iterator operator++(int) noexcept {
            iterator prev = *this;
            ++*this;
            return prev;
        }

        /** @brief Returns the byte offset of the current code point within the text. */
        [[nodiscard]] constexpr size_t offset() const noexcept { return _pos; }

        /** @brief Returns true if both iterators are at the same position in the same text. */
        [[nodiscard]] constexpr bool operator==(const iterator& other) const noexcept {
            return _text.data() == other._text.data() && _pos == other._pos;
        }

        /** @brief Returns true if the iterator has reached the end of the text. */
        [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const noexcept {
            return _pos >= _text.size();
        }

    private:
        friend class utf8_view;

        constexpr explicit iterator(std::string_view text) noexcept
            : _text(text) {
            _decode();
        }

        [[nodiscard]] constexpr unsigned _byte(size_t i) const noexcept {
            return static_cast<unsigned char>(_text[i]);
        }

        // Decodes the character at _pos into _cp and _len.
        constexpr void _decode() noexcept {
            if (_pos >= _text.size()) {
                _cp = 0;
                _len = 0;
                return;
            }
            const unsigned lead = _byte(_pos);
            if (lead < 0x80) {
                _cp = lead;
                _len = 1;
                return;
            }
            // Expected length, initial bits, and the valid range of the second byte
            // (narrowed for leads that would allow overlong, surrogate or >U+10FFFF
            // encodings).
            size_t need;
            char32_t cp;
            unsigned lo = 0x80;
            unsigned hi = 0xBF;
            if (lead >= 0xC2 && lead <= 0xDF) {
                need = 2;
                cp = lead & 0x1Fu;
            } else if (lead >= 0xE0 && lead <= 0xEF) {
                need = 3;
                cp = lead & 0x0Fu;
                if (lead == 0xE0) {
                    lo = 0xA0;
                } else if (lead == 0xED) {
                    hi = 0x9F;
                }
            } else if (lead >= 0xF0 && lead <= 0xF4) {
                need = 4;
                cp = lead & 0x07u;
                if (lead == 0xF0) {
                    lo = 0x90;
                } else if (lead == 0xF4) {
                    hi = 0x8F;
                }
            } else {
                _cp = replacement_character;
                _len = 1;
                return;
            }
            size_t len = 1;
            while (len < need && _pos + len < _text.size()) {
                const unsigned b = _byte(_pos + len);
                if (b < lo || b > hi) {
                    break;
                }
                cp = (cp << 6) | (b & 0x3Fu);
                lo = 0x80;
                hi = 0xBF;
                ++len;
            }
            _cp = len == need ? cp : replacement_character;
            _len = len;
        }

        std::string_view _text;
        size_t _pos = 0;
        size_t _len = 0;
        char32_t _cp = 0;
    };

    /** @brief Creates an empty view. */
    constexpr utf8_view() noexcept = default;

    /**
     * @brief Creates a view decoding @p text.
     * @param text UTF-8 encoded text; must outlive the view and its iterators.
     */
    constexpr explicit utf8_view(std::string_view text) noexcept
        : _text(text) {}

    /** @brief Returns an iterator to the first code point. */
    [[nodiscard]] constexpr iterator begin() const noexcept { return iterator(_text); }

    /** @brief Returns the end-of-text sentinel. */
    [[nodiscard]] constexpr std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

private:
    std::string_view _text;
};

} // namespace idfxx

/** @cond INTERNAL */
template<>
inline constexpr bool std::ranges::enable_borrowed_range<idfxx::utf8_view> = true;
/** @endcond */

/** @} */ // end of idfxx_core_utf8
/** @} */ // end of idfxx_core
