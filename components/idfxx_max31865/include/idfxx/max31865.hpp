// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/max31865>
 * @file max31865.hpp
 * @brief MAX31865 RTD-to-digital converter driver.
 *
 * @defgroup idfxx_max31865 MAX31865 Component
 * @brief Type-safe MAX31865 RTD-to-digital converter driver for ESP32.
 *
 * Measures platinum RTDs (PT100, PT1000, ...) through a MAX31865 on an SPI
 * bus: one-shot and continuous conversions, Callendar–Van Dusen temperature
 * conversion, fault thresholds, fault detection cycles, and register
 * verification and restore.
 *
 * Depends on @ref idfxx_core for error handling, @ref idfxx_gpio for pin
 * configuration, and @ref idfxx_spi for the SPI bus.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/error>
#include <idfxx/flags>
#include <idfxx/gpio>
#include <idfxx/required>
#include <idfxx/spi/master>

#include <chrono>
#include <cstdint>
#include <memory>
#include <quanta/electro>
#include <quanta/frequency>
#include <quanta/thermo>
#include <string>
#include <utility>

/**
 * @headerfile <idfxx/max31865>
 * @brief MAX31865 RTD-to-digital converter driver and types.
 */
namespace idfxx::max31865 {

/**
 * @headerfile <idfxx/max31865>
 * @brief How the RTD is wired to the converter.
 */
enum class wiring {
    two_wire,   ///< 2-wire RTD connection.
    three_wire, ///< 3-wire RTD connection (lead resistance compensated).
    four_wire,  ///< 4-wire RTD connection (Kelvin connection).
};

/**
 * @headerfile <idfxx/max31865>
 * @brief Mains-frequency rejection filter.
 *
 * Choose the frequency of the local mains supply. The filter also sets the
 * conversion time: about 52 ms (55 ms max) at 60 Hz and 62.5 ms (66 ms max)
 * at 50 Hz for a one-shot conversion.
 */
enum class notch_filter {
    hz_50, ///< Reject 50 Hz and its harmonics.
    hz_60, ///< Reject 60 Hz and its harmonics.
};

/**
 * @headerfile <idfxx/max31865>
 * @brief Callendar–Van Dusen coefficients of the RTD.
 *
 * The RTD resistance at temperature T (°C) is
 * R(T) = R0 (1 + aT + bT² + c(T − 100)T³), where the c term applies only
 * below 0 °C. The defaults are the IEC 60751 values for industrial platinum
 * RTDs (alpha = 0.00385).
 */
struct cvd_coefficients {
    double a = 3.9083e-3;  ///< Linear coefficient (1/°C).
    double b = -5.775e-7;  ///< Quadratic coefficient (1/°C²).
    double c = -4.183e-12; ///< Quartic coefficient below 0 °C (1/°C⁴).
};

/**
 * @headerfile <idfxx/max31865>
 * @brief Fault Status register bits.
 *
 * Combine into `idfxx::flags<fault>`.
 */
enum class fault : uint8_t {
    high_threshold = 0x80,     ///< RTD code at or above the high fault threshold.
    low_threshold = 0x40,      ///< RTD code at or below the low fault threshold.
    refin_high = 0x20,         ///< REFIN- above 0.85 × VBIAS.
    refin_low = 0x10,          ///< REFIN- below 0.85 × VBIAS with FORCE- open.
    rtdin_low = 0x08,          ///< RTDIN- below 0.85 × VBIAS with FORCE- open.
    over_under_voltage = 0x04, ///< An input is over- or under-voltage.
};

} // namespace idfxx::max31865

/** @cond INTERNAL */
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::max31865::fault> = true;
/** @endcond */

