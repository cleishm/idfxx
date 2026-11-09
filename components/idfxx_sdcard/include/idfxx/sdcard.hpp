// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/sdcard>
 * @file sdcard.hpp
 * @brief SD and MMC/eMMC memory cards.
 *
 * @defgroup idfxx_sdcard SD Card Component
 * @brief SD and MMC/eMMC memory cards on an SPI bus or an SDMMC host slot.
 *
 * Initializes a memory card, reports its type, identity and geometry, and
 * provides raw sector reads, writes and erases. A card can be attached to an
 * SPI bus (any target) or to an SDMMC host slot (targets with an SDMMC host:
 * esp32, esp32s3 and esp32p4). A filesystem can be mounted on an initialized
 * card with idfxx_fatfs.
 *
 * Depends on @ref idfxx_core for error handling, @ref idfxx_gpio for pin
 * configuration and @ref idfxx_spi for the SPI bus.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/required>
#include <idfxx/spi/master>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <quanta/frequency>
#include <sd_protocol_types.h>
#include <soc/soc_caps.h>
#include <span>
#include <string>
#include <string_view>

/**
 * @headerfile <idfxx/sdcard>
 * @brief SD and MMC/eMMC memory card classes.
 */
namespace idfxx::sdcard {

/**
 * @headerfile <idfxx/sdcard>
 * @brief Kind of memory card.
 *
 * SDIO-only cards (I/O cards without memory) are not memory cards and are
 * rejected by card::make() with `errc::not_supported`.
 */
enum class card_type {
    sdsc, ///< SD Standard Capacity (up to 2 GB).
    sdhc, ///< SD High Capacity (more than 2 GB, less than 32 GiB).
    /// SD Extended Capacity (32 GiB, 2^35 bytes, or more). SDXC cards identify themselves as SDHC on the bus; the
    /// type is derived from the card's size, which for an SDXC card is above the largest size an SDHC card can have.
    sdxc,
    mmc, ///< MMC or eMMC.
};

/**
 * @headerfile <idfxx/sdcard>
 * @brief Data bus width.
 *
 * The enumerator values are the number of data lines. Cards on an SPI bus
 * always report `one`.
 */
enum class bus_width : uint8_t {
    one = 1,   ///< 1-bit bus (DAT0 only, or SPI).
    four = 4,  ///< 4-bit bus (DAT0-DAT3).
    eight = 8, ///< 8-bit bus (DAT0-DAT7, MMC/eMMC only).
};

/**
 * @headerfile <idfxx/sdcard>
 * @brief Tag type selecting discard rather than erase in card::erase_sectors().
 *
 * Use the `discard` constant rather than constructing this directly.
 */
struct discard_t {
    explicit discard_t() = default;
};

/**
 * @headerfile <idfxx/sdcard>
 * @brief Tag requesting a discard: `card.erase_sectors(first, count, idfxx::sdcard::discard)`.
 *
 * Discarded sectors are marked as unused without being cleared, so their
 * contents are undefined until they are rewritten.
 *
 * @see discard_t
 */
inline constexpr discard_t discard{};

class card;

/**
 * @headerfile <idfxx/sdcard>
 * @brief Card identification, from the card's CID register.
 *
 * A copyable value, obtained with card::id().
 */
class card_id {
public:
    /**
     * @brief Returns the product name.
     *
     * Up to five characters on SD cards, up to seven on MMC v1 devices and up
     * to six on later MMC/eMMC devices. The view refers to this card_id
     * object and is valid as long as it is.
     *
     * @return The product name.
     */
    [[nodiscard]] std::string_view product_name() const noexcept;

    /**
     * @brief Returns the manufacturer ID, assigned by the SD Association or JEDEC.
     *
     * @return The manufacturer ID.
     */
    [[nodiscard]] uint8_t manufacturer_id() const noexcept;

    /**
     * @brief Returns the OEM/application ID.
     *
     * On SD cards this is usually two ASCII characters; it is 0 on MMC v1 devices.
     *
     * @return The OEM/application ID.
     */
    [[nodiscard]] uint16_t oem_id() const noexcept;

    /**
     * @brief Returns the product revision.
     *
     * The upper and lower four bits are the major and minor revision numbers.
     * Reported as 0 by MMC/eMMC devices newer than MMC v1.
     *
     * @return The product revision.
     */
    [[nodiscard]] uint8_t revision() const noexcept;

    /**
     * @brief Returns the product serial number.
     *
     * @return The serial number.
     */
    [[nodiscard]] uint32_t serial_number() const noexcept;

