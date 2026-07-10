// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::lcd::rgb565 and idfxx::lcd::rgb565_framebuffer
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/rgb565_framebuffer"
#include "recording_panel.hpp"
#include "unity.h"

#include <array>
#include <bit>
#include <type_traits>
#include <utility>

using namespace idfxx::lcd;
using idfxx_lcd_test::recording_panel;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// rgb565 is a two-byte trivially copyable value type, so arrays of it can be
// handed to draw_bitmap directly.
static_assert(sizeof(rgb565) == 2);
static_assert(std::is_trivially_copyable_v<rgb565>);

// Component packing truncates to the 5-6-5 layout.
static_assert(rgb565{}.value() == 0x0000);
static_assert(rgb565(255, 255, 255).value() == 0xFFFF);
static_assert(rgb565(255, 0, 0).value() == 0xF800);
static_assert(rgb565(0, 255, 0).value() == 0x07E0);
static_assert(rgb565(0, 0, 255).value() == 0x001F);
static_assert(rgb565(0x08, 0x04, 0x08).value() == 0x0821); // lowest surviving bits

// from_value round-trips and equality compares colors.
static_assert(rgb565::from_value(0x1234).value() == 0x1234);
static_assert(rgb565::from_value(0xF800) == rgb565(255, 0, 0));
static_assert(rgb565(255, 0, 0) != rgb565(0, 0, 255));

// The stored representation is big-endian (panel byte order): most
// significant byte first, regardless of host endianness.
static_assert(std::bit_cast<std::array<unsigned char, 2>>(rgb565(255, 0, 0)) == std::array<unsigned char, 2>{0xF8, 0x00});
static_assert(std::bit_cast<std::array<unsigned char, 2>>(rgb565(0, 0, 255)) == std::array<unsigned char, 2>{0x00, 0x1F});

// rgb565_framebuffer is a regular value type: copyable and movable
static_assert(std::is_copy_constructible_v<rgb565_framebuffer>);
static_assert(std::is_copy_assignable_v<rgb565_framebuffer>);
static_assert(std::is_move_constructible_v<rgb565_framebuffer>);
static_assert(std::is_move_assignable_v<rgb565_framebuffer>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("rgb565_framebuffer::make rejects invalid dimensions", "[idfxx][lcd]") {
    auto zero_width = rgb565_framebuffer::make(0, 320);
    TEST_ASSERT_FALSE(zero_width.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), zero_width.error().value());

    auto zero_height = rgb565_framebuffer::make(240, 0);
    TEST_ASSERT_FALSE(zero_height.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), zero_height.error().value());
}

TEST_CASE("rgb565_framebuffer::make creates a black framebuffer", "[idfxx][lcd]") {
    auto fb = rgb565_framebuffer::make(240, 40);
    TEST_ASSERT_TRUE(fb.has_value());

    TEST_ASSERT_EQUAL(240, fb->width());
    TEST_ASSERT_EQUAL(40, fb->height());
    TEST_ASSERT_EQUAL(240 * 40, fb->data().size());

    for (rgb565 px : fb->data()) {
        TEST_ASSERT_TRUE(px == rgb565{});
    }
}

TEST_CASE("rgb565_framebuffer set_pixel uses row-major layout", "[idfxx][lcd]") {
    constexpr size_t width = 32;
    auto fb = rgb565_framebuffer::make(width, 16);
    TEST_ASSERT_TRUE(fb.has_value());

    constexpr rgb565 red(255, 0, 0);
    fb->set_pixel(3, 10, red);
    TEST_ASSERT_TRUE(fb->data()[10 * width + 3] == red);

    fb->set_pixel(0, 0, red);
    TEST_ASSERT_TRUE(fb->data()[0] == red);

    fb->set_pixel(31, 15, red);
    TEST_ASSERT_TRUE(fb->data()[15 * width + 31] == red);
}

TEST_CASE("rgb565_framebuffer set/get pixel round trip", "[idfxx][lcd]") {
    auto fb = rgb565_framebuffer::make(16, 16);
    TEST_ASSERT_TRUE(fb.has_value());

    constexpr rgb565 amber(255, 191, 0);
    TEST_ASSERT_TRUE(fb->get_pixel(5, 9) == rgb565{});
    fb->set_pixel(5, 9, amber);
    TEST_ASSERT_TRUE(fb->get_pixel(5, 9) == amber);

    // Neighbouring pixels are unaffected.
    TEST_ASSERT_TRUE(fb->get_pixel(4, 9) == rgb565{});
    TEST_ASSERT_TRUE(fb->get_pixel(6, 9) == rgb565{});
    TEST_ASSERT_TRUE(fb->get_pixel(5, 8) == rgb565{});
    TEST_ASSERT_TRUE(fb->get_pixel(5, 10) == rgb565{});
}

TEST_CASE("rgb565_framebuffer out-of-range pixels are ignored", "[idfxx][lcd]") {
    auto fb = rgb565_framebuffer::make(16, 16);
    TEST_ASSERT_TRUE(fb.has_value());

    constexpr rgb565 white(255, 255, 255);

    // Out-of-range writes are silently ignored...
    fb->set_pixel(16, 0, white);
    fb->set_pixel(0, 16, white);
    fb->set_pixel(1000, 1000, white);

    // ...and out-of-range reads return black.
    TEST_ASSERT_TRUE(fb->get_pixel(16, 0) == rgb565{});
    TEST_ASSERT_TRUE(fb->get_pixel(0, 16) == rgb565{});

    for (rgb565 px : fb->data()) {
        TEST_ASSERT_TRUE(px == rgb565{});
    }
}