namespace idfxx::max31865 {

/**
 * @headerfile <idfxx/max31865>
 * @brief A raw conversion result read from the RTD registers.
 */
struct sample {
    uint16_t code; ///< 15-bit RTD code (0–32767): the ratio R_RTD / R_REF scaled by 2^15.
    bool fault;    ///< True if the device has a latched fault (see @ref device::read_faults).
};

/** @cond INTERNAL */
namespace detail {

// Register addresses (datasheet Table 1). Writes set bit 7 of the address.
inline constexpr uint8_t reg_config = 0x00;
inline constexpr uint8_t reg_rtd_msb = 0x01;
inline constexpr uint8_t reg_rtd_lsb = 0x02;
inline constexpr uint8_t reg_high_threshold_msb = 0x03;
inline constexpr uint8_t reg_high_threshold_lsb = 0x04;
inline constexpr uint8_t reg_low_threshold_msb = 0x05;
inline constexpr uint8_t reg_low_threshold_lsb = 0x06;
inline constexpr uint8_t reg_fault_status = 0x07;
inline constexpr uint8_t write_bit = 0x80;

// Configuration register bits (datasheet Table 2).
inline constexpr uint8_t cfg_bias = 0x80;
inline constexpr uint8_t cfg_continuous = 0x40;
inline constexpr uint8_t cfg_one_shot = 0x20;
inline constexpr uint8_t cfg_three_wire = 0x10;
inline constexpr uint8_t cfg_fault_cycle_mask = 0x0C;
inline constexpr uint8_t cfg_fault_auto = 0x04;
inline constexpr uint8_t cfg_fault_manual_1 = 0x08;
inline constexpr uint8_t cfg_fault_manual_2 = 0x0C;
inline constexpr uint8_t cfg_fault_clear = 0x02;
inline constexpr uint8_t cfg_notch_50hz = 0x01;
// Bits that clear themselves, so never compare against the intended config.
inline constexpr uint8_t cfg_self_clearing = cfg_one_shot | cfg_fault_cycle_mask | cfg_fault_clear;

// Fault Status bits that carry meaning (D1:D0 are don't-care).
inline constexpr uint8_t fault_mask = 0xFC;

// Largest 15-bit RTD code.
inline constexpr uint16_t max_code = 0x7FFF;

// Power-on threshold codes (datasheet Table 1: high FFFFh, low 0000h).
inline constexpr uint16_t por_low_threshold = 0;
inline constexpr uint16_t por_high_threshold = max_code;

// Maximum SPI clock (datasheet tCLK).
inline constexpr quanta::frequency::hertz max_clock_speed{5'000'000};

// Automatic fault detection cycle: max time from CS high to completion.
inline constexpr std::chrono::microseconds auto_fault_cycle_time{600};

// Input filter time constant above which the fault cycle must be timed manually.
inline constexpr std::chrono::microseconds auto_fault_cycle_max_tau{100};

// CS-to-SCLK setup time (datasheet tCC) expressed in SPI clock cycles, plus one cycle of margin.
[[nodiscard]] constexpr uint16_t cs_pretrans_cycles(quanta::frequency::hertz clock_speed) noexcept {
    constexpr int64_t t_cc_ns = 400;
    constexpr int64_t ns_per_s = 1'000'000'000;
    const int64_t f = clock_speed.count() > 0 ? clock_speed.count() : 0;
    return static_cast<uint16_t>((t_cc_ns * f + ns_per_s - 1) / ns_per_s + 1);
}

// Maximum one-shot conversion time for a notch filter (datasheet tCONV).
[[nodiscard]] constexpr std::chrono::microseconds max_conversion_time(notch_filter notch) noexcept {
    return notch == notch_filter::hz_50 ? std::chrono::microseconds{66'000} : std::chrono::microseconds{55'000};
}

// Time to wait after enabling VBIAS before a conversion: 10.5 time constants plus 1 ms.
[[nodiscard]] constexpr std::chrono::microseconds bias_settle_time(std::chrono::microseconds tau) noexcept {
    return tau * 21 / 2 + std::chrono::microseconds{1'000};
}

// Callendar–Van Dusen resistance (ohms) at a temperature (°C).
[[nodiscard]] double cvd_resistance(double celsius, double r0_ohms, const cvd_coefficients& k) noexcept;

// Inverse Callendar–Van Dusen: temperature (°C) at a resistance (ohms).
[[nodiscard]] double cvd_temperature(double ohms, double r0_ohms, const cvd_coefficients& k) noexcept;

// RTD resistance for a code: code × RREF / 2^15, rounded to the nearest milliohm.
[[nodiscard]] quanta::electro::milliohms code_to_resistance(uint16_t code, quanta::electro::milliohms rref) noexcept;

// Code for an RTD resistance, rounded to the nearest code and saturated to [0, 32767].
[[nodiscard]] uint16_t resistance_to_code(quanta::electro::milliohms r, quanta::electro::milliohms rref) noexcept;

// Temperature for a code, rounded to the nearest millidegree.
[[nodiscard]] quanta::thermo::millicelsius code_to_temperature(
    uint16_t code,
    quanta::electro::milliohms rref,
    quanta::electro::milliohms r0,
    const cvd_coefficients& k
) noexcept;

// Code for a temperature, rounded to the nearest code and saturated to [0, 32767].
[[nodiscard]] uint16_t temperature_to_code(
    quanta::thermo::millicelsius t,
    quanta::electro::milliohms rref,
    quanta::electro::milliohms r0,
    const cvd_coefficients& k
) noexcept;

} // namespace detail
/** @endcond */

/**
 * @headerfile <idfxx/max31865>
 * @brief A MAX31865 RTD-to-digital converter on an SPI bus.
 *
 * The device creates and owns its own SPI device on the given bus, so the
 * caller only keeps the bus alive. It remembers the intended configuration
 * (bias, conversion mode, wiring, notch filter) and fault thresholds, which
 * verify() checks against the chip and restore() writes back.
 *
 * @code
 * using namespace quanta::electro_literals;
 * using namespace std::chrono_literals;
 *
 * idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, {
 *     .mosi = idfxx::gpio_11,
 *     .miso = idfxx::gpio_13,
 *     .sclk = idfxx::gpio_12,
 * });
 *
 * idfxx::max31865::device rtd(bus, {
 *     .cs = idfxx::gpio_10,
 *     .r0 = 1000_Ohm,
 *     .rref = 4300_Ohm,
 *     .wiring = idfxx::max31865::wiring::three_wire,
 *     .notch = idfxx::max31865::notch_filter::hz_50,
 *     .filter_time_constant = 100us,
 * });
 *
 * auto s = rtd.measure();
 * quanta::thermo::millicelsius t = rtd.to_temperature(s.code);
 * @endcode
 *
 * A device is not thread-safe: use it from one task, or synchronize access
 * externally. Several devices may share a bus; ESP-IDF arbitrates the bus
 * between devices for each transaction.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior.
 */
class device {
public:
    /**
     * @headerfile <idfxx/max31865>
     * @brief Configuration for a MAX31865 device.
     *
     * Fields wrapped in `idfxx::required` have no safe default and must be
     * set; omitting one from a designated initializer fails to compile.
     */
    struct config {
        idfxx::required<idfxx::gpio> cs;                  ///< SPI chip-select pin; must be connected.
        idfxx::required<quanta::electro::milliohms> r0;   ///< RTD resistance at 0 °C (e.g. 100 Ω for PT100).
        idfxx::required<quanta::electro::milliohms> rref; ///< Reference resistor value, as fitted on the board.
        idfxx::required<max31865::wiring> wiring;         ///< RTD wiring.
        idfxx::required<notch_filter> notch;              ///< Mains-rejection filter; fixed for the device's lifetime.
        /// Time constant of the RC filter on the RTDIN inputs. It sets the
        /// settling wait after the bias is enabled, and above 100 µs selects
        /// the manually timed fault detection cycle.
        idfxx::required<std::chrono::microseconds> filter_time_constant;
        quanta::frequency::hertz clock_speed{5'000'000}; ///< SPI clock; must be above 0 and at most 5 MHz.
        cvd_coefficients coefficients = {};              ///< Callendar–Van Dusen coefficients of the RTD.
        idfxx::gpio drdy = gpio::nc();                   ///< Optional DRDY input; nc() to wait by time instead.
    };