    /**
     * @brief Returns the manufacturing date.
     *
     * SD cards report the year and month of manufacture. MMC/eMMC devices
     * newer than MMC v1 (which includes all eMMC) do not report a date that
     * can be decoded.
     *
     * @return The year and month of manufacture, or std::nullopt when the card does not report a valid one.
     *
     * @code
     * if (auto date = card.id().manufacture_date()) {
     *     std::println("made {}-{:02}", int(date->year()), unsigned(date->month()));
     * }
     * @endcode
     */
    [[nodiscard]] std::optional<std::chrono::year_month> manufacture_date() const noexcept;

private:
    friend class card;
    explicit card_id(const sdmmc_card_t& card) noexcept;
    sdmmc_cid_t _cid;
    uint8_t _name_len;
    bool _is_mmc;
};

/**
 * @headerfile <idfxx/sdcard>
 * @brief An initialized memory card (SD or MMC/eMMC) on an SPI bus or an SDMMC host slot.
 *
 * Construction attaches to the card and runs its initialization sequence;
 * the card is then ready for raw sector I/O or for mounting a filesystem.
 * The card owns the SPI device or SDMMC slot it uses and releases it when
 * destroyed.
 *
 * The card's state is held on the heap at a fixed address, so a filesystem
 * mounted on the card stays valid when the card object is moved. The card
 * must outlive anything mounted on it. Over SPI, the caller must also ensure
 * that the spi::master_bus outlives the card.
 *
 * Raw sector operations, probe() and the erase operations are serialized by
 * a per-card mutex, so they may be called from multiple tasks.
 *
 * While a filesystem is mounted on the card, only these may be used: the
 * identity and geometry accessors (type(), id(), size(), sector_size(),
 * sector_count(), frequency(), width()), is_present(), is_write_protected()
 * and idf_handle(). probe(), the raw sector reads and writes and the erase
 * operations must not be: they send commands to the card that bypass the
 * filesystem's locking and caches.
 *
 * A card that is removed must be initialized again once it is reinserted:
 * unmount any filesystem on it, destroy the card object, create a new one
 * once a card is present (make() fails with `errc::not_found` while the
 * card-detect pin reports an empty slot), then mount the filesystem again.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior.
 *
 * @code
 * idfxx::spi::bus_config bus_cfg{};
 * bus_cfg.mosi = idfxx::gpio_11;
 * bus_cfg.miso = idfxx::gpio_13;
 * bus_cfg.sclk = idfxx::gpio_12;
 * idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);
 *
 * idfxx::sdcard::card card(bus, {.cs = idfxx::gpio_10, .card_detect = idfxx::gpio_4});
 *
 * std::println("{} card, {} bytes", card.type(), card.size());
 * std::vector<uint8_t> sector(card.sector_size());
 * card.read_sectors(0, sector);
 * @endcode
 */
class card {
public:
    /**
     * @headerfile <idfxx/sdcard>
     * @brief Configuration for a card on an SPI bus.
     *
     * The bus must already be initialized, with DMA enabled
     * (spi::dma_chan::ch_auto or a specific channel). The card adds its own
     * device to the bus and drives its chip select itself, so the bus can be
     * shared with other devices.
     *
     * `cs`, `card_detect` and `write_protect` must not be any of the bus's
     * own SCLK, MOSI or MISO pins; this is not checked.
     *
     * @code
     * using namespace quanta::frequency_literals;
     *
     * idfxx::sdcard::card card(bus, {
     *     .cs = idfxx::gpio_10,
     *     .card_detect = idfxx::gpio_4,
     *     .max_frequency = 10_MHz,
     * });
     * @endcode
     */
    struct spi_config {
        required<idfxx::gpio> cs; ///< Chip select. Must be connected and output-capable.
        /// Card-detect input, or gpio::nc() if the slot has none. Active low: the card is present when the pin is
        /// low. The internal pull-up is enabled.
        idfxx::gpio card_detect = gpio::nc();
        idfxx::gpio write_protect = gpio::nc(); ///< Write-protect input, or gpio::nc() if the slot has none.
        /// Level of the write-protect input when the card is write-protected. The pin's internal pull-up (active
        /// low) or pull-down (active high) is enabled, so an unconnected switch reads as not protected.
        gpio::level write_protect_active_level = gpio::level::low;
        /// Upper bound on the bus clock once the card is initialized. The clock used is the lower of this and the
        /// card's own maximum; see card::frequency(). Must be at least 400 kHz, the card identification clock.
        quanta::frequency::hertz max_frequency{20'000'000};
    };

#if SOC_SDMMC_HOST_SUPPORTED || defined(__DOXYGEN__)
    /**
     * @headerfile <idfxx/sdcard>
     * @brief SDMMC host slot.
     *
     * @note Only available on targets with an SDMMC host (esp32, esp32s3, esp32p4).
     */
    enum class slot : int {
        slot_0 = 0, ///< Slot 0 (up to 8 data lines).
        slot_1 = 1, ///< Slot 1 (up to 4 data lines on esp32; up to 8 on esp32s3 and esp32p4).
    };

