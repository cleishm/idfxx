// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/gpio>
 * @file gpio.hpp
 * @brief GPIO pin management class.
 *
 * @defgroup idfxx_gpio GPIO Component
 * @brief Type-safe GPIO pin management for ESP32.
 *
 * Provides type-safe GPIO pin management with compile-time validation
 * and ISR handler management supporting multiple handlers per pin.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include <idfxx/error>
#include <idfxx/flags>
#include <idfxx/intr_alloc>

#include <driver/gpio.h>
#include <functional>
#include <string>

namespace idfxx {

/** @cond INTERNAL */
template<int N>
struct gpio_constant;
/** @endcond */

/**
 * @headerfile <idfxx/gpio>
 * @brief A GPIO pin.
 *
 * Lightweight, non-owning GPIO pin identifier. Provides type-safe
 * configuration and supports both exception-throwing and result-returning error
 * handling.
 *
 * Construction is validated: only valid GPIO numbers (or nc) are allowed.
 * Use gpio::make() for result-based construction, or the throwing constructor
 * when exceptions are enabled.
 *
 * For interrupt handling, first install the ISR service, then add handlers:
 * @code
 * idfxx::gpio::install_isr_service();
 * auto button = idfxx::gpio_28;
 * button.set_direction(idfxx::gpio::mode::input);
 * button.set_intr_type(idfxx::gpio::intr_type::negedge);
 * auto handle = button.isr_handler_add([]() { ... });
 * button.intr_enable();
 * @endcode
 */
class gpio {
    template<int N>
    friend struct gpio_constant;

public:
    /** @brief GPIO direction mode. */
    enum class mode : int {
        disable = GPIO_MODE_DISABLE,                 ///< GPIO mode : disable input and output
        input = GPIO_MODE_INPUT,                     ///< GPIO mode : input only
        output = GPIO_MODE_OUTPUT,                   ///< GPIO mode : output only mode
        output_od = GPIO_MODE_OUTPUT_OD,             ///< GPIO mode : output only with open-drain mode
        input_output_od = GPIO_MODE_INPUT_OUTPUT_OD, ///< GPIO mode : output and input with open-drain mode
        input_output = GPIO_MODE_INPUT_OUTPUT,       ///< GPIO mode : output and input mode
    };

    /** @brief Pull resistor configuration. */
    enum class pull_mode : int {
        pullup = GPIO_PULLUP_ONLY,              ///< Pin pull up
        pulldown = GPIO_PULLDOWN_ONLY,          ///< Pin pull down
        pullup_pulldown = GPIO_PULLUP_PULLDOWN, ///< Pin pull up + pull down
        floating = GPIO_FLOATING,               ///< Pin floating
    };

    /** @brief Pin drive capability (output strength). */
    enum class drive_cap : int {
        cap_0 = GPIO_DRIVE_CAP_0,             ///< Pin drive capability: weak
        cap_1 = GPIO_DRIVE_CAP_1,             ///< Pin drive capability: stronger
        cap_2 = GPIO_DRIVE_CAP_2,             ///< Pin drive capability: medium
        cap_default = GPIO_DRIVE_CAP_DEFAULT, ///< Pin drive capability: medium
        cap_3 = GPIO_DRIVE_CAP_3,             ///< Pin drive capability: strongest
    };

    /** @brief Interrupt trigger type. */
    enum class intr_type : int {
        disable = GPIO_INTR_DISABLE,       ///< Disable GPIO interrupt
        posedge = GPIO_INTR_POSEDGE,       ///< GPIO interrupt type : rising edge
        negedge = GPIO_INTR_NEGEDGE,       ///< GPIO interrupt type : falling edge
        anyedge = GPIO_INTR_ANYEDGE,       ///< GPIO interrupt type : both rising and falling edge
        low_level = GPIO_INTR_LOW_LEVEL,   ///< GPIO interrupt type : input low level trigger
        high_level = GPIO_INTR_HIGH_LEVEL, ///< GPIO interrupt type : input high level trigger
    };

#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
    /** @brief Hysteresis control mode. */
    enum class hys_ctrl_mode : int {
#if SOC_GPIO_SUPPORT_PIN_HYS_CTRL_BY_EFUSE
        efuse = GPIO_HYS_CTRL_EFUSE, ///< Pin input hysteresis ctrl by efuse
#endif
        soft_disable = GPIO_HYS_CTRL_SOFT_DISABLE, ///< Pin input hysteresis disable by software
        soft_enable = GPIO_HYS_CTRL_SOFT_ENABLE,   ///< Pin input hysteresis enable by software
    };
#endif

