// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/timer>
 * @file timer.hpp
 * @brief High-resolution timer class.
 *
 * @defgroup idfxx_timer Timer Component
 * @brief High-resolution timer management for ESP32.
 *
 * Provides timer lifecycle management with type-safe duration handling.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/chrono>
#include <idfxx/error>

#include <algorithm>
#include <chrono>
#include <esp_idf_version.h>
#include <esp_timer.h>
#include <functional>
#include <string>

namespace idfxx {

/**
 * @headerfile <idfxx/timer>
 * @brief High-resolution timer with microsecond precision.
 *
 * Supports both one-shot and periodic timers with callbacks dispatched either
 * from a dedicated timer task or directly from ISR context.
 *
 * When exceptions are enabled, an exception thrown by a std::move_only_function
 * callback is logged and otherwise ignored, and the timer keeps running. Raw
 * function pointer callbacks are called directly and must not throw.
 *
 * This type is non-copyable and move-only. A moved-from
 * object must not be used: any operation other than destruction or
 * assignment is undefined behavior.
 */
class timer {
public:
    /**
     * @headerfile <idfxx/timer>
     * @brief Monotonic clock based on boot time.
     *
     * Provides a steady clock with microsecond precision that continues
     * running during light sleep. Compatible with std::chrono.
     */
    struct clock {
        using rep = int64_t;
        using period = std::micro;
        using duration = std::chrono::microseconds;
        using time_point = std::chrono::time_point<clock>;
        static constexpr bool is_steady = true;

        /**
         * @brief Returns the current time.
         * @return Current time as a time_point.
         */
        [[nodiscard]] static time_point now() noexcept { return time_point{duration{esp_timer_get_time()}}; }
    };

    /**
     * @brief Callback dispatch type.
     *
     * Determines whether the timer callback runs in a high-priority timer task
     * or directly in ISR context.
     *
     * @note ISR dispatch is only available when CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
     *       is enabled in menuconfig.
     */
    enum class dispatch_method : int {
        task = ESP_TIMER_TASK, ///< Callback runs in high-priority timer task (default)
#if CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD || __DOXYGEN__
        isr = ESP_TIMER_ISR, ///< Callback runs directly in ISR context
#endif
    };

    /**
     * @brief Timer configuration parameters.
     */
    struct config {
        std::string_view name = "";                            ///< Timer name for debugging
        enum dispatch_method dispatch = dispatch_method::task; ///< Callback dispatch type
        bool skip_unhandled_events = false;                    ///< Skip events if callback busy
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a timer with a std::move_only_function callback.
     *
     * @warning This overload cannot be used with dispatch_method::isr.
     *
     * @param cfg Timer configuration.
     * @param callback Function to call when timer fires.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit timer(const config& cfg, std::move_only_function<void()> callback);
#endif

    /**
     * @brief Creates a timer with a std::move_only_function callback.
     *
     * @warning This method cannot be used with dispatch_method::isr. Use the raw function pointer overload for ISR
     * dispatch.
     *
     * @param cfg Timer configuration.
     * @param callback Function to call when timer fires.
     * @return The new timer, or an error.

     * @retval invalid_arg Invalid configuration.
     */
    [[nodiscard]] static result<timer> make(config cfg, std::move_only_function<void()> callback);

    /**
     * @brief Creates a timer with a raw function pointer callback.
     *
     * @note This overload is suitable for ISR dispatch.
     *
     * @param cfg Timer configuration.
     * @param callback Function to call when timer fires.
     * @param arg Argument passed to the callback.
     * @return The new timer, or an error.

     * @retval invalid_arg Invalid configuration.
     */
    [[nodiscard]] static result<timer> make(config cfg, void (*callback)(void*), void* arg);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates and starts a one-shot timer with a std::move_only_function callback.
     *
     * Combines timer creation and one-shot start into a single operation. If creation
     * succeeds but starting fails, the timer is automatically cleaned up via RAII.
     *
     * @warning This overload cannot be used with dispatch_method::isr.
     *
     * @code
     * using namespace std::chrono_literals;
     * auto t = idfxx::timer::start_once(
     *     {.name = "my_timer"}, 100ms,
     *     []() { idfxx::log::info("timer", "Fired!"); });
     * @endcode
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param timeout Time until the callback is invoked.
     * @param callback Function to call when timer fires.
     * @return The new running timer.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static timer start_once(
        config cfg,
        const std::chrono::duration<Rep, Period>& timeout,
        std::move_only_function<void()> callback
    ) {
        return unwrap(try_start_once(std::move(cfg), timeout, std::move(callback)));
    }

