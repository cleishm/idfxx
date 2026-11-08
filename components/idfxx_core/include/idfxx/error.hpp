// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/error>
 * @file error.hpp
 * @brief IDFXX error handling.
 *
 * @defgroup idfxx_core Core Utilities
 * @brief Core utilities for the idfxx component family.
 *
 * Provides foundational error handling, memory allocators, and chrono utilities
 * for modern C++ ESP-IDF development.
 *
 * @defgroup idfxx_core_error Error Handling
 * @ingroup idfxx_core
 * @brief Result-based error handling with ESP-IDF integration.
 *
 * Provides `idfxx::result<T>` (C++23 `std::expected`) for fallible operations
 * and `idfxx::errc` enum compatible with ESP-IDF error codes.
 * @{
 */

#include "sdkconfig.h"

#include <concepts>
#include <cstdlib>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

typedef int esp_err_t;

namespace idfxx {

/**
 * @headerfile <idfxx/error>
 * @brief IDFXX error codes.
 *
 * These error codes are compatible with ESP-IDF error codes and can be used
 * with std::error_code through the IDFXX error category.
 */
enum class errc : esp_err_t {
    // clang-format off
    fail                = -1,    /**< Generic failure. */
    invalid_arg         = 0x102, /**< Invalid argument */
    invalid_state       = 0x103, /**< Invalid state */
    invalid_size        = 0x104, /**< Invalid size */
    not_found           = 0x105, /**< Requested resource not found */
    not_supported       = 0x106, /**< Operation or feature not supported */
    timeout             = 0x107, /**< Operation timed out */
    invalid_response    = 0x108, /**< Received response was invalid */
    invalid_crc         = 0x109, /**< CRC or checksum was invalid */
    invalid_version     = 0x10A, /**< Version was invalid */
    invalid_mac         = 0x10B, /**< MAC address was invalid */
    not_finished        = 0x10C, /**< Operation has not fully completed */
    not_allowed         = 0x10D, /**< Operation is not allowed */
    // clang-format on
};

/**
 * @headerfile <idfxx/error>
 * @brief Error category for IDFXX and ESP-IDF error codes.
 *
 * This category provides human-readable error messages for IDFXX and ESP-IDF
 * error codes, enabling integration with the standard C++ error handling
 * mechanisms.
 */
class error_category : public std::error_category {
public:
    /**
     * @brief Returns the name of the error category.
     *
     * @return The category name.
     */
    [[nodiscard]] const char* name() const noexcept override final;

    /**
     * @brief Returns a human-readable message for the given error code.
     *
     * @param ec The error code value.
     *
     * @return A descriptive error message.
     */
    [[nodiscard]] std::string message(int ec) const override final;
};

/**
 * @brief Returns a reference to the IDFXX error category singleton.
 *
 * This follows the standard library pattern (like std::generic_category()).
 *
 * @return Reference to the singleton error_category instance.
 */
[[nodiscard]] const error_category& default_category() noexcept;

/** @brief Creates a std::error_code from an idfxx::errc value. */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), default_category()};
}

/**
 * @brief Creates a std::error_code from a general esp_err_t value.
 *
 * Maps common ESP-IDF error codes to corresponding idfxx::errc values, or
 * returns an std::error_code with idfxx::errc::fail for unknown ESP-IDF
 * error codes. Throws std::bad_alloc (or aborts) on ESP_ERR_NO_MEM.
 */
[[nodiscard]] std::error_code make_error_code(esp_err_t e);

/**
 * @brief result type wrapping a value or error code.
 *
 * @tparam T The success value type.
 */
template<typename T>
using result = std::expected<T, std::error_code>;

/**
 * @brief Concept for types that provide a static `make()` factory method returning `result<T>`.
 *
 * A type satisfies this concept if `T::make(args...)` is a valid expression
 * returning `result<T>` for the given argument types.
 *
 * @tparam T    The type to check.
 * @tparam Args The argument types to pass to `T::make()`.
 */
