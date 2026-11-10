// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx sdcard
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "../src/sdcard_dma.hpp"
#include "../src/sdcard_spi_clock.hpp"
#include "idfxx/sdcard"
#include "sd_test_rig.hpp"
#include "unity.h"

#include <idfxx/gpio>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <driver/spi_master.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <optional>
#include <soc/soc.h>
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

// bus_speed values are fixed, whichever modes the target has
static_assert(std::to_underlying(bus_speed::default_speed) == 0);
static_assert(std::to_underlying(bus_speed::high_speed) == 1);
#if SOC_SDMMC_UHS_I_SUPPORTED
static_assert(std::to_underlying(bus_speed::sdr50) == 2);
static_assert(std::to_underlying(bus_speed::ddr50) == 3);
#endif
#if SOC_SDMMC_UHS_I_SUPPORTED && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
static_assert(std::to_underlying(bus_speed::sdr104) == 4);
#endif

#if SOC_SDMMC_HOST_SUPPORTED
// slot values are the SDMMC host slot numbers
static_assert(std::to_underlying(card::slot::slot_0) == 0);
static_assert(std::to_underlying(card::slot::slot_1) == 1);

static_assert(std::is_same_v<decltype(card::make(std::declval<const card::sdmmc_config&>())), result<card>>);

// sdmmc_config selects a speed mode, with an optional clock limit
static_assert(std::is_same_v<decltype(card::sdmmc_config::speed), bus_speed>);
static_assert(std::is_same_v<decltype(card::sdmmc_config::max_frequency), std::optional<quanta::frequency::hertz>>);

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

TEST_CASE("sdcard to_string(bus_speed)", "[idfxx][sdcard]") {
    TEST_ASSERT_EQUAL_STRING("default speed", to_string(bus_speed::default_speed).c_str());
    TEST_ASSERT_EQUAL_STRING("high speed", to_string(bus_speed::high_speed).c_str());
#if SOC_SDMMC_UHS_I_SUPPORTED
    TEST_ASSERT_EQUAL_STRING("SDR50", to_string(bus_speed::sdr50).c_str());
    TEST_ASSERT_EQUAL_STRING("DDR50", to_string(bus_speed::ddr50).c_str());
#else
    // The UHS-I modes don't exist without UHS-I support
    TEST_ASSERT_EQUAL_STRING("unknown(2)", to_string(static_cast<bus_speed>(2)).c_str());
#endif
#if SOC_SDMMC_UHS_I_SUPPORTED && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    TEST_ASSERT_EQUAL_STRING("SDR104", to_string(bus_speed::sdr104).c_str());
#else
    TEST_ASSERT_EQUAL_STRING("unknown(4)", to_string(static_cast<bus_speed>(4)).c_str());
#endif
    TEST_ASSERT_EQUAL_STRING("unknown(42)", to_string(static_cast<bus_speed>(42)).c_str());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
TEST_CASE("sdcard formatters", "[idfxx][sdcard]") {
    TEST_ASSERT_EQUAL_STRING("SDHC", std::format("{}", card_type::sdhc).c_str());
    TEST_ASSERT_EQUAL_STRING("4-bit", std::format("{}", bus_width::four).c_str());
    TEST_ASSERT_EQUAL_STRING("high speed", std::format("{}", bus_speed::high_speed).c_str());
#if SOC_SDMMC_HOST_SUPPORTED
    TEST_ASSERT_EQUAL_STRING("slot_1", std::format("{}", card::slot::slot_1).c_str());
#endif
}
#endif

// =============================================================================
// SPI configuration validation
// These fail before the card is contacted, or fail for lack of a card, so they
// run without a card (and, unless tagged [hw], in QEMU), on an SPI2 bus with
// no pins.
// =============================================================================

namespace {

namespace rig = idfxx::sdcard::test;

// Pins for configurations that are rejected or that fail without a card. Most configurations are rejected before any
// pin is configured; those that pass the pin checks make spare_pin_a the card's chip select. None of them is a flash,
// PSRAM, USB, UART0 console, JTAG or strapping pin on its target.
#if CONFIG_IDF_TARGET_ESP32
constexpr gpio spare_pin_a = gpio_21;
constexpr gpio spare_pin_b = gpio_22;
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
constexpr gpio spare_pin_a = gpio_4;
constexpr gpio spare_pin_b = gpio_5;
#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2 || CONFIG_IDF_TARGET_ESP32P4
constexpr gpio spare_pin_a = gpio_0;
constexpr gpio spare_pin_b = gpio_1;
#else
#error "sdcard tests: choose spare pins for this target"
#endif

void assert_invalid_arg(const result<card>& r) {
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == errc::invalid_arg);
}

} // namespace

