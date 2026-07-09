// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/radio/sx126x>
 * @file sx126x.hpp
 * @brief Semtech SX126x family LoRa radio driver (SX1261/SX1262/SX1268).
 * @ingroup idfxx_radio
 */

#include <idfxx/cpu>
#include <idfxx/error>
#include <idfxx/event>
#include <idfxx/flags>
#include <idfxx/future>
#include <idfxx/gpio>
#include <idfxx/radio/events.hpp>
#include <idfxx/radio/transceiver.hpp>
#include <idfxx/radio/types.hpp>
#include <idfxx/spi/master>

#include <chrono>
#include <cstdint>
#include <frequency/frequency>
#include <memory>
#include <optional>
#include <span>

namespace idfxx::radio {

/**
 * @headerfile <idfxx/radio/sx126x>
 * @brief SX126x IRQ-register bitfield.
 *
 * The chip's IRQ register is 16 bits wide; each bit reflects an
 * underlying chip event. The driver normally translates these into
 * chip-agnostic events from `<idfxx/radio/events>` — only reach for
 * this surface when you need direct register-level access. Accessible
 * as `sx126x::irq_flag` after the class definition.
 */
enum class sx126x_irq_flag : uint16_t {
    tx_done = 1 << 0,
    rx_done = 1 << 1,
    preamble_detected = 1 << 2,
    sync_word_valid = 1 << 3,
    header_valid = 1 << 4,
    header_err = 1 << 5,
    crc_err = 1 << 6,
    cad_done = 1 << 7,
    cad_detected = 1 << 8,
    timeout = 1 << 9,
};

} // namespace idfxx::radio

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::radio::sx126x_irq_flag> = true;

namespace idfxx::radio {

/**
 * @headerfile <idfxx/radio/sx126x>
 * @brief Concrete LoRa radio driver for the Semtech SX126x family.
 *
 * Covers the SX1261 (sub-GHz, max +15 dBm), SX1262 (sub-GHz, max +22 dBm),
 * and SX1268 (sub-GHz, max +22 dBm, PA tuned for 410–810 MHz). The variant
 * is selected at construction via `config::variant`; the driver then picks
 * the correct PA-config table and validates output-power range.
 *
 * Move-only and non-copyable, like the rest of idfxx. The destructor
 * tears down the worker task, removes the DIO1 ISR, and puts the chip in
 * sleep mode.
 */
class sx126x final : public transceiver {
public:
    /// IRQ-register bitfield enumeration. See @ref sx126x_irq_flag.
    using irq_flag = sx126x_irq_flag;

    /**
     * @headerfile <idfxx/radio/sx126x>
     * @brief SX126x family chip variant.
     *
     * Determines the PA configuration the driver applies and the output-power
     * range it accepts from `set_output_power`.
     */
    enum class chip_variant : uint8_t {
        sx1261, ///< Sub-GHz, low-power PA, max +15 dBm.
        sx1262, ///< Sub-GHz, high-power PA, max +22 dBm.
        sx1268, ///< Sub-GHz, high-power PA tuned for 410–810 MHz, max +22 dBm.
    };

    /**
     * @headerfile <idfxx/radio/sx126x>
     * @brief Voltage regulator selection.
     */
    enum class regulator : uint8_t {
        ldo = 0x00,   ///< LDO regulator (universally safe).
        dc_dc = 0x01, ///< DC-DC + LDO (requires board support).
    };

    /**
     * @headerfile <idfxx/radio/sx126x>
     * @brief Clock source kept running in standby mode.
     *
     * The chip-agnostic @ref transceiver::standby uses the low-power RC oscillator;
     * the SX126x-specific @ref standby(standby_clock) overload selects the
     * crystal oscillator for a faster transition into transmit or receive.
     */
    enum class standby_clock : uint8_t {
        rc,   ///< 13 MHz RC oscillator (lowest standby current).
        xosc, ///< Crystal oscillator (faster TX/RX transitions, higher current).
    };

    /**
     * @headerfile <idfxx/radio/sx126x>
     * @brief Sleep-mode configuration retention.
     *
     * The chip-agnostic @ref transceiver::sleep performs a warm sleep; the
     * SX126x-specific @ref sleep(sleep_mode) overload can request a cold
     * sleep, which loses all configuration and requires the driver's defaults
     * and the caller's settings to be reapplied on wake.
     */
    enum class sleep_mode : uint8_t {
        warm, ///< Retain register configuration for fast wake-up.
        cold, ///< Lowest-power sleep; all configuration is lost.
    };

