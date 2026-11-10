// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/rtc/pcf8563>
 * @file pcf8563.hpp
 * @brief NXP PCF8563 real-time clock driver.
 * @ingroup idfxx_rtc
 *
 * Keeps calendar time on a PCF8563 on an I2C bus, through the chip-agnostic
 * @ref idfxx::rtc::clock interface, and exposes the chip's alarm, countdown
 * timer, clock output and oscillator stop control.
 *
 * Depends on @ref idfxx_rtc for the clock interface, @ref idfxx_i2c for the
 * I2C bus, and @ref idfxx_core for error handling.
 */

#include "sdkconfig.h"

#include <idfxx/error>
#include <idfxx/flags>
#include <idfxx/i2c/master>
#include <idfxx/rtc/clock>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <quanta/frequency>
#include <ratio>
#include <string>
#include <utility>

namespace idfxx::rtc {

/** @cond INTERNAL */
namespace detail {

// The longest countdown timer period, 255 minutes, in ticks of 1/4096 s.
inline constexpr uint32_t max_timer_ticks = 255u * 60u * 4096u;

// The number of 1/4096 s ticks in a period, or std::nullopt if the period is
// not positive, is longer than 255 minutes, or is not a whole number of ticks.
// It never rounds and never overflows, at compile time or run time: the count
// is checked to be positive before it is widened, and every product is bounded
// by division before it is formed.
template<typename Rep, typename Period>
    requires std::integral<Rep>
[[nodiscard]] constexpr std::optional<uint32_t> timer_ticks(const std::chrono::duration<Rep, Period>& period) noexcept {
    // One unit of Period is n/d s = n * k / b ticks, where k/b is 4096/d in
    // lowest terms. n/d is reduced and so is k/b, so units * n * k / b is whole
    // exactly when b divides units.
    constexpr auto n = static_cast<uintmax_t>(Period::type::num);
    constexpr auto d = static_cast<uintmax_t>(Period::type::den);
    constexpr uintmax_t g = std::gcd(uintmax_t{4096}, d);
    constexpr uintmax_t k = 4096 / g;
    constexpr uintmax_t b = d / g;

    const Rep count = period.count();
    if (!(count > 0)) {
        return std::nullopt;
    }
    const auto units = static_cast<uintmax_t>(count);
    if (units % b != 0) {
        return std::nullopt;
    }
    constexpr uintmax_t max_whole_units = n > max_timer_ticks / k ? 0 : max_timer_ticks / k / n;
    const uintmax_t whole = units / b;
    if (whole > max_whole_units) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(whole * n * k);
}

} // namespace detail
/** @endcond */

/**
 * @headerfile <idfxx/rtc/pcf8563>
 * @brief PCF8563 event flags.
 *
 * Each value is the bit the chip sets in its Control_status_2 register when
 * the event occurs; it stays set until cleared. Accessible as
 * `pcf8563::event` after the class definition.
 */
enum class pcf8563_event : uint8_t {
    timer = 1 << 2, ///< Countdown timer flag (TF).
    alarm = 1 << 3, ///< Alarm flag (AF).
};

} // namespace idfxx::rtc

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::rtc::pcf8563_event> = true;

