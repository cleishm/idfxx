// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/i2c/master>
 * @file master.hpp
 * @brief I2C master bus and device classes.
 *
 * @defgroup idfxx_i2c I2C Component
 * @brief Type-safe I2C master bus driver for ESP32.
 *
 * Provides I2C bus lifecycle management with thread-safe device access
 * and register operations.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin configuration.
 * @{
 */

#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/intr_alloc>

#include <array>
#include <cassert>
#include <chrono>
#include <concepts>
#include <frequency/frequency>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

typedef struct i2c_master_bus_t* i2c_master_bus_handle_t;
typedef struct i2c_master_dev_t* i2c_master_dev_handle_t;

/**
 * @headerfile <idfxx/i2c/master>
 * @brief I2C master driver classes.
 */
namespace idfxx::i2c {

/** @brief Default timeout for I2C operations. */
static constexpr auto DEFAULT_TIMEOUT = std::chrono::milliseconds(50);

/**
 * @headerfile <idfxx/i2c/master>
 * @brief I2C port identifiers.
 */
enum class port : int {
    i2c0 = 0, ///< I2C port 0
#if SOC_HP_I2C_NUM >= 2
    i2c1 = 1, ///< I2C port 1
#endif
#if SOC_LP_I2C_NUM >= 1
    lp_i2c0 = 2, ///< LP_I2C port 0
#endif
};

// Clock source enums use sequential values rather than matching the ESP-IDF
// constants directly, since the underlying SOC_MOD_CLK_* values differ per
// target. Conversion to ESP-IDF types is done in the implementation.

/**
 * @headerfile <idfxx/i2c/master>
 * @brief I2C master bus clock source.
 */
enum class clk_source : int {
    default_source = 0, ///< Default clock source for the target.
#if SOC_I2C_SUPPORT_APB
    apb, ///< APB clock source.
#endif
#if SOC_I2C_SUPPORT_REF_TICK
    ref_tick, ///< REF_TICK clock source.
#endif
#if SOC_I2C_SUPPORT_XTAL
    xtal, ///< XTAL clock source.
#endif
#if SOC_I2C_SUPPORT_RTC
    rc_fast, ///< RC_FAST clock source.
#endif
};

#if SOC_LP_I2C_SUPPORTED
/**
 * @headerfile <idfxx/i2c/master>
 * @brief LP I2C bus clock source.
 */
enum class lp_clk_source : int {
    default_source = 0, ///< Default LP clock source for the target.
    lp_fast,            ///< LP_FAST (RTC_FAST) clock source.
    xtal_d2,            ///< XTAL_D2 clock source.
};
#endif

/**
 * @headerfile <idfxx/i2c/master>
 * @brief I2C operation command type for custom transaction sequences.
 */
enum class operation_command : int {
    start = 0, ///< Send START or repeated-START condition.
    write = 1, ///< Write data to the bus.
    read = 2,  ///< Read data from the bus.
    stop = 3,  ///< Send STOP condition.
};

/**
 * @headerfile <idfxx/i2c/master>
 * @brief ACK value sent after a read operation.
 */
enum class ack_value : uint8_t {
    ack = 0,  ///< Acknowledge — request more data.
    nack = 1, ///< Not-acknowledge — signal end of read.
};

/**
 * @headerfile <idfxx/i2c/master>
 * @brief Describes a single operation in a custom I2C transaction sequence.
 *
 * Use with master_device::execute_operations() to perform arbitrary
 * START / WRITE / READ / STOP sequences.
 *
 * For write operations, set command to operation_command::write and
 * populate write_data. For read operations, set command to
 * operation_command::read, populate read_data, and set ack_type
 * (must be ack_value::nack if the next operation is a STOP).
 */
struct operation {
    operation_command command;           ///< The operation to perform.
    bool ack_check = true;               ///< (write) Whether to check for ACK.
    std::span<const uint8_t> write_data; ///< (write) Data to send.
    std::span<uint8_t> read_data;        ///< (read) Buffer to receive into.
    ack_value ack_type = ack_value::ack; ///< (read) ACK value to send after reading.
};

/**
 * @headerfile <idfxx/i2c/master>
 * @brief I2C master bus controller with thread-safe device access.
 *
 * Represents an I2C bus operating in master mode. Satisfies the Lockable
 * named requirement for use with std::lock_guard and std::unique_lock.
 *
 * This type is non-copyable and move-only. Result-returning methods on a
 * moved-from object return errc::invalid_state. Simple accessors return
 * default/null values. The Lockable methods (lock/try_lock/unlock)
 * silently no-op on a moved-from object.
 */
class master_bus {
public:
    /**
     * @brief I2C master bus configuration.
     *
     * All fields have sensible defaults, but you should define sda, scl, and frequency at minimum. For example:
     *
     * @code
     * idfxx::i2c::master_bus bus(idfxx::i2c::port::i2c0, {
     *     .sda = idfxx::gpio_21,
     *     .scl = idfxx::gpio_22,
     *     .frequency = 400_kHz,
     *     .glitch_ignore_cnt = 10,
     *     .enable_internal_pullup = false,
     * });
     * @endcode
     */
    struct config {
        idfxx::gpio sda = gpio::nc();                            ///< GPIO pin for the SDA line.
        idfxx::gpio scl = gpio::nc();                            ///< GPIO pin for the SCL line.
        freq::hertz frequency{0};                                ///< Clock frequency in Hz.
        enum clk_source clk_source = clk_source::default_source; ///< Clock source for the I2C master bus.
#if SOC_LP_I2C_SUPPORTED
        enum lp_clk_source lp_source_clk = lp_clk_source::default_source; ///< Clock source for LP I2C bus.
#endif
        uint8_t glitch_ignore_cnt = 7; ///< Glitch filter count (0-7). Higher values filter more noise.
        std::optional<idfxx::intr_level> intr_level = {}; ///< Interrupt priority level, or empty for automatic.
        size_t trans_queue_depth = 0;       ///< Depth of internal transfer queue for asynchronous transactions.
        bool enable_internal_pullup = true; ///< Enable internal pull-up resistors on SDA and SCL.
        bool allow_pd = false;              ///< Allow powering down the bus during light sleep.
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new I2C master bus.
     *
     * @param port   I2C port number.
     * @param config Bus configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     *
     * @code
     * idfxx::i2c::master_bus bus(idfxx::i2c::port::i2c0, {
     *     .sda = idfxx::gpio_21,
     *     .scl = idfxx::gpio_22,
     *     .frequency = 400_kHz,
     *     .enable_internal_pullup = false,
     * });
     * @endcode
     */
    [[nodiscard]] explicit master_bus(enum port port, const struct config& config);

    /**
     * @brief Creates a new I2C master bus with default settings.
     *
     * @param port      I2C port number.
     * @param sda       GPIO pin for the SDA line.
     * @param scl       GPIO pin for the SCL line.
     * @param frequency Clock frequency in Hz.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit master_bus(enum port port, gpio sda, gpio scl, freq::hertz frequency);
#endif

    /**
     * @brief Creates a new I2C master bus.
     *
     * @param port   I2C port number.
     * @param config Bus configuration.
     *
     * @return The new master_bus, or an error.
     *
     * @code
     * auto bus = idfxx::i2c::master_bus::make(idfxx::i2c::port::i2c0, {
     *     .sda = idfxx::gpio_21,
     *     .scl = idfxx::gpio_22,
     *     .frequency = 400_kHz,
     *     .enable_internal_pullup = false,
     * });
     * @endcode
     */
    [[nodiscard]] static result<master_bus> make(enum port port, const struct config& config);

    /**
     * @brief Creates a new I2C master bus with default settings.
     *
     * @param port      I2C port number.
     * @param sda       GPIO pin for the SDA line.
     * @param scl       GPIO pin for the SCL line.
     * @param frequency Clock frequency in Hz.
     *
     * @return The new master_bus, or an error.
     */
    [[nodiscard]] static result<master_bus> make(enum port port, gpio sda, gpio scl, freq::hertz frequency);

    ~master_bus();

    master_bus(const master_bus&) = delete;
    master_bus& operator=(const master_bus&) = delete;
    master_bus(master_bus&& other) noexcept;
    master_bus& operator=(master_bus&& other) noexcept;

    /** @brief Acquires exclusive access to the bus. */
    void lock() const {
        if (!_mux) {
            return;
        }
        _mux->lock();
    }

    /** @brief Tries to acquire exclusive access without blocking. */
    [[nodiscard]] bool try_lock() const noexcept {
        if (!_mux) {
            return false;
        }
        return _mux->try_lock();
    }

    /** @brief Releases exclusive access to the bus. */
    void unlock() const {
        if (!_mux) {
            return;
        }
        _mux->unlock();
    }

    /** @brief Returns the underlying ESP-IDF bus handle. */
    [[nodiscard]] i2c_master_bus_handle_t handle() const { return _handle; }

    /** @brief Returns the I2C port. */
    [[nodiscard]] enum port port() const { return _port; }

    /** @brief Returns the bus clock frequency in Hz. */
    [[nodiscard]] freq::hertz frequency() const { return _frequency; }

    /**
     * @brief Scans for devices on the bus.
     *
     * Probes addresses 0x08-0x77 and returns those that acknowledge.
     *
     * @return Addresses of responding devices.
     */
    [[nodiscard]] std::vector<uint8_t> scan_devices() const { return scan_devices(DEFAULT_TIMEOUT); }

    /**
     * @brief Scans for devices on the bus.
     *
     * Probes addresses 0x08-0x77 and returns those that acknowledge.
     *
     * @param timeout Maximum time to wait for each probe.
     *
     * @return Addresses of responding devices.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] std::vector<uint8_t> scan_devices(const std::chrono::duration<Rep, Period>& timeout) const {
        return _scan_devices(std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Probes for a device at the specified address.
     *
     * @param address Device address.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the device does not acknowledge or on error.
     */
    void probe(uint16_t address) const { probe(address, DEFAULT_TIMEOUT); }

    /**
     * @brief Probes for a device at the specified address.
     *
     * @param address Device address.
     * @param timeout Maximum time to wait for response.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the device does not acknowledge or on error.
     */
    template<typename Rep, typename Period>
    void probe(uint16_t address, const std::chrono::duration<Rep, Period>& timeout) const {
        unwrap(_probe(address, std::chrono::ceil<std::chrono::milliseconds>(timeout)));
    }
#endif

    /**
     * @brief Probes for a device at the specified address.
     *
     * @param address Device address.
     *
     * @return Success if the device acknowledges, or an error.
     */
    [[nodiscard]] result<void> try_probe(uint16_t address) const { return try_probe(address, DEFAULT_TIMEOUT); }

    /**
     * @brief Probes for a device at the specified address.
     *
     * @param address Device address.
     * @param timeout Maximum time to wait for response.
     *
     * @return Success if the device acknowledges, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_probe(uint16_t address, const std::chrono::duration<Rep, Period>& timeout) const {
        return _probe(address, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Waits for all pending asynchronous operations on the bus to complete.
     *
     * Blocks until every queued transaction has finished or the timeout expires.
     * Only useful when the bus was created with a non-zero trans_queue_depth.
     *
     * @param timeout Maximum time to wait.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on timeout or failure.
     */
    template<typename Rep, typename Period>
    void wait_all_done(const std::chrono::duration<Rep, Period>& timeout) const {
        unwrap(try_wait_all_done(timeout));
    }
#endif

    /**
     * @brief Waits for all pending asynchronous operations on the bus to complete.
     *
     * Blocks until every queued transaction has finished or the timeout expires.
     * Only useful when the bus was created with a non-zero trans_queue_depth.
     *
     * @param timeout Maximum time to wait.
     *
     * @return Success, or an error (e.g. errc::timeout).
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_wait_all_done(const std::chrono::duration<Rep, Period>& timeout) const {
        return _try_wait_all_done(std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

private:
    explicit master_bus(i2c_master_bus_handle_t handle, enum port port, freq::hertz frequency);

    void _delete() noexcept;

    [[nodiscard]] std::vector<uint8_t> _scan_devices(std::chrono::milliseconds timeout) const;
    [[nodiscard]] result<void> _probe(uint16_t address, std::chrono::milliseconds timeout) const;
    [[nodiscard]] result<void> _try_wait_all_done(std::chrono::milliseconds timeout) const;

    mutable std::unique_ptr<std::recursive_mutex> _mux;
    i2c_master_bus_handle_t _handle = nullptr;
    enum port _port;
    freq::hertz _frequency;
};

/**
 * @headerfile <idfxx/i2c/master>
 * @brief I2C device at a specific address with register operations.
 *
 * Represents a specific device on an I2C bus. Provides methods for
 * raw data transfer and register-based read/write operations.
 *
 * This type is non-copyable and move-only. Result-returning methods on a
 * moved-from object return errc::invalid_state. Simple accessors return
 * default/null values.
 */
class master_device {
public:
    /**
     * @brief I2C device configuration.
     *
     * Provides per-device settings for SCL speed, addressing mode, and ACK behavior.
     *
     * @code
     * idfxx::i2c::master_device device(bus, 0x3C, {
     *     .scl_speed = 400_kHz,
     *     .disable_ack_check = true,
     * });
     * @endcode
     */
    struct config {
        freq::hertz scl_speed{0}; ///< Per-device SCL speed in Hz (0 = use bus frequency).
        uint32_t scl_wait_us = 0; ///< SCL wait time in microseconds (0 = default).
#if SOC_I2C_SUPPORT_10BIT_ADDR
        bool addr_10bit = false; ///< Use 10-bit addressing mode instead of 7-bit.
#endif
        bool disable_ack_check = false; ///< Disable ACK checking for this device.
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new device on the specified bus.
     *
     * Does not take ownership of @p bus. It is the caller's responsibility to ensure that
     * this device does not outlive the bus.
     *
     * @param bus     The parent bus.
     * @param address Device address.
     * @param config  Device configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     *
     * @code
     * idfxx::i2c::master_device device(bus, 0x3C, {
     *     .scl_speed = 400_kHz,
     * });
     * @endcode
     */
    [[nodiscard]] explicit master_device(master_bus& bus, uint16_t address, const struct config& config);

    /**
     * @brief Creates a new device on the specified bus.
     *
     * Does not take ownership of @p bus. It is the caller's responsibility to ensure that
     * this device does not outlive the bus.
     *
     * @param bus     The parent bus.
     * @param address Device address.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit master_device(master_bus& bus, uint16_t address);
#endif

    /**
     * @brief Creates a new device on the specified bus.
     *
     * Does not take ownership of @p bus. It is the caller's responsibility to ensure that
     * this device does not outlive the bus.
     *
     * @param bus     The parent bus.
     * @param address Device address.
     * @param config  Device configuration.
     *
     * @return The new master_device, or an error.
     *
     * @code
     * auto device = idfxx::i2c::master_device::make(bus, 0x3C, {
     *     .scl_speed = 400_kHz,
     * });
     * @endcode
     */
    [[nodiscard]] static result<master_device> make(master_bus& bus, uint16_t address, const struct config& config);

    /**
     * @brief Creates a new device on the specified bus.
     *
     * Does not take ownership of @p bus. It is the caller's responsibility to ensure that
     * this device does not outlive the bus.
     *
     * @param bus     The parent bus.
     * @param address Device address.
     *
     * @return The new master_device, or an error.
     */
    [[nodiscard]] static result<master_device> make(master_bus& bus, uint16_t address);

    ~master_device();

    master_device(const master_device&) = delete;
    master_device& operator=(const master_device&) = delete;
    master_device(master_device&& other) noexcept;
    master_device& operator=(master_device&& other) noexcept;

    /**
     * @brief Returns the parent bus.
     * @pre The object has not been moved from.
     */
    [[nodiscard]] master_bus& bus() const {
        assert(_bus != nullptr);
        return *_bus;
    }

    /** @brief Returns the underlying ESP-IDF device handle. */
    [[nodiscard]] i2c_master_dev_handle_t handle() const { return _handle; }

    /** @brief Returns the device address. */
    [[nodiscard]] uint16_t address() const { return _address; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Probes the device.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the device does not acknowledge or on error.
     */
    void probe() const { probe(DEFAULT_TIMEOUT); }

    /**
     * @brief Probes the device.
     *
     * @param timeout Maximum time to wait for response.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the device does not acknowledge or on error.
     */
    template<typename Rep, typename Period>
    void probe(const std::chrono::duration<Rep, Period>& timeout) const {
        unwrap(try_probe(timeout));
    }
#endif

    /**
     * @brief Probes the device.
     *
     * @return Success if the device acknowledges, or an error.
     */
    [[nodiscard]] result<void> try_probe() const { return try_probe(DEFAULT_TIMEOUT); }

    /**
     * @brief Probes the device.
     *
     * @param timeout Maximum time to wait for response.
     *
     * @return Success if the device acknowledges, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_probe(const std::chrono::duration<Rep, Period>& timeout) const {
        if (_handle == nullptr) {
            return error(errc::invalid_state);
        }
        return _bus->try_probe(_address, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Transmits data to the device.
     *
     * @param data Data to transmit.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void transmit(std::span<const uint8_t> data) { unwrap(try_transmit(data)); }

    /**
     * @brief Transmits data from multiple buffers in a single I2C transaction.
     *
     * Convenience overload that accepts two or more buffers as separate arguments.
     *
     * @tparam Buffers Types convertible to std::span<const uint8_t>.
     * @param buffers Buffers to transmit, in order.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     *
     * @code
     * std::array<uint8_t, 1> reg{0x10};
     * std::array<uint8_t, 4> data{0x01, 0x02, 0x03, 0x04};
     * device.transmit(std::span{reg}, std::span{data});
     * @endcode
     */
    template<std::convertible_to<std::span<const uint8_t>>... Buffers>
        requires(sizeof...(Buffers) > 1)
    void transmit(Buffers... buffers) {
        unwrap(try_transmit(buffers...));
    }

    /**
     * @brief Transmits data from multiple buffers in a single I2C transaction.
     *
     * Performs a scatter-gather transmit, sending data from each buffer
     * sequentially without intermediate stop conditions.
     *
     * @param buffers Buffers to transmit, in order.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void transmit(std::span<const std::span<const uint8_t>> buffers) { unwrap(try_transmit(buffers)); }

    /**
     * @brief Transmits data to the device.
     *
     * @param data Data to transmit.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void transmit(std::span<const uint8_t> data, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_transmit(data, timeout));
    }

    /**
     * @brief Transmits data from multiple buffers in a single I2C transaction.
     *
     * Performs a scatter-gather transmit, sending data from each buffer
     * sequentially without intermediate stop conditions.
     *
     * @param buffers Buffers to transmit, in order.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void
    transmit(std::span<const std::span<const uint8_t>> buffers, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_transmit(buffers, timeout));
    }

    /**
     * @brief Transmits data to the device.
     *
     * @param buf  Data to transmit.
     * @param size Number of bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void transmit(const uint8_t* buf, size_t size) { unwrap(try_transmit(buf, size)); }

    /**
     * @brief Transmits data to the device.
     *
     * @param buf     Data to transmit.
     * @param size    Number of bytes.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void transmit(const uint8_t* buf, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_transmit(buf, size, timeout));
    }
#endif

    /**
     * @brief Transmits data to the device.
     *
     * @param data Data to transmit.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_transmit(std::span<const uint8_t> data) {
        return try_transmit(data, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Transmits data from multiple buffers in a single I2C transaction.
     *
     * Convenience overload that accepts two or more buffers as separate arguments.
     *
     * @tparam Buffers Types convertible to std::span<const uint8_t>.
     * @param buffers Buffers to transmit, in order.
     *
     * @return Success, or an error.
     */
    template<std::convertible_to<std::span<const uint8_t>>... Buffers>
        requires(sizeof...(Buffers) > 1)
    [[nodiscard]] result<void> try_transmit(Buffers... buffers) {
        std::array<std::span<const uint8_t>, sizeof...(Buffers)> arr{buffers...};
        return try_transmit(std::span<const std::span<const uint8_t>>{arr});
    }

    /**
     * @brief Transmits data from multiple buffers in a single I2C transaction.
     *
     * Performs a scatter-gather transmit, sending data from each buffer
     * sequentially without intermediate stop conditions.
     *
     * @param buffers Buffers to transmit, in order.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_transmit(std::span<const std::span<const uint8_t>> buffers) {
        return try_transmit(buffers, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Transmits data to the device.
     *
     * @param data Data to transmit.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_transmit(std::span<const uint8_t> data, const std::chrono::duration<Rep, Period>& timeout) {
        return try_transmit(data.data(), data.size(), timeout);
    }

    /**
     * @brief Transmits data from multiple buffers in a single I2C transaction.
     *
     * Performs a scatter-gather transmit, sending data from each buffer
     * sequentially without intermediate stop conditions.
     *
     * @param buffers Buffers to transmit, in order.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_transmit(std::span<const std::span<const uint8_t>> buffers, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_multi_buffer_transmit(buffers, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    /**
     * @brief Transmits data to the device.
     *
     * @param buf  Data to transmit.
     * @param size Number of bytes.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_transmit(const uint8_t* buf, size_t size) {
        return try_transmit(buf, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Transmits data to the device.
     *
     * @param buf     Data to transmit.
     * @param size    Number of bytes.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_transmit(const uint8_t* buf, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_transmit(buf, size, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Changes the I2C address used for subsequent operations.
     *
     * Useful for devices whose address can change at runtime, such as
     * after configuring an I2C multiplexer.
     *
     * @param new_address The new device address.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void change_address(uint16_t new_address) { unwrap(try_change_address(new_address)); }

    /**
     * @brief Changes the I2C address used for subsequent operations.
     *
     * Useful for devices whose address can change at runtime, such as
     * after configuring an I2C multiplexer.
     *
     * @param new_address The new device address.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void change_address(uint16_t new_address, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_change_address(new_address, timeout));
    }
#endif

    /**
     * @brief Changes the I2C address used for subsequent operations.
     *
     * Useful for devices whose address can change at runtime, such as
     * after configuring an I2C multiplexer.
     *
     * @param new_address The new device address.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_change_address(uint16_t new_address) {
        return try_change_address(new_address, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Changes the I2C address used for subsequent operations.
     *
     * Useful for devices whose address can change at runtime, such as
     * after configuring an I2C multiplexer.
     *
     * @param new_address The new device address.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_change_address(uint16_t new_address, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_change_address(new_address, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Registers a callback for transaction-done events.
     *
     * The callback runs in ISR context when an asynchronous transaction
     * completes. It should return true if a higher-priority task was woken.
     * Only useful when the bus was created with a non-zero trans_queue_depth.
     *
     * @param on_trans_done Transaction-done callback.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void register_event_callbacks(std::move_only_function<bool() const> on_trans_done) {
        unwrap(try_register_event_callbacks(std::move(on_trans_done)));
    }
#endif

    /**
     * @brief Registers a callback for transaction-done events.
     *
     * The callback runs in ISR context when an asynchronous transaction
     * completes. It should return true if a higher-priority task was woken.
     * Only useful when the bus was created with a non-zero trans_queue_depth.
     *
     * @param on_trans_done Transaction-done callback.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_register_event_callbacks(std::move_only_function<bool() const> on_trans_done);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Executes a custom sequence of I2C operations.
     *
     * Performs the given sequence of START, WRITE, READ, and STOP
     * operations. Useful for non-standard I2C protocols.
     *
     * @param ops Operations to execute.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     *
     * @code
     * uint8_t reg = 0x10;
     * uint8_t buf[4];
     * device.execute_operations(std::array{
     *     idfxx::i2c::operation{.command = idfxx::i2c::operation_command::start},
     *     idfxx::i2c::operation{.command = idfxx::i2c::operation_command::write,
     *                           .write_data = std::span{&reg, 1}},
     *     idfxx::i2c::operation{.command = idfxx::i2c::operation_command::start},
     *     idfxx::i2c::operation{.command = idfxx::i2c::operation_command::read,
     *                           .read_data = std::span{buf},
     *                           .ack_type = idfxx::i2c::ack_value::nack},
     *     idfxx::i2c::operation{.command = idfxx::i2c::operation_command::stop},
     * });
     * @endcode
     */
    void execute_operations(std::span<const operation> ops) { unwrap(try_execute_operations(ops)); }

    /**
     * @brief Executes a custom sequence of I2C operations.
     *
     * @param ops Operations to execute.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void execute_operations(std::span<const operation> ops, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_execute_operations(ops, timeout));
    }
#endif

    /**
     * @brief Executes a custom sequence of I2C operations.
     *
     * Performs the given sequence of START, WRITE, READ, and STOP
     * operations. Useful for non-standard I2C protocols.
     *
     * @param ops Operations to execute.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_execute_operations(std::span<const operation> ops) {
        return try_execute_operations(ops, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Executes a custom sequence of I2C operations.
     *
     * @param ops Operations to execute.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_execute_operations(std::span<const operation> ops, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_execute_operations(ops, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives data from the device.
     *
     * @param size Number of bytes to receive.
     *
     * @return Received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    [[nodiscard]] std::vector<uint8_t> receive(size_t size) { return unwrap(try_receive(size)); }

    /**
     * @brief Receives data from the device.
     *
     * @param size    Number of bytes to receive.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] std::vector<uint8_t> receive(size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_receive(size, timeout));
    }

    /**
     * @brief Receives data from the device.
     *
     * @param buf  Buffer for received data.
     * @param size Number of bytes to receive.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void receive(uint8_t* buf, size_t size) { unwrap(try_receive(buf, size)); }

    /**
     * @brief Receives data from the device.
     *
     * @param buf    Buffer for received data.
     * @param size   Number of bytes to receive.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void receive(uint8_t* buf, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_receive(buf, size, timeout));
    }

    /**
     * @brief Receives data from the device.
     *
     * @param buf  Buffer for received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void receive(std::span<uint8_t> buf) { unwrap(try_receive(buf)); }

    /**
     * @brief Receives data from the device.
     *
     * @param buf    Buffer for received data.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void receive(std::span<uint8_t> buf, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_receive(buf, timeout));
    }
#endif

    /**
     * @brief Receives data from the device.
     *
     * @param size Number of bytes to receive.
     *
     * @return Received data, or an error.
     */
    [[nodiscard]] result<std::vector<uint8_t>> try_receive(size_t size) { return try_receive(size, DEFAULT_TIMEOUT); }

    /**
     * @brief Receives data from the device.
     *
     * @param size    Number of bytes to receive.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Received data, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<std::vector<uint8_t>>
    try_receive(size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        std::vector<uint8_t> buf(size);
        return try_receive(buf, timeout).transform([&]() { return buf; });
    }

    /**
     * @brief Receives data from the device.
     *
     * @param buf  Buffer for received data.
     * @param size Number of bytes to receive.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_receive(uint8_t* buf, size_t size) {
        return try_receive(buf, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Receives data from the device.
     *
     * @param buf    Buffer for received data.
     * @param size   Number of bytes to receive.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_receive(uint8_t* buf, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_receive(buf, size, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    /**
     * @brief Receives data from the device.
     *
     * @param buf  Buffer for received data.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_receive(std::span<uint8_t> buf) { return try_receive(buf, DEFAULT_TIMEOUT); }

    /**
     * @brief Receives data from the device.
     *
     * @param buf    Buffer for received data.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_receive(std::span<uint8_t> buf, const std::chrono::duration<Rep, Period>& timeout) {
        return try_receive(buf.data(), buf.size(), timeout);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Writes data to a register.
     *
     * @param reg Register address (16-bit, MSB first).
     * @param buf Data to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_register(uint16_t reg, std::span<const uint8_t> buf) { unwrap(try_write_register(reg, buf)); }

    /**
     * @brief Writes data to a register.
     *
     * @param reg Register address (16-bit, MSB first).
     * @param buf Data to write.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void write_register(uint16_t reg, std::span<const uint8_t> buf, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_write_register(reg, buf, timeout));
    }

    /**
     * @brief Writes data to a register.
     *
     * @param reg  Register address (16-bit, MSB first).
     * @param buf  Data to write.
     * @param size Number of bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_register(uint16_t reg, const uint8_t* buf, size_t size) { unwrap(try_write_register(reg, buf, size)); }

    /**
     * @brief Writes data to a register.
     *
     * @param reg  Register address (16-bit, MSB first).
     * @param buf  Data to write.
     * @param size Number of bytes.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void
    write_register(uint16_t reg, const uint8_t* buf, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_write_register(reg, buf, size, timeout));
    }
#endif

    /**
     * @brief Writes data to a register.
     *
     * @param reg Register address (16-bit, MSB first).
     * @param buf Data to write.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_register(uint16_t reg, std::span<const uint8_t> buf) {
        return try_write_register(reg, buf.data(), buf.size(), DEFAULT_TIMEOUT);
    }

    /**
     * @brief Writes data to a register.
     *
     * @param reg Register address (16-bit, MSB first).
     * @param buf Data to write.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_write_register(uint16_t reg, std::span<const uint8_t> buf, const std::chrono::duration<Rep, Period>& timeout) {
        return try_write_register(reg, buf.data(), buf.size(), timeout);
    }

    /**
     * @brief Writes data to a register.
     *
     * @param reg  Register address (16-bit, MSB first).
     * @param buf  Data to write.
     * @param size Number of bytes.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_register(uint16_t reg, const uint8_t* buf, size_t size) {
        return try_write_register(reg, buf, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Writes data to a register.
     *
     * @param reg  Register address (16-bit, MSB first).
     * @param buf  Data to write.
     * @param size Number of bytes.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_write_register(
        uint16_t reg,
        const uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return _try_write_register(reg, buf, size, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Writes data to a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Data to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_register(uint8_t high, uint8_t low, std::span<const uint8_t> buf) {
        unwrap(try_write_register(high, low, buf));
    }

    /**
     * @brief Writes data to a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Data to write.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void write_register(
        uint8_t high,
        uint8_t low,
        std::span<const uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        unwrap(try_write_register(high, low, buf, timeout));
    }

    /**
     * @brief Writes data to a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Data to write.
     * @param size    Number of bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_register(uint8_t high, uint8_t low, const uint8_t* buf, size_t size) {
        unwrap(try_write_register(high, low, buf, size));
    }

    /**
     * @brief Writes data to a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Data to write.
     * @param size    Number of bytes.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void write_register(
        uint8_t high,
        uint8_t low,
        const uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        unwrap(try_write_register(high, low, buf, size, timeout));
    }
#endif

    /**
     * @brief Writes data to a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Data to write.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_register(uint8_t high, uint8_t low, std::span<const uint8_t> buf) {
        return try_write_register(high, low, buf, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Writes data to a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Data to write.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_write_register(
        uint8_t high,
        uint8_t low,
        std::span<const uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return try_write_register(high, low, buf.data(), buf.size(), timeout);
    }

    /**
     * @brief Writes data to a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Data to write.
     * @param size    Number of bytes.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_register(uint8_t high, uint8_t low, const uint8_t* buf, size_t size) {
        return try_write_register(high, low, buf, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Writes data to a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Data to write.
     * @param size    Number of bytes.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_write_register(
        uint8_t high,
        uint8_t low,
        const uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return _try_write_register(high, low, buf, size, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_registers(std::span<const uint16_t> registers, std::span<const uint8_t> buf) {
        unwrap(try_write_registers(registers, buf));
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param timeout   Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void write_registers(
        std::span<const uint16_t> registers,
        std::span<const uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        unwrap(try_write_registers(registers, buf, timeout));
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param size      Number of bytes per register.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_registers(std::span<const uint16_t> registers, const uint8_t* buf, size_t size) {
        unwrap(try_write_registers(registers, buf, size));
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param size      Number of bytes per register.
     * @param timeout   Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void write_registers(
        std::span<const uint16_t> registers,
        const uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        unwrap(try_write_registers(registers, buf, size, timeout));
    }

    // C++23: std::span cannot be constructed from a braced initializer list until C++26 (P2447).
    // Remove these overloads when C++26 becomes the minimum standard.

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_registers(std::initializer_list<uint16_t> registers, std::span<const uint8_t> buf) {
        write_registers(std::span<const uint16_t>{registers.begin(), registers.size()}, buf);
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param timeout   Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void write_registers(
        std::initializer_list<uint16_t> registers,
        std::span<const uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        write_registers(std::span<const uint16_t>{registers.begin(), registers.size()}, buf, timeout);
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param size      Number of bytes per register.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_registers(std::initializer_list<uint16_t> registers, const uint8_t* buf, size_t size) {
        write_registers(std::span<const uint16_t>{registers.begin(), registers.size()}, buf, size);
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param size      Number of bytes per register.
     * @param timeout   Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void write_registers(
        std::initializer_list<uint16_t> registers,
        const uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        write_registers(std::span<const uint16_t>{registers.begin(), registers.size()}, buf, size, timeout);
    }
#endif

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_registers(std::span<const uint16_t> registers, std::span<const uint8_t> buf) {
        return try_write_registers(registers, buf, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param timeout   Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_write_registers(
        std::span<const uint16_t> registers,
        std::span<const uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return try_write_registers(registers, buf.data(), buf.size(), timeout);
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param size      Number of bytes per register.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void>
    try_write_registers(std::span<const uint16_t> registers, const uint8_t* buf, size_t size) {
        return try_write_registers(registers, buf, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param size      Number of bytes per register.
     * @param timeout   Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_write_registers(
        std::span<const uint16_t> registers,
        const uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return _try_write_registers(registers, buf, size, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    // C++23: std::span cannot be constructed from a braced initializer list until C++26 (P2447).
    // Remove these overloads when C++26 becomes the minimum standard.

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void>
    try_write_registers(std::initializer_list<uint16_t> registers, std::span<const uint8_t> buf) {
        return try_write_registers(std::span<const uint16_t>{registers.begin(), registers.size()}, buf);
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param timeout   Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_write_registers(
        std::initializer_list<uint16_t> registers,
        std::span<const uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return try_write_registers(std::span<const uint16_t>{registers.begin(), registers.size()}, buf, timeout);
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param size      Number of bytes per register.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void>
    try_write_registers(std::initializer_list<uint16_t> registers, const uint8_t* buf, size_t size) {
        return try_write_registers(std::span<const uint16_t>{registers.begin(), registers.size()}, buf, size);
    }

    /**
     * @brief Writes data to multiple registers.
     *
     * @param registers Register addresses.
     * @param buf       Data to write.
     * @param size      Number of bytes per register.
     * @param timeout   Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_write_registers(
        std::initializer_list<uint16_t> registers,
        const uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return try_write_registers(std::span<const uint16_t>{registers.begin(), registers.size()}, buf, size, timeout);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads data from a register.
     *
     * @param reg Register address (16-bit, MSB first).
     * @param size Number of bytes to read.
     *
     * @return Received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    [[nodiscard]] std::vector<uint8_t> read_register(uint16_t reg, size_t size) {
        return unwrap(try_read_register(reg, size));
    }

    /**
     * @brief Reads data from a register.
     *
     * @param reg     Register address (16-bit, MSB first).
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] std::vector<uint8_t>
    read_register(uint16_t reg, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_read_register(reg, size, timeout));
    }

    /**
     * @brief Reads data from a register.
     *
     * @param reg  Register address (16-bit, MSB first).
     * @param buf  Buffer for received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void read_register(uint16_t reg, std::span<uint8_t> buf) { unwrap(try_read_register(reg, buf)); }

    /**
     * @brief Reads data from a register.
     *
     * @param reg     Register address (16-bit, MSB first).
     * @param buf     Buffer for received data.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void read_register(uint16_t reg, std::span<uint8_t> buf, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_read_register(reg, buf, timeout));
    }

    /**
     * @brief Reads data from a register.
     *
     * @param reg  Register address (16-bit, MSB first).
     * @param buf  Buffer for received data.
     * @param size Number of bytes to read.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void read_register(uint16_t reg, uint8_t* buf, size_t size) { unwrap(try_read_register(reg, buf, size)); }

    /**
     * @brief Reads data from a register.
     *
     * @param reg     Register address (16-bit, MSB first).
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void read_register(uint16_t reg, uint8_t* buf, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_read_register(reg, buf, size, timeout));
    }
#endif

    /**
     * @brief Reads data from a register.
     *
     * @param reg Register address (16-bit, MSB first).
     * @param size Number of bytes to read.
     *
     * @return Received data, or an error.
     */
    [[nodiscard]] result<std::vector<uint8_t>> try_read_register(uint16_t reg, size_t size) {
        return try_read_register(reg, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param reg     Register address (16-bit, MSB first).
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Received data, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<std::vector<uint8_t>>
    try_read_register(uint16_t reg, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        std::vector<uint8_t> buf(size);
        return try_read_register(reg, buf, timeout).transform([&]() { return buf; });
    }

    /**
     * @brief Reads data from a register.
     *
     * @param reg  Register address (16-bit, MSB first).
     * @param buf  Buffer for received data.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read_register(uint16_t reg, std::span<uint8_t> buf) {
        return try_read_register(reg, buf, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param reg Register address (16-bit, MSB first).
     * @param buf Buffer for received data.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_read_register(uint16_t reg, std::span<uint8_t> buf, const std::chrono::duration<Rep, Period>& timeout) {
        return try_read_register(reg, buf.data(), buf.size(), timeout);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param reg  Register address (16-bit, MSB first).
     * @param buf  Buffer for received data.
     * @param size Number of bytes to read.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read_register(uint16_t reg, uint8_t* buf, size_t size) {
        return try_read_register(reg, buf, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param reg     Register address (16-bit, MSB first).
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_read_register(uint16_t reg, uint8_t* buf, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_read_register(reg, buf, size, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param size    Number of bytes to read.
     *
     * @return Received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    [[nodiscard]] std::vector<uint8_t> read_register(uint8_t high, uint8_t low, size_t size) {
        return unwrap(try_read_register(high, low, size));
    }

    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] std::vector<uint8_t>
    read_register(uint8_t high, uint8_t low, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_read_register(high, low, size, timeout));
    }

    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void read_register(uint8_t high, uint8_t low, uint8_t* buf, size_t size) {
        unwrap(try_read_register(high, low, buf, size));
    }

    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void read_register(
        uint8_t high,
        uint8_t low,
        uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        unwrap(try_read_register(high, low, buf, size, timeout));
    }
#endif

    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param size    Number of bytes to read.
     *
     * @return Received data, or an error.
     */
    [[nodiscard]] result<std::vector<uint8_t>> try_read_register(uint8_t high, uint8_t low, size_t size) {
        return try_read_register(high, low, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Received data, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<std::vector<uint8_t>>
    try_read_register(uint8_t high, uint8_t low, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        std::vector<uint8_t> buf(size);
        return try_read_register(high, low, buf, timeout).transform([&]() { return buf; });
    }

    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Buffer for received data.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read_register(uint8_t high, uint8_t low, std::span<uint8_t> buf) {
        return try_read_register(high, low, buf, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Buffer for received data.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_read_register(
        uint8_t high,
        uint8_t low,
        std::span<uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return try_read_register(high, low, buf.data(), buf.size(), timeout);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read_register(uint8_t high, uint8_t low, uint8_t* buf, size_t size) {
        return try_read_register(high, low, buf, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param high    High byte of register address.
     * @param low     Low byte of register address.
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_read_register(
        uint8_t high,
        uint8_t low,
        uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return _try_read_register(high, low, buf, size, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

private:
    explicit master_device(master_bus* bus, i2c_master_dev_handle_t handle, uint16_t address);

    void _delete() noexcept;

    [[nodiscard]] result<void> _try_transmit(const uint8_t* buf, size_t size, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void>
    _try_multi_buffer_transmit(std::span<const std::span<const uint8_t>> buffers, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void> _try_change_address(uint16_t new_address, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void>
    _try_execute_operations(std::span<const operation> ops, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void> _try_receive(uint8_t* buf, size_t size, std::chrono::milliseconds timeout);

    [[nodiscard]] result<void>
    _try_write_register(uint16_t reg, const uint8_t* buf, size_t size, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void>
    _try_write_register(uint8_t high, uint8_t low, const uint8_t* buf, size_t size, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void> _try_write_registers(
        std::span<const uint16_t> registers,
        const uint8_t* buf,
        size_t size,
        std::chrono::milliseconds timeout
    );
    [[nodiscard]] result<void>
    _try_read_register(uint16_t reg, uint8_t* buf, size_t size, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void>
    _try_read_register(uint8_t high, uint8_t low, uint8_t* buf, size_t size, std::chrono::milliseconds timeout);

    master_bus* _bus = nullptr;
    i2c_master_dev_handle_t _handle = nullptr;
    uint16_t _address = 0;
    std::unique_ptr<std::move_only_function<bool() const>> _on_trans_done;
};

/** @} */ // end of idfxx_i2c

} // namespace idfxx::i2c

namespace idfxx {

/**
 * @headerfile <idfxx/i2c/master>
 * @brief Returns a string representation of an I2C port.
 *
 * @param p The port identifier to convert.
 * @return "I2C0", "I2C1" (when available), "LP_I2C0" (when available), or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(i2c::port p) {
    switch (p) {
    case i2c::port::i2c0:
        return "I2C0";
#if SOC_HP_I2C_NUM >= 2
    case i2c::port::i2c1:
        return "I2C1";
#endif
#if SOC_LP_I2C_NUM >= 1
    case i2c::port::lp_i2c0:
        return "LP_I2C0";
#endif
    default:
        return "unknown(" + std::to_string(static_cast<int>(p)) + ")";
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
struct formatter<idfxx::i2c::port> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::i2c::port p, FormatContext& ctx) const {
        auto s = idfxx::to_string(p);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