    /**
     * @headerfile <idfxx/radio/sx126x>
     * @brief TCXO configuration applied through DIO3.
     *
     * Set `config::tcxo` only if your board's reference is a TCXO driven
     * from the SX126x DIO3 pin. Otherwise leave it unset — the chip will
     * use the crystal oscillator.
     */
    struct tcxo_config {
        uint16_t voltage_mv = 1700;               ///< TCXO supply voltage (1600–3300 mV).
        std::chrono::microseconds startup{5'000}; ///< TCXO startup time before the chip uses it.
    };

    /**
     * @headerfile <idfxx/radio/sx126x>
     * @brief SX126x-specific channel-activity-detection parameters.
     *
     * @ref transceiver::try_start_channel_scan and @ref transceiver::try_scan_channel
     * scan with driver defaults (`symbol_num = 2`, `det_peak = 22`,
     * `det_min = 10`). Use @ref try_set_cad_params or @ref set_cad_params to
     * tune.
     */
    struct cad_params {
        uint8_t symbol_num = 2; ///< Number of symbols on which CAD operates.
        uint8_t det_peak = 22;  ///< Detector peak threshold.
        uint8_t det_min = 10;   ///< Detector minimum threshold.
    };

    /**
     * @headerfile <idfxx/radio/sx126x>
     * @brief Configuration for an SX126x driver instance.
     *
     * Most fields have sensible defaults. The cs, busy, and dio1 GPIO pins
     * are required and must be set; nreset is strongly recommended (without
     * it the chip is not reset at construction and is assumed to be in its
     * power-on state).
     */
    struct config {
        chip_variant variant = chip_variant::sx1262;    ///< Chip family member.
        idfxx::gpio cs = gpio::nc();                    ///< SPI chip-select pin (required).
        idfxx::gpio busy = gpio::nc();                  ///< BUSY input from chip (required).
        idfxx::gpio dio1 = gpio::nc();                  ///< DIO1 IRQ input from chip (required).
        idfxx::gpio nreset = gpio::nc();                ///< Active-low reset to chip; nc() skips the hardware reset.
        freq::hertz clock_speed{10'000'000};            ///< SPI clock; chip max is 18 MHz.
        bool dio2_as_rf_switch = true;                  ///< Use DIO2 as the antenna T/R switch.
        idfxx::gpio rxen = gpio::nc();                  ///< Optional external RF-switch RX-enable pin; nc() if unused.
        idfxx::gpio txen = gpio::nc();                  ///< Optional external RF-switch TX-enable pin; nc() if unused.
        std::optional<tcxo_config> tcxo = std::nullopt; ///< Optional TCXO; leave unset for crystal.
        enum regulator regulator = regulator::ldo;      ///< Voltage regulator selection.
        bool rx_boost = false;                          ///< Use boosted-RX gain (more sensitive, higher current).
        std::chrono::milliseconds busy_timeout{100};    ///< Max wait for BUSY to go low.
        /// IRQ flags latched by the chip and routed to DIO1. Leave unset for
        /// the driver default (tx_done, rx_done, preamble_detected, crc_err,
        /// cad_done, cad_detected). Wake-on-radio designs that sleep the host
        /// while the chip listens typically drop preamble_detected so DIO1
        /// only rises once a packet has fully arrived.
        std::optional<flags<sx126x_irq_flag>> dio1_mask = std::nullopt;
        /// Attach to an already configured chip instead of resetting and
        /// reconfiguring it. No reset pulse and no configuration command is
        /// issued at construction — the chip is assumed to carry the
        /// configuration a previous (cold-started) driver instance applied,
        /// e.g. across a host deep sleep the chip spent duty-cycle
        /// listening. Use @ref try_adopt_pending after construction to
        /// recover a packet the chip received while no driver was running.
        bool warm_start = false;
        event_loop* loop = nullptr;        ///< Event loop to post radio events on; nullptr disables posting.
        task_priority worker_priority{18}; ///< Worker task priority.
        size_t worker_stack_size = 4096;   ///< Worker task stack size in bytes.
        std::optional<core_id> worker_core = std::nullopt; ///< Worker task core affinity.
    };

    // =========================================================================
    // Construction / lifetime
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Constructs a new SX126x driver on the given SPI bus.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this driver.
     *
     * @param bus    Parent SPI master bus.
     * @param config Driver configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit sx126x(spi::master_bus& bus, config config);
#endif

    /**
     * @brief Creates a new SX126x driver on the given SPI bus.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this driver.
     *
     * @param bus    Parent SPI master bus.
     * @param config Driver configuration.
     * @return The new driver, or an error.
     */
    [[nodiscard]] static result<sx126x> make(spi::master_bus& bus, config config);

    ~sx126x() override;

    sx126x(const sx126x&) = delete;
    sx126x& operator=(const sx126x&) = delete;
    sx126x(sx126x&& other) noexcept;
    sx126x& operator=(sx126x&& other) noexcept;

    // =========================================================================
    // Accessors
    // =========================================================================

