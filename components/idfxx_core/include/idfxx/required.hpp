// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/required>
 * @file required.hpp
 * @brief Compile-time required fields for configuration aggregates.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_required Required Fields
 * @ingroup idfxx_core
 * @brief Configuration fields that must be set explicitly.
 *
 * Provides the `required<T>` template for fields of configuration structs
 * that have no safe default value. A `required<T>` member cannot be
 * default-constructed, so leaving it out of a designated initializer is a
 * compile-time error rather than a silently wrong default.
 *
 * @code
 * struct config {
 *     idfxx::required<quanta::electro::milliohms> r0; // no safe default
 *     int retries = 3;                                // optional, has a default
 * };
 *
 * config ok{.r0 = 100_Ohm};   // OK: ohms convert to milliohms
 * config bad{.retries = 5};   // error: r0 is not initialized
 * @endcode
 * @{
 */

#include <concepts>
#include <type_traits>
#include <utility>

namespace idfxx {

/** @cond INTERNAL */
template<typename T>
class required;

namespace detail {
// Used to test that U initializes T without narrowing, as copy-list-initialization
// of an aggregate member would (the technique std::variant's converting constructor uses).
template<typename T>
struct required_arr {
    T x[1];
};

template<typename T, typename U>
concept required_non_narrowing = requires(U&& u) { required_arr<T>{{std::forward<U>(u)}}; };

// Whether the integer v is representable in the integral type T.
template<typename T, typename U>
constexpr bool required_fits(U v) noexcept {
    if constexpr (std::same_as<T, bool>) {
        return v == U{0} || v == U{1};
    } else {
        const T t = static_cast<T>(v);
        if (static_cast<U>(t) != v) {
            return false;
        }
        if constexpr (std::is_signed_v<U> && !std::is_signed_v<T>) {
            return !(v < U{});
        } else if constexpr (!std::is_signed_v<U> && std::is_signed_v<T>) {
            return !(t < T{});
        } else {
            return true;
        }
    }
}

// Not constexpr: reaching it during constant evaluation makes the
// initialization ill-formed, as a narrowing integer constant would be.
inline void required_integer_constant_out_of_range() noexcept {}

template<typename T>
inline constexpr bool is_required_v = false;
template<typename T>
inline constexpr bool is_required_v<required<T>> = true;
} // namespace detail
/** @endcond */

/**
 * @headerfile <idfxx/required>
 * @brief A value that must be provided explicitly when its enclosing aggregate is initialized.
 *
 * `required<T>` holds a `T` but has no default constructor. Used as a member
 * of an aggregate configuration struct, it makes the field mandatory:
 * omitting it from a designated initializer (or value-initializing the
 * struct with `{}`) fails to compile.
 *
 * Any value implicitly convertible to `T` initializes it, so the field is
 * set exactly as a plain `T` member would be, including conversions such as
 * `quanta::electro::ohms` to `quanta::electro::milliohms` or
 * `std::chrono::milliseconds` to `std::chrono::microseconds`. Narrowing
 * conversions (such as `int` to `uint8_t`, `double` to `int`, or a pointer to
 * `bool`) are rejected, as they are for a plain member in aggregate
 * initialization; as there, an integer constant that fits in an integral `T`
 * is accepted (`.address = 0x3C` for a `required<uint16_t>`).
 *
 * A braced list does not initialize the field directly, because the
 * converting constructor cannot deduce its type; name the type instead, as in
 * `.range = std::pair{1, 2}`.
 *
 * Access the stored value with get(), `*` or `->`; these follow the value
 * category of the `required<T>`, so a move-only value such as a
 * `std::move_only_function` can be called through a non-const lvalue and
 * moved out of an rvalue (`std::move(cfg.callback).get()`). The implicit
 * conversion yields a `const T&`, so a `required<T>` can be passed wherever a
 * `const T&` is expected. `required<T>` is copyable and movable whenever `T`
 * is, and compares equal to another `required<T>` or to any value that `T`
 * compares equal to.
 *
 * @tparam T The type of the stored value.
 *
 * @code
 * struct sensor_config {
 *     idfxx::gpio cs = idfxx::gpio::nc();
 *     idfxx::required<std::chrono::microseconds> settle_time;
 * };
 *
 * sensor_config cfg{.cs = idfxx::gpio_5, .settle_time = 2ms};
 * std::chrono::microseconds t = cfg.settle_time; // 2000us
 *
 * sensor_config missing{.cs = idfxx::gpio_5}; // compile error: settle_time is required
 * @endcode
 */
template<typename T>
class required {
public:
    /** @brief The type of the stored value. */
    using value_type = T;

    /**
     * @brief Initializes the stored value from any value implicitly convertible to `T` without narrowing.
     *
     * @tparam U The source type, implicitly convertible to `T` without narrowing.
     * @param value The value to store.
     */
    template<typename U>
        requires(
            !std::same_as<std::remove_cvref_t<U>, required> && std::convertible_to<U, T> &&
            detail::required_non_narrowing<T, U>
        )
    constexpr required(U&& value) noexcept(std::is_nothrow_constructible_v<T, U>)
        : _value(std::forward<U>(value)) {}

    /**
     * @brief Initializes an integral stored value from an integer constant that fits in `T`.
     *
     * Accepts a constant expression of a wider integer type when its value is
     * representable in `T`, as aggregate initialization of a plain `T` member
     * does (so `.address = 0x3C` initializes a `required<uint16_t>`). A value
     * that does not fit, or one that is not a constant expression, fails to
     * compile.
     *
     * @tparam U The source integer type, whose conversion to `T` can narrow.
     * @param value The integer constant to store.
     */
    template<typename U>
        requires(std::integral<T> && std::integral<std::remove_cvref_t<U>> && !detail::required_non_narrowing<T, U>)
    consteval required(U&& value) noexcept
        : _value(static_cast<T>(value)) {
        if (!detail::required_fits<T, std::remove_cvref_t<U>>(value)) {
            detail::required_integer_constant_out_of_range();
        }
    }

    /**
     * @brief Returns the stored value.
     *
     * @return A const reference to the stored value.
     */
    [[nodiscard]] constexpr const T& get() const& noexcept { return _value; }

    /**
     * @brief Returns the stored value.
     *
     * @return A mutable reference to the stored value.
     */
    [[nodiscard]] constexpr T& get() & noexcept { return _value; }

    /**
     * @brief Returns the stored value for moving out.
     *
     * @return An rvalue reference to the stored value.
     */
    [[nodiscard]] constexpr T&& get() && noexcept { return std::move(_value); }

    /**
     * @brief Returns the stored value.
     *
     * @return A const reference to the stored value.
     */
    [[nodiscard]] constexpr const T& operator*() const& noexcept { return _value; }

    /**
     * @brief Returns the stored value.
     *
     * @return A mutable reference to the stored value.
     */
    [[nodiscard]] constexpr T& operator*() & noexcept { return _value; }

    /**
     * @brief Returns the stored value for moving out.
     *
     * @return An rvalue reference to the stored value.
     */
    [[nodiscard]] constexpr T&& operator*() && noexcept { return std::move(_value); }

    /**
     * @brief Accesses members of the stored value.
     *
     * @return A pointer to the const stored value.
     */
    [[nodiscard]] constexpr const T* operator->() const noexcept { return &_value; }

    /**
     * @brief Accesses members of the stored value.
     *
     * @return A pointer to the stored value.
     */
    [[nodiscard]] constexpr T* operator->() noexcept { return &_value; }

    /**
     * @brief Converts to the stored value.
     *
     * @return A const reference to the stored value.
     */
    [[nodiscard]] constexpr operator const T&() const noexcept { return _value; }

    /**
     * @brief Compares the stored values of two `required<T>`.
     *
     * Also provides `!=` through the rewritten-candidate rules.
     *
     * @param lhs The left operand.
     * @param rhs The right operand.
     * @return `lhs.get() == rhs.get()`.
     */
    [[nodiscard]] friend constexpr bool operator==(const required& lhs, const required& rhs) noexcept(
        noexcept(static_cast<bool>(std::declval<const T&>() == std::declval<const T&>()))
    )
        requires requires(const T& a) {
            { a == a } -> std::convertible_to<bool>;
        }
    {
        return static_cast<bool>(lhs._value == rhs._value);
    }

    /**
     * @brief Compares the stored value with a value of another type.
     *
     * Available when `T` is equality-comparable with `U`. Also provides
     * `value == req` and the `!=` forms through the rewritten-candidate rules.
     *
     * @tparam U The type of the other operand (not a `required`).
     * @param lhs The `required<T>` operand.
     * @param rhs The value to compare the stored value with.
     * @return `lhs.get() == rhs`.
     */
    template<typename U>
        requires(!detail::is_required_v<U>) && requires(const T& a, const U& b) {
            { a == b } -> std::convertible_to<bool>;
        }
    [[nodiscard]] friend constexpr bool operator==(const required& lhs, const U& rhs) noexcept(
        noexcept(static_cast<bool>(std::declval<const T&>() == std::declval<const U&>()))
    ) {
        return static_cast<bool>(lhs._value == rhs);
    }

private:
    T _value;
};

} // namespace idfxx

/** @} */ // end of idfxx_core_required
/** @} */ // end of idfxx_core
