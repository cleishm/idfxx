// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::epaper::write_inks
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// Frames are packed by the cleishm/epaper-image library's epi_pack_row, a
// test-only dependency, and what should reach a destination is drawn pixel by
// pixel into a framebuffer, which is independent of the code under test.

#include "idfxx/byte_source"
#include "idfxx/epaper/color"
#include "idfxx/epaper/color6_framebuffer"
#include "idfxx/epaper/gray4_framebuffer"
#include "idfxx/epaper/ink_writer"
#include "idfxx/epaper/mono_framebuffer"
#include "idfxx/epaper/panel"
#include "recording_epaper_panel.hpp"
#include "sdkconfig.h"
#include "unity.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epaper_image.h>
#include <esp_rom_crc.h>
#include <initializer_list>
#include <optional>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace idfxx::epaper;
using idfxx::errc;
using idfxx::result;
using idfxx_epaper_test::recording_epaper_panel;

namespace {

// A source over a buffer, counting how it's read. It can deliver short reads and fail
// at an offset.
class test_source {
public:
    explicit test_source(std::span<const uint8_t> bytes)
        : _bytes(bytes) {}

    result<size_t> try_read(std::span<uint8_t> buf) {
        ++reads;
        if (buf.empty()) {
            ++empty_reads;
        }
        if (pos >= frame_bytes) {
            ++reads_past_frame;
        }
        if (failed) {
            ++reads_after_failure;
        }
        if (pos == fail_at) {
            failed = true;
            return idfxx::error(errc::timeout);
        }
        size_t n = std::min({buf.size(), chunk, _bytes.size() - pos});
        if (fail_at > pos) {
            n = std::min(n, fail_at - pos);
        }
        std::copy_n(_bytes.begin() + pos, n, buf.begin());
        pos += n;
        return n;
    }

    size_t chunk = SIZE_MAX;       // the most a read delivers
    size_t fail_at = SIZE_MAX;     // the offset that fails with errc::timeout
    size_t frame_bytes = SIZE_MAX; // a read at or after this offset is counted
    size_t pos = 0;
    size_t reads = 0;
    size_t empty_reads = 0;
    size_t reads_past_frame = 0;
    size_t reads_after_failure = 0;
    bool failed = false;

private:
    std::span<const uint8_t> _bytes;
};

static_assert(idfxx::byte_source<test_source>);

// The recording panel, which also keeps the bytes written to it and can fail a write.
// A gray4 write's planes are kept apart.
class capture_panel : public recording_epaper_panel {
public:
    capture_panel(size_t width, size_t height, enum idfxx::epaper::color_mode mode)
        : recording_epaper_panel(width, height) {
        TEST_ASSERT_TRUE(try_set_color_mode(mode).has_value());
    }

    struct capture {
        size_t x;
        size_t y;
        size_t width;
        size_t rows;
        std::vector<uint8_t> first;  // the packed rows, or plane 0
        std::vector<uint8_t> second; // plane 1
    };

    std::vector<capture> captures;
    size_t fail_at_write = SIZE_MAX; // the write that fails with errc::not_allowed, from 0

    // Everything written, in order: all of plane 0 first, as in a framebuffer.
    [[nodiscard]] std::vector<uint8_t> bytes() const {
        std::vector<uint8_t> out;
        for (const auto& c : captures) {
            out.insert(out.end(), c.first.begin(), c.first.end());
        }
        for (const auto& c : captures) {
            out.insert(out.end(), c.second.begin(), c.second.end());
        }
        return out;
    }

private:
    [[nodiscard]] result<void> _fails(size_t attempt) const {
        if (attempt == fail_at_write) {
            return idfxx::error(errc::not_allowed);
        }
        return {};
    }