template<typename T, typename... Args>
concept has_factory = requires {
    { T::make(std::declval<Args>()...) } -> std::same_as<result<T>>;
};

/**
 * @brief Constructs a `T` via its `make()` factory and wraps it in a `std::unique_ptr`.
 *
 * Convenience helper for obtaining heap-allocated, polymorphically usable objects
 * from result-based factory methods.
 *
 * @tparam T    The type to construct. Must have a static `make()` method returning `result<T>`.
 * @tparam Args The argument types forwarded to `T::make()`.
 * @param args  Arguments forwarded to `T::make()`.
 *
 * @return A `result` containing a `unique_ptr<T>`, or an error.
 *
 * @code
 * auto r = idfxx::make_unique<ili9341>(panel_io, config);
 * if (r) {
 *     std::unique_ptr<panel> p = std::move(*r);
 * }
 * @endcode
 */
template<typename T, typename... Args>
    requires has_factory<T, Args...>
[[nodiscard]] result<std::unique_ptr<T>> make_unique(Args&&... args) {
    auto r = T::make(std::forward<Args>(args)...);
    if (!r) {
        return error(r.error());
    }
    return std::make_unique<T>(std::move(*r));
}

/**
 * @brief Creates an unexpected error from an error code enum.
 *
 * Convenience helper for returning errors from functions that return
 * `idfxx::result<T>`. Works with any registered error code enum
 * (`idfxx::errc`, `nvs::errc`, etc.) via ADL `make_error_code`.
 *
 * @tparam E An error code enum type registered via `std::is_error_code_enum`.
 * @param e The error code enum value.
 * @return An unexpected value suitable for returning from result-returning functions.
 *
 * @code
 * idfxx::result<int> read_sensor() {
 *     if (error_condition) {
 *         return idfxx::error(idfxx::errc::invalid_state);
 *     }
 *     return 42;
 * }
 * @endcode
 */
template<typename E>
    requires std::is_error_code_enum_v<E>
[[nodiscard]] constexpr std::unexpected<std::error_code> error(E e) noexcept {
    return std::unexpected<std::error_code>(e);
}

/**
 * @brief Creates an unexpected error from an error condition enum.
 *
 * Convenience helper for returning errors from functions that return
 * `idfxx::result<T>`. Works with any registered error condition enum
 * (`std::errc`, etc.) via ADL `make_error_code`.
 *
 * @tparam E An error condition enum type registered via `std::is_error_condition_enum`.
 * @param e The error condition enum value.
 * @return An unexpected value suitable for returning from result-returning functions.
 *
 * @code
 * idfxx::result<void> try_lock() {
 *     if (deadlock_detected) {
 *         return idfxx::error(std::errc::resource_deadlock_would_occur);
 *     }
 *     return {};
 * }
 * @endcode
 */
template<typename E>
    requires std::is_error_condition_enum_v<E>
[[nodiscard]] constexpr std::unexpected<std::error_code> error(E e) noexcept {
    return std::unexpected<std::error_code>(make_error_code(e));
}

/** @cond INTERNAL */
namespace detail {
/// ESP_ERR_NO_MEM value — verified by static_assert in error.cpp
constexpr esp_err_t esp_err_no_mem = 0x101;
} // namespace detail

/**
 * @brief Throws std::bad_alloc (or aborts) on out-of-memory.
 *
 * When exceptions are enabled, throws std::bad_alloc. Otherwise, calls abort().
 * Treats all OOM conditions as fatal, consistent with C++ operator new behavior.
 */
[[noreturn]] inline void raise_no_mem() {
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    throw std::bad_alloc();
#else
    abort();
#endif
}
/** @endcond */