    class unique_isr_handle;

    /**
     * @brief Handle to a registered ISR handler.
     *
     * Returned by isr_handler_add() and used to remove the handler later.
     *
     * @note Unlike most idfxx types, this handle does not provide RAII semantics. It can be
     * copied and will not disable the ISR when it goes out of scope. Use isr_handler_remove()
     * to explicitly remove the handler, or convert to unique_isr_handle for RAII semantics.
     */
    class isr_handle {
        friend class gpio;
        friend class unique_isr_handle;
        constexpr isr_handle(gpio_num_t num, uint32_t id)
            : _num(num)
            , _id(id) {}
        gpio_num_t _num;
        uint32_t _id;
    };

    /**
     * @brief RAII handle for ISR registration that removes the handler on destruction.
     *
     * Unlike isr_handle, this class provides RAII semantics: when the unique_isr_handle
     * is destroyed, the ISR handler is automatically removed.
     *
     * This class is move-only. Use release() to transfer ownership back to a
     * non-RAII isr_handle if needed.
     */
    class unique_isr_handle {
    public:
        /**
         * @brief Constructs an empty unique_isr_handle.
         *
         * Represents no ownership of any ISR handler.
         */
        explicit unique_isr_handle() noexcept
            : _num(GPIO_NUM_NC)
            , _id(0) {}

        /**
         * @brief Constructs from an isr_handle, taking ownership.
         * @param handle The isr_handle to take ownership of.
         */
        explicit unique_isr_handle(isr_handle handle) noexcept
            : _num(handle._num)
            , _id(handle._id) {}

        /**
         * @brief Move constructor.
         * @param other The handle to move from.
         */
        unique_isr_handle(unique_isr_handle&& other) noexcept
            : _num(other._num)
            , _id(other._id) {
            other._num = GPIO_NUM_NC;
        }

        /**
         * @brief Move assignment operator.
         * @param other The handle to move from.
         * @return Reference to this handle.
         */
        unique_isr_handle& operator=(unique_isr_handle&& other) noexcept {
            if (this != &other) {
                if (_num != GPIO_NUM_NC) {
                    gpio{_num}.try_isr_handler_remove(isr_handle{_num, _id});
                }
                _num = other._num;
                _id = other._id;
                other._num = GPIO_NUM_NC;
            }
            return *this;
        }

        unique_isr_handle(const unique_isr_handle&) = delete;
        unique_isr_handle& operator=(const unique_isr_handle&) = delete;

        /**
         * @brief Destructor. Removes the ISR handler if still owned.
         */
        ~unique_isr_handle() {
            if (_num != GPIO_NUM_NC) {
                // Removing the ISR handler will not fail, so we can ignore the result
                gpio{_num}.try_isr_handler_remove(isr_handle{_num, _id});
            }
        }

        /**
         * @brief Releases ownership of the handle without removing the ISR.
         * @return The underlying isr_handle.
         */
        [[nodiscard]] isr_handle release() noexcept {
            auto handle = isr_handle{_num, _id};
            _num = GPIO_NUM_NC;
            return handle;
        }

    private:
        gpio_num_t _num;
        uint32_t _id;
    };

    /** @brief Configuration parameters for idfxx::gpio_config */
    struct config {
        enum mode mode = mode::disable;
        enum pull_mode pull_mode = pull_mode::floating;
        enum intr_type intr_type = intr_type::disable;
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
        enum hys_ctrl_mode hys_ctrl_mode = hys_ctrl_mode::soft_disable;
#endif
    };

public:
    /** @brief Constructs a GPIO representing "not connected". */
    constexpr gpio()
        : _num(GPIO_NUM_NC) {}

    /**
     * @brief Creates a validated GPIO pin.
     * @param num GPIO pin number.
     * @return A gpio object or an error if the pin number is invalid.
     */
    [[nodiscard]] static result<gpio> make(int num);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Constructs a validated GPIO pin.
     * @param num GPIO pin number.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the pin number is invalid.
     */
    [[nodiscard]] explicit gpio(int num);
#endif