    /**
     * @brief Creates and starts a one-shot timer with a raw function pointer callback.
     *
     * Combines timer creation and one-shot start into a single operation. If creation
     * succeeds but starting fails, the timer is automatically cleaned up via RAII.
     *
     * @note This overload is suitable for ISR dispatch.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param timeout Time until the callback is invoked.
     * @param callback Function to call when timer fires.
     * @param arg Argument passed to the callback.
     * @return The new running timer.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static timer
    start_once(config cfg, const std::chrono::duration<Rep, Period>& timeout, void (*callback)(void*), void* arg) {
        return unwrap(try_start_once(std::move(cfg), timeout, callback, arg));
    }

    /**
     * @brief Creates and starts a one-shot timer at an absolute time with a std::move_only_function callback.
     *
     * Combines timer creation and one-shot start into a single operation. The timer
     * fires at the specified time point. If the time point is in the past, the timer
     * fires as soon as possible.
     *
     * @warning This overload cannot be used with dispatch_method::isr.
     *
     * @code
     * auto t = idfxx::timer::start_once(
     *     {.name = "alarm"}, idfxx::timer::clock::now() + 500ms,
     *     []() { idfxx::log::info("timer", "Alarm!"); });
     * @endcode
     *
     * @param cfg Timer configuration.
     * @param time Absolute time at which the callback should be invoked.
     * @param callback Function to call when timer fires.
     * @return The new running timer.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] static timer
    start_once(config cfg, clock::time_point time, std::move_only_function<void()> callback) {
        return unwrap(try_start_once(std::move(cfg), time, std::move(callback)));
    }

    /**
     * @brief Creates and starts a one-shot timer at an absolute time with a raw function pointer callback.
     *
     * Combines timer creation and one-shot start into a single operation. The timer
     * fires at the specified time point. If the time point is in the past, the timer
     * fires as soon as possible.
     *
     * @note This overload is suitable for ISR dispatch.
     *
     * @param cfg Timer configuration.
     * @param time Absolute time at which the callback should be invoked.
     * @param callback Function to call when timer fires.
     * @param arg Argument passed to the callback.
     * @return The new running timer.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] static timer start_once(config cfg, clock::time_point time, void (*callback)(void*), void* arg) {
        return unwrap(try_start_once(std::move(cfg), time, callback, arg));
    }

    /**
     * @brief Creates and starts a periodic timer with a std::move_only_function callback.
     *
     * Combines timer creation and periodic start into a single operation. If creation
     * succeeds but starting fails, the timer is automatically cleaned up via RAII.
     *
     * @warning This overload cannot be used with dispatch_method::isr.
     *
     * @code
     * using namespace std::chrono_literals;
     * auto t = idfxx::timer::start_periodic(
     *     {.name = "heartbeat"}, 100ms,
     *     []() { idfxx::log::info("timer", "Tick!"); });
     * @endcode
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param interval Time between callback invocations.
     * @param callback Function to call when timer fires.
     * @return The new running timer.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static timer start_periodic(
        config cfg,
        const std::chrono::duration<Rep, Period>& interval,
        std::move_only_function<void()> callback
    ) {
        return unwrap(try_start_periodic(std::move(cfg), interval, std::move(callback)));
    }

    /**
     * @brief Creates and starts a periodic timer with a raw function pointer callback.
     *
     * Combines timer creation and periodic start into a single operation. If creation
     * succeeds but starting fails, the timer is automatically cleaned up via RAII.
     *
     * @note This overload is suitable for ISR dispatch.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param interval Time between callback invocations.
     * @param callback Function to call when timer fires.
     * @param arg Argument passed to the callback.
     * @return The new running timer.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static timer
    start_periodic(config cfg, const std::chrono::duration<Rep, Period>& interval, void (*callback)(void*), void* arg) {
        return unwrap(try_start_periodic(std::move(cfg), interval, callback, arg));
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    /**
     * @brief Creates and starts a periodic timer with an absolute first time and a std::move_only_function callback.
     *
     * Combines timer creation and periodic start into a single operation. The callback
     * is first invoked at the specified time point and then repeatedly at the given
     * interval. If the time point is in the past, the first invocation happens as soon
     * as possible. If creation succeeds but starting fails, the timer is automatically
     * cleaned up via RAII.
     *
     * @warning This overload cannot be used with dispatch_method::isr.
     *
     * @code
     * using namespace std::chrono_literals;
     * auto t = idfxx::timer::start_periodic(
     *     {.name = "heartbeat"}, idfxx::timer::clock::now() + 500ms, 100ms,
     *     []() { idfxx::log::info("timer", "Tick!"); });
     * @endcode
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param first Absolute time at which the callback is first invoked.
     * @param interval Time between callback invocations.
     * @param callback Function to call when timer fires.
     * @return The new running timer.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @note Only available on ESP-IDF 6.1 and later.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static timer start_periodic(
        config cfg,
        clock::time_point first,
        const std::chrono::duration<Rep, Period>& interval,
        std::move_only_function<void()> callback
    ) {
        return unwrap(try_start_periodic(std::move(cfg), first, interval, std::move(callback)));
    }

    /**
     * @brief Creates and starts a periodic timer with an absolute first time and a raw function pointer callback.
     *
     * Combines timer creation and periodic start into a single operation. The callback
     * is first invoked at the specified time point and then repeatedly at the given
     * interval. If the time point is in the past, the first invocation happens as soon
     * as possible. If creation succeeds but starting fails, the timer is automatically
     * cleaned up via RAII.
     *
     * @note This overload is suitable for ISR dispatch.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param first Absolute time at which the callback is first invoked.
     * @param interval Time between callback invocations.
     * @param callback Function to call when timer fires.
     * @param arg Argument passed to the callback.
     * @return The new running timer.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @note Only available on ESP-IDF 6.1 and later.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static timer start_periodic(
        config cfg,
        clock::time_point first,
        const std::chrono::duration<Rep, Period>& interval,
        void (*callback)(void*),
        void* arg
    ) {
        return unwrap(try_start_periodic(std::move(cfg), first, interval, callback, arg));
    }
#endif

#endif

    /**
     * @brief Creates and starts a one-shot timer with a std::move_only_function callback.
     *
     * Combines timer creation and one-shot start into a single operation. If creation
     * succeeds but starting fails, the timer is automatically cleaned up via RAII.
     *
     * @warning This method cannot be used with dispatch_method::isr. Use the raw function pointer overload for ISR
     * dispatch.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param timeout Time until the callback is invoked.
     * @param callback Function to call when timer fires.
     * @return The new running timer, or an error.

     * @retval invalid_arg Invalid configuration or timeout value.
     * @retval invalid_state Timer could not be started.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static result<timer> try_start_once(
        config cfg,
        const std::chrono::duration<Rep, Period>& timeout,
        std::move_only_function<void()> callback
    ) {
        auto t = make(std::move(cfg), std::move(callback));
        if (!t) {
            return t;
        }
        auto r = t->try_start_once(timeout);
        if (!r) {
            return error(r.error());
        }
        return t;
    }

    /**
     * @brief Creates and starts a one-shot timer with a raw function pointer callback.
     *
     * Combines timer creation and one-shot start into a single operation. If creation
     * succeeds but starting fails, the timer is automatically cleaned up via RAII.
     *
     * @note This overload is suitable for ISR dispatch.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param timeout Time until the callback is invoked.
     * @param callback Function to call when timer fires.
     * @param arg Argument passed to the callback.
     * @return The new running timer, or an error.
     *
     * @retval invalid_arg Invalid configuration or timeout value.
     * @retval invalid_state Timer could not be started.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static result<timer>
    try_start_once(config cfg, const std::chrono::duration<Rep, Period>& timeout, void (*callback)(void*), void* arg) {
        auto t = make(std::move(cfg), callback, arg);
        if (!t) {
            return t;
        }
        auto r = t->try_start_once(timeout);
        if (!r) {
            return error(r.error());
        }
        return t;
    }

    /**
     * @brief Creates and starts a one-shot timer at an absolute time with a std::move_only_function callback.
     *
     * Combines timer creation and one-shot start into a single operation. The timer
     * fires at the specified time point. If the time point is in the past, the timer
     * fires as soon as possible.
     *
     * @warning This method cannot be used with dispatch_method::isr. Use the raw function pointer overload for ISR
     * dispatch.
     *
     * @param cfg Timer configuration.
     * @param time Absolute time at which the callback should be invoked.
     * @param callback Function to call when timer fires.
     * @return The new running timer, or an error.

     * @retval invalid_arg Invalid configuration.
     * @retval invalid_state Timer could not be started.
     */
    [[nodiscard]] static result<timer>
    try_start_once(config cfg, clock::time_point time, std::move_only_function<void()> callback) {
        auto t = make(std::move(cfg), std::move(callback));
        if (!t) {
            return t;
        }
        auto r = t->try_start_once(time);
        if (!r) {
            return error(r.error());
        }
        return t;
    }

