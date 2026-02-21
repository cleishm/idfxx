// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/queue>
 * @file queue.hpp
 * @brief Type-safe inter-task message queue.
 *
 * @defgroup idfxx_queue Queue Component
 * @brief Type-safe queue for inter-task communication.
 *
 * Provides a type-safe, fixed-size message queue with support for blocking
 * sends and receives, timeouts, deadlines, ISR operations, and dual error
 * handling (exception-based and result-based APIs).
 *
 * Depends on @ref idfxx_core for error handling and chrono utilities.
 * @{
 */

#include <idfxx/chrono>
#include <idfxx/error>
#include <idfxx/memory>

#include <chrono>
#include <cstddef>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace idfxx {

/**
 * @headerfile <idfxx/queue>
 * @brief Type-safe inter-task message queue.
 *
 * A fixed-size, FIFO message queue for passing messages between tasks and
 * between ISRs and tasks. Messages are copied into and out of the queue
 * by value.
 *
 * Queues are non-copyable and non-movable. Use the factory method make() for
 * result-based construction or the throwing constructor when exceptions are enabled.
 *
 * @tparam T The message type. Must be trivially copyable.
 *
 * @code
 * // Create a queue that can hold up to 10 integers
 * idfxx::queue<int> q(10);
 *
 * // Send and receive
 * q.send(42);
 * int value = q.receive();
 *
 * // With timeout
 * q.send(99, 100ms);
 * int value2 = q.receive(100ms);
 * @endcode
 */
template<typename T>
    requires std::is_trivially_copyable_v<T>
class queue {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a queue with the specified capacity.
     *
     * @param length Maximum number of items the queue can hold.
     * @param mem_type Memory region for queue storage allocation.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::invalid_arg if length is 0.
     * @throws std::system_error with idfxx::errc::invalid_arg if length is 0.
     * @throws std::bad_alloc if memory allocation fails.
     */
    [[nodiscard]] explicit queue(size_t length, memory_type mem_type = memory_type::internal)
        : _handle(nullptr) {
        if (length == 0) {
            throw std::system_error(errc::invalid_arg);
        }
        _handle = xQueueCreateWithCaps(length, sizeof(T), std::to_underlying(mem_type));
        if (_handle == nullptr) {
            raise_no_mem();
        }
    }
#endif

    /**
     * @brief Creates a queue with the specified capacity.
     *
     * @param length Maximum number of items the queue can hold.
     * @param mem_type Memory region for queue storage allocation.
     * @return The new queue, or an error.
     * @retval invalid_arg length is 0.
     */
    [[nodiscard]] static result<std::unique_ptr<queue>>
    make(size_t length, memory_type mem_type = memory_type::internal) {
        if (length == 0) {
            return error(errc::invalid_arg);
        }
        auto q = std::unique_ptr<queue>(new queue());
        q->_handle = xQueueCreateWithCaps(length, sizeof(T), std::to_underlying(mem_type));
        if (q->_handle == nullptr) {
            raise_no_mem();
        }
        return q;
    }

    /**
     * @brief Destroys the queue and releases all resources.
     *
     * Any items remaining in the queue are discarded.
     */
    ~queue() {
        if (_handle != nullptr) {
            vQueueDeleteWithCaps(_handle);
        }
    }

    // Non-copyable and non-movable
    queue(const queue&) = delete;
    queue& operator=(const queue&) = delete;
    queue(queue&&) = delete;
    queue& operator=(queue&&) = delete;

    // =========================================================================
    // Send operations
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends an item to the back of the queue, blocking indefinitely.
     *
     * Blocks until space is available in the queue.
     *
     * @param item The item to send.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains full.
     */
    void send(const T& item) { unwrap(try_send(item)); }

    /**
     * @brief Sends an item to the back of the queue with a timeout.
     *
     * Blocks until space is available or the timeout expires.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param item The item to send.
     * @param timeout Maximum time to wait for space.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains full
     *         for the duration.
     */
    template<typename Rep, typename Period>
    void send(const T& item, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_send(item, timeout));
    }

    /**
     * @brief Sends an item to the back of the queue with a deadline.
     *
     * Blocks until space is available or the deadline is reached.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param item The item to send.
     * @param deadline The time point at which to stop waiting.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains full
     *         until the deadline.
     */
    template<typename Clock, typename Duration>
    void send_until(const T& item, const std::chrono::time_point<Clock, Duration>& deadline) {
        unwrap(try_send_until(item, deadline));
    }

    /**
     * @brief Sends an item to the front of the queue, blocking indefinitely.
     *
     * Blocks until space is available in the queue. The item is placed at the
     * front, so it will be the next item received.
     *
     * @param item The item to send.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains full.
     */
    void send_to_front(const T& item) { unwrap(try_send_to_front(item)); }

    /**
     * @brief Sends an item to the front of the queue with a timeout.
     *
     * Blocks until space is available or the timeout expires. The item is
     * placed at the front, so it will be the next item received.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param item The item to send.
     * @param timeout Maximum time to wait for space.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains full
     *         for the duration.
     */
    template<typename Rep, typename Period>
    void send_to_front(const T& item, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_send_to_front(item, timeout));
    }

    /**
     * @brief Sends an item to the front of the queue with a deadline.
     *
     * Blocks until space is available or the deadline is reached. The item is
     * placed at the front, so it will be the next item received.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param item The item to send.
     * @param deadline The time point at which to stop waiting.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains full
     *         until the deadline.
     */
    template<typename Clock, typename Duration>
    void send_to_front_until(const T& item, const std::chrono::time_point<Clock, Duration>& deadline) {
        unwrap(try_send_to_front_until(item, deadline));
    }