    /** @brief Returns a GPIO representing "not connected". */
    [[nodiscard]] static constexpr gpio nc() { return gpio{GPIO_NUM_NC}; }

    /** @brief Returns a GPIO for the highest valid pin number. */
    [[nodiscard]] static constexpr gpio max() { return gpio{static_cast<gpio_num_t>(GPIO_NUM_MAX - 1)}; }

    // Copyable and movable
    gpio(const gpio&) = default;
    gpio(gpio&&) = default;
    gpio& operator=(const gpio&) = default;
    gpio& operator=(gpio&&) = default;

    constexpr bool operator==(const gpio& other) const = default;

    /** @brief Returns true if this is a valid GPIO pin. */
    [[nodiscard]] constexpr bool is_connected() const {
        return _num != GPIO_NUM_NC && _num >= 0 && _num < GPIO_NUM_MAX && GPIO_IS_VALID_GPIO(_num);
    }

    /** @brief Returns true if this gpio supports output mode. */
    [[nodiscard]] constexpr bool is_output_capable() const { return GPIO_IS_VALID_OUTPUT_GPIO(_num); }

    /** @brief Returns true if this gpio is a valid digital I/O pin. */
    [[nodiscard]] constexpr bool is_digital_io_pin_capable() const { return GPIO_IS_VALID_DIGITAL_IO_PAD(_num); }

    /** @brief Returns the underlying GPIO pin number. */
    [[nodiscard]] constexpr int num() const { return static_cast<int>(_num); }

    /** @brief Returns the underlying ESP-IDF GPIO number. */
    [[nodiscard]] constexpr gpio_num_t idf_num() const { return _num; }

    /**
     * @brief Resets the gpio to default state.
     *
     * Selects GPIO function, enables pullup, and disables input and output.
     *
     * @note Does nothing if called on an invalid gpio (e.g. gpio::nc()).
     */
    void reset() {
        if (!is_connected()) {
            return;
        }
        gpio_reset_pin(_num);
    }

    // Direction and pull configuration
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the GPIO direction mode.
     * @note This always overwrites all current modes applied to the gpio.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_direction(enum mode mode) { unwrap(try_set_direction(mode)); }
    /**
     * @brief Enables input on this gpio.
     * @note Only fails if called on gpio::nc().
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void input_enable() { unwrap(try_input_enable()); }
    /**
     * @brief Sets the pull resistor mode.
     * @note On ESP32, only GPIOs that support both input & output have integrated
     *       pull-up and pull-down resistors. Input-only GPIOs 34-39 do not.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_pull_mode(enum pull_mode mode) { unwrap(try_set_pull_mode(mode)); }
    /**
     * @brief Enables the internal pull-up resistor.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void pullup_enable() { unwrap(try_pullup_enable()); }
    /**
     * @brief Disables the internal pull-up resistor.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void pullup_disable() { unwrap(try_pullup_disable()); }
    /**
     * @brief Enables the internal pull-down resistor.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void pulldown_enable() { unwrap(try_pulldown_enable()); }
    /**
     * @brief Disables the internal pull-down resistor.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void pulldown_disable() { unwrap(try_pulldown_disable()); }
#endif
    /**
     * @brief Sets the GPIO direction mode.
     * @note This always overwrites all current modes applied to the gpio.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the gpio is input-only and an output mode is requested.
     */
    result<void> try_set_direction(enum mode mode) {
        return guarded([&] { return gpio_set_direction(_num, static_cast<gpio_mode_t>(mode)); });
    }
    /**
     * @brief Enables input on this gpio.
     * @retval invalid_state If called on gpio::nc().
     * @note Only fails if called on gpio::nc().
     */
    result<void> try_input_enable() {
        return guarded([&] { return gpio_input_enable(_num); });
    }
    /**
     * @brief Sets the pull resistor mode.
     * @note On ESP32, only GPIOs that support both input & output have integrated
     *       pull-up and pull-down resistors. Input-only GPIOs 34-39 do not.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the GPIO doesn't support pull resistors.
     */
    result<void> try_set_pull_mode(enum pull_mode mode) {
        return guarded([&] { return gpio_set_pull_mode(_num, static_cast<gpio_pull_mode_t>(mode)); });
    }
    /**
     * @brief Enables the internal pull-up resistor.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the GPIO doesn't support pull resistors.
     */
    result<void> try_pullup_enable() {
        return guarded([&] { return gpio_pullup_en(_num); });
    }
    /**
     * @brief Disables the internal pull-up resistor.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the GPIO doesn't support pull resistors.
     */
    result<void> try_pullup_disable() {
        return guarded([&] { return gpio_pullup_dis(_num); });
    }
    /**
     * @brief Enables the internal pull-down resistor.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the GPIO doesn't support pull resistors.
     */
    result<void> try_pulldown_enable() {
        return guarded([&] { return gpio_pulldown_en(_num); });
    }
    /**
     * @brief Disables the internal pull-down resistor.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the GPIO doesn't support pull resistors.
     */
    result<void> try_pulldown_disable() {
        return guarded([&] { return gpio_pulldown_dis(_num); });
    }