    [[nodiscard]] result<void>
    do_write(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override {
        mono_writes.push_back({x, y, row_start, row_end});
        if (auto r = _fails(mono_writes.size() - 1); !r) {
            return r;
        }
        capture c{
            .x = x, .y = y + row_start, .width = fb.width(), .rows = row_end - row_start, .first = {}, .second = {}
        };
        for (size_t row = row_start; row < row_end; ++row) {
            c.first.insert(c.first.end(), fb.row(row).begin(), fb.row(row).end());
        }
        captures.push_back(std::move(c));
        return {};
    }

    [[nodiscard]] result<void>
    do_write(const gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override {
        gray_writes.push_back({x, y, row_start, row_end});
        if (auto r = _fails(gray_writes.size() - 1); !r) {
            return r;
        }
        capture c{
            .x = x, .y = y + row_start, .width = fb.width(), .rows = row_end - row_start, .first = {}, .second = {}
        };
        for (size_t row = row_start; row < row_end; ++row) {
            c.first.insert(c.first.end(), fb.plane_row(0, row).begin(), fb.plane_row(0, row).end());
            c.second.insert(c.second.end(), fb.plane_row(1, row).begin(), fb.plane_row(1, row).end());
        }
        captures.push_back(std::move(c));
        return {};
    }

    [[nodiscard]] result<void>
    do_write(const color6_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override {
        color6_writes.push_back({x, y, row_start, row_end});
        if (auto r = _fails(color6_writes.size() - 1); !r) {
            return r;
        }
        capture c{
            .x = x, .y = y + row_start, .width = fb.width(), .rows = row_end - row_start, .first = {}, .second = {}
        };
        for (size_t row = row_start; row < row_end; ++row) {
            c.first.insert(c.first.end(), fb.row(row).begin(), fb.row(row).end());
        }
        captures.push_back(std::move(c));
        return {};
    }
};

// What each framebuffer type means for a frame: the library's codes, and how they draw.
template<typename Framebuffer>
struct ink_format;

template<>
struct ink_format<mono_framebuffer> {
    static constexpr enum idfxx::epaper::color_mode mode = color_mode::mono;
    static constexpr epi_pack_format_t pack = EPI_PACK_1BPP;
    static const epi_panel_t* library() { return &epi_panel_gdey075t7_mono; }
    static uint8_t code(uint32_t random) { return random & 1; }
    static bool pixel(uint8_t code) { return code == 0; } // true is black ink
};

template<>
struct ink_format<gray4_framebuffer> {
    static constexpr enum idfxx::epaper::color_mode mode = color_mode::gray4;
    static constexpr epi_pack_format_t pack = EPI_PACK_2BPP;
    static const epi_panel_t* library() { return &epi_panel_gdey075t7_grey4; }
    static uint8_t code(uint32_t random) { return random & 3; }
    static gray4 pixel(uint8_t code) { return static_cast<gray4>(3 - code); }
};

template<>
struct ink_format<color6_framebuffer> {
    static constexpr enum idfxx::epaper::color_mode mode = color_mode::color6;
    static constexpr epi_pack_format_t pack = EPI_PACK_4BPP;
    static const epi_panel_t* library() { return &epi_panel_el133uf1; }
    static uint8_t code(uint32_t random) {
        static constexpr std::array<uint8_t, 6> codes{0, 1, 2, 3, 5, 6};
        return codes[random % codes.size()];
    }
    static color6 pixel(uint8_t code) { return static_cast<color6>(code); }
};

struct frame {
    size_t width;
    size_t height;
    size_t stride;
    std::vector<uint8_t> codes;  // one for each pixel
    std::vector<uint8_t> packed; // what the library packs them to
};

// A frame of varied inks, reproducible from its size.
template<typename Framebuffer>
frame make_frame(size_t width, size_t height) {
    using format = ink_format<Framebuffer>;
    frame f{
        .width = width,
        .height = height,
        .stride = epi_pack_row_size(format::pack, static_cast<uint16_t>(width)),
        .codes = std::vector<uint8_t>(width * height),
        .packed = {},
    };
    f.packed.resize(f.stride * height);
    uint32_t state = 0x9E3779B9u ^ static_cast<uint32_t>(width * 65599u + height);
    for (auto& code : f.codes) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        code = format::code(state >> 8);
    }
    for (size_t y = 0; y < height; ++y) {
        const auto err = epi_pack_row(
            format::library(),
            format::pack,
            f.codes.data() + y * width,
            static_cast<uint16_t>(width),
            f.packed.data() + y * f.stride
        );
        TEST_ASSERT_EQUAL(EPI_OK, err);
    }
    return f;
}

// Draws the first rows of a frame into a framebuffer, with set_pixel.
template<typename Framebuffer>
void draw(Framebuffer& fb, const frame& f, size_t x = 0, size_t y = 0, size_t rows = SIZE_MAX) {
    for (size_t row = 0; row < std::min(rows, f.height); ++row) {
        for (size_t col = 0; col < f.width; ++col) {
            fb.set_pixel(x + col, y + row, ink_format<Framebuffer>::pixel(f.codes[row * f.width + col]));
        }
    }
}

template<typename Framebuffer>
std::vector<uint8_t> bytes_of(const Framebuffer& fb) {
    if constexpr (std::same_as<Framebuffer, gray4_framebuffer>) {
        std::vector<uint8_t> out(fb.plane(0).begin(), fb.plane(0).end());
        out.insert(out.end(), fb.plane(1).begin(), fb.plane(1).end());
        return out;
    } else {
        return std::vector<uint8_t>(fb.data().begin(), fb.data().end());
    }
}

void assert_bytes_equal(std::span<const uint8_t> expected, std::span<const uint8_t> actual) {
    TEST_ASSERT_EQUAL(expected.size(), actual.size());
    if (!expected.empty()) {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), actual.data(), expected.size());
    }
}

void assert_error(errc expected, const result<void>& r) {
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(expected), r.error().value());
}

template<typename Framebuffer>
void check_panel_bytes(size_t width, size_t height) {
    const auto f = make_frame<Framebuffer>(width, height);
    auto expected = Framebuffer::make(width, height).value();
    draw(expected, f);

    capture_panel display(width, height, ink_format<Framebuffer>::mode);
    test_source source(f.packed);
    TEST_ASSERT_TRUE(try_write_inks(display, source).has_value());
    assert_bytes_equal(bytes_of(expected), display.bytes());
    if constexpr (!std::same_as<Framebuffer, gray4_framebuffer>) {
        assert_bytes_equal(f.packed, display.bytes());
    }
}

template<typename Framebuffer>
void check_framebuffer_bytes(size_t width, size_t height) {
    const auto f = make_frame<Framebuffer>(width, height);
    auto expected = Framebuffer::make(width, height).value();
    draw(expected, f);

    auto fb = Framebuffer::make(width, height).value();
    test_source source(f.packed);
    TEST_ASSERT_TRUE(try_write_inks(fb, source).has_value());
    assert_bytes_equal(bytes_of(expected), bytes_of(fb));
    if constexpr (!std::same_as<Framebuffer, gray4_framebuffer>) {
        assert_bytes_equal(f.packed, bytes_of(fb));
    }
}

template<typename Framebuffer>
void check_panel_rectangle() {
    const auto mode = ink_format<Framebuffer>::mode;
    {
        // An explicit rectangle, inside the panel.
        const auto f = make_frame<Framebuffer>(24, 7);
        auto expected = Framebuffer::make(24, 7).value();
        draw(expected, f);
        capture_panel display(64, 30, mode);
        test_source source(f.packed);
        source.frame_bytes = f.packed.size();
        TEST_ASSERT_TRUE(try_write_inks(display, source, {.x = 16, .y = 5, .width = 24, .height = 7}).has_value());
        TEST_ASSERT_EQUAL(1, display.captures.size());
        TEST_ASSERT_EQUAL(16, display.captures[0].x);
        TEST_ASSERT_EQUAL(5, display.captures[0].y);
        TEST_ASSERT_EQUAL(24, display.captures[0].width);
        TEST_ASSERT_EQUAL(7, display.captures[0].rows);
        assert_bytes_equal(bytes_of(expected), display.bytes());
        TEST_ASSERT_EQUAL(f.packed.size(), source.pos);
        TEST_ASSERT_EQUAL(0, source.reads_past_frame);
    }
    {
        // No size: to the panel's right and bottom edges, including a right edge
        // that isn't a multiple of the alignment.
        const auto f = make_frame<Framebuffer>(35, 25);
        auto expected = Framebuffer::make(35, 25).value();
        draw(expected, f);
        capture_panel display(43, 30, mode);
        test_source source(f.packed);
        source.frame_bytes = f.packed.size();
        TEST_ASSERT_TRUE(try_write_inks(display, source, {.x = 8, .y = 5}).has_value());
        TEST_ASSERT_EQUAL(1, display.captures.size());
        TEST_ASSERT_EQUAL(8, display.captures[0].x);
        TEST_ASSERT_EQUAL(5, display.captures[0].y);
        TEST_ASSERT_EQUAL(35, display.captures[0].width);
        TEST_ASSERT_EQUAL(25, display.captures[0].rows);
        assert_bytes_equal(bytes_of(expected), display.bytes());
        TEST_ASSERT_EQUAL(f.packed.size(), source.pos);
        TEST_ASSERT_EQUAL(0, source.reads_past_frame);
    }
}

