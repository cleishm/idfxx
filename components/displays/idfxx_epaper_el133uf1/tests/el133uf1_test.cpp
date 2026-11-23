// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_epaper_el133uf1
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// Constructing the driver needs a real SPI bus, an idle BUSY line and a
// 960 KB PSRAM shadow frame, so the hardware-independent tests cover only
// configuration values. The [hw] tests run on a board with no panel
// attached, with BUSY pulled up so it reads idle.

#include "idfxx/epaper/el133uf1"
#include "idfxx/epaper/panel"
#include "unity.h"

#include <concepts>
#include <type_traits>
#include <utility>

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

// Fast-refresh quality levels, ordered highest to lowest (slowest to fastest).
static_assert(std::is_same_v<std::underlying_type_t<el133uf1::fast_quality>, uint8_t>);
static_assert(el133uf1::fast_quality::high < el133uf1::fast_quality::draft);

// Per-refresh quality overloads, alongside (not hiding) the panel's own.
static_assert(requires(el133uf1& d) {
    { d.try_refresh(el133uf1::fast_quality::medium) } -> std::same_as<idfxx::result<void>>;
    { d.try_refresh(refresh_mode::fast) } -> std::same_as<idfxx::result<void>>;
    { d.try_refresh() } -> std::same_as<idfxx::result<void>>;
    {
        d.try_start_refresh(el133uf1::fast_quality::draft)
    } -> std::same_as<idfxx::result<idfxx::future<void>>>;
    { d.try_start_refresh(refresh_mode::full) } -> std::same_as<idfxx::result<idfxx::future<void>>>;
});
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(requires(el133uf1& d) {
    { d.refresh(el133uf1::fast_quality::low) } -> std::same_as<void>;
    { d.refresh(refresh_mode::fast) } -> std::same_as<void>;
    { d.refresh() } -> std::same_as<void>;
    { d.start_refresh(el133uf1::fast_quality::high) } -> std::same_as<idfxx::future<void>>;
    { d.start_refresh() } -> std::same_as<idfxx::future<void>>;
});
#endif

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
    // Fast refreshes default to the highest quality.
    TEST_ASSERT_TRUE(cfg.fast_refresh_quality == el133uf1::fast_quality::high);
}

// Drives a real SPI bus and allocates the PSRAM shadow frame, so it runs on hardware only. No panel
// is attached: BUSY is pulled up so it reads idle, and the commands go nowhere.
TEST_CASE("el133uf1 accepts full and fast refreshes, not partial", "[idfxx][epaper][el133uf1][hw]") {
    idfxx::spi::bus_config bus_cfg{};
    bus_cfg.mosi = idfxx::gpio_11;
    bus_cfg.sclk = idfxx::gpio_12;
    auto bus = idfxx::spi::master_bus::make(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);
    TEST_ASSERT_TRUE(bus.has_value());

    idfxx::gpio busy = idfxx::gpio_7;
    TEST_ASSERT_TRUE(busy.try_set_pull_mode(idfxx::gpio::pull_mode::pullup).has_value());

    auto display = el133uf1::make(*bus, {
        .master_cs_gpio = idfxx::gpio_10,
        .slave_cs_gpio = idfxx::gpio_9,
        .dc_gpio = idfxx::gpio_8,
        .busy_gpio = busy,
    });
    TEST_ASSERT_TRUE(display.has_value());

    TEST_ASSERT_TRUE(display->try_refresh(refresh_mode::full).has_value());
    TEST_ASSERT_TRUE(display->try_refresh(refresh_mode::fast).has_value());
    TEST_ASSERT_TRUE(display->try_refresh(el133uf1::fast_quality::draft).has_value());
    auto started = display->try_start_refresh(el133uf1::fast_quality::medium);
    TEST_ASSERT_TRUE(started.has_value());
    TEST_ASSERT_TRUE(started->try_wait().has_value());

    auto partial = display->try_refresh(refresh_mode::partial);
    TEST_ASSERT_FALSE(partial.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::not_supported), partial.error().value());
}