    /**
     * @brief Creates and starts a one-shot timer at an absolute time with a raw function pointer callback.
     *
     * Combines timer creation and one-shot start into a single operation. The timer
     * fires at the specified time point. If the time point is in the past, the timer
     * fires as soon as possible.
     *
     * @note This overload is suitable for ISR dispatch.
     *
     * @param cfg Timer configuration.
     * @param time Absolute time at which the callback should be invoked.
     * @param callback Function to call when timer fires.
     * @param arg Argument passed to the callback.
     * @return The new running timer, or an error.
     *
     * @retval invalid_arg Invalid configuration.
     * @retval invalid_state Timer could not be started.
     */
    [[nodiscard]] static result<timer>
    try_start_once(config cfg, clock::time_point time, void (*callback)(void*), void* arg) {
        auto t = make(std::move(cfg), callback, arg);
        if (!t) {
            return t;
        }
        auto r = t->try_start_once(time);
        if (!r) {
            return error(r.error());
        }
        return t;
    }

    /**
     * @brief Creates and starts a periodic timer with a std::move_only_function callback.
     *
     * Combines timer creation and periodic start into a single operation. If creation
     * succeeds but starting fails, the timer is automatically cleaned up via RAII.
     *
     * @warning This method cannot be used with dispatch_method::isr. Use the raw function pointer overload for ISR
     * dispatch.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param interval Time between callback invocations.
     * @param callback Function to call when timer fires.
     * @return The new running timer, or an error.

     * @retval invalid_arg Invalid configuration or interval value.
     * @retval invalid_state Timer could not be started.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static result<timer> try_start_periodic(
        config cfg,
        const std::chrono::duration<Rep, Period>& interval,
        std::move_only_function<void()> callback
    ) {
        auto t = make(std::move(cfg), std::move(callback));
        if (!t) {
            return t;
        }
        auto r = t->try_start_periodic(interval);
        if (!r) {
            return error(r.error());
        }
        return t;
    }

    /**
     * @brief Creates and starts a periodic timer with a raw function pointer callback.
     *
     * Combines timer creation and periodic start into a single operation. If creation
     * succeeds but starting fails, the timer is automatically cleaned up via RAII.
     *
     * @note This overload is suitable for ISR dispatch.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param interval Time between callback invocations.
     * @param callback Function to call when timer fires.
     * @param arg Argument passed to the callback.
     * @return The new running timer, or an error.
     *
     * @retval invalid_arg Invalid configuration or interval value.
     * @retval invalid_state Timer could not be started.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static result<timer> try_start_periodic(
        config cfg,
        const std::chrono::duration<Rep, Period>& interval,
        void (*callback)(void*),
        void* arg
    ) {
        auto t = make(std::move(cfg), callback, arg);
        if (!t) {
            return t;
        }
        auto r = t->try_start_periodic(interval);
        if (!r) {
            return error(r.error());
        }
        return t;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    /**
     * @brief Creates and starts a periodic timer with an absolute first time and a std::move_only_function callback.
     *
     * Combines timer creation and periodic start into a single operation. The callback
     * is first invoked at the specified time point and then repeatedly at the given
     * interval. If the time point is in the past, the first invocation happens as soon
     * as possible. If creation succeeds but starting fails, the timer is automatically
     * cleaned up via RAII.
     *
     * @warning This method cannot be used with dispatch_method::isr. Use the raw function pointer overload for ISR
     * dispatch.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param first Absolute time at which the callback is first invoked.
     * @param interval Time between callback invocations.
     * @param callback Function to call when timer fires.
     * @return The new running timer, or an error.
     * @note Only available on ESP-IDF 6.1 and later.
     *
     * @retval invalid_arg Invalid configuration.
     * @retval invalid_state Timer could not be started.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static result<timer> try_start_periodic(
        config cfg,
        clock::time_point first,
        const std::chrono::duration<Rep, Period>& interval,
        std::move_only_function<void()> callback
    ) {
        auto t = make(std::move(cfg), std::move(callback));
        if (!t) {
            return t;
        }
        auto r = t->try_start_periodic(first, interval);
        if (!r) {
            return error(r.error());
        }
        return t;
    }

    /**
     * @brief Creates and starts a periodic timer with an absolute first time and a raw function pointer callback.
     *
     * Combines timer creation and periodic start into a single operation. The callback
     * is first invoked at the specified time point and then repeatedly at the given
     * interval. If the time point is in the past, the first invocation happens as soon
     * as possible. If creation succeeds but starting fails, the timer is automatically
     * cleaned up via RAII.
     *
     * @note This overload is suitable for ISR dispatch.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param cfg Timer configuration.
     * @param first Absolute time at which the callback is first invoked.
     * @param interval Time between callback invocations.
     * @param callback Function to call when timer fires.
     * @param arg Argument passed to the callback.
     * @return The new running timer, or an error.
     * @note Only available on ESP-IDF 6.1 and later.
     *
     * @retval invalid_arg Invalid configuration.
     * @retval invalid_state Timer could not be started.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] static result<timer> try_start_periodic(
        config cfg,
        clock::time_point first,
        const std::chrono::duration<Rep, Period>& interval,
        void (*callback)(void*),
        void* arg
    ) {
        auto t = make(std::move(cfg), callback, arg);
        if (!t) {
            return t;
        }
        auto r = t->try_start_periodic(first, interval);
        if (!r) {
            return error(r.error());
        }
        return t;
    }
#endif

    /**
     * @brief Destroys the timer.
     *
     * Stops the timer if running and waits for any in-flight callback to
     * complete before releasing resources.
     */
    ~timer();