// A framebuffer that isn't blank, so a stray write to any pixel shows.
template<typename Framebuffer>
void paint_background(Framebuffer& fb) {
    if constexpr (std::same_as<Framebuffer, mono_framebuffer>) {
        fb.fill(true);
    } else if constexpr (std::same_as<Framebuffer, gray4_framebuffer>) {
        fb.fill(gray4::dark);
    } else {
        fb.fill(color6::green);
    }
}

template<typename Framebuffer>
void check_framebuffer_rectangle() {
    struct area {
        size_t dest_width;
        size_t dest_height;
        size_t x;
        size_t y;
        size_t width;
        size_t height;
        ink_options options;
    };
    const std::array<area, 3> areas{{
        // Inside the framebuffer, with pixels outside on every side.
        {64, 30, 16, 5, 24, 7, {.x = 16, .y = 5, .width = 24, .height = 7, .band_rows = 3}},
        // To the edges, by default.
        {64, 30, 16, 5, 48, 25, {.x = 16, .y = 5, .band_rows = 4}},
        // To a right edge that isn't a multiple of the alignment.
        {43, 30, 8, 5, 35, 7, {.x = 8, .y = 5, .height = 7}},
    }};
    for (const auto& a : areas) {
        const auto f = make_frame<Framebuffer>(a.width, a.height);
        auto fb = Framebuffer::make(a.dest_width, a.dest_height).value();
        paint_background(fb);
        auto expected = fb;
        draw(expected, f, a.x, a.y);

        test_source source(f.packed);
        source.frame_bytes = f.packed.size();
        TEST_ASSERT_TRUE(try_write_inks(fb, source, a.options).has_value());
        assert_bytes_equal(bytes_of(expected), bytes_of(fb));
        TEST_ASSERT_EQUAL(f.packed.size(), source.pos);
        TEST_ASSERT_EQUAL(0, source.reads_past_frame);
    }
}

template<typename Framebuffer>
void check_bands(size_t height, size_t band_rows, std::initializer_list<size_t> expected_rows) {
    const auto f = make_frame<Framebuffer>(24, height);
    auto expected = Framebuffer::make(24, height).value();
    draw(expected, f);

    capture_panel display(24, height, ink_format<Framebuffer>::mode);
    test_source to_panel(f.packed);
    TEST_ASSERT_TRUE(try_write_inks(display, to_panel, {.band_rows = band_rows}).has_value());
    TEST_ASSERT_EQUAL(expected_rows.size(), display.captures.size());
    size_t i = 0;
    size_t y = 0;
    for (size_t rows : expected_rows) {
        TEST_ASSERT_EQUAL(0, display.captures[i].x);
        TEST_ASSERT_EQUAL(y, display.captures[i].y);
        TEST_ASSERT_EQUAL(rows, display.captures[i].rows);
        y += rows;
        ++i;
    }
    assert_bytes_equal(bytes_of(expected), display.bytes());

    auto fb = Framebuffer::make(24, height).value();
    test_source to_framebuffer(f.packed);
    TEST_ASSERT_TRUE(try_write_inks(fb, to_framebuffer, {.band_rows = band_rows}).has_value());
    assert_bytes_equal(bytes_of(expected), bytes_of(fb));
}

template<typename Framebuffer>
void check_one_byte_reads() {
    // An odd width, to include a partly used last byte.
    const auto f = make_frame<Framebuffer>(43, 9);
    auto expected = Framebuffer::make(43, 9).value();
    draw(expected, f);

    capture_panel display(43, 9, ink_format<Framebuffer>::mode);
    test_source to_panel(f.packed);
    to_panel.chunk = 1;
    TEST_ASSERT_TRUE(try_write_inks(display, to_panel, {.band_rows = 4}).has_value());
    assert_bytes_equal(bytes_of(expected), display.bytes());
    TEST_ASSERT_EQUAL(f.packed.size(), to_panel.reads);

    auto fb = Framebuffer::make(43, 9).value();
    test_source to_framebuffer(f.packed);
    to_framebuffer.chunk = 1;
    TEST_ASSERT_TRUE(try_write_inks(fb, to_framebuffer, {.band_rows = 4}).has_value());
    assert_bytes_equal(bytes_of(expected), bytes_of(fb));
    TEST_ASSERT_EQUAL(f.packed.size(), to_framebuffer.reads);
}

// A source that fills the buffer it's given but reports more bytes than that.
template<typename Source>
class overreporting_source {
public:
    explicit overreporting_source(Source& source)
        : _source(source) {}

    result<size_t> try_read(std::span<uint8_t> buf) {
        auto n = _source.try_read(buf);
        if (n) {
            return *n + 7;
        }
        return n;
    }

private:
    Source& _source;
};

template<typename Framebuffer>
void check_overreported_reads() {
    const auto f = make_frame<Framebuffer>(24, 10);
    auto expected = Framebuffer::make(24, 10).value();
    draw(expected, f);

    capture_panel display(24, 10, ink_format<Framebuffer>::mode);
    test_source inner_panel(f.packed);
    overreporting_source to_panel(inner_panel);
    TEST_ASSERT_TRUE(try_write_inks(display, to_panel, {.band_rows = 4}).has_value());
    assert_bytes_equal(bytes_of(expected), display.bytes());

    auto fb = Framebuffer::make(24, 10).value();
    test_source inner_fb(f.packed);
    overreporting_source to_framebuffer(inner_fb);
    TEST_ASSERT_TRUE(try_write_inks(fb, to_framebuffer, {.band_rows = 4}).has_value());
    assert_bytes_equal(bytes_of(expected), bytes_of(fb));
}

template<typename Framebuffer>
void check_exact_read(size_t chunk) {
    const auto f = make_frame<Framebuffer>(24, 10);
    auto bytes = f.packed;
    // A byte that's invalid in any mode that has invalid values: it mustn't be looked at.
    bytes.push_back(0xFF);

    capture_panel display(24, 10, ink_format<Framebuffer>::mode);
    test_source to_panel(bytes);
    to_panel.chunk = chunk;
    to_panel.frame_bytes = f.packed.size();
    TEST_ASSERT_TRUE(try_write_inks(display, to_panel, {.band_rows = 4}).has_value());
    TEST_ASSERT_EQUAL(f.packed.size(), to_panel.pos);
    TEST_ASSERT_EQUAL(0, to_panel.reads_past_frame);
    TEST_ASSERT_EQUAL(0, to_panel.empty_reads);

    auto fb = Framebuffer::make(24, 10).value();
    test_source to_framebuffer(bytes);
    to_framebuffer.chunk = chunk;
    to_framebuffer.frame_bytes = f.packed.size();
    TEST_ASSERT_TRUE(try_write_inks(fb, to_framebuffer, {.band_rows = 4}).has_value());
    TEST_ASSERT_EQUAL(f.packed.size(), to_framebuffer.pos);
    TEST_ASSERT_EQUAL(0, to_framebuffer.reads_past_frame);
    TEST_ASSERT_EQUAL(0, to_framebuffer.empty_reads);
}

