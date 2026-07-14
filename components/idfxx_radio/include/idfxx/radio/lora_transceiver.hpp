// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/radio/lora_transceiver>
 * @file lora_transceiver.hpp
 * @brief Abstract LoRa transceiver interface.
 *
 * @defgroup idfxx_radio Radio Component
 * @brief Abstract LoRa radio transceiver interface and shared types.
 *
 * Provides a chip-agnostic abstract base class (`idfxx::radio::lora_transceiver`) that
 * concrete drivers — `idfxx::radio::sx126x` and future siblings — implement.
 * Application code, higher-layer protocols (LoRaWAN, mesh), and event-loop
 * listeners bind to this interface rather than to any specific chip.
 *
 * Frequency, output power, sync word, and the transmit/receive operations are
 * modulation-agnostic; the LoRa modulation and packet parameters are configured
 * through `set_modulation` and `set_packet_params`.
 *
 * Depends on @ref idfxx_core for error handling; the typed events drivers
 * post are declared in `<idfxx/radio/events>`.
 * @{
 */

#include <idfxx/error>
#include <idfxx/future>
#include <idfxx/radio/airtime.hpp>
#include <idfxx/radio/duty_cycle.hpp>
#include <idfxx/radio/types.hpp>

#include <chrono>
#include <cstdint>
#include <electro/decibel>
#include <frequency/frequency>
#include <optional>
#include <span>

namespace idfxx::radio {

/**
 * @headerfile <idfxx/radio/lora_transceiver>
 * @brief Abstract base class for LoRa radio transceivers.
 *
 * The public interface is non-virtual; concrete drivers (e.g.
 * `idfxx::radio::sx126x`) customize behaviour by overriding the protected
 * `do_*` hooks, mirroring the standard library's non-virtual-interface
 * pattern (cf. `std::pmr::memory_resource`).
 *
 * Three coordinated styles are exposed: blocking calls (`transmit`/`receive`/
 * `scan_channel`), future-based one-shot operations (`start_transmit`,
 * single-shot `start_receive(buffer)`, and `start_channel_scan`, each
 * returning an `idfxx::future`), and event-loop dispatch for packet streams
 * (continuous and duty-cycled `start_listening`, surfacing packets via the
 * typed events declared in `<idfxx/radio/events>`; pair them with
 * `read_received`). Futures and events coexist: one-shot operations also post
 * their completion event when an event loop is configured.
 */
class lora_transceiver {
public:
    virtual ~lora_transceiver() = default;

    lora_transceiver(const lora_transceiver&) = delete;
    lora_transceiver& operator=(const lora_transceiver&) = delete;

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Returns the radio's current high-level mode.
     *
     * @return The current @ref chip_mode.
     */
    [[nodiscard]] chip_mode current_mode() const noexcept { return do_current_mode(); }

    /**
     * @brief Returns the configured LoRa modulation parameters.
     *
     * Reflects the most recent successful @ref try_set_modulation /
     * @ref set_modulation, or the defaults (`lora_modulation{}`) before
     * any call. Used by @ref time_on_air and @ref rx_duty_cycle_for so
     * callers need not keep their own copy of the link configuration.
     *
     * @return The configured modulation parameters.
     */
    [[nodiscard]] lora_modulation modulation() const noexcept { return _modulation; }

    /**
     * @brief Returns the configured LoRa packet framing parameters.
     *
     * Reflects the most recent successful @ref try_set_packet_params /
     * @ref set_packet_params, or the defaults (`lora_packet_params{}`)
     * before any call.
     *
     * @return The configured packet framing parameters.
     */
    [[nodiscard]] lora_packet_params packet_params() const noexcept { return _packet_params; }

    // =========================================================================
    // Link calculations
    // =========================================================================

    /**
     * @brief Computes the time-on-air of a packet under the configured link parameters.
     *
     * Equivalent to the free function @ref idfxx::radio::time_on_air called
     * with @ref modulation and @ref packet_params, so it always agrees with
     * what the radio was actually configured with.
     *
     * @param payload_length Number of payload bytes the packet carries.
     * @return The packet's time-on-air.
     *
     * @code
     * radio.transmit(payload, radio.time_on_air(payload.size()) + 200ms);
     * @endcode
     */
    [[nodiscard]] std::chrono::microseconds time_on_air(size_t payload_length) const noexcept {
        return idfxx::radio::time_on_air(_modulation, _packet_params, payload_length);
    }

    /**
     * @brief Computes duty-cycle receive windows for the configured link parameters.
     *
     * Equivalent to the free function @ref idfxx::radio::rx_duty_cycle_for
     * called with @ref modulation, the configured preamble length (senders on
     * the link are assumed to use the same @ref packet_params), and the
     * driver's minimum worthwhile sleep window — which includes any oscillator
     * start-up cost the chip pays on each wake (e.g. the SX126x TCXO delay).
     *
     * Returns `std::nullopt` when duty-cycling cannot reliably catch packets;
     * passing the result straight to @ref start_listening then falls back to
     * continuous receive.
     *
     * @param min_symbols Minimum preamble symbols the radio must observe to
     *                    detect a packet; must be at least 1.
     * @return The listen/sleep windows, or `std::nullopt` if duty-cycling
     *         cannot reliably catch packets.
     *
     * @code
     * radio.start_listening(radio.rx_duty_cycle_for());
     * @endcode
     */
    [[nodiscard]] std::optional<rx_duty_cycle> rx_duty_cycle_for(uint16_t min_symbols = 8) const noexcept {
        return idfxx::radio::rx_duty_cycle_for(
            _modulation, _packet_params.preamble_length, min_symbols, do_rx_duty_cycle_min_sleep()
        );
    }

    // =========================================================================
    // Mode control
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Puts the radio in standby mode.
     *
     * Standby stops any in-progress receive or channel scan and parks the
     * radio with its configuration retained, ready for the next operation.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void standby() { unwrap(try_standby()); }

    /**
     * @brief Puts the radio in its lowest-power sleep mode.
     *
     * Configuration is retained where the chip supports it, so the next
     * operation wakes the radio without a full reconfiguration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void sleep() { unwrap(try_sleep()); }

    /**
     * @brief Starts continuous-receive mode.
     *
     * The radio runs continuously, posting `rx_done` (and on errors,
     * `crc_error`) events as packets arrive. Use @ref read_received to
     * extract the payload from inside the event handler. Call @ref standby
     * to stop receiving.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void start_listening() { unwrap(try_start_listening()); }

    /**
     * @brief Starts duty-cycled (periodic) receive for low-power listening.
     *
     * The radio alternates between listening for @p rx_period and sleeping
     * for @p sleep_period, repeating until a packet arrives or the mode is
     * changed. As with the continuous overload, packets surface through
     * `rx_done` (and `crc_error`) events; use @ref read_received to extract
     * the payload.
     *
     * @param rx_period    Time to listen in each cycle.
     * @param sleep_period Time to sleep in each cycle.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg` for a
     *         non-positive @p rx_period or @p sleep_period.
     */
    void start_listening(std::chrono::microseconds rx_period, std::chrono::microseconds sleep_period) {
        unwrap(try_start_listening(rx_period, sleep_period));
    }

    /**
     * @brief Starts duty-cycled (periodic) receive from precomputed windows.
     *
     * Equivalent to `start_listening(cycle.rx_period, cycle.sleep_period)`.
     * Compute the windows with @ref rx_duty_cycle_for
     * (`<idfxx/radio/duty_cycle>`) or build them directly.
     *
     * @param cycle Listen/sleep windows for each cycle.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg` for a
     *         non-positive listen or sleep window.
     */
    void start_listening(rx_duty_cycle cycle) { unwrap(try_start_listening(cycle)); }

    /**
     * @brief Starts duty-cycled receive, falling back to continuous receive.
     *
     * Accepts the result of @ref rx_duty_cycle_for directly: starts
     * duty-cycled receive when @p cycle holds windows, and continuous receive
     * when it is `std::nullopt` (duty-cycling could not reliably catch
     * packets for the link's modulation and preamble length).
     *
     * @param cycle Listen/sleep windows, or `std::nullopt` for continuous receive.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     *
     * @code
     * radio.start_listening(idfxx::radio::rx_duty_cycle_for(mod, pkt.preamble_length));
     * @endcode
     */
    void start_listening(const std::optional<rx_duty_cycle>& cycle) { unwrap(try_start_listening(cycle)); }

    /**
     * @brief Starts a one-shot channel-activity scan and returns a future.
     *
     * The returned future completes with the scan result (@ref cad_info);
     * when an event loop is configured the result is also posted as a
     * `cad_done` event. Uses driver-default detection parameters; drivers may
     * expose a chip-specific method to tune those (e.g.
     * `sx126x::set_cad_params`). For a blocking scan, use @ref scan_channel
     * instead.
     *
     * Dropping the future without waiting is safe: the scan continues and
     * the result remains observable through the `cad_done` event.
     *
     * @return A future completing with the scan result.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] idfxx::future<cad_info> start_channel_scan() { return unwrap(try_start_channel_scan()); }

    /**
     * @brief Scans the channel for LoRa activity, blocking until the result is known.
     *
     * Runs one channel-activity-detection operation with driver-default
     * detection parameters and blocks until it completes, returning whether
     * activity was detected. Intended for listen-before-talk: scan, then
     * transmit only if the channel is clear.
     *
     * @return The scan result (whether activity was detected).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::timeout` if the
     *         scan does not complete within @ref scan_guard_window.
     */
    [[nodiscard]] cad_info scan_channel() { return unwrap(try_scan_channel()); }

    /**
     * @brief Returns whether LoRa activity is currently detected on the channel.
     *
     * Convenience over @ref scan_channel for listen-before-talk: scan, then
     * transmit only if the channel is clear.
     *
     * @return true if LoRa activity was detected on the channel.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::timeout` if the
     *         scan does not complete within @ref scan_guard_window.
     *
     * @code
     * if (radio.channel_busy()) {
     *     // defer the transmission
     * }
     * @endcode
     */
    [[nodiscard]] bool channel_busy() { return unwrap(try_channel_busy()); }
#endif

    /**
     * @brief Puts the radio in standby mode.
     *
     * Standby stops any in-progress receive or channel scan and parks the
     * radio with its configuration retained, ready for the next operation.
     *
     * @return Success, or an error.
     */
    result<void> try_standby() { return do_standby(); }

    /**
     * @brief Puts the radio in its lowest-power sleep mode.
     *
     * Configuration is retained where the chip supports it, so the next
     * operation wakes the radio without a full reconfiguration.
     *
     * @return Success, or an error.
     */
    result<void> try_sleep() { return do_sleep(); }

    /**
     * @brief Starts continuous-receive mode.
     *
     * Packets surface through `rx_done` (and `crc_error`) events; use
     * @ref try_read_received to extract the payload. Call @ref try_standby
     * to stop receiving.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_start_listening() { return do_start_listening(); }

    /**
     * @brief Starts duty-cycled (periodic) receive for low-power listening.
     * @param rx_period    Time to listen in each cycle.
     * @param sleep_period Time to sleep in each cycle.
     * @return Success, or an error.
     * @retval invalid_arg @p rx_period or @p sleep_period is not positive.
     * @retval not_supported The driver has no hardware duty-cycled listening.
     */
    [[nodiscard]] result<void>
    try_start_listening(std::chrono::microseconds rx_period, std::chrono::microseconds sleep_period) {
        if (rx_period <= std::chrono::microseconds::zero() || sleep_period <= std::chrono::microseconds::zero()) {
            return error(errc::invalid_arg);
        }
        return do_start_listening(rx_period, sleep_period);
    }

    /**
     * @brief Starts duty-cycled (periodic) receive from precomputed windows.
     *
     * Equivalent to `try_start_listening(cycle.rx_period, cycle.sleep_period)`.
     *
     * @param cycle Listen/sleep windows for each cycle.
     * @return Success, or an error.
     * @retval invalid_arg The listen or sleep window is not positive.
     * @retval not_supported The driver has no hardware duty-cycled listening.
     */
    [[nodiscard]] result<void> try_start_listening(rx_duty_cycle cycle) {
        return try_start_listening(cycle.rx_period, cycle.sleep_period);
    }

    /**
     * @brief Starts duty-cycled receive, falling back to continuous receive.
     *
     * Accepts the result of @ref rx_duty_cycle_for directly: starts
     * duty-cycled receive when @p cycle holds windows, and continuous receive
     * when it is `std::nullopt`.
     *
     * @param cycle Listen/sleep windows, or `std::nullopt` for continuous receive.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_start_listening(const std::optional<rx_duty_cycle>& cycle) {
        return cycle ? try_start_listening(*cycle) : try_start_listening();
    }

    /**
     * @brief Starts a one-shot channel-activity scan and returns a future.
     *
     * The returned future completes with the scan result (@ref cad_info);
     * when an event loop is configured the result is also posted as a
     * `cad_done` event. The future completes with `errc::not_finished` if the
     * scan is cancelled by a mode change (@ref try_standby or @ref try_sleep).
     *
     * Dropping the future without waiting is safe: the scan continues and
     * the result remains observable through the `cad_done` event.
     *
     * @return A future completing with the scan result, or an error.
     * @retval invalid_state Another transmit, receive, or scan is already in flight.
     */
    [[nodiscard]] result<idfxx::future<cad_info>> try_start_channel_scan() { return do_start_channel_scan(); }

    /**
     * @brief Scans the channel for LoRa activity, blocking until the result is known.
     *
     * Equivalent to @ref try_start_channel_scan followed by a wait on the
     * returned future. If the scan does not complete within
     * @ref scan_guard_window the radio is returned to standby and
     * `errc::timeout` is reported.
     *
     * @return The scan result, or an error.
     * @retval timeout The scan did not complete within @ref scan_guard_window.
     * @retval invalid_state Another transmit, receive, or scan is already in flight.
     */
    [[nodiscard]] result<cad_info> try_scan_channel() {
        auto f = try_start_channel_scan();
        if (!f) {
            return error(f.error());
        }
        return _await(*f, scan_guard_window);
    }

    /**
     * @brief Returns whether LoRa activity is currently detected on the channel.
     *
     * Convenience over @ref try_scan_channel for listen-before-talk: scan,
     * then transmit only if the channel is clear.
     *
     * @return true if LoRa activity was detected on the channel, or an error.
     * @retval timeout The scan did not complete within @ref scan_guard_window.
     * @retval invalid_state Another transmit, receive, or scan is already in flight.
     */
    [[nodiscard]] result<bool> try_channel_busy() {
        auto r = try_scan_channel();
        if (!r) {
            return error(r.error());
        }
        return r->detected;
    }

    // =========================================================================
    // Configuration
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Applies a complete link configuration.
     *
     * Sets the frequency, output power, modulation, packet framing, and
     * network (sync word) in one call — equivalent to calling the individual
     * setters in sequence. Both ends of a link must agree on every field
     * except the output power.
     *
     * @param link Link configuration; `link.frequency` is required.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg` if
     *         `link.frequency` is unset (zero).
     *
     * @code
     * radio.configure({
     *     .frequency = 915_MHz,
     *     .output_power = 14_dBm,
     *     .modulation = {.sf = idfxx::radio::spreading_factor::sf9},
     * });
     * @endcode
     */
    void configure(const lora_link& link) { unwrap(try_configure(link)); }

    /**
     * @brief Sets the RF carrier frequency.
     * @param hz Carrier frequency.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_frequency(freq::hertz hz) { unwrap(try_set_frequency(hz)); }

    /**
     * @brief Sets the transmit output power.
     * @param power Output power. Valid range depends on the chip variant;
     *              drivers return `errc::invalid_arg` for values outside
     *              their supported range.
     * @param ramp  Output-power ramp-up time (driver maps to nearest supported value).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_output_power(electro::dbm power, ramp_time ramp = ramp_time::us_200) {
        unwrap(try_set_output_power(power, ramp));
    }

    /**
     * @brief Configures the LoRa modulation parameters.
     * @param mod Spreading factor, bandwidth, coding rate.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_modulation(lora_modulation mod) { unwrap(try_set_modulation(mod)); }

    /**
     * @brief Configures the LoRa packet framing.
     * @param params Preamble, header type, CRC, IQ inversion, etc.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_packet_params(lora_packet_params params) { unwrap(try_set_packet_params(params)); }

    /**
     * @brief Selects the LoRa network by setting the sync word.
     *
     * Senders and receivers must select the same network to hear each other.
     * Each driver maps the selection to its chip's native sync-word encoding;
     * chip-specific raw values remain available through driver-specific
     * overloads (e.g. `sx126x::set_sync_word(uint16_t)`).
     *
     * @param network Network whose sync word to use.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_sync_word(lora_network network) { unwrap(try_set_sync_word(network)); }
#endif

    /**
     * @brief Applies a complete link configuration.
     *
     * Sets the frequency, output power, modulation, packet framing, and
     * network (sync word) in one call — equivalent to calling the individual
     * setters in sequence, stopping at the first failure. Both ends of a link
     * must agree on every field except the output power.
     *
     * @param link Link configuration; `link.frequency` is required.
     * @return Success, or the first setter's error.
     * @retval invalid_arg `link.frequency` is unset (zero), or a field is
     *         outside the driver's supported range.
     */
    [[nodiscard]] result<void> try_configure(const lora_link& link) {
        if (link.frequency.count() == 0) {
            return error(errc::invalid_arg);
        }
        if (auto r = try_set_frequency(link.frequency); !r) {
            return r;
        }
        if (auto r = try_set_output_power(link.output_power, link.ramp); !r) {
            return r;
        }
        if (auto r = try_set_modulation(link.modulation); !r) {
            return r;
        }
        if (auto r = try_set_packet_params(link.packet_params); !r) {
            return r;
        }
        return try_set_sync_word(link.network);
    }

    /**
     * @brief Sets the RF carrier frequency.
     * @param hz Carrier frequency.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_frequency(freq::hertz hz) { return do_set_frequency(hz); }

    /**
     * @brief Sets the transmit output power.
     * @param power Output power.
     * @param ramp  Output-power ramp-up time.
     * @return Success, or an error.
     * @retval invalid_arg The requested power is outside the chip variant's supported range.
     */
    [[nodiscard]] result<void> try_set_output_power(electro::dbm power, ramp_time ramp = ramp_time::us_200) {
        return do_set_output_power(power, ramp);
    }

    /**
     * @brief Configures the LoRa modulation parameters.
     * @param mod Spreading factor, bandwidth, coding rate.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_modulation(lora_modulation mod) {
        if (auto r = do_set_modulation(mod); !r) {
            return r;
        }
        _modulation = mod;
        return {};
    }

    /**
     * @brief Configures the LoRa packet framing.
     * @param params Preamble, header type, CRC, IQ inversion, etc.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_packet_params(lora_packet_params params) {
        if (auto r = do_set_packet_params(params); !r) {
            return r;
        }
        _packet_params = params;
        return {};
    }

    /**
     * @brief Selects the LoRa network by setting the sync word.
     *
     * Senders and receivers must select the same network to hear each other.
     * Each driver maps the selection to its chip's native sync-word encoding.
     *
     * @param network Network whose sync word to use.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_sync_word(lora_network network) { return do_set_sync_word(network); }

    // =========================================================================
    // Data path: blocking
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Transmits a packet and blocks until completion, sizing the timeout automatically.
     *
     * Waits at most the packet's @ref time_on_air — computed from the
     * configured link parameters — plus @ref transmit_timeout_margin.
     *
     * @param data Payload to transmit (1–255 bytes).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure or timeout.
     */
    void transmit(std::span<const uint8_t> data) { unwrap(try_transmit(data)); }

    /**
     * @brief Transmits a packet and blocks until completion.
     * @param data    Payload to transmit (1–255 bytes).
     * @param timeout Maximum time to wait for transmit completion.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure or timeout.
     */
    template<typename Rep, typename Period>
    void transmit(std::span<const uint8_t> data, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_transmit(data, timeout));
    }

    /**
     * @brief Receives a single packet, blocking until one arrives or the timeout expires.
     *
     * @param buffer  Buffer to receive into.
     * @param timeout Maximum time to wait for a packet.
     * @return Information about the received packet (length, RSSI, SNR).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::timeout` and `errc::invalid_crc`.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] rx_info receive(std::span<uint8_t> buffer, const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_receive(buffer, timeout));
    }
#endif

    /**
     * @brief Transmits a packet and blocks until completion, sizing the timeout automatically.
     *
     * Waits at most the packet's @ref time_on_air — computed from the
     * configured link parameters — plus @ref transmit_timeout_margin.
     *
     * @param data Payload to transmit (1–255 bytes).
     * @return Success, or an error.
     * @retval invalid_arg The payload is empty or longer than 255 bytes.
     * @retval invalid_state Another transmit, receive, or scan is already in flight.
     * @retval timeout The transmit did not complete in time.
     */
    [[nodiscard]] result<void> try_transmit(std::span<const uint8_t> data) {
        return try_transmit(data, time_on_air(data.size()) + transmit_timeout_margin);
    }

    /**
     * @brief Transmits a packet and blocks until completion.
     *
     * Equivalent to @ref try_start_transmit followed by a wait on the
     * returned future. If the transmit does not complete within @p timeout
     * the radio is returned to standby and `errc::timeout` is reported.
     *
     * @param data    Payload to transmit (1–255 bytes).
     * @param timeout Maximum time to wait for transmit completion.
     * @return Success, or an error.
     * @retval invalid_arg The payload is empty or longer than 255 bytes.
     * @retval invalid_state Another transmit, receive, or scan is already in flight.
     * @retval timeout The transmit did not complete within the specified duration.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_transmit(std::span<const uint8_t> data, const std::chrono::duration<Rep, Period>& timeout) {
        auto f = try_start_transmit(data);
        if (!f) {
            return error(f.error());
        }
        return _await(*f, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    /**
     * @brief Receives a single packet, blocking until one arrives or the timeout expires.
     *
     * Equivalent to @ref try_start_receive(std::span<uint8_t>) followed by a
     * wait on the returned future. If no packet arrives within @p timeout the
     * radio is returned to standby and `errc::timeout` is reported.
     *
     * @param buffer  Buffer to receive into.
     * @param timeout Maximum time to wait for a packet.
     * @return Information about the received packet, or an error.
     * @retval timeout No packet arrived within the timeout.
     * @retval invalid_crc The received packet failed its CRC check.
     * @retval invalid_state Another transmit, receive, or scan is already in flight.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<rx_info>
    try_receive(std::span<uint8_t> buffer, const std::chrono::duration<Rep, Period>& timeout) {
        auto f = try_start_receive(buffer);
        if (!f) {
            return error(f.error());
        }
        return _await(*f, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    // =========================================================================
    // Data path: async
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Starts a transmit and returns a future tracking its completion.
     *
     * The companion to the blocking @ref transmit: the call returns as soon
     * as the packet is staged and transmission has started, so the caller's
     * task is free to do other work and can await the returned future when it
     * needs the result. When an event loop is configured, completion is also
     * posted as a `tx_done` event.
     *
     * Only one data-path operation may be in flight at a time; calling this
     * (or any other transmit/receive/scan) again before completion returns
     * `errc::invalid_state`. There is no built-in transmit timeout — wait on
     * the future with a `try_wait_for()` sized via @ref time_on_air and call
     * @ref standby to cancel the operation if it never completes.
     *
     * Dropping the future without waiting is safe: the transmission continues
     * and completion remains observable through the `tx_done` event.
     *
     * @param data Payload to transmit (1–255 bytes). The payload is staged
     *             on-chip before this call returns, so the buffer need not
     *             outlive the call.
     * @return A future completing when the packet has been sent.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] idfxx::future<void> start_transmit(std::span<const uint8_t> data) {
        return unwrap(try_start_transmit(data));
    }

    /**
     * @brief Starts a single-shot receive into the caller's buffer.
     *
     * Receives exactly one packet: the radio listens until a packet arrives,
     * copies the payload into @p buffer (clamped to its size), completes the
     * returned future with the packet's @ref rx_info, and returns to standby.
     * For a packet stream, use the continuous or duty-cycled
     * @ref start_listening overloads instead.
     *
     * @p buffer must remain valid until the future completes or the
     * operation is cancelled by a mode change (@ref standby or @ref sleep).
     *
     * Dropping the future without waiting is safe: the receive continues and
     * the packet remains observable through the `rx_done` event and
     * @ref read_received.
     *
     * @param buffer Buffer to receive the payload into.
     * @return A future completing with information about the received packet.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] idfxx::future<rx_info> start_receive(std::span<uint8_t> buffer) {
        return unwrap(try_start_receive(buffer));
    }

    /**
     * @brief Reads the most recently received packet into the caller's buffer.
     *
     * Typically called from a `rx_done` event handler. The radio's internal
     * cache holds only the most recent packet; calling this after a second
     * `rx_done` returns that newer packet. Always pair the copied bytes with
     * the @ref rx_info this call returns — the `rx_done` event's payload may
     * describe an older packet than the cache.
     *
     * @param buffer Buffer to copy the payload into.
     * @return Information about the packet (length, RSSI, SNR).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] rx_info read_received(std::span<uint8_t> buffer) { return unwrap(try_read_received(buffer)); }
#endif

    /**
     * @brief Starts a transmit and returns a future tracking its completion.
     *
     * The call returns as soon as the packet is staged and transmission has
     * started; the returned future completes when the packet has been sent
     * (or with `errc::not_finished` if the operation is cancelled by a mode
     * change). When an event loop is configured, completion is also posted as
     * a `tx_done` event. Only one data-path operation may be in flight at a
     * time.
     *
     * Dropping the future without waiting is safe: the transmission continues
     * and completion remains observable through the `tx_done` event.
     *
     * @param data Payload to transmit (1–255 bytes). The payload is staged
     *             on-chip before this call returns, so the buffer need not
     *             outlive the call.
     * @return A future completing when the packet has been sent, or an error.
     * @retval invalid_arg The payload is empty or longer than 255 bytes.
     * @retval invalid_state Another transmit, receive, or scan is already in flight.
     */
    [[nodiscard]] result<idfxx::future<void>> try_start_transmit(std::span<const uint8_t> data) {
        if (data.empty() || data.size() > max_payload_length) {
            return error(errc::invalid_arg);
        }
        return do_start_transmit(data);
    }

    /**
     * @brief Starts a single-shot receive into the caller's buffer.
     *
     * Receives exactly one packet: the radio listens until a packet arrives,
     * copies the payload into @p buffer (clamped to its size), completes the
     * returned future with the packet's @ref rx_info, and returns to standby.
     * The future completes with `errc::invalid_crc` if the packet failed its
     * CRC check, and with `errc::not_finished` if the operation is cancelled
     * by a mode change (@ref try_standby or @ref try_sleep).
     *
     * @p buffer must remain valid until the future completes or the
     * operation is cancelled.
     *
     * Dropping the future without waiting is safe: the receive continues and
     * the packet remains observable through the `rx_done` event and
     * @ref try_read_received.
     *
     * @param buffer Buffer to receive the payload into.
     * @return A future completing with information about the received packet,
     *         or an error.
     * @retval invalid_state Another transmit, receive, or scan is already in flight.
     */
    [[nodiscard]] result<idfxx::future<rx_info>> try_start_receive(std::span<uint8_t> buffer) {
        return do_start_receive(buffer);
    }

    /**
     * @brief Reads the most recently received packet into the caller's buffer.
     *
     * Always pair the copied bytes with the @ref rx_info this call returns —
     * the `rx_done` event's payload may describe an older packet than the
     * cache.
     *
     * @param buffer Buffer to copy the payload into.
     * @return Information about the packet, or an error.
     * @retval not_found No packet has been received yet.
     */
    [[nodiscard]] result<rx_info> try_read_received(std::span<uint8_t> buffer) { return do_read_received(buffer); }

    // =========================================================================
    // Status
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns detailed status for the most recent packet.
     * @return Packet status (RSSI, SNR, signal RSSI).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] packet_status last_packet_status() { return unwrap(try_last_packet_status()); }

    /**
     * @brief Returns the instantaneous RSSI on the configured channel.
     * @return The RSSI.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] electro::centi_dbm current_rssi() { return unwrap(try_current_rssi()); }
#endif

    /**
     * @brief Returns detailed status for the most recent packet.
     * @return Packet status, or an error.
     */
    [[nodiscard]] result<packet_status> try_last_packet_status() { return do_last_packet_status(); }

    /**
     * @brief Returns the instantaneous RSSI on the configured channel.
     * @return The RSSI, or an error.
     */
    [[nodiscard]] result<electro::centi_dbm> try_current_rssi() { return do_current_rssi(); }

    /// Maximum LoRa payload length in bytes.
    static constexpr size_t max_payload_length = 255;

    /// Margin added to the packet's time-on-air when the blocking @ref transmit
    /// overload without a timeout sizes its own wait: covers command staging,
    /// PA ramp-up, and scheduling latency on top of the pure air-time.
    static constexpr std::chrono::milliseconds transmit_timeout_margin{250};

    /// Guard window for the blocking @ref scan_channel. Channel-activity
    /// detection completes within a few symbol times even at the slowest
    /// modulation, so it takes no caller-supplied timeout; the blocking form
    /// waits at most this long before cancelling the scan and reporting
    /// `errc::timeout`.
    static constexpr std::chrono::milliseconds scan_guard_window{1000};