    /**
     * @headerfile <idfxx/sdcard>
     * @brief Configuration for a card on an SDMMC host slot.
     *
     * The SDMMC host is initialized on first use and shared between slots
     * (and with other code that uses it); each card owns its slot.
     *
     * The bus width is the number of data lines supplied: D0 alone gives a
     * 1-bit bus, D0-D3 a 4-bit bus and D0-D7 an 8-bit bus (data lines must be
     * supplied in those groups). On esp32, where each slot uses its dedicated
     * pins, the default width is the slot's maximum.
     *
     * The card's CMD and data lines need pull-ups (10 kOhm is typical).
     *
     * @note Only available on targets with an SDMMC host (esp32, esp32s3, esp32p4).
     *
     * @code
     * using namespace quanta::frequency_literals;
     *
     * idfxx::sdcard::card card({
     *     .slot = idfxx::sdcard::card::slot::slot_1,
     *     .clk = idfxx::gpio_14,
     *     .cmd = idfxx::gpio_15,
     *     .d0 = idfxx::gpio_2,
     *     .d1 = idfxx::gpio_4,
     *     .d2 = idfxx::gpio_12,
     *     .d3 = idfxx::gpio_13,
     *     .max_frequency = 40_MHz,
     * });
     * @endcode
     */
    struct sdmmc_config {
        enum slot slot = slot::slot_1; ///< Host slot.
#if SOC_SDMMC_USE_GPIO_MATRIX || defined(__DOXYGEN__)
        /// @name Signal pins
        /// Routed through the GPIO matrix, except on esp32p4 slot 0, where they must be the slot's dedicated pins
        /// (CLK 43, CMD 44, D0-D3 39-42, D4-D7 45-48; `errc::invalid_arg` otherwise).
        /// @note Absent on esp32, whose slots use dedicated pins only.
        /// @{
        required<idfxx::gpio> clk;   ///< Clock. Must be connected.
        required<idfxx::gpio> cmd;   ///< Command. Must be connected.
        required<idfxx::gpio> d0;    ///< Data line 0. Must be connected.
        idfxx::gpio d1 = gpio::nc(); ///< Data line 1 (4- and 8-bit buses), or gpio::nc().
        idfxx::gpio d2 = gpio::nc(); ///< Data line 2 (4- and 8-bit buses), or gpio::nc().
        idfxx::gpio d3 = gpio::nc(); ///< Data line 3 (4- and 8-bit buses), or gpio::nc().
        idfxx::gpio d4 = gpio::nc(); ///< Data line 4 (8-bit bus), or gpio::nc().
        idfxx::gpio d5 = gpio::nc(); ///< Data line 5 (8-bit bus), or gpio::nc().
        idfxx::gpio d6 = gpio::nc(); ///< Data line 6 (8-bit bus), or gpio::nc().
        idfxx::gpio d7 = gpio::nc(); ///< Data line 7 (8-bit bus), or gpio::nc().
                                     /// @}
#endif
        /// Bus width to use, or std::nullopt for the widest the supplied data pins allow (on esp32, the slot's
        /// maximum). A width wider than the supplied pins (or the slot) allow is rejected with `errc::invalid_arg`;
        /// a narrower one leaves the extra pins unused. The width used is the lower of this and the card's own;
        /// see card::width().
        std::optional<enum bus_width> width = std::nullopt;
        /// Card-detect input, or gpio::nc() if the slot has none. Active low: the card is present when the pin is
        /// low. Needs an external pull-up.
        idfxx::gpio card_detect = gpio::nc();
        idfxx::gpio write_protect = gpio::nc(); ///< Write-protect input, or gpio::nc() if the slot has none.
        /// Level of the write-protect input when the card is write-protected.
        gpio::level write_protect_active_level = gpio::level::low;
        /// Enables the weak internal pull-ups on the CMD and data lines. These are no substitute for external
        /// pull-ups, but can be enough for short connections at low clock rates.
        bool internal_pullups = false;
        /// Upper bound on the bus clock once the card is initialized: for example 20 MHz (default speed), 40 MHz
        /// (SD high speed) or 52 MHz (MMC high speed). The clock used is the lower of this and the card's own
        /// maximum; see card::frequency(). Must be at least 400 kHz, the card identification clock.
        quanta::frequency::hertz max_frequency{20'000'000};
#if SOC_SDMMC_IO_POWER_EXTERNAL || defined(__DOXYGEN__)
        /// The on-chip LDO channel that supplies the card's I/O voltage, or std::nullopt if the board supplies it
        /// otherwise. On Espressif's ESP32-P4 boards the SD slot is powered from channel 4. The card takes the
        /// channel when it is created and releases it when it is destroyed. A channel that doesn't exist, or that
        /// is already in use by other code, fails the card's creation with `errc::invalid_arg`.
        /// @note Only available on targets whose SD I/O can be supplied by an on-chip LDO (esp32p4).
        std::optional<int> io_power_ldo = std::nullopt;
#endif
#if SOC_SDMMC_UHS_I_SUPPORTED || defined(__DOXYGEN__)
        /// Allows UHS-I bus modes, which signal at 1.8 V. The switch to 1.8 V goes through `io_power_ldo`, which
        /// must be set: `errc::invalid_arg` otherwise.
        /// @note Only available on targets that support UHS-I (esp32p4).
        bool uhs1 = false;
#endif
    };
#endif