    // Level (input/output)
    /**
     * @brief Sets the output level.
     * @note Does nothing if the gpio is not configured for output.
     */
    void set_level(bool level) { gpio_set_level(_num, level ? 1 : 0); }
    /**
     * @brief Reads the current input level.
     * @warning If the gpio is not configured for input, the returned value is always false.
     * @note Returns false for gpio::nc().
     */
    [[nodiscard]] bool get_level() const { return gpio_get_level(_num) != 0; }

    // Drive capability
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the gpio drive capability.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_drive_capability(enum drive_cap strength) { unwrap(try_set_drive_capability(strength)); }
    /**
     * @brief Gets the current drive capability.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] enum drive_cap get_drive_capability() const { return unwrap(try_get_drive_capability()); }
#endif
    /**
     * @brief Sets the gpio drive capability.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If called on a gpio that is not output-capable.
     */
    result<void> try_set_drive_capability(enum drive_cap strength) {
        return guarded([&] { return gpio_set_drive_capability(_num, static_cast<gpio_drive_cap_t>(strength)); });
    }
    /**
     * @brief Gets the current drive capability.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If called on a gpio that is not output-capable.
     */
    result<drive_cap> try_get_drive_capability() const {
        if (!is_connected()) {
            return error(errc::invalid_state);
        }
        gpio_drive_cap_t cap;
        auto err = gpio_get_drive_capability(_num, &cap);
        return wrap(err).transform([cap]() { return static_cast<drive_cap>(cap); });
    }

    // ISR service (must be installed before adding handlers)
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Installs the GPIO ISR service for per-gpio interrupt handlers.
     *
     * Must be called before adding handlers with isr_handler_add().
     *
     * @param intr_alloc_flags Flags for interrupt allocation (ESP_INTR_FLAG_*).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void install_isr_service(flags<intr_flag> intr_alloc_flags = {}) {
        unwrap(try_install_isr_service(intr_alloc_flags));
    }
#endif
    /**
     * @brief Installs the GPIO ISR service for per-gpio interrupt handlers.
     *
     * Must be called before adding handlers with isr_handler_add().
     *
     * @param intr_flags Flags for interrupt allocation (ESP_INTR_FLAG_*).
     */
    [[nodiscard]] static result<void> try_install_isr_service(flags<intr_flag> intr_flags = {});
    /**
     * @brief Uninstalls the GPIO ISR service, freeing related resources.
     *
     * All registered ISR handlers are removed and internal state is reset.
     */
    static void uninstall_isr_service();

