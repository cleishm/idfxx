// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx sdcard
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/sdcard"
#include "sd_test_rig.hpp"
#include "unity.h"

#include <idfxx/gpio>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <algorithm>
#include <chrono>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef CONFIG_IDFXX_STD_FORMAT
#include <format>
#endif

using namespace idfxx;
using namespace idfxx::sdcard;
using namespace quanta::frequency_literals;
using namespace std::chrono_literals;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// card is not default constructible (must use make() or a constructor)
static_assert(!std::is_default_constructible_v<card>);

// card is not copyable (unique ownership of the SPI device or SDMMC slot)
static_assert(!std::is_copy_constructible_v<card>);
static_assert(!std::is_copy_assignable_v<card>);

// card is move-only, and moving never throws
static_assert(std::is_nothrow_move_constructible_v<card>);
static_assert(std::is_nothrow_move_assignable_v<card>);

// card_id is a copyable value
static_assert(std::is_copy_constructible_v<card_id>);
static_assert(std::is_copy_assignable_v<card_id>);

// make() returns result<card>
static_assert(std::is_same_v<
              decltype(card::make(std::declval<spi::master_bus&>(), std::declval<const card::spi_config&>())),
              result<card>>);

// spi_config::cs is required
static_assert(!std::is_default_constructible_v<card::spi_config>);

// discard is the tag constant selecting a discard in erase_sectors()
static_assert(std::is_same_v<decltype(discard), const discard_t>);

// bus_width values are the number of data lines
static_assert(std::to_underlying(bus_width::one) == 1);
static_assert(std::to_underlying(bus_width::four) == 4);
static_assert(std::to_underlying(bus_width::eight) == 8);

#if SOC_SDMMC_HOST_SUPPORTED
// slot values are the SDMMC host slot numbers
static_assert(std::to_underlying(card::slot::slot_0) == 0);
static_assert(std::to_underlying(card::slot::slot_1) == 1);

static_assert(std::is_same_v<decltype(card::make(std::declval<const card::sdmmc_config&>())), result<card>>);

#if SOC_SDMMC_USE_GPIO_MATRIX
// clk, cmd and d0 are required where the pins are routed through the GPIO matrix
static_assert(!std::is_default_constructible_v<card::sdmmc_config>);
#else
// esp32 uses the slots' dedicated pins, so there is nothing to require
static_assert(std::is_default_constructible_v<card::sdmmc_config>);
#endif
#endif

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("sdcard spi_config default initialization", "[idfxx][sdcard]") {
    card::spi_config cfg{.cs = gpio_14};
    TEST_ASSERT_TRUE(cfg.cs.get() == gpio_14);
    TEST_ASSERT_FALSE(cfg.card_detect.is_connected());
    TEST_ASSERT_FALSE(cfg.write_protect.is_connected());
    TEST_ASSERT_TRUE(cfg.write_protect_active_level == gpio::level::low);
    TEST_ASSERT_EQUAL_INT32(20'000'000, static_cast<int32_t>(cfg.max_frequency.count()));
}

