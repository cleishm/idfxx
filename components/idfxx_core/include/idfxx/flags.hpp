// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/flags>
 * @file flags.hpp
 * @brief Type-safe bitflags from scoped enums.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_flags Flags
 * @ingroup idfxx_core
 * @brief Type-safe bitfield operations on scoped enums.
 *
 * Provides the `flags<E>` template class for type-safe flag combinations with
 * operator overloading. Requires opt-in via `enable_flags_operators<E>`.
 *
 * @code
 * enum class my_flags : uint32_t {
 *     none  = 0,
 *     read  = 1u << 0,
 *     write = 1u << 1,
 *     exec  = 1u << 2,
 * };
 *
 * template<>
 * inline constexpr bool idfxx::enable_flags_operators<my_flags> = true;
 *
 * auto perms = my_flags::read | my_flags::write;
 * if (perms.contains(my_flags::write)) { ... }
 * @endcode
 * @{
 */

#include <charconv>
#include <concepts>
#include <string>
#include <type_traits>
#include <utility>

namespace idfxx {

/**
 * @brief Opt-in trait for enabling flag operators on an enum.
 *
 * Specialize this to `true` for enum types that should support bitwise
 * operators and work with `flags<E>`.
 *
 * @tparam E The enum type.
 *
 * @see flag_enum
 */
template<typename E>
inline constexpr bool enable_flags_operators = false;

/**
 * @brief Concept for enums that have opted into flag operators.
 *
 * An enum satisfies this concept if it's a scoped enum and
 * `enable_flags_operators<E>` is specialized to `true`.
 *
 * @tparam E The type to check.
 */
template<typename E>
concept flag_enum = std::is_enum_v<E> && enable_flags_operators<E>;

/**
 * @brief Type-safe set of flags from a scoped enum.
 *
 * Provides type-safe bitflag operations with full operator support.
 * Individual enum values implicitly convert to flags<E>, allowing natural
 * syntax like: `auto f = my_flag::a | my_flag::b;`
 *
 * All operations are constexpr and marked `[[nodiscard]]` to prevent
 * accidental misuse. The class provides zero-overhead abstractions over
 * raw bitwise operations.
 *
 * Requires opt-in via:
 * @code
 * template<> inline constexpr bool idfxx::enable_flags_operators<MyEnum> = true;
 * @endcode
 *
 * @tparam E The flag enum type (must satisfy flag_enum concept).
 */
template<flag_enum E>
class flags {
public:
    /** @brief The scoped enum type. */
    using enum_type = E;

    /** @brief The underlying integral type of the enum. */
    using underlying = std::underlying_type_t<E>;

private:
    underlying value_{};

    /** @cond INTERNAL */
    constexpr explicit flags(underlying v) noexcept
        : value_(v) {}
    /** @endcond */

public:
    /**
     * @brief Default constructor, initializes to empty flags (zero).
     */
    constexpr flags() noexcept = default;

    /**
     * @brief Constructs from a single enum value.
     *
     * Allows implicit conversion from enum values to flags, enabling
     * natural syntax when passing enum values to functions expecting flags.
     *
     * @param e The enum value to initialize from.
     */
    constexpr flags(E e) noexcept
        : value_(std::to_underlying(e)) {}

    /**
     * @brief Combines flags using bitwise OR.
     *
     * @param other The flags to combine with.
     *
     * @return A new flags object containing all set bits from both operands.
     */
    [[nodiscard]] constexpr flags operator|(flags other) const noexcept { return flags{value_ | other.value_}; }

    /**
     * @brief Combines flags in-place using bitwise OR.
     *
     * @param other The flags to combine with.
     *
     * @return Reference to this object.
     */
    constexpr flags& operator|=(flags other) noexcept {
        value_ |= other.value_;
        return *this;
    }

    /**
     * @brief Intersects flags using bitwise AND.
     *
     * @param other The flags to intersect with.
     *
     * @return A new flags object containing only bits set in both operands.
     */
    [[nodiscard]] constexpr flags operator&(flags other) const noexcept { return flags{value_ & other.value_}; }

    /**
     * @brief Intersects flags in-place using bitwise AND.
     *
     * @param other The flags to intersect with.
     *
     * @return Reference to this object.
     */
    constexpr flags& operator&=(flags other) noexcept {
        value_ &= other.value_;
        return *this;
    }

    /**
     * @brief Toggles flags using bitwise XOR.
     *
     * @param other The flags to toggle.
     *
     * @return A new flags object with toggled bits.
     */
    [[nodiscard]] constexpr flags operator^(flags other) const noexcept { return flags{value_ ^ other.value_}; }

    /**
     * @brief Toggles flags in-place using bitwise XOR.
     *
     * @param other The flags to toggle.
     *
     * @return Reference to this object.
     */
    constexpr flags& operator^=(flags other) noexcept {
        value_ ^= other.value_;
        return *this;
    }

    /**
     * @brief Clears specific flags (set difference).
     *
     * Removes all bits set in other from this flags object.
     *
     * @param other The flags to remove.
     *
     * @return A new flags object with the specified flags cleared.
     */
    [[nodiscard]] constexpr flags operator-(flags other) const noexcept { return flags{value_ & ~other.value_}; }

    /**
     * @brief Clears specific flags in-place (set difference).
     *
     * @param other The flags to remove.
     *
     * @return Reference to this object.
     */
    constexpr flags& operator-=(flags other) noexcept {
        value_ &= ~other.value_;
        return *this;
    }

    /**
     * @brief Computes the bitwise complement.
     *
     * Inverts all bits in the underlying type. Note that this includes bits
     * beyond those defined in the enum.
     *
     * @return A new flags object with all bits inverted.
     */
    [[nodiscard]] constexpr flags operator~() const noexcept { return flags{~value_}; }

    /**
     * @brief Checks if all specified flags are set.
     *
     * Returns true if all bits set in other are also set in this object.
     * Returns false if other is empty (zero).
     *
     * @param other The flags to check for.
     *
     * @return True if all specified flags are set, false otherwise.
     */
    [[nodiscard]] constexpr bool contains(flags other) const noexcept {
        return other.value_ != 0 && (value_ & other.value_) == other.value_;
    }

    /**
     * @brief Checks if any of the specified flags are set.
     *
     * Returns true if at least one bit set in other is also set in this object.
     *
     * @param other The flags to check for.
     *
     * @return True if any specified flags are set, false otherwise.
     */
    [[nodiscard]] constexpr bool contains_any(flags other) const noexcept { return (value_ & other.value_) != 0; }

    /**
     * @brief Checks if no flags are set.
     *
     * @return True if the flags object is empty (all bits zero).
     */
    [[nodiscard]] constexpr bool empty() const noexcept { return value_ == 0; }

    /**
     * @brief Explicit conversion to bool.
     *
     * @return True if any flags are set (non-zero), false if empty.
     */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != 0; }

    /**
     * @brief Returns the underlying integral value.
     *
     * @return The underlying enum value.
     */
    [[nodiscard]] constexpr underlying value() const noexcept { return value_; }

    /**
     * @brief Constructs flags from a raw underlying value.
     *
     * Use with care, as this bypasses type safety and may create flags with
     * bits set that don't correspond to defined enum values.
     *
     * @param v The raw underlying value.
     *
     * @return A flags object with the specified raw value.
     */
    [[nodiscard]] static constexpr flags from_raw(underlying v) noexcept { return flags{v}; }

    /**
     * @brief Equality comparison between flags objects.
     *
     * @return True if both flags have the same underlying value.
     */
    [[nodiscard]] constexpr bool operator==(flags const&) const noexcept = default;

    /**
     * @brief Equality comparison with an individual enum value.
     *
     * @param e The enum value to compare against.
     *
     * @return True if this flags object equals the single enum value.
     */
    [[nodiscard]] constexpr bool operator==(E e) const noexcept { return value_ == std::to_underlying(e); }
};

/**
 * @brief Class template argument deduction guide.
 *
 * Allows `flags` to be constructed without explicit template arguments:
 * @code
 * auto f = flags{my_flags::read};  // deduces flags<my_flags>
 * @endcode
 */
template<flag_enum E>
flags(E) -> flags<E>;

/**
 * @brief Combines two enum values into a flags object.
 *
 * Enables natural syntax for combining flag enum values without explicitly
 * constructing flags objects.
 *
 * @param a First flag value.
 * @param b Second flag value.
 *
 * @return A flags object containing both flags.
 */
template<flag_enum E>
[[nodiscard]] constexpr flags<E> operator|(E a, E b) noexcept {
    return flags<E>{a} | flags<E>{b};
}

/**
 * @brief Intersects two enum values into a flags object.
 *
 * @param a First flag value.
 * @param b Second flag value.
 *
 * @return A flags object containing the intersection.
 */
template<flag_enum E>
[[nodiscard]] constexpr flags<E> operator&(E a, E b) noexcept {
    return flags<E>{a} & flags<E>{b};
}

/**
 * @brief Toggles two enum values into a flags object.
 *
 * @param a First flag value.
 * @param b Second flag value.
 *
 * @return A flags object with toggled bits.
 */
template<flag_enum E>
[[nodiscard]] constexpr flags<E> operator^(E a, E b) noexcept {
    return flags<E>{a} ^ flags<E>{b};
}

/**
 * @brief Computes the bitwise complement of an enum value.
 *
 * @param a The flag value to complement.
 *
 * @return A flags object with all bits inverted.
 */
template<flag_enum E>
[[nodiscard]] constexpr flags<E> operator~(E a) noexcept {
    return ~flags<E>{a};
}

/**
 * @brief Returns a hexadecimal string representation of a flags value.
 *
 * @tparam E The flag enum type (must satisfy flag_enum concept).
 * @param f The flags value to convert.
 * @return A string in the form "0x..." representing the underlying value in hexadecimal.
 */
template<flag_enum E>
[[nodiscard]] inline std::string to_string(flags<E> f) {
    using underlying = typename flags<E>::underlying;
    char buf[2 + sizeof(underlying) * 2];
    buf[0] = '0';
    buf[1] = 'x';
    auto [ptr, ec] = std::to_chars(buf + 2, buf + sizeof(buf), f.value(), 16);
    return std::string(buf, ptr);
}

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<idfxx::flag_enum E>
struct formatter<idfxx::flags<E>> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::flags<E> f, FormatContext& ctx) const {
        auto s = to_string(f);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_core_flags
/** @} */ // end of idfxx_core
