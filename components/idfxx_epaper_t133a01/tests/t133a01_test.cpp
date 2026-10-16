// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_epaper_t133a01
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// Hardware-independent tests only: constructing the driver requires a
// physical panel (BUSY handshake) and a 960 KB PSRAM shadow frame, so
// runtime coverage here is limited to configuration values.

#include "idfxx/epaper/panel"
#include "idfxx/epaper/t133a01"
#include "unity.h"

#include <type_traits>

using namespace idfxx::epaper;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// The driver implements the abstract panel interface and is a concrete type.
static_assert(std::is_base_of_v<panel, t133a01>);
static_assert(std::is_final_v<t133a01>);
static_assert(!std::is_abstract_v<t133a01>);

// Move-only, like all idfxx resource-owning types.
static_assert(!std::is_copy_constructible_v<t133a01>);
static_assert(!std::is_copy_assignable_v<t133a01>);
static_assert(std::is_move_constructible_v<t133a01>);
static_assert(std::is_move_assignable_v<t133a01>);

// The 13.3" Spectra 6 glass is 1200x1600 in its portrait-native orientation.
static_assert(t133a01::native_width == 1200);
static_assert(t133a01::native_height == 1600);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("t133a01 config defaults", "[idfxx][epaper][t133a01]") {
    t133a01::config cfg{};
    TEST_ASSERT_FALSE(cfg.reset_gpio.is_connected());
    TEST_ASSERT_FALSE(cfg.busy_gpio.is_connected());
    // A full six-color refresh takes 30-40 s.
    TEST_ASSERT_EQUAL(60'000, cfg.busy_timeout.count());
}

TEST_CASE("t133a01::spi_io_config fills the controller's SPI framing", "[idfxx][epaper][t133a01]") {
    auto io_cfg = t133a01::spi_io_config(idfxx::gpio_1, idfxx::gpio_2);
    TEST_ASSERT_EQUAL(idfxx::gpio_1.num(), io_cfg.cs_gpio.num());
    TEST_ASSERT_EQUAL(idfxx::gpio_2.num(), io_cfg.dc_gpio.num());
    TEST_ASSERT_EQUAL(0, io_cfg.spi_mode);
    TEST_ASSERT_EQUAL(10'000'000, io_cfg.pclk_freq.count());
    TEST_ASSERT_EQUAL(10, io_cfg.trans_queue_depth);
    TEST_ASSERT_EQUAL(8, io_cfg.lcd_cmd_bits);
    TEST_ASSERT_EQUAL(8, io_cfg.lcd_param_bits);

    auto slow = t133a01::spi_io_config(idfxx::gpio_1, idfxx::gpio_2, quanta::frequency::hertz{4'000'000});
    TEST_ASSERT_EQUAL(4'000'000, slow.pclk_freq.count());
}
