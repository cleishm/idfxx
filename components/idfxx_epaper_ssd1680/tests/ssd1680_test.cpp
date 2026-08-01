// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_epaper_ssd1680
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// Hardware-independent tests only: constructing the driver requires a
// physical panel (BUSY handshake), so runtime coverage here is limited to
// configuration values.

#include "idfxx/epaper/panel"
#include "idfxx/epaper/ssd1680"
#include "unity.h"

#include <type_traits>

using namespace idfxx::epaper;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// The driver implements the abstract panel interface and is a concrete type.
static_assert(std::is_base_of_v<panel, ssd1680>);
static_assert(std::is_final_v<ssd1680>);
static_assert(!std::is_abstract_v<ssd1680>);

// Move-only, like all idfxx resource-owning types.
static_assert(!std::is_copy_constructible_v<ssd1680>);
static_assert(!std::is_copy_assignable_v<ssd1680>);
static_assert(std::is_move_constructible_v<ssd1680>);
static_assert(std::is_move_assignable_v<ssd1680>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("ssd1680 config defaults match the Seeed 2.13\" panel", "[idfxx][epaper][ssd1680]") {
    ssd1680::config cfg{};
    TEST_ASSERT_EQUAL(122, cfg.width);
    TEST_ASSERT_EQUAL(250, cfg.height);
    TEST_ASSERT_FALSE(cfg.reset_gpio.is_connected());
    TEST_ASSERT_FALSE(cfg.busy_gpio.is_connected());
    TEST_ASSERT_EQUAL(15'000, cfg.busy_timeout.count());
    TEST_ASSERT_FALSE(cfg.mirror_x);
    TEST_ASSERT_FALSE(cfg.mirror_y);
}

TEST_CASE("ssd1680::spi_io_config fills the controller's SPI framing", "[idfxx][epaper][ssd1680]") {
    auto io_cfg = ssd1680::spi_io_config(idfxx::gpio_1, idfxx::gpio_2);
    TEST_ASSERT_EQUAL(idfxx::gpio_1.num(), io_cfg.cs_gpio.num());
    TEST_ASSERT_EQUAL(idfxx::gpio_2.num(), io_cfg.dc_gpio.num());
    TEST_ASSERT_EQUAL(0, io_cfg.spi_mode);
    TEST_ASSERT_EQUAL(10'000'000, io_cfg.pclk_freq.count());
    TEST_ASSERT_EQUAL(10, io_cfg.trans_queue_depth);
    TEST_ASSERT_EQUAL(8, io_cfg.lcd_cmd_bits);
    TEST_ASSERT_EQUAL(8, io_cfg.lcd_param_bits);

    auto fast = ssd1680::spi_io_config(idfxx::gpio_1, idfxx::gpio_2, freq::hertz{20'000'000});
    TEST_ASSERT_EQUAL(20'000'000, fast.pclk_freq.count());
}