    timer(const timer&) = delete;
    timer& operator=(const timer&) = delete;
    timer(timer&& other) noexcept;
    timer& operator=(timer&& other) noexcept;

    /**
     * @brief Returns the underlying ESP-IDF timer handle.
     * @return The esp_timer handle.
     */
    [[nodiscard]] esp_timer_handle_t idf_handle() const noexcept { return _handle; }

    /**
     * @brief Returns the timer name.
     * @return The timer name.
     */
    [[nodiscard]] const std::string& name() const noexcept { return _name; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Starts the timer as a one-shot timer.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param timeout Time until the callback is invoked.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void start_once(const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_start_once(timeout));
    }

    /**
     * @brief Starts the timer as a one-shot timer at an absolute time.
     *
     * The callback will be invoked once at the specified time point. If the
     * time point is in the past, the timer fires as soon as possible.
     *
     * @code
     * using namespace std::chrono_literals;
     * auto t = idfxx::timer(
     *     {.name = "alarm"},
     *     []() { idfxx::log::info("timer", "Alarm!"); });
     * t.start_once(idfxx::timer::clock::now() + 500ms);
     * @endcode
     *
     * @param time Absolute time at which the callback should be invoked.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void start_once(clock::time_point time) { unwrap(try_start_once(time)); }

    /**
     * @brief Starts the timer as a periodic timer.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param interval Time between callback invocations.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void start_periodic(const std::chrono::duration<Rep, Period>& interval) {
        unwrap(try_start_periodic(interval));
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    /**
     * @brief Starts the timer as a periodic timer with an absolute first time.
     *
     * The callback is first invoked at the specified time point and then
     * repeatedly at the given interval. If the time point is in the past, the
     * first invocation happens as soon as possible.
     *
     * @code
     * using namespace std::chrono_literals;
     * auto t = idfxx::timer(
     *     {.name = "sampler"},
     *     []() { idfxx::log::info("timer", "Sample!"); });
     * t.start_periodic(idfxx::timer::clock::now() + 1s, 100ms);
     * @endcode
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param first Absolute time at which the callback is first invoked.
     * @param interval Time between callback invocations.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @note Only available on ESP-IDF 6.1 and later.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void start_periodic(clock::time_point first, const std::chrono::duration<Rep, Period>& interval) {
        unwrap(try_start_periodic(first, interval));
    }
#endif
#endif

    /**
     * @brief Starts the timer as a one-shot timer.
     *
     * The callback will be invoked once after the specified timeout.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param timeout Time until the callback is invoked.
     * @return Success, or an error.
     * @retval invalid_state Timer is already running.
     * @retval invalid_arg Invalid timeout value.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_start_once(const std::chrono::duration<Rep, Period>& timeout) {
        if (_handle == nullptr) {
            return error(errc::invalid_state);
        }
        return wrap(esp_timer_start_once(_handle, to_us(timeout)));
    }

    /**
     * @brief Starts the timer as a one-shot timer at an absolute time.
     *
     * The callback will be invoked once at the specified time point. If the
     * time point is in the past, the timer fires as soon as possible.
     *
     * @param time Absolute time at which the callback should be invoked.
     * @return Success, or an error.
     * @retval invalid_state Timer is already running.
     */
    [[nodiscard]] result<void> try_start_once(clock::time_point time);