    // ISR handlers
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Adds an ISR handler for this gpio.
     *
     * The handler will be called from an ISR context. ISR handlers do not need
     * IRAM_ATTR unless ESP_INTR_FLAG_IRAM was passed to install_isr_service().
     *
     * @warning This overload cannot be used if ESP_INTR_FLAG_IRAM was passed to
     *          install_isr_service(), as std::move_only_function may allocate
     *          memory outside of IRAM. Registration will fail with not_supported.
     *          Use the raw function pointer overload instead.
     *
     * @pre install_isr_service() must have been called.
     * @param handler Function to call on interrupt.
     * @return Handle for removing this specific handler.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including not_supported if the ISR
     *         service was installed with ESP_INTR_FLAG_IRAM.
     */
    isr_handle isr_handler_add(std::move_only_function<void() const> handler) {
        return unwrap(try_isr_handler_add(std::move(handler)));
    }
    /**
     * @brief Adds an ISR handler for this gpio.
     *
     * The handler will be called from an ISR context. ISR handlers do not need
     * IRAM_ATTR unless ESP_INTR_FLAG_IRAM was passed to install_isr_service().
     *
     * @pre install_isr_service() must have been called.
     * @param fn Function to call on interrupt.
     * @param arg Argument passed to the handler.
     * @return Handle for removing this specific handler.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    isr_handle isr_handler_add(void (*fn)(void*), void* arg) { return unwrap(try_isr_handler_add(fn, arg)); }
    /**
     * @brief Removes a specific ISR handler.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void isr_handler_remove(isr_handle handle) { unwrap(try_isr_handler_remove(handle)); }
    /**
     * @brief Removes all ISR handlers for this gpio.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void isr_handler_remove_all() { unwrap(try_isr_handler_remove_all()); }
#endif
    /**
     * @brief Adds an ISR handler for this gpio.
     *
     * The handler will be called from an ISR context. ISR handlers do not need
     * IRAM_ATTR unless ESP_INTR_FLAG_IRAM was passed to install_isr_service().
     *
     * @warning This overload cannot be used if ESP_INTR_FLAG_IRAM was passed to
     *          install_isr_service(), as std::move_only_function may allocate
     *          memory outside of IRAM. Registration will fail with not_supported.
     *          Use the raw function pointer overload instead.
     *
     * @pre install_isr_service() must have been called.
     * @param handler Function to call on interrupt.
     * @return Handle for removing this specific handler.
     * @retval not_supported If the ISR service was installed with ESP_INTR_FLAG_IRAM.
     */
    result<isr_handle> try_isr_handler_add(std::move_only_function<void() const> handler);
    /**
     * @brief Adds an ISR handler for this gpio.
     *
     * The handler will be called from an ISR context. ISR handlers do not need
     * IRAM_ATTR unless ESP_INTR_FLAG_IRAM was passed to install_isr_service().
     *
     * @pre install_isr_service() must have been called.
     * @param fn Function to call on interrupt.
     * @param arg Argument passed to the handler.
     * @return Handle for removing this specific handler.
     * @note Only fails if called on gpio::nc() or the ISR service is not installed.
     */
    result<isr_handle> try_isr_handler_add(void (*fn)(void*), void* arg);
    /**
     * @brief Removes a specific ISR handler.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the handle is not valid for this gpio.
     */
    result<void> try_isr_handler_remove(isr_handle handle);
    /**
     * @brief Removes all ISR handlers for this gpio.
     * @retval invalid_state If called on gpio::nc().
     * @note Only fails if called on gpio::nc().
     */
    result<void> try_isr_handler_remove_all();

    // Interrupt configuration
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the interrupt trigger type.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_intr_type(enum intr_type intr_type) { unwrap(try_set_intr_type(intr_type)); }
    /**
     * @brief Enables interrupts for this gpio.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void intr_enable() { unwrap(try_intr_enable()); }
    /**
     * @brief Disables interrupts for this gpio.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void intr_disable() { unwrap(try_intr_disable()); }
#endif
    /**
     * @brief Sets the interrupt trigger type.
     * @retval invalid_state If called on gpio::nc().
     * @note Only fails if called on gpio::nc().
     */
    result<void> try_set_intr_type(enum intr_type intr_type) {
        return guarded(gpio_set_intr_type, _num, static_cast<gpio_int_type_t>(intr_type));
    }
    /**
     * @brief Enables interrupts for this gpio.
     * @retval invalid_state If called on gpio::nc().
     * @note Only fails if called on gpio::nc().
     */
    result<void> try_intr_enable() { return guarded(gpio_intr_enable, _num); }
    /**
     * @brief Disables interrupts for this gpio.
     * @retval invalid_state If called on gpio::nc().
     * @note Only fails if called on gpio::nc().
     */
    result<void> try_intr_disable() { return guarded(gpio_intr_disable, _num); }