    /** @brief Returns the configured chip variant. */
    [[nodiscard]] chip_variant variant() const noexcept;

    /**
     * @brief Returns the underlying SPI device for advanced/diagnostic use.
     * @pre The driver has not been moved from.
     */
    [[nodiscard]] spi::master_device& spi() noexcept;

    // =========================================================================
    // SX126x-specific mode control
    //
    // The chip-agnostic mode-control methods (standby, sleep, start_receive,
    // start_channel_scan, ...) are inherited from radio::transceiver. The overloads
    // here expose SX126x-specific refinements of the same operations.
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    using transceiver::sleep;
    using transceiver::standby;
#endif
    using transceiver::try_sleep;
    using transceiver::try_standby;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Puts the radio in standby mode with the selected clock source.
     *
     * The inherited @ref transceiver::standby uses the low-power RC oscillator;
     * select @ref standby_clock::xosc to keep the crystal oscillator running
     * for faster transitions into transmit or receive.
     *
     * @param clock Clock source to keep running in standby.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void standby(standby_clock clock) { unwrap(try_standby(clock)); }

    /**
     * @brief Puts the radio in sleep mode with the selected retention.
     *
     * The inherited @ref transceiver::sleep performs a warm sleep (configuration
     * retained). Select @ref sleep_mode::cold for the lowest-power sleep, in
     * which all configuration is lost and must be reapplied on wake.
     *
     * @param mode Configuration-retention mode.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void sleep(sleep_mode mode) { unwrap(try_sleep(mode)); }

    /**
     * @brief Puts the radio in frequency-synthesis mode (PLL locked, transceiver idle).
     *
     * Primarily a diagnostic mode for verifying PLL lock; normal operation
     * never needs it.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_fs() { unwrap(try_set_fs()); }
#endif

    /**
     * @brief Puts the radio in standby mode with the selected clock source.
     * @param clock Clock source to keep running in standby.
     * @return Success, or an error.
     */
    result<void> try_standby(standby_clock clock);

    /**
     * @brief Puts the radio in sleep mode with the selected retention.
     * @param mode Configuration-retention mode.
     * @return Success, or an error.
     */
    result<void> try_sleep(sleep_mode mode);

    /**
     * @brief Puts the radio in frequency-synthesis mode (PLL locked, transceiver idle).
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_fs();

    // =========================================================================
    // SX126x-specific surface
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets CAD detection parameters.
     *
     * Tunes the chip's CAD detection thresholds. The chip-agnostic
     * @ref transceiver::try_start_channel_scan uses driver defaults.
     *
     * @param params CAD detection parameters.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_cad_params(cad_params params) { unwrap(try_set_cad_params(params)); }

    /**
     * @brief Drains an IRQ the chip latched while no driver was running.
     *
     * Intended for use immediately after a warm start (see
     * `config::warm_start`): a packet the chip received while the host slept
     * leaves `rx_done` latched and DIO1 high, but the driver's interrupt
     * handler never saw the edge. This reads and clears the pending IRQ
     * status and, for a latched `rx_done`, reads the packet out of the chip
     * into the receive cache, where @ref transceiver::read_received picks it up.
     *
     * @return The received packet's info if one was adopted, or `std::nullopt`
     *         if no packet was pending.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (including a corrupt pending
     *         packet, reported as `idfxx::errc::invalid_crc`).
     *
     * @code
     * // After waking from deep sleep with the chip left duty-cycle listening:
     * idfxx::radio::sx126x radio(bus, {..., .nreset = idfxx::gpio::nc(), .warm_start = true});
     * if (auto pkt = radio.adopt_pending()) {
     *     std::array<uint8_t, 255> buf;
     *     auto info = radio.read_received(buf);
     *     // handle buf[0..info.length)
     * }
     * @endcode
     */
    [[nodiscard]] std::optional<rx_info> adopt_pending() { return unwrap(try_adopt_pending()); }

    /**
     * @brief Returns the current IRQ-register status.
     * @return Bitfield of pending IRQs.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] flags<irq_flag> get_irq_status() { return unwrap(try_get_irq_status()); }

    /**
     * @brief Clears the specified IRQ bits.
     * @param mask Bits to clear.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void clear_irq_status(flags<irq_flag> mask) { unwrap(try_clear_irq_status(mask)); }
#endif

    /**
     * @brief Drains an IRQ the chip latched while no driver was running.
     *
     * Result variant of @ref adopt_pending; see there for the warm-start
     * recovery flow this supports.
     *
     * @return The received packet's info if one was adopted, `std::nullopt`
     *         if no packet was pending, or an error.
     * @retval idfxx::errc::invalid_crc A packet was pending but failed the
     *         chip's CRC check; the IRQ has been cleared.
     */
    [[nodiscard]] result<std::optional<rx_info>> try_adopt_pending();