#endif

    /**
     * @brief Sends an item to the back of the queue, blocking indefinitely.
     *
     * Blocks until space is available in the queue.
     *
     * @param item The item to send.
     * @return Success, or an error.
     * @retval timeout The queue remained full.
     */
    [[nodiscard]] result<void> try_send(const T& item) { return _try_send(item, portMAX_DELAY); }

    /**
     * @brief Sends an item to the back of the queue with a timeout.
     *
     * Blocks until space is available or the timeout expires.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param item The item to send.
     * @param timeout Maximum time to wait for space.
     * @return Success, or an error.
     * @retval timeout The queue remained full for the duration.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_send(const T& item, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_send(item, chrono::ticks(timeout));
    }

    /**
     * @brief Sends an item to the back of the queue with a deadline.
     *
     * Blocks until space is available or the deadline is reached.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param item The item to send.
     * @param deadline The time point at which to stop waiting.
     * @return Success, or an error.
     * @retval timeout The queue remained full until the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] result<void> try_send_until(const T& item, const std::chrono::time_point<Clock, Duration>& deadline) {
        auto remaining = deadline - Clock::now();
        if (remaining <= decltype(remaining)::zero()) {
            return _try_send(item, 0);
        }
        return _try_send(item, chrono::ticks(remaining));
    }

    /**
     * @brief Sends an item to the front of the queue, blocking indefinitely.
     *
     * Blocks until space is available in the queue. The item is placed at the
     * front, so it will be the next item received.
     *
     * @param item The item to send.
     * @return Success, or an error.
     * @retval timeout The queue remained full.
     */
    [[nodiscard]] result<void> try_send_to_front(const T& item) { return _try_send_to_front(item, portMAX_DELAY); }

    /**
     * @brief Sends an item to the front of the queue with a timeout.
     *
     * Blocks until space is available or the timeout expires. The item is
     * placed at the front, so it will be the next item received.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param item The item to send.
     * @param timeout Maximum time to wait for space.
     * @return Success, or an error.
     * @retval timeout The queue remained full for the duration.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_send_to_front(const T& item, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_send_to_front(item, chrono::ticks(timeout));
    }

    /**
     * @brief Sends an item to the front of the queue with a deadline.
     *
     * Blocks until space is available or the deadline is reached. The item is
     * placed at the front, so it will be the next item received.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param item The item to send.
     * @param deadline The time point at which to stop waiting.
     * @return Success, or an error.
     * @retval timeout The queue remained full until the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] result<void>
    try_send_to_front_until(const T& item, const std::chrono::time_point<Clock, Duration>& deadline) {
        auto remaining = deadline - Clock::now();
        if (remaining <= decltype(remaining)::zero()) {
            return _try_send_to_front(item, 0);
        }
        return _try_send_to_front(item, chrono::ticks(remaining));
    }

    // =========================================================================
    // Overwrite operations
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Overwrites the last item in the queue, or sends if the queue is not full.
     *
     * If the queue is full, the most recently written item is overwritten.
     * If the queue is not full, the item is added to the back. This is most
     * useful with a queue of length 1 to implement a "latest value" mailbox.
     *
     * This operation never blocks and always succeeds.
     *
     * @param item The item to write.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     */
    void overwrite(const T& item) noexcept { try_overwrite(item); }
