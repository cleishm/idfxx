// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/spi/master>
 * @file master
 * @brief SPI master bus and device classes.
 *
 * @defgroup idfxx_spi SPI Component
 * @brief Type-safe SPI master bus and device driver for ESP32.
 *
 * Provides SPI bus lifecycle management with support for DMA transfers,
 * along with blocking, polling, and asynchronous queue-based transaction
 * APIs for communicating with devices on the bus.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin configuration.
 * @{
 */

#include <idfxx/cpu>
#include <idfxx/error>
#include <idfxx/flags>
#include <idfxx/future>
#include <idfxx/gpio>
#include <idfxx/intr_alloc>

#include <chrono>
#include <cstdint>
#include <driver/spi_master.h>
#include <esp_idf_version.h>
#include <frequency/frequency>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

/**
 * @headerfile <idfxx/spi/master>
 * @brief SPI device capability and configuration flags.
 *
 * These flags control the behavior of an SPI device, such as bit ordering,
 * CS polarity, and duplex mode.
 */
enum class device_flags : uint32_t {
    txbit_lsbfirst = SPI_DEVICE_TXBIT_LSBFIRST,     ///< Transmit command/address/data LSB first.
    rxbit_lsbfirst = SPI_DEVICE_RXBIT_LSBFIRST,     ///< Receive data LSB first.
    bit_lsbfirst = SPI_DEVICE_BIT_LSBFIRST,         ///< Transmit and receive LSB first.
    three_wire = SPI_DEVICE_3WIRE,                  ///< Use MOSI for both sending and receiving.
    positive_cs = SPI_DEVICE_POSITIVE_CS,           ///< CS is active high during a transaction.
    halfduplex = SPI_DEVICE_HALFDUPLEX,             ///< Transmit data before receiving, not simultaneously.
    clk_as_cs = SPI_DEVICE_CLK_AS_CS,               ///< Output clock on CS line while CS is active.
    no_dummy = SPI_DEVICE_NO_DUMMY,                 ///< Disable automatic dummy bit insertion.
    ddrclk = SPI_DEVICE_DDRCLK,                     ///< Use double data rate clocking.
    no_return_result = SPI_DEVICE_NO_RETURN_RESULT, ///< Don't return descriptor on completion (use post_cb).
};

/**
 * @headerfile <idfxx/spi/master>
 * @brief SPI transaction flags.
 *
 * Per-transaction flags that control data transfer mode, variable-length
 * command/address/dummy phases, and DMA buffer behavior.
 */
enum class trans_flags : uint32_t {
    mode_dio = SPI_TRANS_MODE_DIO,                               ///< Transmit/receive data in 2-bit mode.
    mode_qio = SPI_TRANS_MODE_QIO,                               ///< Transmit/receive data in 4-bit mode.
    mode_oct = SPI_TRANS_MODE_OCT,                               ///< Transmit/receive data in 8-bit mode.
    use_rxdata = SPI_TRANS_USE_RXDATA,                           ///< Receive into inline rx_data instead of rx_buffer.
    use_txdata = SPI_TRANS_USE_TXDATA,                           ///< Transmit inline tx_data instead of tx_buffer.
    multiline_addr = SPI_TRANS_MULTILINE_ADDR,                   ///< Use multi-line mode for address phase.
    multiline_cmd = SPI_TRANS_MULTILINE_CMD,                     ///< Use multi-line mode for command phase.
    variable_cmd = SPI_TRANS_VARIABLE_CMD,                       ///< Use per-transaction command_bits length.
    variable_addr = SPI_TRANS_VARIABLE_ADDR,                     ///< Use per-transaction address_bits length.
    variable_dummy = SPI_TRANS_VARIABLE_DUMMY,                   ///< Use per-transaction dummy_bits length.
    cs_keep_active = SPI_TRANS_CS_KEEP_ACTIVE,                   ///< Keep CS active after data transfer.
    dma_buffer_align_manual = SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL, ///< Disable automatic DMA buffer re-alignment.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    dma_use_psram = SPI_TRANS_DMA_USE_PSRAM, ///< Use PSRAM for DMA buffer directly. Requires ESP-IDF 6.0+.
#endif
};

} // namespace idfxx::spi

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::spi::bus_flags> = true;

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::spi::device_flags> = true;

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::spi::trans_flags> = true;

