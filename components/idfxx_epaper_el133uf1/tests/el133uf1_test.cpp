// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_epaper_el133uf1
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// Hardware-independent tests only: constructing the driver requires a
// physical panel (BUSY handshake) and a 960 KB PSRAM shadow frame, so
// runtime coverage here is limited to configuration values.

#include "idfxx/epaper/el133uf1"
#include "idfxx/epaper/panel"
#include "unity.h"

#include <type_traits>

using namespace idfxx::epaper;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// The driver implements the abstract panel interface and is a concrete type.
static_assert(std::is_base_of_v<panel, el133uf1>);
static_assert(std::is_final_v<el133uf1>);
static_assert(!std::is_abstract_v<el133uf1>);

// Move-only, like all idfxx resource-owning types.
static_assert(!std::is_copy_constructible_v<el133uf1>);
static_assert(!std::is_copy_assignable_v<el133uf1>);
static_assert(std::is_move_constructible_v<el133uf1>);
static_assert(std::is_move_assignable_v<el133uf1>);

// The 13.3" Spectra 6 glass is 1200x1600 in its portrait-native orientation.
static_assert(el133uf1::native_width == 1200);
static_assert(el133uf1::native_height == 1600);

// The config requires both chip-select pins, the D/C pin and the BUSY pin.
static_assert(!std::is_default_constructible_v<el133uf1::config>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("el133uf1 config defaults", "[idfxx][epaper][el133uf1]") {
    el133uf1::config cfg{
        .master_cs_gpio = idfxx::gpio_1,
        .slave_cs_gpio = idfxx::gpio_2,
        .dc_gpio = idfxx::gpio_3,
        .busy_gpio = idfxx::gpio_4,
    };
    TEST_ASSERT_TRUE(cfg.master_cs_gpio == idfxx::gpio_1);
    TEST_ASSERT_TRUE(cfg.slave_cs_gpio == idfxx::gpio_2);
    TEST_ASSERT_TRUE(cfg.dc_gpio == idfxx::gpio_3);
    TEST_ASSERT_FALSE(cfg.reset_gpio.is_connected());
    TEST_ASSERT_TRUE(cfg.busy_gpio == idfxx::gpio_4);
    // The presence check needs MISO wired, so it is opt-in.
    TEST_ASSERT_FALSE(cfg.check_presence);
    // The controllers accept up to 10 MHz for writes.
    TEST_ASSERT_EQUAL(10'000'000, cfg.pclk.count());
    // A full six-color refresh takes 25-30 s.
    TEST_ASSERT_EQUAL(60'000, cfg.busy_timeout.count());
}
