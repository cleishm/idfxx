// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/font>
 * @file font.hpp
 * @brief Bitmap font representation, text metrics and line breaking.
 *
 * @defgroup idfxx_font Font Component
 * @brief Bitmap font models, text metrics and line breaking.
 *
 * Provides two compact font representations — fixed-cell (@ref idfxx::font::mono_font)
 * and proportional (@ref idfxx::font::proportional_font) — plus text measurement
 * and line breaking (@ref idfxx::font::break_lines).
 * Rendering lives in the @ref idfxx_gfx component; bundled fonts are
 * published as separate data packages (`idfxx_font_spleen` for fixed-cell
 * fonts, `idfxx_font_inter` for proportional ones), each font in its own
 * translation unit so fonts an application never references are dropped at
 * link time. Generators for adding more fonts are included under
 * `scripts/`: `bdf_to_c.py` (monospace BDF to @ref idfxx::font::mono_font) and
 * `ttf_to_c.py` (TrueType/OpenType to @ref idfxx::font::proportional_font).
 * @{
 */

#include <idfxx/utf8>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>

/**
 * @headerfile <idfxx/font>
 * @brief Bitmap font types, text metrics, and bundled fonts.
 */
namespace idfxx::font {

/**
 * @headerfile <idfxx/font>
 * @brief A fixed-cell monochrome bitmap font.
 *
 * Glyphs cover a contiguous character range and share one cell size. The
 * bitmap stores glyphs consecutively, each as `height` rows of
 * `ceil(width / 8)` bytes, MSB = leftmost pixel.
 */
struct mono_font {
    uint8_t width;         ///< Glyph cell width in pixels.
    uint8_t height;        ///< Glyph cell height in pixels.
    char first_char;       ///< First character in the glyph range.
    char last_char;        ///< Last character in the glyph range (inclusive).
    const uint8_t* bitmap; ///< Glyph data (see struct description for layout).

    /** @brief Returns the number of bytes in one glyph row. */
    [[nodiscard]] constexpr size_t bytes_per_row() const noexcept { return (width + 7u) / 8u; }

    /** @brief Returns the number of bytes in one glyph. */
    [[nodiscard]] constexpr size_t bytes_per_glyph() const noexcept {
        return bytes_per_row() * height;
    }

    /** @brief Returns true if the font has a glyph for @p c. */
    [[nodiscard]] constexpr bool contains(char c) const noexcept {
        return c >= first_char && c <= last_char;
    }

    /**
     * @brief Returns a pointer to the glyph bitmap for @p c.
     * @param c A character satisfying @ref contains.
     * @return The first byte of the glyph's rows.
     */
    [[nodiscard]] constexpr const uint8_t* glyph(char c) const noexcept {
        return bitmap + static_cast<size_t>(c - first_char) * bytes_per_glyph();
    }

    /**
     * @brief Returns true if the glyph for @p c has ink at (@p col, @p row).
     *
     * Per-pixel accessor decoding the glyph bitmap layout, so consumers
     * need not address the packed bytes themselves.
     *
     * @param c   A character satisfying @ref contains.
     * @param col Column within the glyph cell, in `[0, width)`.
     * @param row Row within the glyph cell, in `[0, height)`.
     * @return true if the pixel is inked.
     */
    [[nodiscard]] constexpr bool ink_at(char c, size_t col, size_t row) const noexcept {
        return glyph(c)[row * bytes_per_row() + col / 8] & (0x80u >> (col % 8));
    }

    /**
     * @brief Returns the horizontal distance the cursor moves after @p c.
     *
     * In a fixed-cell font every character advances one cell width,
     * including characters outside the glyph range (rendered blank).
     *
     * @param c The character.
     * @return The advance in pixels.
     */
    [[nodiscard]] constexpr size_t advance([[maybe_unused]] char c) const noexcept { return width; }
};

/**
 * @brief Returns the width in pixels of @p text rendered in @p font.
 *
 * Sums the advance of every character in @p text; characters outside the
 * font's glyph range advance like any other (rendered blank).
 *
 * @param font  The font.
 * @param text  The text to measure.
 * @param scale Integer magnification factor (>= 1; 0 is treated as 1).
 * @return The rendered width in pixels.
 */
[[nodiscard]] constexpr size_t
text_width(const mono_font& font, std::string_view text, unsigned scale = 1) noexcept {
    if (scale == 0) {
        scale = 1;
    }
    size_t width = 0;
    for (char c : text) {
        width += font.advance(c);
    }
    return width * scale;
}

/**
 * @headerfile <idfxx/font>
 * @brief Placement and bitmap location of one glyph in a @ref proportional_font.
 *
 * The glyph's bitmap covers only its inked bounding box, positioned
 * relative to the pen: with the pen at column `pen_x` on the baseline row
 * `baseline_y`, the bitmap's top-left pixel lands at
 * (`pen_x + x_offset`, `baseline_y + y_offset`). After drawing, the pen
 * moves right by `advance`.
 *
 * The bitmap stores `height` rows of `ceil(width / 8)` bytes, MSB = leftmost
 * pixel, starting `bitmap_offset` bytes into the font's bitmap. A glyph with
 * no ink (e.g. a space) has zero width and height.
 */
struct glyph_metrics {
    /// Byte offset of the glyph's first row within the font's bitmap.
    uint32_t bitmap_offset;
    /// Bitmap width in pixels.
    uint16_t width;
    /// Bitmap height in pixels.
    uint16_t height;
    /// Distance the pen moves right after this glyph, in pixels.
    uint16_t advance;
    /// Columns from the pen position to the bitmap's left edge (negative = left of the pen).
    int16_t x_offset;
    /// Rows from the baseline to the bitmap's top edge (negative = above the baseline).
    int16_t y_offset;

    /** @brief Returns the number of bytes in one bitmap row. */
    [[nodiscard]] constexpr size_t bytes_per_row() const noexcept { return (width + 7u) / 8u; }
};

/**
 * @headerfile <idfxx/font>
 * @brief A run of consecutive code points with consecutive glyphs.
 *
 * Code points `first` through `last` (inclusive) map to the glyphs starting
 * at index `first_glyph` of the font's glyph table, in order.
 */
struct glyph_range {
    char32_t first;       ///< First code point in the run.
    char32_t last;        ///< Last code point in the run (inclusive).
    uint32_t first_glyph; ///< Glyph table index of the glyph for @ref first.
};

/**
 * @headerfile <idfxx/font>
 * @brief A proportional monochrome bitmap font.
 *
 * Each glyph has its own advance and a bitmap trimmed to its inked bounding
 * box, so text sets naturally ("il" narrower than "mw") and large sizes
 * stay compact. Glyphs are rendered at one fixed size: a font is produced
 * at the pixel size it will be drawn at, typically by rasterizing an
 * outline font (TrueType/OpenType) with the `ttf_to_c.py` generator under
 * `scripts/`. Coverage is any set of Unicode code points, described by
 * @ref ranges.
 *
 * Text is laid out on lines: a line's top edge sits @ref ascent rows above
 * its baseline and its bottom edge @ref descent rows below; consecutive
 * lines are @ref line_height rows apart. Ink may reach outside this line
 * box — accents on capitals above its top edge, a `j` left of the pen — so
 * the font also bounds its ink: with the pen at column `pen_x` on baseline
 * row `baseline_y`, every glyph's ink lies within rows
 * [`baseline_y - ink_ascent`, `baseline_y + ink_descent`) and columns
 * [`pen_x + min_left_bearing`, `pen_x + advance - min_right_bearing`).
 * Reserve `ink_ascent - ascent` rows above a line (when positive) to keep
 * every glyph's ink inside the area being drawn, and likewise for the
 * other edges. The bounds cover every glyph, including the
 * @ref replacement; they need not be tight but are never too small, so
 * renderers may skip text whose ink box misses the area being drawn.
 *
 * Code points the font has no glyph for are drawn and measured as the
 * @ref replacement glyph, when the font has one; otherwise they are
 * skipped entirely. In text — @ref text_width, @ref break_lines and
 * `idfxx::gfx::draw_text` — U+00AD SOFT HYPHEN is always invisible and
 * zero-width, whatever glyph @ref find, @ref resolve and @ref advance
 * report for it.
 */
struct proportional_font {
    /// Rows from the top of a line to its baseline.
    uint16_t ascent;
    /// Rows from the baseline to the bottom of a line.
    uint16_t descent;
    /// Rows from one line's baseline to the next (ascent + descent + line gap).
    uint16_t line_height;
    /// Most rows any glyph's ink reaches above the baseline.
    uint16_t ink_ascent;
    /// Most rows any glyph's ink reaches below the baseline (the baseline row counts as below).
    uint16_t ink_descent;
    /// Smallest columns from the pen to any glyph's ink (negative = ink left of the pen).
    int16_t min_left_bearing;
    /// Smallest columns from any glyph's ink to its advance (negative = ink past the advance).
    int16_t min_right_bearing;
    /// Covered code points, sorted ascending and non-overlapping.
    std::span<const glyph_range> ranges;
    /// Glyph table indexed by the ranges.
    std::span<const glyph_metrics> glyphs;
    /// Glyph used for code points without a glyph, or null to skip them.
    const glyph_metrics* replacement;
    /// Packed glyph bitmaps (see @ref glyph_metrics for the layout).
    const uint8_t* bitmap;

    /**
     * @brief Returns the font's own glyph for @p cp.
     * @param cp The code point.
     * @return The glyph, or null if the font does not cover @p cp.
     */
    [[nodiscard]] constexpr const glyph_metrics* find(char32_t cp) const noexcept {
        const auto it = std::ranges::lower_bound(ranges, cp, {}, &glyph_range::last);
        if (it == ranges.end() || cp < it->first) {
            return nullptr;
        }
        return &glyphs[it->first_glyph + (cp - it->first)];
    }

    /** @brief Returns true if the font has its own glyph for @p cp. */
    [[nodiscard]] constexpr bool contains(char32_t cp) const noexcept {
        return find(cp) != nullptr;
    }

    /**
     * @brief Returns the glyph drawn for @p cp.
     * @param cp The code point.
     * @return The font's glyph for @p cp, else the @ref replacement glyph
     *         (null if the font has none, meaning @p cp is skipped).
     */
    [[nodiscard]] constexpr const glyph_metrics* resolve(char32_t cp) const noexcept {
        const glyph_metrics* g = find(cp);
        return g != nullptr ? g : replacement;
    }

    /**
     * @brief Returns the horizontal distance the pen moves after @p cp.
     * @param cp The code point.
     * @return The advance of the glyph drawn for @p cp (see @ref resolve),
     *         or 0 if it is skipped. Text measurement ignores this for
     *         U+00AD SOFT HYPHEN, which always takes no space in text.
     */
    [[nodiscard]] constexpr size_t advance(char32_t cp) const noexcept {
        const glyph_metrics* g = resolve(cp);
        return g != nullptr ? g->advance : 0;
    }

    /**
     * @brief Returns a pointer to the bitmap rows of @p g.
     * @param g A glyph of this font.
     * @return The first byte of the glyph's rows.
     */
    [[nodiscard]] constexpr const uint8_t* glyph_bitmap(const glyph_metrics& g) const noexcept {
        return bitmap + g.bitmap_offset;
    }

    /**
     * @brief Returns true if @p g has ink at (@p col, @p row) of its bitmap.
     * @param g   A glyph of this font.
     * @param col Column within the glyph bitmap, in `[0, g.width)`.
     * @param row Row within the glyph bitmap, in `[0, g.height)`.
     * @return true if the pixel is inked.
     */
    [[nodiscard]] constexpr bool
    ink_at(const glyph_metrics& g, size_t col, size_t row) const noexcept {
        return glyph_bitmap(g)[row * g.bytes_per_row() + col / 8] & (0x80u >> (col % 8));
    }
};

/** @cond INTERNAL */
namespace detail {

// U+00AD SOFT HYPHEN: invisible and zero-width in text, a hyphenation point
// for line breaking.
inline constexpr char32_t soft_hyphen = U'\u00AD';

// The glyph drawn for cp within text: its resolved glyph, or null for a soft
// hyphen, which text never draws.
[[nodiscard]] constexpr const glyph_metrics*
text_glyph(const proportional_font& font, char32_t cp) noexcept {
    return cp == soft_hyphen ? nullptr : font.resolve(cp);
}

// The pen advance of cp within text: its glyph's advance, or 0 if text skips it.
[[nodiscard]] constexpr size_t text_advance(const proportional_font& font, char32_t cp) noexcept {
    const glyph_metrics* g = text_glyph(font, cp);
    return g != nullptr ? g->advance : 0;
}

} // namespace detail
/** @endcond */

/**
 * @brief Returns the width in pixels of @p text rendered in @p font.
 *
 * Decodes @p text as UTF-8 (see @ref idfxx::utf8_view) and sums the advance of
 * every code point (see @ref proportional_font::advance), except that U+00AD
 * SOFT HYPHEN is always invisible and takes no space, even when the font
 * has a glyph or a replacement for it (it only marks where @ref break_lines
 * may hyphenate a word). This is the distance the pen travels, the width to
 * use when centering, aligning or wrapping text; a glyph's ink may overhang
 * it slightly at either end.
 *
 * @param font The font.
 * @param text The UTF-8 text to measure.
 * @return The rendered width in pixels.
 */
[[nodiscard]] constexpr size_t
text_width(const proportional_font& font, std::string_view text) noexcept {
    size_t width = 0;
    for (char32_t cp : utf8_view(text)) {
        width += detail::text_advance(font, cp);
    }
    return width;
}

/**
 * @headerfile <idfxx/font>
 * @brief One line of text produced by @ref break_lines.
 *
 * A line is a slice of the text passed to @ref break_lines, ready to draw
 * with `idfxx::gfx::draw_text` at the line's position. When @ref hyphenated
 * is set, a `'-'` must be drawn immediately after @ref text (at
 * `text_width(font, text)` from the line's left edge); `idfxx::gfx::draw_text_box`
 * does this automatically.
 */
struct text_line {
    /// The line's text: a slice of the input, excluding the spaces at a wrap
    /// point, trailing spaces, the line break (`'\n'` or `"\r\n"`) and a soft
    /// hyphen at the break.
    std::string_view text;
    /// Width of the line in pixels, including the drawn hyphen when @ref hyphenated.
    size_t width;
    /// True if the line was broken at a soft hyphen (U+00AD): a `'-'` is drawn after @ref text.
    bool hyphenated;

    /** @brief Returns true if both lines have the same text, width and hyphenation. */
    [[nodiscard]] constexpr bool operator==(const text_line&) const noexcept = default;
};

/**
 * @headerfile <idfxx/font>
 * @brief A lazy view of text broken into lines that fit a given width.
 *
 * Returned by @ref break_lines; iterating it yields one @ref text_line per
 * line, computed on demand without allocating. Lines are filled greedily:
 * each takes as many words as fit within the maximum width, breaking at the
 * last opportunity that fits.
 *
 * - **Hard breaks:** a `'\n'` always ends a line; a `'\r'` immediately
 *   before it belongs to the break. Consecutive breaks yield empty lines;
 *   a break at the very end of the text ends the last line without
 *   starting another. Empty text yields no lines.
 * - **Break opportunities:** after a run of spaces (U+0020), after a `'-'`
 *   that follows a non-space character on the same line (the `'-'` stays on
 *   the first line), and — in proportional fonts — at a U+00AD SOFT HYPHEN,
 *   when the text before it plus the font's `'-'` still fits; the line is then marked
 *   @ref text_line::hyphenated and its width includes the `'-'`.
 * - **Spaces:** the spaces at a wrap point are dropped from both lines, and
 *   spaces before a hard break or at the end of the text are dropped from
 *   the line. Leading spaces at the start of the text and after a `'\n'`
 *   are kept as indentation (unless the indentation and the first
 *   character after it do not fit together, in which case the indentation
 *   is dropped from that line). A line holding only spaces is empty.
 * - **Long words:** a word that does not fit on a line by itself is split
 *   after the last character that fits, without a hyphen. Every line that
 *   does not end at a hard break takes at least one character, so
 *   iteration always advances; with a maximum width of 0, each character
 *   gets its own line.
 *
 * The view does not own the font or the text; the caller must keep both
 * alive while the view or any of its iterators is in use. Iterators refer
 * to the text and font directly, not to the view, so they remain valid
 * after the view is destroyed (the view is a borrowed range).
 *
 * @tparam Font The font type: @ref proportional_font (text decoded as
 *              UTF-8) or @ref mono_font (one character per byte).
 */
template<typename Font>
    requires std::same_as<Font, proportional_font> || std::same_as<Font, mono_font>
class line_break_view : public std::ranges::view_interface<line_break_view<Font>> {
    struct params {
        const Font* font = nullptr;
        std::string_view text;
        size_t max_width = 0;
        unsigned scale = 1;
    };

public:
    /** @brief Forward iterator over the lines. */
    class iterator {
    public:
        /** @brief The iterator category. */
        using iterator_concept = std::forward_iterator_tag;
        /** @brief The line type. */
        using value_type = text_line;
        /** @brief Distance type (required by the iterator concepts). */
        using difference_type = std::ptrdiff_t;

        /** @brief Creates an end-of-text iterator. */
        constexpr iterator() noexcept = default;

        /** @brief Returns the current line. */
        [[nodiscard]] constexpr text_line operator*() const noexcept { return _line; }

        /** @brief Advances to the next line. */
        constexpr iterator& operator++() noexcept {
            _start = _next;
            _layout();
            return *this;
        }

        /** @brief Advances to the next line, returning the previous position. */
        constexpr iterator operator++(int) noexcept {
            iterator prev = *this;
            ++*this;
            return prev;
        }

        /** @brief Returns true if both iterators are at the same line of the same text. */
        [[nodiscard]] constexpr bool operator==(const iterator& other) const noexcept {
            return _p.text.data() == other._p.text.data() && _start == other._start;
        }

        /** @brief Returns true if the iterator has passed the last line. */
        [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const noexcept {
            return _start >= _p.text.size();
        }

    private:
        friend class line_break_view;

        static constexpr bool _proportional = std::same_as<Font, proportional_font>;

        // A break opportunity: where the line would end if broken there.
        struct break_point {
            size_t end;   // line end
            size_t width; // line width, including a drawn hyphen
            size_t next;  // start of the following line
            bool hyphen;  // whether a hyphen is drawn (a soft hyphen break)
        };

        constexpr explicit iterator(const params& p) noexcept
            : _p(p) {
            _layout();
        }

        [[nodiscard]] constexpr size_t _advance(char32_t cp) const noexcept {
            if constexpr (_proportional) {
                return detail::text_advance(*_p.font, cp);
            } else {
                return _p.font->advance(static_cast<char>(cp)) * _p.scale;
            }
        }

        // Ends the line starting at begin as broken at b.
        constexpr void _finish(size_t begin, const break_point& b) noexcept {
            _line = {_p.text.substr(begin, b.end - begin), b.width, b.hyphen};
            _next = b.next;
        }

        // Computes the line starting at _start into _line and _next.
        constexpr void _layout() noexcept {
            const std::string_view text = _p.text;
            if (_start >= text.size()) {
                return;
            }
            const size_t max_width = _p.max_width;
            const bool paragraph = _start == 0 || text[_start - 1] == '\n';
            const size_t hyphen_width = _proportional ? _advance(U'-') : 0; // at a soft hyphen

            size_t begin = _start;          // start of the line's text
            size_t end = _start;            // end of the placed characters
            size_t width = 0;               // width of the placed characters
            bool placed = false;            // any character placed on the line
            bool spaces = false;            // spaces pending after the placed characters
            size_t spaces_width = 0;        // their width, counted once a character follows
            std::optional<break_point> brk; // the latest break opportunity

            // Proportional text is decoded as UTF-8 in one pass; offsets are relative to _start.
            [[maybe_unused]] auto chars = utf8_view(text.substr(_start)).begin();
            for (size_t pos = _start, next; pos < text.size(); pos = next) {
                char32_t cp;
                if constexpr (_proportional) {
                    cp = *chars;
                    ++chars;
                    next = _start + chars.offset();
                } else {
                    cp = static_cast<unsigned char>(text[pos]);
                    next = pos + 1;
                }
                if (cp == U'\n' || (cp == U'\r' && next < text.size() && text[next] == '\n')) {
                    _finish(begin, {end, width, cp == U'\r' ? next + 1 : next, false});
                    return;
                }
                if (cp == U' ') {
                    spaces = true;
                    spaces_width += _advance(cp);
                    continue;
                }
                if (_proportional && cp == detail::soft_hyphen) {
                    // Invisible and zero-width; a break opportunity when the hyphen fits.
                    if (placed && !spaces && width + hyphen_width <= max_width) {
                        brk = break_point{pos, width + hyphen_width, next, true};
                    }
                    continue;
                }
                const size_t advance = _advance(cp);
                const bool follows_space = spaces || !placed; // then a '-' is no break
                if (!placed) {
                    // The first character is placed whatever its width, so every line
                    // advances; indentation is kept only if it fits alongside it.
                    if (spaces && paragraph && spaces_width + advance <= max_width) {
                        width = spaces_width;
                    } else {
                        begin = pos;
                    }
                    width += advance;
                } else {
                    if (spaces) {
                        brk = break_point{end, width, pos, false};
                    }
                    const size_t new_width = width + spaces_width + advance;
                    if (new_width > max_width) {
                        // Break at the latest opportunity, else split the word here.
                        _finish(begin, brk.value_or(break_point{end, width, pos, false}));
                        return;
                    }
                    width = new_width;
                }
                placed = true;
                spaces = false;
                spaces_width = 0;
                end = next;
                if (cp == U'-' && !follows_space) {
                    brk = break_point{end, width, end, false};
                }
            }
            _finish(begin, {end, width, text.size(), false});
        }

        params _p;
        size_t _start = 0;
        size_t _next = 0;
        text_line _line{};
    };

    /** @brief Creates an empty view. */
    constexpr line_break_view() noexcept = default;

    /** @brief Returns an iterator to the first line. */
    [[nodiscard]] constexpr iterator begin() const noexcept { return iterator(_p); }

    /** @brief Returns the end-of-lines sentinel. */
    [[nodiscard]] constexpr std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

private:
    friend constexpr line_break_view<proportional_font>
    break_lines(const proportional_font& font, std::string_view text, size_t max_width) noexcept;
    friend constexpr line_break_view<mono_font> break_lines(
        const mono_font& font,
        std::string_view text,
        size_t max_width,
        unsigned scale
    ) noexcept;

    constexpr line_break_view(
        const Font& font,
        std::string_view text,
        size_t max_width,
        unsigned scale
    ) noexcept
        : _p{&font, text, max_width, scale == 0 ? 1u : scale} {}

    params _p;
};

/**
 * @brief Breaks UTF-8 text into lines that fit @p max_width in a proportional font.
 *
 * Returns a lazy view yielding one @ref text_line per line; see
 * @ref line_break_view for the line-breaking rules. Widths are measured as
 * by @ref text_width. Each line's text is a slice of @p text, drawn at its
 * line's position (followed by a `'-'` when hyphenated);
 * `idfxx::gfx::draw_text_box` lays out and draws a whole block this way.
 *
 * Write U+00AD SOFT HYPHEN (`"\u00AD"`) inside long words to mark where
 * they may be hyphenated; it is never drawn otherwise.
 *
 * @code
 * using namespace idfxx::font;
 * std::string_view text = "Fresh pastries, baked every morning.";
 * size_t y = 0;
 * for (const text_line& line : break_lines(inter_16, text, 120)) {
 *     canvas.draw_text(inter_16, 0, y, line.text, white);
 *     if (line.hyphenated) {
 *         canvas.draw_text(inter_16, text_width(inter_16, line.text), y, "-", white);
 *     }
 *     y += inter_16.line_height;
 * }
 * @endcode
 *
 * @param font      The font. Must outlive the returned view.
 * @param text      The UTF-8 text to break. Must outlive the returned view.
 * @param max_width Maximum line width in pixels.
 * @return A view of the lines.
 */
[[nodiscard]] constexpr line_break_view<proportional_font>
break_lines(const proportional_font& font, std::string_view text, size_t max_width) noexcept {
    return line_break_view<proportional_font>(font, text, max_width, 1);
}

/**
 * @brief Breaks text into lines that fit @p max_width in a fixed-cell font.
 *
 * Returns a lazy view yielding one @ref text_line per line; see
 * @ref line_break_view for the line-breaking rules. Text is treated as one
 * character per byte, every character advancing one (scaled) cell, as by
 * @ref text_width; soft hyphens are not interpreted. Each line's text is a
 * slice of @p text.
 *
 * @code
 * using namespace idfxx::font;
 * size_t y = 0;
 * for (const text_line& line : break_lines(spleen_5x8, "Battery low - plug in", 60)) {
 *     canvas.draw_text(spleen_5x8, 0, y, line.text);
 *     y += spleen_5x8.height;
 * }
 * @endcode
 *
 * @param font      The font. Must outlive the returned view.
 * @param text      The text to break. Must outlive the returned view.
 * @param max_width Maximum line width in pixels.
 * @param scale     Integer magnification the text is drawn at (>= 1; 0 is
 *                  treated as 1); widths are multiplied by it.
 * @return A view of the lines.
 */
[[nodiscard]] constexpr line_break_view<mono_font> break_lines(
    const mono_font& font,
    std::string_view text,
    size_t max_width,
    unsigned scale = 1
) noexcept {
    return line_break_view<mono_font>(font, text, max_width, scale);
}

} // namespace idfxx::font

/** @cond INTERNAL */
template<typename Font>
inline constexpr bool std::ranges::enable_borrowed_range<idfxx::font::line_break_view<Font>> = true;
/** @endcond */

/** @} */ // end of idfxx_font
