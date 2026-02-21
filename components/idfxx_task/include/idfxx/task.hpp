// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/task>
 * @file task.hpp
 * @brief Task management.
 *
 * @defgroup idfxx_task Task Component
 * @brief Task lifecycle management for ESP32.
 *
 * Provides task lifecycle management with support for both owned and fire-and-forget tasks.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include <idfxx/chrono>
#include <idfxx/cpu>
#include <idfxx/error>
#include <idfxx/memory>

#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace idfxx {

/**
 * @headerfile <idfxx/task>
 * @brief Task lifecycle management.
 *
 * Manages a task with automatic cleanup on destruction.
 *
 * Tasks are non-copyable and non-movable.
 *
 * @note The task function runs in the created task's context, not the caller's context.
 */
class task {
    struct context;

public:
    /**
     * @headerfile <idfxx/task>
     * @brief Handle for task self-interaction.
     *
     * Provides access to the current task's own state within the task function.
     */
    class self {
        friend class task;
        explicit self(context* ctx) noexcept
            : _context(ctx) {}

    public:
        self(const self&) = delete;
        self& operator=(const self&) = delete;

        /**
         * @brief Checks if the current task has been detached.
         *
         * A detached task is self-managing: when the task function returns,
         * the task cleans up its resources automatically.
         *
         * @return true if the task has been detached or was spawned, false if still owned.
         */
        [[nodiscard]] bool is_detached() const noexcept;

        /**
         * @brief Checks if a stop has been requested for this task.
         *
         * @return true if request_stop() has been called on the owning task object, false otherwise.
         */
        [[nodiscard]] bool stop_requested() const noexcept;

        /**
         * @brief Suspends the current task.
         *
         * The task will not run again until another task calls resume() on it.
         * If a stop has been requested, this method returns immediately without
         * suspending.
         */
        void suspend() noexcept;

        /**
         * @brief Waits for a notification (binary semaphore pattern).
         *
         * Blocks until another task calls notify() or notify_from_isr().
         * If a stop has been requested, returns immediately without waiting.
         *
         * Uses notification index 0, which is reserved for this purpose.
         */
        void wait() noexcept;

        /**
         * @brief Waits for a notification with a timeout (binary semaphore pattern).
         *
         * Blocks until a notification is received or the timeout expires.
         * If a stop has been requested, returns immediately without waiting.
         *
         * Uses notification index 0, which is reserved for this purpose.
         *
         * @tparam Rep The representation type of the duration.
         * @tparam Period The period type of the duration.
         * @param timeout Maximum time to wait for a notification.
         * @return true if a notification was received, false if the timeout expired
         *         or a stop was requested.
         */
        template<typename Rep, typename Period>
        [[nodiscard]] bool wait_for(const std::chrono::duration<Rep, Period>& timeout) noexcept {
            return _take(chrono::ticks(timeout)) != 0;
        }

        /**
         * @brief Waits for a notification until a deadline (binary semaphore pattern).
         *
         * Blocks until a notification is received or the deadline is reached.
         * If a stop has been requested, returns immediately without waiting.
         *
         * Uses notification index 0, which is reserved for this purpose.
         *
         * @tparam Clock The clock type.
         * @tparam Duration The duration type of the time point.
         * @param deadline The time point at which to stop waiting.
         * @return true if a notification was received, false if the deadline was
         *         reached or a stop was requested.
         */
        template<typename Clock, typename Duration>
        [[nodiscard]] bool wait_until(const std::chrono::time_point<Clock, Duration>& deadline) noexcept {
            auto remaining = deadline - Clock::now();
            if (remaining <= decltype(remaining)::zero()) {
                return _take(0) != 0;
            }
            return _take(chrono::ticks(remaining)) != 0;
        }

        /**
         * @brief Takes accumulated notifications (counting semaphore pattern).
         *
         * Blocks until at least one notification is received. Returns the
         * number of accumulated notifications and resets the count to zero.
         * If a stop has been requested, returns 0 immediately without waiting.
         *
         * Uses notification index 0, which is reserved for this purpose.
         *
         * @return The number of accumulated notifications, or 0 if a stop was requested.
         */
        uint32_t take() noexcept;

