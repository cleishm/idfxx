// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx sdcard
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "../src/sdcard_dma.hpp"
#include "../src/sdcard_erase.hpp"
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
#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <iterator>
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
    assert_invalid_arg(card::make(bus.get(), {.cs = gpio::nc()}));}

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
// The clock a card on an SPI bus is configured with: the search against
// synthetic buses, and on this target's SPI bus.
// =============================================================================

namespace {

namespace clk = idfxx::sdcard::internal;

constexpr uint32_t mhz = 1'000'000;

// The clock a bus generates for a request when it rounds to the nearest of source_hz divided by a whole number (the
// lower one on a tie), as the SPI peripheral does, or 0 if it rejects the request.
uint32_t nearest_clock(uint32_t source_hz, uint32_t request_hz) {
    if (request_hz == 0 || request_hz > source_hz) {
        return 0;
    }
    const uint32_t d = source_hz / request_hz;
    const uint32_t above = source_hz / d;
    const uint32_t below = source_hz / (d + 1);
    return above - request_hz < request_hz - below ? above : below;
}

// The clock a bus reports, in whole kHz, for a generated clock (0 if it rejects the request).
std::optional<uint32_t> reported(uint32_t hz) {
    return hz == 0 ? std::nullopt : std::optional<uint32_t>{hz / 1000};
}

// A bus on an 80 MHz source (esp32, esp32s2, esp32s3, esp32c3 and esp32c6).
std::optional<uint32_t> nearest_80mhz_bus(uint32_t khz) {
    return reported(nearest_clock(80 * mhz, khz * 1000));
}

// As nearest_80mhz_bus, but rejecting any clock above 26.7 MHz (as an esp32 bus on the GPIO matrix does).
std::optional<uint32_t> limited_80mhz_bus(uint32_t khz) {
    const uint32_t hz = nearest_clock(80 * mhz, khz * 1000);
    return reported(hz > 80 * mhz / 3 ? 0 : hz);
}

// A bus on a 480 MHz source that the driver divides first by an even divider, at least 6, that divides the source's
// ratio to the request, falling back to 6 (as ESP-IDF 6.1 does on esp32p4), and then rounds as nearest_clock() does.
std::optional<uint32_t> pre_divided_480mhz_bus(uint32_t khz) {
    const uint32_t source_hz = 480 * mhz;
    const uint32_t request_hz = khz * 1000;
    const uint32_t total = source_hz / request_hz;
    uint32_t pre_div = 6;
    for (uint32_t d = 6; d <= std::min<uint32_t>(total, 256); d += 2) {
        if (total % d == 0 && total / d <= 1024) {
            pre_div = d;
            break;
        }
    }
    return reported(nearest_clock(source_hz / pre_div, request_hz));
}

// A bus on a 44 MHz source, where a 20 MHz request runs at 22 MHz.
std::optional<uint32_t> nearest_44mhz_bus(uint32_t khz) {
    return reported(nearest_clock(44 * mhz, khz * 1000));
}

void check_choice(
    uint32_t source_hz, uint32_t limit_hz, std::optional<uint32_t> (*bus)(uint32_t), uint32_t request_khz, uint32_t hz
) {
    const auto choice = clk::choose_spi_clock(source_hz, limit_hz, bus);
    TEST_ASSERT_TRUE(choice.has_value());
    TEST_ASSERT_EQUAL_UINT32(request_khz, choice->request_khz);
    TEST_ASSERT_EQUAL_UINT32(hz, choice->hz);
}

} // namespace

