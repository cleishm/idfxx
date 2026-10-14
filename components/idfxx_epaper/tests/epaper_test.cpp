// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_epaper
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/epaper/color"
#include "idfxx/epaper/color6_framebuffer"
#include "idfxx/epaper/gray4_framebuffer"
#include "idfxx/epaper/mono_framebuffer"
#include "idfxx/epaper/panel"
#include "idfxx/gfx"
#include "recording_epaper_panel.hpp"
#include "unity.h"

#include <chrono>
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
static_assert(std::is_copy_constructible_v<color6_framebuffer>);
static_assert(std::is_move_constructible_v<color6_framebuffer>);

// All framebuffers are drawable surfaces for the idfxx_gfx primitives.
static_assert(idfxx::gfx::pixel_surface<mono_framebuffer>);
static_assert(idfxx::gfx::pixel_surface<gray4_framebuffer>);
static_assert(idfxx::gfx::pixel_surface<color6_framebuffer>);

// A value-initialized pixel is blank paper, so gfx::canvas::clear() blanks
// ePaper surfaces to white.
static_assert(gray4{} == gray4::white);
static_assert(bool{} == false); // mono: false = white paper
// ...except six-color, whose panel-native code 0 is black: the framebuffer's
// own clear() blanks to white, and windowed canvas clears ink black.
static_assert(color6{} == color6::black);

// Gray levels are the canonical 2-bit values streamed to controller planes.
static_assert(std::to_underlying(gray4::white) == 0);
static_assert(std::to_underlying(gray4::light) == 1);
static_assert(std::to_underlying(gray4::dark) == 2);
static_assert(std::to_underlying(gray4::black) == 3);

// Six-color values are the panel-native 4-bit Spectra 6 pixel codes.
static_assert(std::to_underlying(color6::black) == 0x0);
static_assert(std::to_underlying(color6::white) == 0x1);
static_assert(std::to_underlying(color6::yellow) == 0x2);
static_assert(std::to_underlying(color6::red) == 0x3);
static_assert(std::to_underlying(color6::blue) == 0x5);
static_assert(std::to_underlying(color6::green) == 0x6);

// Destination-column alignment follows each format's pixel packing.
static_assert(mono_framebuffer::x_alignment == 8);
static_assert(gray4_framebuffer::x_alignment == 8);
static_assert(color6_framebuffer::x_alignment == 2);

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

    // fill inks every pixel; the 4 padding bits past the right edge stay white.
    fb.fill(true);
    for (size_t y = 0; y < 3; ++y) {
        TEST_ASSERT_EQUAL_HEX8(0x00, fb.row(y)[0]);
        TEST_ASSERT_EQUAL_HEX8(0x0F, fb.row(y)[1]);
    }
    TEST_ASSERT_TRUE(fb.get_pixel(11, 2));

    // A byte-aligned width has no padding: every byte is inked.
    auto aligned = mono_framebuffer::make(16, 2).value();
    aligned.fill(true);
    for (uint8_t byte : aligned.data()) {
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

    // fill sets every pixel; the 4 padding bits past the right edge stay
    // white (clear) in both planes.
    fb.fill(gray4::black);
    for (size_t p = 0; p < 2; ++p) {
        for (size_t y = 0; y < 3; ++y) {
            TEST_ASSERT_EQUAL_HEX8(0xFF, fb.plane_row(p, y)[0]);
            TEST_ASSERT_EQUAL_HEX8(0xF0, fb.plane_row(p, y)[1]);
        }
    }
    TEST_ASSERT_EQUAL(std::to_underlying(gray4::black), std::to_underlying(fb.get_pixel(11, 2)));

    // light sets plane 0 only; its padding stays clear too.
    fb.fill(gray4::light);
    for (size_t y = 0; y < 3; ++y) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, fb.plane_row(0, y)[0]);
        TEST_ASSERT_EQUAL_HEX8(0xF0, fb.plane_row(0, y)[1]);
        TEST_ASSERT_EQUAL_HEX8(0x00, fb.plane_row(1, y)[0]);
        TEST_ASSERT_EQUAL_HEX8(0x00, fb.plane_row(1, y)[1]);
    }

    // A byte-aligned width has no padding: every byte is set.
    auto aligned = gray4_framebuffer::make(16, 2).value();
    aligned.fill(gray4::black);
    for (size_t p = 0; p < 2; ++p) {
        for (uint8_t byte : aligned.plane(p)) {
            TEST_ASSERT_EQUAL_HEX8(0xFF, byte);
        }
    }
}

// =============================================================================
// Runtime tests: color6_framebuffer
// =============================================================================