TEST_CASE("rgb565_framebuffer fill and clear", "[idfxx][lcd]") {
    auto fb = rgb565_framebuffer::make(32, 8);
    TEST_ASSERT_TRUE(fb.has_value());

    constexpr rgb565 green(0, 255, 0);
    fb->fill(green);
    for (rgb565 px : fb->data()) {
        TEST_ASSERT_TRUE(px == green);
    }
    TEST_ASSERT_TRUE(fb->get_pixel(31, 7) == green);

    fb->clear();
    for (rgb565 px : fb->data()) {
        TEST_ASSERT_TRUE(px == rgb565{});
    }
    TEST_ASSERT_TRUE(fb->get_pixel(31, 7) == rgb565{});
}

TEST_CASE("rgb565_framebuffer flush draws the full frame at an offset", "[idfxx][lcd]") {
    auto fb = rgb565_framebuffer::make(240, 40);
    TEST_ASSERT_TRUE(fb.has_value());

    recording_panel display;

    // Default offset: top-left corner.
    TEST_ASSERT_TRUE(fb->try_flush(display).has_value());
    TEST_ASSERT_EQUAL(1, display.draws.size());
    TEST_ASSERT_EQUAL(0, display.draws[0].x_start);
    TEST_ASSERT_EQUAL(0, display.draws[0].y_start);
    TEST_ASSERT_EQUAL(240, display.draws[0].x_end);
    TEST_ASSERT_EQUAL(40, display.draws[0].y_end);
    TEST_ASSERT_EQUAL_PTR(fb->data().data(), display.draws[0].data);

    // A band flushed lower down the frame.
    display.draws.clear();
    TEST_ASSERT_TRUE(fb->try_flush(display, 0, 80).has_value());
    TEST_ASSERT_EQUAL(1, display.draws.size());
    TEST_ASSERT_EQUAL(80, display.draws[0].y_start);
    TEST_ASSERT_EQUAL(120, display.draws[0].y_end);
    TEST_ASSERT_EQUAL_PTR(fb->data().data(), display.draws[0].data);
}

TEST_CASE("rgb565_framebuffer flush_rows sends one transfer", "[idfxx][lcd]") {
    constexpr size_t width = 64;
    constexpr size_t height = 48;
    auto fb = rgb565_framebuffer::make(width, height);
    TEST_ASSERT_TRUE(fb.has_value());

    recording_panel display;

    TEST_ASSERT_TRUE(fb->try_flush_rows(display, 26, 30).has_value());
    TEST_ASSERT_EQUAL(1, display.draws.size());
    TEST_ASSERT_EQUAL(0, display.draws[0].x_start);
    TEST_ASSERT_EQUAL(26, display.draws[0].y_start);
    TEST_ASSERT_EQUAL(width, display.draws[0].x_end);
    TEST_ASSERT_EQUAL(30, display.draws[0].y_end);
    TEST_ASSERT_EQUAL_PTR(fb->data().data() + 26 * width, display.draws[0].data);

    // Invalid row ranges are rejected.
    auto empty = fb->try_flush_rows(display, 30, 30);
    TEST_ASSERT_FALSE(empty.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), empty.error().value());

    auto overflow = fb->try_flush_rows(display, 0, height + 1);
    TEST_ASSERT_FALSE(overflow.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), overflow.error().value());
}

TEST_CASE("rgb565_framebuffer flush_region sends one transfer per row", "[idfxx][lcd]") {
    constexpr size_t width = 64;
    constexpr size_t height = 48;
    auto fb = rgb565_framebuffer::make(width, height);
    TEST_ASSERT_TRUE(fb.has_value());

    recording_panel display;

    // Columns [3, 5) over rows [10, 13): one draw per row.
    TEST_ASSERT_TRUE(fb->try_flush_region(display, 3, 10, 5, 13).has_value());
    TEST_ASSERT_EQUAL(3, display.draws.size());
    for (size_t i = 0; i < 3; ++i) {
        size_t row = 10 + i;
        TEST_ASSERT_EQUAL(3, display.draws[i].x_start);
        TEST_ASSERT_EQUAL(row, display.draws[i].y_start);
        TEST_ASSERT_EQUAL(5, display.draws[i].x_end);
        TEST_ASSERT_EQUAL(row + 1, display.draws[i].y_end);
        TEST_ASSERT_EQUAL_PTR(fb->data().data() + row * width + 3, display.draws[i].data);
    }

    // A full-width region collapses to a single transfer.
    display.draws.clear();
    TEST_ASSERT_TRUE(fb->try_flush_region(display, 0, 0, width, height).has_value());
    TEST_ASSERT_EQUAL(1, display.draws.size());

    // Invalid column ranges are rejected.
    auto empty = fb->try_flush_region(display, 5, 0, 5, 8);
    TEST_ASSERT_FALSE(empty.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), empty.error().value());

    auto overflow = fb->try_flush_region(display, 0, 0, width + 1, 8);
    TEST_ASSERT_FALSE(overflow.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), overflow.error().value());
}