    /**
     * @brief Starts the timer as a one-shot timer (ISR-compatible).
     *
     * This method returns the underlying ESP-IDF esp_err_t directly to avoid
     * flash-resident code paths. When CONFIG_ESP_TIMER_IN_IRAM is enabled, this
     * method is placed in IRAM and can be called from ISR context.
     *
     * @code
     * using namespace std::chrono_literals;
     * constexpr uint64_t timeout_us = std::chrono::microseconds(100ms).count();
     * t.try_start_once_isr(timeout_us);
     * @endcode
     *
     * @param timeout_us Timeout in microseconds.
     * @return ESP_OK on success, or an esp_err_t error code.
     * @retval ESP_ERR_INVALID_STATE Timer is already running.
     * @retval ESP_ERR_INVALID_ARG Invalid timeout value.
     */
    esp_err_t try_start_once_isr(uint64_t timeout_us);

    /**
     * @brief Starts the timer as a periodic timer.
     *
     * The callback will be invoked repeatedly at the specified interval.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param interval Time between callback invocations.
     * @return Success, or an error.
     * @retval invalid_state Timer is already running.
     * @retval invalid_arg Invalid interval value.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_start_periodic(const std::chrono::duration<Rep, Period>& interval) {
        if (_handle == nullptr) {
            return error(errc::invalid_state);
        }
        return wrap(esp_timer_start_periodic(_handle, to_us(interval)));
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    /**
     * @brief Starts the timer as a periodic timer with an absolute first time.
     *
     * The callback is first invoked at the specified time point and then
     * repeatedly at the given interval. If the time point is in the past, the
     * first invocation happens as soon as possible.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param first Absolute time at which the callback is first invoked.
     * @param interval Time between callback invocations.
     * @return Success, or an error.
     * @note Only available on ESP-IDF 6.1 and later.
     * @retval invalid_state Timer is already running.
     * @retval invalid_arg @p first kept slipping into the past before the timer could be started.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_start_periodic(clock::time_point first, const std::chrono::duration<Rep, Period>& interval) {
        return _try_start_periodic_at(first, to_us(interval));
    }
#endif

    /**
     * @brief Starts the timer as a periodic timer (ISR-compatible).
     *
     * This method returns the underlying ESP-IDF esp_err_t directly to avoid
     * flash-resident code paths. When CONFIG_ESP_TIMER_IN_IRAM is enabled, this
     * method is placed in IRAM and can be called from ISR context.
     *
     * @code
     * using namespace std::chrono_literals;
     * constexpr uint64_t interval_us = std::chrono::microseconds(100ms).count();
     * t.try_start_periodic_isr(interval_us);
     * @endcode
     *
     * @param interval_us Interval in microseconds.
     * @return ESP_OK on success, or an esp_err_t error code.
     */
    esp_err_t try_start_periodic_isr(uint64_t interval_us);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Restarts the timer with a new timeout.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param timeout Time until the callback is invoked.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void restart(const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_restart(timeout));
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    /**
     * @brief Restarts the timer so that it next fires at an absolute time.
     *
     * A running one-shot timer is rearmed to fire once at @p next. A running
     * periodic timer next fires at @p next and keeps its current period. A
     * timer that is not running is started as a one-shot timer firing at
     * @p next. If @p next is in the past, the timer fires as soon as possible.
     *
     * @code
     * using namespace std::chrono_literals;
     * // Push the deadline back each time activity is seen
     * watchdog.restart(idfxx::timer::clock::now() + 5s);
     * @endcode
     *
     * @param next Absolute time at which the callback is next invoked.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @note Only available on ESP-IDF 6.1 and later.
     * @throws std::system_error on failure.
     */
    void restart(clock::time_point next) { unwrap(try_restart(next)); }

