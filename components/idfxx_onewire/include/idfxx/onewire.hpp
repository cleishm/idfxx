// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/onewire>
 * @file onewire.hpp
 * @brief 1-Wire bus protocol driver.
 *
 * @defgroup idfxx_onewire 1-Wire Component
 * @brief Type-safe 1-Wire bus protocol driver for ESP32.
 *
 * Provides 1-Wire bus lifecycle management with thread-safe device access,
 * device search, ROM commands, and CRC utilities.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin configuration.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/byte_view>
#include <idfxx/error>
#include <idfxx/gpio>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

/**
 * @headerfile <idfxx/onewire>
 * @brief 1-Wire bus protocol classes and utilities.
 */
namespace idfxx::onewire {

/**
 * @headerfile <idfxx/onewire>
 * @brief 1-Wire device address.
 *
 * Typed wrapper around a 64-bit 1-Wire ROM address. Each device on the bus
 * has a unique address containing a family code (low byte), serial number,
 * and CRC (high byte).
 *
 * For single-device buses, address::any() can be used to skip ROM matching.
 */
class address {
public:
    /** @brief Constructs an address with value 0 (equivalent to any()). */
    constexpr address() = default;

    /**
     * @brief Constructs an address from a raw 64-bit value.
     * @param raw The 64-bit ROM address.
     */
    constexpr explicit address(uint64_t raw)
        : _raw(raw) {}

    /**
     * @brief Returns the wildcard address for single-device buses.
     *
     * When only one device is connected, this address can be used to skip
     * ROM matching, simplifying communication.
     *
     * @return The wildcard address (value 0).
     */
    [[nodiscard]] static constexpr address any() { return address{}; }

    /**
     * @brief Returns an invalid sentinel address indicating no device.
     *
     * This address will never occur in a real device (CRC mismatch) and
     * can be used as a "no-such-device" indicator.
     *
     * @return The sentinel address (all bits set).
     */
    [[nodiscard]] static constexpr address none() { return address{0xFFFFFFFFFFFFFFFF}; }

    /**
     * @brief Returns the underlying 64-bit ROM address.
     * @return The raw address value.
     */
    [[nodiscard]] constexpr uint64_t raw() const { return _raw; }

    /**
     * @brief Extracts the family code from the address.
     *
     * The family code is stored in the low byte of the ROM address and
     * identifies the device type (e.g., 0x28 for DS18B20).
     *
     * @return The 8-bit family code.
     */
    [[nodiscard]] constexpr uint8_t family() const { return static_cast<uint8_t>(_raw & 0xFF); }

    /** @brief Compares two addresses for equality. */
    [[nodiscard]] constexpr bool operator==(const address&) const noexcept = default;