/**
 * @brief Creates an unexpected error from an esp_err_t value.
 *
 * Convenience helper for returning errors from functions that return
 * `idfxx::result<T>`. Converts the ESP-IDF error code to a
 * `std::error_code` using `make_error_code`.
 *
 * Throws std::bad_alloc (or aborts) on ESP_ERR_NO_MEM.
 *
 * @param e The ESP-IDF error code.
 * @return An unexpected value suitable for returning from result-returning functions.
 */
[[nodiscard]] inline std::unexpected<std::error_code> error(esp_err_t e) {
    return std::unexpected<std::error_code>(make_error_code(e));
}

/**
 * @brief Creates an unexpected error from a std::error_code.
 *
 * Convenience helper for propagating errors between result types with
 * different value types.
 *
 * @param ec The error code.
 * @return An unexpected value suitable for returning from result-returning functions.
 *
 * @code
 * idfxx::result<int> try_compute() {
 *     idfxx::result<void> r = try_setup();
 *     if (!r) {
 *         return idfxx::error(r.error());
 *     }
 *     return 42;
 * }
 * @endcode
 */
[[nodiscard]] inline std::unexpected<std::error_code> error(std::error_code ec) noexcept {
    return std::unexpected<std::error_code>(ec);
}

/**
 * @brief Wraps an esp_err_t into a result<void>.
 *
 * Throws std::bad_alloc (or aborts) on ESP_ERR_NO_MEM.
 *
 * @param e The esp_err_t value to wrap.
 *
 * @return A result representing success or failure.
 */
[[nodiscard]] inline result<void> wrap(esp_err_t e) {
    if (e) {
        return error(e);
    }
    return {};
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Throws a std::system_error if the result is an error.
 *
 * @tparam T The success value type.
 *
 * @param result The result to check.
 *
 * @return The success value.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error if the result is an error.
 */
template<typename T>
[[nodiscard]] inline T unwrap(result<T> result) {
    return std::move(result).transform_error([](auto ec) -> std::error_code { throw std::system_error(ec); }).value();
}

// Specialization for void
template<>
inline void unwrap(result<void> result) {
    if (!result) {
        throw std::system_error(result.error());
    }
}
#endif

/** @cond INTERNAL */
namespace detail {

template<typename T>
inline constexpr bool is_result_v = false;

template<typename T>
inline constexpr bool is_result_v<result<T>> = true;

template<typename T>
struct guarded_result {
    using type = result<T>;
};

template<typename T>
struct guarded_result<result<T>> {
    using type = result<T>;
};

template<typename F>
using guarded_result_t = typename guarded_result<std::remove_cvref_t<std::invoke_result_t<F>>>::type;

template<typename F>
guarded_result_t<F> invoke_as_result(F&& fn) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
        std::invoke(std::forward<F>(fn));
        return {};
    } else {
        return std::invoke(std::forward<F>(fn));
    }
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/// Logs an exception caught by invoke_guarded. `subject` and `what` may be null.
void log_guarded_exception(const char* tag, const char* context, const char* subject, const char* what) noexcept;
#endif

template<typename F>
guarded_result_t<F> invoke_guarded_impl(const char* tag, const char* context, const char* subject, F&& fn) noexcept {
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    try {
        return invoke_as_result(std::forward<F>(fn));
    } catch (const std::bad_alloc&) {
        // Allocation failure is fatal
        std::terminate();
    } catch (const std::system_error& e) {
        // Equivalent to returning the error, for a callback that can return one
        if constexpr (!is_result_v<std::remove_cvref_t<std::invoke_result_t<F>>>) {
            log_guarded_exception(tag, context, subject, e.what());
        }
        return error(e.code());
    } catch (const std::exception& e) {
        log_guarded_exception(tag, context, subject, e.what());
    } catch (...) {
        log_guarded_exception(tag, context, subject, nullptr);
    }
    // errc is not yet declared an error code enum here, so convert explicitly
    return error(make_error_code(errc::fail));
#else
    (void)tag;
    (void)context;
    (void)subject;
    return invoke_as_result(std::forward<F>(fn));
#endif
}

} // namespace detail

