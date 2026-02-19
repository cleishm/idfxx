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

#include <chrono>
#include <frequency/frequency>
#include <initializer_list>
#include <memory>
#include <mutex>
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

/**
 * @headerfile <idfxx/i2c/master>
 * @brief I2C master bus controller with thread-safe device access.
 *
 * Represents an I2C bus operating in master mode. Satisfies the Lockable
 * named requirement for use with std::lock_guard and std::unique_lock.
 */
class master_bus {
public:
    /**
     * @brief Creates a new I2C master bus.
     *
     * @param port      I2C port number.
     * @param sda       GPIO pin for the SDA line.
     * @param scl       GPIO pin for the SCL line.
     * @param frequency Clock frequency in Hz.
     *
     * @return The new master_bus, or an error.
     */
    [[nodiscard]] static result<std::unique_ptr<master_bus>>
    make(enum port port, gpio sda, gpio scl, freq::hertz frequency);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new I2C master bus.
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

    ~master_bus();

    master_bus(const master_bus&) = delete;
    master_bus& operator=(const master_bus&) = delete;
    master_bus(master_bus&&) = delete;
    master_bus& operator=(master_bus&&) = delete;

    /** @brief Acquires exclusive access to the bus. */
    void lock() const { _mux.lock(); }

    /** @brief Tries to acquire exclusive access without blocking. */
    [[nodiscard]] bool try_lock() const noexcept { return _mux.try_lock(); }

    /** @brief Releases exclusive access to the bus. */
    void unlock() const { _mux.unlock(); }

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
     * @param address 7-bit device address.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the device does not acknowledge or on error.
     */
    void probe(uint8_t address) const { probe(address, DEFAULT_TIMEOUT); }

    /**
     * @brief Probes for a device at the specified address.
     *
     * @param address 7-bit device address.
     * @param timeout Maximum time to wait for response.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the device does not acknowledge or on error.
     */
    template<typename Rep, typename Period>
    void probe(uint8_t address, const std::chrono::duration<Rep, Period>& timeout) const {
        unwrap(_probe(address, std::chrono::ceil<std::chrono::milliseconds>(timeout)));
    }
#endif

    /**
     * @brief Probes for a device at the specified address.
     *
     * @param address 7-bit device address.
     *
     * @return Success if the device acknowledges, or an error.
     */
    [[nodiscard]] result<void> try_probe(uint8_t address) const { return try_probe(address, DEFAULT_TIMEOUT); }

    /**
     * @brief Probes for a device at the specified address.
     *
     * @param address 7-bit device address.
     * @param timeout Maximum time to wait for response.
     *
     * @return Success if the device acknowledges, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_probe(uint8_t address, const std::chrono::duration<Rep, Period>& timeout) const {
        return _probe(address, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

private:
    explicit master_bus(i2c_master_bus_handle_t handle, enum port port, freq::hertz frequency);

    [[nodiscard]] std::vector<uint8_t> _scan_devices(std::chrono::milliseconds timeout) const;
    [[nodiscard]] result<void> _probe(uint8_t address, std::chrono::milliseconds timeout) const;

    mutable std::recursive_mutex _mux;
    i2c_master_bus_handle_t _handle;
    enum port _port;
    freq::hertz _frequency;
};

/**
 * @headerfile <idfxx/i2c/master>
 * @brief I2C device at a specific 7-bit address with register operations.
 *
 * Represents a specific device at a 7-bit address. Provides methods for
 * raw data transfer and register-based read/write operations.
 */
class master_device {
public:
    /**
     * @brief Creates a new device on the specified bus.
     *
     * @param bus     The parent bus.
     * @param address 7-bit device address.
     *
     * @return The new master_device, or an error.
     */
    [[nodiscard]] static result<std::unique_ptr<master_device>> make(std::shared_ptr<master_bus> bus, uint8_t address);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new device on the specified bus.
     *
     * @param bus     The parent bus.
     * @param address 7-bit device address.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit master_device(std::shared_ptr<master_bus> bus, uint8_t address);
#endif

    ~master_device();

    master_device(const master_device&) = delete;
    master_device& operator=(const master_device&) = delete;
    master_device(master_device&&) = delete;
    master_device& operator=(master_device&&) = delete;

    /** @brief Returns the parent bus. */
    [[nodiscard]] const std::shared_ptr<master_bus>& bus() const { return _bus; }

    /** @brief Returns the underlying ESP-IDF device handle. */
    [[nodiscard]] i2c_master_dev_handle_t handle() const { return _handle; }

    /** @brief Returns the 7-bit device address. */
    [[nodiscard]] uint8_t address() const { return _address; }

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
        _bus->probe(_address, std::chrono::ceil<std::chrono::milliseconds>(timeout));
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
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Data to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_register(uint8_t regHigh, uint8_t regLow, std::span<const uint8_t> buf) {
        unwrap(try_write_register(regHigh, regLow, buf));
    }