TEST_CASE("epaper color6_framebuffer::make rejects invalid dimensions", "[idfxx][epaper]") {
    auto zero_width = color6_framebuffer::make(0, 3);
    TEST_ASSERT_FALSE(zero_width.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), zero_width.error().value());

    auto zero_height = color6_framebuffer::make(5, 0);
    TEST_ASSERT_FALSE(zero_height.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), zero_height.error().value());
}

TEST_CASE("epaper color6_framebuffer starts all-white with nibble-padded rows", "[idfxx][epaper]") {
    auto fb = color6_framebuffer::make(5, 3);
    TEST_ASSERT_TRUE(fb.has_value());

    TEST_ASSERT_EQUAL(5, fb->width());
    TEST_ASSERT_EQUAL(3, fb->height());
    TEST_ASSERT_EQUAL(3, fb->stride_bytes()); // (5 + 1) / 2
    TEST_ASSERT_EQUAL(9, fb->data().size());

    // White is code 1 in both nibbles.
    for (uint8_t byte : fb->data()) {
        TEST_ASSERT_EQUAL_HEX8(0x11, byte);
    }
    TEST_ASSERT_EQUAL(std::to_underlying(color6::white), std::to_underlying(fb->get_pixel(0, 0)));
    // Out-of-range reads are white too.
    TEST_ASSERT_EQUAL(std::to_underlying(color6::white), std::to_underlying(fb->get_pixel(5, 0)));
}

TEST_CASE("epaper color6_framebuffer packs the even pixel in the high nibble", "[idfxx][epaper]") {
    auto fb = color6_framebuffer::make(5, 3).value(); // stride 3

    // (0, 0) is the high nibble of byte 0.
    fb.set_pixel(0, 0, color6::red);
    TEST_ASSERT_EQUAL_HEX8(0x31, fb.data()[0]);
    TEST_ASSERT_EQUAL(std::to_underlying(color6::red), std::to_underlying(fb.get_pixel(0, 0)));

    // (1, 0) is the low nibble of the same byte; the high nibble is kept.
    fb.set_pixel(1, 0, color6::blue);
    TEST_ASSERT_EQUAL_HEX8(0x35, fb.data()[0]);
    TEST_ASSERT_EQUAL(std::to_underlying(color6::blue), std::to_underlying(fb.get_pixel(1, 0)));

    // (4, 1) is the high nibble of the last byte of row 1; the padding
    // nibble past the right edge stays white.
    fb.set_pixel(4, 1, color6::green);
    TEST_ASSERT_EQUAL_HEX8(0x61, fb.row(1)[2]);
    TEST_ASSERT_EQUAL(std::to_underlying(color6::green), std::to_underlying(fb.get_pixel(4, 1)));
    TEST_ASSERT_EQUAL_HEX8(0x11, fb.row(0)[2]);
    TEST_ASSERT_EQUAL_HEX8(0x11, fb.row(2)[2]);

    // Out-of-range writes are ignored (including the padding nibble's x).
    fb.set_pixel(5, 1, color6::black);
    fb.set_pixel(0, 3, color6::black);
    TEST_ASSERT_EQUAL_HEX8(0x61, fb.row(1)[2]);
    TEST_ASSERT_EQUAL_HEX8(0x35, fb.data()[0]);
}