    /** @brief Default three-way comparison for ordering. */
    [[nodiscard]] constexpr auto operator<=>(const address&) const noexcept = default;

private:
    uint64_t _raw = 0;
};

class bus;

// -- CRC utilities -----------------------------------------------------------

/**
 * @headerfile <idfxx/onewire>
 * @brief Computes a Dallas Semiconductor 8-bit CRC.
 *
 * Used to verify the integrity of ROM addresses and scratchpad data.
 *
 * @param data The data bytes to checksum.
 * @return The computed 8-bit CRC.
 */
[[nodiscard]] uint8_t crc8(byte_view data);

/**
 * @headerfile <idfxx/onewire>
 * @brief Computes a Dallas Semiconductor 16-bit CRC.
 *
 * Used to verify data integrity for certain 1-Wire device operations.
 *
 * @param data The data bytes to checksum.
 * @param crc_iv The CRC starting value (default: 0).
 * @return The computed 16-bit CRC.
 *
 * @note The CRC computed here is not what you'll get directly from the
 *       1-Wire network, as it is transmitted bitwise inverted.
 */
[[nodiscard]] uint16_t crc16(byte_view data, uint16_t crc_iv = 0);

/**
 * @headerfile <idfxx/onewire>
 * @brief Verifies a 16-bit CRC against received data.
 *
 * @param data The data bytes to checksum.
 * @param inverted_crc The two CRC16 bytes as received from the device. Any other size is
 *        rejected.
 * @param crc_iv The CRC starting value (default: 0).
 * @return true if the CRC matches, false if it doesn't or @p inverted_crc is not exactly 2 bytes.
 */
[[nodiscard]] bool check_crc16(byte_view data, byte_view inverted_crc, uint16_t crc_iv = 0);

// -- bus ---------------------------------------------------------------------

/**
 * @headerfile <idfxx/onewire>
 * @brief 1-Wire bus controller with thread-safe access.
 *
 * Provides low-level 1-Wire bus operations: reset, ROM commands, data
 * transfer, parasitic power control, and device search. Satisfies the
 * Lockable named requirement for use with std::lock_guard and std::unique_lock.
 *
 * This type is non-copyable and move-only. A moved-from
 * object must not be used: any operation other than destruction or
 * assignment is undefined behavior.
 *
 * @code
 * auto bus = idfxx::onewire::bus(idfxx::gpio_4);
 * if (bus.reset()) {
 *     bus.skip_rom();
 *     bus.write(0x44);  // Convert T command
 * }
 * @endcode
 */
class bus {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new 1-Wire bus controller.
     *
     * @param pin GPIO pin connected to the 1-Wire bus.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the pin is not connected.
     */
    [[nodiscard]] explicit bus(gpio pin);
#endif

    /**
     * @brief Creates a new 1-Wire bus controller.
     *
     * @param pin GPIO pin connected to the 1-Wire bus.
     * @return The new bus, or an error if the pin is not connected.
     * @retval invalid_state If the pin is not connected.
     */
    [[nodiscard]] static result<bus> make(gpio pin);

    ~bus() = default;

    bus(const bus&) = delete;
    bus& operator=(const bus&) = delete;
    bus(bus&& other) noexcept = default;
    bus& operator=(bus&& other) noexcept = default;

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

    /** @brief Returns the GPIO pin. */
    [[nodiscard]] gpio pin() const noexcept { return _pin; }

    // -- Bus reset -----------------------------------------------------------

    /**
     * @brief Performs a 1-Wire reset cycle.
     *
     * Must be called before issuing ROM commands (select/skip_rom).
     *
     * @return true if at least one device responds with a presence pulse,
     *         false if no devices were detected (or the bus is shorted).
     */
    [[nodiscard]] bool reset();

    // -- ROM commands --------------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Selects a specific device by ROM address.
     *
     * Issues a "ROM select" command on the bus. A reset() must be performed
     * before calling this method.
     *
     * @param addr The ROM address of the device to select.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     */
    void select(address addr) { unwrap(try_select(addr)); }

    /**
     * @brief Selects all devices on the bus.
     *
     * Issues a "skip ROM" command. A reset() must be performed before
     * calling this method.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     */
    void skip_rom() { unwrap(try_skip_rom()); }
#endif

    /**
     * @brief Selects a specific device by ROM address.
     *
     * Issues a "ROM select" command on the bus. A reset() must be performed
     * before calling this method.
     *
     * @param addr The ROM address of the device to select.
     * @return Success, or an error on bus communication failure.
     */
    [[nodiscard]] result<void> try_select(address addr);

    /**
     * @brief Selects all devices on the bus.
     *
     * Issues a "skip ROM" command. A reset() must be performed before
     * calling this method.
     *
     * @return Success, or an error on bus communication failure.
     */
    [[nodiscard]] result<void> try_skip_rom();

    // -- Write operations ----------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Writes a single byte, such as a command, to the bus.
     *
     * @param value The byte to write.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     *
     * @code
     * bus.write(0x44);  // Convert T
     * @endcode
     */
    void write(uint8_t value) { unwrap(try_write(value)); }

