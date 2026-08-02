// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_epaper
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/epaper/color"
#include "idfxx/epaper/gray4_framebuffer"
#include "idfxx/epaper/mono_framebuffer"
#include "idfxx/epaper/panel"
#include "idfxx/gfx"
#include "recording_epaper_panel.hpp"
#include "unity.h"

#include <type_traits>
#include <utility>

using namespace idfxx::epaper;
using idfxx_epaper_test::recording_epaper_panel;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// The panel base is abstract — no concrete controller details leak in.
static_assert(std::is_abstract_v<panel>);

// The base is non-copyable so concrete drivers inherit that property.
static_assert(!std::is_copy_constructible_v<panel>);
static_assert(!std::is_copy_assignable_v<panel>);

// The framebuffers are regular value types: copyable and movable.
static_assert(std::is_copy_constructible_v<mono_framebuffer>);
static_assert(std::is_move_constructible_v<mono_framebuffer>);
static_assert(std::is_copy_constructible_v<gray4_framebuffer>);
static_assert(std::is_move_constructible_v<gray4_framebuffer>);

// Both framebuffers are drawable surfaces for the idfxx_gfx primitives.
static_assert(idfxx::gfx::pixel_surface<mono_framebuffer>);
static_assert(idfxx::gfx::pixel_surface<gray4_framebuffer>);

// A value-initialized pixel is blank paper, so gfx::canvas::clear() blanks
// ePaper surfaces to white.
static_assert(gray4{} == gray4::white);
static_assert(bool{} == false); // mono: false = white paper

// Gray levels are the canonical 2-bit values streamed to controller planes.
static_assert(std::to_underlying(gray4::white) == 0);
static_assert(std::to_underlying(gray4::light) == 1);
static_assert(std::to_underlying(gray4::dark) == 2);
static_assert(std::to_underlying(gray4::black) == 3);

// =============================================================================
// Runtime tests: mono_framebuffer
// =============================================================================

TEST_CASE("epaper mono_framebuffer::make rejects invalid dimensions", "[idfxx][epaper]") {
    auto zero_width = mono_framebuffer::make(0, 250);
    TEST_ASSERT_FALSE(zero_width.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), zero_width.error().value());

    auto zero_height = mono_framebuffer::make(122, 0);
    TEST_ASSERT_FALSE(zero_height.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), zero_height.error().value());
}

TEST_CASE("epaper mono_framebuffer starts all-white with byte-padded rows", "[idfxx][epaper]") {
    auto fb = mono_framebuffer::make(122, 250);
    TEST_ASSERT_TRUE(fb.has_value());

    TEST_ASSERT_EQUAL(122, fb->width());
    TEST_ASSERT_EQUAL(250, fb->height());
    TEST_ASSERT_EQUAL(16, fb->stride_bytes()); // (122 + 7) / 8
    TEST_ASSERT_EQUAL(16 * 250, fb->data().size());

    // White paper is all bits set in controller RAM convention.
    for (uint8_t byte : fb->data()) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, byte);
    }
    TEST_ASSERT_FALSE(fb->get_pixel(0, 0));
}

TEST_CASE("epaper mono_framebuffer set_pixel uses row-major MSB-first layout", "[idfxx][epaper]") {
    auto fb = mono_framebuffer::make(16, 4).value();

    // (0, 0) is bit 7 of byte 0; ink clears the bit.
    fb.set_pixel(0, 0, true);
    TEST_ASSERT_EQUAL_HEX8(0x7F, fb.data()[0]);
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));

    // (10, 2) is bit 5 of byte (2 * 2) + 1.
    fb.set_pixel(10, 2, true);
    TEST_ASSERT_EQUAL_HEX8(0xDF, fb.data()[2 * 2 + 1]);
    TEST_ASSERT_TRUE(fb.get_pixel(10, 2));

    // Blanking restores the white bit.
    fb.set_pixel(0, 0, false);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fb.data()[0]);
    TEST_ASSERT_FALSE(fb.get_pixel(0, 0));

    // Out-of-range writes are ignored.
    fb.set_pixel(16, 0, true);
    fb.set_pixel(0, 4, true);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fb.data()[0]);
}