namespace idfxx::spi {

/**
 * @headerfile <idfxx/spi/master>
 * @brief SPI transaction descriptor for full-control transactions.
 *
 * All length fields are in **bits**.
 *
 * A @c transaction is a value type: it can be freely copied, moved, and
 * stored in containers (e.g. @c std::vector). When passed to the async
 * queue API, the @c transaction value itself may be dropped or reused
 * once queue_trans() returns — only the buffers referenced by
 * @c tx_buffer / @c rx_buffer must remain valid until the returned
 * @ref idfxx::future "future" signals completion.
 *
 * @note For simple byte-oriented transfers, prefer the simple API methods
 * (transmit/receive/transfer) which handle bit-length conversion automatically.
 */
struct transaction {
    idfxx::flags<trans_flags> flags = {};    ///< Per-transaction flags.
    uint16_t cmd = 0;                        ///< Command data (length set by device config or variable_cmd).
    uint64_t addr = 0;                       ///< Address data (length set by device config or variable_addr).
    std::span<const uint8_t> tx_buffer = {}; ///< Transmit buffer, or empty for no MOSI phase.
    std::span<uint8_t> rx_buffer = {};       ///< Receive buffer, or empty for no MISO phase.
    size_t length = 0;                       ///< Total data length in bits.
    size_t rx_length = 0;                    ///< Receive length in bits (0 = same as length).
    freq::hertz override_freq{0};            ///< Override clock speed for this transaction (0 = no override).
    uint8_t command_bits = 0;                ///< Command length for variable_cmd flag.
    uint8_t address_bits = 0;                ///< Address length for variable_addr flag.
    uint8_t dummy_bits = 0;                  ///< Dummy length for variable_dummy flag.
};

class master_device;

/**
 * @headerfile <idfxx/spi/master>
 * @brief SPI bus configuration.
 *
 * @note For most SPI configurations, you should set mosi, miso, and sclk.
 * All pins default to gpio::nc() (not connected).
 *
 * @par Example:
 * @code
 * bus_config cfg{
 *     .mosi = gpio_18,
 *     .miso = gpio_19,
 *     .sclk = gpio_5,
 * };
 * @endcode
 */
struct bus_config {
    union {
        idfxx::gpio mosi = gpio::nc(); ///< GPIO pin for Master Out Slave In (=spi_d) signal.
        idfxx::gpio data0;             ///< GPIO pin for spi data0 signal in quad/octal mode.
    };
    union {
        idfxx::gpio miso = gpio::nc(); ///< GPIO pin for Master In Slave Out (=spi_q) signal.
        idfxx::gpio data1;             ///< GPIO pin for spi data1 signal in quad/octal mode.
    };
    idfxx::gpio sclk = gpio::nc(); ///< GPIO pin for SPI Clock signal.
    union {
        idfxx::gpio quadwp = gpio::nc(); ///< GPIO pin for WP (Write Protect) signal, or gpio::nc() if not used.
        idfxx::gpio data2; ///< GPIO pin for spi data2 signal in quad/octal mode, or gpio::nc() if not used.
    };
    union {
        idfxx::gpio quadhd = gpio::nc(); ///< GPIO pin for HD (Hold) signal, or gpio::nc() if not used.
        idfxx::gpio data3; ///< GPIO pin for spi data3 signal in quad/octal mode, or gpio::nc() if not used.
    };
    idfxx::gpio data4 = gpio::nc(); ///< GPIO pin for spi data4 signal in octal mode, or gpio::nc() if not used.
    idfxx::gpio data5 = gpio::nc(); ///< GPIO pin for spi data5 signal in octal mode, or gpio::nc() if not used.
    idfxx::gpio data6 = gpio::nc(); ///< GPIO pin for spi data6 signal in octal mode, or gpio::nc() if not used.
    idfxx::gpio data7 = gpio::nc(); ///< GPIO pin for spi data7 signal in octal mode, or gpio::nc() if not used.