    // =========================================================================
    // Construction
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Initializes a card on an SPI bus.
     *
     * Adds a device for the card to @p bus and runs the card's
     * initialization sequence. Does not take ownership of @p bus: the caller
     * must ensure that the bus outlives the card.
     *
     * @param bus The SPI bus the card is on, initialized with DMA enabled.
     * @param cfg Card configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for make(spi::master_bus&, const spi_config&).
     *
     * @code
     * idfxx::sdcard::card card(bus, {.cs = idfxx::gpio_10, .card_detect = idfxx::gpio_4});
     * @endcode
     */
    [[nodiscard]] explicit card(spi::master_bus& bus, const spi_config& cfg);
#endif

    /**
     * @brief Initializes a card on an SPI bus.
     *
     * Adds a device for the card to @p bus and runs the card's
     * initialization sequence. Does not take ownership of @p bus: the caller
     * must ensure that the bus outlives the card.
     *
     * @param bus The SPI bus the card is on, initialized with DMA enabled.
     * @param cfg Card configuration.
     *
     * @return The initialized card, or an error.
     * @retval errc::invalid_arg The configuration is invalid: `cs` is not connected or not output-capable, two of
     *         the pins are the same, or `max_frequency` is below 400 kHz. Checked before the bus is used.
     * @retval errc::invalid_state The SPI bus has no free device slot for the card (each bus has a limited
     *         number of devices, shared with its other users).
     * @retval errc::not_found The card-detect pin reports that no card is inserted.
     * @retval errc::timeout The card did not respond (a missing card is reported this way when there is no
     *         card-detect pin).
     * @retval errc::invalid_response The card sent an invalid response.
     * @retval errc::invalid_crc A response failed its CRC check.
     * @retval errc::not_supported The card is an SDIO-only card, without memory.
     */
    [[nodiscard]] static result<card> make(spi::master_bus& bus, const spi_config& cfg);

#if SOC_SDMMC_HOST_SUPPORTED || defined(__DOXYGEN__)
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Initializes a card on an SDMMC host slot.
     *
     * Initializes the SDMMC host if it is not already, sets up the slot and
     * runs the card's initialization sequence.
     *
     * @param cfg Card and slot configuration.
     *
     * @note Only available on targets with an SDMMC host (esp32, esp32s3, esp32p4).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for make(const sdmmc_config&).
     *
     * @code
     * idfxx::sdcard::card card({
     *     .clk = idfxx::gpio_14,
     *     .cmd = idfxx::gpio_15,
     *     .d0 = idfxx::gpio_2,
     * });
     * @endcode
     */
    [[nodiscard]] explicit card(const sdmmc_config& cfg);
#endif

    /**
     * @brief Initializes a card on an SDMMC host slot.
     *
     * Initializes the SDMMC host if it is not already, sets up the slot and
     * runs the card's initialization sequence.
     *
     * @param cfg Card and slot configuration.
     *
     * @return The initialized card, or an error.
     * @retval errc::invalid_arg The configuration is invalid: a required pin is not connected or a pin is not
     *         usable for its signal, two of the pins are the same, the data pins are not supplied in the groups
     *         D0, D0-D3 or D0-D7, `width` is wider than the pins or the slot allow, the pins are not the slot's
     *         dedicated pins on esp32p4 slot 0, `max_frequency` is below 400 kHz, or `uhs1` is set without
     *         `io_power_ldo`. Checked before the host is used. Also returned, once the slot is set up, when
     *         `io_power_ldo` is not an LDO channel or is already in use by other code.
     * @retval errc::invalid_state The slot is already in use by another card.
     * @retval errc::not_found The card-detect pin reports that no card is inserted.
     * @retval errc::timeout The card did not respond.
     * @retval errc::invalid_response The card sent an invalid response.
     * @retval errc::invalid_crc A response failed its CRC check.
     * @retval errc::not_supported The card is an SDIO-only card, without memory.
     * @note Only available on targets with an SDMMC host (esp32, esp32s3, esp32p4).
     */
    [[nodiscard]] static result<card> make(const sdmmc_config& cfg);
#endif

