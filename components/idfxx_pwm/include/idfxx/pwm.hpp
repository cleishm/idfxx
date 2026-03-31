// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/pwm>
 * @file pwm.hpp
 * @brief PWM controller classes.
 *
 * @defgroup idfxx_pwm PWM Component
 * @brief Type-safe PWM controller for ESP32.
 *
 * Provides PWM output management with lightweight timer identifiers and
 * RAII-managed output bindings. Timers are fixed hardware resources configured
 * via methods; outputs bind a timer, channel, and GPIO pin into an active
 * PWM signal with automatic cleanup on destruction.
 *
 * Depends on @ref idfxx_core for error handling, @ref idfxx_gpio for pin
 * identifiers, and @ref idfxx_hw_support for interrupt allocation types.
 * @{
 */

#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/intr_alloc>

#include <algorithm>
#include <chrono>
#include <frequency/frequency>
#include <optional>
#include <string>
#include <utility>

namespace idfxx::pwm {

// ======================================================================
// Enums
// ======================================================================

/**
 * @headerfile <idfxx/pwm>
 * @brief PWM speed mode selection.
 *
 * Most targets only support low-speed mode. The original ESP32 also provides
 * a high-speed mode with hardware-driven duty updates.
 */
enum class speed_mode : int {
    low_speed = 0, ///< Low-speed mode (available on all targets)
#if SOC_LEDC_SUPPORT_HS_MODE
    high_speed, ///< High-speed mode (ESP32 only)
#endif
};

/**
 * @headerfile <idfxx/pwm>
 * @brief PWM channel slot identifiers.
 *
 * Selects the channel slot for PWM output.
 * The number of available channels is target-dependent.
 */
enum class channel : int {
    ch_0 = 0, ///< Channel 0
    ch_1,     ///< Channel 1
    ch_2,     ///< Channel 2
    ch_3,     ///< Channel 3
    ch_4,     ///< Channel 4
    ch_5,     ///< Channel 5
#if SOC_LEDC_CHANNEL_NUM > 6
    ch_6, ///< Channel 6
#endif
#if SOC_LEDC_CHANNEL_NUM > 7
    ch_7, ///< Channel 7
#endif
};

/**
 * @headerfile <idfxx/pwm>
 * @brief PWM timer clock source.
 *
 * Selects the clock source for the timer. The default `auto_select` lets
 * the driver choose the best source for the requested frequency.
 */
enum class clk_source : int {
    auto_select = 0, ///< Automatic clock source selection
#if SOC_LEDC_SUPPORT_APB_CLOCK
    apb, ///< APB clock (80 MHz)
#endif
#if SOC_LEDC_SUPPORT_REF_TICK
    ref_tick, ///< REF_TICK clock (1 MHz, ESP32 only)
#endif
#if SOC_LEDC_SUPPORT_PLL_DIV_CLOCK
    pll_div, ///< PLL divided clock
#endif
#if SOC_LEDC_SUPPORT_XTAL_CLOCK
    xtal, ///< XTAL clock
#endif
};

/**
 * @headerfile <idfxx/pwm>
 * @brief Fade operation blocking mode.
 */
enum class fade_mode : int {
    no_wait = 0, ///< Return immediately, fade runs in background
    wait_done,   ///< Block until the fade completes
};

/**
 * @headerfile <idfxx/pwm>
 * @brief Channel behavior during light sleep.
 */
enum class sleep_mode : int {
    no_alive_no_pd = 0, ///< No output, keep power domain on (default)
    no_alive_allow_pd,  ///< No output, allow power domain off (saves power)
    keep_alive,         ///< Maintain PWM output during light sleep
};

// ======================================================================
// Forward declarations
// ======================================================================

/** @cond INTERNAL */
class output;
struct output_config;
template<int N, speed_mode M>
struct timer_constant;
/** @endcond */

// ======================================================================
// timer (value type)
// ======================================================================

/**
 * @headerfile <idfxx/pwm>
 * @brief A lightweight identifier for a hardware PWM timer.
 *
 * Timers are fixed hardware resources, like GPIO pins. This class provides
 * a copyable, lightweight handle for configuring and controlling a specific
 * timer unit. Use the predefined constants (timer_0 through timer_3).
 *
 * Call configure() to set up the timer's frequency and resolution
 * before creating any output bound to it.
 *
 * @code
 * using namespace frequency_literals;
 *
 * auto tmr = idfxx::pwm::timer_0;
 * tmr.configure({.frequency = 5_kHz, .resolution_bits = 13});
 *
 * auto pwm = idfxx::pwm::start(idfxx::gpio_18, tmr, idfxx::pwm::channel::ch_0);
 * pwm.set_duty(0.5f);  // 50% duty at 13-bit resolution
 * @endcode
 */
class timer {
    template<int N, speed_mode M>
    friend struct timer_constant;
    friend std::optional<timer> get_timer(enum channel, enum speed_mode);

public:
    /**
     * @headerfile <idfxx/pwm>
     * @brief Timer configuration parameters.
     */
    struct config {
        freq::hertz frequency{0};     ///< PWM frequency.
        uint8_t resolution_bits = 13; ///< Duty resolution in bits (1 to SOC_LEDC_TIMER_BIT_WIDTH).
        enum clk_source clk_source = clk_source::auto_select; ///< Clock source selection.
    };

    timer(const timer&) = default;
    timer& operator=(const timer&) = default;
    timer(timer&&) = default;
    timer& operator=(timer&&) = default;

    constexpr bool operator==(const timer& other) const = default;

    /** @brief Returns the timer number (0-3). */
    [[nodiscard]] constexpr unsigned int num() const noexcept { return _num; }

    /** @brief Returns the speed mode. */
    [[nodiscard]] constexpr enum speed_mode speed_mode() const noexcept { return _speed_mode; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Configures the timer with the given parameters.
     *
     * Sets up the timer's frequency, resolution, and clock source.
     * Must be called before creating any output bound to this timer.
     *
     * @param cfg Timer configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     *
     * @code
     * auto tmr = idfxx::pwm::timer_0;
     * tmr.configure({.frequency = 5_kHz, .resolution_bits = 13});
     * @endcode
     */
    void configure(const struct config& cfg) { unwrap(try_configure(cfg)); }

    /**
     * @brief Configures the timer with frequency and resolution.
     *
     * Convenience overload using automatic clock source selection.
     *
     * @param frequency PWM frequency.
     * @param resolution_bits Duty resolution in bits (default: 13).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void configure(freq::hertz frequency, uint8_t resolution_bits = 13) {
        unwrap(try_configure(frequency, resolution_bits));
    }
#endif

    /**
     * @brief Configures the timer with the given parameters.
     *
     * Sets up the timer's frequency, resolution, and clock source.
     * Must be called before creating any output bound to this timer.
     *
     * @param cfg Timer configuration.
     * @return Success, or an error.
     *
     * @retval invalid_arg Invalid frequency (zero) or resolution.
     * @retval fail Requested frequency cannot be achieved.
     */
    [[nodiscard]] result<void> try_configure(const struct config& cfg);

    /**
     * @brief Configures the timer with frequency and resolution.
     *
     * Convenience overload using automatic clock source selection.
     *
     * @param frequency PWM frequency.
     * @param resolution_bits Duty resolution in bits (default: 13).
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_configure(freq::hertz frequency, uint8_t resolution_bits = 13) {
        return try_configure({.frequency = frequency, .resolution_bits = resolution_bits});
    }

    /**
     * @brief Returns true if the timer has been configured.
     *
     * This reflects global state — any copy of the same timer identifier
     * will see the same configured status.
     *
     * @return True if configure() has been called for this timer.
     */
    [[nodiscard]] bool is_configured() const noexcept;

    /**
     * @brief Returns the configured resolution in bits.
     *
     * Returns 0 if the timer has not been configured via configure().
     *
     * @return Duty resolution bits, or 0 if not configured.
     */
    [[nodiscard]] uint8_t resolution_bits() const noexcept;

    /**
     * @brief Returns the maximum duty ticks for the configured resolution.
     *
     * Equal to `1 << resolution_bits()`. Returns 1 if not configured.
     *
     * @return Maximum duty ticks.
     */
    [[nodiscard]] uint32_t ticks_max() const noexcept;

    /**
     * @brief Returns the current timer frequency.
     *
     * The actual frequency may differ slightly from the requested value
     * due to rounding.
     *
     * @return Current frequency, or 0 Hz if the timer is not configured.
     */
    [[nodiscard]] freq::hertz frequency() const;

    /**
     * @brief Returns the PWM period (1 / frequency) as a duration.
     *
     * Returns zero if the timer is not configured.
     *
     * @return PWM period in nanoseconds, or 0 if not configured.
     */
    [[nodiscard]] std::chrono::nanoseconds period() const;

    /**
     * @brief Returns the duration of a single timer tick.
     *
     * Equal to period() / ticks_max(). Returns zero if the timer is not
     * configured.
     *
     * @return Tick duration in nanoseconds, or 0 if not configured.
     */
    [[nodiscard]] std::chrono::nanoseconds tick_period() const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Changes the timer frequency.
     *
     * The timer must have been configured first via configure().
     *
     * @param frequency New PWM frequency.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void set_frequency(freq::hertz frequency) { unwrap(try_set_frequency(frequency)); }
#endif

    /**
     * @brief Changes the timer frequency.
     *
     * The timer must have been configured first via configure().
     *
     * @param frequency New PWM frequency.
     * @return Success, or an error.
     *
     * @retval invalid_arg Invalid frequency or timer not configured.
     * @retval fail Requested frequency cannot be achieved.
     */
    [[nodiscard]] result<void> try_set_frequency(freq::hertz frequency);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Changes the timer period.
     *
     * The timer must have been configured first via configure().
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param period New PWM period.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     *
     * @code
     * using namespace std::chrono_literals;
     * tmr.set_period(20ms);  // 50 Hz
     * @endcode
     */
    template<typename Rep, typename Period>
    void set_period(const std::chrono::duration<Rep, Period>& period) {
        unwrap(try_set_period(period));
    }
#endif

    /**
     * @brief Changes the timer period.
     *
     * The timer must have been configured first via configure().
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param period New PWM period.
     * @return Success, or an error.
     *
     * @retval invalid_arg Invalid period (zero or negative).
     * @retval fail Requested period cannot be achieved.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_set_period(const std::chrono::duration<Rep, Period>& period) {
        auto ns = std::chrono::ceil<std::chrono::nanoseconds>(period).count();
        if (ns <= 0) {
            return error(errc::invalid_arg);
        }
        return try_set_frequency(freq::hertz{1'000'000'000 / ns});
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Pauses the timer counter.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void pause() { unwrap(try_pause()); }
#endif

    /**
     * @brief Pauses the timer counter.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_pause();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Resumes the timer counter.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void resume() { unwrap(try_resume()); }
#endif

    /**
     * @brief Resumes the timer counter.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_resume();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Resets the timer counter.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void reset() { unwrap(try_reset()); }
#endif

    /**
     * @brief Resets the timer counter.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_reset();

private:
    friend result<output> try_start(idfxx::gpio, const config&, const output_config&);

    constexpr timer(unsigned int num, enum speed_mode mode) noexcept
        : _num(num)
        , _speed_mode(mode) {}

    unsigned int _num;
    enum speed_mode _speed_mode;
};

/** @cond INTERNAL */
template<int N, speed_mode M>
struct timer_constant {
    static_assert(N >= 0 && N < SOC_LEDC_TIMER_NUM, "Invalid PWM timer number");
    static constexpr timer value{N, M};
};
/** @endcond */

/**
 * @defgroup idfxx_pwm_timers Timer Constants
 * @ingroup idfxx_pwm
 * @brief Predefined timer identifiers.
 * @{
 */
inline constexpr timer timer_0 = timer_constant<0, speed_mode::low_speed>::value;
inline constexpr timer timer_1 = timer_constant<1, speed_mode::low_speed>::value;
inline constexpr timer timer_2 = timer_constant<2, speed_mode::low_speed>::value;
inline constexpr timer timer_3 = timer_constant<3, speed_mode::low_speed>::value;

#if SOC_LEDC_SUPPORT_HS_MODE
inline constexpr timer hs_timer_0 = timer_constant<0, speed_mode::high_speed>::value;
inline constexpr timer hs_timer_1 = timer_constant<1, speed_mode::high_speed>::value;
inline constexpr timer hs_timer_2 = timer_constant<2, speed_mode::high_speed>::value;
inline constexpr timer hs_timer_3 = timer_constant<3, speed_mode::high_speed>::value;
#endif
/** @} */ // end of Timer Constants

// ======================================================================
// start / try_start — free functions for starting PWM output
// ======================================================================

/**
 * @headerfile <idfxx/pwm>
 * @brief Output configuration parameters.
 */
struct output_config {
    float duty = 0.0f;                                       ///< Initial duty cycle ratio (0.0 to 1.0).
    int hpoint = 0;                                          ///< High point in the PWM cycle.
    bool output_invert = false;                              ///< Invert the output signal.
    enum sleep_mode sleep_mode = sleep_mode::no_alive_no_pd; ///< Behavior during light sleep.
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @headerfile <idfxx/pwm>
 * @brief Starts PWM output on a GPIO pin.
 *
 * Configures the specified channel to output PWM on the given GPIO pin
 * using the provided timer's frequency and resolution.
 *
 * @param gpio GPIO pin for the output.
 * @param tmr Configured timer to use. Must have been configured via timer::configure().
 * @param ch Channel slot to use.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 *
 * @code
 * using namespace frequency_literals;
 * using namespace std::chrono_literals;
 *
 * auto tmr = idfxx::pwm::timer_0;
 * tmr.configure({.frequency = 5_kHz, .resolution_bits = 13});
 *
 * auto pwm = idfxx::pwm::start(idfxx::gpio_18, tmr, idfxx::pwm::channel::ch_0);
 * pwm.set_duty(0.5f);  // 50% duty
 *
 * // Fade to off over 1 second (requires fade service)
 * idfxx::pwm::output::install_fade_service();
 * pwm.fade_to(0.0f, 1s);
 * @endcode
 */
[[nodiscard]] output start(idfxx::gpio gpio, const timer& tmr, enum channel ch);

/** @brief Starts PWM output on a GPIO pin with custom configuration.
 *  @throws std::system_error on failure. */
[[nodiscard]] output start(idfxx::gpio gpio, const timer& tmr, enum channel ch, const output_config& cfg);

/**
 * @headerfile <idfxx/pwm>
 * @brief Starts PWM output with automatic timer and channel allocation.
 *
 * Finds a timer already configured with matching parameters, or allocates
 * and configures a new one. Then finds a free channel and starts PWM output.
 * Only low-speed mode timers and channels are considered.
 *
 * @param gpio GPIO pin for the output.
 * @param cfg Timer configuration (frequency, resolution, clock source).
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 *
 * @code
 * using namespace frequency_literals;
 *
 * auto pwm = idfxx::pwm::start(idfxx::gpio_18, {.frequency = 5_kHz});
 * pwm.set_duty(0.5f);
 * @endcode
 */
[[nodiscard]] output start(idfxx::gpio gpio, const timer::config& cfg);

/** @brief Starts PWM output with automatic allocation and custom output configuration.
 *  @throws std::system_error on failure. */
[[nodiscard]] output start(idfxx::gpio gpio, const timer::config& cfg, const output_config& out_cfg);

/** @brief Stops PWM output on a channel and sets it to an idle level.
 *  @throws std::system_error on failure. */
void stop(
    enum channel ch,
    enum speed_mode mode = speed_mode::low_speed,
    idfxx::gpio::level idle_level = idfxx::gpio::level::low
);
#endif

/**
 * @headerfile <idfxx/pwm>
 * @brief Starts PWM output on a GPIO pin.
 *
 * Configures the specified channel to output PWM on the given GPIO pin
 * using the provided timer's frequency and resolution.
 *
 * @param gpio GPIO pin for the output.
 * @param tmr Configured timer to use. Must have been configured via timer::configure().
 * @param ch Channel slot to use.
 * @return The active output, or an error.
 *
 * @retval invalid_state Timer not configured.
 * @retval invalid_arg GPIO not connected or invalid channel.
 *
 * @code
 * using namespace frequency_literals;
 *
 * auto tmr = idfxx::pwm::timer_0;
 * tmr.configure({.frequency = 5_kHz, .resolution_bits = 13});
 *
 * auto pwm = idfxx::pwm::try_start(idfxx::gpio_18, tmr, idfxx::pwm::channel::ch_0);
 * if (pwm) {
 *     pwm->try_set_duty(0.5f);
 * }
 * @endcode
 */
[[nodiscard]] result<output> try_start(idfxx::gpio gpio, const timer& tmr, enum channel ch);

/**
 * @headerfile <idfxx/pwm>
 * @brief Starts PWM output on a GPIO pin with custom configuration.
 *
 * @param gpio GPIO pin for the output.
 * @param tmr Configured timer to use. Must have been configured via timer::configure().
 * @param ch Channel slot to use.
 * @param cfg Output configuration.
 * @return The active output, or an error.
 *
 * @retval invalid_state Timer not configured.
 * @retval invalid_arg GPIO not connected or invalid channel.
 */
[[nodiscard]] result<output> try_start(idfxx::gpio gpio, const timer& tmr, enum channel ch, const output_config& cfg);

/**
 * @headerfile <idfxx/pwm>
 * @brief Starts PWM output with automatic timer and channel allocation.
 *
 * Finds a timer already configured with matching parameters, or allocates
 * and configures a new one. Then finds a free channel and starts PWM output.
 * Only low-speed mode timers and channels are considered.
 *
 * @param gpio GPIO pin for the output.
 * @param cfg Timer configuration (frequency, resolution, clock source).
 * @return The active output, or an error.
 *
 * @retval not_found No free timer or channel available.
 * @retval invalid_arg GPIO not connected or invalid configuration.
 *
 * @code
 * using namespace frequency_literals;
 *
 * auto pwm = idfxx::pwm::try_start(idfxx::gpio_18, {.frequency = 5_kHz});
 * if (pwm) {
 *     pwm->try_set_duty(0.5f);
 * }
 * @endcode
 */
[[nodiscard]] result<output> try_start(idfxx::gpio gpio, const timer::config& cfg);

/**
 * @headerfile <idfxx/pwm>
 * @brief Starts PWM output with automatic allocation and custom output configuration.
 *
 * @param gpio GPIO pin for the output.
 * @param cfg Timer configuration (frequency, resolution, clock source).
 * @param out_cfg Output configuration (initial duty, hpoint, inversion, sleep mode).
 * @return The active output, or an error.
 *
 * @retval not_found No free timer or channel available.
 * @retval invalid_arg GPIO not connected or invalid configuration.
 */
[[nodiscard]] result<output> try_start(idfxx::gpio gpio, const timer::config& cfg, const output_config& out_cfg);

/**
 * @headerfile <idfxx/pwm>
 * @brief Returns true if the specified channel currently has an active output.
 *
 * @param ch Channel to check.
 * @param mode Speed mode group (defaults to low_speed).
 * @return True if the channel has an active output.
 */
[[nodiscard]] bool is_active(enum channel ch, enum speed_mode mode = speed_mode::low_speed);

/**
 * @headerfile <idfxx/pwm>
 * @brief Returns the timer associated with an active channel, if any.
 *
 * @param ch Channel to check.
 * @param mode Speed mode group (defaults to low_speed).
 * @return The timer bound to the channel, or std::nullopt if the channel is not active.
 */
[[nodiscard]] std::optional<timer> get_timer(enum channel ch, enum speed_mode mode = speed_mode::low_speed);

/**
 * @headerfile <idfxx/pwm>
 * @brief Stops PWM output on a channel and sets it to an idle level.
 *
 * Stops the channel regardless of whether an output object owns it.
 * Any existing output for this channel will become inactive.
 *
 * @param ch Channel to stop.
 * @param mode Speed mode group (defaults to low_speed).
 * @param idle_level Output level after stopping.
 * @return Success, or an error.
 */
result<void> try_stop(
    enum channel ch,
    enum speed_mode mode = speed_mode::low_speed,
    idfxx::gpio::level idle_level = idfxx::gpio::level::low
);

// ======================================================================
// output (RAII, move-only)
// ======================================================================

/**
 * @headerfile <idfxx/pwm>
 * @brief An active PWM output binding a timer, channel, and GPIO pin.
 *
 * Represents a configured PWM output that is actively driving a GPIO pin
 * with a PWM signal. This type is non-copyable and move-only. The destructor
 * automatically stops the PWM output.
 *
 * Create via start() or try_start(), which accept either a timer::config
 * (for automatic timer and channel allocation) or an explicit timer and
 * channel.
 *
 * @code
 * using namespace frequency_literals;
 * using namespace std::chrono_literals;
 *
 * auto pwm = idfxx::pwm::start(idfxx::gpio_18, {.frequency = 5_kHz});
 * pwm.set_duty(0.5f);  // 50% duty
 *
 * // Fade to off over 1 second
 * idfxx::pwm::output::install_fade_service();
 * pwm.fade_to(0.0f, 1s);
 * @endcode
 */
class output {
public:
    /**
     * @brief Destroys the output, stopping PWM on the bound GPIO.
     *
     * Sets the output to idle low level.
     */
    ~output();

    output(const output&) = delete;
    output& operator=(const output&) = delete;
    output(output&& other) noexcept;
    output& operator=(output&& other) noexcept;

    /**
     * @brief Returns the timer that was bound to this output.
     *
     * @note This output may no longer be driving the channel.
     */
    [[nodiscard]] class timer timer() const noexcept { return _timer; }

    /**
     * @brief Returns the channel slot that was bound to this output.
     *
     * @note This output may no longer be driving the channel.
     */
    [[nodiscard]] enum channel channel() const noexcept { return _channel; }

    /** @brief Returns true if the output is still actively driving the channel. */
    [[nodiscard]] bool is_active() const noexcept;

    /** @brief Returns the maximum duty ticks for the configured resolution. */
    [[nodiscard]] uint32_t ticks_max() const noexcept;

    /**
     * @brief Returns the current duty cycle as a ratio.
     *
     * @return Duty ratio in range [0.0, 1.0].
     */
    [[nodiscard]] float duty() const;

    /**
     * @brief Returns the current duty cycle in ticks.
     *
     * Returns the duty at the current point in the PWM cycle.
     *
     * @return Current duty ticks.
     */
    [[nodiscard]] uint32_t duty_ticks() const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the PWM duty cycle as a ratio.
     *
     * Thread-safe. The new duty takes effect on the next PWM cycle.
     *
     * @param duty Duty ratio in range [0.0, 1.0].
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void set_duty(float duty) { unwrap(try_set_duty(duty)); }
#endif

    /**
     * @brief Sets the PWM duty cycle as a ratio.
     *
     * Thread-safe. The new duty takes effect on the next PWM cycle.
     *
     * @param duty Duty ratio in range [0.0, 1.0].
     * @return Success, or an error.
     *
     * @retval invalid_state Output has been moved from.
     * @retval invalid_arg Invalid duty ratio.
     */
    [[nodiscard]] result<void> try_set_duty(float duty);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the PWM duty cycle in ticks.
     *
     * Thread-safe.
     *
     * @param duty Duty in ticks, in range [0, ticks_max()].
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void set_duty_ticks(uint32_t duty) { unwrap(try_set_duty_ticks(duty)); }

    /**
     * @brief Sets the PWM duty cycle in ticks with high point.
     *
     * Thread-safe.
     *
     * @param duty Duty in ticks, in range [0, ticks_max()].
     * @param hpoint High point in the PWM cycle.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void set_duty_ticks(uint32_t duty, uint32_t hpoint) { unwrap(try_set_duty_ticks(duty, hpoint)); }
#endif

    /**
     * @brief Sets the PWM duty cycle in ticks.
     *
     * Thread-safe.
     *
     * @param duty Duty in ticks, in range [0, ticks_max()].
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_duty_ticks(uint32_t duty);

    /**
     * @brief Sets the PWM duty cycle in ticks with high point.
     *
     * Thread-safe.
     *
     * @param duty Duty in ticks, in range [0, ticks_max()].
     * @param hpoint High point in the PWM cycle.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_duty_ticks(uint32_t duty, uint32_t hpoint);

    /**
     * @brief Returns the current pulse width as a duration.
     *
     * @return Current pulse width in nanoseconds.
     */
    [[nodiscard]] std::chrono::nanoseconds pulse_width() const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the PWM duty cycle as a pulse width duration.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param width Pulse width duration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     *
     * @code
     * using namespace std::chrono_literals;
     * pwm.set_pulse_width(1500us);  // 1.5 ms pulse width
     * @endcode
     */
    template<typename Rep, typename Period>
    void set_pulse_width(const std::chrono::duration<Rep, Period>& width) {
        unwrap(try_set_pulse_width(width));
    }
#endif

    /**
     * @brief Sets the PWM duty cycle as a pulse width duration.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param width Pulse width duration.
     * @return Success, or an error.
     *
     * @retval invalid_state Output has been moved from.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_set_pulse_width(const std::chrono::duration<Rep, Period>& width) {
        return _try_set_pulse_width(std::chrono::ceil<std::chrono::nanoseconds>(width));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Fades to a target duty ratio over the specified duration.
     *
     * Requires the fade service to be installed via install_fade_service().
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param target Duty ratio in range [0.0, 1.0].
     * @param duration Fade duration.
     * @param mode Whether to block until the fade completes.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void fade_to(
        float target,
        const std::chrono::duration<Rep, Period>& duration,
        enum fade_mode mode = fade_mode::no_wait
    ) {
        unwrap(try_fade_to(target, duration, mode));
    }
#endif

    /**
     * @brief Fades to a target duty ratio over the specified duration.
     *
     * Requires the fade service to be installed via install_fade_service().
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param target Duty ratio in range [0.0, 1.0].
     * @param duration Fade duration.
     * @param mode Whether to block until the fade completes.
     * @return Success, or an error.
     *
     * @retval invalid_state Output moved from or fade service not installed.
     * @retval invalid_arg Invalid parameters.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_fade_to(
        float target,
        const std::chrono::duration<Rep, Period>& duration,
        enum fade_mode mode = fade_mode::no_wait
    ) {
        return _try_fade_to_duty(
            static_cast<uint32_t>(std::clamp(target, 0.0f, 1.0f) * static_cast<float>(ticks_max())),
            std::chrono::ceil<std::chrono::milliseconds>(duration),
            mode
        );
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Fades to a target duty in ticks over the specified duration.
     *
     * Requires the fade service to be installed via install_fade_service().
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param target_duty Duty in ticks, in range [0, ticks_max()].
     * @param duration Fade duration.
     * @param mode Whether to block until the fade completes.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void fade_to_duty_ticks(
        uint32_t target_duty,
        const std::chrono::duration<Rep, Period>& duration,
        enum fade_mode mode = fade_mode::no_wait
    ) {
        unwrap(try_fade_to_duty_ticks(target_duty, duration, mode));
    }
#endif

    /**
     * @brief Fades to a target duty in ticks over the specified duration.
     *
     * Requires the fade service to be installed via install_fade_service().
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param target_duty Duty in ticks, in range [0, ticks_max()].
     * @param duration Fade duration.
     * @param mode Whether to block until the fade completes.
     * @return Success, or an error.
     *
     * @retval invalid_state Output moved from or fade service not installed.
     * @retval invalid_arg Invalid parameters.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_fade_to_duty_ticks(
        uint32_t target_duty,
        const std::chrono::duration<Rep, Period>& duration,
        enum fade_mode mode = fade_mode::no_wait
    ) {
        return _try_fade_to_duty(target_duty, std::chrono::ceil<std::chrono::milliseconds>(duration), mode);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Fades to a target pulse width over the specified duration.
     *
     * Requires the fade service to be installed via install_fade_service().
     *
     * @tparam Rep1 Target width representation type.
     * @tparam Period1 Target width period type.
     * @tparam Rep2 Duration representation type.
     * @tparam Period2 Duration period type.
     * @param target_width Target pulse width.
     * @param duration Fade duration.
     * @param mode Whether to block until the fade completes.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep1, typename Period1, typename Rep2, typename Period2>
    void fade_to_pulse_width(
        const std::chrono::duration<Rep1, Period1>& target_width,
        const std::chrono::duration<Rep2, Period2>& duration,
        enum fade_mode mode = fade_mode::no_wait
    ) {
        unwrap(try_fade_to_pulse_width(target_width, duration, mode));
    }
#endif

    /**
     * @brief Fades to a target pulse width over the specified duration.
     *
     * Requires the fade service to be installed via install_fade_service().
     *
     * @tparam Rep1 Target width representation type.
     * @tparam Period1 Target width period type.
     * @tparam Rep2 Duration representation type.
     * @tparam Period2 Duration period type.
     * @param target_width Target pulse width.
     * @param duration Fade duration.
     * @param mode Whether to block until the fade completes.
     * @return Success, or an error.
     *
     * @retval invalid_state Output moved from or fade service not installed.
     * @retval invalid_arg Invalid parameters.
     */
    template<typename Rep1, typename Period1, typename Rep2, typename Period2>
    [[nodiscard]] result<void> try_fade_to_pulse_width(
        const std::chrono::duration<Rep1, Period1>& target_width,
        const std::chrono::duration<Rep2, Period2>& duration,
        enum fade_mode mode = fade_mode::no_wait
    ) {
        return _try_fade_to_pulse_width(
            std::chrono::ceil<std::chrono::nanoseconds>(target_width),
            std::chrono::ceil<std::chrono::milliseconds>(duration),
            mode
        );
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Starts a fade to a target duty in ticks with step control.
     *
     * Requires the fade service to be installed via install_fade_service().
     *
     * @param target_duty Duty in ticks, in range [0, ticks_max()].
     * @param scale Duty change per step.
     * @param cycle_num Number of PWM cycles per step.
     * @param mode Whether to block until the fade completes.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void
    fade_with_step(uint32_t target_duty, uint32_t scale, uint32_t cycle_num, enum fade_mode mode = fade_mode::no_wait) {
        unwrap(try_fade_with_step(target_duty, scale, cycle_num, mode));
    }
#endif

    /**
     * @brief Starts a fade to the target duty with step control.
     *
     * Requires the fade service to be installed via install_fade_service().
     *
     * @param target_duty Target duty in ticks, in range [0, ticks_max()].
     * @param scale Duty change per step.
     * @param cycle_num Number of PWM cycles per step.
     * @param mode Whether to block until the fade completes.
     * @return Success, or an error.
     *
     * @retval invalid_state Output moved from or fade service not installed.
     * @retval invalid_arg Invalid parameters.
     */
    [[nodiscard]] result<void> try_fade_with_step(
        uint32_t target_duty,
        uint32_t scale,
        uint32_t cycle_num,
        enum fade_mode mode = fade_mode::no_wait
    );

#if SOC_LEDC_SUPPORT_FADE_STOP
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Stops an in-progress fade operation.
     *
     * The duty is guaranteed to be fixed within one PWM cycle after return.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void fade_stop() { unwrap(try_fade_stop()); }
#endif

    /**
     * @brief Stops an in-progress fade operation.
     *
     * The duty is guaranteed to be fixed within one PWM cycle after return.
     *
     * @return Success, or an error.
     */
    result<void> try_fade_stop();
#endif

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Installs the PWM fade service.
     *
     * Must be called before using any fade operations. This is a global
     * service shared by all PWM channels.
     *
     * @param levels Interrupt priority levels to accept.
     * @param flags Behavioral flags.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    static void
    install_fade_service(idfxx::intr_levels levels = intr_level_lowmed, idfxx::flags<intr_flag> flags = {}) {
        unwrap(try_install_fade_service(levels, flags));
    }
#endif

    /**
     * @brief Installs the PWM fade service.
     *
     * Must be called before using any fade operations. This is a global
     * service shared by all PWM channels.
     *
     * @param levels Interrupt priority levels to accept.
     * @param flags Behavioral flags.
     * @return Success, or an error.
     *
     * @retval invalid_state Fade service already installed.
     */
    [[nodiscard]] static result<void>
    try_install_fade_service(idfxx::intr_levels levels = intr_level_lowmed, idfxx::flags<intr_flag> flags = {});

    /**
     * @brief Uninstalls the PWM fade service.
     *
     * All fade operations must be stopped before calling this.
     */
    static void uninstall_fade_service();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Stops PWM output and sets the GPIO to an idle level.
     *
     * @param idle_level Output level after stopping.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void stop(idfxx::gpio::level idle_level = idfxx::gpio::level::low) { unwrap(try_stop(idle_level)); }
#endif

    /**
     * @brief Stops PWM output and sets the GPIO to an idle level.
     *
     * @param idle_level Output level after stopping.
     * @return Success, or an error.
     */
    result<void> try_stop(idfxx::gpio::level idle_level = idfxx::gpio::level::low);

    /**
     * @brief Releases ownership of the channel without stopping the PWM output.
     *
     * After calling release(), the PWM signal continues running and the
     * destructor will not stop it. Use the free function stop() to later
     * stop the channel.
     *
     * @return The channel that was released.
     */
    enum channel release() noexcept;

private:
    friend result<output>
    try_start(idfxx::gpio gpio, const class timer& tmr, enum channel ch, const output_config& cfg);
    friend result<output> try_start(idfxx::gpio, const timer::config&, const output_config&);

    [[nodiscard]] static result<output>
    _make(idfxx::gpio gpio, const class timer& tmr, enum channel ch, const output_config& cfg);

    explicit output(class timer tmr, enum channel ch, uint32_t gen, uint32_t duty_ticks) noexcept;

    [[nodiscard]] result<void> _try_set_pulse_width(std::chrono::nanoseconds width);

    [[nodiscard]] result<void> _try_fade_to_pulse_width(
        std::chrono::nanoseconds target_width,
        std::chrono::milliseconds duration,
        enum fade_mode mode
    );

    [[nodiscard]] result<void>
    _try_fade_to_duty(uint32_t target_duty, std::chrono::milliseconds duration, enum fade_mode mode);

    void _delete() noexcept;

    class timer _timer;
    enum channel _channel;
    std::optional<uint32_t> _gen;
    uint32_t _duty_ticks = 0;
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
inline output start(idfxx::gpio gpio, const timer& tmr, enum channel ch) {
    return unwrap(try_start(gpio, tmr, ch));
}
inline output start(idfxx::gpio gpio, const timer& tmr, enum channel ch, const output_config& cfg) {
    return unwrap(try_start(gpio, tmr, ch, cfg));
}
inline output start(idfxx::gpio gpio, const timer::config& cfg) {
    return unwrap(try_start(gpio, cfg));
}
inline output start(idfxx::gpio gpio, const timer::config& cfg, const output_config& out_cfg) {
    return unwrap(try_start(gpio, cfg, out_cfg));
}
inline void stop(enum channel ch, enum speed_mode mode, idfxx::gpio::level idle_level) {
    unwrap(try_stop(ch, mode, idle_level));
}
#endif

/** @} */ // end of idfxx_pwm

} // namespace idfxx::pwm

namespace idfxx {

/**
 * @headerfile <idfxx/pwm>
 * @brief Returns a string representation of a PWM timer.
 *
 * @param t The timer to convert.
 * @return A string like "PWM_TIMER_0" or "PWM_HS_TIMER_2".
 */
[[nodiscard]] inline std::string to_string(const pwm::timer& t) {
    std::string s;
#if SOC_LEDC_SUPPORT_HS_MODE
    if (t.speed_mode() == pwm::speed_mode::high_speed) {
        s = "PWM_HS_TIMER_";
    } else {
        s = "PWM_TIMER_";
    }
#else
    s = "PWM_TIMER_";
#endif
    s += std::to_string(t.num());
    return s;
}

/**
 * @headerfile <idfxx/pwm>
 * @brief Returns a string representation of a PWM channel.
 *
 * @param ch The channel to convert.
 * @return A string like "PWM_CH_0".
 */
[[nodiscard]] inline std::string to_string(pwm::channel ch) {
    return "PWM_CH_" + std::to_string(std::to_underlying(ch));
}

/**
 * @headerfile <idfxx/pwm>
 * @brief Returns a string representation of a PWM speed mode.
 *
 * @param m The speed mode to convert.
 * @return "low_speed" or "high_speed".
 */
[[nodiscard]] inline std::string to_string(pwm::speed_mode m) {
    switch (m) {
    case pwm::speed_mode::low_speed:
        return "low_speed";
#if SOC_LEDC_SUPPORT_HS_MODE
    case pwm::speed_mode::high_speed:
        return "high_speed";
#endif
    default:
        return "unknown(" + std::to_string(std::to_underlying(m)) + ")";
    }
}

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <format>
namespace std {
template<>
struct formatter<idfxx::pwm::timer> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const idfxx::pwm::timer& t, FormatContext& ctx) const {
        auto s = idfxx::to_string(t);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
template<>
struct formatter<idfxx::pwm::channel> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::pwm::channel ch, FormatContext& ctx) const {
        auto s = idfxx::to_string(ch);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
template<>
struct formatter<idfxx::pwm::speed_mode> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::pwm::speed_mode m, FormatContext& ctx) const {
        auto s = idfxx::to_string(m);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