    /**
     * @brief Restarts the timer so that it next fires at an absolute time, with a new period.
     *
     * A running periodic timer next fires at @p next and then repeatedly at
     * the new @p period. A running one-shot timer is rearmed to fire once at
     * @p next and ignores @p period. A timer that is not running is started
     * as a one-shot timer firing at @p next, also ignoring @p period. If
     * @p next is in the past, the timer fires as soon as possible.
     *
     * @code
     * using namespace std::chrono_literals;
     * // Resynchronize a periodic sampler to a new schedule
     * sampler.restart(idfxx::timer::clock::now() + 1s, 250ms);
     * @endcode
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param next Absolute time at which the callback is next invoked.
     * @param period New time between callback invocations of a periodic timer.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @note Only available on ESP-IDF 6.1 and later.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void restart(clock::time_point next, const std::chrono::duration<Rep, Period>& period) {
        unwrap(try_restart(next, period));
    }
#endif
#endif

    /**
     * @brief Restarts the timer with a new timeout.
     *
     * If the timer is running, it will be stopped and restarted. If not running,
     * it will be started as a one-shot timer.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param timeout Time until the callback is invoked.
     * @return Success, or an error.
     * @retval invalid_arg Invalid timeout value.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_restart(const std::chrono::duration<Rep, Period>& timeout) {
        if (_handle == nullptr) {
            return error(errc::invalid_state);
        }
        auto err = esp_timer_restart(_handle, to_us(timeout));
        if (err == ESP_ERR_INVALID_STATE) {
            return wrap(esp_timer_start_once(_handle, to_us(timeout)));
        }
        return wrap(err);
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    /**
     * @brief Restarts the timer so that it next fires at an absolute time.
     *
     * A running one-shot timer is rearmed to fire once at @p next. A running
     * periodic timer next fires at @p next and keeps its current period. A
     * timer that is not running is started as a one-shot timer firing at
     * @p next. If @p next is in the past, the timer fires as soon as possible.
     *
     * @param next Absolute time at which the callback is next invoked.
     * @return Success, or an error.
     * @note Only available on ESP-IDF 6.1 and later.
     * @retval invalid_state The timer could not be started.
     * @retval invalid_arg @p next kept slipping into the past before the timer could be rearmed.
     */
    [[nodiscard]] result<void> try_restart(clock::time_point next);

