// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::lcd::panel draw_bitmap validation
// Uses ESP-IDF Unity test framework

#include "idfxx/lcd/mono_framebuffer"
#include "idfxx/lcd/panel"
#include "idfxx/lcd/rgb565_framebuffer"
#include "recording_panel.hpp"
#include "unity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

using namespace idfxx::lcd;
using idfxx_lcd_test::assert_error;
using idfxx_lcd_test::recording_panel;

TEST_CASE("panel draw_bitmap passes exactly sized data to the driver", "[idfxx][lcd]") {
    // 4x2 pixels at 16 bpp: 16 bytes.
    recording_panel color(16);
    std::array<uint8_t, 4 * 2 * 2> pixels{};
    TEST_ASSERT_TRUE(color.try_draw_bitmap(1, 2, 5, 4, pixels).has_value());
    TEST_ASSERT_EQUAL(1, color.draws.size());
    TEST_ASSERT_EQUAL(1, color.draws[0].x_start);
    TEST_ASSERT_EQUAL(2, color.draws[0].y_start);
    TEST_ASSERT_EQUAL(5, color.draws[0].x_end);
    TEST_ASSERT_EQUAL(4, color.draws[0].y_end);
    TEST_ASSERT_EQUAL_PTR(pixels.data(), color.draws[0].data);
    TEST_ASSERT_EQUAL(pixels.size(), color.draws[0].size);

    // std::byte buffers and rgb565 arrays pass the same way.
    std::vector<std::byte> bytes(3 * 3 * 2);
    TEST_ASSERT_TRUE(color.try_draw_bitmap(0, 0, 3, 3, bytes).has_value());
    TEST_ASSERT_EQUAL(bytes.size(), color.draws[1].size);

    std::array<rgb565, 6> row{};
    TEST_ASSERT_TRUE(color.try_draw_bitmap(0, 0, 6, 1, std::as_bytes(std::span{row})).has_value());
    TEST_ASSERT_EQUAL_PTR(row.data(), color.draws[2].data);
    TEST_ASSERT_EQUAL(12, color.draws[2].size);

    // 2x1 pixels at 24 bpp: 6 bytes.
    recording_panel wide(24);
    std::array<uint8_t, 6> rgb666{};
    TEST_ASSERT_TRUE(wide.try_draw_bitmap(0, 0, 2, 1, rgb666).has_value());
    TEST_ASSERT_EQUAL(6, wide.draws[0].size);

    // A page of 3 columns at 1 bpp: one byte per column.
    recording_panel mono(1);
    std::array<uint8_t, 3> page{};
    TEST_ASSERT_TRUE(mono.try_draw_bitmap(5, 8, 8, 16, page).has_value());
    TEST_ASSERT_EQUAL(3, mono.draws[0].size);
}

TEST_CASE("panel draw_bitmap rejects data of the wrong size", "[idfxx][lcd]") {
    recording_panel display(16);
    // A full 8x4 frame; the region below is the top 8x2 rows (32 bytes).
    std::array<uint8_t, 8 * 4 * 2> frame{};
    idfxx::byte_view view{frame};

    // Too small.
    assert_error(display.try_draw_bitmap(0, 0, 8, 2, view.first(31)), idfxx::errc::invalid_size);
    assert_error(display.try_draw_bitmap(0, 0, 8, 2, {}), idfxx::errc::invalid_size);

    // Too large, including the whole frame for a smaller region.
    assert_error(display.try_draw_bitmap(0, 0, 8, 2, view.first(33)), idfxx::errc::invalid_size);
    assert_error(display.try_draw_bitmap(0, 0, 8, 2, frame), idfxx::errc::invalid_size);

    // A region whose size overflows size_t; 65536 * 8192 * 24 bits wraps to 0 in 32 bits.
    recording_panel wide(24);
    assert_error(wide.try_draw_bitmap(0, 0, 65536, 8192, {}), idfxx::errc::invalid_size);
    assert_error(display.try_draw_bitmap(0, 0, INT32_MAX, INT32_MAX, {}), idfxx::errc::invalid_size);
    TEST_ASSERT_EQUAL(0, wide.draws.size());
    TEST_ASSERT_EQUAL(0, display.draws.size());

    // A view of just the region's bytes is accepted.
    TEST_ASSERT_TRUE(display.try_draw_bitmap(0, 0, 8, 2, view.first(32)).has_value());
    TEST_ASSERT_EQUAL(1, display.draws.size());
    TEST_ASSERT_EQUAL_PTR(frame.data(), display.draws[0].data);
    TEST_ASSERT_EQUAL(32, display.draws[0].size);
}