        /**
         * @brief Takes accumulated notifications with a timeout (counting semaphore pattern).
         *
         * Blocks until at least one notification is received or the timeout expires.
         * Returns the number of accumulated notifications and resets the count to zero.
         * If a stop has been requested, returns 0 immediately without waiting.
         *
         * Uses notification index 0, which is reserved for this purpose.
         *
         * @tparam Rep The representation type of the duration.
         * @tparam Period The period type of the duration.
         * @param timeout Maximum time to wait for notifications.
         * @return The number of accumulated notifications, or 0 if the timeout
         *         expired or a stop was requested.
         */
        template<typename Rep, typename Period>
        [[nodiscard]] uint32_t take_for(const std::chrono::duration<Rep, Period>& timeout) noexcept {
            return _take(chrono::ticks(timeout));
        }

        /**
         * @brief Takes accumulated notifications until a deadline (counting semaphore pattern).
         *
         * Blocks until at least one notification is received or the deadline is reached.
         * Returns the number of accumulated notifications and resets the count to zero.
         * If a stop has been requested, returns 0 immediately without waiting.
         *
         * Uses notification index 0, which is reserved for this purpose.
         *
         * @tparam Clock The clock type.
         * @tparam Duration The duration type of the time point.
         * @param deadline The time point at which to stop waiting.
         * @return The number of accumulated notifications, or 0 if the deadline
         *         was reached or a stop was requested.
         */
        template<typename Clock, typename Duration>
        [[nodiscard]] uint32_t take_until(const std::chrono::time_point<Clock, Duration>& deadline) noexcept {
            auto remaining = deadline - Clock::now();
            if (remaining <= decltype(remaining)::zero()) {
                return _take(0);
            }
            return _take(chrono::ticks(remaining));
        }

        /**
         * @brief Returns the current task priority.
         *
         * @return The task priority level.
         */
        [[nodiscard]] unsigned int priority() const noexcept;

        /**
         * @brief Changes the current task priority.
         *
         * @param new_priority The new priority level.
         */
        void set_priority(unsigned int new_priority) noexcept;

        /**
         * @brief Returns the minimum free stack space (in bytes) since the task started.
         *
         * This is useful for tuning stack sizes — a value approaching zero indicates
         * potential stack overflow.
         *
         * @return The minimum free stack space in bytes since the task started running.
         */
        [[nodiscard]] size_t stack_high_water_mark() const noexcept;

        /**
         * @brief Returns the current task name.
         *
         * @return The task name.
         */
        [[nodiscard]] std::string name() const;

        /**
         * @brief Returns the FreeRTOS handle of the current task.
         *
         * @return The current task's TaskHandle_t.
         */
        [[nodiscard]] TaskHandle_t idf_handle() const noexcept;

    private:
        uint32_t _take(TickType_t ticks) noexcept;

        context* _context;
    };

    /**
     * @headerfile <idfxx/task>
     * @brief Task configuration parameters.
     */
    struct config {
        std::string_view name = "task";                      ///< Task name (max 16 chars)
        size_t stack_size = 4096;                            ///< Stack size in bytes
        unsigned int priority = 5;                           ///< Task priority (0 = lowest)
        std::optional<core_id> core_affinity = std::nullopt; ///< Core pin (nullopt = any core)
        memory_type stack_mem = memory_type::internal;       ///< Stack memory type
    };

    /**
     * @brief Creates a task with a std::move_only_function callback.
     *
     * The task starts executing immediately after construction.
     *
     * @param cfg Task configuration.
     * @param task_func Function to execute in the task context. Receives a @ref self
     *                  reference for task self-interaction.
     * @throws std::bad_alloc if memory allocation fails.
     */
    [[nodiscard]] explicit task(const config& cfg, std::move_only_function<void(self&)> task_func);

    /**
     * @brief Creates a task with a raw function pointer callback.
     *
     * The task starts executing immediately after construction.
     *
     * @param cfg Task configuration.
     * @param task_func Function to execute in the task context. Receives a @ref self
     *                  reference for task self-interaction and the user argument.
     * @param arg Argument passed to the task function.
     * @throws std::bad_alloc if memory allocation fails.
     */
    [[nodiscard]] explicit task(const config& cfg, void (*task_func)(self&, void*), void* arg);

    /**
     * @brief Creates a fire-and-forget task with a std::move_only_function callback.
     *
     * The task starts executing immediately and cleans up its resources when complete.
     *
     * @param cfg Task configuration.
     * @param task_func Function to execute in the task context. Receives a @ref self
     *                  reference for task self-interaction.
     * @throws std::bad_alloc if memory allocation fails.
     */
    static void spawn(config cfg, std::move_only_function<void(self&)> task_func);