    // =========================================================================
    // Construction
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a MAX31865 device on the given SPI bus.
     *
     * Writes the configuration (bias off, conversions off) and the power-on
     * default fault thresholds, then reads them back to confirm the chip is
     * responding.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this device.
     *
     * @param bus    Parent SPI master bus.
     * @param config Device configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if the chip-select pin is not
     *         connected, the clock speed is 0 or above 5 MHz, r0 or rref is not positive, or
     *         the filter time constant is negative; with `errc::not_found` if the chip does
     *         not read back the written configuration; or on SPI failure.
     */
    [[nodiscard]] explicit device(spi::master_bus& bus, config config);
#endif

    /**
     * @brief Creates a MAX31865 device on the given SPI bus.
     *
     * Writes the configuration (bias off, conversions off) and the power-on
     * default fault thresholds, then reads them back to confirm the chip is
     * responding.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this device.
     *
     * @param bus    Parent SPI master bus.
     * @param config Device configuration.
     * @return The new device, or an error.
     * @retval invalid_arg The chip-select pin is not connected, the clock speed is 0 or above
     *         5 MHz, r0 or rref is not positive, or the filter time constant is negative.
     * @retval not_found The chip did not read back the written configuration (no device responding).
     */
    [[nodiscard]] static result<device> make(spi::master_bus& bus, config config);

