// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/adc>
 * @file adc.hpp
 * @brief Analog input reading with calibrated voltages.
 *
 * @defgroup idfxx_adc ADC Component
 * @brief Type-safe one-shot and continuous ADC reads for ESP32.
 *
 * Two acquisition modes, one type per mode:
 *
 * - @ref input reads a single analog input on demand — the typical
 *   battery-voltage or sensor-divider case.
 * - @ref sampler converts one or more pins round-robin at a fixed rate in
 *   the background, delivering results as parsed per-pin samples — the
 *   waveform-capture and signal-statistics case.
 *
 * In both modes the ADC unit and channel are resolved from the GPIO pin,
 * and readings convert to typed voltages (`electro::millivolts`) with the
 * chip's factory calibration when available.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin
 * configuration.
 * @{
 */

#include <idfxx/error>
#include <idfxx/gpio>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <electro/electro>
#include <frequency/frequency>
#include <memory>
#include <optional>
#include <span>
#include <vector>

/**
 * @headerfile <idfxx/adc>
 * @brief ADC classes and utilities.
 */
namespace idfxx::adc {

/**
 * @headerfile <idfxx/adc>
 * @brief Input attenuation, which sets the measurable voltage range.
 *
 * Higher attenuation extends the range at some cost in accuracy. On most
 * chips `db_12` measures roughly up to the supply voltage.
 */
enum class attenuation : uint8_t {
    db_0,   ///< No attenuation (~0–950 mV typical range).
    db_2_5, ///< ~0–1250 mV.
    db_6,   ///< ~0–1750 mV.
    db_12,  ///< ~0–3100 mV (full range).
};

/**
 * @headerfile <idfxx/adc>
 * @brief External voltage-divider ratio between a source and an ADC pin.
 *
 * Describes a resistive divider on the board: the source voltage is the pin
 * voltage multiplied by `num / den`. The default 1:1 means the pin is
 * connected directly to the source.
 */
struct divider_ratio {
    int num = 1; ///< Ratio numerator (source = pin × num / den); must be positive.
    int den = 1; ///< Ratio denominator; must be positive.

    /**
     * @brief Compares two divider ratios for equality.
     */
    [[nodiscard]] constexpr bool operator==(const divider_ratio&) const noexcept = default;
};

/**
 * @headerfile <idfxx/adc>
 * @brief A one-shot analog input on a single pin.
 *
 * Claims the pin's ADC unit for the lifetime of the object. Move-only.
 *
 * @code
 * idfxx::adc::input battery({.pin = idfxx::gpio_3});
 * electro::millivolts v = battery.read_voltage();  // calibrated voltage at the pin
 * @endcode
 */
class input {
public:
    /**
     * @headerfile <idfxx/adc>
     * @brief Configuration for a one-shot analog input.
     */
    struct config {
        idfxx::gpio pin = gpio::nc();                      ///< ADC-capable GPIO (required).
        enum attenuation attenuation = attenuation::db_12; ///< Input range selection.
        /// External voltage-divider ratio between the source and the pin.
        /// @ref read_voltage scales by it to report the source voltage;
        /// @ref read_raw is unaffected.
        divider_ratio divider{};

        /**
         * @brief Compares two configurations for equality.
         */
        [[nodiscard]] constexpr bool operator==(const config&) const noexcept = default;
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Constructs a one-shot analog input.
     *
     * Resolves the ADC unit/channel from the pin, claims the unit, and sets
     * up factory calibration when the chip provides it.
     *
     * @param config Input configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (e.g. the pin is not ADC-capable
     *         or the unit is already claimed).
     */
    [[nodiscard]] explicit input(config config);
#endif

    /**
     * @brief Creates a one-shot analog input.
     *
     * Resolves the ADC unit/channel from the pin, claims the unit, and sets
     * up factory calibration when the chip provides it.
     *
     * @param config Input configuration.
     * @return The input, or an error.
     * @retval idfxx::errc::invalid_arg The pin is not connected or not
     *         ADC-capable, or the divider ratio is not positive.
     */
    [[nodiscard]] static result<input> make(config config);

    ~input();

    input(const input&) = delete;
    input& operator=(const input&) = delete;
    input(input&& other) noexcept;
    input& operator=(input&& other) noexcept;

    /** @brief Returns the configured pin. */
    [[nodiscard]] idfxx::gpio pin() const noexcept;

    /** @brief Returns the configured input-range attenuation. */
    [[nodiscard]] enum attenuation attenuation() const noexcept;

    /** @brief Returns the configured external voltage-divider ratio. */
    [[nodiscard]] divider_ratio divider() const noexcept;

    /** @brief Returns true when factory calibration is active for this input. */
    [[nodiscard]] bool calibrated() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the raw ADC conversion value.
     * @return The raw value (range depends on the chip's bit width).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] int read_raw() { return unwrap(try_read_raw()); }

    /**
     * @brief Reads the input voltage.
     *
     * Returns the source voltage: the calibrated voltage at the pin, scaled
     * by the configured @ref config::divider (1:1 by default).
     *
     * @return The calibrated, divider-scaled voltage.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including
     *         `idfxx::errc::not_supported` when the chip has no usable
     *         calibration data.
     */
    [[nodiscard]] electro::millivolts read_voltage() { return unwrap(try_read_voltage()); }
#endif

    /**
     * @brief Reads the raw ADC conversion value.
     * @return The raw value, or an error.
     */
    [[nodiscard]] result<int> try_read_raw();

    /**
     * @brief Reads the input voltage.
     *
     * Returns the source voltage: the calibrated voltage at the pin, scaled
     * by the configured @ref config::divider (1:1 by default).
     *
     * @return The calibrated, divider-scaled voltage, or an error.
     * @retval idfxx::errc::not_supported The chip has no usable calibration
     *         data (check @ref calibrated).
     */
    [[nodiscard]] result<electro::millivolts> try_read_voltage();

private:
    /// @cond INTERNAL
    struct state;
    explicit input(std::unique_ptr<state> s) noexcept;
    /// @endcond

    std::unique_ptr<state> _state;
};

/**
 * @headerfile <idfxx/adc>
 * @brief A continuous sampler over one or more analog pins.
 *
 * Converts the configured pins round-robin at a fixed total rate and
 * buffers the results internally; readers drain them with @ref read /
 * @ref try_read. Claims the pins' ADC unit (ADC1) for the lifetime of the
 * object. Move-only.
 *
 * @code
 * using namespace frequency_literals;
 *
 * idfxx::adc::sampler mic({.pins = {idfxx::gpio_3}, .sample_rate = 20_kHz});
 * mic.start();
 *
 * std::array<idfxx::adc::sampler::sample, 256> buf;
 * size_t n = mic.read(buf);
 * for (size_t i = 0; i < n; ++i) {
 *     electro::millivolts v = mic.to_voltage(buf[i]);
 *     // ...
 * }
 * @endcode
 */
class sampler {
public:
    /**
     * @headerfile <idfxx/adc>
     * @brief Configuration for a continuous sampler.
     */
    struct config {
        /// ADC1-capable GPIOs, converted round-robin in this order (required, no duplicates).
        std::vector<idfxx::gpio> pins{};
        /// Input range selection, applied uniformly to all pins (hardware limitation).
        enum attenuation attenuation = attenuation::db_12;
        /// Total conversion rate across all pins; each pin samples at this rate divided by
        /// the pin count. Valid range is chip-specific; 20 kHz works on every target.
        freq::hertz sample_rate{20'000};
        /// Samples delivered per driver transfer — the read granularity.
        size_t frame_samples = 256;
        /// Internal pool capacity in samples (at least @ref frame_samples). Reads must
        /// drain the pool faster than it fills or data is dropped (see @ref overruns).
        size_t buffer_samples = 1024;

        /**
         * @brief Compares two configurations for equality.
         */
        [[nodiscard]] bool operator==(const config&) const noexcept = default;
    };

    /**
     * @headerfile <idfxx/adc>
     * @brief A single conversion result.
     */
    struct sample {
        idfxx::gpio pin = gpio::nc(); ///< The pin this conversion was taken from.
        int raw = 0;                  ///< Raw conversion value (chip bit-width dependent).

        /**
         * @brief Compares two conversion results for equality.
         */
        [[nodiscard]] constexpr bool operator==(const sample&) const noexcept = default;
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Constructs a continuous sampler.
     *
     * Resolves each pin's ADC channel, claims ADC1, configures the digital
     * controller for round-robin conversion, and sets up factory calibration
     * when the chip provides it. Sampling does not begin until @ref start.
     *
     * @param config Sampler configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure (e.g. a pin is not ADC1-capable,
     *         the pin list is empty or has duplicates, or the sizes/rate are
     *         invalid).
     */
    [[nodiscard]] explicit sampler(config config);
#endif

    /**
     * @brief Creates a continuous sampler.
     *
     * Resolves each pin's ADC channel, claims ADC1, configures the digital
     * controller for round-robin conversion, and sets up factory calibration
     * when the chip provides it. Sampling does not begin until @ref try_start.
     *
     * @param config Sampler configuration.
     * @return The sampler, or an error.
     * @retval idfxx::errc::invalid_arg The pin list is empty, exceeds the
     *         chip's conversion-pattern length, contains duplicates or
     *         unconnected pins, or a pin is not ADC1-capable; or
     *         `frame_samples` is zero, `buffer_samples` is smaller than
     *         `frame_samples`, or `sample_rate` is not positive.
     */
    [[nodiscard]] static result<sampler> make(config config);

    /**
     * @brief Destroys the sampler, stopping conversion and releasing ADC1.
     */
    ~sampler();

    sampler(const sampler&) = delete;
    sampler& operator=(const sampler&) = delete;
    sampler(sampler&& other) noexcept;
    sampler& operator=(sampler&& other) noexcept;

    /** @brief Returns the configured pins in conversion order. */
    [[nodiscard]] std::span<const idfxx::gpio> pins() const noexcept;

    /** @brief Returns the configured input-range attenuation (uniform across all pins). */
    [[nodiscard]] enum attenuation attenuation() const noexcept;

    /** @brief Returns the configured total sample rate across all pins. */
    [[nodiscard]] freq::hertz sample_rate() const noexcept;

    /** @brief Returns true while the sampler is started. */
    [[nodiscard]] bool running() const noexcept;

    /** @brief Returns true when factory calibration is active for every configured pin. */
    [[nodiscard]] bool calibrated() const noexcept;

    /**
     * @brief Returns the number of dropped-data events since the last start.
     *
     * Increments each time the internal pool overflows because samples were
     * not read fast enough; the samples produced while the pool was full are
     * lost. Reset to zero by @ref start / @ref try_start.
     *
     * @return Cumulative overrun count since the sampler was last started.
     */
    [[nodiscard]] size_t overruns() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Starts continuous conversion.
     *
     * Resets @ref overruns to zero. Samples accumulate in the internal pool
     * from this point and are drained with @ref read.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::invalid_state if already
     *         running, or on driver failure.
     */
    void start() { unwrap(try_start()); }

    /**
     * @brief Stops continuous conversion.
     *
     * Idempotent: stopping a sampler that is not running has no effect.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on driver failure.
     */
    void stop() { unwrap(try_stop()); }
#endif

    /**
     * @brief Starts continuous conversion.
     *
     * Resets @ref overruns to zero. Samples accumulate in the internal pool
     * from this point and are drained with @ref try_read.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state The sampler is already running.
     */
    [[nodiscard]] result<void> try_start();

    /**
     * @brief Stops continuous conversion.
     *
     * Idempotent: stopping a sampler that is not running succeeds with no
     * effect.
     *
     * @return Success, or an error.
     */
    result<void> try_stop();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads samples, blocking until at least one is available.
     *
     * Fills `out` with parsed samples in conversion order, tagging each with
     * its source pin. Returns as soon as at least one sample is available;
     * it does not wait to fill the entire span.
     *
     * @param out Destination for the samples (must not be empty).
     * @return The number of samples written to `out` (always at least 1).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::invalid_state if the
     *         sampler is not running, or idfxx::errc::invalid_arg if `out`
     *         is empty.
     */
    [[nodiscard]] size_t read(std::span<sample> out) { return unwrap(try_read(out)); }

    /**
     * @brief Reads samples, blocking until at least one is available or the timeout expires.
     *
     * Fills `out` with parsed samples in conversion order, tagging each with
     * its source pin. Returns as soon as at least one sample is available;
     * it does not wait to fill the entire span.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param out Destination for the samples (must not be empty).
     * @param timeout Maximum time to wait for a sample.
     * @return The number of samples written to `out` (always at least 1).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::timeout if no sample
     *         arrived within the timeout, or idfxx::errc::invalid_state if
     *         the sampler is not running.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] size_t read(std::span<sample> out, const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_read(out, timeout));
    }

    /**
     * @brief Reads samples as calibrated voltages, blocking until at least one is available.
     *
     * Convenience for the single-pin case: reads and converts in one call, writing calibrated
     * voltages in conversion order. Returns as soon as at least one sample is available; it does
     * not wait to fill the entire span.
     *
     * For a multi-pin sampler the source pin of each voltage cannot be recovered from `out`; read
     * with @ref read(std::span<sample>) and convert with @ref to_voltage to keep the association.
     *
     * @param out Destination for the voltages (must not be empty).
     * @return The number of voltages written to `out` (always at least 1).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::invalid_state if the sampler is not running,
     *         idfxx::errc::invalid_arg if `out` is empty, or idfxx::errc::not_supported if a
     *         sampled pin has no usable calibration data (check @ref calibrated).
     */
    [[nodiscard]] size_t read(std::span<electro::millivolts> out) { return unwrap(try_read(out)); }

    /**
     * @brief Reads samples as calibrated voltages, blocking until at least one is available or the
     *        timeout expires.
     *
     * Convenience for the single-pin case: reads and converts in one call, writing calibrated
     * voltages in conversion order. Returns as soon as at least one sample is available; it does
     * not wait to fill the entire span.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param out Destination for the voltages (must not be empty).
     * @param timeout Maximum time to wait for a sample.
     * @return The number of voltages written to `out` (always at least 1).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::timeout if no sample arrived within the timeout,
     *         idfxx::errc::invalid_state if the sampler is not running, or
     *         idfxx::errc::not_supported if a sampled pin has no usable calibration data.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] size_t read(std::span<electro::millivolts> out, const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_read(out, timeout));
    }
#endif

    /**
     * @brief Reads samples, blocking until at least one is available.
     *
     * Fills `out` with parsed samples in conversion order, tagging each with
     * its source pin. Returns as soon as at least one sample is available;
     * it does not wait to fill the entire span.
     *
     * @param out Destination for the samples (must not be empty).
     * @return The number of samples written to `out` (always at least 1), or
     *         an error.
     * @retval idfxx::errc::invalid_state The sampler is not running.
     * @retval idfxx::errc::invalid_arg `out` is empty.
     */
    [[nodiscard]] result<size_t> try_read(std::span<sample> out) { return _try_read(out, std::nullopt); }

    /**
     * @brief Reads samples, blocking until at least one is available or the timeout expires.
     *
     * Fills `out` with parsed samples in conversion order, tagging each with
     * its source pin. Returns as soon as at least one sample is available;
     * it does not wait to fill the entire span.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param out Destination for the samples (must not be empty).
     * @param timeout Maximum time to wait for a sample.
     * @return The number of samples written to `out` (always at least 1), or
     *         an error.
     * @retval idfxx::errc::timeout No sample arrived within the timeout.
     * @retval idfxx::errc::invalid_state The sampler is not running.
     * @retval idfxx::errc::invalid_arg `out` is empty.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<size_t> try_read(std::span<sample> out, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_read(out, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    /**
     * @brief Reads samples as calibrated voltages, blocking until at least one is available.
     *
     * Convenience for the single-pin case: reads and converts in one call, writing calibrated
     * voltages in conversion order. Returns as soon as at least one sample is available; it does
     * not wait to fill the entire span.
     *
     * For a multi-pin sampler the source pin of each voltage cannot be recovered from `out`; read
     * with @ref try_read(std::span<sample>) and convert with @ref try_to_voltage to keep the
     * association.
     *
     * @param out Destination for the voltages (must not be empty).
     * @return The number of voltages written to `out` (always at least 1), or an error.
     * @retval idfxx::errc::invalid_state The sampler is not running.
     * @retval idfxx::errc::invalid_arg `out` is empty.
     * @retval idfxx::errc::not_supported A sampled pin has no usable calibration data (check
     *         @ref calibrated).
     */
    [[nodiscard]] result<size_t> try_read(std::span<electro::millivolts> out) {
        return _read_voltage(out, std::nullopt);
    }

    /**
     * @brief Reads samples as calibrated voltages, blocking until at least one is available or the
     *        timeout expires.
     *
     * Convenience for the single-pin case: reads and converts in one call, writing calibrated
     * voltages in conversion order. Returns as soon as at least one sample is available; it does
     * not wait to fill the entire span.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param out Destination for the voltages (must not be empty).
     * @param timeout Maximum time to wait for a sample.
     * @return The number of voltages written to `out` (always at least 1), or an error.
     * @retval idfxx::errc::timeout No sample arrived within the timeout.
     * @retval idfxx::errc::invalid_state The sampler is not running.
     * @retval idfxx::errc::not_supported A sampled pin has no usable calibration data.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<size_t>
    try_read(std::span<electro::millivolts> out, const std::chrono::duration<Rep, Period>& timeout) {
        return _read_voltage(out, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Converts a sample to a voltage using factory calibration.
     *
     * @param s A sample previously produced by this sampler.
     * @return The calibrated voltage at the pin.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::invalid_arg if the
     *         sample's pin is not one of this sampler's configured pins, or
     *         idfxx::errc::not_supported if the chip has no usable
     *         calibration data (check @ref calibrated).
     */
    [[nodiscard]] electro::millivolts to_voltage(const sample& s) const { return unwrap(try_to_voltage(s)); }

    /**
     * @brief Converts a raw conversion value from a single-pin sampler to a voltage.
     *
     * Convenience for samplers configured with exactly one pin, where the pin
     * association is unambiguous — converts values computed from raw samples
     * (a minimum, maximum, or mean) without fabricating a @ref sample.
     *
     * @param raw Raw conversion value (as in @ref sample::raw).
     * @return The calibrated voltage at the sampler's pin.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::invalid_state if the sampler
     *         is configured with more than one pin, or
     *         idfxx::errc::not_supported if the chip has no usable calibration
     *         data (check @ref calibrated).
     */
    [[nodiscard]] electro::millivolts to_voltage(int raw) const { return unwrap(try_to_voltage(raw)); }

    /**
     * @brief Converts a batch of samples to voltages using factory calibration.
     *
     * Writes `out[i]` as the calibrated voltage of `in[i]`, preserving order and pin association.
     * Resolves each pin's calibration once per run of like-pinned samples, so converting a whole
     * read is cheaper than calling @ref to_voltage(const sample&) per element.
     *
     * @param in Samples previously produced by this sampler.
     * @param out Destination for the voltages; must be at least as large as `in`.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::invalid_arg if `out` is smaller than `in` or a
     *         sample's pin is not one of this sampler's configured pins, or
     *         idfxx::errc::not_supported if a pin has no usable calibration data (check
     *         @ref calibrated).
     */
    void to_voltage(std::span<const sample> in, std::span<electro::millivolts> out) const {
        unwrap(try_to_voltage(in, out));
    }
#endif

    /**
     * @brief Converts a sample to a voltage using factory calibration.
     *
     * @param s A sample previously produced by this sampler.
     * @return The calibrated voltage at the pin, or an error.
     * @retval idfxx::errc::invalid_arg The sample's pin is not one of this
     *         sampler's configured pins.
     * @retval idfxx::errc::not_supported The chip has no usable calibration
     *         data (check @ref calibrated).
     */
    [[nodiscard]] result<electro::millivolts> try_to_voltage(const sample& s) const;

    /**
     * @brief Converts a raw conversion value from a single-pin sampler to a voltage.
     *
     * Convenience for samplers configured with exactly one pin, where the pin
     * association is unambiguous.
     *
     * @param raw Raw conversion value (as in @ref sample::raw).
     * @return The calibrated voltage at the sampler's pin, or an error.
     * @retval idfxx::errc::invalid_state The sampler is configured with more
     *         than one pin.
     * @retval idfxx::errc::not_supported The chip has no usable calibration
     *         data (check @ref calibrated).
     */
    [[nodiscard]] result<electro::millivolts> try_to_voltage(int raw) const;

    /**
     * @brief Converts a batch of samples to voltages using factory calibration.
     *
     * Writes `out[i]` as the calibrated voltage of `in[i]`, preserving order and pin association.
     *
     * @param in Samples previously produced by this sampler.
     * @param out Destination for the voltages; must be at least as large as `in`.
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg `out` is smaller than `in`, or a sample's pin is not one of
     *         this sampler's configured pins.
     * @retval idfxx::errc::not_supported A pin has no usable calibration data (check
     *         @ref calibrated).
     */
    [[nodiscard]] result<void> try_to_voltage(std::span<const sample> in, std::span<electro::millivolts> out) const;

private:
    /// @cond INTERNAL
    struct state;
    explicit sampler(std::unique_ptr<state> s) noexcept;
    // A nullopt timeout means wait forever.
    [[nodiscard]] result<size_t> _try_read(std::span<sample> out, std::optional<std::chrono::milliseconds> timeout);
    [[nodiscard]] result<size_t>
    _read_voltage(std::span<electro::millivolts> out, std::optional<std::chrono::milliseconds> timeout);
    /// @endcond

    std::unique_ptr<state> _state;
};

/** @} */ // end of idfxx_adc

} // namespace idfxx::adc