    /**
     * @brief Creates a fire-and-forget task with a raw function pointer callback.
     *
     * The task starts executing immediately and cleans up its resources when complete.
     *
     * @param cfg Task configuration.
     * @param task_func Function to execute in the task context. Receives a @ref self
     *                  reference for task self-interaction and the user argument.
     * @param arg Argument passed to the task function.
     * @throws std::bad_alloc if memory allocation fails.
     */
    static void spawn(config cfg, void (*task_func)(self&, void*), void* arg);

    /**
     * @brief Destroys the task, requesting stop and blocking until the task function completes.
     *
     * If the task is still joinable (not detached), the destructor calls
     * request_stop() and then blocks until the task function returns, similar to
     * `std::jthread`. If the task is suspended, it is automatically resumed so it
     * can observe the stop request and exit. The destructor repeatedly resumes the
     * task to ensure it doesn't suspend again. Tasks that check stop_requested()
     * in their loops will exit cleanly.
     *
     * @warning For tasks that ignore the stop request and never return, this will
     * block indefinitely. Call join() with a timeout or detach() explicitly before
     * destruction to avoid indefinite blocking.
     */
    ~task();

    // Non-copyable and non-movable
    task(const task&) = delete;
    task& operator=(const task&) = delete;
    task(task&&) = delete;
    task& operator=(task&&) = delete;

    /**
     * @brief Returns the underlying FreeRTOS task handle.
     *
     * @return The TaskHandle_t, or nullptr if the task has been detached.
     * @note The handle may be invalid after the task function completes.
     *       Use is_completed() to check before using the handle.
     */
    [[nodiscard]] TaskHandle_t idf_handle() const noexcept { return _handle; }

    /**
     * @brief Returns the task name.
     *
     * @return The task name.
     */
    [[nodiscard]] const std::string& name() const noexcept { return _name; }

    /**
     * @brief Returns the current task priority.
     *
     * @return The task priority, or 0 if the task has been detached or completed.
     */
    [[nodiscard]] unsigned int priority() const noexcept;

    /**
     * @brief Returns the minimum free stack space (in bytes) since the task started.
     *
     * This is useful for tuning stack sizes — a value approaching zero indicates
     * potential stack overflow.
     *
     * @return The minimum free stack space in bytes, or 0 if the task has been
     *         detached or completed.
     */
    [[nodiscard]] size_t stack_high_water_mark() const noexcept;

    /**
     * @brief Checks if the task function has returned.
     *
     * @return true if the task function has completed, false otherwise.
     */
    [[nodiscard]] bool is_completed() const noexcept;

    /**
     * @brief Checks if this task object owns the task.
     *
     * Returns false if the task has been detached or was never successfully created.
     *
     * @return true if this object owns the task handle, false otherwise.
     */
    [[nodiscard]] bool joinable() const noexcept { return _handle != nullptr; }

    /**
     * @brief Requests the task to stop.
     *
     * Requests the task to stop cooperatively. The task function can observe this
     * via self::stop_requested(). Also requested automatically by the destructor
     * before joining.
     *
     * @return true if the stop was newly requested (flag was previously false),
     *         false if stop was already requested or the task is not joinable.
     */
    bool request_stop() noexcept;

    /**
     * @brief Checks if a stop has been requested for this task.
     *
     * @return true if request_stop() has been called, false if not or if the task is not joinable.
     */
    [[nodiscard]] bool stop_requested() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Suspends the task.
     *
     * A suspended task will not run until resume() is called.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error if the task has been detached or completed.
     */
    void suspend() { unwrap(try_suspend()); }

    /**
     * @brief Resumes a suspended task.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error if the task has been detached or completed.
     */
    void resume() { unwrap(try_resume()); }
#endif

    /**
     * @brief Suspends the task.
     *
     * A suspended task will not run until resume() is called.
     *
     * @return Success, or an error.
     * @retval invalid_state The task has been detached or completed.
     */
    result<void> try_suspend();

    /**
     * @brief Resumes a suspended task.
     *
     * @return Success, or an error.
     * @retval invalid_state The task has been detached or completed.
     */
    result<void> try_resume();

