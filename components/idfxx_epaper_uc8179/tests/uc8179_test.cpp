// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_epaper_uc8179
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// Hardware-independent tests only: constructing the driver requires a
// physical panel (BUSY handshake), so runtime coverage here is limited to
// configuration values.

#include "idfxx/epaper/panel"
#include "idfxx/epaper/uc8179"
#include "unity.h"

#include <type_traits>

using namespace idfxx::epaper;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// The driver implements the abstract panel interface and is a concrete type.
static_assert(std::is_base_of_v<panel, uc8179>);
static_assert(std::is_final_v<uc8179>);
static_assert(!std::is_abstract_v<uc8179>);

// Move-only, like all idfxx resource-owning types.
static_assert(!std::is_copy_constructible_v<uc8179>);
static_assert(!std::is_copy_assignable_v<uc8179>);
static_assert(std::is_move_constructible_v<uc8179>);
static_assert(std::is_move_assignable_v<uc8179>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("uc8179 config defaults match the 7.5\" GDEY075T7 panel", "[idfxx][epaper][uc8179]") {
    uc8179::config cfg{};
    TEST_ASSERT_EQUAL(800, cfg.width);
    TEST_ASSERT_EQUAL(480, cfg.height);
    TEST_ASSERT_FALSE(cfg.reset_gpio.is_connected());
    TEST_ASSERT_FALSE(cfg.busy_gpio.is_connected());
    TEST_ASSERT_EQUAL(30'000, cfg.busy_timeout.count());
}

TEST_CASE("uc8179::spi_io_config fills the controller's SPI framing", "[idfxx][epaper][uc8179]") {
    auto io_cfg = uc8179::spi_io_config(idfxx::gpio_1, idfxx::gpio_2);
    TEST_ASSERT_EQUAL(idfxx::gpio_1.num(), io_cfg.cs_gpio.num());
    TEST_ASSERT_EQUAL(idfxx::gpio_2.num(), io_cfg.dc_gpio.num());
    TEST_ASSERT_EQUAL(0, io_cfg.spi_mode);
    TEST_ASSERT_EQUAL(10'000'000, io_cfg.pclk_freq.count());
    TEST_ASSERT_EQUAL(10, io_cfg.trans_queue_depth);
    TEST_ASSERT_EQUAL(8, io_cfg.lcd_cmd_bits);
    TEST_ASSERT_EQUAL(8, io_cfg.lcd_param_bits);

    auto slow = uc8179::spi_io_config(idfxx::gpio_1, idfxx::gpio_2, freq::hertz{4'000'000});
    TEST_ASSERT_EQUAL(4'000'000, slow.pclk_freq.count());
}