    /**
     * @brief Restarts the timer so that it next fires at an absolute time, with a new period.
     *
     * A running periodic timer next fires at @p next and then repeatedly at
     * the new @p period. A running one-shot timer is rearmed to fire once at
     * @p next and ignores @p period. A timer that is not running is started
     * as a one-shot timer firing at @p next, also ignoring @p period. If
     * @p next is in the past, the timer fires as soon as possible.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param next Absolute time at which the callback is next invoked.
     * @param period New time between callback invocations of a periodic timer.
     * @return Success, or an error.
     * @note Only available on ESP-IDF 6.1 and later.
     * @retval invalid_state The timer could not be started.
     * @retval invalid_arg @p next kept slipping into the past before the timer could be rearmed.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_restart(clock::time_point next, const std::chrono::duration<Rep, Period>& period) {
        return _try_restart_at(next, to_us(period));
    }
#endif

    /**
     * @brief Restarts the timer with a new timeout (ISR-compatible).
     *
     * This method returns the underlying ESP-IDF esp_err_t directly to avoid
     * flash-resident code paths. When CONFIG_ESP_TIMER_IN_IRAM is enabled, this
     * method is placed in IRAM and can be called from ISR context.
     *
     * @code
     * using namespace std::chrono_literals;
     * constexpr uint64_t timeout_us = std::chrono::microseconds(100ms).count();
     * t.try_restart_isr(timeout_us);
     * @endcode
     *
     * @param timeout_us Timeout in microseconds.
     * @return ESP_OK on success, or an esp_err_t error code.
     */
    esp_err_t try_restart_isr(uint64_t timeout_us);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Stops the timer.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void stop() { unwrap(try_stop()); }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    /**
     * @brief Stops the timer and waits for a running callback to finish.
     *
     * Once this returns, the callback is not running and will not run again
     * until the timer is restarted. The timer is disarmed so that no further
     * callbacks are dispatched, then, if the callback is currently executing,
     * this waits for it to return, for as long as that takes.
     *
     * Unlike stop(), stopping a timer that is not running succeeds.
     *
     * When called from within this timer's own callback, the timer is disarmed
     * and the call returns immediately without waiting; the callback finishes
     * after it returns.
     *
     * @code
     * // Make sure the callback is no longer touching shared state
     * t.stop_and_wait();
     * release_shared_state();
     * @endcode
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @note Only available on ESP-IDF 6.1 and later.
     * @throws std::system_error with idfxx::errc::not_finished if the callback may still be
     *         running and could not be waited for. The timer is disarmed in that case too.
     */
    void stop_and_wait() { unwrap(try_stop_and_wait()); }

    /**
     * @brief Stops the timer and waits, up to a timeout, for a running callback to finish.
     *
     * Once this returns without throwing, the callback is not running and will
     * not run again until the timer is restarted. The timer is disarmed so
     * that no further callbacks are dispatched, then, if the callback is
     * currently executing, this waits up to @p timeout for it to return.
     *
     * Unlike stop(), stopping a timer that is not running succeeds.
     *
     * When called from within this timer's own callback, the timer is disarmed
     * and the call returns immediately without waiting; the callback finishes
     * after it returns.
     *
     * @code
     * using namespace std::chrono_literals;
     * // Make sure the callback is no longer touching shared state
     * t.stop_and_wait(100ms);
     * release_shared_state();
     * @endcode
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param timeout Maximum time to wait for a running callback to finish. A zero timeout
     *                does not wait.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @note Only available on ESP-IDF 6.1 and later.
     * @throws std::system_error with idfxx::errc::timeout if the callback was still running
     *         when the timeout expired, or with idfxx::errc::not_finished if the callback may
     *         still be running and could not be waited for. The timer is disarmed in both cases.
     */
    template<typename Rep, typename Period>
    void stop_and_wait(const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_stop_and_wait(timeout));
    }
#endif
#endif