    /**
     * @brief Releases the SPI device.
     *
     * The chip is left in its current state.
     */
    ~device();

    device(const device&) = delete;
    device& operator=(const device&) = delete;

    /**
     * @brief Move constructor.
     *
     * @param other The device to move from; it must not be used afterwards.
     */
    device(device&& other) noexcept;

    /**
     * @brief Move assignment operator.
     *
     * Releases this device's SPI device, then takes over @p other's.
     *
     * @param other The device to move from; it must not be used afterwards.
     * @return A reference to this device.
     */
    device& operator=(device&& other) noexcept;

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Returns the SPI device used to talk to the chip.
     *
     * @return The SPI device owned by this device.
     */
    [[nodiscard]] spi::master_device& spi() noexcept;

    // =========================================================================
    // Conversions
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Performs a single blocking conversion.
     *
     * If the bias is off, it is turned on and allowed to settle for
     * 10.5 time constants of the input filter plus 1 ms, then turned off
     * again after the conversion. The call then waits for the conversion:
     * on the DRDY pin if one is configured, otherwise for the maximum
     * conversion time (55 ms at 60 Hz, 66 ms at 50 Hz).
     *
     * @return The conversion result.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_state` if continuous conversion is running,
     *         with `errc::timeout` if DRDY does not go low in time, or on SPI failure.
     */
    [[nodiscard]] sample measure() { return unwrap(try_measure()); }

    /**
     * @brief Starts continuous conversion.
     *
     * Turns the bias on and starts converting at the notch filter rate
     * (about 60 or 50 conversions per second). Before starting, it blocks
     * until the input filter has settled: 10.5 time constants plus 1 ms after
     * the bias was turned on, and the settling time after any fault detection
     * cycle. It does not wait for the first conversion, which takes as long
     * as a one-shot conversion, so a read() immediately after this may return
     * a stale or zero result. Does nothing if continuous conversion is
     * already running.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on SPI failure.
     */
    void start_continuous() { unwrap(try_start_continuous()); }

    /**
     * @brief Stops continuous conversion.
     *
     * Leaves the bias as it was before start_continuous(): it stays on if it
     * was enabled with enable_bias(). Does nothing if continuous conversion
     * is not running.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on SPI failure.
     */
    void stop_continuous() { unwrap(try_stop_continuous()); }

    /**
     * @brief Reads the latest conversion result from the RTD registers.
     *
     * Reading the RTD registers also returns DRDY high.
     *
     * @return The latest conversion result.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on SPI failure.
     */
    [[nodiscard]] sample read() { return unwrap(try_read()); }

    /**
     * @brief Reads the latest conversion result as a temperature.
     *
     * @return The temperature from the latest conversion result.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_response` if the result's fault bit is set,
     *         or on SPI failure.
     */
    [[nodiscard]] quanta::thermo::millicelsius read_temperature() { return unwrap(try_read_temperature()); }
#endif