/**
 * @brief Invokes a user callback from a context that an exception must never escape.
 *
 * Intended for callbacks invoked from ESP-IDF C code or from a task function,
 * where an escaping exception would terminate the program. The callback's
 * return value is returned as a result: `void` becomes `result<void>`, a
 * `result<T>` is returned as is, and any other type `U` becomes `result<U>`.
 *
 * When exceptions are enabled, an exception thrown by the callback is caught
 * and returned as an error: a std::system_error as its error code, and any
 * other exception as errc::fail. Each is logged as an error, as
 * "<context> threw: <what>", except a std::system_error thrown by a callback
 * returning `result<T>`, which is treated exactly like returning its error
 * code and left for the caller to handle. A std::bad_alloc is not caught:
 * allocation failure terminates the program, as elsewhere in idfxx. Without
 * exceptions, the callback is invoked directly.
 *
 * As a result, a caller acting on an error from a callback that does not
 * return `result<T>` need not log it again.
 *
 * @tparam F The callable type.
 * @param tag The log tag to use.
 * @param context What was invoked, for the log message (e.g. "Timer callback").
 * @param fn The callable to invoke, taking no arguments.
 * @return The callback's result, or the error it threw.
 */
template<typename F>
    requires std::invocable<F>
[[nodiscard]] detail::guarded_result_t<F> invoke_guarded(const char* tag, const char* context, F&& fn) noexcept {
    return detail::invoke_guarded_impl(tag, context, nullptr, std::forward<F>(fn));
}

/**
 * @brief Invokes a user callback from a context that an exception must never escape.
 *
 * As for invoke_guarded(const char*, const char*, F&&), with the log message
 * naming a subject: "<context> <subject> threw: <what>" (e.g. context
 * "Handler for" and a URI as the subject).
 *
 * @tparam F The callable type.
 * @param tag The log tag to use.
 * @param context What was invoked, for the log message.
 * @param subject The subject of the call, appended to the context in the log message.
 * @param fn The callable to invoke, taking no arguments.
 * @return The callback's result, or the error it threw.
 */
template<typename F>
    requires std::invocable<F>
[[nodiscard]] detail::guarded_result_t<F>
invoke_guarded(const char* tag, const char* context, const char* subject, F&& fn) noexcept {
    return detail::invoke_guarded_impl(tag, context, subject, std::forward<F>(fn));
}
/** @endcond */

/**
 * @brief Aborts the program if the result contains an error.
 *
 * This overload invokes an optional callback before aborting, allowing for
 * error logging or cleanup. The callback is invoked inline without type erasure,
 * enabling zero-overhead error handling.
 *
 * @warning For std::function or std::move_only_function, ensure the callback is
 *          not null to avoid std::bad_function_call.
 *
 * @tparam T The success value type.
 * @tparam Callback The callback type (deduced).
 * @param result The result to check.
 * @param on_error Function called with the error code before aborting. Must be
 *                 a valid callable (not null for function wrappers).
 */
template<typename T, typename Callback>
    requires std::invocable<Callback, std::error_code> &&
    std::same_as<std::invoke_result_t<Callback, std::error_code>, void>
inline void abort_on_error(result<T> result, Callback&& on_error) {
    if (result) {
        return;
    }
    // Check if the callback is valid (for types with operator bool)
    if constexpr (requires { static_cast<bool>(on_error); }) {
        if (!on_error) {
            std::abort();
        }
    }
    std::forward<Callback>(on_error)(result.error());
    std::abort();
}

/**
 * @brief Aborts the program if the result contains an error.
 *
 * @tparam T The success value type.
 * @param result The result to check.
 */
template<typename T>
inline void abort_on_error(result<T> result) {
    if (!result) {
        std::abort();
    }
}

} // namespace idfxx

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::errc> : true_type {};
} // namespace std
/** @endcond */

/** @} */ // end of idfxx_core_error