#endif

    /**
     * @brief Overwrites the last item in the queue, or sends if the queue is not full.
     *
     * If the queue is full, the most recently written item is overwritten.
     * If the queue is not full, the item is added to the back. This is most
     * useful with a queue of length 1 to implement a "latest value" mailbox.
     *
     * This operation never blocks and always succeeds.
     *
     * @param item The item to write.
     */
    void try_overwrite(const T& item) noexcept { xQueueOverwrite(_handle, &item); }

    // =========================================================================
    // Receive operations
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives an item from the queue, blocking indefinitely.
     *
     * Blocks until an item is available.
     *
     * @return The received item.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains empty.
     */
    [[nodiscard]] T receive() { return unwrap(try_receive()); }

    /**
     * @brief Receives an item from the queue with a timeout.
     *
     * Blocks until an item is available or the timeout expires.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param timeout Maximum time to wait for an item.
     * @return The received item.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains empty
     *         for the duration.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] T receive(const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_receive(timeout));
    }

    /**
     * @brief Receives an item from the queue with a deadline.
     *
     * Blocks until an item is available or the deadline is reached.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param deadline The time point at which to stop waiting.
     * @return The received item.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains empty
     *         until the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] T receive_until(const std::chrono::time_point<Clock, Duration>& deadline) {
        return unwrap(try_receive_until(deadline));
    }
#endif

    /**
     * @brief Receives an item from the queue, blocking indefinitely.
     *
     * Blocks until an item is available.
     *
     * @return The received item, or an error.
     * @retval timeout The queue remained empty.
     */
    [[nodiscard]] result<T> try_receive() { return _try_receive(portMAX_DELAY); }

    /**
     * @brief Receives an item from the queue with a timeout.
     *
     * Blocks until an item is available or the timeout expires.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param timeout Maximum time to wait for an item.
     * @return The received item, or an error.
     * @retval timeout The queue remained empty for the duration.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<T> try_receive(const std::chrono::duration<Rep, Period>& timeout) {
        return _try_receive(chrono::ticks(timeout));
    }

    /**
     * @brief Receives an item from the queue with a deadline.
     *
     * Blocks until an item is available or the deadline is reached.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param deadline The time point at which to stop waiting.
     * @return The received item, or an error.
     * @retval timeout The queue remained empty until the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] result<T> try_receive_until(const std::chrono::time_point<Clock, Duration>& deadline) {
        auto remaining = deadline - Clock::now();
        if (remaining <= decltype(remaining)::zero()) {
            return _try_receive(0);
        }
        return _try_receive(chrono::ticks(remaining));
    }

    // =========================================================================
    // Peek operations
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Peeks at the front item in the queue without removing it, blocking indefinitely.
     *
     * Blocks until an item is available. The item remains in the queue after peeking.
     *
     * @return A copy of the front item.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains empty.
     */
    [[nodiscard]] T peek() { return unwrap(try_peek()); }

    /**
     * @brief Peeks at the front item in the queue without removing it, with a timeout.
     *
     * Blocks until an item is available or the timeout expires. The item remains
     * in the queue after peeking.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param timeout Maximum time to wait for an item.
     * @return A copy of the front item.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains empty
     *         for the duration.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] T peek(const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_peek(timeout));
    }

    /**
     * @brief Peeks at the front item in the queue without removing it, with a deadline.
     *
     * Blocks until an item is available or the deadline is reached. The item
     * remains in the queue after peeking.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param deadline The time point at which to stop waiting.
     * @return A copy of the front item.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the queue remains empty
     *         until the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] T peek_until(const std::chrono::time_point<Clock, Duration>& deadline) {
        return unwrap(try_peek_until(deadline));
    }
#endif

    /**
     * @brief Peeks at the front item in the queue without removing it, blocking indefinitely.
     *
     * Blocks until an item is available. The item remains in the queue after peeking.
     *
     * @return A copy of the front item, or an error.
     * @retval timeout The queue remained empty.
     */
    [[nodiscard]] result<T> try_peek() { return _try_peek(portMAX_DELAY); }

    /**
     * @brief Peeks at the front item in the queue without removing it, with a timeout.
     *
     * Blocks until an item is available or the timeout expires. The item remains
     * in the queue after peeking.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param timeout Maximum time to wait for an item.
     * @return A copy of the front item, or an error.
     * @retval timeout The queue remained empty for the duration.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<T> try_peek(const std::chrono::duration<Rep, Period>& timeout) {
        return _try_peek(chrono::ticks(timeout));
    }

    /**
     * @brief Peeks at the front item in the queue without removing it, with a deadline.
     *
     * Blocks until an item is available or the deadline is reached. The item
     * remains in the queue after peeking.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param deadline The time point at which to stop waiting.
     * @return A copy of the front item, or an error.
     * @retval timeout The queue remained empty until the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] result<T> try_peek_until(const std::chrono::time_point<Clock, Duration>& deadline) {
        auto remaining = deadline - Clock::now();
        if (remaining <= decltype(remaining)::zero()) {
            return _try_peek(0);
        }
        return _try_peek(chrono::ticks(remaining));
    }

    // =========================================================================
    // ISR operations
    // =========================================================================

    /**
     * @headerfile <idfxx/queue>
     * @brief Result of an ISR send operation.
     */
    struct isr_send_result {
        bool success; ///< true if the item was sent successfully.
        bool yield;   ///< true if a context switch should be requested.
    };

    /**
     * @headerfile <idfxx/queue>
     * @brief Result of an ISR receive operation.
     */
    struct isr_receive_result {
        std::optional<T> item; ///< The received item, or std::nullopt if the queue was empty.
        bool yield;            ///< true if a context switch should be requested.
    };

    /**
     * @brief Sends an item to the back of the queue from ISR context.
     *
     * @param item The item to send.
     * @return Result containing success status and whether a context switch should
     *         be requested.
     *
     * @note Pass the yield field to idfxx::yield_from_isr() to perform
     *       the context switch if needed.
     *
     * @code
     * void IRAM_ATTR my_isr() {
     *     auto [success, yield] = q->send_from_isr(value);
     *     idfxx::yield_from_isr(yield);
     * }
     * @endcode
     */
    [[nodiscard]] isr_send_result IRAM_ATTR send_from_isr(const T& item) noexcept {
        BaseType_t woken = pdFALSE;
        BaseType_t ret = xQueueSendFromISR(_handle, &item, &woken);
        return {ret == pdTRUE, woken == pdTRUE};
    }

    /**
     * @brief Sends an item to the front of the queue from ISR context.
     *
     * @param item The item to send.
     * @return Result containing success status and whether a context switch should
     *         be requested.
     *
     * @note Pass the yield field to idfxx::yield_from_isr() to perform
     *       the context switch if needed.
     *
     * @code
     * void IRAM_ATTR my_isr() {
     *     auto [success, yield] = q->send_to_front_from_isr(urgent_value);
     *     idfxx::yield_from_isr(yield);
     * }
     * @endcode
     */
    [[nodiscard]] isr_send_result IRAM_ATTR send_to_front_from_isr(const T& item) noexcept {
        BaseType_t woken = pdFALSE;
        BaseType_t ret = xQueueSendToFrontFromISR(_handle, &item, &woken);
        return {ret == pdTRUE, woken == pdTRUE};
    }

    /**
     * @brief Overwrites the last item in the queue from ISR context.
     *
     * If the queue is full, the most recently written item is overwritten.
     * This operation always succeeds.
     *
     * @param item The item to write.
     * @return true if a context switch should be requested, false otherwise.
     *
     * @note Pass the return value to idfxx::yield_from_isr() to perform
     *       the context switch if needed.
     *
     * @code
     * void IRAM_ATTR my_isr() {
     *     bool yield = q->overwrite_from_isr(latest_value);
     *     idfxx::yield_from_isr(yield);
     * }
     * @endcode
     */
    [[nodiscard]] bool IRAM_ATTR overwrite_from_isr(const T& item) noexcept {
        BaseType_t woken = pdFALSE;
        xQueueOverwriteFromISR(_handle, &item, &woken);
        return woken == pdTRUE;
    }

    /**
     * @brief Receives an item from the queue in ISR context.
     *
     * @return Result containing the received item (or std::nullopt if the queue
     *         was empty) and whether a context switch should be requested.
     *
     * @note Pass the yield field to idfxx::yield_from_isr() to perform
     *       the context switch if needed.
     *
     * @code
     * void IRAM_ATTR my_isr() {
     *     auto [item, yield] = q->receive_from_isr();
     *     if (item) {
     *         // process *item
     *     }
     *     idfxx::yield_from_isr(yield);
     * }
     * @endcode
     */
    [[nodiscard]] isr_receive_result IRAM_ATTR receive_from_isr() noexcept {
        T item;
        BaseType_t woken = pdFALSE;
        if (xQueueReceiveFromISR(_handle, &item, &woken) == pdTRUE) {
            return {item, woken == pdTRUE};
        }
        return {std::nullopt, woken == pdTRUE};
    }

    /**
     * @brief Peeks at the front item in the queue from ISR context without removing it.
     *
     * @return The front item, or std::nullopt if the queue is empty.
     *
     * @code
     * void IRAM_ATTR my_isr() {
     *     if (auto item = q->peek_from_isr()) {
     *         // inspect *item without removing it
     *     }
     * }
     * @endcode
     */
    [[nodiscard]] std::optional<T> IRAM_ATTR peek_from_isr() const noexcept {
        T item;
        if (xQueuePeekFromISR(_handle, &item) == pdTRUE) {
            return item;
        }
        return std::nullopt;
    }

    // =========================================================================
    // Query and reset
    // =========================================================================

    /**
     * @brief Returns the number of items currently in the queue.
     *
     * @return The number of items in the queue.
     */
    [[nodiscard]] size_t size() const noexcept { return uxQueueMessagesWaiting(_handle); }

    /**
     * @brief Returns the number of free spaces in the queue.
     *
     * @return The number of items that can be sent before the queue is full.
     */
    [[nodiscard]] size_t available() const noexcept { return uxQueueSpacesAvailable(_handle); }

    /**
     * @brief Checks if the queue is empty.
     *
     * @return true if the queue contains no items, false otherwise.
     */
    [[nodiscard]] bool empty() const noexcept { return uxQueueMessagesWaiting(_handle) == 0; }

    /**
     * @brief Checks if the queue is full.
     *
     * @return true if the queue has no free spaces, false otherwise.
     */
    [[nodiscard]] bool full() const noexcept { return uxQueueSpacesAvailable(_handle) == 0; }

    /**
     * @brief Returns the underlying FreeRTOS queue handle.
     *
     * Provides access to the raw handle for interoperability with ESP-IDF
     * APIs that require a QueueHandle_t.
     *
     * @return The QueueHandle_t.
     */
    [[nodiscard]] QueueHandle_t idf_handle() const noexcept { return _handle; }

    /**
     * @brief Removes all items from the queue.
     *
     * After reset, the queue is empty and all previously stored items are discarded.
     */
    void reset() noexcept { xQueueReset(_handle); }

private:
    queue() noexcept
        : _handle(nullptr) {}

    [[nodiscard]] result<void> _try_send(const T& item, TickType_t ticks) {
        if (xQueueSend(_handle, &item, ticks) != pdTRUE) {
            return error(errc::timeout);
        }
        return {};
    }

    [[nodiscard]] result<void> _try_send_to_front(const T& item, TickType_t ticks) {
        if (xQueueSendToFront(_handle, &item, ticks) != pdTRUE) {
            return error(errc::timeout);
        }
        return {};
    }

    [[nodiscard]] result<T> _try_receive(TickType_t ticks) {
        T item;
        if (xQueueReceive(_handle, &item, ticks) != pdTRUE) {
            return error(errc::timeout);
        }
        return item;
    }

    [[nodiscard]] result<T> _try_peek(TickType_t ticks) {
        T item;
        if (xQueuePeek(_handle, &item, ticks) != pdTRUE) {
            return error(errc::timeout);
        }
        return item;
    }

    QueueHandle_t _handle;
};

/** @} */ // end of idfxx_queue

} // namespace idfxx