TEST_CASE("sdcard make over SPI rejects an unconnected cs", "[idfxx][sdcard]") {
    rig::unwired_bus_fixture bus;
    assert_invalid_arg(card::make(bus.get(), {.cs = gpio::nc()}));
}

TEST_CASE("sdcard make over SPI rejects shared pins", "[idfxx][sdcard]") {
    rig::unwired_bus_fixture bus;
    assert_invalid_arg(card::make(bus.get(), {.cs = spare_pin_a, .card_detect = spare_pin_a}));
    assert_invalid_arg(card::make(bus.get(), {.cs = spare_pin_a, .write_protect = spare_pin_a}));
    assert_invalid_arg(
        card::make(bus.get(), {.cs = spare_pin_a, .card_detect = spare_pin_b, .write_protect = spare_pin_b})
    );
}

TEST_CASE("sdcard make over SPI rejects a max_frequency below 400 kHz", "[idfxx][sdcard]") {
    rig::unwired_bus_fixture bus;
    assert_invalid_arg(card::make(bus.get(), {.cs = spare_pin_a, .max_frequency = 0_Hz}));
    assert_invalid_arg(card::make(bus.get(), {.cs = spare_pin_a, .max_frequency = 399_kHz}));
}

TEST_CASE("sdcard make over SPI rejects a bus without DMA", "[idfxx][sdcard]") {
    rig::unwired_bus_fixture bus(spi::dma_chan::disabled);
    // Without DMA, a transaction is limited to the SPI peripheral's own buffer, shorter than a data block
    TEST_ASSERT_LESS_THAN_UINT32(512, bus.get().max_transaction_length());
    // The check is made once the card's device is on the bus, and the device is removed again: were it left behind,
    // the bus would run out of device slots (six) and make() fail with invalid_state instead
    for (int i = 0; i < 8; ++i) {
        assert_invalid_arg(card::make(bus.get(), {.cs = spare_pin_a}));
    }
}