TEST_CASE("epaper color6_framebuffer fill and clear", "[idfxx][epaper]") {
    auto fb = color6_framebuffer::make(5, 3).value();

    // fill colors every pixel; the padding nibble past the right edge stays
    // white.
    fb.fill(color6::yellow);
    for (size_t y = 0; y < 3; ++y) {
        TEST_ASSERT_EQUAL_HEX8(0x22, fb.row(y)[0]);
        TEST_ASSERT_EQUAL_HEX8(0x22, fb.row(y)[1]);
        TEST_ASSERT_EQUAL_HEX8(0x21, fb.row(y)[2]);
    }

    // An even width has no padding nibble: every byte is colored.
    auto even = color6_framebuffer::make(4, 2).value();
    even.fill(color6::yellow);
    for (uint8_t byte : even.data()) {
        TEST_ASSERT_EQUAL_HEX8(0x22, byte);
    }
    TEST_ASSERT_EQUAL(std::to_underlying(color6::yellow), std::to_underlying(fb.get_pixel(3, 2)));

    fb.clear();
    for (uint8_t byte : fb.data()) {
        TEST_ASSERT_EQUAL_HEX8(0x11, byte);
    }

    // A whole-surface canvas clear goes through the framebuffer's clear()
    // and so blanks to white rather than the value-initialized black.
    fb.fill(color6::black);
    idfxx::gfx::canvas canvas(fb);
    canvas.clear();
    TEST_ASSERT_EQUAL_HEX8(0x11, fb.data()[0]);
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

TEST_CASE("epaper panel write places the origin and write_rows forwards the band", "[idfxx][epaper]") {
    recording_epaper_panel display(128, 64);

    auto fb = mono_framebuffer::make(128, 16).value();
    TEST_ASSERT_TRUE(display.try_write(fb, 0, 32).has_value());
    TEST_ASSERT_EQUAL(1, display.mono_writes.size());
    TEST_ASSERT_EQUAL(0, display.mono_writes[0].x);
    TEST_ASSERT_EQUAL(32, display.mono_writes[0].y);
    TEST_ASSERT_EQUAL(0, display.mono_writes[0].row_start);
    TEST_ASSERT_EQUAL(16, display.mono_writes[0].row_end);

    auto full = mono_framebuffer::make(128, 64).value();
    TEST_ASSERT_TRUE(display.try_write_rows(full, 8, 24).has_value());
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

TEST_CASE("epaper panel rejects unsupported color modes before the driver", "[idfxx][epaper]") {
    recording_epaper_panel display(122, 250);
    display.supports_gray4 = false;
    display.supports_color6 = false;

    // Monochrome is always supported, whatever the driver reports.
    TEST_ASSERT_TRUE(display.supports(color_mode::mono));
    TEST_ASSERT_FALSE(display.supports(color_mode::gray4));
    TEST_ASSERT_FALSE(display.supports(color_mode::color6));

    // Unsupported modes fail without reaching the driver or changing state.
    auto gray = display.try_set_color_mode(color_mode::gray4);
    TEST_ASSERT_FALSE(gray.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::not_supported), gray.error().value());
    auto color = display.try_set_color_mode(color_mode::color6);
    TEST_ASSERT_FALSE(color.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::not_supported), color.error().value());
    TEST_ASSERT_EQUAL(0, display.color_mode_changes);
    TEST_ASSERT_EQUAL(std::to_underlying(color_mode::mono), std::to_underlying(display.color_mode()));

    // A supported mode reaches the driver.
    display.supports_color6 = true;
    TEST_ASSERT_TRUE(display.supports(color_mode::color6));
    TEST_ASSERT_TRUE(display.try_set_color_mode(color_mode::color6).has_value());
    TEST_ASSERT_EQUAL(1, display.color_mode_changes);
    TEST_ASSERT_EQUAL(std::to_underlying(color_mode::color6), std::to_underlying(display.color_mode()));
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

TEST_CASE("epaper panel color6 writes use even-column alignment", "[idfxx][epaper]") {
    recording_epaper_panel display(16, 4);
    auto cfb = color6_framebuffer::make(8, 4).value();

    // Six-color framebuffer while in mono mode.
    auto wrong_mode = display.try_write(cfb);
    TEST_ASSERT_FALSE(wrong_mode.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), wrong_mode.error().value());

    TEST_ASSERT_TRUE(display.try_set_color_mode(color_mode::color6).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(color_mode::color6), std::to_underlying(display.color_mode()));

    // An odd column is unaligned for the nibble-packed format...
    auto unaligned = display.try_write(cfb, 1, 0);
    TEST_ASSERT_FALSE(unaligned.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), unaligned.error().value());
    TEST_ASSERT_EQUAL(0, display.color6_writes.size());

    // ...but an even one that is not a multiple of 8 is fine.
    TEST_ASSERT_TRUE(display.try_write(cfb, 2, 0).has_value());
    TEST_ASSERT_EQUAL(1, display.color6_writes.size());
    TEST_ASSERT_EQUAL(2, display.color6_writes[0].x);
    TEST_ASSERT_EQUAL(0, display.color6_writes[0].y);
    TEST_ASSERT_EQUAL(0, display.color6_writes[0].row_start);
    TEST_ASSERT_EQUAL(4, display.color6_writes[0].row_end);

    // Band writes map through the same overload.
    TEST_ASSERT_TRUE(display.try_write_rows(cfb, 1, 3).has_value());
    TEST_ASSERT_EQUAL(2, display.color6_writes.size());
    TEST_ASSERT_EQUAL(0, display.color6_writes[1].x);
    TEST_ASSERT_EQUAL(1, display.color6_writes[1].row_start);
    TEST_ASSERT_EQUAL(3, display.color6_writes[1].row_end);

    // Mono framebuffers are rejected in color6 mode.
    auto fb = mono_framebuffer::make(8, 4).value();
    auto mono_in_color6 = display.try_write(fb);
    TEST_ASSERT_FALSE(mono_in_color6.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), mono_in_color6.error().value());

    // The 1-bpp formats keep their byte alignment: x = 2 is still rejected
    // for a mono framebuffer back in mono mode.
    TEST_ASSERT_TRUE(display.try_set_color_mode(color_mode::mono).has_value());
    auto mono_unaligned = display.try_write(fb, 2, 0);
    TEST_ASSERT_FALSE(mono_unaligned.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), mono_unaligned.error().value());
    TEST_ASSERT_EQUAL(0, display.mono_writes.size());
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