namespace idfxx::rtc {

/**
 * @headerfile <idfxx/rtc/pcf8563>
 * @brief An NXP PCF8563 real-time clock on an I2C bus.
 *
 * The driver creates and owns its own I2C device on the given bus, so the
 * caller only keeps the bus alive. Creating the driver probes the chip's
 * address to check that it responds, and writes nothing: the chip keeps
 * whatever time, alarm, timer and clock output settings it already had.
 *
 * @code
 * using namespace quanta::frequency_literals;
 *
 * idfxx::i2c::master_bus bus(idfxx::i2c::port::i2c0, idfxx::gpio_8, idfxx::gpio_9, 400_kHz);
 * idfxx::rtc::pcf8563 rtc(bus);
 *
 * if (auto t = rtc.now()) {
 *     idfxx::chrono::set_system_time(*t);
 * }
 * @endcode
 *
 * **Time range.** The chip holds times from 2000-01-01T00:00:00 to
 * 2099-12-31T23:59:59 UTC. set_time() rejects anything outside that range with
 * `errc::invalid_arg`. It writes the weekday register from the date, and the
 * century bit as 0.
 *
 * **Time validity.** The time is valid while the chip is keeping reliable
 * time. It is not valid while the voltage-low (VL) flag is set, which the chip
 * does at power-on and when its supply drops too far for the oscillator to keep
 * time; while the clock is stopped (see stop()); or while the time registers
 * hold something other than a time from 2000 to 2099. While it is not valid,
 * now() returns `std::nullopt`. set_time() writes the time, which clears VL,
 * then restarts a stopped clock, so the time is valid afterward: it recovers
 * every case. If the restart fails, the time has already been written and
 * set_time() reports the transfer error; call it again.
 *
 * **Errors.** `errc::invalid_response` and `errc::timeout` mean an I2C
 * transfer failed, as @ref idfxx::i2c::master_device documents; retry the
 * call. `errc::invalid_state` means an alarm setting read back from the chip
 * doesn't decode (see alarm()); set it again with set_alarm().
 *
 * **Clock output.** The CLKOUT pin runs at 32.768 kHz from power-on. Boards
 * that don't use it should call `set_clkout(std::nullopt)` to turn it off and
 * save current.
 *
 * **Interrupts.** The chip's INT pin is an open-drain, active-low interrupt
 * output: the chip pulls it low while an enabled alarm or timer interrupt is
 * signaling, and releases it otherwise, so it needs a pull-up. The driver
 * exposes the alarm and timer interrupt enables and their event flags (see
 * events() and clear_events()); it does not configure a GPIO or an ISR for INT.
 *
 * A driver is not thread-safe: use it from one task, or synchronize access
 * externally. Other devices may share the bus.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior.
 */
class pcf8563 final : public clock {
public:
    /// Event flag enumeration. See @ref pcf8563_event.
    using event = pcf8563_event;

    /**
     * @headerfile <idfxx/rtc/pcf8563>
     * @brief Duration of the countdown timer, in ticks of 1/4096 s.
     *
     * Holds every period the timer can produce exactly, from 1/4096 s up to
     * 255 minutes. timer_remaining() returns the time left in this type.
     */
    using timer_duration = std::chrono::duration<uint32_t, std::ratio<1, 4096>>;

    /**
     * @headerfile <idfxx/rtc/pcf8563>
     * @brief Source clock of the countdown timer.
     *
     * The values match the chip's TD field (Timer_control register 0Eh).
     */
    enum class timer_source : uint8_t {
        hz_4096 = 0, ///< 4096 Hz: counts of 1/4096 s.
        hz_64 = 1,   ///< 64 Hz: counts of 1/64 s.
        hz_1 = 2,    ///< 1 Hz: counts of 1 s.
        hz_1_60 = 3, ///< 1/60 Hz: counts of 1 minute.
    };

    /**
     * @headerfile <idfxx/rtc/pcf8563>
     * @brief How the countdown timer drives the INT pin.
     */
    enum class timer_interrupt : uint8_t {
        none,  ///< INT is not driven by the timer (TIE = 0); the timer flag is still set.
        level, ///< INT is held low while the timer flag is set (TIE = 1, TI_TP = 0).
        pulse, ///< INT pulses each time the countdown reaches zero (TIE = 1, TI_TP = 1).
    };

    /**
     * @headerfile <idfxx/rtc/pcf8563>
     * @brief Frequency of the CLKOUT pin.
     *
     * The values match the chip's FD field (CLKOUT_control register 0Dh).
     */
    enum class clkout_frequency : uint8_t {
        hz_32768 = 0, ///< 32.768 kHz (the power-on setting).
        hz_1024 = 1,  ///< 1.024 kHz.
        hz_32 = 2,    ///< 32 Hz.
        hz_1 = 3,     ///< 1 Hz.
    };

    /**
     * @headerfile <idfxx/rtc/pcf8563>
     * @brief Alarm settings.
     *
     * The alarm fires when every set field matches the current time: the
     * chip then sets the alarm flag (see events()) and, if @ref interrupt
     * is true, pulls INT low until the flag is cleared with clear_events(). A field left
     * empty takes no part in the match.
     *
     * The weekday field compares against the chip's weekday register, which
     * set_time() writes from the date.
     *
     * @code
     * using namespace std::chrono_literals;
     * // Every day at 07:30
     * rtc.set_alarm({.minute = 30min, .hour = 7h, .interrupt = true});
     * @endcode
     */
    struct alarm_config {
        std::optional<std::chrono::minutes> minute = std::nullopt;  ///< Minute (0–59), or empty to ignore.
        std::optional<std::chrono::hours> hour = std::nullopt;      ///< Hour (0–23), or empty to ignore.
        std::optional<std::chrono::day> day = std::nullopt;         ///< Day of the month (1–31), or empty to ignore.
        std::optional<std::chrono::weekday> weekday = std::nullopt; ///< Weekday, or empty to ignore.
        bool interrupt = false;                                     ///< Drive INT when the alarm fires (AIE).
    };