// Needs no card, but runs on hardware only: QEMU reads the pulled-up card-detect pin as low, and never completes the
// card initialization's SPI transfers on a DMA bus.
TEST_CASE("sdcard make over SPI accepts a bus with DMA", "[idfxx][sdcard][hw]") {
    rig::unwired_bus_fixture bus;
    // The configuration is accepted, so make() goes on to initialize the card, which fails: there is none. (The
    // card-detect pin, unconnected and pulled up, normally reports an empty slot before any bus traffic.)
    bool accepted = false;
    {
        auto r = card::make(bus.get(), {.cs = spare_pin_a, .card_detect = spare_pin_b});
        accepted = r.has_value() || r.error() != errc::invalid_arg;
    }
    TEST_ASSERT_TRUE(accepted);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("sdcard constructor over SPI throws on an invalid config", "[idfxx][sdcard]") {
    rig::unwired_bus_fixture bus;
    bool threw = false;
    try {
        card c(bus.get(), {.cs = gpio::nc()});
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == errc::invalid_arg);
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}
#endif

// =============================================================================
// SPI clock choice
// The clock a card on an SPI bus is configured with, checked against ESP-IDF's
// own calculation of the SPI peripheral's clock, against an ideal divider, and
// against models of how ESP-IDF's versions divide a clock source down first.
// =============================================================================

namespace {

namespace clk = idfxx::sdcard::internal;

// The SPI peripheral's clock for a request, as the SPI master driver calculates it on this target.
uint32_t idf_divide(uint32_t base_hz, uint32_t request_hz) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return static_cast<uint32_t>(spi_get_actual_clock(static_cast<int>(base_hz), static_cast<int>(request_hz), 128));
#pragma GCC diagnostic pop
}

// A divider that generates the nearest of base_hz / d, for any whole d, to the request (the lower one on a tie).
uint32_t ideal_divide(uint32_t base_hz, uint32_t request_hz) {
    const uint32_t d = std::max<uint32_t>(1, base_hz / request_hz);
    const uint32_t above = base_hz / d;
    const uint32_t below = base_hz / (d + 1);
    return above - request_hz < request_hz - below ? above : below;
}

using divide_fn = uint32_t (*)(uint32_t, uint32_t);

constexpr uint32_t mhz = 1'000'000;

// What the SPI clock must stay at or below.
uint32_t limit_of(int64_t cap_hz, std::optional<uint32_t> pin_limit_hz) {
    auto limit = static_cast<uint32_t>(std::min<int64_t>(cap_hz, clk::sd_high_speed_max_hz));
    return pin_limit_hz ? std::min(limit, *pin_limit_hz) : limit;
}

// Checks the choice for a source and limits: the generated clock is at or below the limit, also for a card without
// high speed (configured with 20 MHz instead), and is the highest that any request in whole kHz generates within those
// rules, found by trying every request from just below the chosen clock up. (No lower request generates more: each
// one generates the nearest clock the peripheral can.) Returns the choice.
clk::spi_clock_choice check_choice(
    const clk::spi_clock_source& src, int64_t cap_hz, std::optional<uint32_t> pin_limit_hz, divide_fn divide
) {
    const auto choice = clk::choose_spi_clock(src, cap_hz, pin_limit_hz, divide);
    TEST_ASSERT_TRUE(choice.has_value());
    const uint32_t limit = limit_of(cap_hz, pin_limit_hz);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(limit, choice->generated_hz);
    const auto generated = clk::spi_generated_clock(src, choice->request_khz * 1000, divide);
    TEST_ASSERT_TRUE(generated.has_value());
    TEST_ASSERT_EQUAL_UINT32(choice->generated_hz, *generated);

    const auto default_speed = clk::spi_generated_clock(src, clk::sdspi_default_speed_khz * 1000, divide);
    const bool high_speed_allowed = default_speed && *default_speed <= limit;
    // High speed exactly when the clock is above 20 MHz (for these sources, which generate nothing just above it)
    if (choice->request_khz > clk::sdspi_default_speed_khz) {
        TEST_ASSERT_TRUE(high_speed_allowed);
        TEST_ASSERT_GREATER_THAN_UINT32(clk::sdspi_default_speed_khz * 1000, choice->generated_hz);
    } else {
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(clk::sdspi_default_speed_khz * 1000, choice->generated_hz);
    }

    uint32_t best = 0;
    const uint32_t top_khz = std::min(limit, src.hz / src.pre_div) / 1000 + 1;
    for (uint32_t khz = std::max(clk::sdspi_probing_khz, choice->generated_hz / 1000 - 1); khz <= top_khz; ++khz) {
        if (khz > clk::sdspi_default_speed_khz && !high_speed_allowed) {
            continue;
        }
        const auto g = clk::spi_generated_clock(src, khz * 1000, divide);
        if (g && *g <= limit) {
            best = std::max(best, *g);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(best, choice->generated_hz);
    return *choice;
}

// Caps around the clocks an 80 MHz source generates, and around the limits.
constexpr int64_t caps[] = {
    400'000,    401'000,    1'000'000,  10'000'000, 19'999'999, 20'000'000, 20'000'001,    24'000'000,
    26'000'000, 26'666'665, 26'666'666, 26'666'667, 30'000'000, 40'000'000, 50'000'000, 1'000'000'000,
};

// How an ESP-IDF version picks the divider it applies to a clock source before the SPI peripheral, on targets that
// have one. The peripheral then divides the result as idf_divide() does (the same calculation as on esp32p4).
enum class pre_div_scheme {
    v5_5_0, // Rounds the source's ratio to twice the request, in frequency, and doubles it; up to 255 * 2.
    v5_5_1, // The smallest even divider, from spi_min_pre_div() up, that divides the source's ratio to the request.
    v6_1,   // As v5_5_1, but tries dividers up to 256 only.
};

// The clock such a driver generates for a request, or nullopt if it rejects the request.
std::optional<uint32_t> pre_divided_clock(pre_div_scheme scheme, uint32_t source_hz, uint32_t request_hz) {
    uint32_t div = 0;
    if (scheme == pre_div_scheme::v5_5_0) {
        if (request_hz > std::min<uint32_t>(source_hz / 2, 80 * mhz)) {
            return std::nullopt;
        }
        div = 1;
        if (source_hz / 2 > 80 * mhz) {
            // hal_utils_calc_clk_div_integer(), rounding, for twice the request, within [1, 256)
            const uint32_t exp = 2 * request_hz;
            div = source_hz / exp;
            if (source_hz % exp >= source_hz / (2 * div * (div + 1))) {
                ++div;
            }
            div = std::clamp<uint32_t>(div, 1, 255);
        }
        div *= 2;
    } else {
        const uint32_t min_div = clk::spi_min_pre_div(source_hz);
        const uint32_t total = source_hz / request_hz;
        const uint32_t max_div = scheme == pre_div_scheme::v6_1 ? std::min<uint32_t>(total, 256) : total;
        div = min_div;
        for (uint32_t d = min_div; d <= max_div; d += 2) {
            if (total % d == 0 && total / d <= 1024) {
                div = d;
                break;
            }
        }
    }
    const uint32_t base = source_hz / div;
    if (request_hz > base) {
        return std::nullopt;
    }
    return idf_divide(base, request_hz);
}

// Checks a choice on a source that the driver divides first against each ESP-IDF version's divider. ESP-IDF 5.5.0
// may generate a lower clock than predicted, for this request and for the 20 MHz one.
void check_pre_divided(uint32_t source_hz, int64_t cap_hz) {
    const clk::spi_clock_source src{.hz = source_hz, .pre_div = clk::spi_min_pre_div(source_hz)};
    const auto choice = check_choice(src, cap_hz, std::nullopt, idf_divide);
    const uint32_t limit = limit_of(cap_hz, std::nullopt);
    for (auto scheme : {pre_div_scheme::v5_5_1, pre_div_scheme::v6_1, pre_div_scheme::v5_5_0}) {
        const auto g = pre_divided_clock(scheme, source_hz, choice.request_khz * 1000);
        TEST_ASSERT_TRUE(g.has_value());
        if (scheme == pre_div_scheme::v5_5_0) {
            TEST_ASSERT_LESS_OR_EQUAL_UINT32(choice.generated_hz, *g);
        } else {
            TEST_ASSERT_EQUAL_UINT32(choice.generated_hz, *g);
        }
        if (choice.request_khz > clk::sdspi_default_speed_khz) {
            const auto fallback = pre_divided_clock(scheme, source_hz, clk::sdspi_default_speed_khz * 1000);
            TEST_ASSERT_TRUE(fallback.has_value());
            TEST_ASSERT_LESS_OR_EQUAL_UINT32(limit, *fallback);
        }
    }
}

} // namespace

TEST_CASE("sdcard SPI clock is the highest the bus generates within the limits", "[idfxx][sdcard]") {
    struct source {
        uint32_t hz;
        divide_fn divide;
    };
    // 80 MHz: esp32, esp32s2, esp32s3, esp32c3 and esp32c6; 48 MHz: esp32h2; 40 MHz: esp32c2. The ideal divider
    // generates clocks this target's peripheral can't.
    const source sources[] = {
        {80 * mhz, idf_divide},
        {48 * mhz, idf_divide},
        {40 * mhz, idf_divide},
        {44 * mhz, ideal_divide},
    };
    for (const auto& s : sources) {
        for (int64_t cap : caps) {
            check_choice({.hz = s.hz}, cap, std::nullopt, s.divide);
            // As limited by the pins of an esp32 bus on the GPIO matrix
            check_choice({.hz = s.hz}, cap, 26'666'666, s.divide);
        }
    }
}

TEST_CASE("sdcard SPI clock on an 80 MHz source", "[idfxx][sdcard]") {
    struct expected {
        int64_t cap;
        std::optional<uint32_t> pin_limit;
        uint32_t request_khz;
        uint32_t generated_hz;
    };
    const expected cases[] = {
        {400'000, std::nullopt, 400, 400'000},
        {10'000'000, std::nullopt, 10'000, 10'000'000},
        {20'000'000, std::nullopt, 20'000, 20'000'000},
        // Nothing between 20 MHz and 26.7 MHz, so the card stays in default speed
        {26'000'000, std::nullopt, 20'000, 20'000'000},
        {26'666'665, std::nullopt, 20'000, 20'000'000},
        {26'666'666, std::nullopt, 26'666, 26'666'666},
        {30'000'000, std::nullopt, 26'666, 26'666'666},
        {40'000'000, std::nullopt, 40'000, 40'000'000},
        // The SD high-speed limit: the driver would generate 80 MHz for anything above 60 MHz
        {50'000'000, std::nullopt, 40'000, 40'000'000},
        {1'000'000'000, std::nullopt, 40'000, 40'000'000},
        // An esp32 bus on the GPIO matrix
        {40'000'000, 26'666'666, 26'666, 26'666'666},
        {20'000'000, 26'666'666, 20'000, 20'000'000},
    };
    for (const auto& e : cases) {
        const auto choice = clk::choose_spi_clock({.hz = 80 * mhz}, e.cap, e.pin_limit, idf_divide);
        TEST_ASSERT_TRUE(choice.has_value());
        TEST_ASSERT_EQUAL_UINT32(e.request_khz, choice->request_khz);
        TEST_ASSERT_EQUAL_UINT32(e.generated_hz, choice->generated_hz);
    }
}

TEST_CASE("sdcard SPI clock on a source below the SD high-speed limit", "[idfxx][sdcard]") {
    // esp32h2's 48 MHz source: the driver rejects a request above it, so a 50 MHz cap gives the source itself
    auto choice = clk::choose_spi_clock({.hz = 48 * mhz}, 50'000'000, std::nullopt, idf_divide);
    TEST_ASSERT_TRUE(choice.has_value());
    TEST_ASSERT_EQUAL_UINT32(48'000, choice->request_khz);
    TEST_ASSERT_EQUAL_UINT32(48'000'000, choice->generated_hz);
    TEST_ASSERT_FALSE(clk::spi_generated_clock({.hz = 48 * mhz}, 48'000'001, idf_divide).has_value());

    choice = clk::choose_spi_clock({.hz = 48 * mhz}, 23'000'000, std::nullopt, idf_divide);
    TEST_ASSERT_TRUE(choice.has_value());
    TEST_ASSERT_EQUAL_UINT32(16'000'000, choice->generated_hz);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(clk::sdspi_default_speed_khz, choice->request_khz);
}

TEST_CASE("sdcard SPI clock on a source the driver divides first", "[idfxx][sdcard]") {
    TEST_ASSERT_EQUAL_UINT32(2, clk::spi_min_pre_div(48 * mhz));
    TEST_ASSERT_EQUAL_UINT32(2, clk::spi_min_pre_div(80 * mhz));
    TEST_ASSERT_EQUAL_UINT32(2, clk::spi_min_pre_div(160 * mhz));
    TEST_ASSERT_EQUAL_UINT32(6, clk::spi_min_pre_div(480 * mhz));

    // 480 MHz: esp32p4; 160 MHz: esp32c5 and esp32c61; 48 MHz: esp32h4. ESP-IDF 5.5.0's range of dividers runs out
    // below about 940 kHz on 480 MHz, so the lowest caps are only checked against the later versions.
    for (uint32_t source_hz : {480 * mhz, 160 * mhz, 48 * mhz}) {
        const clk::spi_clock_source src{.hz = source_hz, .pre_div = clk::spi_min_pre_div(source_hz)};
        for (int64_t cap : caps) {
            if (cap >= 1'000'000 || source_hz != 480 * mhz) {
                check_pre_divided(source_hz, cap);
            } else {
                check_choice(src, cap, std::nullopt, idf_divide);
            }
        }
    }

    // On esp32p4, 26 MHz gives 20 MHz, not the 26.7 MHz a request for 26 MHz would generate
    auto choice = clk::choose_spi_clock({.hz = 480 * mhz, .pre_div = 6}, 26'000'000, std::nullopt, idf_divide);
    TEST_ASSERT_TRUE(choice.has_value());
    TEST_ASSERT_EQUAL_UINT32(20'000'000, choice->generated_hz);
    choice = clk::choose_spi_clock({.hz = 480 * mhz, .pre_div = 6}, 50'000'000, std::nullopt, idf_divide);
    TEST_ASSERT_TRUE(choice.has_value());
    TEST_ASSERT_EQUAL_UINT32(40'000'000, choice->generated_hz);

    // A request that the driver could divide differently is not predicted
    TEST_ASSERT_FALSE(clk::spi_generated_clock({.hz = 480 * mhz, .pre_div = 6}, 30'000'000, idf_divide).has_value());
}

TEST_CASE("sdcard SPI clock stays in default speed when 20 MHz can't be predicted", "[idfxx][sdcard]") {
    // On a 420 MHz source divided first by 6, a 20 MHz request isn't predictable, so a card without high speed
    // could be clocked above the cap: the clock stays at or below 20 MHz
    const clk::spi_clock_source src{.hz = 420 * mhz, .pre_div = clk::spi_min_pre_div(420 * mhz)};
    TEST_ASSERT_EQUAL_UINT32(6, src.pre_div);
    TEST_ASSERT_FALSE(clk::spi_generated_clock(src, 20 * mhz, idf_divide).has_value());
    const auto choice = check_choice(src, 50'000'000, std::nullopt, idf_divide);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(clk::sdspi_default_speed_khz, choice.request_khz);
    TEST_ASSERT_EQUAL_UINT32(17'500'000, choice.generated_hz);
}

// =============================================================================
// SDMMC buffer classification
// Which raw I/O buffers are copied through a DMA-capable buffer on an SDMMC
// host slot, checked against real addresses.
// =============================================================================

namespace {

namespace dma = idfxx::sdcard::internal;

// In flash, which DMA can't reach. Initialized so that it is kept in .rodata.
const uint8_t rodata_sector[512] = {1, 2, 3, 4};

// In RTC memory, which DMA can't reach.
RTC_DATA_ATTR uint8_t rtc_buffer[64];

#if SOC_DIRAM_IRAM_LOW != SOC_DIRAM_DRAM_LOW
// In IRAM, which DMA can't reach where IRAM has addresses of its own.
IRAM_ATTR void iram_function() {}
#endif

// A synthetic address, for ranges at the edges of DMA-capable memory. Never dereferenced.
const void* address(uintptr_t a) {
    return reinterpret_cast<const void*>(a);
}

} // namespace

TEST_CASE("sdcard SDMMC I/O uses DMA-capable internal RAM directly", "[idfxx][sdcard]") {
    uint8_t stack_buffer[512] = {};
    TEST_ASSERT_TRUE(esp_ptr_dma_capable(stack_buffer));
    TEST_ASSERT_FALSE(dma::sdmmc_needs_dma_copy(stack_buffer, sizeof(stack_buffer)));
    // Misaligned buffers are left to ESP-IDF, which copies them itself
    TEST_ASSERT_FALSE(dma::sdmmc_needs_dma_copy(stack_buffer + 1, sizeof(stack_buffer) - 1));

    void* heap_buffer = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(heap_buffer);
    const bool heap_dma_capable = esp_ptr_dma_capable(heap_buffer);
    const bool heap_copied = dma::sdmmc_needs_dma_copy(heap_buffer, 4096);
    heap_caps_free(heap_buffer);
    TEST_ASSERT_TRUE(heap_dma_capable);
    TEST_ASSERT_FALSE(heap_copied);
}

#if CONFIG_SPIRAM
TEST_CASE("sdcard SDMMC I/O leaves PSRAM buffers to ESP-IDF", "[idfxx][sdcard]") {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        TEST_IGNORE_MESSAGE("no PSRAM available");
    }
    void* psram_buffer = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    TEST_ASSERT_NOT_NULL(psram_buffer);
    const bool external = esp_ptr_external_ram(psram_buffer);
    const bool copied = dma::sdmmc_needs_dma_copy(psram_buffer, 4096);
    heap_caps_free(psram_buffer);
    TEST_ASSERT_TRUE(external);
    TEST_ASSERT_FALSE(copied);
}
#endif

TEST_CASE("sdcard SDMMC I/O copies buffers that DMA can't reach", "[idfxx][sdcard]") {
    TEST_ASSERT_TRUE(esp_ptr_in_drom(rodata_sector));
    TEST_ASSERT_TRUE(dma::sdmmc_needs_dma_copy(rodata_sector, sizeof(rodata_sector)));

    TEST_ASSERT_TRUE(esp_ptr_in_rtc_slow(rtc_buffer) || esp_ptr_in_rtc_dram_fast(rtc_buffer));
    TEST_ASSERT_TRUE(dma::sdmmc_needs_dma_copy(rtc_buffer, sizeof(rtc_buffer)));

#if SOC_DIRAM_IRAM_LOW != SOC_DIRAM_DRAM_LOW
    const void* iram = reinterpret_cast<const void*>(&iram_function);
    TEST_ASSERT_TRUE(esp_ptr_in_iram(iram));
    TEST_ASSERT_TRUE(dma::sdmmc_needs_dma_copy(iram, 4));
#endif
}

TEST_CASE("sdcard SDMMC I/O checks both ends of a buffer", "[idfxx][sdcard]") {
    // Inside DMA-capable memory, up to its last byte
    TEST_ASSERT_FALSE(dma::sdmmc_needs_dma_copy(address(SOC_DMA_HIGH - 16), 16));
    TEST_ASSERT_FALSE(dma::sdmmc_needs_dma_copy(address(SOC_DMA_LOW), 16));
    // Running past the end, or starting before the start
    TEST_ASSERT_TRUE(dma::sdmmc_needs_dma_copy(address(SOC_DMA_HIGH - 16), 17));
    TEST_ASSERT_TRUE(dma::sdmmc_needs_dma_copy(address(SOC_DMA_LOW - 1), 16));
    // A single byte on either side of the boundary
    TEST_ASSERT_FALSE(dma::sdmmc_needs_dma_copy(address(SOC_DMA_HIGH - 1), 1));
    TEST_ASSERT_TRUE(dma::sdmmc_needs_dma_copy(address(SOC_DMA_HIGH), 1));
}

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
    TEST_ASSERT_TRUE(cfg.speed == bus_speed::default_speed);
    TEST_ASSERT_FALSE(cfg.max_frequency.has_value());
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
    cfg = four_bit_config();
    cfg.speed = bus_speed::high_speed;
    cfg.max_frequency = 399_kHz;
    assert_invalid_arg(card::make(cfg));
}

TEST_CASE("sdcard make over SDMMC rejects an invalid speed", "[idfxx][sdcard]") {
    auto cfg = four_bit_config();
    cfg.speed = static_cast<bus_speed>(42);
    assert_invalid_arg(card::make(cfg));
#if !SOC_SDMMC_UHS_I_SUPPORTED
    // The value of a UHS-I mode, which the target doesn't have
    cfg = four_bit_config();
    cfg.speed = static_cast<bus_speed>(2);
    assert_invalid_arg(card::make(cfg));
#endif
}

#if SOC_SDMMC_UHS_I_SUPPORTED && SOC_SDMMC_IO_POWER_EXTERNAL
namespace {

const bus_speed uhs1_speeds[] = {
    bus_speed::sdr50,
    bus_speed::ddr50,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    bus_speed::sdr104,
#endif
};

} // namespace

TEST_CASE("sdcard make over SDMMC rejects a UHS-I speed without io_power_ldo", "[idfxx][sdcard]") {
    for (auto speed : uhs1_speeds) {
        auto cfg = four_bit_config();
        cfg.speed = speed;
        assert_invalid_arg(card::make(cfg));
    }
}

TEST_CASE("sdcard make over SDMMC rejects max_frequency with a UHS-I speed", "[idfxx][sdcard]") {
    for (auto speed : uhs1_speeds) {
        auto cfg = four_bit_config();
        cfg.speed = speed;
        cfg.io_power_ldo = 4; // so that only max_frequency is wrong
        cfg.max_frequency = 50_MHz;
        assert_invalid_arg(card::make(cfg));
    }
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

#else // !SOC_SDMMC_USE_GPIO_MATRIX

// Only slot 1 is used: should a configuration ever get past validation, slot 0's pins are the SPI flash pins.
TEST_CASE("sdcard make over SDMMC rejects the slot's pins as card-detect or write-protect", "[idfxx][sdcard]") {
    // Slot 1: CLK 14, CMD 15, D0 2, D1 4, D2 12, D3 13
    assert_invalid_arg(card::make({.card_detect = gpio_14}));
    assert_invalid_arg(card::make({.card_detect = gpio_15}));
    assert_invalid_arg(card::make({.write_protect = gpio_2}));
    assert_invalid_arg(card::make({.write_protect = gpio_13}));
    // D1 is in use on a 4-bit bus (on a 1-bit bus it is free)
    assert_invalid_arg(card::make({.width = bus_width::four, .card_detect = gpio_4}));
}

#endif // SOC_SDMMC_USE_GPIO_MATRIX
#endif // SOC_SDMMC_HOST_SUPPORTED

// =============================================================================
// Hardware-dependent tests
// These need a card in the SD slot of the board chosen in tests/Kconfig.projbuild
// (see tests/sd_test_rig.hpp). They are ignored when no board is chosen, or
// when its card-detect line reports an empty slot.
// =============================================================================

namespace {

// A sector near the end of the card, clear of any partition table or filesystem metadata at the start.
size_t scratch_sector(const card& c) {
    return c.sector_count() - 64;
}

} // namespace

TEST_CASE("sdcard reports identity and geometry", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    auto& c = f.get();

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

TEST_CASE("sdcard over SPI runs at or below max_frequency", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    const quanta::frequency::hertz caps[] = {26_MHz, 10_MHz, 400_kHz};
    for (auto cap : caps) {
        auto& c = f.reopen(cap);
        const auto hz = static_cast<int32_t>(c.frequency().count());
        TEST_ASSERT_LESS_OR_EQUAL_INT32(static_cast<int32_t>(cap.count()), hz);
        // The highest clock at or below the cap, so not far below it
        TEST_ASSERT_GREATER_OR_EQUAL_INT32(static_cast<int32_t>(cap.count() / 2), hz);
        TEST_ASSERT_TRUE(c.try_probe().has_value());
    }
}

TEST_CASE("sdcard is_present and is_write_protected read the slot pins", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    auto& c = f.get();
    TEST_ASSERT_TRUE(c.is_present());
    TEST_ASSERT_FALSE(c.is_write_protected()); // no write-protect pin
}

TEST_CASE("sdcard probe succeeds while the card responds", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    auto& c = f.get();
    TEST_ASSERT_TRUE(c.try_probe().has_value());
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    c.probe();
#endif
}

TEST_CASE("sdcard move keeps the card structure in place", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    auto& c = f.get();
    sdmmc_card_t* handle = c.idf_handle();
    card moved = std::move(c);
    const bool same_handle = moved.idf_handle() == handle;
    const bool responds = moved.try_probe().has_value();
    // Back into the fixture before asserting, so that a failure doesn't leave the card on the stack
    c = std::move(moved);
    TEST_ASSERT_TRUE(same_handle);
    TEST_ASSERT_TRUE(responds);
    TEST_ASSERT_TRUE(c.idf_handle() == handle);
}

TEST_CASE("sdcard raw read and write round-trip", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    auto& c = f.get();
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

TEST_CASE("sdcard raw I/O rejects partial sectors and out-of-range sectors", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    auto& c = f.get();
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

TEST_CASE("sdcard erase and discard", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    std::vector<uint8_t> original;
    size_t first = 0;
    {
        auto& c = f.get();
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
        auto& c = f.reopen();
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

TEST_CASE("sdcard erase across an erase chunk boundary", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    // Erases are sent in chunks on multiples of 2^21 sectors, so a range across the last such boundary on the card
    // takes two erase commands in one call
    constexpr size_t chunk = size_t{1} << 21;
    if (f.get().sector_count() <= chunk) {
        return; // the card is a single chunk
    }
    const size_t first = (f.get().sector_count() - 1) / chunk * chunk - 1;
    const size_t bytes = 2 * f.get().sector_size();
    std::vector<uint8_t> original(bytes);
    {
        auto& c = f.get();
        TEST_ASSERT_TRUE(c.try_read_sectors(first, original).has_value());
        std::vector<uint8_t> pattern(bytes, 0x5A);
        TEST_ASSERT_TRUE(c.try_write_sectors(first, pattern).has_value());
        TEST_ASSERT_TRUE(c.try_erase_sectors(first, 2).has_value());
    }
    // Cards on an SPI bus may need initializing again after an erase
    {
        auto& c = f.reopen();
        std::vector<uint8_t> erased(bytes);
        TEST_ASSERT_TRUE(c.try_read_sectors(first, erased).has_value());
        const uint8_t fill = erased[0];
        TEST_ASSERT_TRUE(fill == 0x00 || fill == 0xFF);
        TEST_ASSERT_TRUE(std::all_of(erased.begin(), erased.end(), [fill](uint8_t b) { return b == fill; }));

        TEST_ASSERT_TRUE(c.try_write_sectors(first, original).has_value());
        TEST_ASSERT_TRUE(c.try_read_sectors(first, erased).has_value());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(original.data(), erased.data(), bytes);
    }
}