    /**
     * @brief Performs a single blocking conversion.
     *
     * If the bias is off, it is turned on and allowed to settle for
     * 10.5 time constants of the input filter plus 1 ms, then turned off
     * again after the conversion. The call then waits for the conversion:
     * on the DRDY pin if one is configured, otherwise for the maximum
     * conversion time (55 ms at 60 Hz, 66 ms at 50 Hz).
     *
     * @return The conversion result, or an error.
     * @retval invalid_state Continuous conversion is running.
     * @retval timeout DRDY did not go low within the maximum conversion time.
     */
    [[nodiscard]] result<sample> try_measure();

    /**
     * @brief Starts continuous conversion.
     *
     * Turns the bias on and starts converting at the notch filter rate
     * (about 60 or 50 conversions per second). Before starting, it blocks
     * until the input filter has settled: 10.5 time constants plus 1 ms after
     * the bias was turned on, and the settling time after any fault detection
     * cycle. It does not wait for the first conversion, which takes as long
     * as a one-shot conversion, so a read() immediately after this may return
     * a stale or zero result. Does nothing if continuous conversion is
     * already running.
     *
     * @return Success, or an error on SPI failure.
     */
    [[nodiscard]] result<void> try_start_continuous();

    /**
     * @brief Stops continuous conversion.
     *
     * Leaves the bias as it was before start_continuous(): it stays on if it
     * was enabled with enable_bias(). Does nothing if continuous conversion
     * is not running.
     *
     * @return Success, or an error on SPI failure.
     */
    result<void> try_stop_continuous();

    /**
     * @brief Reads the latest conversion result from the RTD registers.
     *
     * Reading the RTD registers also returns DRDY high.
     *
     * @return The latest conversion result, or an error on SPI failure.
     */
    [[nodiscard]] result<sample> try_read();

    /**
     * @brief Reads the latest conversion result as a temperature.
     *
     * @return The temperature from the latest conversion result, or an error.
     * @retval invalid_response The result's fault bit is set; see try_read_faults().
     */
    [[nodiscard]] result<quanta::thermo::millicelsius> try_read_temperature();

    // =========================================================================
    // Bias
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Turns the RTD bias voltage on.
     *
     * Keeping the bias on avoids the settling wait in each measure() at the
     * cost of supply current. A measure() shortly after this still waits
     * for the remainder of the settling time.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on SPI failure.
     */
    void enable_bias() { unwrap(try_enable_bias()); }

    /**
     * @brief Turns the RTD bias voltage off.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_state` if continuous conversion is running,
     *         or on SPI failure.
     */
    void disable_bias() { unwrap(try_disable_bias()); }
#endif

    /**
     * @brief Turns the RTD bias voltage on.
     *
     * Keeping the bias on avoids the settling wait in each measure() at the
     * cost of supply current. A measure() shortly after this still waits
     * for the remainder of the settling time.
     *
     * @return Success, or an error on SPI failure.
     */
    [[nodiscard]] result<void> try_enable_bias();

    /**
     * @brief Turns the RTD bias voltage off.
     *
     * @return Success, or an error.
     * @retval invalid_state Continuous conversion is running (it needs the bias).
     */
    result<void> try_disable_bias();

    // =========================================================================
    // Unit conversion
    // =========================================================================

