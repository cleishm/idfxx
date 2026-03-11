// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/rotary_encoder>
 * @file rotary_encoder.hpp
 * @brief Incremental rotary encoder driver with optional button support.
 *
 * @defgroup idfxx_rotary_encoder Rotary Encoder Component
 * @brief Incremental rotary encoder position tracking for ESP32.
 *
 * Provides rotary encoder position tracking with optional push-button support,
 * acceleration, and event-driven callbacks.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin management.
 * @{
 */

#include <idfxx/error>
#include <idfxx/gpio>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace idfxx {

/**
 * @headerfile <idfxx/rotary_encoder>
 * @brief Incremental rotary encoder with optional push-button support.
 *
 * Tracks rotary encoder shaft rotation and optional button presses, delivering
 * events via a callback. Supports acceleration for faster turning and
 * configurable debounce for button presses.
 *
 * This type is non-copyable and move-only.
 */
class rotary_encoder {
public:
    /**
     * @headerfile <idfxx/rotary_encoder>
     * @brief Rotary encoder event type.
     */
    enum class event_type : int {
        changed = 0,          ///< Encoder shaft rotated
        btn_released = 1,     ///< Button released
        btn_pressed = 2,      ///< Button pressed
        btn_long_pressed = 3, ///< Button held beyond long-press threshold
        btn_clicked = 4,      ///< Short press completed (pressed then released)
    };

    /**
     * @headerfile <idfxx/rotary_encoder>
     * @brief Rotary encoder event data.
     */
    struct event {
        event_type type; ///< Event type
        int32_t diff;    ///< Position change delta (only meaningful for event_type::changed)
    };

    /**
     * @headerfile <idfxx/rotary_encoder>
     * @brief Rotary encoder configuration.
     *
     * All fields have sensible defaults, but you must define pin_a, pin_b, and callback at minimum.
     *
     * The pull mode fields default to pullup as a convenience, but external pull-up resistors are recommended.
     * When using external pull-ups, set the pull mode fields to gpio::pull_mode::floating to disable internal
     * pull resistors, or to std::nullopt if the pins have already been configured.
     *
     * @code
     * idfxx::rotary_encoder encoder({
     *     .pin_a = idfxx::gpio_4,
     *     .pin_b = idfxx::gpio_5,
     *     .callback = [](const idfxx::rotary_encoder::event& ev) {
     *         // handle event
     *     },
     * });
     * @endcode
     */
    struct config {
        idfxx::gpio pin_a = gpio::nc(); ///< Encoder pin A (required)
        idfxx::gpio pin_b = gpio::nc(); ///< Encoder pin B (required)
        /// Pull mode for encoder pins A and B, or std::nullopt to leave unchanged.
        std::optional<gpio::pull_mode> encoder_pins_pull_mode = gpio::pull_mode::pullup;
        idfxx::gpio pin_btn = gpio::nc();                ///< Button pin, or gpio::nc() if unused
        gpio::level btn_active_level = gpio::level::low; ///< GPIO level when button is pressed
        /// Pull mode for button pin, or std::nullopt to leave unchanged.
        std::optional<gpio::pull_mode> btn_pin_pull_mode = gpio::pull_mode::pullup;
        std::chrono::microseconds btn_dead_time{std::chrono::milliseconds{10}};        ///< Button debounce time
        std::chrono::microseconds btn_long_press_time{std::chrono::milliseconds{500}}; ///< Long press threshold
        std::chrono::milliseconds acceleration_threshold{200}; ///< Acceleration starts below this interval
        std::chrono::milliseconds acceleration_cap{4};         ///< Minimum interval (limits max acceleration)
        std::chrono::microseconds polling_interval{std::chrono::milliseconds{1}}; ///< GPIO polling interval
        std::move_only_function<void(const event&)> callback;                     ///< Event callback (required)
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a rotary encoder and begins tracking.
     *
     * @param cfg Encoder configuration. pin_a, pin_b, and callback must be set. Pull mode fields default to
     *            pullup as a convenience; set to gpio::pull_mode::floating when using external pull-ups
     *            (recommended), or std::nullopt if the pins have already been configured.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     * @throws std::system_error with errc::invalid_arg if pin_a or pin_b is not connected, or callback is not set.
     */
    [[nodiscard]] explicit rotary_encoder(config cfg);
#endif

    /**
     * @brief Creates a rotary encoder and begins tracking.
     *
     * @param cfg Encoder configuration. pin_a, pin_b, and callback must be set. Pull mode fields default to
     *            pullup as a convenience; set to gpio::pull_mode::floating when using external pull-ups
     *            (recommended), or std::nullopt if the pins have already been configured.
     * @return The new rotary encoder, or an error.
     * @retval invalid_arg pin_a or pin_b is not connected, or callback is not set.
     */
    [[nodiscard]] static result<rotary_encoder> make(config cfg);

    /**
     * @brief Enables acceleration on the encoder.
     *
     * When enabled, faster turning produces larger position deltas. The
     * acceleration coefficient controls the sensitivity.
     *
     * Has no effect on a moved-from encoder.
     *
     * @param coeff Acceleration coefficient. Higher values increase acceleration.
     */
    void enable_acceleration(uint16_t coeff);

    /**
     * @brief Disables acceleration on the encoder.
     *
     * Position deltas return to reporting single steps regardless of turning speed.
     *
     * Has no effect on a moved-from encoder.
     */
    void disable_acceleration();

    /**
     * @brief Destroys the encoder.
     *
     * Stops tracking and releases resources.
     */
    ~rotary_encoder();

    rotary_encoder(const rotary_encoder&) = delete;
    rotary_encoder& operator=(const rotary_encoder&) = delete;
    rotary_encoder(rotary_encoder&& other) noexcept;
    rotary_encoder& operator=(rotary_encoder&& other) noexcept;

private:
    explicit rotary_encoder(void* ctx);

    void _delete() noexcept;

    void* _context = nullptr;
};

/** @} */ // end of idfxx_rotary_encoder

} // namespace idfxx