    gpio::level data_idle_level = gpio::level::low; ///< Data pin output level while no transaction is active.
    size_t max_transfer_sz = 0; ///< Maximum transfer size, in bytes (DMA only). A value of 0 selects the 4092-byte
                                ///< default. Larger values allocate additional DMA descriptors (4092 bytes each).
                                ///< Ignored when DMA is disabled; the effective limit is then
                                ///< `SOC_SPI_MAXIMUM_BUFFER_SIZE`.

    idfxx::flags<bus_flags> flags = {}; ///< Abilities of bus to be checked by the driver.

    std::optional<idfxx::core_id> isr_cpu_id = std::nullopt; ///< Select cpu core to register SPI ISR.

    idfxx::intr_levels intr_levels = {};            ///< Interrupt priority levels to accept.
    idfxx::flags<idfxx::intr_flag> intr_flags = {}; ///< Behavioral interrupt flags. The `intr_flag::edge` and
                                                    ///< `intr_flag::disabled` flags are ignored by the driver.
                                                    ///< If `intr_flag::iram` is set, all callbacks must be
                                                    ///< placed in IRAM.
};

/**
 * @headerfile <idfxx/spi/master>
 * @brief A SPI master bus.
 *
 * Represents a SPI bus operating in master mode. This type is non-copyable
 * and move-only. The destructor is a no-op on a moved-from object.
 */
class master_bus {
public:
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

    /**
     * @brief Creates a new SPI master bus.
     *
     * @param host     SPI host device (e.g., SPI2_HOST).
     * @param dma_chan DMA channel, or idfxx::spi::dma_chan::disabled.
     * @param config   Bus configuration.
     *
     * @return The new bus, or an error.
     */
    [[nodiscard]] static result<master_bus>
    make(enum host_device host, enum dma_chan dma_chan, struct bus_config config);

    ~master_bus();

    master_bus(const master_bus&) = delete;
    master_bus& operator=(const master_bus&) = delete;

    /** @brief Move constructor. Transfers bus ownership. */
    master_bus(master_bus&& other) noexcept;

    /** @brief Move assignment. Transfers bus ownership. */
    master_bus& operator=(master_bus&& other) noexcept;

    /** @brief Returns the host device ID the bus is using. */
    [[nodiscard]] enum host_device host() const { return _host; }

    /**
     * @brief Returns the maximum transaction length for this bus.
     *
     * The maximum length depends on the DMA configuration.
     * Returns 0 if the bus has been moved from.
     *
     * @return Maximum transaction length in bytes.
     */
    [[nodiscard]] size_t max_transaction_length() const;

private:
    explicit master_bus(enum host_device host);

    enum host_device _host;
    bool _initialized = true;
};

/**
 * @headerfile <idfxx/spi/master>
 * @brief A device on a SPI master bus.
 *
 * Represents a device attached to a SPI bus. Provides byte-oriented
 * transmit/receive helpers, a full-control transaction API, a polling
 * (busy-wait) variant for low-latency transfers, and an asynchronous queue
 * API for overlapping transfers with other work.
 *
 * The blocking APIs are thread-safe and may be called concurrently from
 * multiple tasks. The polling APIs are NOT thread-safe — acquire exclusive
 * bus access via lock() (or a std::lock_guard) before calling them.
 *
 * Satisfies the Lockable named requirement, so it can be used directly with
 * std::lock_guard and similar standard RAII wrappers.
 *
 * This type is non-copyable and move-only. The caller must ensure the parent
 * master_bus outlives this device.
 */
class master_device {
public:
    /**
     * @brief SPI device configuration.
     *
     * @par Example:
     * @code
     * spi::master_device device(bus, {
     *     .mode = 0,
     *     .clock_speed = 1_MHz,
     *     .cs = idfxx::gpio_5,
     *     .queue_size = 3,
     * });
     * @endcode
     */
    struct config {
        uint8_t command_bits = 0;     ///< Default command phase length in bits (0-16).
        uint8_t address_bits = 0;     ///< Default address phase length in bits (0-64).
        uint8_t dummy_bits = 0;       ///< Dummy bits between address and data phase.
        uint8_t mode = 0;             ///< SPI mode (0-3): (CPOL, CPHA) pair.
        uint16_t duty_cycle_pos = 0;  ///< Duty cycle of positive clock in 1/256 increments (128 = 50%, 0 = default).
        uint16_t cs_ena_pretrans = 0; ///< SPI bit-cycles CS is active before transmission (half-duplex only).
        uint8_t cs_ena_posttrans = 0; ///< SPI bit-cycles CS stays active after transmission.
        freq::hertz clock_speed{0};   ///< SPI clock speed in Hz.
        std::chrono::nanoseconds input_delay{0}; ///< Maximum data valid time of slave.
        idfxx::gpio cs = gpio::nc();             ///< GPIO pin for chip select, or gpio::nc() if not used.
        idfxx::flags<device_flags> flags = {};   ///< Device capability flags.
        int queue_size = 1;                      ///< Transaction queue depth for async API.
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new SPI device on the specified bus.
     *
     * Does not take ownership of @p bus. The caller must ensure that
     * this device does not outlive the bus.
     *
     * @param bus    The parent SPI master bus.
     * @param config Device configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     *
     * @code
     * spi::master_device device(bus, {
     *     .mode = 0,
     *     .clock_speed = 1_MHz,
     *     .cs = idfxx::gpio_5,
     * });
     * @endcode
     */
    [[nodiscard]] explicit master_device(master_bus& bus, const struct config& config);
#endif