TEST_CASE("epaper mono_framebuffer row spans and fill", "[idfxx][epaper]") {
    auto fb = mono_framebuffer::make(12, 3).value(); // stride 2

    fb.fill(true);
    for (uint8_t byte : fb.data()) {
        TEST_ASSERT_EQUAL_HEX8(0x00, byte);
    }

    fb.clear();
    for (uint8_t byte : fb.data()) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, byte);
    }

    fb.set_pixel(0, 1, true);
    auto row = fb.row(1);
    TEST_ASSERT_EQUAL(2, row.size());
    TEST_ASSERT_EQUAL_HEX8(0x7F, row[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fb.row(0)[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fb.row(2)[0]);
}

// =============================================================================
// Runtime tests: gray4_framebuffer
// =============================================================================

TEST_CASE("epaper gray4_framebuffer starts all-white with two planes", "[idfxx][epaper]") {
    auto fb = gray4_framebuffer::make(12, 3).value(); // stride 2

    TEST_ASSERT_EQUAL(2, fb.stride_bytes());
    TEST_ASSERT_EQUAL(2 * 3, fb.plane(0).size());
    TEST_ASSERT_EQUAL(2 * 3, fb.plane(1).size());

    // White is level 0: both planes all-zero.
    for (size_t p = 0; p < 2; ++p) {
        for (uint8_t byte : fb.plane(p)) {
            TEST_ASSERT_EQUAL_HEX8(0x00, byte);
        }
    }
    TEST_ASSERT_EQUAL(std::to_underlying(gray4::white), std::to_underlying(fb.get_pixel(0, 0)));
}

TEST_CASE("epaper gray4_framebuffer encodes levels across planes", "[idfxx][epaper]") {
    auto fb = gray4_framebuffer::make(12, 3).value();

    // dark = 0b10: bit 0 clear (plane 0), bit 1 set (plane 1).
    fb.set_pixel(1, 0, gray4::dark);
    TEST_ASSERT_EQUAL_HEX8(0x00, fb.plane(0)[0]);
    TEST_ASSERT_EQUAL_HEX8(0x40, fb.plane(1)[0]);
    TEST_ASSERT_EQUAL(std::to_underlying(gray4::dark), std::to_underlying(fb.get_pixel(1, 0)));

    // light = 0b01: bit 0 set (plane 0), bit 1 clear (plane 1).
    fb.set_pixel(9, 2, gray4::light);
    TEST_ASSERT_EQUAL_HEX8(0x40, fb.plane_row(0, 2)[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, fb.plane_row(1, 2)[1]);
    TEST_ASSERT_EQUAL(std::to_underlying(gray4::light), std::to_underlying(fb.get_pixel(9, 2)));

    // black = 0b11: both planes set.
    fb.set_pixel(1, 0, gray4::black);
    TEST_ASSERT_EQUAL_HEX8(0x40, fb.plane(0)[0]);
    TEST_ASSERT_EQUAL_HEX8(0x40, fb.plane(1)[0]);

    // Back to white clears both planes.
    fb.set_pixel(1, 0, gray4::white);
    TEST_ASSERT_EQUAL_HEX8(0x00, fb.plane(0)[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, fb.plane(1)[0]);

    fb.fill(gray4::black);
    for (size_t p = 0; p < 2; ++p) {
        for (uint8_t byte : fb.plane(p)) {
            TEST_ASSERT_EQUAL_HEX8(0xFF, byte);
        }
    }
}

// =============================================================================
// Runtime tests: panel state machine (via recording stub)
// =============================================================================

TEST_CASE("epaper panel write validates placement and mode", "[idfxx][epaper]") {
    recording_epaper_panel display(122, 250);
    auto fb = mono_framebuffer::make(122, 250).value();

    // Unaligned destination column.
    auto unaligned = display.try_write(fb, 4, 0);
    TEST_ASSERT_FALSE(unaligned.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), unaligned.error().value());

    // Out-of-bounds placement.
    auto oob = display.try_write(fb, 8, 0);
    TEST_ASSERT_FALSE(oob.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), oob.error().value());

    // Invalid row band.
    auto bad_rows = display.try_write_rows(fb, 10, 10);
    TEST_ASSERT_FALSE(bad_rows.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), bad_rows.error().value());

    // Grayscale framebuffer while in mono mode.
    auto gfb = gray4_framebuffer::make(122, 250).value();
    auto wrong_mode = display.try_write(gfb);
    TEST_ASSERT_FALSE(wrong_mode.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), wrong_mode.error().value());

    TEST_ASSERT_EQUAL(0, display.mono_writes.size());
    TEST_ASSERT_EQUAL(0, display.gray_writes.size());

    // A valid full write reaches the driver hook with the full row range.
    TEST_ASSERT_TRUE(display.try_write(fb).has_value());
    TEST_ASSERT_EQUAL(1, display.mono_writes.size());
    TEST_ASSERT_EQUAL(0, display.mono_writes[0].x);
    TEST_ASSERT_EQUAL(0, display.mono_writes[0].y);
    TEST_ASSERT_EQUAL(0, display.mono_writes[0].row_start);
    TEST_ASSERT_EQUAL(250, display.mono_writes[0].row_end);
}