    /**
     * @headerfile <idfxx/rtc/pcf8563>
     * @brief Countdown timer settings.
     */
    struct timer_config {
        timer_interrupt interrupt = timer_interrupt::none; ///< How the timer drives INT.
    };

    /**
     * @headerfile <idfxx/rtc/pcf8563>
     * @brief Configuration for a PCF8563 driver.
     */
    struct config {
        uint16_t address = 0x51; ///< 7-bit I2C address of the chip.
        /// I2C clock for the chip, or empty to use the bus frequency. The chip
        /// supports at most 400 kHz.
        std::optional<quanta::frequency::hertz> scl_speed = std::nullopt;
    };

    /**
     * @name Construction
     * @brief Creating, moving and destroying a driver.
     * @{
     */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a PCF8563 driver on the given I2C bus with the default configuration.
     *
     * Equivalent to `pcf8563(bus, config{})`: address 0x51, at the bus frequency.
     * Probes the chip's address to check that it responds, and writes nothing.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this driver.
     *
     * @param bus Parent I2C master bus.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if the bus frequency is above 400 kHz,
     *         with `errc::not_found` if no chip acknowledges the probe, with `errc::timeout` if
     *         the bus could not be acquired in time or the probe timed out on the bus (for
     *         example, SCL held low), or on another I2C error.
     */
    [[nodiscard]] explicit pcf8563(i2c::master_bus& bus);

    /**
     * @brief Creates a PCF8563 driver on the given I2C bus.
     *
     * Probes the chip's address to check that it responds, and writes nothing.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this driver.
     *
     * @param bus Parent I2C master bus.
     * @param cfg Driver configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if the I2C clock (the configured
     *         `scl_speed`, or else the bus frequency) is above 400 kHz or `scl_speed` is not
     *         positive, with `errc::not_found` if no chip acknowledges the probe, with
     *         `errc::timeout` if the bus could not be acquired in time or the probe timed out on
     *         the bus (for example, SCL held low), or on another I2C error.
     */
    [[nodiscard]] explicit pcf8563(i2c::master_bus& bus, const config& cfg);
#endif

    /**
     * @brief Creates a PCF8563 driver on the given I2C bus with the default configuration.
     *
     * Equivalent to `make(bus, config{})`: address 0x51, at the bus frequency.
     * Probes the chip's address to check that it responds, and writes nothing.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this driver.
     *
     * @param bus Parent I2C master bus.
     * @return The new driver, or an error. Other I2C errors are passed through.
     * @retval invalid_arg The bus frequency is above 400 kHz.
     * @retval not_found No chip acknowledged the probe.
     * @retval timeout The bus could not be acquired in time, or the probe timed out on the bus
     *         (for example, SCL held low).
     */
    [[nodiscard]] static result<pcf8563> make(i2c::master_bus& bus);

    /**
     * @brief Creates a PCF8563 driver on the given I2C bus.
     *
     * Probes the chip's address to check that it responds, and writes nothing.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this driver.
     *
     * @param bus Parent I2C master bus.
     * @param cfg Driver configuration.
     * @return The new driver, or an error. Other I2C errors are passed through.
     * @retval invalid_arg The I2C clock (the configured `scl_speed`, or else the bus
     *         frequency) is above 400 kHz, or `scl_speed` is not positive.
     * @retval not_found No chip acknowledged the probe.
     * @retval timeout The bus could not be acquired in time, or the probe timed out on the bus
     *         (for example, SCL held low).
     */
    [[nodiscard]] static result<pcf8563> make(i2c::master_bus& bus, const config& cfg);

    /**
     * @brief Releases the I2C device.
     *
     * The chip is left in its current state and keeps time.
     */
    ~pcf8563() override;

    pcf8563(const pcf8563&) = delete;
    pcf8563& operator=(const pcf8563&) = delete;

    /**
     * @brief Move constructor.
     *
     * @param other The driver to move from; it must not be used afterward.
     */
    pcf8563(pcf8563&& other) noexcept;