    /**
     * @brief Sets CAD detection parameters.
     * @param params CAD detection parameters.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_cad_params(cad_params params);

    /**
     * @brief Returns the current IRQ-register status.
     * @return Bitfield of pending IRQs, or an error.
     */
    [[nodiscard]] result<flags<irq_flag>> try_get_irq_status();

    /**
     * @brief Clears the specified IRQ bits.
     * @param mask Bits to clear.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_clear_irq_status(flags<irq_flag> mask);

    // =========================================================================
    // Low-level opcode escape hatch
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a command opcode with parameter bytes.
     * @param opcode Command opcode.
     * @param params Parameter bytes to send after the opcode.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write_command(uint8_t opcode, std::span<const uint8_t> params) { unwrap(try_write_command(opcode, params)); }

    /**
     * @brief Sends a command opcode and reads the response bytes.
     * @param opcode   Command opcode.
     * @param response Buffer to receive response bytes into.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void read_command(uint8_t opcode, std::span<uint8_t> response) { unwrap(try_read_command(opcode, response)); }

    /**
     * @brief Writes one or more chip registers.
     * @param addr 16-bit register address.
     * @param data Bytes to write.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write_register(uint16_t addr, std::span<const uint8_t> data) { unwrap(try_write_register(addr, data)); }

    /**
     * @brief Reads one or more chip registers.
     * @param addr 16-bit register address.
     * @param data Buffer to receive into.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void read_register(uint16_t addr, std::span<uint8_t> data) { unwrap(try_read_register(addr, data)); }

    /**
     * @brief Writes the chip's data buffer at the specified offset.
     * @param offset Byte offset within the 256-byte chip buffer.
     * @param data   Bytes to write.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write_buffer(uint8_t offset, std::span<const uint8_t> data) { unwrap(try_write_buffer(offset, data)); }

    /**
     * @brief Reads the chip's data buffer at the specified offset.
     * @param offset Byte offset within the 256-byte chip buffer.
     * @param data   Buffer to receive into.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void read_buffer(uint8_t offset, std::span<uint8_t> data) { unwrap(try_read_buffer(offset, data)); }
#endif

    /** @brief Result variant of @ref write_command. */
    [[nodiscard]] result<void> try_write_command(uint8_t opcode, std::span<const uint8_t> params);
    /** @brief Result variant of @ref read_command. */
    [[nodiscard]] result<void> try_read_command(uint8_t opcode, std::span<uint8_t> response);
    /** @brief Result variant of @ref write_register. */
    [[nodiscard]] result<void> try_write_register(uint16_t addr, std::span<const uint8_t> data);
    /** @brief Result variant of @ref read_register. */
    [[nodiscard]] result<void> try_read_register(uint16_t addr, std::span<uint8_t> data);
    /** @brief Result variant of @ref write_buffer. */
    [[nodiscard]] result<void> try_write_buffer(uint8_t offset, std::span<const uint8_t> data);
    /** @brief Result variant of @ref read_buffer. */
    [[nodiscard]] result<void> try_read_buffer(uint8_t offset, std::span<uint8_t> data);

    /// @cond INTERNAL
    struct state;
    /// @endcond

private:
    explicit sx126x(std::unique_ptr<state> s) noexcept;

    // radio::transceiver customization hooks.
    [[nodiscard]] chip_mode do_current_mode() const noexcept override;
    [[nodiscard]] result<void> do_standby() override;
    [[nodiscard]] result<void> do_sleep() override;
    [[nodiscard]] result<void> do_start_receive() override;
    [[nodiscard]] result<void>
    do_start_receive(std::chrono::microseconds rx_period, std::chrono::microseconds sleep_period) override;
    [[nodiscard]] result<idfxx::future<rx_info>> do_start_receive(std::span<uint8_t> buffer) override;
    [[nodiscard]] result<idfxx::future<cad_info>> do_start_channel_scan() override;
    [[nodiscard]] result<void> do_set_frequency(freq::hertz hz) override;
    [[nodiscard]] result<void> do_set_output_power(int8_t dbm, ramp_time ramp) override;
    [[nodiscard]] result<void> do_set_lora_modulation(lora_modulation mod) override;
    [[nodiscard]] result<void> do_set_lora_packet_params(lora_packet_params params) override;
    [[nodiscard]] result<void> do_set_sync_word(uint16_t sync_word) override;
    [[nodiscard]] result<idfxx::future<void>> do_start_transmit(std::span<const uint8_t> data) override;
    [[nodiscard]] result<rx_info> do_read_received(std::span<uint8_t> buffer) override;
    [[nodiscard]] result<packet_status> do_get_packet_status() override;
    [[nodiscard]] result<int16_t> do_get_rssi_inst_dbm() override;

    std::unique_ptr<state> _state;
};

} // namespace idfxx::radio