    /**
     * @brief Writes bytes to the bus.
     *
     * @param data The bytes to write: any contiguous range of `uint8_t` or `std::byte`, or
     *             `{ptr, len}`.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     *
     * @code
     * std::array<uint8_t, 4> cmd{0x4E, th, tl, cfg};  // Write Scratchpad
     * bus.write(cmd);
     * @endcode
     */
    void write(byte_view data) { unwrap(try_write(data)); }

    /**
     * @brief Writes a short sequence of literal bytes, such as a command, to the bus.
     *
     * @param data The bytes to write.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     *
     * @code
     * bus.write({0xCC, 0x44});              // Skip ROM, Convert T
     * bus.write({0x4E, 0x4B, 0x46, 0x7F});  // Write Scratchpad: TH, TL, config
     * @endcode
     */
    void write(std::initializer_list<uint8_t> data) { unwrap(try_write(data)); }
#endif

    /**
     * @brief Writes a single byte, such as a command, to the bus.
     *
     * @param value The byte to write.
     * @return Success, or an error on bus communication failure.
     */
    [[nodiscard]] result<void> try_write(uint8_t value);

    /**
     * @brief Writes bytes to the bus.
     *
     * @param data The bytes to write: any contiguous range of `uint8_t` or `std::byte`, or
     *             `{ptr, len}`.
     * @return Success, or an error on bus communication failure.
     */
    [[nodiscard]] result<void> try_write(byte_view data);

    /**
     * @brief Writes a short sequence of literal bytes, such as a command, to the bus.
     *
     * @param data The bytes to write.
     * @return Success, or an error on bus communication failure.
     */
    [[nodiscard]] result<void> try_write(std::initializer_list<uint8_t> data) {
        return try_write(byte_view{data.begin(), data.size()});
    }

    // -- Read operations -----------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads a single byte from the bus.
     *
     * @return The byte read.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     */
    [[nodiscard]] uint8_t read() { return unwrap(try_read()); }

    /**
     * @brief Reads a fixed number of bytes from the bus.
     *
     * @tparam N The number of bytes to read.
     * @return The bytes read.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     *
     * @code
     * if (bus.reset()) {
     *     bus.select(addr);
     *     bus.write(0xBE);  // Read Scratchpad
     *     auto scratchpad = bus.read<9>();
     *     if (idfxx::onewire::crc8({scratchpad.data(), 8}) != scratchpad[8]) {
     *         // corrupted transfer
     *     }
     * }
     * @endcode
     */
    template<size_t N>
        requires(N > 0)
    [[nodiscard]] std::array<uint8_t, N> read() {
        return unwrap(try_read<N>());
    }

    /**
     * @brief Reads bytes from the bus, filling the whole buffer.
     *
     * @param buf The buffer to fill: any writable contiguous range of `uint8_t` or `std::byte`,
     *            or `{ptr, len}`. Its whole length is read.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     */
    void read(byte_span buf) { unwrap(try_read(buf)); }
#endif

    /**
     * @brief Reads a single byte from the bus.
     *
     * @return The byte read, or an error on bus communication failure.
     */
    [[nodiscard]] result<uint8_t> try_read();

    /**
     * @brief Reads a fixed number of bytes from the bus.
     *
     * @tparam N The number of bytes to read.
     * @return The bytes read, or an error on bus communication failure.
     */
    template<size_t N>
        requires(N > 0)
    [[nodiscard]] result<std::array<uint8_t, N>> try_read() {
        std::array<uint8_t, N> buf{};
        return try_read(byte_span{buf}).transform([&buf]() { return buf; });
    }

    /**
     * @brief Reads bytes from the bus, filling the whole buffer.
     *
     * @param buf The buffer to fill: any writable contiguous range of `uint8_t` or `std::byte`,
     *            or `{ptr, len}`. Its whole length is read.
     * @return Success, or an error on bus communication failure.
     */
    [[nodiscard]] result<void> try_read(byte_span buf);