    /**
     * @brief Move assignment operator.
     *
     * Releases this driver's I2C device, then takes over @p other's.
     *
     * @param other The driver to move from; it must not be used afterward.
     * @return A reference to this driver.
     */
    pcf8563& operator=(pcf8563&& other) noexcept;

    /** @} */

    /**
     * @name Oscillator
     * @brief Stopping and restarting timekeeping.
     * @{
     */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Resumes timekeeping after stop().
     *
     * Clears the chip's STOP bit, continuing from the time the clock held when
     * it stopped. The first one-second increment comes about 0.508 s
     * afterward. Does nothing to a clock that is already running. To set the
     * time as well, call set_time() instead, which also restarts the clock.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on I2C failure.
     */
    void start() { unwrap(try_start()); }

    /**
     * @brief Stops timekeeping.
     *
     * Sets the chip's STOP bit, which holds its sub-second divider in reset:
     * the time counters stop until start() or set_time(). A 32.768 kHz clock
     * output keeps running; the 1.024 kHz, 32 Hz and 1 Hz outputs stop. While
     * the clock is stopped its time is not valid: now() returns
     * `std::nullopt`.
     *
     * To set the time on a precise second, stop the clock, then call
     * set_time(t): the clock starts counting from that write, its first
     * one-second increment about 0.508 s later, so make the write about 0.49 s
     * after t.
     *
     * @code
     * rtc.stop();
     * // ...wait until about 0.49 s after t...
     * rtc.set_time(t);
     * @endcode
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on I2C failure.
     */
    void stop() { unwrap(try_stop()); }

    /**
     * @brief Reports whether the clock is running.
     *
     * @return False if timekeeping has been stopped with stop(), otherwise true.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on I2C failure.
     */
    [[nodiscard]] bool running() { return unwrap(try_running()); }
#endif

    /**
     * @brief Resumes timekeeping after try_stop().
     *
     * Clears the chip's STOP bit, continuing from the time the clock held when
     * it stopped. The first one-second increment comes about 0.508 s
     * afterward. Does nothing to a clock that is already running. To set the
     * time as well, call try_set_time() instead, which also restarts the clock.
     *
     * @return Success, or an error on I2C failure.
     */
    [[nodiscard]] result<void> try_start();

    /**
     * @brief Stops timekeeping.
     *
     * Sets the chip's STOP bit, which holds its sub-second divider in reset:
     * the time counters stop until try_start() or try_set_time(). A
     * 32.768 kHz clock output keeps running; the 1.024 kHz, 32 Hz and 1 Hz
     * outputs stop. While the clock is stopped its time is not valid:
     * try_now() returns `std::nullopt`.
     *
     * To set the time on a precise second, stop the clock, then call
     * try_set_time(t): the clock starts counting from that write, its first
     * one-second increment about 0.508 s later, so make the write about 0.49 s
     * after t.
     *
     * @return Success, or an error on I2C failure.
     */
    [[nodiscard]] result<void> try_stop();

    /**
     * @brief Reports whether the clock is running.
     *
     * @return False if timekeeping has been stopped with try_stop(), otherwise
     *         true, or an error on I2C failure.
     */
    [[nodiscard]] result<bool> try_running();

    /** @} */

    /**
     * @name Alarm
     * @brief The chip's alarm: its settings and interrupt enable.
     * @{
     */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Configures the alarm.
     *
     * Writes all four alarm fields, then the alarm interrupt enable. Fields
     * left empty are disabled; `set_alarm({})` turns the alarm off entirely.
     * The alarm flag is left as it is; clear it with clear_events().
     *
     * @code
     * using namespace std::chrono_literals;
     * // At 07:30 on Mondays
     * rtc.set_alarm({.minute = 30min, .hour = 7h, .weekday = std::chrono::Monday, .interrupt = true});
     * @endcode
     *
     * @param alarm The alarm settings.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if a field is out of range (nothing is
     *         written), or on I2C failure.
     */
    void set_alarm(const alarm_config& alarm) { unwrap(try_set_alarm(alarm)); }

    /**
     * @brief Reads the alarm settings back from the chip.
     *
     * @return The alarm settings, including the interrupt enable.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_state` if an enabled alarm field holds
     *         an impossible value, or on I2C failure.
     */
    [[nodiscard]] alarm_config alarm() { return unwrap(try_alarm()); }
#endif