    /**
     * @brief Releases the card's SPI device or SDMMC slot.
     *
     * Nothing may be mounted on the card when it is destroyed.
     */
    ~card();

    card(const card&) = delete;
    card& operator=(const card&) = delete;

    /** @brief Move constructor. Transfers ownership of the card; a mounted filesystem stays valid. */
    card(card&& other) noexcept;

    /** @brief Move assignment. Releases this card, then takes ownership of @p other's card. */
    card& operator=(card&& other) noexcept;

    // =========================================================================
    // Identity and geometry
    // =========================================================================

    /**
     * @brief Returns the kind of card.
     *
     * @return The card type.
     */
    [[nodiscard]] card_type type() const noexcept;

    /**
     * @brief Returns the card's identification.
     *
     * @return The decoded CID register.
     */
    [[nodiscard]] card_id id() const noexcept;

    /**
     * @brief Returns the card's capacity.
     *
     * @return The capacity in bytes (sector_count() times sector_size()).
     */
    [[nodiscard]] uint64_t size() const noexcept;

    /**
     * @brief Returns the size of a sector, the unit of the raw I/O operations.
     *
     * @return The sector size in bytes (normally 512).
     */
    [[nodiscard]] size_t sector_size() const noexcept;

    /**
     * @brief Returns the number of sectors on the card.
     *
     * @return The number of sectors.
     */
    [[nodiscard]] size_t sector_count() const noexcept;

    /**
     * @brief Returns the bus clock in use.
     *
     * The lower of the configured `max_frequency` and the card's maximum,
     * rounded down to a frequency the host can generate.
     *
     * @return The actual clock frequency.
     */
    [[nodiscard]] quanta::frequency::hertz frequency() const noexcept;

    /**
     * @brief Returns the data bus width in use.
     *
     * @return The bus width; always bus_width::one over SPI.
     */
    [[nodiscard]] enum bus_width width() const noexcept;

    // =========================================================================
    // Slot state
    // =========================================================================

    /**
     * @brief Returns whether a card is in the slot, according to the card-detect pin.
     *
     * Reads the pin each time it is called. Does not communicate with the
     * card, so it may be used while a filesystem is mounted on the card; use
     * probe() to check that the card responds.
     *
     * @return true if the card-detect pin is low, or if no card-detect pin is configured.
     */
    [[nodiscard]] bool is_present() const;

    /**
     * @brief Returns whether the card is write-protected, according to the write-protect pin.
     *
     * Reads the pin each time it is called. Does not communicate with the
     * card, so it may be used while a filesystem is mounted on the card.
     *
     * @return true if the write-protect pin is at its active level; false if it is not, or if no write-protect pin
     *         is configured.
     */
    [[nodiscard]] bool is_write_protected() const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Checks that the card responds, by querying its status.
     *
     * Must not be used while a filesystem is mounted on the card, whose
     * commands it is not serialized with.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the card does not respond, with the error codes listed for try_probe().
     *
     * @code
     * try {
     *     card.probe();
     * } catch (const std::system_error& e) {
     *     // card removed or not responding
     * }
     * @endcode
     */
    void probe() const;
#endif

    /**
     * @brief Checks that the card responds, by querying its status.
     *
     * Must not be used while a filesystem is mounted on the card, whose
     * commands it is not serialized with.
     *
     * @return Success, or an error.
     * @retval errc::not_found The card-detect pin reports that the card has been removed.
     * @retval errc::timeout The card did not respond.
     * @retval errc::invalid_response The card sent an invalid response.
     * @retval errc::invalid_crc The response failed its CRC check.
     */
    [[nodiscard]] result<void> try_probe() const;

    // =========================================================================
    // Raw sector I/O
    // =========================================================================
    //
    // Sizes are in bytes and must be whole sectors. Buffers may be anywhere in
    // memory, at any alignment: data in memory that DMA can't reach is copied
    // through an internal buffer. Must not be used while a filesystem is
    // mounted on the card.

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads whole sectors from the card.
     *
     * Reading no bytes succeeds without checking @p first or contacting the
     * card. Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to read.
     * @param dst   Buffer to read into; its size is the number of bytes to read, a multiple of sector_size().
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_read_sectors().
     *
     * @code
     * std::vector<uint8_t> buf(card.sector_size() * 4);
     * card.read_sectors(100, buf);
     * @endcode
     */
    void read_sectors(size_t first, std::span<uint8_t> dst) const;