    // Wakeup configuration
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Enables GPIO wake-up from light sleep.
     * @param intr_type Only low_level or high_level can be used.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void wakeup_enable(enum intr_type intr_type) { unwrap(try_wakeup_enable(intr_type)); }
    /**
     * @brief Disables GPIO wake-up.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void wakeup_disable() { unwrap(try_wakeup_disable()); }
#endif
    /**
     * @brief Enables GPIO wake-up from light sleep.
     * @param intr_type Only low_level or high_level can be used.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If intr_type is not low_level or high_level.
     */
    result<void> try_wakeup_enable(enum intr_type intr_type) {
        return guarded(gpio_wakeup_enable, _num, static_cast<gpio_int_type_t>(intr_type));
    }
    /**
     * @brief Disables GPIO wake-up.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the GPIO is not an RTC GPIO.
     */
    result<void> try_wakeup_disable() { return guarded(gpio_wakeup_disable, _num); }

    // Hold configuration (maintains state during sleep/reset)
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Enables gpio hold function.
     *
     * When enabled, the gpio's state is latched and will not change when the internal
     * signal or IO MUX/GPIO configuration is modified. Use this to retain GPIO state
     * during sleep or reset. Only applicable to output-capable GPIOs.
     *
     * @note On ESP32/S2/C3/S3/C2, this cannot hold digital GPIO state during deep sleep.
     *       Use deep_sleep_hold_enable() for that purpose.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void hold_enable() { unwrap(try_hold_enable()); }
    /**
     * @brief Disables gpio hold function.
     *
     * @warning After waking from sleep, the GPIO will be set to default mode.
     *          Configure the GPIO to the desired state before calling this.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void hold_disable() { unwrap(try_hold_disable()); }
#endif
    /**
     * @brief Enables gpio hold function.
     *
     * When enabled, the gpio's state is latched and will not change when the internal
     * signal or IO MUX/GPIO configuration is modified. Use this to retain GPIO state
     * during sleep or reset. Only applicable to output-capable GPIOs.
     *
     * @note On ESP32/S2/C3/S3/C2, this cannot hold digital GPIO state during deep sleep.
     *       Use deep_sleep_hold_enable() for that purpose.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the GPIO doesn't support hold.
     */
    result<void> try_hold_enable() { return guarded(gpio_hold_en, _num); }
    /**
     * @brief Disables gpio hold function.
     *
     * @warning After waking from sleep, the GPIO will be set to default mode.
     *          Configure the GPIO to the desired state before calling this.
     * @retval invalid_state If called on gpio::nc().
     * @retval not_supported If the GPIO doesn't support hold.
     */
    result<void> try_hold_disable() { return guarded(gpio_hold_dis, _num); }
    /**
     * @brief Enables hold for all digital GPIOs during deep sleep.
     *
     * The hold only takes effect during deep sleep. In active mode, GPIO state can
     * still be changed. Each gpio must also have hold_enable() called for this to work.
     */
    static void deep_sleep_hold_enable() { gpio_deep_sleep_hold_en(); }
    /** @brief Disables hold for all digital GPIOs during deep sleep. */
    static void deep_sleep_hold_disable() { gpio_deep_sleep_hold_dis(); }