    /**
     * @brief Configures the alarm.
     *
     * Writes all four alarm fields, then the alarm interrupt enable. Fields
     * left empty are disabled; `try_set_alarm({})` turns the alarm off
     * entirely. The alarm flag is left as it is; clear it with
     * try_clear_events().
     *
     * @param alarm The alarm settings.
     * @return Success, or an error.
     * @retval invalid_arg A field is out of range; nothing was written.
     */
    [[nodiscard]] result<void> try_set_alarm(const alarm_config& alarm);

    /**
     * @brief Reads the alarm settings back from the chip.
     *
     * @return The alarm settings, including the interrupt enable, or an error.
     * @retval invalid_state An enabled alarm field holds an impossible value.
     */
    [[nodiscard]] result<alarm_config> try_alarm();

    /** @} */

    /**
     * @name Countdown timer
     * @brief The chip's countdown timer: starting and stopping it, and the time left.
     * @{
     */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Starts the countdown timer with a period, without driving INT.
     *
     * Equivalent to `start_timer(period, timer_config{})`; see that overload
     * for how the period is produced.
     *
     * @code
     * using namespace std::chrono_literals;
     * rtc.start_timer(250ms);
     * @endcode
     *
     * @tparam Rep    Duration arithmetic type; must be an integer type.
     * @tparam Period Duration period type.
     * @param period The period, from 1/4096 s to 255 minutes, in whole ticks of 1/4096 s.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if the timer cannot produce @p period
     *         exactly (nothing is written), or on I2C failure.
     */
    template<typename Rep, typename Period>
        requires std::integral<Rep>
    void start_timer(const std::chrono::duration<Rep, Period>& period) {
        unwrap(try_start_timer(period));
    }

    /**
     * @brief Starts the countdown timer with a period.
     *
     * Picks the fastest source clock that produces @p period exactly as a
     * count of 1 to 255 ticks: for example 30 s runs at 1 Hz with a count of
     * 30, 250 ms at 64 Hz with a count of 16, and 5 min at 1/60 Hz with a
     * count of 5. The period is never rounded: one that is not positive, is
     * longer than 255 minutes, or that no source produces exactly, such as
     * 100 ms, is rejected.
     *
     * The timer repeats: each time the countdown reaches zero the chip sets
     * the timer flag (see events()) and reloads the count.
     *
     * The chip is written in this order, so INT cannot fire from a stale
     * setting: the timer is disabled with its new source and count in one
     * write, then the interrupt mode is set, then the timer is enabled. A timer
     * flag left set by an earlier period stays set; clear it with
     * clear_events() first if INT is in level mode.
     *
     * @code
     * using namespace std::chrono_literals;
     * rtc.start_timer(30s, {.interrupt = idfxx::rtc::pcf8563::timer_interrupt::pulse});
     * rtc.start_timer(5min, {.interrupt = idfxx::rtc::pcf8563::timer_interrupt::level});
     * @endcode
     *
     * @tparam Rep    Duration arithmetic type; must be an integer type.
     * @tparam Period Duration period type.
     * @param period The period, from 1/4096 s to 255 minutes, in whole ticks of 1/4096 s.
     * @param cfg    The timer settings.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if the timer cannot produce @p period
     *         exactly or @p cfg is invalid (nothing is written), or on I2C failure.
     */
    template<typename Rep, typename Period>
        requires std::integral<Rep>
    void start_timer(const std::chrono::duration<Rep, Period>& period, const timer_config& cfg) {
        unwrap(try_start_timer(period, cfg));
    }

    /**
     * @brief Starts the countdown timer with an explicit source and count, without driving INT.
     *
     * Equivalent to `start_timer(source, count, timer_config{})`.
     *
     * @param source The source clock.
     * @param count  The number of source ticks per period (1–255).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if @p count is 0 or @p source is invalid
     *         (nothing is written), or on I2C failure.
     */
    void start_timer(timer_source source, uint8_t count) { unwrap(try_start_timer(source, count)); }

    /**
     * @brief Starts the countdown timer with an explicit source and count.
     *
     * The period is @p count ticks of @p source. The timer repeats, and the
     * chip is written in the same order as by the start_timer() overload that
     * takes a period and a timer_config.
     *
     * @param source The source clock.
     * @param count  The number of source ticks per period (1–255).
     * @param cfg    The timer settings.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if @p count is 0, or @p source or
     *         @p cfg is invalid (nothing is written), or on I2C failure.
     */
    void start_timer(timer_source source, uint8_t count, const timer_config& cfg) {
        unwrap(try_start_timer(source, count, cfg));
    }

