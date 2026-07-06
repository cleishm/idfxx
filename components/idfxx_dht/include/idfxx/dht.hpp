// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/dht>
 * @file dht.hpp
 * @brief DHT11/DHT22 temperature and humidity sensor driver.
 *
 * @defgroup idfxx_dht DHT Component
 * @brief Type-safe DHT11/DHT22 (AM2302) sensor driver for ESP32.
 *
 * Reads temperature and relative humidity over the sensor's single-wire
 * protocol. The host issues a start pulse on the data line and the sensor
 * replies with a 40-bit frame, which is captured with the RMT peripheral
 * (no interrupt-latency-sensitive bit-banging) and decoded with checksum
 * verification.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin
 * configuration.
 * @{
 */

#include <idfxx/error>
#include <idfxx/gpio>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thermo/thermo>
#include <utility>

/**
 * @headerfile <idfxx/dht>
 * @brief DHT sensor classes and utilities.
 */
namespace idfxx::dht {

/**
 * @headerfile <idfxx/dht>
 * @brief Supported sensor models.
 *
 * Selects the start-pulse timing and the 40-bit frame encoding. The AM2301
 * (DHT21) and AM2302 modules speak the DHT22 protocol.
 */
enum class model : uint8_t {
    dht11, ///< DHT11: 1 °C / 1 %RH resolution, 1 s minimum sampling period.
    dht22, ///< DHT22 / AM2301 / AM2302: 0.1 °C / 0.1 %RH resolution, 2 s period.
};

/**
 * @headerfile <idfxx/dht>
 * @brief One temperature and humidity measurement.
 */
struct reading {
    thermo::millicelsius temperature{0}; ///< Measured temperature.
    float humidity_pct = 0.0f;           ///< Relative humidity in percent (0–100).
};

/**
 * @headerfile <idfxx/dht>
 * @brief DHT11/DHT22 sensor on a single GPIO data line.
 *
 * Move-only. The destructor releases the RMT receive channel.
 *
 * @code
 * idfxx::dht::sensor sensor({.pin = idfxx::gpio_1, .model = idfxx::dht::model::dht22});
 * auto r = sensor.read();
 * // r.temperature, r.humidity_pct
 * @endcode
 */
class sensor {
public:
    /**
     * @headerfile <idfxx/dht>
     * @brief Configuration for a DHT sensor.
     */
    struct config {
        idfxx::gpio pin = gpio::nc();    ///< Data line GPIO (required).
        enum model model = model::dht22; ///< Sensor model on the line.
        /// Enable the internal pull-up on the data line. The bus idles high;
        /// disable only if the board has an external pull-up resistor.
        bool internal_pullup = true;
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Constructs a sensor driver.
     *
     * Claims an RMT receive channel for the data line and configures the pin
     * as an open-drain input/output.
     *
     * @param config Sensor configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (e.g. no free RMT channel).
     */
    [[nodiscard]] explicit sensor(config config);
#endif

    /**
     * @brief Creates a sensor driver.
     *
     * Claims an RMT receive channel for the data line and configures the pin
     * as an open-drain input/output.
     *
     * @param config Sensor configuration.
     * @return The driver, or an error.
     * @retval idfxx::errc::invalid_arg If the pin is not connected.
     */
    [[nodiscard]] static result<sensor> make(config config);

    ~sensor();

    sensor(const sensor&) = delete;
    sensor& operator=(const sensor&) = delete;
    sensor(sensor&& other) noexcept;
    sensor& operator=(sensor&& other) noexcept;

    /** @brief Returns the configured data-line GPIO. */
    [[nodiscard]] idfxx::gpio pin() const noexcept;

    /** @brief Returns the configured sensor model. */
    [[nodiscard]] enum model model() const noexcept;

    /**
     * @brief Returns the minimum interval between reads for the model.
     *
     * 1 s for the DHT11, 2 s for the DHT22. @ref try_read fails with
     * `idfxx::errc::invalid_state` when called again sooner.
     *
     * @return The model's minimum sampling period.
     */
    [[nodiscard]] std::chrono::milliseconds min_read_interval() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Performs one measurement.
     *
     * Issues the start pulse, captures the sensor's 40-bit reply, verifies
     * its checksum, and converts it. Blocks for the start pulse plus the
     * reply: roughly 25 ms for the DHT11 (its ~20 ms start pulse dominates)
     * and under 10 ms for the DHT22.
     *
     * @return The measurement.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure; see @ref try_read for the error
     *         conditions.
     */
    [[nodiscard]] reading read() { return unwrap(try_read()); }
#endif

    /**
     * @brief Performs one measurement.
     *
     * Issues the start pulse, captures the sensor's 40-bit reply, verifies
     * its checksum, and converts it. Blocks for the start pulse plus the
     * reply: roughly 25 ms for the DHT11 (its ~20 ms start pulse dominates)
     * and under 10 ms for the DHT22.
     *
     * @return The measurement, or an error.
     * @retval idfxx::errc::invalid_state Called again within
     *         @ref min_read_interval of the previous read (the sensor needs
     *         the gap to refresh its measurement).
     * @retval idfxx::errc::timeout The sensor did not answer the start pulse:
     *         no response preamble was captured. Typically no sensor on the
     *         line, or a wiring, power, or pull-up fault.
     * @retval idfxx::errc::invalid_response A reply began but the 40-bit frame
     *         was incomplete or malformed — line noise, a marginal pull-up, or
     *         the wrong model selected.
     * @retval idfxx::errc::invalid_crc The reply failed its checksum.
     */
    [[nodiscard]] result<reading> try_read();

private:
    /// @cond INTERNAL
    struct state;
    explicit sensor(std::unique_ptr<state> s) noexcept;
    /// @endcond

    std::unique_ptr<state> _state;
};

/** @} */ // end of idfxx_dht

} // namespace idfxx::dht

namespace idfxx {

/**
 * @headerfile <idfxx/dht>
 * @brief Returns a string representation of a DHT sensor model.
 *
 * @param m The model to convert.
 * @return "DHT11", "DHT22", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(dht::model m) {
    switch (m) {
    case dht::model::dht11:
        return "DHT11";
    case dht::model::dht22:
        return "DHT22";
    default:
        return "unknown(" + std::to_string(std::to_underlying(m)) + ')';
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
struct formatter<idfxx::dht::model> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::dht::model m, FormatContext& ctx) const {
        auto s = idfxx::to_string(m);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