TEST_CASE("epaper framebuffer flush maps to panel writes", "[idfxx][epaper]") {
    recording_epaper_panel display(128, 64);

    auto fb = mono_framebuffer::make(128, 16).value();
    TEST_ASSERT_TRUE(fb.try_flush(display, 0, 32).has_value());
    TEST_ASSERT_EQUAL(1, display.mono_writes.size());
    TEST_ASSERT_EQUAL(0, display.mono_writes[0].x);
    TEST_ASSERT_EQUAL(32, display.mono_writes[0].y);
    TEST_ASSERT_EQUAL(0, display.mono_writes[0].row_start);
    TEST_ASSERT_EQUAL(16, display.mono_writes[0].row_end);

    auto full = mono_framebuffer::make(128, 64).value();
    TEST_ASSERT_TRUE(full.try_flush_rows(display, 8, 24).has_value());
    TEST_ASSERT_EQUAL(2, display.mono_writes.size());
    TEST_ASSERT_EQUAL(0, display.mono_writes[1].x);
    TEST_ASSERT_EQUAL(0, display.mono_writes[1].y);
    TEST_ASSERT_EQUAL(8, display.mono_writes[1].row_start);
    TEST_ASSERT_EQUAL(24, display.mono_writes[1].row_end);
}

TEST_CASE("epaper panel promotes partial refresh without a baseline", "[idfxx][epaper]") {
    recording_epaper_panel display(122, 250);

    // No baseline yet: partial promotes to full.
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::partial).has_value());
    TEST_ASSERT_EQUAL(1, display.refreshes.size());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::full), std::to_underlying(display.refreshes[0]));

    // With a baseline, partial stays partial.
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::partial).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::partial), std::to_underlying(display.refreshes[1]));

    // A color-mode change invalidates the baseline.
    TEST_ASSERT_TRUE(display.try_set_color_mode(color_mode::gray4).has_value());
    TEST_ASSERT_TRUE(display.try_set_color_mode(color_mode::mono).has_value());
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::partial).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::full), std::to_underlying(display.refreshes[2]));
}

TEST_CASE("epaper panel clear invalidates the partial baseline", "[idfxx][epaper]") {
    recording_epaper_panel display(122, 250);

    // Establish a baseline so partial refreshes stay partial.
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::full).has_value());
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::partial).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::partial), std::to_underlying(display.refreshes.back()));

    // Clearing reaches the driver and invalidates the baseline: the next
    // partial refresh is promoted to full.
    TEST_ASSERT_TRUE(display.try_clear().has_value());
    TEST_ASSERT_EQUAL(1, display.clears);
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::partial).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::full), std::to_underlying(display.refreshes.back()));

    // A sleeping panel rejects clears.
    TEST_ASSERT_TRUE(display.try_sleep().has_value());
    auto asleep = display.try_clear();
    TEST_ASSERT_FALSE(asleep.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), asleep.error().value());
    TEST_ASSERT_EQUAL(1, display.clears);
}

TEST_CASE("epaper panel forwards gray4 refresh modes to the driver", "[idfxx][epaper]") {
    recording_epaper_panel display(122, 250);
    TEST_ASSERT_TRUE(display.try_set_color_mode(color_mode::gray4).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(color_mode::gray4), std::to_underlying(display.color_mode()));

    // Refresh-mode capability (e.g. gray4 being full-only on current
    // drivers) is driver policy: the base forwards the mode unchanged.
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::full).has_value());
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::fast).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::fast), std::to_underlying(display.refreshes.back()));
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::partial).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::partial), std::to_underlying(display.refreshes.back()));

    // Gray4 writes are accepted in gray4 mode; mono writes are not.
    auto gfb = gray4_framebuffer::make(122, 250).value();
    TEST_ASSERT_TRUE(display.try_write(gfb).has_value());
    TEST_ASSERT_EQUAL(1, display.gray_writes.size());

    auto fb = mono_framebuffer::make(122, 250).value();
    auto wrong_mode = display.try_write(fb);
    TEST_ASSERT_FALSE(wrong_mode.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), wrong_mode.error().value());
}

TEST_CASE("epaper panel sleep blocks writes and refreshes until wake", "[idfxx][epaper]") {
    recording_epaper_panel display(122, 250);
    auto fb = mono_framebuffer::make(122, 250).value();

    TEST_ASSERT_TRUE(display.try_refresh().has_value());
    TEST_ASSERT_TRUE(display.try_sleep().has_value());
    TEST_ASSERT_TRUE(display.asleep());
    TEST_ASSERT_EQUAL(1, display.sleeps);

    // Sleeping again is a no-op.
    TEST_ASSERT_TRUE(display.try_sleep().has_value());
    TEST_ASSERT_EQUAL(1, display.sleeps);

    auto write = display.try_write(fb);
    TEST_ASSERT_FALSE(write.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), write.error().value());

    auto refresh = display.try_refresh();
    TEST_ASSERT_FALSE(refresh.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), refresh.error().value());

    TEST_ASSERT_TRUE(display.try_wake().has_value());
    TEST_ASSERT_FALSE(display.asleep());
    TEST_ASSERT_EQUAL(1, display.wakes);
    TEST_ASSERT_TRUE(display.try_write(fb).has_value());

    // Wake invalidated the baseline: the next partial promotes to full.
    TEST_ASSERT_TRUE(display.try_refresh(refresh_mode::partial).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::full), std::to_underlying(display.refreshes.back()));
}