    /**
     * @brief Reads the time left in the current countdown period.
     *
     * The countdown register cannot be frozen while it is read, so it is read
     * until two consecutive reads agree. At 4096 Hz it may change between
     * every read; after a few attempts the latest value is returned.
     *
     * @return The time left in the current period, or zero if the timer is not running.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on I2C failure.
     */
    [[nodiscard]] timer_duration timer_remaining() { return unwrap(try_timer_remaining()); }

    /**
     * @brief Stops the countdown timer.
     *
     * Disables the timer, switches it to its lowest-power source (1/60 Hz),
     * and disables its interrupt. The timer flag is left as it is.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on I2C failure.
     */
    void stop_timer() { unwrap(try_stop_timer()); }
#endif

    /**
     * @brief Starts the countdown timer with a period, without driving INT.
     *
     * Equivalent to `try_start_timer(period, timer_config{})`; see that
     * overload for how the period is produced.
     *
     * @tparam Rep    Duration arithmetic type; must be an integer type.
     * @tparam Period Duration period type.
     * @param period The period, from 1/4096 s to 255 minutes, in whole ticks of 1/4096 s.
     * @return Success, or an error.
     * @retval invalid_arg The timer cannot produce @p period exactly; nothing was written.
     */
    template<typename Rep, typename Period>
        requires std::integral<Rep>
    [[nodiscard]] result<void> try_start_timer(const std::chrono::duration<Rep, Period>& period) {
        return try_start_timer(period, timer_config{});
    }

    /**
     * @brief Starts the countdown timer with a period.
     *
     * Picks the fastest source clock that produces @p period exactly as a
     * count of 1 to 255 ticks: for example 30 s runs at 1 Hz with a count of
     * 30, 250 ms at 64 Hz with a count of 16, and 5 min at 1/60 Hz with a
     * count of 5. The period is never rounded: one that is not positive, is
     * longer than 255 minutes, or that no source produces exactly, such as
     * 100 ms, is rejected.
     *
     * The timer repeats: each time the countdown reaches zero the chip sets
     * the timer flag (see try_events()) and reloads the count.
     *
     * The chip is written in this order, so INT cannot fire from a stale
     * setting: the timer is disabled with its new source and count in one
     * write, then the interrupt mode is set, then the timer is enabled. A timer
     * flag left set by an earlier period stays set; clear it with
     * try_clear_events() first if INT is in level mode.
     *
     * @tparam Rep    Duration arithmetic type; must be an integer type.
     * @tparam Period Duration period type.
     * @param period The period, from 1/4096 s to 255 minutes, in whole ticks of 1/4096 s.
     * @param cfg    The timer settings.
     * @return Success, or an error.
     * @retval invalid_arg The timer cannot produce @p period exactly, or @p cfg is invalid;
     *         nothing was written.
     */
    template<typename Rep, typename Period>
        requires std::integral<Rep>
    [[nodiscard]] result<void>
    try_start_timer(const std::chrono::duration<Rep, Period>& period, const timer_config& cfg) {
        const auto ticks = detail::timer_ticks(period);
        if (!ticks) {
            return error(errc::invalid_arg);
        }
        return _try_start_timer(timer_duration{*ticks}, cfg);
    }

    /**
     * @brief Starts the countdown timer with an explicit source and count, without driving INT.
     *
     * Equivalent to `try_start_timer(source, count, timer_config{})`.
     *
     * @param source The source clock.
     * @param count  The number of source ticks per period (1–255).
     * @return Success, or an error.
     * @retval invalid_arg @p count is 0 or @p source is invalid; nothing was written.
     */
    [[nodiscard]] result<void> try_start_timer(timer_source source, uint8_t count);

    /**
     * @brief Starts the countdown timer with an explicit source and count.
     *
     * The period is @p count ticks of @p source. The timer repeats, and the
     * chip is written in the same order as by the try_start_timer() overload
     * that takes a period and a timer_config.
     *
     * @param source The source clock.
     * @param count  The number of source ticks per period (1–255).
     * @param cfg    The timer settings.
     * @return Success, or an error.
     * @retval invalid_arg @p count is 0, or @p source or @p cfg is invalid; nothing was written.
     */
    [[nodiscard]] result<void> try_start_timer(timer_source source, uint8_t count, const timer_config& cfg);

