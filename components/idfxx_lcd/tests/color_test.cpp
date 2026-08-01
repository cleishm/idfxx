// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx lcd color types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/color"
#include "unity.h"

#include <esp_lcd_panel_io.h>
#include <utility>

using namespace idfxx::lcd;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// Enum values match ESP-IDF constants
static_assert(std::to_underlying(rgb_element_order::rgb) == LCD_RGB_ELEMENT_ORDER_RGB);
static_assert(std::to_underlying(rgb_element_order::bgr) == LCD_RGB_ELEMENT_ORDER_BGR);

static_assert(std::to_underlying(rgb_data_endian::big) == LCD_RGB_DATA_ENDIAN_BIG);
static_assert(std::to_underlying(rgb_data_endian::little) == LCD_RGB_DATA_ENDIAN_LITTLE);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("rgb_element_order enum values", "[idfxx][lcd]") {
    TEST_ASSERT_EQUAL(LCD_RGB_ELEMENT_ORDER_RGB, std::to_underlying(rgb_element_order::rgb));
    TEST_ASSERT_EQUAL(LCD_RGB_ELEMENT_ORDER_BGR, std::to_underlying(rgb_element_order::bgr));
}

TEST_CASE("rgb_data_endian enum values", "[idfxx][lcd]") {
    TEST_ASSERT_EQUAL(LCD_RGB_DATA_ENDIAN_BIG, std::to_underlying(rgb_data_endian::big));
    TEST_ASSERT_EQUAL(LCD_RGB_DATA_ENDIAN_LITTLE, static_cast<int>(rgb_data_endian::little));
}