    /**
     * @brief Creates a new SPI device on the specified bus.
     *
     * Does not take ownership of @p bus. The caller must ensure that
     * this device does not outlive the bus.
     *
     * @param bus    The parent SPI master bus.
     * @param config Device configuration.
     *
     * @return The new master_device, or an error.
     *
     * @code
     * auto device = spi::master_device::make(bus, {
     *     .mode = 0,
     *     .clock_speed = 1_MHz,
     *     .cs = idfxx::gpio_5,
     * });
     * @endcode
     */
    [[nodiscard]] static result<master_device> make(master_bus& bus, const struct config& config);

    ~master_device();

    master_device(const master_device&) = delete;
    master_device& operator=(const master_device&) = delete;
    master_device(master_device&& other) noexcept;
    master_device& operator=(master_device&& other) noexcept;

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Returns the parent bus.
     * @pre The object has not been moved from.
     */
    [[nodiscard]] master_bus& bus() const {
        assert(_bus != nullptr);
        return *_bus;
    }

    /** @brief Returns the underlying ESP-IDF device handle. */
    [[nodiscard]] spi_device_handle_t idf_handle() const { return _handle; }

    /**
     * @brief Returns the actual clock frequency in use.
     *
     * The requested @c config::clock_speed may not always be exactly
     * achievable; this returns the closest frequency the hardware is
     * running at.
     *
     * @return The actual frequency in Hz.
     */
    [[nodiscard]] freq::hertz frequency() const;

    // =========================================================================
    // Simple API (byte-oriented, blocking)
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Transmits data to the device.
     *
     * @param tx_data Data to transmit.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     *
     * @code
     * std::array<uint8_t, 4> data = {0x01, 0x02, 0x03, 0x04};
     * device.transmit(data);
     * @endcode
     */
    void transmit(std::span<const uint8_t> tx_data) { unwrap(try_transmit(tx_data)); }

    /**
     * @brief Receives data from the device into the provided buffer.
     *
     * @param rx_data Buffer to receive into.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void receive(std::span<uint8_t> rx_data) { unwrap(try_receive(rx_data)); }

    /**
     * @brief Receives data from the device.
     *
     * @param size Number of bytes to receive.
     *
     * @return Received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     *
     * @code
     * auto data = device.receive(4);
     * @endcode
     */
    [[nodiscard]] std::vector<uint8_t> receive(size_t size) { return unwrap(try_receive(size)); }

    /**
     * @brief Performs a full-duplex transfer.
     *
     * @param tx_data Data to transmit.
     * @param rx_data Buffer for received data (must be same size as tx_data).
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     *
     * @code
     * std::array<uint8_t, 4> tx = {0x01, 0x02, 0x03, 0x04};
     * std::array<uint8_t, 4> rx;
     * device.transfer(tx, rx);
     * @endcode
     */
    void transfer(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) {
        unwrap(try_transfer(tx_data, rx_data));
    }
#endif