    /**
     * @brief Converts an RTD code to resistance.
     *
     * Computes code × RREF / 2^15 from the configured reference resistor.
     *
     * @param code 15-bit RTD code.
     * @return The RTD resistance, rounded to the nearest milliohm.
     */
    [[nodiscard]] quanta::electro::milliohms to_resistance(uint16_t code) const noexcept;

    /**
     * @brief Converts an RTD code to temperature.
     *
     * Applies the Callendar–Van Dusen equation with the configured R0 and
     * coefficients, solved exactly (to within 1 m°C) over the full range,
     * including below 0 °C.
     *
     * @param code 15-bit RTD code.
     * @return The temperature, rounded to the nearest millidegree.
     */
    [[nodiscard]] quanta::thermo::millicelsius to_temperature(uint16_t code) const noexcept;

    /**
     * @brief Converts a resistance to the nearest RTD code.
     *
     * @param resistance The RTD resistance.
     * @return The nearest code, saturated to 0–32767.
     */
    [[nodiscard]] uint16_t to_code(quanta::electro::milliohms resistance) const noexcept;

    /**
     * @brief Converts a temperature to the nearest RTD code.
     *
     * @param temperature The temperature.
     * @return The nearest code, saturated to 0–32767.
     */
    [[nodiscard]] uint16_t to_code(quanta::thermo::millicelsius temperature) const noexcept;

    // =========================================================================
    // Faults
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the latched faults from the Fault Status register.
     *
     * @return The latched faults (empty if none).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on SPI failure.
     */
    [[nodiscard]] flags<fault> read_faults() { return unwrap(try_read_faults()); }

    /**
     * @brief Clears the latched faults.
     *
     * A fault that persists (such as an over- or under-voltage) may latch
     * again immediately.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on SPI failure.
     */
    void clear_faults() { unwrap(try_clear_faults()); }

    /**
     * @brief Runs a fault detection cycle and returns the faults it found.
     *
     * Checks for an open or shorted RTD and broken reference connections.
     * Faults latched earlier are cleared first, so the result reflects only
     * this cycle (plus any fault that persists, such as an over- or
     * under-voltage). The bias must have been on for 5 time constants
     * first, so the call turns it on and waits if needed. With a filter time constant of up to 100 µs the chip times
     * the cycle itself (about 600 µs); above that, the driver times it, waiting 5 time constants for each of the two
     * steps. Afterwards the driver waits for the input filter to settle and restores the previous configuration,
     * resuming continuous conversion if it was running.
     *
     * @return The faults in the Fault Status register after the cycle (empty if none).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::timeout` if the cycle does not complete, or on SPI
     *         failure.
     */
    [[nodiscard]] flags<fault> detect_faults() { return unwrap(try_detect_faults()); }
#endif

    /**
     * @brief Reads the latched faults from the Fault Status register.
     *
     * @return The latched faults (empty if none), or an error on SPI failure.
     */
    [[nodiscard]] result<flags<fault>> try_read_faults();

    /**
     * @brief Clears the latched faults.
     *
     * A fault that persists (such as an over- or under-voltage) may latch
     * again immediately.
     *
     * @return Success, or an error on SPI failure.
     */
    [[nodiscard]] result<void> try_clear_faults();

    /**
     * @brief Runs a fault detection cycle and returns the faults it found.
     *
     * Checks for an open or shorted RTD and broken reference connections.
     * Faults latched earlier are cleared first, so the result reflects only
     * this cycle (plus any fault that persists, such as an over- or
     * under-voltage). The bias must have been on for 5 time constants
     * first, so the call turns it on and waits if needed. With a filter time constant of up to 100 µs the chip times
     * the cycle itself (about 600 µs); above that, the driver times it, waiting 5 time constants for each of the two
     * steps. Afterwards the driver waits for the input filter to settle and restores the previous configuration,
     * resuming continuous conversion if it was running.
     *
     * @return The faults in the Fault Status register after the cycle (empty if none), or an
     *         error.
     * @retval timeout The cycle did not complete in time.
     */
    [[nodiscard]] result<flags<fault>> try_detect_faults();

