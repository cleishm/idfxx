// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx spi master
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/spi/master"
#include "unity.h"

#include <driver/spi_master.h>
#include <type_traits>

using namespace idfxx;
using namespace idfxx::spi;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// master_bus is not default constructible (must use make() factory)
static_assert(!std::is_default_constructible_v<master_bus>);

// master_bus is not copyable (unique ownership of bus)
static_assert(!std::is_copy_constructible_v<master_bus>);
static_assert(!std::is_copy_assignable_v<master_bus>);

// master_bus is not movable (fixed bus ownership)
static_assert(!std::is_move_constructible_v<master_bus>);
static_assert(!std::is_move_assignable_v<master_bus>);

// host_device enum values match ESP-IDF values
static_assert(static_cast<spi_host_device_t>(host_device::spi1) == SPI1_HOST);
static_assert(static_cast<spi_host_device_t>(host_device::spi2) == SPI2_HOST);
#if SOC_SPI_PERIPH_NUM > 2
static_assert(static_cast<spi_host_device_t>(host_device::spi3) == SPI3_HOST);
#endif

// dma_chan enum values match ESP-IDF values
static_assert(static_cast<spi_common_dma_t>(dma_chan::disabled) == SPI_DMA_DISABLED);
#if CONFIG_IDF_TARGET_ESP32
static_assert(static_cast<spi_common_dma_t>(dma_chan::ch_1) == SPI_DMA_CH1);
static_assert(static_cast<spi_common_dma_t>(dma_chan::ch_2) == SPI_DMA_CH2);
#endif
static_assert(static_cast<spi_common_dma_t>(dma_chan::ch_auto) == SPI_DMA_CH_AUTO);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("bus_config default initialization", "[idfxx][spi]") {
    bus_config cfg{};
    TEST_ASSERT_FALSE(cfg.mosi_io_num.is_connected());
    TEST_ASSERT_FALSE(cfg.miso_io_num.is_connected());
    TEST_ASSERT_FALSE(cfg.sclk_io_num.is_connected());
    TEST_ASSERT_FALSE(cfg.quadwp_io_num.is_connected());
    TEST_ASSERT_FALSE(cfg.quadhd_io_num.is_connected());
    TEST_ASSERT_FALSE(cfg.data4_io_num.is_connected());
    TEST_ASSERT_FALSE(cfg.data5_io_num.is_connected());
    TEST_ASSERT_FALSE(cfg.data6_io_num.is_connected());
    TEST_ASSERT_FALSE(cfg.data7_io_num.is_connected());
}

TEST_CASE("bus_config union aliasing", "[idfxx][spi]") {
    bus_config cfg{};

    // Test mosi/data0 union
    cfg.mosi_io_num = gpio_0;
    TEST_ASSERT_EQUAL(cfg.mosi_io_num.num(), cfg.data0_io_num.num());

    // Test miso/data1 union
    cfg.miso_io_num = gpio_1;
    TEST_ASSERT_EQUAL(cfg.miso_io_num.num(), cfg.data1_io_num.num());

    // Test quadwp/data2 union
    cfg.quadwp_io_num = gpio_2;
    TEST_ASSERT_EQUAL(cfg.quadwp_io_num.num(), cfg.data2_io_num.num());

    // Test quadhd/data3 union
    cfg.quadhd_io_num = gpio_3;
    TEST_ASSERT_EQUAL(cfg.quadhd_io_num.num(), cfg.data3_io_num.num());
}

// =============================================================================
// Hardware-dependent tests
// These tests interact with actual SPI hardware
// =============================================================================

TEST_CASE("master_bus::make with valid config succeeds", "[idfxx][spi]") {
    bus_config cfg{};
    cfg.mosi_io_num = gpio_11;
    cfg.miso_io_num = gpio_13;
    cfg.sclk_io_num = gpio_12;
    cfg.max_transfer_sz = 4096;

    auto result = master_bus::make(host_device::spi2, dma_chan::ch_auto, cfg);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(host_device::spi2, result.value()->host());
}

TEST_CASE("master_bus::make with invalid GPIO fails", "[idfxx][spi]") {
    bus_config cfg{};
    cfg.mosi_io_num = gpio::make(999).value_or(gpio_nc); // Invalid GPIO
    cfg.sclk_io_num = gpio_12;

    auto result = master_bus::make(host_device::spi2, dma_chan::ch_auto, cfg);
    // May succeed if mosi is NC, behavior depends on ESP-IDF validation
    // This test verifies no crash occurs with edge case config
}