    /**
     * @brief Transmits data to the device.
     *
     * @param tx_data Data to transmit.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_transmit(std::span<const uint8_t> tx_data);

    /**
     * @brief Receives data from the device into the provided buffer.
     *
     * @param rx_data Buffer to receive into.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_receive(std::span<uint8_t> rx_data);

    /**
     * @brief Receives data from the device.
     *
     * @param size Number of bytes to receive.
     *
     * @return Received data, or an error.
     */
    [[nodiscard]] result<std::vector<uint8_t>> try_receive(size_t size);

    /**
     * @brief Performs a full-duplex transfer.
     *
     * @param tx_data Data to transmit.
     * @param rx_data Buffer for received data (must be same size as tx_data).
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_transfer(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data);

    // =========================================================================
    // Full transaction API (blocking)
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Executes a full-control transaction.
     *
     * @param trans Transaction descriptor (lengths in bits).
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void transmit(const transaction& trans) { unwrap(try_transmit(trans)); }
#endif

    /**
     * @brief Executes a full-control transaction.
     *
     * @param trans Transaction descriptor (lengths in bits).
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_transmit(const transaction& trans);

    // =========================================================================
    // Polling API (busy-wait; NOT thread-safe — lock() the device first)
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Transmits data using polling (busy-wait).
     *
     * @param tx_data Data to transmit.
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void polling_transmit(std::span<const uint8_t> tx_data) { unwrap(try_polling_transmit(tx_data)); }

    /**
     * @brief Receives data using polling (busy-wait).
     *
     * @param rx_data Buffer to receive into.
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void polling_receive(std::span<uint8_t> rx_data) { unwrap(try_polling_receive(rx_data)); }

    /**
     * @brief Receives data using polling (busy-wait).
     *
     * @param size Number of bytes to receive.
     *
     * @return Received data.
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] std::vector<uint8_t> polling_receive(size_t size) { return unwrap(try_polling_receive(size)); }

    /**
     * @brief Performs a full-duplex transfer using polling (busy-wait).
     *
     * @param tx_data Data to transmit.
     * @param rx_data Buffer for received data (must be same size as tx_data).
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void polling_transfer(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) {
        unwrap(try_polling_transfer(tx_data, rx_data));
    }

    /**
     * @brief Executes a full-control transaction using polling (busy-wait).
     *
     * @param trans Transaction descriptor (lengths in bits).
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void polling_transmit(const transaction& trans) { unwrap(try_polling_transmit(trans)); }
#endif

    /**
     * @brief Transmits data using polling (busy-wait).
     *
     * @param tx_data Data to transmit.
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_polling_transmit(std::span<const uint8_t> tx_data);

    /**
     * @brief Receives data using polling (busy-wait).
     *
     * @param rx_data Buffer to receive into.
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_polling_receive(std::span<uint8_t> rx_data);

    /**
     * @brief Receives data using polling (busy-wait).
     *
     * @param size Number of bytes to receive.
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     *
     * @return Received data, or an error.
     */
    [[nodiscard]] result<std::vector<uint8_t>> try_polling_receive(size_t size);