    /**
     * @brief Reads the time left in the current countdown period.
     *
     * The countdown register cannot be frozen while it is read, so it is read
     * until two consecutive reads agree. At 4096 Hz it may change between
     * every read; after a few attempts the latest value is returned.
     *
     * @return The time left in the current period, or zero if the timer is not
     *         running, or an error on I2C failure.
     */
    [[nodiscard]] result<timer_duration> try_timer_remaining();

    /**
     * @brief Stops the countdown timer.
     *
     * Disables the timer, switches it to its lowest-power source (1/60 Hz),
     * and disables its interrupt. The timer flag is left as it is.
     *
     * @return Success, or an error on I2C failure.
     */
    [[nodiscard]] result<void> try_stop_timer();

    /** @} */

    /**
     * @name Events
     * @brief The alarm and timer event flags.
     * @{
     */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads which events have occurred.
     *
     * Reads the chip's status once and reports which of the alarm and timer
     * flags are set. A flag stays set until cleared with clear_events().
     *
     * @code
     * using idfxx::rtc::pcf8563;
     *
     * // After an INT wake
     * auto e = rtc.events();
     * if (e.contains(pcf8563::event::alarm)) {
     *     // ...handle the alarm
     * }
     * rtc.clear_events(e);
     * @endcode
     *
     * @return The set of events whose flags are set; empty if none is.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on I2C failure.
     */
    [[nodiscard]] flags<event> events() { return unwrap(try_events()); }

    /**
     * @brief Clears event flags.
     *
     * Clears only the named flags, in one write of the chip's status, and
     * writes nothing if none of them is set. Clearing a flag releases INT if
     * that flag was driving it.
     *
     * The chip clears a flag when a 0 is written to it and leaves it set when
     * a 1 is written, so clearing the events that were read never wipes one
     * that arrived after the read: an event that fires in between stays set
     * for the next events().
     *
     * @code
     * auto e = rtc.events();
     * // ...handle e
     * rtc.clear_events(e);
     * @endcode
     *
     * @param events The events whose flags to clear; the others are left as they are.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on I2C failure.
     */
    void clear_events(flags<event> events) { unwrap(try_clear_events(events)); }
#endif

    /**
     * @brief Reads which events have occurred.
     *
     * Reads the chip's status once and reports which of the alarm and timer
     * flags are set. A flag stays set until cleared with try_clear_events().
     *
     * @return The set of events whose flags are set (empty if none is), or an
     *         error on I2C failure.
     */
    [[nodiscard]] result<flags<event>> try_events();

    /**
     * @brief Clears event flags.
     *
     * Clears only the named flags, in one write of the chip's status, and
     * writes nothing if none of them is set. Clearing a flag releases INT if
     * that flag was driving it.
     *
     * The chip clears a flag when a 0 is written to it and leaves it set when
     * a 1 is written, so clearing the events that were read never wipes one
     * that arrived after the read: an event that fires in between stays set
     * for the next try_events().
     *
     * @param events The events whose flags to clear; the others are left as they are.
     * @return Success, or an error on I2C failure.
     */
    [[nodiscard]] result<void> try_clear_events(flags<event> events);

    /** @} */

    /**
     * @name Clock output
     * @brief The CLKOUT pin.
     * @{
     */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the frequency of the CLKOUT pin, or turns it off.
     *
     * CLKOUT runs at 32.768 kHz from power-on. Turn it off on boards that
     * don't use it, to save current.
     *
     * @code
     * rtc.set_clkout(std::nullopt);
     * @endcode
     *
     * @param frequency The output frequency, or `std::nullopt` to turn the output off.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if @p frequency is invalid, or on I2C
     *         failure.
     */
    void set_clkout(std::optional<clkout_frequency> frequency) { unwrap(try_set_clkout(frequency)); }

    /**
     * @brief Reads the CLKOUT setting.
     *
     * @return The output frequency, or `std::nullopt` if the output is off.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on I2C failure.
     */
    [[nodiscard]] std::optional<clkout_frequency> clkout() { return unwrap(try_clkout()); }
#endif

    /**
     * @brief Sets the frequency of the CLKOUT pin, or turns it off.
     *
     * CLKOUT runs at 32.768 kHz from power-on. Turn it off on boards that
     * don't use it, to save current.
     *
     * @param frequency The output frequency, or `std::nullopt` to turn the output off.
     * @return Success, or an error.
     * @retval invalid_arg @p frequency is invalid.
     */
    [[nodiscard]] result<void> try_set_clkout(std::optional<clkout_frequency> frequency);