TEST_CASE("epaper panel start_refresh returns a completion future", "[idfxx][epaper]") {
    recording_epaper_panel display(122, 250);

    // Starts the refresh and hands back a future; with no BUSY line it is
    // already complete.
    auto started = display.try_start_refresh();
    TEST_ASSERT_TRUE(started.has_value());
    TEST_ASSERT_TRUE(started->valid());
    TEST_ASSERT_TRUE(started->done());
    TEST_ASSERT_TRUE(started->try_wait().has_value());
    TEST_ASSERT_EQUAL(1, display.refreshes.size());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::full), std::to_underlying(display.refreshes[0]));

    // Baseline handling matches refresh: a partial now stays partial, and
    // the first one after a clear is promoted.
    TEST_ASSERT_TRUE(display.try_start_refresh(refresh_mode::partial).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::partial), std::to_underlying(display.refreshes.back()));
    TEST_ASSERT_TRUE(display.try_clear().has_value());
    TEST_ASSERT_TRUE(display.try_start_refresh(refresh_mode::partial).has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(refresh_mode::full), std::to_underlying(display.refreshes.back()));

    // A sleeping panel cannot start a refresh.
    TEST_ASSERT_TRUE(display.try_sleep().has_value());
    auto asleep = display.try_start_refresh();
    TEST_ASSERT_FALSE(asleep.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), asleep.error().value());
    TEST_ASSERT_EQUAL(3, display.refreshes.size());
}

TEST_CASE("epaper panel refresh blocks and start_refresh does not", "[idfxx][epaper]") {
    recording_epaper_panel display(122, 250);

    // A blocking refresh settles before the driver hook and waits after it.
    TEST_ASSERT_TRUE(display.try_refresh().has_value());
    TEST_ASSERT_EQUAL(2, display.waits);
    TEST_ASSERT_EQUAL(1, display.hooks_before_last_wait);

    // start_refresh only settles beforehand: completion is the future's.
    TEST_ASSERT_TRUE(display.try_start_refresh().has_value());
    TEST_ASSERT_EQUAL(3, display.waits);
    TEST_ASSERT_EQUAL(1, display.hooks_before_last_wait);
    TEST_ASSERT_EQUAL(2, display.refreshes.size());
}

TEST_CASE("epaper panel settles an outstanding refresh before every controller hook", "[idfxx][epaper]") {
    recording_epaper_panel display(122, 250);
    auto fb = mono_framebuffer::make(122, 250).value();

    // Each command-issuing operation waits first, with the wait landing
    // before the hook runs.
    TEST_ASSERT_TRUE(display.try_write(fb).has_value());
    TEST_ASSERT_EQUAL(1, display.waits);
    TEST_ASSERT_EQUAL(0, display.hooks_before_last_wait);

    TEST_ASSERT_TRUE(display.try_write_rows(fb, 0, 8).has_value());
    TEST_ASSERT_EQUAL(2, display.waits);
    TEST_ASSERT_EQUAL(1, display.hooks_before_last_wait);

    TEST_ASSERT_TRUE(display.try_clear().has_value());
    TEST_ASSERT_EQUAL(3, display.waits);
    TEST_ASSERT_EQUAL(2, display.hooks_before_last_wait);

    TEST_ASSERT_TRUE(display.try_set_color_mode(color_mode::gray4).has_value());
    TEST_ASSERT_EQUAL(4, display.waits);
    // Setting the same mode again is a no-op and does not wait.
    TEST_ASSERT_TRUE(display.try_set_color_mode(color_mode::gray4).has_value());
    TEST_ASSERT_EQUAL(4, display.waits);

    TEST_ASSERT_TRUE(display.try_sleep().has_value());
    TEST_ASSERT_EQUAL(5, display.waits);
    TEST_ASSERT_EQUAL(3, display.hooks_before_last_wait);

    // Validation failures and wake do not wait.
    auto asleep = display.try_clear();
    TEST_ASSERT_FALSE(asleep.has_value());
    TEST_ASSERT_TRUE(display.try_wake().has_value());
    TEST_ASSERT_EQUAL(5, display.waits);
}