    /**
     * @brief Stops the timer.
     *
     * @return Success, or an error.
     * @retval invalid_state Timer is not running.
     */
    result<void> try_stop() {
        if (_handle == nullptr) {
            return error(errc::invalid_state);
        }
        return wrap(esp_timer_stop(_handle));
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    /**
     * @brief Stops the timer and waits for a running callback to finish.
     *
     * On success, the callback is not running and will not run again until
     * the timer is restarted. The timer is disarmed so that no further
     * callbacks are dispatched, then, if the callback is currently executing,
     * this waits for it to return, for as long as that takes.
     *
     * Unlike try_stop(), stopping a timer that is not running succeeds.
     *
     * When called from within this timer's own callback, the timer is disarmed
     * and the call returns success immediately without waiting; the callback
     * finishes after it returns.
     *
     * Whatever the result, the timer is disarmed when this returns. After a
     * not_finished error, poll is_active() until it returns false to learn
     * when the callback has finished.
     *
     * @return Success, or an error.
     * @note Only available on ESP-IDF 6.1 and later.
     * @retval not_finished The callback may still be running and could not be waited for:
     *         the call was made from ISR context, or from a task-dispatched timer callback while
     *         this timer's ISR-dispatched callback was running.
     * @retval invalid_state The timer service is not initialized.
     */
    [[nodiscard]] result<void> try_stop_and_wait() {
        if (_handle == nullptr) {
            return error(errc::invalid_state);
        }
        return wrap(esp_timer_stop_blocking(_handle, portMAX_DELAY));
    }

    /**
     * @brief Stops the timer and waits, up to a timeout, for a running callback to finish.
     *
     * On success, the callback is not running and will not run again until
     * the timer is restarted. The timer is disarmed so that no further
     * callbacks are dispatched, then, if the callback is currently executing,
     * this waits up to @p timeout for it to return.
     *
     * Unlike try_stop(), stopping a timer that is not running succeeds.
     *
     * When called from within this timer's own callback, the timer is disarmed
     * and the call returns success immediately without waiting; the callback
     * finishes after it returns.
     *
     * Whatever the result, the timer is disarmed when this returns. After a
     * timeout or not_finished error, poll is_active() until it returns false to
     * learn when the callback has finished.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param timeout Maximum time to wait for a running callback to finish. A zero timeout
     *                does not wait.
     * @return Success, or an error.
     * @note Only available on ESP-IDF 6.1 and later.
     * @retval timeout The callback was still running when the timeout expired.
     * @retval not_finished The callback may still be running and could not be waited for:
     *         the call was made from ISR context, or from a task-dispatched timer callback while
     *         this timer's ISR-dispatched callback was running.
     * @retval invalid_state The timer service is not initialized.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_stop_and_wait(const std::chrono::duration<Rep, Period>& timeout) {
        if (_handle == nullptr) {
            return error(errc::invalid_state);
        }
        return wrap(esp_timer_stop_blocking(_handle, chrono::ticks(timeout)));
    }
#endif

    /**
     * @brief Stops the timer (ISR-compatible).
     *
     * This method returns the underlying ESP-IDF esp_err_t directly to avoid
     * flash-resident code paths. When CONFIG_ESP_TIMER_IN_IRAM is enabled, this
     * method is placed in IRAM and can be called from ISR context.
     *
     * @return ESP_OK on success, or an esp_err_t error code.
     * @retval ESP_ERR_INVALID_STATE Timer is not running.
     */
    esp_err_t try_stop_isr();

    /**
     * @brief Checks if the timer is currently running.
     *
     * @note On ESP-IDF 6.1 and later, a timer is also active while its callback
     *       is executing, even once the timer is no longer armed: after stop(),
     *       or after a one-shot timer has fired. On earlier versions, only an
     *       armed timer is active.
     *
     * @return true if the timer is active, false otherwise.
     */
    [[nodiscard]] bool is_active() const noexcept {
        if (_handle == nullptr) {
            return false;
        }
        return esp_timer_is_active(_handle);
    }

    /**
     * @brief Returns the period of a periodic timer.
     *
     * For periodic timers, returns the configured interval. For one-shot timers
     * returns zero.
     *
     * @return The timer period as a duration, or zero on error.
     * @retval 0ms For one-shot timers.
     */
    [[nodiscard]] std::chrono::microseconds period() const noexcept {
        if (_handle == nullptr) {
            return std::chrono::microseconds{0};
        }
        uint64_t period_us = 0;
        esp_timer_get_period(_handle, &period_us);
        return std::chrono::microseconds{static_cast<int64_t>(period_us)};
    }

    /**
     * @brief Returns the absolute expiry time for a one-shot timer.
     *
     * For periodic timers, returns clock::time_point::max() as a sentinel value
     * since periodic timers do not have a single expiry time.
     *
     * @return The expiry time.
     * @retval clock::time_point::max() For periodic timers.
     */
    [[nodiscard]] clock::time_point expiry_time() const noexcept {
        if (_handle == nullptr) {
            return clock::time_point::max();
        }
        uint64_t expiry;
        if (esp_timer_get_expiry_time(_handle, &expiry) != ESP_OK) {
            return clock::time_point::max();
        }
        if (expiry > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            return clock::time_point::max();
        }
        return clock::time_point{clock::duration{static_cast<int64_t>(expiry)}};
    }

    /**
     * @brief Returns the time of the next scheduled timer event.
     *
     * Returns the absolute time of the next timer alarm across all timers
     * in the system.
     *
     * @return The time of the next alarm.
     */
    [[nodiscard]] static clock::time_point next_alarm() {
        return clock::time_point{clock::duration{esp_timer_get_next_alarm()}};
    }

private:
    /// @cond INTERNAL
    struct context;
    /// @endcond

    explicit timer(esp_timer_handle_t handle, std::string name, context* ctx = nullptr);

    void _stop_and_delete() noexcept;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    [[nodiscard]] result<void> _try_start_periodic_at(clock::time_point first, uint64_t interval_us);
    [[nodiscard]] result<void> _try_restart_at(clock::time_point next, uint64_t period_us);
#endif

    template<typename Rep, typename Period>
    static constexpr int64_t to_us(const std::chrono::duration<Rep, Period>& d) {
        return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
    }

    static void trampoline(void* arg);

    esp_timer_handle_t _handle = nullptr;
    std::string _name;
    context* _context = nullptr;
};

/** @} */ // end of idfxx_timer

} // namespace idfxx