    /**
     * @brief Writes data to a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Data to write.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void write_register(
        uint8_t regHigh,
        uint8_t regLow,
        std::span<const uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        unwrap(try_write_register(regHigh, regLow, buf, timeout));
    }

    /**
     * @brief Writes data to a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Data to write.
     * @param size    Number of bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write_register(uint8_t regHigh, uint8_t regLow, const uint8_t* buf, size_t size) {
        unwrap(try_write_register(regHigh, regLow, buf, size));
    }

    /**
     * @brief Writes data to a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Data to write.
     * @param size    Number of bytes.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void write_register(
        uint8_t regHigh,
        uint8_t regLow,
        const uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        unwrap(try_write_register(regHigh, regLow, buf, size, timeout));
    }
#endif

    /**
     * @brief Writes data to a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Data to write.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_register(uint8_t regHigh, uint8_t regLow, std::span<const uint8_t> buf) {
        return try_write_register(regHigh, regLow, buf, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Writes data to a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Data to write.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_write_register(
        uint8_t regHigh,
        uint8_t regLow,
        std::span<const uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return try_write_register(regHigh, regLow, buf.data(), buf.size(), timeout);
    }

    /**
     * @brief Writes data to a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Data to write.
     * @param size    Number of bytes.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_register(uint8_t regHigh, uint8_t regLow, const uint8_t* buf, size_t size) {
        return try_write_register(regHigh, regLow, buf, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Writes data to a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Data to write.
     * @param size    Number of bytes.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_write_register(
        uint8_t regHigh,
        uint8_t regLow,
        const uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return _try_write_register(regHigh, regLow, buf, size, std::chrono::ceil<std::chrono::milliseconds>(timeout));
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
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param size    Number of bytes to read.
     *
     * @return Received data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    [[nodiscard]] std::vector<uint8_t> read_register(uint8_t regHigh, uint8_t regLow, size_t size) {
        return unwrap(try_read_register(regHigh, regLow, size));
    }

    /**
     * @brief Reads data from a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
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
    read_register(uint8_t regHigh, uint8_t regLow, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_read_register(regHigh, regLow, size, timeout));
    }

    /**
     * @brief Reads data from a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void read_register(uint8_t regHigh, uint8_t regLow, uint8_t* buf, size_t size) {
        unwrap(try_read_register(regHigh, regLow, buf, size));
    }

    /**
     * @brief Reads data from a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename Rep, typename Period>
    void read_register(
        uint8_t regHigh,
        uint8_t regLow,
        uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        unwrap(try_read_register(regHigh, regLow, buf, size, timeout));
    }
#endif

    /**
     * @brief Reads data from a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param size    Number of bytes to read.
     *
     * @return Received data, or an error.
     */
    [[nodiscard]] result<std::vector<uint8_t>> try_read_register(uint8_t regHigh, uint8_t regLow, size_t size) {
        return try_read_register(regHigh, regLow, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Received data, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<std::vector<uint8_t>>
    try_read_register(uint8_t regHigh, uint8_t regLow, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        std::vector<uint8_t> buf(size);
        return try_read_register(regHigh, regLow, buf, timeout).transform([&]() { return buf; });
    }

    /**
     * @brief Reads data from a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Buffer for received data.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read_register(uint8_t regHigh, uint8_t regLow, std::span<uint8_t> buf) {
        return try_read_register(regHigh, regLow, buf, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Buffer for received data.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_read_register(
        uint8_t regHigh,
        uint8_t regLow,
        std::span<uint8_t> buf,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return try_read_register(regHigh, regLow, buf.data(), buf.size(), timeout);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read_register(uint8_t regHigh, uint8_t regLow, uint8_t* buf, size_t size) {
        return try_read_register(regHigh, regLow, buf, size, DEFAULT_TIMEOUT);
    }

    /**
     * @brief Reads data from a register.
     *
     * @param regHigh High byte of register address.
     * @param regLow  Low byte of register address.
     * @param buf     Buffer for received data.
     * @param size    Number of bytes to read.
     * @param timeout Maximum time to wait for completion.
     *
     * @return Success, or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_read_register(
        uint8_t regHigh,
        uint8_t regLow,
        uint8_t* buf,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return _try_read_register(regHigh, regLow, buf, size, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

private:
    explicit master_device(std::shared_ptr<master_bus> bus, i2c_master_dev_handle_t handle, uint8_t address);

    [[nodiscard]] result<void> _try_transmit(const uint8_t* buf, size_t size, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void> _try_receive(uint8_t* buf, size_t size, std::chrono::milliseconds timeout);

    [[nodiscard]] result<void>
    _try_write_register(uint16_t reg, const uint8_t* buf, size_t size, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void> _try_write_register(
        uint8_t regHigh,
        uint8_t regLow,
        const uint8_t* buf,
        size_t size,
        std::chrono::milliseconds timeout
    );
    [[nodiscard]] result<void> _try_write_registers(
        std::span<const uint16_t> registers,
        const uint8_t* buf,
        size_t size,
        std::chrono::milliseconds timeout
    );
    [[nodiscard]] result<void>
    _try_read_register(uint16_t reg, uint8_t* buf, size_t size, std::chrono::milliseconds timeout);
    [[nodiscard]] result<void>
    _try_read_register(uint8_t regHigh, uint8_t regLow, uint8_t* buf, size_t size, std::chrono::milliseconds timeout);

    std::shared_ptr<master_bus> _bus;
    i2c_master_dev_handle_t _handle;
    uint8_t _address;
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