    // -- Parasitic power -----------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Actively drives the bus high for parasitic power.
     *
     * Some devices need more power than the pull-up resistor can provide
     * during certain operations (e.g., temperature conversion).
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (e.g., bus is being held low).
     */
    void power() { unwrap(try_power()); }
#endif

    /**
     * @brief Actively drives the bus high for parasitic power.
     *
     * Some devices need more power than the pull-up resistor can provide
     * during certain operations (e.g., temperature conversion).
     *
     * @return Success, or an error on failure.
     */
    [[nodiscard]] result<void> try_power();

    /**
     * @brief Stops driving power onto the bus.
     *
     * Only needed after a previous call to power(). Note that reset()
     * will also automatically depower the bus.
     */
    void depower() noexcept;

    // -- Device search -------------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Searches for all devices on the bus.
     *
     * @param max_devices Maximum number of devices to discover.
     * @return A vector of discovered device addresses.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     */
    [[nodiscard]] std::vector<address> search(size_t max_devices = 8) {
        return unwrap(try_search(max_devices));
    }

    /**
     * @brief Searches for devices with a specific family code.
     *
     * @param family_code The family code to filter by (e.g., 0x28 for DS18B20).
     * @param max_devices Maximum number of devices to discover.
     * @return A vector of discovered device addresses matching the family code.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on bus communication failure.
     */
    [[nodiscard]] std::vector<address> search(uint8_t family_code, size_t max_devices = 8) {
        return unwrap(try_search(family_code, max_devices));
    }
#endif

    /**
     * @brief Searches for all devices on the bus.
     *
     * @param max_devices Maximum number of devices to discover.
     * @return A vector of discovered device addresses, or an error on bus communication failure.
     */
    [[nodiscard]] result<std::vector<address>> try_search(size_t max_devices = 8);

    /**
     * @brief Searches for devices with a specific family code.
     *
     * @param family_code The family code to filter by (e.g., 0x28 for DS18B20).
     * @param max_devices Maximum number of devices to discover.
     * @return A vector of discovered device addresses matching the family code,
     *         or an error on bus communication failure.
     */
    [[nodiscard]] result<std::vector<address>>
    try_search(uint8_t family_code, size_t max_devices = 8);

private:
    /** @cond INTERNAL */
    struct validated {};
    bus(gpio pin, validated)
        : _pin(pin)
        , _mux(std::make_unique<std::recursive_mutex>()) {}
    /** @endcond */

    gpio _pin;
    mutable std::unique_ptr<std::recursive_mutex> _mux;
};

} // namespace idfxx::onewire

/** @} */ // end of idfxx_onewire

namespace idfxx {

/**
 * @headerfile <idfxx/onewire>
 * @brief Returns a string representation of a 1-Wire address.
 *
 * @param addr The address to convert.
 * @return "ONEWIRE_ANY" for the wildcard address, "ONEWIRE_NONE" for the
 *         sentinel address, or colon-separated hex bytes
 *         (e.g., "28:FF:12:34:56:78:9A:BC").
 */
[[nodiscard]] inline std::string to_string(onewire::address addr) {
    if (addr == onewire::address::any()) {
        return "ONEWIRE_ANY";
    }
    if (addr == onewire::address::none()) {
        return "ONEWIRE_NONE";
    }
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(23);
    auto raw = addr.raw();
    for (int i = 0; i < 8; ++i) {
        if (i > 0) {
            result += ':';
        }
        auto byte = static_cast<uint8_t>((raw >> (i * 8)) & 0xFF);
        result += hex[byte >> 4];
        result += hex[byte & 0xF];
    }
    return result;
}

} // namespace idfxx

#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::onewire::address> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::onewire::address addr, FormatContext& ctx) const {
        auto s = idfxx::to_string(addr);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