    /**
     * @brief Resumes a suspended task from ISR context.
     *
     * @return true if a context switch should be requested, false otherwise.
     *         Returns false if the task has been detached or completed.
     *
     * @note Pass the return value to idfxx::yield_from_isr() to perform
     *       the context switch if needed.
     *
     * @code
     * void IRAM_ATTR my_isr() {
     *     bool need_yield = worker_task->resume_from_isr();
     *     idfxx::yield_from_isr(need_yield);
     * }
     * @endcode
     */
    [[nodiscard]] bool resume_from_isr() noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a notification to the task.
     *
     * Increments the task's notification value, waking the task if it is
     * blocked in wait() or take(). Can be called multiple times before
     * the task receives — each call increments the notification count.
     *
     * Uses notification index 0, which is reserved for this purpose.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error if the task has been detached or completed.
     */
    void notify() { unwrap(try_notify()); }
#endif

    /**
     * @brief Sends a notification to the task.
     *
     * Increments the task's notification value, waking the task if it is
     * blocked in wait() or take(). Can be called multiple times before
     * the task receives — each call increments the notification count.
     *
     * Uses notification index 0, which is reserved for this purpose.
     *
     * @return Success, or an error.
     * @retval invalid_state The task has been detached or completed.
     */
    result<void> try_notify();

    /**
     * @brief Sends a notification to the task from ISR context.
     *
     * Increments the task's notification value, waking the task if it is
     * blocked in wait() or take().
     *
     * Uses notification index 0, which is reserved for this purpose.
     *
     * @return true if a context switch should be requested, false otherwise.
     *         Returns false if the task has been detached or completed.
     *
     * @note Pass the return value to idfxx::yield_from_isr() to perform
     *       the context switch if needed.
     *
     * @code
     * void IRAM_ATTR my_isr() {
     *     bool need_yield = worker_task->notify_from_isr();
     *     idfxx::yield_from_isr(need_yield);
     * }
     * @endcode
     */
    [[nodiscard]] bool notify_from_isr() noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Changes the task priority.
     *
     * @param new_priority The new priority level.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error if the task has been detached or completed.
     */
    void set_priority(unsigned int new_priority) { unwrap(try_set_priority(new_priority)); }
#endif

    /**
     * @brief Changes the task priority.
     *
     * @param new_priority The new priority level.
     * @return Success, or an error.
     * @retval invalid_state The task has been detached or completed.
     */
    [[nodiscard]] result<void> try_set_priority(unsigned int new_priority);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Releases ownership of the task.
     *
     * After detaching, the task becomes self-managing and cleans up its resources
     * automatically when the task function returns. The task object becomes
     * non-joinable and the destructor becomes a no-op.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error if the task has already been detached.
     */
    void detach() { unwrap(try_detach()); }
#endif

    /**
     * @brief Releases ownership of the task.
     *
     * After detaching, the task becomes self-managing and cleans up its resources
     * automatically when the task function returns. The task object becomes
     * non-joinable and the destructor becomes a no-op.
     *
     * If the task has already completed, ownership is released and resources are
     * cleaned up immediately.
     *
     * @return Success, or an error.
     * @retval invalid_state The task has already been detached.
     */
    result<void> try_detach();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Immediately terminates the task without waiting for completion.
     *
     * Immediately terminates the task. After killing, the task becomes non-joinable.
     *
     * @warning This is a dangerous operation that should only be used as a last resort.
     * The task is terminated abruptly at whatever point it is executing:
     * - C++ destructors for stack-allocated objects will **not** run
     * - Mutexes and locks held by the task will **not** be released
     * - Dynamic memory allocated by the task will **not** be freed
     * - Any invariants the task was maintaining may be left in an inconsistent state
     *
     * These consequences can lead to resource leaks, deadlocks, and undefined behavior
     * in the rest of the system. Prefer using request_stop() and join() to allow the
     * task to complete gracefully, or allow the destructor to block.
     *
     * If the task function has already returned by the time this method is called,
     * this method simply cleans up resources.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error if the task has been detached, already joined, or
     *         if called from within the task itself.
     */
    void kill() { unwrap(try_kill()); }
#endif

