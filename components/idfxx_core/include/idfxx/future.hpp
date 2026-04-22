// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/future>
 * @file future.hpp
 * @brief Async completion token.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_future Future
 * @brief Lightweight, copyable async completion tokens.
 *
 * Provides `idfxx::future<T>` — a `std::shared_future`-like handle for
 * awaiting the result of an asynchronous operation. Components producing
 * async results can expose them through a uniform interface regardless of
 * the underlying completion mechanism (polling queues, condition variables,
 * event groups, ...), and callers get a single, recognisable async API.
 * @{
 */

#include <idfxx/error.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace idfxx {

/**
 * @headerfile <idfxx/future>
 * @brief Async completion token.
 *
 * Provides a `std::shared_future`-like API for awaiting an asynchronous
 * operation. Callers can block until completion (`wait()` / `try_wait()`),
 * wait with a timeout (`wait_for()` / `try_wait_for()`), or poll for
 * completion non-blockingly (`done()`).
 *
 * A `future` is a lightweight, copyable handle: copies and moves are cheap,
 * and it can be freely stored in containers. All copies share the same
 * underlying operation, so a completion observed on one copy is visible on
 * every other copy. Any resources held by the producer on behalf of the
 * async operation are released automatically when the last copy is
 * destroyed.
 *
 * @tparam T The value type produced on successful completion. Use `void` for
 * pure completion notifications.
 *
 * @code
 * auto f = make_async_op();      // returns idfxx::future<int>
 * int value = f.wait();          // blocks until ready
 * if (f.done()) { ... }          // non-blocking poll
 * auto v = f.try_wait_for(10ms); // returns result<int>
 * @endcode
 */
template<typename T>
class future {
public:
    /** @brief Constructs an invalid (detached) future. */
    future() = default;

    /**
     * @brief Constructs a future from a waiter callable.
     *
     * Intended for producers exposing an async operation via @ref future. The
     * waiter is called to service `wait()` / `try_wait()` / `try_wait_for()`
     * and must honour the timeout argument:
     * - `std::nullopt` — wait indefinitely;
     * - an explicit duration — wait at most that long;
     * - `std::chrono::milliseconds{0}` — non-blocking check, returning
     *   immediately.
     *
     * The waiter returns `result<T>` — either the completed value or an
     * error (typically `idfxx::errc::timeout` when the deadline expires).
     *
     * With this constructor, `done()` is serviced by calling the waiter with
     * a zero timeout. Prefer the two-argument constructor when a cheap,
     * non-consuming completion check is available.
     *
     * @tparam W       Waiter callable type.
     * @param waiter   Waiter callable.
     *
     * @note Any resource captured by the waiter (for example an RAII handle
     * to an in-flight operation) is released automatically when the last
     * copy of the future is destroyed.
     */
    template<typename W>
        requires std::is_invocable_r_v<result<T>, W&, std::optional<std::chrono::milliseconds>>
    explicit future(W waiter)
        : _state(std::make_shared<state>(std::move(waiter))) {}

    /**
     * @brief Constructs a future with an explicit non-blocking done-check.
     *
     * Intended for producers that have a cheap, non-consuming way to check
     * completion (e.g. an atomic flag). When supplied, `done_check` is
     * called to service `done()` instead of the waiter, avoiding a spurious
     * wait round-trip.
     *
     * @tparam W           Waiter callable type.
     * @tparam D           Done-check callable type (must be noexcept).
     * @param waiter       Waiter callable (see single-argument constructor).
     * @param done_check   Non-blocking, noexcept completion check returning
     *                     `true` when the operation has completed.
     */
    template<typename W, typename D>
        requires std::is_invocable_r_v<result<T>, W&, std::optional<std::chrono::milliseconds>> &&
        std::is_nothrow_invocable_r_v<bool, const D&>
    future(W waiter, D done_check)
        : _state(std::make_shared<state>(std::move(waiter), std::move(done_check))) {}

    /**
     * @brief Returns whether this future is associated with an async operation.
     *
     * @return `true` if the future was constructed from a producer and has
     * not been moved-from; `false` for a default-constructed or moved-from
     * future.
     */
    [[nodiscard]] bool valid() const noexcept { return _state != nullptr; }

    /**
     * @brief Non-blocking check for whether the operation has completed.
     *
     * @return `true` if the operation is complete; also `true` for an
     * invalid future. `false` if the operation is still in progress.
     */
    [[nodiscard]] bool done() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Blocks until the operation completes.
     *
     * Safe to call on an invalid future: returns a value-initialized value.
     *
     * @return The completed value.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    T wait() const { return unwrap(try_wait()); }

    /**
     * @brief Blocks until the operation completes or the timeout expires.
     *
     * @tparam Rep     Duration arithmetic type.
     * @tparam Period  Duration period type.
     * @param timeout  Maximum time to wait for completion.
     *
     * @return The completed value.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure or timeout.
     */
    template<typename Rep, typename Period>
    T wait_for(const std::chrono::duration<Rep, Period>& timeout) const {
        return unwrap(try_wait_for(timeout));
    }
#endif

    /**
     * @brief Blocks until the operation completes.
     *
     * Safe to call on an invalid future: returns a value-initialized value.
     *
     * @return The completed value, or an error.
     */
    [[nodiscard]] result<T> try_wait() const;

    /**
     * @brief Blocks until the operation completes or the timeout expires.
     *
     * @tparam Rep     Duration arithmetic type.
     * @tparam Period  Duration period type.
     * @param timeout  Maximum time to wait for completion.
     *
     * @return The completed value, or an error (`idfxx::errc::timeout` if the
     * deadline expired).
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<T> try_wait_for(const std::chrono::duration<Rep, Period>& timeout) const;

private:
    /** @cond INTERNAL */
    struct state {
        std::move_only_function<result<T>(std::optional<std::chrono::milliseconds>)> waiter;
        std::move_only_function<bool() const noexcept> done_check;

        template<typename W>
        explicit state(W&& w)
            : waiter(std::forward<W>(w)) {}

        template<typename W, typename D>
        state(W&& w, D&& d)
            : waiter(std::forward<W>(w))
            , done_check(std::forward<D>(d)) {}
    };
    /** @endcond */

    std::shared_ptr<state> _state;
};

template<typename T>
bool future<T>::done() const noexcept {
    if (!_state) {
        return true;
    }
    if (_state->done_check) {
        return _state->done_check();
    }
    auto r = _state->waiter(std::optional<std::chrono::milliseconds>{std::chrono::milliseconds{0}});
    return r.has_value();
}

template<typename T>
result<T> future<T>::try_wait() const {
    if (!_state) {
        if constexpr (std::is_void_v<T>) {
            return {};
        } else {
            return T{};
        }
    }
    return _state->waiter(std::nullopt);
}

template<typename T>
template<typename Rep, typename Period>
result<T> future<T>::try_wait_for(const std::chrono::duration<Rep, Period>& timeout) const {
    if (!_state) {
        if constexpr (std::is_void_v<T>) {
            return {};
        } else {
            return T{};
        }
    }
    return _state->waiter(
        std::optional<std::chrono::milliseconds>{std::chrono::ceil<std::chrono::milliseconds>(timeout)}
    );
}

} // namespace idfxx

/** @} */ // end of idfxx_core_future
/** @} */ // end of idfxx_core
