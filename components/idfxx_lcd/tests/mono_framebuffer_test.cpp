// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::lcd::mono_framebuffer
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/mono_framebuffer"
#include "unity.h"

#include <type_traits>
#include <utility>

using namespace idfxx::lcd;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// mono_framebuffer is a regular value type: copyable and movable
static_assert(std::is_copy_constructible_v<mono_framebuffer>);
static_assert(std::is_copy_assignable_v<mono_framebuffer>);
static_assert(std::is_move_constructible_v<mono_framebuffer>);
static_assert(std::is_move_assignable_v<mono_framebuffer>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("mono_framebuffer::make rejects invalid dimensions", "[idfxx][lcd]") {
    auto zero_width = mono_framebuffer::make(0, 64);
    TEST_ASSERT_FALSE(zero_width.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), zero_width.error().value());

    auto zero_height = mono_framebuffer::make(128, 0);
    TEST_ASSERT_FALSE(zero_height.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), zero_height.error().value());

    auto unaligned_height = mono_framebuffer::make(128, 12);
    TEST_ASSERT_FALSE(unaligned_height.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), unaligned_height.error().value());
}

TEST_CASE("mono_framebuffer::make creates a cleared framebuffer", "[idfxx][lcd]") {
    auto fb = mono_framebuffer::make(128, 64);
    TEST_ASSERT_TRUE(fb.has_value());

    TEST_ASSERT_EQUAL(128, fb->width());
    TEST_ASSERT_EQUAL(64, fb->height());
    TEST_ASSERT_EQUAL(128 * 64 / 8, fb->data().size());

    for (uint8_t byte : fb->data()) {
        TEST_ASSERT_EQUAL(0x00, byte);
    }
}

TEST_CASE("mono_framebuffer set_pixel uses page-packed layout", "[idfxx][lcd]") {
    constexpr size_t width = 128;
    auto fb = mono_framebuffer::make(width, 64);
    TEST_ASSERT_TRUE(fb.has_value());

    // Pixel (3, 10) lives in page 1 (rows 8-15), bit 2 of byte width + 3.
    fb->set_pixel(3, 10, true);
    TEST_ASSERT_EQUAL(0x04, fb->data()[width + 3]);

    // Pixel (0, 0) is bit 0 of byte 0.
    fb->set_pixel(0, 0, true);
    TEST_ASSERT_EQUAL(0x01, fb->data()[0]);

    // Pixel (127, 63) is bit 7 of the last byte.
    fb->set_pixel(127, 63, true);
    TEST_ASSERT_EQUAL(0x80, fb->data()[7 * width + 127]);

    // Clearing a pixel resets its bit.
    fb->set_pixel(3, 10, false);
    TEST_ASSERT_EQUAL(0x00, fb->data()[width + 3]);
}

TEST_CASE("mono_framebuffer set/get pixel round trip", "[idfxx][lcd]") {
    auto fb = mono_framebuffer::make(16, 16);
    TEST_ASSERT_TRUE(fb.has_value());

    TEST_ASSERT_FALSE(fb->get_pixel(5, 9));
    fb->set_pixel(5, 9, true);
    TEST_ASSERT_TRUE(fb->get_pixel(5, 9));

    // Neighbouring pixels are unaffected.
    TEST_ASSERT_FALSE(fb->get_pixel(4, 9));
    TEST_ASSERT_FALSE(fb->get_pixel(6, 9));
    TEST_ASSERT_FALSE(fb->get_pixel(5, 8));
    TEST_ASSERT_FALSE(fb->get_pixel(5, 10));

    fb->set_pixel(5, 9, false);
    TEST_ASSERT_FALSE(fb->get_pixel(5, 9));
}

TEST_CASE("mono_framebuffer out-of-range pixels are ignored", "[idfxx][lcd]") {
    auto fb = mono_framebuffer::make(16, 16);
    TEST_ASSERT_TRUE(fb.has_value());

    // Out-of-range writes are silently ignored...
    fb->set_pixel(16, 0, true);
    fb->set_pixel(0, 16, true);
    fb->set_pixel(1000, 1000, true);

    // ...and out-of-range reads return false.
    TEST_ASSERT_FALSE(fb->get_pixel(16, 0));
    TEST_ASSERT_FALSE(fb->get_pixel(0, 16));

    for (uint8_t byte : fb->data()) {
        TEST_ASSERT_EQUAL(0x00, byte);
    }
}

TEST_CASE("mono_framebuffer fill and clear", "[idfxx][lcd]") {
    auto fb = mono_framebuffer::make(32, 8);
    TEST_ASSERT_TRUE(fb.has_value());

    fb->fill(true);
    for (uint8_t byte : fb->data()) {
        TEST_ASSERT_EQUAL(0xFF, byte);
    }
    TEST_ASSERT_TRUE(fb->get_pixel(31, 7));

    fb->clear();
    for (uint8_t byte : fb->data()) {
        TEST_ASSERT_EQUAL(0x00, byte);
    }
    TEST_ASSERT_FALSE(fb->get_pixel(31, 7));
}