    /**
     * @brief Reads the CLKOUT setting.
     *
     * @return The output frequency, or `std::nullopt` if the output is off, or
     *         an error on I2C failure.
     */
    [[nodiscard]] result<std::optional<clkout_frequency>> try_clkout();

    /** @} */

    /// @cond INTERNAL
    struct state;
    /// @endcond

private:
    explicit pcf8563(std::unique_ptr<state> s) noexcept;

    // Starts the timer with a period already converted to a whole number of 1/4096 s ticks.
    [[nodiscard]] result<void> _try_start_timer(timer_duration period, const timer_config& cfg);

    [[nodiscard]] result<std::optional<std::chrono::sys_seconds>> do_now() override;
    [[nodiscard]] result<void> do_set_time(std::chrono::sys_seconds t) override;

    std::unique_ptr<state> _state;
};

} // namespace idfxx::rtc

namespace idfxx {

/**
 * @headerfile <idfxx/rtc/pcf8563>
 * @brief Returns a string representation of a PCF8563 event.
 *
 * @param e The event to convert.
 * @return "alarm", "timer", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(rtc::pcf8563_event e) {
    switch (e) {
    case rtc::pcf8563_event::alarm:
        return "alarm";
    case rtc::pcf8563_event::timer:
        return "timer";
    default:
        return "unknown(" + std::to_string(std::to_underlying(e)) + ")";
    }
}

/**
 * @headerfile <idfxx/rtc/pcf8563>
 * @brief Returns a string representation of a PCF8563 timer source.
 *
 * @param s The timer source to convert.
 * @return "4096Hz", "64Hz", "1Hz", "1/60Hz", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(rtc::pcf8563::timer_source s) {
    switch (s) {
    case rtc::pcf8563::timer_source::hz_4096:
        return "4096Hz";
    case rtc::pcf8563::timer_source::hz_64:
        return "64Hz";
    case rtc::pcf8563::timer_source::hz_1:
        return "1Hz";
    case rtc::pcf8563::timer_source::hz_1_60:
        return "1/60Hz";
    default:
        return "unknown(" + std::to_string(std::to_underlying(s)) + ")";
    }
}

/**
 * @headerfile <idfxx/rtc/pcf8563>
 * @brief Returns a string representation of a PCF8563 timer interrupt mode.
 *
 * @param m The timer interrupt mode to convert.
 * @return "none", "level", "pulse", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(rtc::pcf8563::timer_interrupt m) {
    switch (m) {
    case rtc::pcf8563::timer_interrupt::none:
        return "none";
    case rtc::pcf8563::timer_interrupt::level:
        return "level";
    case rtc::pcf8563::timer_interrupt::pulse:
        return "pulse";
    default:
        return "unknown(" + std::to_string(std::to_underlying(m)) + ")";
    }
}

/**
 * @headerfile <idfxx/rtc/pcf8563>
 * @brief Returns a string representation of a PCF8563 CLKOUT frequency.
 *
 * @param f The CLKOUT frequency to convert.
 * @return "32768Hz", "1024Hz", "32Hz", "1Hz", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(rtc::pcf8563::clkout_frequency f) {
    switch (f) {
    case rtc::pcf8563::clkout_frequency::hz_32768:
        return "32768Hz";
    case rtc::pcf8563::clkout_frequency::hz_1024:
        return "1024Hz";
    case rtc::pcf8563::clkout_frequency::hz_32:
        return "32Hz";
    case rtc::pcf8563::clkout_frequency::hz_1:
        return "1Hz";
    default:
        return "unknown(" + std::to_string(std::to_underlying(f)) + ")";
    }
}

} // namespace idfxx

#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::rtc::pcf8563_event> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::rtc::pcf8563_event e, FormatContext& ctx) const {
        auto str = idfxx::to_string(e);
        return std::copy(str.begin(), str.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::rtc::pcf8563::timer_source> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::rtc::pcf8563::timer_source s, FormatContext& ctx) const {
        auto str = idfxx::to_string(s);
        return std::copy(str.begin(), str.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::rtc::pcf8563::timer_interrupt> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::rtc::pcf8563::timer_interrupt m, FormatContext& ctx) const {
        auto str = idfxx::to_string(m);
        return std::copy(str.begin(), str.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::rtc::pcf8563::clkout_frequency> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::rtc::pcf8563::clkout_frequency f, FormatContext& ctx) const {
        auto str = idfxx::to_string(f);
        return std::copy(str.begin(), str.end(), ctx.out());
    }
};

} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