    // =========================================================================
    // Fault thresholds
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the fault thresholds as RTD codes.
     *
     * A conversion at or above @p high latches fault::high_threshold; one at
     * or below @p low latches fault::low_threshold.
     *
     * @param low  Low threshold code (0–32767).
     * @param high High threshold code (0–32767), not below @p low.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if a code is above 32767 or
     *         @p low is above @p high, or on SPI failure.
     */
    void set_fault_thresholds(uint16_t low, uint16_t high) { unwrap(try_set_fault_thresholds(low, high)); }

    /**
     * @brief Sets the fault thresholds as resistances.
     *
     * @param low  Low threshold resistance, at least 0 and below RREF.
     * @param high High threshold resistance, at least 0 and below RREF, not below @p low.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if a resistance is outside
     *         [0, RREF) or @p low is above @p high, or on SPI failure.
     */
    void set_fault_thresholds(quanta::electro::milliohms low, quanta::electro::milliohms high) {
        unwrap(try_set_fault_thresholds(low, high));
    }

    /**
     * @brief Sets the fault thresholds as temperatures.
     *
     * @code
     * using namespace quanta::thermo_literals;
     * rtd.set_fault_thresholds(-20_c, 150_c);
     * @endcode
     *
     * @param low  Low threshold temperature.
     * @param high High threshold temperature, not below @p low.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if a temperature's resistance is
     *         outside [0, RREF) or @p low is above @p high, or on SPI failure.
     */
    void set_fault_thresholds(quanta::thermo::millicelsius low, quanta::thermo::millicelsius high) {
        unwrap(try_set_fault_thresholds(low, high));
    }

    /**
     * @brief Reads the fault thresholds from the chip.
     *
     * @return The low and high threshold codes, in that order.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on SPI failure.
     */
    [[nodiscard]] std::pair<uint16_t, uint16_t> read_fault_thresholds() { return unwrap(try_read_fault_thresholds()); }
#endif

    /**
     * @brief Sets the fault thresholds as RTD codes.
     *
     * A conversion at or above @p high latches fault::high_threshold; one at
     * or below @p low latches fault::low_threshold.
     *
     * @param low  Low threshold code (0–32767).
     * @param high High threshold code (0–32767), not below @p low.
     * @return Success, or an error.
     * @retval invalid_arg A code is above 32767, or @p low is above @p high.
     */
    [[nodiscard]] result<void> try_set_fault_thresholds(uint16_t low, uint16_t high);

    /**
     * @brief Sets the fault thresholds as resistances.
     *
     * @param low  Low threshold resistance, at least 0 and below RREF.
     * @param high High threshold resistance, at least 0 and below RREF, not below @p low.
     * @return Success, or an error.
     * @retval invalid_arg A resistance is outside [0, RREF), or @p low is above @p high.
     */
    [[nodiscard]] result<void>
    try_set_fault_thresholds(quanta::electro::milliohms low, quanta::electro::milliohms high);

    /**
     * @brief Sets the fault thresholds as temperatures.
     *
     * @param low  Low threshold temperature.
     * @param high High threshold temperature, not below @p low.
     * @return Success, or an error.
     * @retval invalid_arg A temperature's resistance is outside [0, RREF), or @p low is above
     *         @p high.
     */
    [[nodiscard]] result<void>
    try_set_fault_thresholds(quanta::thermo::millicelsius low, quanta::thermo::millicelsius high);

    /**
     * @brief Reads the fault thresholds from the chip.
     *
     * @return The low and high threshold codes, in that order, or an error on SPI failure.
     */
    [[nodiscard]] result<std::pair<uint16_t, uint16_t>> try_read_fault_thresholds();