    // Sleep mode configuration
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Enables SLP_SEL to change GPIO status automatically in light sleep.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void sleep_sel_enable() { unwrap(try_sleep_sel_enable()); }
    /**
     * @brief Disables SLP_SEL to change GPIO status automatically in light sleep.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void sleep_sel_disable() { unwrap(try_sleep_sel_disable()); }
    /**
     * @brief Sets GPIO direction at sleep.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void sleep_set_direction(enum mode mode) { unwrap(try_sleep_set_direction(mode)); }
    /**
     * @brief Sets pull resistor mode at sleep.
     * @note On ESP32, only GPIOs that support both input & output have integrated
     *       pull-up and pull-down resistors. Input-only GPIOs 34-39 do not.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void sleep_set_pull_mode(enum pull_mode pull) { unwrap(try_sleep_set_pull_mode(pull)); }
#endif
    /**
     * @brief Enables SLP_SEL to change GPIO status automatically in light sleep.
     * @note Only fails if called on gpio::nc().
     */
    result<void> try_sleep_sel_enable() { return guarded(gpio_sleep_sel_en, _num); }
    /**
     * @brief Disables SLP_SEL to change GPIO status automatically in light sleep.
     * @note Only fails if called on gpio::nc().
     */
    result<void> try_sleep_sel_disable() { return guarded(gpio_sleep_sel_dis, _num); }
    /**
     * @brief Sets GPIO direction at sleep.
     * @retval invalid_state If called on gpio::nc().
     * @retval invalid_arg If the gpio is input-only and an output mode is requested.
     */
    result<void> try_sleep_set_direction(enum mode mode) {
        return guarded(gpio_sleep_set_direction, _num, static_cast<gpio_mode_t>(mode));
    }
    /**
     * @brief Sets pull resistor mode at sleep.
     * @note On ESP32, only GPIOs that support both input & output have integrated
     *       pull-up and pull-down resistors. Input-only GPIOs 34-39 do not.
     * @retval invalid_state If called on gpio::nc().
     * @note Only fails if called on gpio::nc().
     */
    result<void> try_sleep_set_pull_mode(enum pull_mode pull) {
        return guarded(gpio_sleep_set_pull_mode, _num, static_cast<gpio_pull_mode_t>(pull));
    }

private:
    constexpr gpio(gpio_num_t num)
        : _num(num) {}