// The stream ends after some bytes of an 8-row frame in bands of 4.
template<typename Framebuffer>
void check_short_stream(size_t available, size_t expected_bands) {
    const auto f = make_frame<Framebuffer>(24, 8);
    const auto bytes = std::span<const uint8_t>(f.packed).first(available);
    const size_t expected_rows = expected_bands * 4;

    capture_panel display(24, 8, ink_format<Framebuffer>::mode);
    test_source to_panel(bytes);
    assert_error(errc::invalid_size, try_write_inks(display, to_panel, {.band_rows = 4}));
    TEST_ASSERT_EQUAL(expected_bands, display.hooks());
    TEST_ASSERT_EQUAL(available, to_panel.pos);

    auto expected = Framebuffer::make(24, 8).value();
    draw(expected, f, 0, 0, expected_rows);
    // Only the rows of whole bands reach the panel.
    const auto all = bytes_of(expected);
    const size_t written = expected.stride_bytes() * expected_rows;
    std::vector<uint8_t> wanted(all.begin(), all.begin() + written);
    if constexpr (std::same_as<Framebuffer, gray4_framebuffer>) {
        const size_t plane = expected.stride_bytes() * 8;
        wanted.insert(wanted.end(), all.begin() + plane, all.begin() + plane + written);
    }
    assert_bytes_equal(wanted, display.bytes());

    auto fb = Framebuffer::make(24, 8).value();
    test_source to_framebuffer(bytes);
    assert_error(errc::invalid_size, try_write_inks(fb, to_framebuffer, {.band_rows = 4}));
    assert_bytes_equal(bytes_of(expected), bytes_of(fb));
    TEST_ASSERT_EQUAL(available, to_framebuffer.pos);
}

template<typename Framebuffer>
void check_short_streams() {
    const size_t stride = make_frame<Framebuffer>(24, 1).stride;
    check_short_stream<Framebuffer>(0, 0);                       // at the start
    check_short_stream<Framebuffer>(stride / 2, 0);              // in the first row
    check_short_stream<Framebuffer>(4 * stride - 1, 0);          // a byte short of the first band
    check_short_stream<Framebuffer>(4 * stride, 1);              // at the end of the first band
    check_short_stream<Framebuffer>(5 * stride + stride / 2, 1); // in a row of the second band
    check_short_stream<Framebuffer>(8 * stride - 1, 1);          // a byte short of the frame
}

template<typename Framebuffer>
void check_source_error(size_t fail_at, size_t expected_bands) {
    const auto f = make_frame<Framebuffer>(24, 8);
    const size_t expected_rows = expected_bands * 4;
    auto expected = Framebuffer::make(24, 8).value();
    draw(expected, f, 0, 0, expected_rows);

    capture_panel display(24, 8, ink_format<Framebuffer>::mode);
    test_source to_panel(f.packed);
    to_panel.fail_at = fail_at;
    assert_error(errc::timeout, try_write_inks(display, to_panel, {.band_rows = 4}));
    TEST_ASSERT_EQUAL(expected_bands, display.hooks());
    TEST_ASSERT_EQUAL(0, to_panel.reads_after_failure);

    auto fb = Framebuffer::make(24, 8).value();
    test_source to_framebuffer(f.packed);
    to_framebuffer.fail_at = fail_at;
    assert_error(errc::timeout, try_write_inks(fb, to_framebuffer, {.band_rows = 4}));
    assert_bytes_equal(bytes_of(expected), bytes_of(fb));
    TEST_ASSERT_EQUAL(0, to_framebuffer.reads_after_failure);
}

template<typename Framebuffer>
void check_panel_error() {
    const auto f = make_frame<Framebuffer>(24, 12);
    const size_t band_bytes = 4 * f.stride;
    {
        capture_panel display(24, 12, ink_format<Framebuffer>::mode);
        display.fail_at_write = 0;
        test_source source(f.packed);
        assert_error(errc::not_allowed, try_write_inks(display, source, {.band_rows = 4}));
        TEST_ASSERT_EQUAL(1, display.hooks());
        TEST_ASSERT_EQUAL(0, display.captures.size());
        TEST_ASSERT_EQUAL(band_bytes, source.pos);
    }
    {
        capture_panel display(24, 12, ink_format<Framebuffer>::mode);
        display.fail_at_write = 1;
        test_source source(f.packed);
        assert_error(errc::not_allowed, try_write_inks(display, source, {.band_rows = 4}));
        TEST_ASSERT_EQUAL(2, display.hooks());
        TEST_ASSERT_EQUAL(1, display.captures.size());
        TEST_ASSERT_EQUAL(2 * band_bytes, source.pos);
    }
}

template<typename Framebuffer>
void check_rejects(const ink_options& options) {
    const std::array<uint8_t, 16> bytes{};
    {
        capture_panel display(64, 16, ink_format<Framebuffer>::mode);
        test_source source(bytes);
        assert_error(errc::invalid_arg, try_write_inks(display, source, options));
        TEST_ASSERT_EQUAL(0, source.reads);
        TEST_ASSERT_EQUAL(0, display.hooks());
    }
    {
        auto fb = Framebuffer::make(64, 16).value();
        paint_background(fb);
        const auto before = bytes_of(fb);
        test_source source(bytes);
        assert_error(errc::invalid_arg, try_write_inks(fb, source, options));
        TEST_ASSERT_EQUAL(0, source.reads);
        assert_bytes_equal(before, bytes_of(fb));
    }
}

