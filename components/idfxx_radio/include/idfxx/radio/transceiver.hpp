// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/radio/transceiver>
 * @file transceiver.hpp
 * @brief Abstract radio transceiver interface.
 *
 * @defgroup idfxx_radio Radio Component
 * @brief Abstract radio transceiver interface and shared types.
 *
 * Provides a chip-agnostic abstract base class (`idfxx::radio::transceiver`) that
 * concrete drivers — `idfxx::radio::sx126x` and future siblings — implement.
 * Application code, higher-layer protocols (LoRaWAN, mesh), and event-loop
 * listeners bind to this interface rather than to any specific chip.
 *
 * Frequency, output power, sync word, and the transmit/receive operations are
 * modulation-agnostic; the LoRa modulation and packet parameters are configured
 * through `set_lora_modulation` and `set_lora_packet_params`.
 *
 * Depends on @ref idfxx_core for error handling; the typed events drivers
 * post are declared in `<idfxx/radio/events>`.
 * @{
 */

#include <idfxx/error>
#include <idfxx/future>
#include <idfxx/radio/types.hpp>

#include <chrono>
#include <cstdint>
#include <frequency/frequency>
#include <optional>
#include <span>

namespace idfxx::radio {

/**
 * @headerfile <idfxx/radio/transceiver>
 * @brief Abstract base class for radio transceivers.
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
 * (continuous and duty-cycled `start_receive`, surfacing packets via the
 * typed events declared in `<idfxx/radio/events>`; pair them with
 * `read_received`). Futures and events coexist: one-shot operations also post
 * their completion event when an event loop is configured.
 */
class transceiver {
public:
    virtual ~transceiver() = default;

    transceiver(const transceiver&) = delete;
    transceiver& operator=(const transceiver&) = delete;

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Returns the radio's current high-level mode.
     *
     * @return The current @ref chip_mode.
     */
    [[nodiscard]] chip_mode current_mode() const noexcept { return do_current_mode(); }

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
    void start_receive() { unwrap(try_start_receive()); }

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
    void start_receive(std::chrono::microseconds rx_period, std::chrono::microseconds sleep_period) {
        unwrap(try_start_receive(rx_period, sleep_period));
    }

    /**
     * @brief Starts duty-cycled (periodic) receive from precomputed windows.
     *
     * Equivalent to `start_receive(cycle.rx_period, cycle.sleep_period)`.
     * Compute the windows with @ref rx_duty_cycle_for
     * (`<idfxx/radio/duty_cycle>`) or build them directly.
     *
     * @param cycle Listen/sleep windows for each cycle.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg` for a
     *         non-positive listen or sleep window.
     */
    void start_receive(rx_duty_cycle cycle) { unwrap(try_start_receive(cycle)); }

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
     * radio.start_receive(idfxx::radio::rx_duty_cycle_for(mod, pkt.preamble_length));
     * @endcode
     */
    void start_receive(const std::optional<rx_duty_cycle>& cycle) { unwrap(try_start_receive(cycle)); }

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
    [[nodiscard]] result<void> try_start_receive() { return do_start_receive(); }

    /**
     * @brief Starts duty-cycled (periodic) receive for low-power listening.
     * @param rx_period    Time to listen in each cycle.
     * @param sleep_period Time to sleep in each cycle.
     * @return Success, or an error.
     * @retval invalid_arg @p rx_period or @p sleep_period is not positive.
     */
    [[nodiscard]] result<void>
    try_start_receive(std::chrono::microseconds rx_period, std::chrono::microseconds sleep_period) {
        if (rx_period <= std::chrono::microseconds::zero() || sleep_period <= std::chrono::microseconds::zero()) {
            return error(errc::invalid_arg);
        }
        return do_start_receive(rx_period, sleep_period);
    }

    /**
     * @brief Starts duty-cycled (periodic) receive from precomputed windows.
     *
     * Equivalent to `try_start_receive(cycle.rx_period, cycle.sleep_period)`.
     *
     * @param cycle Listen/sleep windows for each cycle.
     * @return Success, or an error.
     * @retval invalid_arg The listen or sleep window is not positive.
     */
    [[nodiscard]] result<void> try_start_receive(rx_duty_cycle cycle) {
        return try_start_receive(cycle.rx_period, cycle.sleep_period);
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
    [[nodiscard]] result<void> try_start_receive(const std::optional<rx_duty_cycle>& cycle) {
        return cycle ? try_start_receive(*cycle) : try_start_receive();
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

    // =========================================================================
    // Configuration
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the RF carrier frequency.
     * @param hz Carrier frequency.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_frequency(freq::hertz hz) { unwrap(try_set_frequency(hz)); }

    /**
     * @brief Sets the transmit output power.
     * @param dbm  Output power in dBm. Valid range depends on the chip
     *             variant; drivers return `errc::invalid_arg` for values
     *             outside their supported range.
     * @param ramp Output-power ramp-up time (driver maps to nearest supported value).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_output_power(int8_t dbm, ramp_time ramp = ramp_time::us_200) { unwrap(try_set_output_power(dbm, ramp)); }

    /**
     * @brief Configures the LoRa modulation parameters.
     * @param mod Spreading factor, bandwidth, coding rate.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_lora_modulation(lora_modulation mod) { unwrap(try_set_lora_modulation(mod)); }

    /**
     * @brief Configures the LoRa packet framing.
     * @param params Preamble, header type, CRC, IQ inversion, etc.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_lora_packet_params(lora_packet_params params) { unwrap(try_set_lora_packet_params(params)); }

    /**
     * @brief Sets the LoRa sync word.
     *
     * 16 bits wide; drivers for chips that natively use 8-bit sync words map
     * the low byte. Use `0x3444` for the public LoRa network and `0x1424` for
     * private LoRa networks (SX126x convention).
     *
     * @param sync_word Sync word value.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_sync_word(uint16_t sync_word) { unwrap(try_set_sync_word(sync_word)); }
#endif

    /**
     * @brief Sets the RF carrier frequency.
     * @param hz Carrier frequency.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_frequency(freq::hertz hz) { return do_set_frequency(hz); }

    /**
     * @brief Sets the transmit output power.
     * @param dbm  Output power in dBm.
     * @param ramp Output-power ramp-up time.
     * @return Success, or an error.
     * @retval invalid_arg The requested power is outside the chip variant's supported range.
     */
    [[nodiscard]] result<void> try_set_output_power(int8_t dbm, ramp_time ramp = ramp_time::us_200) {
        return do_set_output_power(dbm, ramp);
    }

    /**
     * @brief Configures the LoRa modulation parameters.
     * @param mod Spreading factor, bandwidth, coding rate.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_lora_modulation(lora_modulation mod) { return do_set_lora_modulation(mod); }

    /**
     * @brief Configures the LoRa packet framing.
     * @param params Preamble, header type, CRC, IQ inversion, etc.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_lora_packet_params(lora_packet_params params) {
        return do_set_lora_packet_params(params);
    }

    /**
     * @brief Sets the LoRa sync word.
     * @param sync_word 16-bit sync word.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_sync_word(uint16_t sync_word) { return do_set_sync_word(sync_word); }

    // =========================================================================
    // Data path: blocking
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Transmits a packet and blocks until completion.
     * @param data    Payload to transmit (1–255 bytes).
     * @param timeout Maximum time to wait for transmit completion.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure or timeout.
     */
    void transmit(std::span<const uint8_t> data, std::chrono::milliseconds timeout) {
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
    [[nodiscard]] rx_info receive(std::span<uint8_t> buffer, std::chrono::milliseconds timeout) {
        return unwrap(try_receive(buffer, timeout));
    }
#endif

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
    [[nodiscard]] result<void> try_transmit(std::span<const uint8_t> data, std::chrono::milliseconds timeout) {
        auto f = try_start_transmit(data);
        if (!f) {
            return error(f.error());
        }
        return _await(*f, timeout);
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
    [[nodiscard]] result<rx_info> try_receive(std::span<uint8_t> buffer, std::chrono::milliseconds timeout) {
        auto f = try_start_receive(buffer);
        if (!f) {
            return error(f.error());
        }
        return _await(*f, timeout);
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
     * @ref start_receive overloads instead.
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
     * `rx_done` returns that newer packet.
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
    [[nodiscard]] packet_status get_packet_status() { return unwrap(try_get_packet_status()); }

    /**
     * @brief Returns the instantaneous RSSI on the configured channel.
     * @return RSSI in dBm.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] int16_t get_rssi_inst_dbm() { return unwrap(try_get_rssi_inst_dbm()); }
#endif

    /**
     * @brief Returns detailed status for the most recent packet.
     * @return Packet status, or an error.
     */
    [[nodiscard]] result<packet_status> try_get_packet_status() { return do_get_packet_status(); }

    /**
     * @brief Returns the instantaneous RSSI on the configured channel.
     * @return RSSI in dBm, or an error.
     */
    [[nodiscard]] result<int16_t> try_get_rssi_inst_dbm() { return do_get_rssi_inst_dbm(); }

    /// Maximum LoRa payload length in bytes.
    static constexpr size_t max_payload_length = 255;

    /// Guard window for the blocking @ref scan_channel. Channel-activity
    /// detection completes within a few symbol times even at the slowest
    /// modulation, so it takes no caller-supplied timeout; the blocking form
    /// waits at most this long before cancelling the scan and reporting
    /// `errc::timeout`.
    static constexpr std::chrono::milliseconds scan_guard_window{1000};

protected:
    transceiver() = default;
    transceiver(transceiver&&) noexcept = default;
    transceiver& operator=(transceiver&&) noexcept = default;

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
    /// Hook for @ref try_start_receive() (continuous receive).
    [[nodiscard]] virtual result<void> do_start_receive() = 0;
    /// Hook for @ref try_start_receive(std::chrono::microseconds, std::chrono::microseconds)
    /// (duty-cycled receive). Both durations are positive.
    [[nodiscard]] virtual result<void>
    do_start_receive(std::chrono::microseconds rx_period, std::chrono::microseconds sleep_period) = 0;
    /// Hook for @ref try_start_channel_scan. The returned future must
    /// complete with the scan result, or with `errc::not_finished` if the
    /// operation is cancelled (standby, sleep, or driver teardown).
    [[nodiscard]] virtual result<idfxx::future<cad_info>> do_start_channel_scan() = 0;

    /// Hook for @ref try_set_frequency.
    [[nodiscard]] virtual result<void> do_set_frequency(freq::hertz hz) = 0;
    /// Hook for @ref try_set_output_power.
    [[nodiscard]] virtual result<void> do_set_output_power(int8_t dbm, ramp_time ramp) = 0;
    /// Hook for @ref try_set_lora_modulation.
    [[nodiscard]] virtual result<void> do_set_lora_modulation(lora_modulation mod) = 0;
    /// Hook for @ref try_set_lora_packet_params.
    [[nodiscard]] virtual result<void> do_set_lora_packet_params(lora_packet_params params) = 0;
    /// Hook for @ref try_set_sync_word.
    [[nodiscard]] virtual result<void> do_set_sync_word(uint16_t sync_word) = 0;

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

    /// Hook for @ref try_get_packet_status.
    [[nodiscard]] virtual result<packet_status> do_get_packet_status() = 0;
    /// Hook for @ref try_get_rssi_inst_dbm.
    [[nodiscard]] virtual result<int16_t> do_get_rssi_inst_dbm() = 0;

private:
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