TEST_CASE("sdcard to_string(card_type)", "[idfxx][sdcard]") {
    TEST_ASSERT_EQUAL_STRING("SDSC", to_string(card_type::sdsc).c_str());
    TEST_ASSERT_EQUAL_STRING("SDHC", to_string(card_type::sdhc).c_str());
    TEST_ASSERT_EQUAL_STRING("SDXC", to_string(card_type::sdxc).c_str());
    TEST_ASSERT_EQUAL_STRING("MMC", to_string(card_type::mmc).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(42)", to_string(static_cast<card_type>(42)).c_str());
}

TEST_CASE("sdcard to_string(bus_width)", "[idfxx][sdcard]") {
    TEST_ASSERT_EQUAL_STRING("1-bit", to_string(bus_width::one).c_str());
    TEST_ASSERT_EQUAL_STRING("4-bit", to_string(bus_width::four).c_str());
    TEST_ASSERT_EQUAL_STRING("8-bit", to_string(bus_width::eight).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(2)", to_string(static_cast<bus_width>(2)).c_str());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
TEST_CASE("sdcard formatters", "[idfxx][sdcard]") {
    TEST_ASSERT_EQUAL_STRING("SDHC", std::format("{}", card_type::sdhc).c_str());
    TEST_ASSERT_EQUAL_STRING("4-bit", std::format("{}", bus_width::four).c_str());
#if SOC_SDMMC_HOST_SUPPORTED
    TEST_ASSERT_EQUAL_STRING("slot_1", std::format("{}", card::slot::slot_1).c_str());
#endif
}
#endif

// =============================================================================
// SPI configuration validation
// These fail before the bus is used, so they run without a card (and in QEMU).
// =============================================================================

namespace {

namespace rig = idfxx::sdcard::test;

void assert_invalid_arg(const result<card>& r) {
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == errc::invalid_arg);
}

} // namespace

TEST_CASE("sdcard make over SPI rejects an unconnected cs", "[idfxx][sdcard]") {
    auto bus = rig::make_bus();
    assert_invalid_arg(card::make(bus, {.cs = gpio::nc()}));
}

TEST_CASE("sdcard make over SPI rejects shared pins", "[idfxx][sdcard]") {
    auto bus = rig::make_bus();
    assert_invalid_arg(card::make(bus, {.cs = rig::cs, .card_detect = rig::cs}));
    assert_invalid_arg(card::make(bus, {.cs = rig::cs, .write_protect = rig::cs}));
    // Rejected before any pin is configured, so any pin other than cs will do
    assert_invalid_arg(card::make(bus, {.cs = rig::cs, .card_detect = gpio_15, .write_protect = gpio_15}));
}

TEST_CASE("sdcard make over SPI rejects a max_frequency below 400 kHz", "[idfxx][sdcard]") {
    auto bus = rig::make_bus();
    assert_invalid_arg(card::make(bus, {.cs = rig::cs, .max_frequency = 0_Hz}));
    assert_invalid_arg(card::make(bus, {.cs = rig::cs, .max_frequency = 399_kHz}));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("sdcard constructor over SPI throws on an invalid config", "[idfxx][sdcard]") {
    auto bus = rig::make_bus();
    bool threw = false;
    try {
        card c(bus, {.cs = gpio::nc()});
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == errc::invalid_arg);
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}
#endif

// =============================================================================
// SDMMC configuration validation
// These fail before the host is used, so they run without a card (and in QEMU).
// =============================================================================

#if SOC_SDMMC_HOST_SUPPORTED
#if SOC_SDMMC_USE_GPIO_MATRIX

namespace {

card::sdmmc_config one_bit_config() {
    return {.clk = gpio_14, .cmd = gpio_15, .d0 = gpio_2};
}

card::sdmmc_config four_bit_config() {
    return {.clk = gpio_14, .cmd = gpio_15, .d0 = gpio_2, .d1 = gpio_4, .d2 = gpio_12, .d3 = gpio_13};
}

} // namespace

TEST_CASE("sdcard sdmmc_config default initialization", "[idfxx][sdcard]") {
    auto cfg = one_bit_config();
    TEST_ASSERT_TRUE(cfg.slot == card::slot::slot_1);
    TEST_ASSERT_FALSE(cfg.d1.is_connected());
    TEST_ASSERT_FALSE(cfg.d7.is_connected());
    TEST_ASSERT_FALSE(cfg.width.has_value());
    TEST_ASSERT_FALSE(cfg.card_detect.is_connected());
    TEST_ASSERT_FALSE(cfg.write_protect.is_connected());
    TEST_ASSERT_TRUE(cfg.write_protect_active_level == gpio::level::low);
    TEST_ASSERT_FALSE(cfg.internal_pullups);
#if SOC_SDMMC_IO_POWER_EXTERNAL
    TEST_ASSERT_FALSE(cfg.io_power_ldo.has_value());
#endif
    TEST_ASSERT_EQUAL_INT32(20'000'000, static_cast<int32_t>(cfg.max_frequency.count()));
}

TEST_CASE("sdcard make over SDMMC rejects unconnected required pins", "[idfxx][sdcard]") {
    auto cfg = one_bit_config();
    cfg.clk = gpio::nc();
    assert_invalid_arg(card::make(cfg));
    cfg = one_bit_config();
    cfg.cmd = gpio::nc();
    assert_invalid_arg(card::make(cfg));
    cfg = one_bit_config();
    cfg.d0 = gpio::nc();
    assert_invalid_arg(card::make(cfg));
}

TEST_CASE("sdcard make over SDMMC rejects incomplete data pin groups", "[idfxx][sdcard]") {
    // D1 without D2 and D3
    auto cfg = one_bit_config();
    cfg.d1 = gpio_4;
    assert_invalid_arg(card::make(cfg));
    // D4-D7 without D1-D3
    cfg = one_bit_config();
    cfg.d4 = gpio_5;
    cfg.d5 = gpio_6;
    cfg.d6 = gpio_7;
    cfg.d7 = gpio_8;
    assert_invalid_arg(card::make(cfg));
    // D1-D3 with only part of D4-D7
    cfg = four_bit_config();
    cfg.d4 = gpio_5;
    assert_invalid_arg(card::make(cfg));
}

TEST_CASE("sdcard make over SDMMC rejects a width wider than the pins", "[idfxx][sdcard]") {
    auto cfg = one_bit_config();
    cfg.width = bus_width::four;
    assert_invalid_arg(card::make(cfg));
    cfg = four_bit_config();
    cfg.width = bus_width::eight;
    assert_invalid_arg(card::make(cfg));
    cfg = four_bit_config();
    cfg.width = static_cast<bus_width>(2);
    assert_invalid_arg(card::make(cfg));
}

TEST_CASE("sdcard make over SDMMC rejects shared pins", "[idfxx][sdcard]") {
    auto cfg = four_bit_config();
    cfg.d3 = gpio_2; // same as d0
    assert_invalid_arg(card::make(cfg));
    cfg = four_bit_config();
    cfg.card_detect = gpio_14; // same as clk
    assert_invalid_arg(card::make(cfg));
    cfg = four_bit_config();
    cfg.card_detect = gpio_16;
    cfg.write_protect = gpio_16;
    assert_invalid_arg(card::make(cfg));
}

TEST_CASE("sdcard make over SDMMC rejects an invalid slot or max_frequency", "[idfxx][sdcard]") {
    auto cfg = four_bit_config();
    cfg.slot = static_cast<card::slot>(2);
    assert_invalid_arg(card::make(cfg));
    cfg = four_bit_config();
    cfg.max_frequency = 0_Hz;
    assert_invalid_arg(card::make(cfg));
    cfg = four_bit_config();
    cfg.max_frequency = 100_kHz;
    assert_invalid_arg(card::make(cfg));
}

#if SOC_SDMMC_UHS_I_SUPPORTED && SOC_SDMMC_IO_POWER_EXTERNAL
TEST_CASE("sdcard make over SDMMC rejects uhs1 without io_power_ldo", "[idfxx][sdcard]") {
    auto cfg = four_bit_config();
    cfg.uhs1 = true;
    assert_invalid_arg(card::make(cfg));
}
#endif

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("sdcard constructor over SDMMC throws on an invalid config", "[idfxx][sdcard]") {
    auto cfg = one_bit_config();
    cfg.width = bus_width::four;
    bool threw = false;
    try {
        card c(cfg);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == errc::invalid_arg);
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}
#endif

#endif // SOC_SDMMC_USE_GPIO_MATRIX
#endif // SOC_SDMMC_HOST_SUPPORTED

// =============================================================================
// Hardware-dependent tests
// These need a card in the SD slot of the board chosen in tests/Kconfig.projbuild
// (see tests/sd_test_rig.hpp), and are ignored when its card-detect line
// reports an empty slot.
// =============================================================================

namespace {

// A sector near the end of the card, clear of any partition table or filesystem metadata at the start.
size_t scratch_sector(const card& c) {
    return c.sector_count() - 64;
}

} // namespace

TEST_CASE("sdcard reports identity and geometry", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::slot_power(true);
    {
        auto bus = rig::make_bus();
        auto c = rig::make_card(bus);

        // MMC is not supported over SPI
        TEST_ASSERT_TRUE(c.type() == card_type::sdsc || c.type() == card_type::sdhc || c.type() == card_type::sdxc);
        TEST_ASSERT_EQUAL_UINT32(512, c.sector_size());
        TEST_ASSERT_GREATER_THAN_UINT32(0, c.sector_count());
        TEST_ASSERT_TRUE(c.size() == uint64_t{c.sector_count()} * c.sector_size());
        if (c.type() == card_type::sdxc) {
            TEST_ASSERT_TRUE(c.size() >= (uint64_t{32} << 30));
        } else if (c.type() == card_type::sdhc) {
            TEST_ASSERT_TRUE(c.size() < (uint64_t{32} << 30));
        }

        TEST_ASSERT_TRUE(c.width() == bus_width::one);
        TEST_ASSERT_GREATER_THAN_INT32(0, static_cast<int32_t>(c.frequency().count()));
        TEST_ASSERT_LESS_OR_EQUAL_INT32(20'000'000, static_cast<int32_t>(c.frequency().count()));

        auto id = c.id();
        TEST_ASSERT_GREATER_THAN_UINT32(0, id.product_name().size());
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(5, id.product_name().size());
        auto date = id.manufacture_date();
        TEST_ASSERT_TRUE(date.has_value());
        TEST_ASSERT_GREATER_OR_EQUAL_INT32(2000, static_cast<int>(date->year()));

        TEST_ASSERT_NOT_NULL(c.idf_handle());
        TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(c.sector_count()), c.idf_handle()->csd.capacity);
        TEST_ASSERT_FALSE(c.supports_sanitize());
    }
    rig::slot_power(false);
}

TEST_CASE("sdcard is_present and is_write_protected read the slot pins", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::slot_power(true);
    {
        auto bus = rig::make_bus();
        auto c = rig::make_card(bus);
        TEST_ASSERT_TRUE(c.is_present());
        TEST_ASSERT_FALSE(c.is_write_protected()); // no write-protect pin
    }
    rig::slot_power(false);
}

TEST_CASE("sdcard probe succeeds while the card responds", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::slot_power(true);
    {
        auto bus = rig::make_bus();
        auto c = rig::make_card(bus);
        TEST_ASSERT_TRUE(c.try_probe().has_value());
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
        c.probe();
#endif
    }
    rig::slot_power(false);
}

TEST_CASE("sdcard move keeps the card structure in place", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::slot_power(true);
    {
        auto bus = rig::make_bus();
        auto c = rig::make_card(bus);
        sdmmc_card_t* handle = c.idf_handle();
        card moved = std::move(c);
        TEST_ASSERT_TRUE(moved.idf_handle() == handle);
        TEST_ASSERT_TRUE(moved.try_probe().has_value());
    }
    rig::slot_power(false);
}

TEST_CASE("sdcard raw read and write round-trip", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::slot_power(true);
    {
        auto bus = rig::make_bus();
        auto c = rig::make_card(bus);
        const size_t first = scratch_sector(c);
        const size_t bytes = 2 * c.sector_size();

        std::vector<uint8_t> original(bytes);
        TEST_ASSERT_TRUE(c.try_read_sectors(first, original).has_value());

        std::vector<uint8_t> pattern(bytes);
        for (size_t i = 0; i < bytes; ++i) {
            pattern[i] = static_cast<uint8_t>(i * 7 + 3);
        }
        TEST_ASSERT_TRUE(c.try_write_sectors(first, pattern).has_value());

        std::vector<uint8_t> readback(bytes);
        TEST_ASSERT_TRUE(c.try_read_sectors(first, readback.data(), readback.size()).has_value());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(pattern.data(), readback.data(), bytes);

        // Restore the original contents
        TEST_ASSERT_TRUE(c.try_write_sectors(first, original.data(), original.size()).has_value());
        TEST_ASSERT_TRUE(c.try_read_sectors(first, readback).has_value());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(original.data(), readback.data(), bytes);
    }
    rig::slot_power(false);
}

TEST_CASE("sdcard raw I/O rejects partial sectors and out-of-range sectors", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::slot_power(true);
    {
        auto bus = rig::make_bus();
        auto c = rig::make_card(bus);
        std::vector<uint8_t> buf(2 * c.sector_size());

        auto r = c.try_read_sectors(0, buf.data(), c.sector_size() + 1);
        TEST_ASSERT_TRUE(!r && r.error() == errc::invalid_size);
        r = c.try_write_sectors(0, buf.data(), 100);
        TEST_ASSERT_TRUE(!r && r.error() == errc::invalid_size);

        // The last sector is readable; one past it is not
        TEST_ASSERT_TRUE(c.try_read_sectors(c.sector_count() - 1, buf.data(), c.sector_size()).has_value());
        r = c.try_read_sectors(c.sector_count() - 1, buf);
        TEST_ASSERT_TRUE(!r && r.error() == errc::invalid_size);
        r = c.try_erase_sectors(c.sector_count(), 1);
        TEST_ASSERT_TRUE(!r && r.error() == errc::invalid_size);

        // Empty operations succeed without contacting the card
        TEST_ASSERT_TRUE(c.try_read_sectors(c.sector_count() + 10, buf.data(), 0).has_value());
        TEST_ASSERT_TRUE(c.try_erase_sectors(c.sector_count() + 10, 0).has_value());

        // Sanitize is MMC-only, and the timeout must be positive
        auto s = c.try_sanitize(0ms);
        TEST_ASSERT_TRUE(!s && s.error() == errc::invalid_arg);
        s = c.try_sanitize(1s);
        TEST_ASSERT_TRUE(!s && s.error() == errc::not_supported);
    }
    rig::slot_power(false);
}

TEST_CASE("sdcard erase and discard", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::slot_power(true);
    {
        auto bus = rig::make_bus();
        std::vector<uint8_t> original;
        size_t first = 0;
        {
            auto c = rig::make_card(bus);
            first = scratch_sector(c);
            original.resize(c.sector_size());
            TEST_ASSERT_TRUE(c.try_read_sectors(first, original).has_value());

            // Make sure the erase has something to clear
            std::vector<uint8_t> pattern(c.sector_size(), 0x5A);
            TEST_ASSERT_TRUE(c.try_write_sectors(first, pattern).has_value());

            // SD cards support discard only on an SDMMC host
            TEST_ASSERT_FALSE(c.supports_discard());
            auto r = c.try_erase_sectors(first, 1, discard);
            TEST_ASSERT_TRUE(!r && r.error() == errc::not_supported);

            TEST_ASSERT_TRUE(c.try_erase_sectors(first, 1).has_value());
        }
        // Cards on an SPI bus may need initializing again after an erase
        {
            auto c = rig::make_card(bus);
            std::vector<uint8_t> erased(c.sector_size());
            TEST_ASSERT_TRUE(c.try_read_sectors(first, erased).has_value());
            const uint8_t fill = erased[0];
            TEST_ASSERT_TRUE(fill == 0x00 || fill == 0xFF);
            TEST_ASSERT_TRUE(std::all_of(erased.begin(), erased.end(), [fill](uint8_t b) { return b == fill; }));

            TEST_ASSERT_TRUE(c.try_write_sectors(first, original).has_value());
            TEST_ASSERT_TRUE(c.try_read_sectors(first, erased).has_value());
            TEST_ASSERT_EQUAL_UINT8_ARRAY(original.data(), erased.data(), original.size());
        }
    }
    rig::slot_power(false);
}