    /**
     * @brief Reads whole sectors from the card.
     *
     * Reading no bytes succeeds without checking @p first or contacting the
     * card. Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to read.
     * @param dst   Buffer to read into.
     * @param size  Number of bytes to read, a multiple of sector_size().
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_read_sectors().
     */
    void read_sectors(size_t first, void* dst, size_t size) const;
#endif

    /**
     * @brief Reads whole sectors from the card.
     *
     * Reading no bytes succeeds without checking @p first or contacting the
     * card. Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to read.
     * @param dst   Buffer to read into; its size is the number of bytes to read, a multiple of sector_size().
     *
     * @return Success, or an error.
     * @retval errc::invalid_size The size is not a multiple of sector_size(), or the range extends past the end of
     *         the card.
     * @retval errc::not_found The card-detect pin reports that the card has been removed.
     * @retval errc::timeout The card did not respond.
     * @retval errc::invalid_response The card sent an invalid response.
     * @retval errc::invalid_crc Data failed its CRC check.
     */
    [[nodiscard]] result<void> try_read_sectors(size_t first, std::span<uint8_t> dst) const;

    /**
     * @brief Reads whole sectors from the card.
     *
     * Reading no bytes succeeds without checking @p first or contacting the
     * card. Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to read.
     * @param dst   Buffer to read into.
     * @param size  Number of bytes to read, a multiple of sector_size().
     *
     * @return Success, or an error.
     * @retval errc::invalid_size The size is not a multiple of sector_size(), or the range extends past the end of
     *         the card.
     * @retval errc::not_found The card-detect pin reports that the card has been removed.
     * @retval errc::timeout The card did not respond.
     * @retval errc::invalid_response The card sent an invalid response.
     * @retval errc::invalid_crc Data failed its CRC check.
     */
    [[nodiscard]] result<void> try_read_sectors(size_t first, void* dst, size_t size) const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Writes whole sectors to the card.
     *
     * Writing no bytes succeeds without checking @p first or contacting the
     * card. Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to write.
     * @param src   Data to write; its size is the number of bytes to write, a multiple of sector_size().
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_write_sectors().
     *
     * @code
     * std::vector<uint8_t> buf(card.sector_size(), 0xA5);
     * card.write_sectors(100, buf);
     * @endcode
     */
    void write_sectors(size_t first, std::span<const uint8_t> src);

    /**
     * @brief Writes whole sectors to the card.
     *
     * Writing no bytes succeeds without checking @p first or contacting the
     * card. Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to write.
     * @param src   Data to write.
     * @param size  Number of bytes to write, a multiple of sector_size().
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_write_sectors().
     */
    void write_sectors(size_t first, const void* src, size_t size);
#endif

    /**
     * @brief Writes whole sectors to the card.
     *
     * Writing no bytes succeeds without checking @p first or contacting the
     * card. Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to write.
     * @param src   Data to write; its size is the number of bytes to write, a multiple of sector_size().
     *
     * @return Success, or an error.
     * @retval errc::invalid_size The size is not a multiple of sector_size(), or the range extends past the end of
     *         the card.
     * @retval errc::invalid_state The write-protect pin reports that the card is write-protected.
     * @retval errc::not_found The card-detect pin reports that the card has been removed.
     * @retval errc::timeout The card did not respond.
     * @retval errc::invalid_response The card sent an invalid response or reported a write error.
     * @retval errc::invalid_crc The card reported a CRC error in the data.
     */
    [[nodiscard]] result<void> try_write_sectors(size_t first, std::span<const uint8_t> src);

    /**
     * @brief Writes whole sectors to the card.
     *
     * Writing no bytes succeeds without checking @p first or contacting the
     * card. Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to write.
     * @param src   Data to write.
     * @param size  Number of bytes to write, a multiple of sector_size().
     *
     * @return Success, or an error.
     * @retval errc::invalid_size The size is not a multiple of sector_size(), or the range extends past the end of
     *         the card.
     * @retval errc::invalid_state The write-protect pin reports that the card is write-protected.
     * @retval errc::not_found The card-detect pin reports that the card has been removed.
     * @retval errc::timeout The card did not respond.
     * @retval errc::invalid_response The card sent an invalid response or reported a write error.
     * @retval errc::invalid_crc The card reported a CRC error in the data.
     */
    [[nodiscard]] result<void> try_write_sectors(size_t first, const void* src, size_t size);

    // =========================================================================
    // Erase
    // =========================================================================

    /**
     * @brief Returns whether the card supports discard (erase_sectors() with ::idfxx::sdcard::discard).
     *
     * SD cards support discard only on an SDMMC host slot, never over SPI;
     * MMC/eMMC devices from version 4.5 support it.
     *
     * @return true if discard is supported.
     */
    [[nodiscard]] bool supports_discard() const noexcept;

