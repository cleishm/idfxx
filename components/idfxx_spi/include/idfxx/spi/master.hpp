// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/spi/master>
 * @file master
 * @brief SPI master bus class.
 *
 * @defgroup idfxx_spi SPI Component
 * @brief Type-safe SPI master bus driver for ESP32.
 *
 * Provides SPI bus lifecycle management with support for DMA transfers.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin configuration.
 * @{
 */

#include <idfxx/error>
#include <idfxx/flags>
#include <idfxx/gpio>
#include <idfxx/intr_alloc>
#include <idfxx/intr_types>

#include <driver/spi_master.h>
#include <memory>
#include <string>

/**
 * @headerfile <idfxx/spi/master>
 * @brief SPI driver classes.
 */
namespace idfxx::spi {

/**
 * @headerfile <idfxx/spi/master>
 * @brief General purpose SPI Host Controller ID.
 */
enum class host_device : int {
    spi1 = SPI1_HOST, ///< SPI1
    spi2 = SPI2_HOST, ///< SPI2
#if SOC_SPI_PERIPH_NUM > 2
    spi3 = SPI3_HOST, ///< SPI3
#endif
};

/**
 * @headerfile <idfxx/spi/master>
 * @brief SPI DMA channel selection.
 */
enum class dma_chan : int {
    disabled = SPI_DMA_DISABLED, ///< No DMA
#if CONFIG_IDF_TARGET_ESP32
    ch_1 = SPI_DMA_CH1, ///< Use DMA channel 1
    ch_2 = SPI_DMA_CH2, ///< Use DMA channel 2
#endif
    ch_auto = SPI_DMA_CH_AUTO, ///< Auto select DMA channel
};

/**
 * @headerfile <idfxx/spi/master>
 * @brief SPI bus capability and configuration flags.
 *
 * These flags serve two purposes:
 * - When passed to bus initialization, they specify required bus capabilities
 *   that the driver will verify (e.g., requiring certain pins to be present).
 * - After initialization, they indicate the actual capabilities of the bus.
 */
enum class bus_flags : uint32_t {
    slave = SPICOMMON_BUSFLAG_SLAVE,           ///< Bus supports slave mode
    master = SPICOMMON_BUSFLAG_MASTER,         ///< Bus supports master mode
    iomux_pins = SPICOMMON_BUSFLAG_IOMUX_PINS, ///< Bus uses IOMUX pins
    sclk = SPICOMMON_BUSFLAG_SCLK,             ///< Check existing of SCLK pin. Or indicates CLK line initialized.
    miso = SPICOMMON_BUSFLAG_MISO,             ///< Check existing of MISO pin. Or indicates MISO line initialized.
    mosi = SPICOMMON_BUSFLAG_MOSI,             ///< Check existing of MOSI pin. Or indicates MOSI line initialized.
    dual =
        SPICOMMON_BUSFLAG_DUAL, ///< Check MOSI and MISO pins can output. Or indicates bus able to work under DIO mode.
    wphd = SPICOMMON_BUSFLAG_WPHD, ///< Check existing of WP and HD pins. Or indicates WP & HD pins initialized.
    quad = SPICOMMON_BUSFLAG_QUAD, ///< Check existing of MOSI/MISO/WP/HD pins as output. Or indicates bus able to work
                                   ///< under QIO mode.
    io4_io7 = SPICOMMON_BUSFLAG_IO4_IO7, ///< Check existing of IO4~IO7 pins. Or indicates IO4~IO7 pins initialized.
    octal = SPICOMMON_BUSFLAG_OCTAL, ///< Check existing of MOSI/MISO/WP/HD/SPIIO4/SPIIO5/SPIIO6/SPIIO7 pins as output.
                                     ///< Or indicates bus able to work under octal mode.
    native_pins = SPICOMMON_BUSFLAG_NATIVE_PINS,   ///< Bus uses native pins
    slp_allow_pd = SPICOMMON_BUSFLAG_SLP_ALLOW_PD, ///< Allow to power down the peripheral during light sleep, and auto
                                                   ///< recover then.
};

} // namespace idfxx::spi

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::spi::bus_flags> = true;

namespace idfxx::spi {

/**
 * @headerfile <idfxx/spi/master>
 * @brief SPI bus configuration.
 *
 * @note For most SPI configurations, you should set mosi_io_num, miso_io_num, and sclk_io_num.
 * All pins default to gpio::nc() (not connected).
 *
 * @par Example:
 * @code
 * bus_config cfg{
 *     .mosi_io_num = gpio_18,
 *     .miso_io_num = gpio_19,
 *     .sclk_io_num = gpio_5,
 * };
 * @endcode
 */
struct bus_config {
    union {
        idfxx::gpio mosi_io_num = gpio::nc(); ///< GPIO pin for Master Out Slave In (=spi_d) signal.
        idfxx::gpio data0_io_num;             ///< GPIO pin for spi data0 signal in quad/octal mode.
    };
    union {
        idfxx::gpio miso_io_num = gpio::nc(); ///< GPIO pin for Master In Slave Out (=spi_q) signal.
        idfxx::gpio data1_io_num;             ///< GPIO pin for spi data1 signal in quad/octal mode.
    };
    idfxx::gpio sclk_io_num = gpio::nc(); ///< GPIO pin for SPI Clock signal.
    union {
        idfxx::gpio quadwp_io_num = gpio::nc(); ///< GPIO pin for WP (Write Protect) signal, or gpio::nc() if not used.
        idfxx::gpio data2_io_num; ///< GPIO pin for spi data2 signal in quad/octal mode, or gpio::nc() if not used.
    };
    union {
        idfxx::gpio quadhd_io_num = gpio::nc(); ///< GPIO pin for HD (Hold) signal, or gpio::nc() if not used.
        idfxx::gpio data3_io_num; ///< GPIO pin for spi data3 signal in quad/octal mode, or gpio::nc() if not used.
    };
    idfxx::gpio data4_io_num = gpio::nc(); ///< GPIO pin for spi data4 signal in octal mode, or gpio::nc() if not used.
    idfxx::gpio data5_io_num = gpio::nc(); ///< GPIO pin for spi data5 signal in octal mode, or gpio::nc() if not used.
    idfxx::gpio data6_io_num = gpio::nc(); ///< GPIO pin for spi data6 signal in octal mode, or gpio::nc() if not used.
    idfxx::gpio data7_io_num = gpio::nc(); ///< GPIO pin for spi data7 signal in octal mode, or gpio::nc() if not used.

    bool data_io_default_level = false; ///< Output data IO default level when no transaction.
    int max_transfer_sz = 0; ///< Maximum transfer size, in bytes. Defaults to 4092 if 0 when DMA enabled, or to
                             ///< `SOC_SPI_MAXIMUM_BUFFER_SIZE` if DMA is disabled.

    idfxx::flags<bus_flags> flags = {}; ///< Abilities of bus to be checked by the driver.

    idfxx::intr_cpu_affinity_t isr_cpu_id =
        idfxx::intr_cpu_affinity_t::automatic; ///< Select cpu core to register SPI ISR.

    idfxx::flags<idfxx::intr_flag> intr_flags = {}; ///< Interrupt flags to set priority and IRAM attribute.
                                                    ///< The `intr_flag::edge` and `intr_flag::intr_disabled` flags are
                                                    ///< ignored by the driver. If `intr_flag::iram` is set, all
                                                    ///< callbacks must be placed in IRAM.
};

/**
 * @headerfile <idfxx/spi/master>
 * @brief A SPI master bus.
 *
 * Represents a SPI bus operating in master mode.
 */
class master_bus {
public:
    /**
     * @brief Creates a new SPI master bus.
     *
     * @param host     SPI host device (e.g., SPI2_HOST).
     * @param dma_chan DMA channel, or idfxx::spi::dma_chan::disabled.
     * @param config   Bus configuration.
     *
     * @return The new bus, or an error.
     */
    [[nodiscard]] static result<std::unique_ptr<master_bus>>
    make(enum host_device host, enum dma_chan dma_chan, struct bus_config config);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Constructs a new SPI master bus.
     *
     * @param host     SPI host device (e.g., SPI2_HOST).
     * @param dma_chan DMA channel, or idfxx::spi::dma_chan::disabled.
     * @param config   Bus configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit master_bus(enum host_device host, enum dma_chan dma_chan, struct bus_config config);
#endif

    ~master_bus();

    master_bus(const master_bus&) = delete;
    master_bus& operator=(const master_bus&) = delete;
    master_bus(master_bus&&) = delete;
    master_bus& operator=(master_bus&&) = delete;

    /** @brief Returns the host device ID the bus is using. */
    [[nodiscard]] enum host_device host() const { return _host; }

private:
    explicit master_bus(enum host_device host);

    enum host_device _host;
};

/** @} */ // end of idfxx_spi

} // namespace idfxx::spi

namespace idfxx {

/**
 * @headerfile <idfxx/spi/master>
 * @brief Returns a string representation of an SPI host device.
 *
 * @param h The host device identifier to convert.
 * @return "SPI1", "SPI2", "SPI3" (when available), or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(spi::host_device h) {
    switch (h) {
    case spi::host_device::spi1:
        return "SPI1";
    case spi::host_device::spi2:
        return "SPI2";
#if SOC_SPI_PERIPH_NUM > 2
    case spi::host_device::spi3:
        return "SPI3";
#endif
    default:
        return "unknown(" + std::to_string(static_cast<int>(h)) + ")";
    }
}

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::spi::host_device> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::spi::host_device h, FormatContext& ctx) const {
        auto s = idfxx::to_string(h);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