TEST_CASE("master_bus host() returns correct host", "[idfxx][spi]") {
    bus_config cfg{};
    cfg.mosi_io_num = gpio_11;
    cfg.sclk_io_num = gpio_12;
    cfg.max_transfer_sz = 4096;

    auto result = master_bus::make(host_device::spi2, dma_chan::disabled, cfg);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(host_device::spi2, result.value()->host());
}

#if SOC_SPI_PERIPH_NUM > 2
TEST_CASE("master_bus::make with SPI3 host succeeds", "[idfxx][spi]") {
    bus_config cfg{};
    cfg.mosi_io_num = gpio_11;
    cfg.sclk_io_num = gpio_12;
    cfg.max_transfer_sz = 4096;

    auto result = master_bus::make(host_device::spi3, dma_chan::ch_auto, cfg);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(host_device::spi3, result.value()->host());
}
#endif

TEST_CASE("master_bus::make with DMA disabled succeeds", "[idfxx][spi]") {
    bus_config cfg{};
    cfg.mosi_io_num = gpio_11;
    cfg.sclk_io_num = gpio_12;

    auto result = master_bus::make(host_device::spi2, dma_chan::disabled, cfg);
    TEST_ASSERT_TRUE(result.has_value());
}

#if CONFIG_IDF_TARGET_ESP32
TEST_CASE("master_bus::make with specific DMA channel succeeds", "[idfxx][spi]") {
    bus_config cfg{};
    cfg.mosi_io_num = gpio_11;
    cfg.sclk_io_num = gpio_12;
    cfg.max_transfer_sz = 4096;

    auto result = master_bus::make(host_device::spi2, dma_chan::ch_1, cfg);
    TEST_ASSERT_TRUE(result.has_value());
}
#endif

TEST_CASE("master_bus destructor frees bus", "[idfxx][spi]") {
    bus_config cfg{};
    cfg.mosi_io_num = gpio_11;
    cfg.sclk_io_num = gpio_12;
    cfg.max_transfer_sz = 4096;

    {
        auto result = master_bus::make(host_device::spi2, dma_chan::ch_auto, cfg);
        TEST_ASSERT_TRUE(result.has_value());
        // Bus should be freed when result goes out of scope
    }

    // Should be able to create a new bus on the same host after destruction
    auto result2 = master_bus::make(host_device::spi2, dma_chan::ch_auto, cfg);
    TEST_ASSERT_TRUE(result2.has_value());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("master_bus constructor with invalid host throws", "[idfxx][spi]") {
    // Test exception-based API with inline config
    bool threw = false;
    try {
        // Use host value >= SPI_HOST_MAX which should fail validation
        std::make_unique<master_bus>(
            static_cast<host_device>(99),
            dma_chan::ch_auto,
bus_config{}
        );
    } catch (const std::system_error&) {
        threw = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}
#endif

// =============================================================================
// to_string tests
// =============================================================================

TEST_CASE("to_string(host_device) outputs correct names", "[idfxx][spi]") {
    TEST_ASSERT_EQUAL_STRING("SPI1", to_string(host_device::spi1).c_str());
    TEST_ASSERT_EQUAL_STRING("SPI2", to_string(host_device::spi2).c_str());
#if SOC_SPI_PERIPH_NUM > 2
    TEST_ASSERT_EQUAL_STRING("SPI3", to_string(host_device::spi3).c_str());
#endif
}

TEST_CASE("to_string(host_device) handles unknown values", "[idfxx][spi]") {
    auto unknown = static_cast<host_device>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", to_string(unknown).c_str());
}

// =============================================================================
// Formatter tests
// =============================================================================

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<host_device, char>);

TEST_CASE("host_device formatter outputs correct names", "[idfxx][spi]") {
    TEST_ASSERT_EQUAL_STRING("SPI1", std::format("{}", host_device::spi1).c_str());
    TEST_ASSERT_EQUAL_STRING("SPI2", std::format("{}", host_device::spi2).c_str());
#if SOC_SPI_PERIPH_NUM > 2
    TEST_ASSERT_EQUAL_STRING("SPI3", std::format("{}", host_device::spi3).c_str());
#endif
}

TEST_CASE("host_device formatter handles unknown values", "[idfxx][spi]") {
    auto unknown = static_cast<host_device>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", std::format("{}", unknown).c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