    /**
     * @brief Immediately terminates the task without waiting for completion.
     *
     * Immediately terminates the task. After killing, the task becomes non-joinable.
     *
     * @warning This is a dangerous operation that should only be used as a last resort.
     * The task is terminated abruptly at whatever point it is executing:
     * - C++ destructors for stack-allocated objects will **not** run
     * - Mutexes and locks held by the task will **not** be released
     * - Dynamic memory allocated by the task will **not** be freed
     * - Any invariants the task was maintaining may be left in an inconsistent state
     *
     * These consequences can lead to resource leaks, deadlocks, and undefined behavior
     * in the rest of the system. Prefer using request_stop() and try_join() to allow the
     * task to complete gracefully, or allow the destructor to block.
     *
     * If the task function has already returned by the time this method is called,
     * this method simply cleans up resources.
     *
     * @return Success, or an error.
     * @retval invalid_state The task has been detached, already joined, or
     *         called from within the task itself.
     */
    result<void> try_kill();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Blocks until the task function completes.
     *
     * Waits indefinitely for the task to finish. After a successful join, the
     * task becomes non-joinable. Prefer join() with a timeout to avoid blocking
     * indefinitely on tasks that may not return promptly.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error if the task has been detached or already joined
     *         (`idfxx::errc::invalid_state`), or if called from within the task
     *         itself (`std::errc::resource_deadlock_would_occur`).
     */
    void join() { unwrap(try_join()); }

    /**
     * @brief Blocks until the task function completes or the timeout expires.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param timeout Maximum time to wait for the task to complete.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error if the task has been detached or already joined
     *         (`idfxx::errc::invalid_state`), if called from within the task
     *         itself (`std::errc::resource_deadlock_would_occur`), or if the
     *         timeout expires (`idfxx::errc::timeout`).
     */
    template<typename Rep, typename Period>
    void join(const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_join(timeout));
    }

    /**
     * @brief Blocks until the task function completes or the deadline is reached.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param deadline The time point at which to stop waiting.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error if the task has been detached or already joined
     *         (`idfxx::errc::invalid_state`), if called from within the task
     *         itself (`std::errc::resource_deadlock_would_occur`), or if the
     *         deadline is reached (`idfxx::errc::timeout`).
     */
    template<typename Clock, typename Duration>
    void join_until(const std::chrono::time_point<Clock, Duration>& deadline) {
        unwrap(try_join_until(deadline));
    }
#endif

    /**
     * @brief Blocks until the task function completes.
     *
     * Waits indefinitely for the task to finish. After a successful join, the
     * task becomes non-joinable. Prefer try_join() with a timeout to avoid
     * blocking indefinitely on tasks that may not return promptly.
     *
     * @return Success, or an error.
     * @retval invalid_state The task has been detached or already joined.
     * @retval std::errc::resource_deadlock_would_occur Called from within the task itself.
     */
    [[nodiscard]] result<void> try_join();

    /**
     * @brief Blocks until the task function completes or the timeout expires.
     *
     * After a successful join, the task becomes non-joinable.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param timeout Maximum time to wait for the task to complete.
     * @return Success, or an error.
     * @retval invalid_state The task has been detached or already joined.
     * @retval std::errc::resource_deadlock_would_occur Called from within the task itself.
     * @retval timeout The task did not complete within the specified duration.
     *         The task remains joinable and can be joined again.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_join(const std::chrono::duration<Rep, Period>& timeout) {
        return _try_join(chrono::ticks(timeout));
    }

    /**
     * @brief Blocks until the task function completes or the deadline is reached.
     *
     * After a successful join, the task becomes non-joinable.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param deadline The time point at which to stop waiting.
     * @return Success, or an error.
     * @retval invalid_state The task has been detached or already joined.
     * @retval std::errc::resource_deadlock_would_occur Called from within the task itself.
     * @retval timeout The task did not complete before the deadline.
     *         The task remains joinable and can be joined again.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] result<void> try_join_until(const std::chrono::time_point<Clock, Duration>& deadline) {
        auto remaining = deadline - Clock::now();
        if (remaining <= decltype(remaining)::zero()) {
            return _try_join(0);
        }
        return _try_join(chrono::ticks(remaining));
    }

    // =========================================================================
    // Static utility methods for current task operations
    // =========================================================================

    /**
     * @brief Returns the handle of the currently executing task.
     *
     * @return The current task's TaskHandle_t.
     */
    [[nodiscard]] static TaskHandle_t current_handle() noexcept { return xTaskGetCurrentTaskHandle(); }

    /**
     * @brief Returns the name of the currently executing task.
     *
     * @return The current task's name.
     */
    [[nodiscard]] static std::string current_name() { return pcTaskGetName(nullptr); }

private:
    static void trampoline(void* arg);

    [[nodiscard]] static TaskHandle_t _create(context* ctx, const config& cfg);
    [[nodiscard]] result<void> _try_join(TickType_t ticks);

    explicit task(TaskHandle_t handle, std::string name, context* ctx);

    TaskHandle_t _handle;
    std::string _name;
    context* _context;
};

/** @} */ // end of idfxx_task

} // namespace idfxx
