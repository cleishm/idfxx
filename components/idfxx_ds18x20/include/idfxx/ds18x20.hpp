// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/ds18x20>
 * @file ds18x20.hpp
 * @brief DS18x20 1-Wire temperature sensor driver.
 *
 * @defgroup idfxx_ds18x20 DS18x20 Component
 * @brief Type-safe DS18x20 1-Wire temperature sensor driver for ESP32.
 *
 * Provides temperature measurement and scratchpad access for DS18S20, DS1822,
 * DS18B20, and MAX31850 1-Wire temperature sensors.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin configuration.
 * @{
 */

#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/onewire>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <thermo/thermo>
#include <vector>

/**
 * @headerfile <idfxx/ds18x20>
 * @brief DS18x20 1-Wire temperature sensor classes and utilities.
 */
namespace idfxx::ds18x20 {

/**
 * @headerfile <idfxx/ds18x20>
 * @brief DS18x20 device family identifiers.
 *
 * Each 1-Wire device family has a unique 8-bit code stored in the low byte
 * of the device's ROM address.
 */
enum class family : uint8_t {
    ds18s20 = 0x10,  ///< DS18S20 (9-bit, +/-0.5C)
    ds1822 = 0x22,   ///< DS1822 (12-bit, +/-2C)
    ds18b20 = 0x28,  ///< DS18B20 (12-bit, +/-0.5C)
    max31850 = 0x3B, ///< MAX31850 (14-bit, +/-0.25C)
};

/**
 * @headerfile <idfxx/ds18x20>
 * @brief DS18B20 ADC resolution configuration.
 *
 * Values correspond to the configuration register byte in the scratchpad.
 * Higher resolution provides better precision but increases conversion time.
 */
enum class resolution : uint8_t {
    bits_9 = 0x1F,  ///< 9-bit resolution (~93.75ms conversion)
    bits_10 = 0x3F, ///< 10-bit resolution (~187.5ms conversion)
    bits_11 = 0x5F, ///< 11-bit resolution (~375ms conversion)
    bits_12 = 0x7F, ///< 12-bit resolution (~750ms conversion, default)
};

class device;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @headerfile <idfxx/ds18x20>
 * @brief Scans for DS18x20 devices on a 1-Wire bus.
 *
 * Discovers all DS18x20-family devices connected to the specified GPIO pin.
 *
 * @param pin GPIO pin connected to the 1-Wire bus.
 * @param max_devices Maximum number of devices to discover.
 * @return A vector of discovered devices.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
[[nodiscard]] inline std::vector<device> scan_devices(idfxx::gpio pin, size_t max_devices = 8);

/**
 * @headerfile <idfxx/ds18x20>
 * @brief Measures and reads temperatures from multiple devices.
 *
 * Sends a measure command for each bus and reads the temperature from each device.
 *
 * @param devices Devices to read from.
 * @return A vector of temperatures in the same order as the input devices.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
[[nodiscard]] inline std::vector<thermo::millicelsius> measure_and_read_multi(std::span<const device> devices);
#endif

/**
 * @headerfile <idfxx/ds18x20>
 * @brief Scans for DS18x20 devices on a 1-Wire bus.
 *
 * Discovers all DS18x20-family devices connected to the specified GPIO pin.
 *
 * @param pin GPIO pin connected to the 1-Wire bus.
 * @param max_devices Maximum number of devices to discover.
 * @return A vector of discovered devices, or an error on bus communication failure.
 * @retval invalid_state If the pin is not connected.
 */
[[nodiscard]] result<std::vector<device>> try_scan_devices(idfxx::gpio pin, size_t max_devices = 8);

/**
 * @headerfile <idfxx/ds18x20>
 * @brief Measures and reads temperatures from multiple devices.
 *
 * Sends a measure command for each bus and reads the temperature from each device.
 *
 * @param devices Devices to read from.
 * @return A vector of temperatures in the same order as the input devices,
 *         or an error on bus communication failure. Returns an empty vector
 *         if the input is empty.
 */
[[nodiscard]] result<std::vector<thermo::millicelsius>> try_measure_and_read_multi(std::span<const device> devices);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
inline std::vector<device> scan_devices(idfxx::gpio pin, size_t max_devices) {
    return unwrap(try_scan_devices(pin, max_devices));
}

inline std::vector<thermo::millicelsius> measure_and_read_multi(std::span<const device> devices) {
    return unwrap(try_measure_and_read_multi(devices));
}
#endif

/**
 * @headerfile <idfxx/ds18x20>
 * @brief DS18x20 1-Wire temperature sensor device.
 *
 * Lightweight, copyable value type representing a specific sensor on a 1-Wire bus.
 * Each device is identified by its GPIO pin and 64-bit ROM address.
 *
 * For single-sensor buses, use onewire::address::any() (the default) to skip ROM matching:
 * @code
 * auto dev = idfxx::ds18x20::device(idfxx::gpio_4);
 * thermo::millicelsius temp = dev.measure_and_read();
 * @endcode
 *
 * For multi-sensor buses, first scan for devices:
 * @code
 * auto devices = idfxx::ds18x20::scan_devices(idfxx::gpio_4);
 * for (auto& dev : devices) {
 *     thermo::millicelsius temp = dev.measure_and_read();
 * }
 * @endcode
 */
class device {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Constructs a validated device.
     *
     * @param pin GPIO pin connected to the 1-Wire bus.
     * @param addr Device address (default: onewire::address::any() for single-device buses).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the pin is not connected.
     */
    [[nodiscard]] explicit device(idfxx::gpio pin, onewire::address addr = onewire::address::any());
#endif

    /**
     * @brief Creates a validated device.
     *
     * @param pin GPIO pin connected to the 1-Wire bus.
     * @param addr Device address (default: onewire::address::any() for single-device buses).
     * @return The device, or an error if the pin is not connected.
     * @retval invalid_state If the pin is not connected.
     */
    [[nodiscard]] static result<device> make(idfxx::gpio pin, onewire::address addr = onewire::address::any());

    // Copyable and movable
    device(const device&) = default;
    device(device&&) = default;
    device& operator=(const device&) = default;
    device& operator=(device&&) = default;

    constexpr bool operator==(const device&) const = default;

    /** @brief Returns the GPIO pin. */
    [[nodiscard]] constexpr idfxx::gpio pin() const { return _pin; }

    /** @brief Returns the device address. */
    [[nodiscard]] constexpr onewire::address addr() const { return _addr; }

    // Temperature measurement
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Initiates a temperature conversion.
     *
     * @param wait If true, blocks until conversion completes (up to 750ms).
     *             If false, returns immediately and the caller must wait.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void measure(bool wait = true) { unwrap(try_measure(wait)); }

    /**
     * @brief Reads the last converted temperature.
     *
     * @return The measured temperature.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] thermo::millicelsius read_temperature() const { return unwrap(try_read_temperature()); }

    /**
     * @brief Measures and reads the temperature in a single operation.
     *
     * Sends a convert command, waits for completion, then reads the result.
     *
     * @return The measured temperature.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] thermo::millicelsius measure_and_read() { return unwrap(try_measure_and_read()); }
#endif

    /**
     * @brief Initiates a temperature conversion.
     *
     * @param wait If true, blocks until conversion completes (up to 750ms).
     *             If false, returns immediately and the caller must wait.
     * @return An error on bus communication failure.
     */
    [[nodiscard]] result<void> try_measure(bool wait = true);

    /**
     * @brief Reads the last converted temperature.
     *
     * @return The measured temperature, or an error on bus communication failure.
     */
    [[nodiscard]] result<thermo::millicelsius> try_read_temperature() const;

    /**
     * @brief Measures and reads the temperature in a single operation.
     *
     * Sends a convert command, waits for completion, then reads the result.
     *
     * @return The measured temperature, or an error on bus communication failure.
     */
    [[nodiscard]] result<thermo::millicelsius> try_measure_and_read();

    // Resolution configuration
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the ADC resolution.
     *
     * Reads the current scratchpad, modifies the configuration register,
     * and writes it back. Only applicable to DS18B20 sensors.
     *
     * @param res The desired resolution.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_resolution(resolution res) { unwrap(try_set_resolution(res)); }

    /**
     * @brief Gets the current ADC resolution.
     *
     * Reads the scratchpad and extracts the configuration register.
     *
     * @return The current resolution setting.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] resolution get_resolution() const { return unwrap(try_get_resolution()); }
#endif

    /**
     * @brief Sets the ADC resolution.
     *
     * Reads the current scratchpad, modifies the configuration register,
     * and writes it back. Only applicable to DS18B20 sensors.
     *
     * @param res The desired resolution.
     * @return An error on bus communication failure.
     */
    [[nodiscard]] result<void> try_set_resolution(resolution res);

    /**
     * @brief Gets the current ADC resolution.
     *
     * Reads the scratchpad and extracts the configuration register.
     *
     * @return The current resolution setting, or an error on bus communication failure.
     */
    [[nodiscard]] result<resolution> try_get_resolution() const;

    // Low-level scratchpad access
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the 9-byte scratchpad memory.
     *
     * @return The scratchpad contents [temp_lsb, temp_msb, th, tl, config, reserved, reserved, reserved, crc].
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] std::array<uint8_t, 9> read_scratchpad() const { return unwrap(try_read_scratchpad()); }

    /**
     * @brief Writes 3 bytes to the scratchpad (TH, TL, configuration register).
     *
     * @param data The 3 bytes to write [th, tl, config].
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write_scratchpad(std::span<const uint8_t, 3> data) { unwrap(try_write_scratchpad(data)); }

    /**
     * @brief Copies the scratchpad to EEPROM.
     *
     * Persists the TH, TL, and configuration register values so they
     * survive power cycles.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void copy_scratchpad() { unwrap(try_copy_scratchpad()); }
#endif

    /**
     * @brief Reads the 9-byte scratchpad memory.
     *
     * @return The scratchpad contents, or an error on bus communication failure.
     */
    [[nodiscard]] result<std::array<uint8_t, 9>> try_read_scratchpad() const;

    /**
     * @brief Writes 3 bytes to the scratchpad (TH, TL, configuration register).
     *
     * @param data The 3 bytes to write [th, tl, config].
     * @return An error on bus communication failure.
     */
    [[nodiscard]] result<void> try_write_scratchpad(std::span<const uint8_t, 3> data);

    /**
     * @brief Copies the scratchpad to EEPROM.
     *
     * Persists the TH, TL, and configuration register values so they
     * survive power cycles.
     *
     * @return An error on bus communication failure.
     */
    [[nodiscard]] result<void> try_copy_scratchpad();

private:
    friend result<std::vector<device>> try_scan_devices(idfxx::gpio pin, size_t max_devices);

    /** @cond INTERNAL */
    struct validated {};
    device(idfxx::gpio pin, onewire::address addr, validated)
        : _pin(pin)
        , _addr(addr) {}
    /** @endcond */

    idfxx::gpio _pin;
    onewire::address _addr;
};

/** @} */ // end of idfxx_ds18x20

} // namespace idfxx::ds18x20