    /**
     * @brief Returns whether the card supports sanitize().
     *
     * Only MMC/eMMC devices support sanitize.
     *
     * @return true if sanitize is supported.
     */
    [[nodiscard]] bool supports_sanitize() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Erases a range of sectors.
     *
     * On SD cards, erased sectors read back as all zeros or all ones,
     * depending on the card. On MMC/eMMC devices the range is trimmed, which
     * requires the device to support it (`errc::not_supported` otherwise).
     *
     * Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to erase.
     * @param count Number of sectors to erase. Erasing no sectors succeeds without checking @p first or contacting the
     * card.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @note Some cards on an SPI bus do not respond correctly after an erase until they are initialized again
     *       (by destroying the card object and creating a new one).
     * @throws std::system_error on failure, with the error codes listed for try_erase_sectors(size_t, size_t).
     *
     * @code
     * card.erase_sectors(100, 8);
     * @endcode
     */
    void erase_sectors(size_t first, size_t count);

    /**
     * @brief Discards a range of sectors.
     *
     * The sectors are marked as unused without being cleared: their contents
     * are undefined until they are rewritten. Requires supports_discard().
     *
     * Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to discard.
     * @param count Number of sectors to discard. Discarding no sectors succeeds without checking @p first or contacting
     * the card.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         try_erase_sectors(size_t, size_t, discard_t).
     *
     * @code
     * if (card.supports_discard()) {
     *     card.erase_sectors(100, 8, idfxx::sdcard::discard);
     * }
     * @endcode
     */
    void erase_sectors(size_t first, size_t count, discard_t);

    /**
     * @brief Erases the whole card.
     *
     * On SD cards every sector is erased. On MMC/eMMC devices that support
     * sanitize, every sector is discarded and then sanitized; on other
     * MMC/eMMC devices every sector is trimmed.
     *
     * Must not be used while a filesystem is mounted on the card. Destroys
     * all data on the card, including any partition table and filesystem.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @note Some cards on an SPI bus do not respond correctly after an erase until they are initialized again
     *       (by destroying the card object and creating a new one).
     * @throws std::system_error on failure, with the error codes listed for try_erase_all().
     */
    void erase_all();

    /**
     * @brief Physically removes data from discarded and trimmed sectors.
     *
     * Requires supports_sanitize() (MMC/eMMC only). A sanitize can take a
     * long time on a large device.
     *
     * Must not be used while a filesystem is mounted on the card.
     *
     * @param timeout How long to wait for the operation to finish. Must be positive.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_sanitize().
     *
     * @code
     * if (card.supports_sanitize()) {
     *     card.erase_sectors(100, 8, idfxx::sdcard::discard);
     *     card.sanitize(30s);
     * }
     * @endcode
     */
    void sanitize(std::chrono::milliseconds timeout);
#endif

    /**
     * @brief Erases a range of sectors.
     *
     * On SD cards, erased sectors read back as all zeros or all ones,
     * depending on the card. On MMC/eMMC devices the range is trimmed, which
     * requires the device to support it (`errc::not_supported` otherwise).
     *
     * Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to erase.
     * @param count Number of sectors to erase. Erasing no sectors succeeds without checking @p first or contacting the
     * card.
     *
     * @note Some cards on an SPI bus do not respond correctly after an erase until they are initialized again
     *       (by destroying the card object and creating a new one).
     *
     * @return Success, or an error.
     * @retval errc::invalid_size The range extends past the end of the card.
     * @retval errc::invalid_state The write-protect pin reports that the card is write-protected.
     * @retval errc::not_supported An MMC/eMMC device that does not support trim.
     * @retval errc::not_found The card-detect pin reports that the card has been removed.
     * @retval errc::timeout The card did not respond, or did not finish the erase in time.
     * @retval errc::invalid_response The card sent an invalid response or reported an erase error.
     */
    [[nodiscard]] result<void> try_erase_sectors(size_t first, size_t count);

    /**
     * @brief Discards a range of sectors.
     *
     * The sectors are marked as unused without being cleared: their contents
     * are undefined until they are rewritten. Requires supports_discard().
     *
     * Must not be used while a filesystem is mounted on the card.
     *
     * @param first The first sector to discard.
     * @param count Number of sectors to discard. Discarding no sectors succeeds without checking @p first or contacting
     * the card.
     *
     * @return Success, or an error.
     * @retval errc::invalid_size The range extends past the end of the card.
     * @retval errc::invalid_state The write-protect pin reports that the card is write-protected.
     * @retval errc::not_supported The card does not support discard (see supports_discard()).
     * @retval errc::not_found The card-detect pin reports that the card has been removed.
     * @retval errc::timeout The card did not respond, or did not finish the discard in time.
     * @retval errc::invalid_response The card sent an invalid response or reported an erase error.
     */
    [[nodiscard]] result<void> try_erase_sectors(size_t first, size_t count, discard_t);