    // =========================================================================
    // Verification
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Checks that the chip still holds the intended configuration and thresholds.
     *
     * A brown-out or reset of the chip silently returns it to its power-on
     * defaults, and SPI has no CRC to catch a corrupted write, so call this
     * periodically and restore() on a mismatch.
     *
     * @return True if the configuration register (ignoring self-clearing
     *         bits) and both fault thresholds match.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on SPI failure.
     */
    [[nodiscard]] bool verify() { return unwrap(try_verify()); }

    /**
     * @brief Rewrites the intended configuration and thresholds to the chip.
     *
     * @code
     * if (!rtd.verify()) {
     *     rtd.restore();
     * }
     * @endcode
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on SPI failure.
     */
    void restore() { unwrap(try_restore()); }
#endif

    /**
     * @brief Checks that the chip still holds the intended configuration and thresholds.
     *
     * A brown-out or reset of the chip silently returns it to its power-on
     * defaults, and SPI has no CRC to catch a corrupted write, so call this
     * periodically and restore on a mismatch.
     *
     * @return True if the configuration register (ignoring self-clearing
     *         bits) and both fault thresholds match, or an error on SPI failure.
     */
    [[nodiscard]] result<bool> try_verify();

    /**
     * @brief Rewrites the intended configuration and thresholds to the chip.
     *
     * @return Success, or an error on SPI failure.
     */
    [[nodiscard]] result<void> try_restore();

    /// @cond INTERNAL
    struct state;
    /// @endcond

private:
    explicit device(std::unique_ptr<state> s) noexcept;

    std::unique_ptr<state> _state;
};

/** @} */ // end of idfxx_max31865

} // namespace idfxx::max31865

namespace idfxx {

/**
 * @headerfile <idfxx/max31865>
 * @brief Returns a string representation of an RTD wiring.
 *
 * @param w The wiring to convert.
 * @return "2-wire", "3-wire", "4-wire", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(max31865::wiring w) {
    switch (w) {
    case max31865::wiring::two_wire:
        return "2-wire";
    case max31865::wiring::three_wire:
        return "3-wire";
    case max31865::wiring::four_wire:
        return "4-wire";
    default:
        return "unknown(" + std::to_string(std::to_underlying(w)) + ")";
    }
}

/**
 * @headerfile <idfxx/max31865>
 * @brief Returns a string representation of a notch filter.
 *
 * @param n The notch filter to convert.
 * @return "50Hz", "60Hz", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(max31865::notch_filter n) {
    switch (n) {
    case max31865::notch_filter::hz_50:
        return "50Hz";
    case max31865::notch_filter::hz_60:
        return "60Hz";
    default:
        return "unknown(" + std::to_string(std::to_underlying(n)) + ")";
    }
}

/**
 * @headerfile <idfxx/max31865>
 * @brief Returns a string representation of a fault.
 *
 * @param f The fault to convert.
 * @return "high_threshold", "low_threshold", "refin_high", "refin_low",
 *         "rtdin_low", "over_under_voltage", or "unknown(0xNN)" for
 *         unrecognized values.
 */
[[nodiscard]] std::string to_string(max31865::fault f);

/**
 * @headerfile <idfxx/max31865>
 * @brief Returns a string representation of a set of faults.
 *
 * @param faults The faults to convert.
 * @return The fault names joined with '|' (e.g. "high_threshold|rtdin_low"),
 *         "none" if empty, with any unrecognized bits as "unknown(0xNN)".
 */
[[nodiscard]] std::string to_string(flags<max31865::fault> faults);

} // namespace idfxx

#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::max31865::wiring> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::max31865::wiring w, FormatContext& ctx) const {
        auto s = idfxx::to_string(w);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::max31865::notch_filter> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::max31865::notch_filter n, FormatContext& ctx) const {
        auto s = idfxx::to_string(n);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::max31865::fault> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::max31865::fault f, FormatContext& ctx) const {
        auto s = idfxx::to_string(f);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::flags<idfxx::max31865::fault>> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::flags<idfxx::max31865::fault> faults, FormatContext& ctx) const {
        auto s = idfxx::to_string(faults);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