    template<typename F, typename... Args>
    result<void> guarded(F&& f, Args&&... args) const {
        if (!is_connected()) {
            return error(errc::invalid_state);
        }
        return wrap(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
    }

    gpio_num_t _num;
};

/**
 * @headerfile <idfxx/gpio>
 * @brief Configures multiple GPIOs with the same settings.
 *
 * @param gpios Vector of GPIOs to configure.
 * @param cfg Configuration parameters.
 * @retval invalid_state If any gpio is gpio::nc().
 * @retval invalid_arg If any gpio is input-only and an output mode is requested.
 */
[[nodiscard]] result<void> try_configure_gpios(const gpio::config& cfg, std::vector<gpio> gpios);
/**
 * @headerfile <idfxx/gpio>
 * @brief Configures multiple GPIOs with the same settings.
 *
 * @param gpios GPIOs to configure.
 * @param cfg Configuration parameters.
 * @retval invalid_state If any gpio is gpio::nc().
 * @retval invalid_arg If any gpio is input-only and an output mode is requested.
 */
template<typename... Gpios>
[[nodiscard]] result<void> try_configure_gpios(const gpio::config& cfg, Gpios&&... gpios) {
    return try_configure_gpios(cfg, std::vector<gpio>{std::forward<Gpios>(gpios)...});
}
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @headerfile <idfxx/gpio>
 * @brief Configures multiple GPIOs with the same settings.
 *
 * @param gpios Vector of GPIOs to configure.
 * @param cfg Configuration parameters.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
inline void configure_gpios(const gpio::config& cfg, std::vector<gpio> gpios) {
    unwrap(try_configure_gpios(cfg, std::move(gpios)));
}
/**
 * @headerfile <idfxx/gpio>
 * @brief Configures multiple GPIOs with the same settings.
 *
 * @param gpios GPIOs to configure.
 * @param cfg Configuration parameters.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
template<typename... Gpios>
void configure_gpios(const gpio::config& cfg, Gpios&&... gpios) {
    unwrap(try_configure_gpios(cfg, std::vector<gpio>{std::forward<Gpios>(gpios)...}));
}
#endif

/** @cond INTERNAL */
/* Validates at compile time that the GPIO number is valid.
 * Used internally to create the predefined GPIO constants.
 */
template<int N>
struct gpio_constant {
    static_assert(N == GPIO_NUM_NC || (SOC_GPIO_VALID_GPIO_MASK & (1ULL << N)), "Invalid GPIO number");
    static constexpr gpio value{static_cast<gpio_num_t>(N)};
};
/** @endcond */

/**
 * @headerfile <idfxx/gpio>
 * @name GPIO Constants
 * @brief Predefined GPIO instances for direct use.
 *
 * Available GPIOs depend on the target chip.
 * @{
 */

inline constexpr gpio gpio_nc = gpio_constant<GPIO_NUM_NC>::value;
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 0))
inline constexpr gpio gpio_0 = gpio_constant<0>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 1))
inline constexpr gpio gpio_1 = gpio_constant<1>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 2))
inline constexpr gpio gpio_2 = gpio_constant<2>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 3))
inline constexpr gpio gpio_3 = gpio_constant<3>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 4))
inline constexpr gpio gpio_4 = gpio_constant<4>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 5))
inline constexpr gpio gpio_5 = gpio_constant<5>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 6))
inline constexpr gpio gpio_6 = gpio_constant<6>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 7))
inline constexpr gpio gpio_7 = gpio_constant<7>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 8))
inline constexpr gpio gpio_8 = gpio_constant<8>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 9))
inline constexpr gpio gpio_9 = gpio_constant<9>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 10))
inline constexpr gpio gpio_10 = gpio_constant<10>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 11))
inline constexpr gpio gpio_11 = gpio_constant<11>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 12))
inline constexpr gpio gpio_12 = gpio_constant<12>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 13))
inline constexpr gpio gpio_13 = gpio_constant<13>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 14))
inline constexpr gpio gpio_14 = gpio_constant<14>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 15))
inline constexpr gpio gpio_15 = gpio_constant<15>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 16))
inline constexpr gpio gpio_16 = gpio_constant<16>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 17))
inline constexpr gpio gpio_17 = gpio_constant<17>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 18))
inline constexpr gpio gpio_18 = gpio_constant<18>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 19))
inline constexpr gpio gpio_19 = gpio_constant<19>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 20))
inline constexpr gpio gpio_20 = gpio_constant<20>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 21))
inline constexpr gpio gpio_21 = gpio_constant<21>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 22))
inline constexpr gpio gpio_22 = gpio_constant<22>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 23))
inline constexpr gpio gpio_23 = gpio_constant<23>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 24))
inline constexpr gpio gpio_24 = gpio_constant<24>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 25))
inline constexpr gpio gpio_25 = gpio_constant<25>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 26))
inline constexpr gpio gpio_26 = gpio_constant<26>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 27))
inline constexpr gpio gpio_27 = gpio_constant<27>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 28))
inline constexpr gpio gpio_28 = gpio_constant<28>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 29))
inline constexpr gpio gpio_29 = gpio_constant<29>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 30))
inline constexpr gpio gpio_30 = gpio_constant<30>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 31))
inline constexpr gpio gpio_31 = gpio_constant<31>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 32))
inline constexpr gpio gpio_32 = gpio_constant<32>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 33))
inline constexpr gpio gpio_33 = gpio_constant<33>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 34))
inline constexpr gpio gpio_34 = gpio_constant<34>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 35))
inline constexpr gpio gpio_35 = gpio_constant<35>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 36))
inline constexpr gpio gpio_36 = gpio_constant<36>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 37))
inline constexpr gpio gpio_37 = gpio_constant<37>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 38))
inline constexpr gpio gpio_38 = gpio_constant<38>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 39))
inline constexpr gpio gpio_39 = gpio_constant<39>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 40))
inline constexpr gpio gpio_40 = gpio_constant<40>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 41))
inline constexpr gpio gpio_41 = gpio_constant<41>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 42))
inline constexpr gpio gpio_42 = gpio_constant<42>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 43))
inline constexpr gpio gpio_43 = gpio_constant<43>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 44))
inline constexpr gpio gpio_44 = gpio_constant<44>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 45))
inline constexpr gpio gpio_45 = gpio_constant<45>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 46))
inline constexpr gpio gpio_46 = gpio_constant<46>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 47))
inline constexpr gpio gpio_47 = gpio_constant<47>::value;
#endif
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 48))
inline constexpr gpio gpio_48 = gpio_constant<48>::value;
#endif

/** @} */ // end of GPIO Constants

/**
 * @headerfile <idfxx/gpio>
 * @brief Returns a string representation of a GPIO pin.
 *
 * @param g The GPIO pin to convert.
 * @return "GPIO_NC" for not-connected, or "GPIO_N" where N is the pin number.
 */
[[nodiscard]] inline std::string to_string(gpio g) {
    if (!g.is_connected()) {
        return "GPIO_NC";
    }
    return "GPIO_" + std::to_string(g.num());
}

/** @} */ // end of idfxx_gpio

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::gpio> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::gpio g, FormatContext& ctx) const {
        auto s = to_string(g);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