TEST_CASE("panel draw_bitmap rejects an invalid region", "[idfxx][lcd]") {
    recording_panel display(16);
    std::array<uint8_t, 2> pixel{};

    // Negative start coordinates.
    assert_error(display.try_draw_bitmap(-1, 0, 0, 1, pixel), idfxx::errc::invalid_arg);
    assert_error(display.try_draw_bitmap(0, -1, 1, 0, pixel), idfxx::errc::invalid_arg);

    // Empty and inverted regions.
    assert_error(display.try_draw_bitmap(3, 0, 3, 1, pixel), idfxx::errc::invalid_arg);
    assert_error(display.try_draw_bitmap(0, 3, 1, 3, pixel), idfxx::errc::invalid_arg);
    assert_error(display.try_draw_bitmap(4, 0, 3, 1, pixel), idfxx::errc::invalid_arg);
    assert_error(display.try_draw_bitmap(0, 4, 1, 3, pixel), idfxx::errc::invalid_arg);

    // The region is checked before the data size.
    assert_error(display.try_draw_bitmap(4, 0, 3, 1, idfxx::byte_view{}), idfxx::errc::invalid_arg);
    TEST_ASSERT_EQUAL(0, display.draws.size());
}

TEST_CASE("panel write rejects a framebuffer in another format", "[idfxx][lcd]") {
    auto color_fb = rgb565_framebuffer::make(16, 8);
    TEST_ASSERT_TRUE(color_fb.has_value());
    auto mono_fb = mono_framebuffer::make(16, 8);
    TEST_ASSERT_TRUE(mono_fb.has_value());

    // An RGB565 framebuffer on a 1-bpp panel.
    recording_panel mono(1);
    assert_error(mono.try_write(*color_fb), idfxx::errc::invalid_size);
    assert_error(mono.try_write_rows(*color_fb, 0, 4), idfxx::errc::invalid_size);
    assert_error(mono.try_write_region(*color_fb, 2, 0, 6, 4), idfxx::errc::invalid_size);
    TEST_ASSERT_EQUAL(0, mono.draws.size());

    // A monochrome framebuffer on a 16-bpp panel.
    recording_panel color(16);
    assert_error(color.try_write(*mono_fb), idfxx::errc::invalid_size);
    assert_error(color.try_write_rows(*mono_fb, 0, 8), idfxx::errc::invalid_size);
    assert_error(color.try_write_region(*mono_fb, 2, 0, 6, 8), idfxx::errc::invalid_size);
    TEST_ASSERT_EQUAL(0, color.draws.size());

    // An RGB565 framebuffer on a panel sending 24 bits per pixel.
    recording_panel wide(24);
    assert_error(wide.try_write(*color_fb), idfxx::errc::invalid_size);
    TEST_ASSERT_EQUAL(0, wide.draws.size());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("panel draw_bitmap throws on an invalid region or size", "[idfxx][lcd]") {
    recording_panel display(16);
    std::array<uint8_t, 4> pixels{};

    display.draw_bitmap(0, 0, 2, 1, pixels); // no throw on success
    TEST_ASSERT_EQUAL(1, display.draws.size());

    bool thrown = false;
    try {
        display.draw_bitmap(0, 0, 1, 1, pixels);
    } catch (const std::system_error& e) {
        thrown = true;
        TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_size), e.code().value());
    }
    TEST_ASSERT_TRUE(thrown);

    thrown = false;
    try {
        display.draw_bitmap(2, 0, 0, 1, pixels);
    } catch (const std::system_error& e) {
        thrown = true;
        TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), e.code().value());
    }
    TEST_ASSERT_TRUE(thrown);
    TEST_ASSERT_EQUAL(1, display.draws.size());
}
#endif

TEST_CASE("panel reports its native bits per pixel", "[idfxx][lcd]") {
    TEST_ASSERT_EQUAL(16, recording_panel(16).native_bits_per_pixel());
    TEST_ASSERT_EQUAL(24, recording_panel(24).native_bits_per_pixel());
    TEST_ASSERT_EQUAL(1, recording_panel(1).native_bits_per_pixel());
}

TEST_CASE("panel reports whether its axes are swapped", "[idfxx][lcd]") {
    recording_panel display(16);
    TEST_ASSERT_FALSE(display.axes_swapped());

    TEST_ASSERT_TRUE(display.try_swap_xy(true).has_value());
    TEST_ASSERT_TRUE(display.axes_swapped());

    // A failed change leaves the axes as they were.
    display.fail_swap_xy = true;
    TEST_ASSERT_FALSE(display.try_swap_xy(false).has_value());
    TEST_ASSERT_TRUE(display.axes_swapped());

    display.fail_swap_xy = false;
    TEST_ASSERT_TRUE(display.try_swap_xy(false).has_value());
    TEST_ASSERT_FALSE(display.axes_swapped());
}