    /**
     * @brief Performs a full-duplex transfer using polling (busy-wait).
     *
     * @param tx_data Data to transmit.
     * @param rx_data Buffer for received data (must be same size as tx_data).
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_polling_transfer(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data);

    /**
     * @brief Executes a full-control transaction using polling (busy-wait).
     *
     * @param trans Transaction descriptor (lengths in bits).
     *
     * @note Not thread-safe. Call lock() or use std::lock_guard first.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_polling_transmit(const transaction& trans);

    // =========================================================================
    // Async queue API
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Queues a transaction for asynchronous execution.
     *
     * Submits @p trans to the device and returns a future that signals
     * completion. Blocks indefinitely if the device's transaction queue is
     * full; the queue depth is set by @c config::queue_size.
     *
     * @param trans Transaction descriptor.
     *
     * @return A future tracking the queued transaction.
     *
     * @note The @ref transaction value itself may be dropped or reused once
     * this call returns; the buffers it references must remain valid until
     * the returned future signals completion.
     * @note The parent master_device must outlive every future it produces.
     * @note Dropping an in-flight future (without waiting) is safe. The
     * transaction continues to occupy a queue slot until a later wait() call
     * drains it or the device is destroyed, which may cause subsequent
     * queue_trans() calls to block if the queue fills up.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] idfxx::future<void> queue_trans(const transaction& trans) { return unwrap(try_queue_trans(trans)); }

    /**
     * @brief Queues a transaction for asynchronous execution with timeout.
     *
     * @tparam Rep     Duration arithmetic type.
     * @tparam Period  Duration period type.
     * @param trans   Transaction descriptor.
     * @param timeout Maximum time to wait for space in the transaction queue.
     *
     * @return A future tracking the queued transaction.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure or timeout.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] idfxx::future<void>
    queue_trans(const transaction& trans, const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_queue_trans(trans, timeout));
    }

#endif

    /**
     * @brief Queues a transaction for asynchronous execution.
     *
     * Submits @p trans to the device and returns a future that signals
     * completion. Blocks indefinitely if the device's transaction queue is
     * full; the queue depth is set by @c config::queue_size.
     *
     * @param trans Transaction descriptor.
     *
     * @return A future tracking the queued transaction, or an error.
     *
     * @note The @ref transaction value itself may be dropped or reused once
     * this call returns; the buffers it references must remain valid until
     * the returned future signals completion.
     * @note The parent master_device must outlive every future it produces.
     */
    [[nodiscard]] result<idfxx::future<void>> try_queue_trans(const transaction& trans);

    /**
     * @brief Queues a transaction for asynchronous execution with timeout.
     *
     * @tparam Rep     Duration arithmetic type.
     * @tparam Period  Duration period type.
     * @param trans   Transaction descriptor.
     * @param timeout Maximum time to wait for space in the transaction queue.
     *
     * @return A future tracking the queued transaction, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<idfxx::future<void>>
    try_queue_trans(const transaction& trans, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_queue_trans(trans, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    // =========================================================================
    // Bus exclusivity (Lockable interface)
    // =========================================================================

    /**
     * @brief Acquires exclusive access to the SPI bus for this device.
     *
     * While the bus is acquired, transactions to all other devices on the
     * same bus are deferred. Blocks indefinitely.
     */
    void lock() const;

    /**
     * @brief Tries to acquire exclusive bus access without blocking.
     *
     * @return true if the bus was acquired, false otherwise.
     */
    [[nodiscard]] bool try_lock() const noexcept;

    /**
     * @brief Releases exclusive bus access.
     */
    void unlock() const;

private:
    // Opaque async state held in the .cpp. master_device keeps a single
    // unique_ptr to it, so the class stays cheap to move while the mutexes,
    // condition variable, and slot pool keep a stable address across the
    // device's lifetime.
    struct async_state;

    explicit master_device(master_bus* bus, spi_device_handle_t handle, size_t queue_size);

    void _delete() noexcept;

    using _trans_fn = esp_err_t (*)(spi_device_handle_t, spi_transaction_t*);

    [[nodiscard]] result<void> _simple_trans(std::span<const uint8_t> tx, std::span<uint8_t> rx, _trans_fn fn);
    [[nodiscard]] result<void> _full_trans(const transaction& trans, _trans_fn fn);

    [[nodiscard]] result<idfxx::future<void>>
    _try_queue_trans(const transaction& trans, std::optional<std::chrono::milliseconds> timeout);
    [[nodiscard]] result<void> _try_wait_for(uint16_t slot_idx, std::optional<std::chrono::milliseconds> timeout);

    [[nodiscard]] result<uint16_t> _acquire_slot(std::optional<std::chrono::milliseconds> timeout);
    void _release_slot_ref(uint16_t slot_idx) noexcept;
    [[nodiscard]] bool _slot_done(uint16_t slot_idx) const noexcept;

    static void _prepare_idf_trans(spi_transaction_t& idf_trans, const transaction& trans);
    static void _prepare_idf_trans_ext(spi_transaction_ext_t& idf_ext, const transaction& trans);

    master_bus* _bus = nullptr;
    spi_device_handle_t _handle = nullptr;
    std::unique_ptr<async_state> _async;
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