namespace idfxx {

/**
 * @headerfile <idfxx/ds18x20>
 * @brief Returns a string representation of a DS18x20 device family.
 *
 * @param f The family identifier to convert.
 * @return "DS18S20", "DS1822", "DS18B20", "MAX31850", or "unknown(0xNN)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(ds18x20::family f) {
    switch (f) {
    case ds18x20::family::ds18s20:
        return "DS18S20";
    case ds18x20::family::ds1822:
        return "DS1822";
    case ds18x20::family::ds18b20:
        return "DS18B20";
    case ds18x20::family::max31850:
        return "MAX31850";
    default: {
        static constexpr char hex[] = "0123456789ABCDEF";
        auto v = static_cast<uint8_t>(std::to_underlying(f));
        return std::string("unknown(0x") + hex[v >> 4] + hex[v & 0xF] + ')';
    }
    }
}

/**
 * @headerfile <idfxx/ds18x20>
 * @brief Returns a string representation of a DS18B20 resolution setting.
 *
 * @param r The resolution to convert.
 * @return "9-bit", "10-bit", "11-bit", "12-bit", or "unknown(0xNN)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(ds18x20::resolution r) {
    switch (r) {
    case ds18x20::resolution::bits_9:
        return "9-bit";
    case ds18x20::resolution::bits_10:
        return "10-bit";
    case ds18x20::resolution::bits_11:
        return "11-bit";
    case ds18x20::resolution::bits_12:
        return "12-bit";
    default: {
        static constexpr char hex[] = "0123456789ABCDEF";
        auto v = static_cast<uint8_t>(std::to_underlying(r));
        return std::string("unknown(0x") + hex[v >> 4] + hex[v & 0xF] + ')';
    }
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
struct formatter<idfxx::ds18x20::family> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::ds18x20::family f, FormatContext& ctx) const {
        auto s = idfxx::to_string(f);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::ds18x20::resolution> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::ds18x20::resolution r, FormatContext& ctx) const {
        auto s = idfxx::to_string(r);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