TEST_CASE("sdcard SPI clock is recovered from the clock reported in kHz", "[idfxx][sdcard]") {
    // Exact where one divider gives the reading
    TEST_ASSERT_EQUAL_UINT32(40'000'000, clk::spi_clock_hz(80 * mhz, 40'000));
    TEST_ASSERT_EQUAL_UINT32(26'666'666, clk::spi_clock_hz(80 * mhz, 26'666));
    TEST_ASSERT_EQUAL_UINT32(400'000, clk::spi_clock_hz(80 * mhz, 400));
    // Otherwise the highest clock the reading allows: 480 MHz divided by 1198 to 1200 all read as 400 kHz
    TEST_ASSERT_EQUAL_UINT32(400'999, clk::spi_clock_hz(480 * mhz, 400));
    TEST_ASSERT_EQUAL_UINT32(999, clk::spi_clock_hz(80 * mhz, 0));
}

TEST_CASE("sdcard SPI clock is the highest the bus runs at within the limit", "[idfxx][sdcard]") {
    check_choice(80 * mhz, 400'000, nearest_80mhz_bus, 400, 400'000);
    check_choice(80 * mhz, 10 * mhz, nearest_80mhz_bus, 10'000, 10 * mhz);
    check_choice(80 * mhz, 20 * mhz, nearest_80mhz_bus, 20'000, 20 * mhz);
    // Nothing between 20 MHz and 26.7 MHz, so the card stays in default speed
    check_choice(80 * mhz, 26 * mhz, nearest_80mhz_bus, 20'000, 20 * mhz);
    check_choice(80 * mhz, 26'666'666, nearest_80mhz_bus, 26'666, 26'666'666);
    check_choice(80 * mhz, 30 * mhz, nearest_80mhz_bus, 26'666, 26'666'666);
    // A limit the bus runs at exactly
    check_choice(80 * mhz, 40 * mhz, nearest_80mhz_bus, 40'000, 40 * mhz);
    check_choice(80 * mhz, 50 * mhz, nearest_80mhz_bus, 40'000, 40 * mhz);
    TEST_ASSERT_FALSE(clk::choose_spi_clock(80 * mhz, 399'999, nearest_80mhz_bus).has_value());
    TEST_ASSERT_FALSE(clk::choose_spi_clock(80 * mhz, 0, nearest_80mhz_bus).has_value());
}

TEST_CASE("sdcard SPI clock skips clocks the bus rejects", "[idfxx][sdcard]") {
    check_choice(80 * mhz, 50 * mhz, limited_80mhz_bus, 26'666, 26'666'666);
    check_choice(80 * mhz, 20 * mhz, limited_80mhz_bus, 20'000, 20 * mhz);
}

TEST_CASE("sdcard SPI clock on a bus that divides its source first", "[idfxx][sdcard]") {
    // A request for 53.3 MHz runs at 40 MHz, but one for 48 MHz (divided first by 10) runs at 48 MHz
    check_choice(480 * mhz, 50 * mhz, pre_divided_480mhz_bus, 48'000, 48 * mhz);
    // A request for 25.3 MHz runs at 26.7 MHz, but one for 24 MHz (divided first by 10) runs at 24 MHz
    check_choice(480 * mhz, 26 * mhz, pre_divided_480mhz_bus, 24'000, 24 * mhz);
    // A request for 17.8 MHz runs at 16 MHz, below the 17.1 MHz that a lower one runs at (divided first by 14)
    check_choice(480 * mhz, 18 * mhz, pre_divided_480mhz_bus, 17'142, 17'142'857);
    // The reading for 400 kHz is ambiguous, and no lower clock is tried: the identification clock is kept
    const auto choice = clk::choose_spi_clock(480 * mhz, 400'000, pre_divided_480mhz_bus);
    TEST_ASSERT_TRUE(choice.has_value());
    TEST_ASSERT_EQUAL_UINT32(SDMMC_FREQ_PROBING, choice->request_khz);
}

TEST_CASE("sdcard SPI clock stays in default speed when 20 MHz runs above the limit", "[idfxx][sdcard]") {
    // A card without high speed would be configured with 20 MHz, which runs at 22 MHz on this bus
    check_choice(44 * mhz, 21 * mhz, nearest_44mhz_bus, 14'666, 14'666'666);
    check_choice(44 * mhz, 22 * mhz, nearest_44mhz_bus, 22'000, 22 * mhz);
}

// Setting a clock on a device needs no transfers, so this runs in QEMU too.
TEST_CASE("sdcard SPI clock on this target's bus is at or below max_frequency", "[idfxx][sdcard]") {
    rig::unwired_bus_fixture bus;
    sdspi_device_config_t dev_cfg{};
    dev_cfg.host_id = SPI2_HOST;
    dev_cfg.gpio_cs = spare_pin_a.idf_num();
    dev_cfg.gpio_cd = GPIO_NUM_NC;
    dev_cfg.gpio_wp = GPIO_NUM_NC;
    dev_cfg.gpio_int = GPIO_NUM_NC;
    sdspi_dev_handle_t handle;
    TEST_ASSERT_EQUAL(ESP_OK, sdspi_host_init_device(&dev_cfg, &handle));

    const quanta::frequency::hertz caps[] = {400_kHz, 10_MHz, 20_MHz, 26_MHz, 40_MHz, 50_MHz, 1_GHz};
    struct outcome {
        bool chosen;
        uint32_t request_khz;
        uint32_t hz;
        int left_khz; // the device's clock afterwards
    };
    outcome outcomes[std::size(caps)]{};
    for (size_t i = 0; i < std::size(caps); ++i) {
        const auto choice = clk::choose_sdspi_clock(handle, SPI2_HOST, caps[i]);
        outcomes[i] = {choice.has_value(), choice ? choice->request_khz : 0, choice ? choice->hz : 0, -1};
        (void)sdspi_host_get_real_freq(handle, &outcomes[i].left_khz);
    }
    const auto too_low = clk::choose_sdspi_clock(handle, SPI2_HOST, 399_kHz);
    // Removed before asserting, so that a failure doesn't leave the device on the bus
    TEST_ASSERT_EQUAL(ESP_OK, sdspi_host_remove_device(handle));

    for (size_t i = 0; i < std::size(caps); ++i) {
        const auto limit = static_cast<uint32_t>(std::min<int64_t>(caps[i].count(), 50 * mhz));
        TEST_ASSERT_TRUE(outcomes[i].chosen);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(limit, outcomes[i].hz);
        // The highest clock at or below the limit, so not far below it
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(limit / 4 * 3, outcomes[i].hz);
        // High speed exactly when the clock is above 20 MHz
        TEST_ASSERT_EQUAL(outcomes[i].hz > 20 * mhz, outcomes[i].request_khz > SDMMC_FREQ_DEFAULT);
        // Left at the identification clock, for card initialization
        TEST_ASSERT_EQUAL_INT32(SDMMC_FREQ_PROBING, outcomes[i].left_khz);
    }
    TEST_ASSERT_FALSE(too_low.has_value());
    TEST_ASSERT_TRUE(too_low.error() == errc::invalid_arg);
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

// Checks that the sectors from first, as many as original holds, read back erased (all zeros or all ones), then writes
// original back to them.
void check_erased_and_restore(card& c, size_t first, const std::vector<uint8_t>& original) {
    std::vector<uint8_t> erased(original.size());
    TEST_ASSERT_TRUE(c.try_read_sectors(first, erased).has_value());
    const uint8_t fill = erased[0];
    TEST_ASSERT_TRUE(fill == 0x00 || fill == 0xFF);
    TEST_ASSERT_TRUE(std::all_of(erased.begin(), erased.end(), [fill](uint8_t b) { return b == fill; }));

    TEST_ASSERT_TRUE(c.try_write_sectors(first, original).has_value());
    TEST_ASSERT_TRUE(c.try_read_sectors(first, erased).has_value());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.data(), erased.data(), original.size());
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
    check_erased_and_restore(f.reopen(), first, original);
}

TEST_CASE("sdcard erase across an erase chunk boundary", "[idfxx][sdcard][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture f;
    // Erases are sent in chunks on multiples of erase_chunk_sectors, so a range across the last such boundary on the
    // card takes two erase commands in one call
    constexpr size_t chunk = internal::erase_chunk_sectors;
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
    check_erased_and_restore(f.reopen(), first, original);
}