template<typename Framebuffer>
void check_up_front_rejections() {
    // Sizes and positions that leave nothing to write.
    check_rejects<Framebuffer>({.band_rows = 0});
    check_rejects<Framebuffer>({.width = 0});
    check_rejects<Framebuffer>({.height = 0});
    check_rejects<Framebuffer>({.x = 64});
    check_rejects<Framebuffer>({.y = 16});
    check_rejects<Framebuffer>({.x = 72});
    check_rejects<Framebuffer>({.y = 24});
    check_rejects<Framebuffer>({.x = 64, .width = 8});
    check_rejects<Framebuffer>({.y = 16, .height = 4});
    // Past the edges: never clipped.
    check_rejects<Framebuffer>({.width = 72});
    check_rejects<Framebuffer>({.height = 17});
    check_rejects<Framebuffer>({.x = 8, .width = 64});
    check_rejects<Framebuffer>({.y = 4, .height = 13});
    // Sizes that would wrap around.
    check_rejects<Framebuffer>({.x = 8, .width = SIZE_MAX});
    check_rejects<Framebuffer>({.x = 8, .width = SIZE_MAX - 7});
    check_rejects<Framebuffer>({.y = 4, .height = SIZE_MAX});
    // Edges off the alignment.
    check_rejects<Framebuffer>({.x = 1});
    check_rejects<Framebuffer>({.x = 1, .width = 7});
    check_rejects<Framebuffer>({.width = 1});
    check_rejects<Framebuffer>({.x = Framebuffer::x_alignment, .width = Framebuffer::x_alignment + 1});
    if (Framebuffer::x_alignment == 8) {
        check_rejects<Framebuffer>({.x = 4});
        check_rejects<Framebuffer>({.width = 4});
        check_rejects<Framebuffer>({.x = 8, .width = 12});
    }
}

