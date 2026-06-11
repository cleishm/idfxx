// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/sleep>
 * @file sleep.hpp
 * @brief Light and deep sleep with type-safe wake-up source configuration.
 *
 * @defgroup idfxx_sleep Sleep Component
 * @brief Power-saving sleep modes for ESP32.
 *
 * Enter light sleep — which suspends the CPU and resumes execution in place —
 * or deep sleep, which powers down most of the chip and restarts the
 * application on wake. Wake-up sources (timer, GPIO level, EXT0/EXT1 pins)
 * are passed directly to the sleep call:
 *
 * @code
 * auto cause = idfxx::sleep::light_sleep(
 *     idfxx::sleep::timer_wake{30s},
 *     idfxx::sleep::gpio_wake{button, idfxx::gpio::level::low});
 * @endcode
 *
 * A lower-level interface (`enable_*` / `disable_*`) is also provided for
 * arming wake-up sources persistently across multiple sleeps.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin
 * types.
 * @{
 */

#include <idfxx/error>
#include <idfxx/gpio>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <soc/soc_caps.h>
#include <string>
#include <type_traits>

namespace idfxx::sleep {

/**
 * @headerfile <idfxx/sleep>
 * @brief Wake-up sources and causes.
 *
 * Identifies what woke the chip from sleep (see @ref wakeup_cause), and
 * selects a configured source to disable (see @ref try_disable_wakeup_source).
 */
enum class wakeup_source : int {
    // clang-format off
    ext0       = 2,  ///< Level on a single RTC-capable pin (EXT0).
    ext1       = 3,  ///< Level on one or more RTC-capable pins (EXT1).
    timer      = 4,  ///< Sleep timer expired.
    touchpad   = 5,  ///< Touch sensor.
    ulp        = 6,  ///< ULP coprocessor program.
    gpio       = 7,  ///< Digital GPIO level (light sleep; also deep sleep on chips without EXT0/EXT1).
    uart       = 8,  ///< UART activity (light sleep only).
    wifi       = 9,  ///< Wi-Fi beacon (light sleep only).
    cocpu      = 10, ///< ULP-RISC-V coprocessor interrupt.
    cocpu_trap = 11, ///< ULP-RISC-V coprocessor crash.
    bt         = 12, ///< Bluetooth activity (light sleep only).
    other      = 13, ///< A source not represented in this enumeration (chip-specific).
    // clang-format on
};

/**
 * @headerfile <idfxx/sleep>
 * @brief Returns a string representation of a wake-up source.
 *
 * @param source The wake-up source to convert.
 * @return The source name, e.g. "timer" or "gpio", or "unknown(N)" for an
 *         unrecognized value.
 */
[[nodiscard]] std::string to_string(wakeup_source source);

// =============================================================================
// Wake-up source specifications
// =============================================================================

/**
 * @headerfile <idfxx/sleep>
 * @brief Wakes the chip after a fixed duration of sleep.
 *
 * The timer starts when sleep is entered. Reported as
 * @ref wakeup_source::timer.
 */
struct timer_wake {
    std::chrono::microseconds after; ///< Time to sleep before waking.
};

/**
 * @headerfile <idfxx/sleep>
 * @brief Wakes the chip from light sleep when a digital pin is at a level.
 *
 * Any digital GPIO may be used. The wake is level-triggered: if the pin is
 * already at the level when sleep is entered, the chip wakes immediately.
 * Reported as @ref wakeup_source::gpio.
 *
 * @note Light sleep only; use @ref ext0_wake, @ref ext1_wake, or
 *       @ref deep_sleep_gpio_wake (chip-dependent) to wake from deep sleep.
 */
struct gpio_wake {
    idfxx::gpio pin;          ///< The pin to wake on.
    idfxx::gpio::level level; ///< The level that triggers the wake.
};

#if SOC_PM_SUPPORT_EXT0_WAKEUP
/**
 * @headerfile <idfxx/sleep>
 * @brief Wakes the chip when a single RTC-capable pin is at a level (EXT0).
 *
 * Works in both light and deep sleep; the RTC peripherals are kept powered
 * while sleeping. Reported as @ref wakeup_source::ext0.
 *
 * @note Only available on chips with EXT0 wake-up support (`SOC_PM_SUPPORT_EXT0_WAKEUP`).
 */
struct ext0_wake {
    idfxx::gpio pin;          ///< The pin to wake on; must be RTC-capable.
    idfxx::gpio::level level; ///< The level that triggers the wake.
};
#endif // SOC_PM_SUPPORT_EXT0_WAKEUP

/// @cond INTERNAL
namespace detail {

/// Builds a GPIO-number bit mask from a list of pins. An unconnected pin
/// yields an empty mask, which arming rejects as invalid.
constexpr uint64_t pin_mask_of(std::initializer_list<idfxx::gpio> pins) noexcept {
    uint64_t mask = 0;
    for (const auto& pin : pins) {
        if (!pin.is_connected()) {
            return 0;
        }
        mask |= uint64_t{1} << static_cast<unsigned>(pin.num());
    }
    return mask;
}

} // namespace detail
/// @endcond

#if SOC_PM_SUPPORT_EXT1_WAKEUP
/**
 * @headerfile <idfxx/sleep>
 * @brief EXT1 wake-up trigger mode.
 *
 * The low trigger is chip-specific, and only the enumerator matching the
 * hardware is defined for a given target: the original ESP32 wakes only when
 * **all** selected pins are low (`ext1_mode::all_low`), whereas every other
 * EXT1-capable chip wakes when **any** selected pin is low
 * (@ref ext1_mode::any_low). Selecting the wrong one for the target is a
 * compile error rather than a silent change in behaviour; portable code picks
 * the enumerator under `#if CONFIG_IDF_TARGET_ESP32`. For a single wake pin
 * the two are equivalent.
 */
enum class ext1_mode : int {
#if CONFIG_IDF_TARGET_ESP32
    all_low = 0, ///< Wake when all selected pins are low (original ESP32 only).
#else
    any_low = 0, ///< Wake when any selected pin goes low.
#endif
    any_high = 1, ///< Wake when any selected pin goes high.
};

/**
 * @headerfile <idfxx/sleep>
 * @brief Wakes the chip on levels of one or more RTC-capable pins (EXT1).
 *
 * Works in both light and deep sleep. Reported as @ref wakeup_source::ext1;
 * the triggering pins are available from @ref ext1_wakeup_status.
 *
 * @note Floating pins need a pull resistor (external, or an RTC pull held
 *       across sleep) to avoid spurious wakes.
 * @note Only available on chips with EXT1 wake-up support (`SOC_PM_SUPPORT_EXT1_WAKEUP`).
 */
class ext1_wake {
public:
    /**
     * @brief Specifies an EXT1 wake on a set of pins.
     *
     * @param pins The pins to wake on; each must be RTC-capable and
     *             connected, or arming the specification fails.
     * @param mode Whether high or low levels trigger the wake.
     */
    constexpr ext1_wake(std::initializer_list<idfxx::gpio> pins, enum ext1_mode mode) noexcept
        : _pin_mask(detail::pin_mask_of(pins))
        , _mode(mode) {}

    /**
     * @brief Specifies an EXT1 wake on a mask of pins.
     *
     * @param pin_mask Bit mask of GPIO numbers to wake on (bit n selects
     *                 GPIO n); every selected pin must be RTC-capable.
     * @param mode     Whether high or low levels trigger the wake.
     */
    constexpr ext1_wake(uint64_t pin_mask, enum ext1_mode mode) noexcept
        : _pin_mask(pin_mask)
        , _mode(mode) {}

    /// @brief Bit mask of GPIO numbers to wake on.
    [[nodiscard]] constexpr uint64_t pin_mask() const noexcept { return _pin_mask; }

    /// @brief Whether high or low levels trigger the wake.
    [[nodiscard]] constexpr enum ext1_mode mode() const noexcept { return _mode; }

private:
    uint64_t _pin_mask;
    enum ext1_mode _mode;
};
#endif // SOC_PM_SUPPORT_EXT1_WAKEUP

#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
/**
 * @headerfile <idfxx/sleep>
 * @brief Trigger level for deep-sleep GPIO wake-up.
 */
enum class deep_sleep_gpio_mode : int {
    wake_low = 0,  ///< Wake when a selected pin goes low.
    wake_high = 1, ///< Wake when a selected pin goes high.
};

/**
 * @headerfile <idfxx/sleep>
 * @brief Wakes the chip from deep sleep on levels of one or more pins.
 *
 * Available on chips without EXT0/EXT1 support (e.g. ESP32-C3), where it is
 * the only pin-based deep-sleep wake-up source. Only deep-sleep-capable pins
 * may be selected (typically the low-numbered GPIOs; see the chip datasheet).
 * Reported as @ref wakeup_source::gpio.
 *
 * @note Only available on chips with deep-sleep GPIO wake-up support
 *       (`SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP`).
 */
class deep_sleep_gpio_wake {
public:
    /**
     * @brief Specifies a deep-sleep GPIO wake on a set of pins.
     *
     * @param pins The pins to wake on; each must be deep-sleep wake capable
     *             and connected, or arming the specification fails.
     * @param mode Whether high or low levels trigger the wake.
     */
    constexpr deep_sleep_gpio_wake(std::initializer_list<idfxx::gpio> pins, deep_sleep_gpio_mode mode) noexcept
        : _pin_mask(detail::pin_mask_of(pins))
        , _mode(mode) {}

    /**
     * @brief Specifies a deep-sleep GPIO wake on a mask of pins.
     *
     * @param pin_mask Bit mask of GPIO numbers to wake on (bit n selects GPIO n).
     * @param mode     Whether high or low levels trigger the wake.
     */
    constexpr deep_sleep_gpio_wake(uint64_t pin_mask, deep_sleep_gpio_mode mode) noexcept
        : _pin_mask(pin_mask)
        , _mode(mode) {}

    /// @brief Bit mask of GPIO numbers to wake on.
    [[nodiscard]] constexpr uint64_t pin_mask() const noexcept { return _pin_mask; }

    /// @brief Whether high or low levels trigger the wake.
    [[nodiscard]] constexpr deep_sleep_gpio_mode mode() const noexcept { return _mode; }

private:
    uint64_t _pin_mask;
    deep_sleep_gpio_mode _mode;
};
#endif // SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP

/// @cond INTERNAL
namespace detail {

[[nodiscard]] result<void> arm(const timer_wake& source);
void disarm(const timer_wake& source) noexcept;
[[nodiscard]] result<void> arm(const gpio_wake& source);
void disarm(const gpio_wake& source) noexcept;
#if SOC_PM_SUPPORT_EXT0_WAKEUP
[[nodiscard]] result<void> arm(const ext0_wake& source);
void disarm(const ext0_wake& source) noexcept;
#endif
#if SOC_PM_SUPPORT_EXT1_WAKEUP
[[nodiscard]] result<void> arm(const ext1_wake& source);
void disarm(const ext1_wake& source) noexcept;
#endif
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
[[nodiscard]] result<void> arm(const deep_sleep_gpio_wake& source);
void disarm(const deep_sleep_gpio_wake& source) noexcept;
#endif

[[nodiscard]] result<wakeup_source> do_light_sleep();
[[noreturn]] void do_deep_sleep() noexcept;

/// Arms each source in order, stopping at the first failure.
template<typename... Sources>
[[nodiscard]] result<void> arm_all(const Sources&... sources) {
    result<void> r{};
    ((r = arm(sources)) && ...);
    return r;
}

/// Disarms every source, best effort.
template<typename... Sources>
void disarm_all(const Sources&... sources) noexcept {
    (disarm(sources), ...);
}

} // namespace detail
/// @endcond

/**
 * @headerfile <idfxx/sleep>
 * @brief A wake-up source specification accepted by the sleep functions.
 *
 * Satisfied by @ref timer_wake, @ref gpio_wake, and — on chips that support
 * them — @ref ext0_wake, @ref ext1_wake, and @ref deep_sleep_gpio_wake.
 */
template<typename T>
concept wake_spec = requires(const std::remove_cvref_t<T>& source) {
    { detail::arm(source) } -> std::same_as<result<void>>;
    detail::disarm(source);
};

// =============================================================================
// Entering sleep
// =============================================================================

template<wake_spec... Sources>
[[nodiscard]] result<wakeup_source> try_light_sleep(const Sources&... sources);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Enters light sleep until one of the given wake-up sources triggers.
 *
 * Arms each source, sleeps, then disarms them again before returning. The
 * CPU is suspended and execution resumes here on wake. Peripheral and memory
 * state is retained, but digital interrupts (including GPIO edge interrupts)
 * are not delivered while sleeping — re-check any interrupt-driven state
 * after waking.
 *
 * With no arguments, sleeps on the wake-up sources previously armed with the
 * `enable_*` functions. Such sources stay armed afterwards, unless a source
 * of the same kind (or the same pin, for pin-level sources) is passed here,
 * in which case it is replaced for this sleep and disarmed on return.
 *
 * @tparam Sources Wake-up source specification types (see @ref wake_spec).
 * @param sources The sources that may wake the chip.
 * @return The source that woke the chip.
 *
 * @note At least one wake-up source must be armed, or sleep is rejected.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error if a source is invalid or sleep is rejected
 *         (e.g. a wake-up source triggered before sleep was entered).
 *
 * @code
 * auto cause = idfxx::sleep::light_sleep(
 *     idfxx::sleep::timer_wake{30s},
 *     idfxx::sleep::gpio_wake{button, idfxx::gpio::level::low});
 * if (cause == idfxx::sleep::wakeup_source::gpio) {
 *     // button pressed
 * }
 * @endcode
 */
template<wake_spec... Sources>
[[nodiscard]] wakeup_source light_sleep(const Sources&... sources) {
    return unwrap(try_light_sleep(sources...));
}
#endif

/**
 * @brief Enters light sleep until one of the given wake-up sources triggers.
 *
 * Arms each source, sleeps, then disarms them again before returning. The
 * CPU is suspended and execution resumes here on wake. Peripheral and memory
 * state is retained, but digital interrupts (including GPIO edge interrupts)
 * are not delivered while sleeping — re-check any interrupt-driven state
 * after waking.
 *
 * With no arguments, sleeps on the wake-up sources previously armed with the
 * `try_enable_*` functions. Such sources stay armed afterwards, unless a
 * source of the same kind (or the same pin, for pin-level sources) is passed
 * here, in which case it is replaced for this sleep and disarmed on return.
 *
 * @tparam Sources Wake-up source specification types (see @ref wake_spec).
 * @param sources The sources that may wake the chip.
 * @return The source that woke the chip, or an error.
 * @retval idfxx::errc::invalid_arg if a source is invalid (e.g. an
 *         unconnected pin, a negative duration, or a timer too short to
 *         enter sleep at all).
 * @retval idfxx::errc::invalid_state if sleep was rejected (e.g. a wake-up
 *         source triggered before sleep was entered).
 *
 * @note At least one wake-up source must be armed, or sleep is rejected.
 */
template<wake_spec... Sources>
[[nodiscard]] result<wakeup_source> try_light_sleep(const Sources&... sources) {
    if (auto armed = detail::arm_all(sources...); !armed) {
        detail::disarm_all(sources...);
        return error(armed.error());
    }
    auto cause = detail::do_light_sleep();
    detail::disarm_all(sources...);
    return cause;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Enters light sleep for a fixed duration.
 *
 * Equivalent to `light_sleep(timer_wake{duration})` — a low-power
 * counterpart to `std::this_thread::sleep_for`. Wake-up sources previously
 * armed with the `enable_*` functions may wake the chip sooner.
 *
 * @param duration Time to sleep before waking.
 * @return The source that woke the chip (normally
 *         @ref wakeup_source::timer).
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error if the duration is invalid or sleep is rejected.
 */
[[nodiscard]] wakeup_source light_sleep_for(std::chrono::microseconds duration);
#endif

/**
 * @brief Enters light sleep for a fixed duration.
 *
 * Equivalent to `try_light_sleep(timer_wake{duration})` — a low-power
 * counterpart to `std::this_thread::sleep_for`. Wake-up sources previously
 * armed with the `try_enable_*` functions may wake the chip sooner.
 *
 * @param duration Time to sleep before waking.
 * @return The source that woke the chip (normally @ref wakeup_source::timer),
 *         or an error.
 * @retval idfxx::errc::invalid_arg if the duration is negative or too short
 *         to enter sleep at all.
 * @retval idfxx::errc::invalid_state if sleep was rejected (e.g. a wake-up
 *         source triggered before sleep was entered).
 */
[[nodiscard]] result<wakeup_source> try_light_sleep_for(std::chrono::microseconds duration);

template<wake_spec First, wake_spec... Rest>
[[nodiscard]] result<void> try_deep_sleep(const First& first, const Rest&... rest);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Enters deep sleep until one of the given wake-up sources triggers.
 *
 * Arms each source, then powers down the CPU and most peripherals. Waking
 * restarts the application from the beginning; use @ref wakeup_cause to
 * detect the wake and its source after the restart.
 *
 * @tparam First First wake-up source specification type (see @ref wake_spec).
 * @tparam Rest  Additional wake-up source specification types.
 * @param first The source that may wake the chip.
 * @param rest  Additional sources that may wake the chip.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error if a source is invalid; otherwise does not
 *         return.
 *
 * @code
 * idfxx::sleep::deep_sleep(
 *     // The original ESP32 uses ext1_mode::all_low instead (see ext1_mode).
 *     idfxx::sleep::ext1_wake{{button}, idfxx::sleep::ext1_mode::any_low},
 *     idfxx::sleep::timer_wake{15min});
 * @endcode
 */
template<wake_spec First, wake_spec... Rest>
[[noreturn]] void deep_sleep(const First& first, const Rest&... rest) {
    unwrap(try_deep_sleep(first, rest...));
    detail::do_deep_sleep(); // unreachable: try_deep_sleep only returns on failure
}
#endif

/**
 * @brief Enters deep sleep until one of the given wake-up sources triggers.
 *
 * Arms each source, then powers down the CPU and most peripherals. Waking
 * restarts the application from the beginning; use @ref wakeup_cause to
 * detect the wake and its source after the restart.
 *
 * **Returns only on failure** — on success the chip is powered down and the
 * call never returns.
 *
 * @tparam First First wake-up source specification type (see @ref wake_spec).
 * @tparam Rest  Additional wake-up source specification types.
 * @param first The source that may wake the chip.
 * @param rest  Additional sources that may wake the chip.
 * @return The error that prevented entering sleep.
 * @retval idfxx::errc::invalid_arg if a source is invalid (e.g. an
 *         unconnected pin or a pin that cannot wake the chip from deep
 *         sleep).
 */
template<wake_spec First, wake_spec... Rest>
[[nodiscard]] result<void> try_deep_sleep(const First& first, const Rest&... rest) {
    if (auto armed = detail::arm_all(first, rest...); !armed) {
        detail::disarm_all(first, rest...);
        return error(armed.error());
    }
    detail::do_deep_sleep();
}

/**
 * @brief Enters deep sleep until a previously armed wake-up source triggers.
 *
 * Powers down the CPU and most peripherals, waking on the sources armed with
 * the `enable_*` functions. Waking restarts the application from the
 * beginning; use @ref wakeup_cause to detect the wake and its source after
 * the restart.
 *
 * Does not return: with no armed (or no functioning) wake-up source the chip
 * simply sleeps until reset.
 */
[[noreturn]] void deep_sleep() noexcept;

/**
 * @brief Enters deep sleep for a fixed duration.
 *
 * Equivalent to enabling a timer wake-up for @p duration and calling
 * @ref deep_sleep(). Any other previously armed wake-up sources remain
 * active and may wake the chip sooner.
 *
 * Powers down the CPU and most peripherals. Waking restarts the application
 * from the beginning; use @ref wakeup_cause to detect the wake and its source
 * after the restart.
 *
 * @param duration Time to sleep before waking. Negative values are treated as
 *                 zero.
 */
[[noreturn]] void deep_sleep(std::chrono::microseconds duration) noexcept;

// =============================================================================
// Status
// =============================================================================

/**
 * @brief Returns the cause of the most recent wake from sleep.
 *
 * After waking from deep sleep (which restarts the application), this
 * identifies the wake-up source. Returns `std::nullopt` when the chip did not
 * wake from sleep (e.g. cold boot or reset).
 *
 * @return The source that woke the chip, or `std::nullopt`.
 *
 * @note If several sources triggered in the same instant, the
 *       lowest-numbered one is reported.
 *
 * @code
 * if (auto cause = idfxx::sleep::wakeup_cause()) {
 *     // woke from sleep; *cause identifies the source
 * } else {
 *     // cold boot
 * }
 * @endcode
 */
[[nodiscard]] std::optional<wakeup_source> wakeup_cause() noexcept;

#if SOC_PM_SUPPORT_EXT1_WAKEUP
/**
 * @brief Returns the pins that triggered the most recent EXT1 wake.
 *
 * @return Bit mask of GPIO numbers (bit n set means GPIO n triggered the
 *         wake). Zero if the last wake was not caused by EXT1.
 *
 * @note Only available on chips with EXT1 wake-up support (`SOC_PM_SUPPORT_EXT1_WAKEUP`).
 */
[[nodiscard]] uint64_t ext1_wakeup_status() noexcept;
#endif

// =============================================================================
// Persistent wake-up source configuration
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Arms a wake-up source persistently.
 *
 * The source stays armed across any number of sleeps until disarmed with
 * @ref disable_wakeup_source. Use this instead of passing sources to the
 * sleep functions when the same sources are reused across many sleep cycles.
 *
 * @tparam Source Wake-up source specification type (see @ref wake_spec).
 * @param source The source that may wake the chip.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error if the source is invalid.
 */
template<wake_spec Source>
void enable_wakeup(const Source& source) {
    unwrap(detail::arm(source));
}
#endif

/**
 * @brief Arms a wake-up source persistently.
 *
 * The source stays armed across any number of sleeps until disarmed with
 * @ref try_disable_wakeup_source. Use this instead of passing sources to the
 * sleep functions when the same sources are reused across many sleep cycles.
 *
 * @tparam Source Wake-up source specification type (see @ref wake_spec).
 * @param source The source that may wake the chip.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg if the source is invalid (e.g. an
 *         unconnected pin or a negative duration).
 */
template<wake_spec Source>
[[nodiscard]] result<void> try_enable_wakeup(const Source& source) {
    return detail::arm(source);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Disarms a previously armed wake-up source.
 *
 * @param source The source to disarm.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void disable_wakeup_source(wakeup_source source);

/**
 * @brief Disarms every armed wake-up source.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void disable_all_wakeup_sources();
#endif

/**
 * @brief Disarms a previously armed wake-up source.
 *
 * @param source The source to disarm.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg if the source cannot be disarmed
 *         (@ref wakeup_source::other).
 * @retval idfxx::errc::invalid_state if the source was not armed.
 */
result<void> try_disable_wakeup_source(wakeup_source source);

/**
 * @brief Disarms every armed wake-up source.
 *
 * @return Success, or an error.
 */
result<void> try_disable_all_wakeup_sources();

} // namespace idfxx::sleep

/** @} */

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::sleep::wakeup_source> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::sleep::wakeup_source source, FormatContext& ctx) const {
        auto s = to_string(source);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
