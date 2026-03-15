// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/button>
 * @file button.hpp
 * @brief GPIO push-button driver with debounce and event detection.
 *
 * @defgroup idfxx_button Button Component
 * @brief GPIO push-button input with debounce, click, long press, and autorepeat support.
 *
 * Provides push-button input handling with configurable debounce, click detection,
 * long press detection, and autorepeat. Supports both polling and interrupt-driven modes.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin management.
 * @{
 */

#include <idfxx/error>
#include <idfxx/gpio>

#include <chrono>
#include <functional>

namespace idfxx {

/**
 * @headerfile <idfxx/button>
 * @brief GPIO push-button with debounce, click, long press, and autorepeat support.
 *
 * Handles push-button input events including press, release, click, long press,
 * and autorepeat. Delivers events via a callback configured at construction time.
 *
 * This type is non-copyable and move-only.
 */
class button {
public:
    /**
     * @headerfile <idfxx/button>
     * @brief Button detection mode.
     */
    enum class mode : int {
        poll = 0,      ///< Periodic timer polling (default)
        interrupt = 1, ///< GPIO interrupt with debounce timer
    };

    /**
     * @headerfile <idfxx/button>
     * @brief Button event type.
     *
     * When autorepeat is disabled, the event sequence for a short press is:
     *   pressed -> released -> clicked
     * and for a long press:
     *   pressed -> long_press -> released
     *
     * When autorepeat is enabled, long press detection is disabled. Instead,
     * holding the button generates repeated clicked events:
     *   pressed -> clicked -> clicked -> ... -> released
     */
    enum class event_type : int {
        pressed = 0,    ///< Button pressed
        released = 1,   ///< Button released
        clicked = 2,    ///< Short press completed (pressed then released)
        long_press = 3, ///< Button held beyond long-press threshold
    };

    /**
     * @headerfile <idfxx/button>
     * @brief Button configuration.
     *
     * All fields have sensible defaults, but you must set pin and callback at minimum.
     *
     * @code
     * idfxx::button btn({
     *     .pin = idfxx::gpio_4,
     *     .callback = [](idfxx::button::event_type ev) {
     *         if (ev == idfxx::button::event_type::clicked) {
     *             // handle click
     *         }
     *     },
     * });
     * @endcode
     */
    struct config {
        idfxx::gpio pin = gpio::nc();                                       ///< GPIO pin (required)
        enum mode mode = mode::poll;                                        ///< Detection mode
        gpio::level pressed_level = gpio::level::low;                       ///< GPIO level when pressed
        bool enable_pull = true;                                            ///< Enable internal pull resistor
        bool autorepeat = false;                                            ///< Enable autorepeat (disables long press)
        std::chrono::microseconds dead_time{std::chrono::milliseconds{50}}; ///< Debounce delay
        std::chrono::microseconds long_press_time{std::chrono::milliseconds{1000}};    ///< Long press threshold
        std::chrono::microseconds autorepeat_timeout{std::chrono::milliseconds{500}};  ///< Autorepeat start delay
        std::chrono::microseconds autorepeat_interval{std::chrono::milliseconds{250}}; ///< Autorepeat repeat interval
        std::chrono::microseconds poll_interval{std::chrono::milliseconds{10}};        ///< Polling interval
        std::move_only_function<void(event_type)> callback;                            ///< Event callback (required)
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a button and begins monitoring.
     *
     * @param cfg Button configuration. pin and callback must be set.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     * @throws std::system_error with errc::invalid_arg if pin is not connected, or callback is not set.
     */
    [[nodiscard]] explicit button(config cfg);
#endif

    /**
     * @brief Creates a button and begins monitoring.
     *
     * @param cfg Button configuration. pin and callback must be set.
     * @return The new button, or an error.
     * @retval invalid_arg pin is not connected, or callback is not set.
     */
    [[nodiscard]] static result<button> make(config cfg);

    /**
     * @brief Destroys the button.
     *
     * Stops monitoring and releases resources.
     */
    ~button();

    button(const button&) = delete;
    button& operator=(const button&) = delete;
    button(button&& other) noexcept;
    button& operator=(button&& other) noexcept;

private:
    explicit button(void* ctx);

    void _delete() noexcept;

    void* _context = nullptr;
};

/** @} */ // end of idfxx_button

} // namespace idfxx