protected:
    lora_transceiver() = default;
    lora_transceiver(lora_transceiver&&) noexcept = default;
    lora_transceiver& operator=(lora_transceiver&&) noexcept = default;

    // =========================================================================
    // Customization hooks
    //
    // Concrete drivers override these (typically privately). Each hook
    // implements the correspondingly named public method; argument validation
    // common to all chips (payload length, positive durations) has already
    // been performed by the public wrappers.
    // =========================================================================

    /// Hook for @ref current_mode.
    [[nodiscard]] virtual chip_mode do_current_mode() const noexcept = 0;

    /// Hook for @ref try_standby.
    [[nodiscard]] virtual result<void> do_standby() = 0;
    /// Hook for @ref try_sleep.
    [[nodiscard]] virtual result<void> do_sleep() = 0;
    /// Hook for @ref try_start_listening() (continuous receive).
    [[nodiscard]] virtual result<void> do_start_listening() = 0;
    /// Hook for @ref try_start_listening(std::chrono::microseconds, std::chrono::microseconds)
    /// (duty-cycled receive). Both durations are positive. Hardware
    /// duty-cycled listening is optional: drivers whose chip cannot listen
    /// autonomously keep this default, which reports `errc::not_supported`.
    [[nodiscard]] virtual result<void>
    do_start_listening(std::chrono::microseconds rx_period, std::chrono::microseconds sleep_period) {
        (void)rx_period;
        (void)sleep_period;
        return error(errc::not_supported);
    }
    /// Hook for @ref try_start_channel_scan. The returned future must
    /// complete with the scan result, or with `errc::not_finished` if the
    /// operation is cancelled (standby, sleep, or driver teardown).
    [[nodiscard]] virtual result<idfxx::future<cad_info>> do_start_channel_scan() = 0;

    /// Hook for @ref try_set_frequency.
    [[nodiscard]] virtual result<void> do_set_frequency(freq::hertz hz) = 0;
    /// Hook for @ref try_set_output_power.
    [[nodiscard]] virtual result<void> do_set_output_power(electro::dbm power, ramp_time ramp) = 0;
    /// Hook for @ref try_set_modulation.
    [[nodiscard]] virtual result<void> do_set_modulation(lora_modulation mod) = 0;
    /// Hook for @ref try_set_packet_params.
    [[nodiscard]] virtual result<void> do_set_packet_params(lora_packet_params params) = 0;
    /// Hook for @ref try_set_sync_word.
    [[nodiscard]] virtual result<void> do_set_sync_word(lora_network network) = 0;

    /// Hook for @ref try_start_transmit. The payload is non-empty and at most
    /// @ref max_payload_length bytes, and is staged before the hook returns
    /// (the caller's buffer need not outlive the call). The returned future
    /// must complete when the packet has been sent, or with
    /// `errc::not_finished` if the operation is cancelled (standby, sleep, or
    /// driver teardown).
    [[nodiscard]] virtual result<idfxx::future<void>> do_start_transmit(std::span<const uint8_t> data) = 0;
    /// Hook for @ref try_start_receive(std::span<uint8_t>) (single-shot
    /// receive). The returned future must complete with the received packet's
    /// info (`errc::invalid_crc` on CRC failure), or with
    /// `errc::not_finished` if the operation is cancelled (standby, sleep, or
    /// driver teardown); the buffer must never be written after the future
    /// completes.
    [[nodiscard]] virtual result<idfxx::future<rx_info>> do_start_receive(std::span<uint8_t> buffer) = 0;
    /// Hook for @ref try_read_received.
    [[nodiscard]] virtual result<rx_info> do_read_received(std::span<uint8_t> buffer) = 0;

    /// Hook for @ref try_last_packet_status.
    [[nodiscard]] virtual result<packet_status> do_last_packet_status() = 0;
    /// Hook for @ref try_current_rssi.
    [[nodiscard]] virtual result<electro::centi_dbm> do_current_rssi() = 0;

    /// Hook for @ref rx_duty_cycle_for: the shortest sleep window worth
    /// duty-cycling for on this chip. Drivers add any per-wake oscillator
    /// start-up cost to the generic default (e.g. the SX126x adds its
    /// configured TCXO start-up delay).
    [[nodiscard]] virtual std::chrono::microseconds do_rx_duty_cycle_min_sleep() const noexcept {
        return default_min_rx_sleep;
    }

private:
    // Link parameters applied by the last successful try_set_modulation /
    // try_set_packet_params; served back by modulation()/packet_params()
    // and consumed by the time_on_air/rx_duty_cycle_for members.
    lora_modulation _modulation{};
    lora_packet_params _packet_params{};

    // Shared tail of the blocking compositions (try_transmit / try_receive /
    // try_scan_channel): waits for the operation's future, cancelling the
    // operation via try_standby if it does not complete in time and masking
    // the resulting cancellation error back to `errc::timeout`.
    template<typename T>
    [[nodiscard]] result<T> _await(const idfxx::future<T>& f, std::chrono::milliseconds timeout) {
        auto r = f.try_wait_for(timeout);
        if (r || r.error() != errc::timeout) {
            return r;
        }
        // The completion IRQ may land between the wait timing out and the
        // cancel below; only cancel an operation that is still pending.
        if (f.done()) {
            return f.try_wait();
        }
        (void)try_standby();
        auto late = f.try_wait_for(std::chrono::milliseconds{0});
        if (late || late.error() != errc::not_finished) {
            return late;
        }
        return error(errc::timeout); // our own cancellation, reported as a timeout
    }
};

} // namespace idfxx::radio

/** @} */ // end of idfxx_radio