template<typename Framebuffer>
void check_asleep() {
    const std::array<uint8_t, 16> bytes{};
    capture_panel display(64, 16, ink_format<Framebuffer>::mode);
    TEST_ASSERT_TRUE(display.try_sleep().has_value());
    const size_t hooks = display.hooks();

    test_source source(bytes);
    assert_error(errc::invalid_state, try_write_inks(display, source));
    TEST_ASSERT_EQUAL(0, source.reads);
    TEST_ASSERT_EQUAL(hooks, display.hooks());

    // Asleep is reported before anything about the arguments.
    assert_error(errc::invalid_state, try_write_inks(display, source, {.band_rows = 0}));
    TEST_ASSERT_EQUAL(0, source.reads);
    TEST_ASSERT_EQUAL(hooks, display.hooks());

    // The same call succeeds once awake.
    TEST_ASSERT_TRUE(display.try_wake().has_value());
    const auto f = make_frame<Framebuffer>(64, 16);
    test_source frame_source(f.packed);
    TEST_ASSERT_TRUE(try_write_inks(display, frame_source).has_value());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
template<typename Framebuffer>
void check_throwing_forms() {
    const auto f = make_frame<Framebuffer>(24, 6);
    auto expected = Framebuffer::make(24, 6).value();
    draw(expected, f);

    capture_panel display(24, 6, ink_format<Framebuffer>::mode);
    test_source to_panel(f.packed);
    write_inks(display, to_panel, {.band_rows = 4});
    assert_bytes_equal(bytes_of(expected), display.bytes());

    auto fb = Framebuffer::make(24, 6).value();
    test_source to_framebuffer(f.packed);
    write_inks(fb, to_framebuffer);
    assert_bytes_equal(bytes_of(expected), bytes_of(fb));

    const auto throws = [](errc expected_error, auto&& call) {
        try {
            call();
            TEST_FAIL_MESSAGE("expected a std::system_error");
        } catch (const std::system_error& e) {
            TEST_ASSERT_EQUAL(std::to_underlying(expected_error), e.code().value());
        }
    };

    const auto bytes = std::span<const uint8_t>(f.packed).first(f.stride);
    test_source short_source(bytes);
    throws(errc::invalid_size, [&] { write_inks(fb, short_source); });
    test_source short_panel_source(bytes);
    throws(errc::invalid_size, [&] { write_inks(display, short_panel_source); });
    test_source bad_args_source(bytes);
    throws(errc::invalid_arg, [&] { write_inks(fb, bad_args_source, {.band_rows = 0}); });
    TEST_ASSERT_EQUAL(0, bad_args_source.reads);
    throws(errc::invalid_arg, [&] { write_inks(display, bad_args_source, {.band_rows = 0}); });
    TEST_ASSERT_EQUAL(0, bad_args_source.reads);

    test_source failing_source(f.packed);
    failing_source.fail_at = 0;
    throws(errc::timeout, [&] { write_inks(fb, failing_source); });

    TEST_ASSERT_TRUE(display.try_sleep().has_value());
    test_source asleep_source(f.packed);
    throws(errc::invalid_state, [&] { write_inks(display, asleep_source); });
    TEST_ASSERT_EQUAL(0, asleep_source.reads);
}
#endif

// The wrapper the documentation shows, for verifying a frame.
template<idfxx::byte_source Source>
class crc_source {
public:
    explicit crc_source(Source& source)
        : _source(source) {}

    result<size_t> try_read(std::span<uint8_t> buf) {
        auto n = _source.try_read(buf);
        if (n) {
            _crc = esp_rom_crc32_le(_crc, buf.data(), *n);
        }
        return n;
    }

    uint32_t crc() const noexcept { return _crc; }

private:
    Source& _source;
    uint32_t _crc = 0;
};

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(ink_options{}.x == 0);
static_assert(ink_options{}.y == 0);
static_assert(!ink_options{}.width.has_value());
static_assert(!ink_options{}.height.has_value());
static_assert(ink_options{}.band_rows == 32);

// The result-based forms return nothing but a result, whatever the destination.
static_assert(std::same_as<
              decltype(try_write_inks(std::declval<panel&>(), std::declval<test_source&>())),
              result<void>>);
static_assert(std::same_as<
              decltype(try_write_inks(std::declval<mono_framebuffer&>(), std::declval<test_source&>())),
              result<void>>);
static_assert(std::same_as<
              decltype(try_write_inks(std::declval<gray4_framebuffer&>(), std::declval<test_source&>())),
              result<void>>);
static_assert(std::same_as<
              decltype(try_write_inks(std::declval<color6_framebuffer&>(), std::declval<test_source&>())),
              result<void>>);

// =============================================================================
// Runtime tests: the bytes that arrive
// =============================================================================

TEST_CASE("epaper write_inks matches the library's packing on a panel", "[idfxx][epaper]") {
    // Multiples of the alignment and not, the latter at the panel's right edge; and more
    // rows than a band of the default size holds.
    for (size_t width : {size_t{40}, size_t{41}, size_t{42}, size_t{43}, size_t{47}}) {
        check_panel_bytes<mono_framebuffer>(width, 40);
        check_panel_bytes<gray4_framebuffer>(width, 40);
        check_panel_bytes<color6_framebuffer>(width, 40);
    }
}

TEST_CASE("epaper write_inks matches the library's packing in a framebuffer", "[idfxx][epaper]") {
    for (size_t width : {size_t{40}, size_t{41}, size_t{42}, size_t{43}, size_t{47}}) {
        check_framebuffer_bytes<mono_framebuffer>(width, 40);
        check_framebuffer_bytes<gray4_framebuffer>(width, 40);
        check_framebuffer_bytes<color6_framebuffer>(width, 40);
    }
}

TEST_CASE("epaper write_inks splits gray4 codes into the two planes", "[idfxx][epaper]") {
    // Codes 0, 1, 2, 3 from darkest to lightest: levels 3, 2, 1, 0.
    const std::array<uint8_t, 2> bytes{0b00'01'10'11, 0b11'00'01'10};
    // Plane 0 is bit 0 of the level, and plane 1 is bit 1.
    const std::array<uint8_t, 1> plane0{0b1010'0101};
    const std::array<uint8_t, 1> plane1{0b1100'0110};

    capture_panel display(8, 1, color_mode::gray4);
    test_source to_panel(bytes);
    TEST_ASSERT_TRUE(try_write_inks(display, to_panel).has_value());
    TEST_ASSERT_EQUAL(1, display.captures.size());
    assert_bytes_equal(plane0, display.captures[0].first);
    assert_bytes_equal(plane1, display.captures[0].second);

    auto fb = gray4_framebuffer::make(8, 1).value();
    test_source to_framebuffer(bytes);
    TEST_ASSERT_TRUE(try_write_inks(fb, to_framebuffer).has_value());
    assert_bytes_equal(plane0, fb.plane(0));
    assert_bytes_equal(plane1, fb.plane(1));
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0) == gray4::black);
    TEST_ASSERT_TRUE(fb.get_pixel(1, 0) == gray4::dark);
    TEST_ASSERT_TRUE(fb.get_pixel(2, 0) == gray4::light);
    TEST_ASSERT_TRUE(fb.get_pixel(3, 0) == gray4::white);
}

TEST_CASE("epaper write_inks keeps gray4 padding white", "[idfxx][epaper]") {
    // 3 pixels, codes 0, 1, 2, then padding that claims to be the darkest or the lightest.
    for (uint8_t padding : {uint8_t{0b00}, uint8_t{0b11}}) {
        const std::array<uint8_t, 1> bytes{static_cast<uint8_t>(0b00'01'10'00 | padding)};
        const std::array<uint8_t, 1> plane0{0b1010'0000};
        const std::array<uint8_t, 1> plane1{0b1100'0000};

        capture_panel display(3, 1, color_mode::gray4);
        test_source to_panel(bytes);
        TEST_ASSERT_TRUE(try_write_inks(display, to_panel).has_value());
        assert_bytes_equal(plane0, display.captures[0].first);
        assert_bytes_equal(plane1, display.captures[0].second);

        auto fb = gray4_framebuffer::make(3, 1).value();
        test_source to_framebuffer(bytes);
        TEST_ASSERT_TRUE(try_write_inks(fb, to_framebuffer).has_value());
        assert_bytes_equal(plane0, fb.plane(0));
        assert_bytes_equal(plane1, fb.plane(1));
    }
}

TEST_CASE("epaper write_inks reads mono as 1 is white, high bit first", "[idfxx][epaper]") {
    const std::array<uint8_t, 1> bytes{0b0111'1111};
    auto fb = mono_framebuffer::make(8, 1).value();
    test_source source(bytes);
    TEST_ASSERT_TRUE(try_write_inks(fb, source).has_value());
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0)); // true is black ink
    for (size_t x = 1; x < 8; ++x) {
        TEST_ASSERT_FALSE(fb.get_pixel(x, 0));
    }
}

TEST_CASE("epaper write_inks reads color6 high nibble first", "[idfxx][epaper]") {
    const std::array<uint8_t, 2> bytes{0x35, 0x60};
    auto fb = color6_framebuffer::make(4, 1).value();
    test_source source(bytes);
    TEST_ASSERT_TRUE(try_write_inks(fb, source).has_value());
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0) == color6::red);
    TEST_ASSERT_TRUE(fb.get_pixel(1, 0) == color6::blue);
    TEST_ASSERT_TRUE(fb.get_pixel(2, 0) == color6::green);
    TEST_ASSERT_TRUE(fb.get_pixel(3, 0) == color6::black);
}

TEST_CASE("epaper write_inks keeps mono and color6 padding white", "[idfxx][epaper]") {
    {
        // 3 pixels on 2 rows, the padding claiming to be black.
        const std::array<uint8_t, 2> bytes{0b1010'0000, 0b0100'0000};
        const std::array<uint8_t, 2> expected{0b1011'1111, 0b0101'1111};
        auto fb = mono_framebuffer::make(3, 2).value();
        test_source to_fb(bytes);
        TEST_ASSERT_TRUE(try_write_inks(fb, to_fb).has_value());
        assert_bytes_equal(expected, fb.data());

        capture_panel display(3, 2, color_mode::mono);
        test_source to_panel(bytes);
        TEST_ASSERT_TRUE(try_write_inks(display, to_panel).has_value());
        assert_bytes_equal(expected, display.bytes());
    }
    {
        // 1 pixel on 2 rows, the padding claiming to be black.
        const std::array<uint8_t, 2> bytes{0x20, 0x30};
        const std::array<uint8_t, 2> expected{0x21, 0x31};
        auto fb = color6_framebuffer::make(1, 2).value();
        test_source to_fb(bytes);
        TEST_ASSERT_TRUE(try_write_inks(fb, to_fb).has_value());
        assert_bytes_equal(expected, fb.data());

        capture_panel display(1, 2, color_mode::color6);
        test_source to_panel(bytes);
        TEST_ASSERT_TRUE(try_write_inks(display, to_panel).has_value());
        assert_bytes_equal(expected, display.bytes());
    }
}

// =============================================================================
// Runtime tests: placement
// =============================================================================

TEST_CASE("epaper write_inks places a rectangle on a panel", "[idfxx][epaper]") {
    check_panel_rectangle<mono_framebuffer>();
    check_panel_rectangle<gray4_framebuffer>();
    check_panel_rectangle<color6_framebuffer>();
}

TEST_CASE("epaper write_inks leaves a framebuffer's other pixels alone", "[idfxx][epaper]") {
    check_framebuffer_rectangle<mono_framebuffer>();
    check_framebuffer_rectangle<gray4_framebuffer>();
    check_framebuffer_rectangle<color6_framebuffer>();
}

TEST_CASE("epaper write_inks takes color6's narrower alignment", "[idfxx][epaper]") {
    const auto f = make_frame<color6_framebuffer>(6, 2);

    capture_panel display(16, 4, color_mode::color6);
    test_source to_panel(f.packed);
    TEST_ASSERT_TRUE(try_write_inks(display, to_panel, {.x = 2, .y = 1, .width = 6, .height = 2}).has_value());
    TEST_ASSERT_EQUAL(2, display.captures[0].x);
    TEST_ASSERT_EQUAL(1, display.captures[0].y);
    assert_bytes_equal(f.packed, display.bytes());

    auto fb = color6_framebuffer::make(16, 4).value();
    paint_background(fb);
    auto expected = fb;
    draw(expected, f, 2, 1);
    test_source to_framebuffer(f.packed);
    TEST_ASSERT_TRUE(try_write_inks(fb, to_framebuffer, {.x = 2, .y = 1, .width = 6, .height = 2}).has_value());
    assert_bytes_equal(bytes_of(expected), bytes_of(fb));
}

// =============================================================================
// Runtime tests: bands and reads
// =============================================================================

TEST_CASE("epaper write_inks splits a frame into bands", "[idfxx][epaper]") {
    // More rows to a band than the frame has, including an amount that couldn't be allocated.
    check_bands<mono_framebuffer>(10, 11, {10});
    check_bands<gray4_framebuffer>(10, 11, {10});
    check_bands<color6_framebuffer>(10, 11, {10});
    check_bands<mono_framebuffer>(10, SIZE_MAX, {10});
    check_bands<gray4_framebuffer>(10, SIZE_MAX, {10});
    check_bands<color6_framebuffer>(10, SIZE_MAX, {10});
    // Exactly the frame's rows.
    check_bands<mono_framebuffer>(10, 10, {10});
    check_bands<gray4_framebuffer>(10, 10, {10});
    check_bands<color6_framebuffer>(10, 10, {10});
    // An exact multiple.
    check_bands<mono_framebuffer>(12, 4, {4, 4, 4});
    check_bands<gray4_framebuffer>(12, 4, {4, 4, 4});
    check_bands<color6_framebuffer>(12, 4, {4, 4, 4});
    // A remainder.
    check_bands<mono_framebuffer>(10, 4, {4, 4, 2});
    check_bands<gray4_framebuffer>(10, 4, {4, 4, 2});
    check_bands<color6_framebuffer>(10, 4, {4, 4, 2});
    // A row at a time.
    check_bands<mono_framebuffer>(3, 1, {1, 1, 1});
    check_bands<gray4_framebuffer>(3, 1, {1, 1, 1});
    check_bands<color6_framebuffer>(3, 1, {1, 1, 1});
}

TEST_CASE("epaper write_inks takes a byte at a time from a source", "[idfxx][epaper]") {
    check_one_byte_reads<mono_framebuffer>();
    check_one_byte_reads<gray4_framebuffer>();
    check_one_byte_reads<color6_framebuffer>();
}

TEST_CASE("epaper write_inks reads exactly the frame's bytes", "[idfxx][epaper]") {
    for (size_t chunk : {size_t{SIZE_MAX}, size_t{1}, size_t{5}}) {
        check_exact_read<mono_framebuffer>(chunk);
        check_exact_read<gray4_framebuffer>(chunk);
        check_exact_read<color6_framebuffer>(chunk);
    }
}

TEST_CASE("epaper write_inks ignores bytes a source reports beyond the buffer", "[idfxx][epaper]") {
    check_overreported_reads<mono_framebuffer>();
    check_overreported_reads<gray4_framebuffer>();
    check_overreported_reads<color6_framebuffer>();
}

TEST_CASE("epaper write_inks reports a stream that ends early", "[idfxx][epaper]") {
    check_short_streams<mono_framebuffer>();
    check_short_streams<gray4_framebuffer>();
    check_short_streams<color6_framebuffer>();
}

// =============================================================================
// Runtime tests: color6 values
// =============================================================================

TEST_CASE("epaper write_inks accepts every color6 value in every position", "[idfxx][epaper]") {
    for (uint8_t value : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{5}, uint8_t{6}}) {
        // High, low, and the padding nibble of an odd width, which comes out white.
        const std::array<uint8_t, 2> high{static_cast<uint8_t>((value << 4) | 1), 0x11};
        const std::array<uint8_t, 2> low{static_cast<uint8_t>(0x10 | value), 0x11};
        const std::array<uint8_t, 2> padding{0x11, static_cast<uint8_t>(0x10 | value)};
        for (const auto& bytes : {high, low}) {
            capture_panel display(4, 1, color_mode::color6);
            test_source source(bytes);
            TEST_ASSERT_TRUE(try_write_inks(display, source).has_value());
            assert_bytes_equal(bytes, display.bytes());
        }
        capture_panel display(3, 1, color_mode::color6);
        test_source source(padding);
        TEST_ASSERT_TRUE(try_write_inks(display, source).has_value());
        const std::array<uint8_t, 2> padded{0x11, 0x11};
        assert_bytes_equal(padded, display.bytes());
    }
}

TEST_CASE("epaper write_inks rejects every invalid color6 nibble", "[idfxx][epaper]") {
    const std::array<uint8_t, 10> values{4, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    for (uint8_t value : values) {
        // At the end of the last row, so everything before it is valid. Width 4 for the
        // high and low nibbles, and 3 for the padding nibble of an odd width.
        const std::array<uint8_t, 4> high{0x11, 0x11, 0x11, static_cast<uint8_t>((value << 4) | 1)};
        const std::array<uint8_t, 4> low{0x11, 0x11, 0x11, static_cast<uint8_t>(0x10 | value)};
        struct invalid {
            std::array<uint8_t, 4> bytes;
            size_t width;
        };
        for (const auto& [bytes, width] : {invalid{high, 4}, invalid{low, 4}, invalid{low, 3}}) {
            capture_panel display(width, 2, color_mode::color6);
            test_source to_panel(bytes);
            assert_error(errc::invalid_arg, try_write_inks(display, to_panel));
            TEST_ASSERT_EQUAL(0, display.hooks());

            auto fb = color6_framebuffer::make(width, 2).value();
            const auto before = bytes_of(fb);
            test_source to_framebuffer(bytes);
            assert_error(errc::invalid_arg, try_write_inks(fb, to_framebuffer));
            assert_bytes_equal(before, bytes_of(fb));
        }
    }
}

TEST_CASE("epaper write_inks writes nothing of an invalid band", "[idfxx][epaper]") {
    // Bands of 2 rows. The second band's last byte is invalid, and a third band follows.
    constexpr size_t width = 8;
    constexpr size_t height = 6;
    constexpr size_t band_rows = 2;
    const auto f = make_frame<color6_framebuffer>(width, height);
    auto bytes = f.packed;
    bytes[2 * band_rows * f.stride - 1] = 0x47;

    capture_panel display(width, height, color_mode::color6);
    test_source to_panel(bytes);
    assert_error(errc::invalid_arg, try_write_inks(display, to_panel, {.band_rows = band_rows}));
    TEST_ASSERT_EQUAL(1, display.hooks());
    TEST_ASSERT_EQUAL(1, display.captures.size());
    TEST_ASSERT_EQUAL(0, display.captures[0].y);
    TEST_ASSERT_EQUAL(band_rows, display.captures[0].rows);
    assert_bytes_equal(std::span<const uint8_t>(f.packed).first(band_rows * f.stride), display.bytes());
    // The frame stops there.
    TEST_ASSERT_EQUAL(2 * band_rows * f.stride, to_panel.pos);

    auto fb = color6_framebuffer::make(width, height).value();
    auto expected = fb; // all white
    draw(expected, f, 0, 0, band_rows);
    test_source to_framebuffer(bytes);
    assert_error(errc::invalid_arg, try_write_inks(fb, to_framebuffer, {.band_rows = band_rows}));
    assert_bytes_equal(bytes_of(expected), bytes_of(fb));
    TEST_ASSERT_EQUAL(2 * band_rows * f.stride, to_framebuffer.pos);
}

TEST_CASE("epaper write_inks reports a short stream before an invalid value in its band", "[idfxx][epaper]") {
    const std::array<uint8_t, 3> bytes{0x11, 0x77, 0x11};
    capture_panel display(4, 2, color_mode::color6);
    test_source source(bytes);
    assert_error(errc::invalid_size, try_write_inks(display, source));
    TEST_ASSERT_EQUAL(0, display.hooks());
}

// =============================================================================
// Runtime tests: errors
// =============================================================================

TEST_CASE("epaper write_inks passes on a source's error and stops", "[idfxx][epaper]") {
    // At the start, and in the first and second bands of 4 rows.
    const auto check = []<typename Framebuffer>(std::type_identity<Framebuffer>) {
        const size_t stride = make_frame<Framebuffer>(24, 1).stride;
        check_source_error<Framebuffer>(0, 0);
        check_source_error<Framebuffer>(stride + 1, 0);
        check_source_error<Framebuffer>(4 * stride + 1, 1);
        check_source_error<Framebuffer>(8 * stride - 1, 1);
    };
    check(std::type_identity<mono_framebuffer>{});
    check(std::type_identity<gray4_framebuffer>{});
    check(std::type_identity<color6_framebuffer>{});
}

TEST_CASE("epaper write_inks passes on a panel's error and stops", "[idfxx][epaper]") {
    check_panel_error<mono_framebuffer>();
    check_panel_error<gray4_framebuffer>();
    check_panel_error<color6_framebuffer>();
}

TEST_CASE("epaper write_inks rejects bad options before reading", "[idfxx][epaper]") {
    check_up_front_rejections<mono_framebuffer>();
    check_up_front_rejections<gray4_framebuffer>();
    check_up_front_rejections<color6_framebuffer>();
}

TEST_CASE("epaper write_inks rejects a sleeping panel before reading", "[idfxx][epaper]") {
    check_asleep<mono_framebuffer>();
    check_asleep<gray4_framebuffer>();
    check_asleep<color6_framebuffer>();
}

// =============================================================================
// Runtime tests: verifying a frame
// =============================================================================

TEST_CASE("epaper write_inks can be verified by a CRC as bytes pass", "[idfxx][epaper]") {
    // The function computes the usual CRC-32, across calls.
    const std::array<uint8_t, 9> check_string{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, esp_rom_crc32_le(0, check_string.data(), check_string.size()));
    const uint32_t part = esp_rom_crc32_le(0, check_string.data(), 4);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, esp_rom_crc32_le(part, check_string.data() + 4, 5));

    // The CRC follows the frame on the stream.
    const auto f = make_frame<mono_framebuffer>(24, 10);
    const uint32_t expected = esp_rom_crc32_le(0, f.packed.data(), f.packed.size());
    auto bytes = f.packed;
    for (size_t i = 0; i < sizeof(expected); ++i) {
        bytes.push_back(static_cast<uint8_t>(expected >> (8 * i)));
    }

    for (size_t chunk : {size_t{SIZE_MAX}, size_t{1}}) {
        capture_panel display(24, 10, color_mode::mono);
        test_source source(bytes);
        source.chunk = chunk;
        crc_source checked(source);
        TEST_ASSERT_TRUE(try_write_inks(display, checked, {.band_rows = 3}).has_value());
        TEST_ASSERT_EQUAL_HEX32(expected, checked.crc());

        std::array<uint8_t, 4> trailer{};
        size_t got = 0;
        while (got < trailer.size()) {
            auto n = source.try_read(std::span<uint8_t>(trailer).subspan(got));
            TEST_ASSERT_TRUE(n.has_value());
            TEST_ASSERT_NOT_EQUAL(0, *n);
            got += *n;
        }
        assert_bytes_equal(std::span<const uint8_t>(bytes).last(4), trailer);
    }

    // A flipped bit is a valid frame, as far as the frame can tell, but the CRC differs.
    auto corrupt = f.packed;
    corrupt[7] ^= 0x10;
    capture_panel display(24, 10, color_mode::mono);
    test_source source(corrupt);
    crc_source checked(source);
    TEST_ASSERT_TRUE(try_write_inks(display, checked).has_value());
    TEST_ASSERT_NOT_EQUAL(expected, checked.crc());
}

// =============================================================================
// Runtime tests: throwing forms
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("epaper write_inks throws on error", "[idfxx][epaper]") {
    check_throwing_forms<mono_framebuffer>();
    check_throwing_forms<gray4_framebuffer>();
    check_throwing_forms<color6_framebuffer>();
}

TEST_CASE("epaper write_inks throws a color6 value's error", "[idfxx][epaper]") {
    const std::array<uint8_t, 2> bytes{0x11, 0x17};
    auto fb = color6_framebuffer::make(4, 1).value();
    test_source source(bytes);
    try {
        write_inks(fb, source);
        TEST_FAIL_MESSAGE("expected a std::system_error");
    } catch (const std::system_error& e) {
        TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), e.code().value());
    }
}
#endif