    /**
     * @brief Erases the whole card.
     *
     * On SD cards every sector is erased. On MMC/eMMC devices that support
     * sanitize, every sector is discarded and then sanitized; on other
     * MMC/eMMC devices every sector is trimmed.
     *
     * Must not be used while a filesystem is mounted on the card. Destroys
     * all data on the card, including any partition table and filesystem.
     *
     * @note Some cards on an SPI bus do not respond correctly after an erase until they are initialized again
     *       (by destroying the card object and creating a new one).
     *
     * @return Success, or an error.
     * @retval errc::invalid_state The write-protect pin reports that the card is write-protected.
     * @retval errc::not_supported An MMC/eMMC device that supports sanitize but not discard, or that supports
     *         neither sanitize nor trim.
     * @retval errc::not_found The card-detect pin reports that the card has been removed.
     * @retval errc::timeout The card did not respond, or did not finish the erase in time.
     * @retval errc::invalid_response The card sent an invalid response or reported an erase error.
     */
    [[nodiscard]] result<void> try_erase_all();

    /**
     * @brief Physically removes data from discarded and trimmed sectors.
     *
     * Requires supports_sanitize() (MMC/eMMC only). A sanitize can take a
     * long time on a large device.
     *
     * Must not be used while a filesystem is mounted on the card.
     *
     * @param timeout How long to wait for the operation to finish. Must be positive.
     *
     * @return Success, or an error.
     * @retval errc::invalid_arg @p timeout is not positive.
     * @retval errc::invalid_state The write-protect pin reports that the card is write-protected.
     * @retval errc::not_supported The card does not support sanitize (see supports_sanitize()).
     * @retval errc::not_found The card-detect pin reports that the card has been removed.
     * @retval errc::timeout The card did not respond, or did not finish within @p timeout.
     * @retval errc::invalid_response The card sent an invalid response or rejected the operation.
     */
    [[nodiscard]] result<void> try_sanitize(std::chrono::milliseconds timeout);

    /**
     * @brief Returns the underlying ESP-IDF card structure.
     *
     * The structure is at a fixed address for the card's lifetime, even if
     * the card object is moved, so it can be passed to ESP-IDF functions
     * such as esp_vfs_fat_register() or sdmmc_card_print_info(). Commands
     * sent through it are not serialized with this object's operations.
     *
     * @return The card structure.
     */
    [[nodiscard]] sdmmc_card_t* idf_handle() const noexcept;

private:
    /// @cond INTERNAL
    struct state;
    explicit card(std::unique_ptr<state> s) noexcept;
    /// @endcond

    std::unique_ptr<state> _state;
};

} // namespace idfxx::sdcard

/** @} */ // end of idfxx_sdcard

namespace idfxx {

/**
 * @headerfile <idfxx/sdcard>
 * @brief Returns a string representation of a card type.
 *
 * @param t The card type to convert.
 * @return "SDSC", "SDHC", "SDXC", "MMC", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(sdcard::card_type t);

/**
 * @headerfile <idfxx/sdcard>
 * @brief Returns a string representation of a bus width.
 *
 * @param w The bus width to convert.
 * @return "1-bit", "4-bit", "8-bit", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(sdcard::bus_width w);

#if SOC_SDMMC_HOST_SUPPORTED || defined(__DOXYGEN__)
/**
 * @headerfile <idfxx/sdcard>
 * @brief Returns a string representation of an SDMMC host slot.
 *
 * @param s The slot to convert.
 * @return "slot_0", "slot_1", or "unknown(N)" for unrecognized values.
 * @note Only available on targets with an SDMMC host (esp32, esp32s3, esp32p4).
 */
[[nodiscard]] std::string to_string(sdcard::card::slot s);
#endif

} // namespace idfxx

#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::sdcard::card_type> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::sdcard::card_type t, FormatContext& ctx) const {
        auto s = idfxx::to_string(t);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::sdcard::bus_width> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::sdcard::bus_width w, FormatContext& ctx) const {
        auto s = idfxx::to_string(w);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

#if SOC_SDMMC_HOST_SUPPORTED
template<>
struct formatter<idfxx::sdcard::card::slot> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::sdcard::card::slot s, FormatContext& ctx) const {
        auto str = idfxx::to_string(s);
        return std::copy(str.begin(), str.end(), ctx.out());
    }
};
#endif

} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
