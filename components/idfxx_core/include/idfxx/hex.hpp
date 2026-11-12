// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/hex>
 * @file hex.hpp
 * @brief Hexadecimal text for byte buffers.
 *
 * @defgroup idfxx_core_hex Hex Formatting
 * @ingroup idfxx_core
 * @brief Render byte buffers as hexadecimal text.
 *
 * `idfxx::hex` presents any byte buffer accepted by `idfxx::byte_view` as hexadecimal text, two
 * digits per byte, for digests, hardware addresses, identifiers and hex dumps. It works with
 * `std::format` and with `idfxx::to_string`:
 *
 * @code
 * std::array<std::byte, 32> digest = ...;
 * std::string s = std::format("{}", idfxx::hex(digest)); // "9f86d081884c7d65..."
 *
 * std::array<uint8_t, 6> mac = ...;
 * auto opts = idfxx::hex_options{.separator = ":", .letter_case = idfxx::hex_case::upper};
 * std::string m = idfxx::to_string(idfxx::hex(mac, opts)); // "AA:BB:CC:DD:EE:FF"
 * @endcode
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/byte_view>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace idfxx {

/**
 * @headerfile <idfxx/hex>
 * @brief Letter case of the hex digits `a`–`f`.
 */
enum class hex_case {
    lower, ///< Digits `0`–`9` and `a`–`f`.
    upper, ///< Digits `0`–`9` and `A`–`F`.
};

/**
 * @headerfile <idfxx/hex>
 * @brief Options controlling how @ref hex renders bytes.
 */
struct hex_options {
    /** @brief Text placed between consecutive bytes, such as `":"` or `" "`. Empty for none. */
    std::string_view separator = {};
    /** @brief Letter case of the hex digits. */
    hex_case letter_case = hex_case::lower;
};

/**
 * @headerfile <idfxx/hex>
 * @brief Hexadecimal presentation of a byte buffer.
 *
 * A lightweight, non-owning value that refers to a byte buffer and the @ref hex_options to render
 * it with. Pass it to `std::format` or `idfxx::to_string` to produce the text. Each byte becomes
 * two hex digits, most significant digit first, in buffer order; an empty buffer renders as empty
 * text.
 *
 * The bytes and the separator are referenced, not copied: both must outlive every use of the
 * `hex` value. Creating it inside the formatting expression, as in the examples below, satisfies
 * this.
 *
 * With `CONFIG_IDFXX_STD_FORMAT` enabled, `std::format` accepts the standard fill, alignment and
 * width options for strings (for example `{:>20}`), applied to the text as a whole. Precision and
 * the debug (`?`) presentation are rejected, since they would cut or quote the digits. Letter
 * case and separator are chosen through @ref hex_options, not the format spec.
 *
 * @code
 * std::format("{}", idfxx::hex(buf));                          // "deadbeef"
 * std::format("{}", idfxx::hex(buf, {.separator = " "}));      // "de ad be ef"
 * std::format("{}", idfxx::hex(buf, {.letter_case = idfxx::hex_case::upper})); // "DEADBEEF"
 * std::format("[{:>12}]", idfxx::hex(buf));                    // "[    deadbeef]"
 * @endcode
 */
class hex {
public:
    /**
     * @brief Creates a hex presentation of a byte buffer.
     *
     * @param bytes The bytes to render. Must outlive every use of this value.
     * @param options How to render them. Its separator must outlive every use of this value.
     */
    explicit constexpr hex(byte_view bytes, hex_options options = {}) noexcept
        : _bytes(bytes)
        , _options(options) {}

    /**
     * @brief Returns the bytes being rendered.
     * @return A view of the bytes.
     */
    [[nodiscard]] constexpr byte_view bytes() const noexcept { return _bytes; }

    /**
     * @brief Returns the rendering options.
     * @return The options given at construction.
     */
    [[nodiscard]] constexpr hex_options options() const noexcept { return _options; }

private:
    byte_view _bytes;
    hex_options _options;
};

/**
 * @headerfile <idfxx/hex>
 * @brief Returns the hexadecimal text of a byte buffer.
 *
 * @param h The bytes and options to render.
 * @return Two hex digits per byte in buffer order, with the separator between bytes, such as
 *         "deadbeef" or "AA:BB:CC:DD:EE:FF".
 */
[[nodiscard]] std::string to_string(hex h);

/// @cond INTERNAL
namespace detail {

// Writes the hex text of h to out and returns the advanced iterator.
template<typename OutputIt>
constexpr OutputIt write_hex(hex h, OutputIt out) {
    constexpr std::string_view lower_digits = "0123456789abcdef";
    constexpr std::string_view upper_digits = "0123456789ABCDEF";
    const auto options = h.options();
    const auto digits = options.letter_case == hex_case::upper ? upper_digits : lower_digits;
    const auto bytes = h.bytes();
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            out = std::ranges::copy(options.separator, out).out;
        }
        const auto v = std::to_integer<unsigned>(bytes[i]);
        *out++ = digits[v >> 4];
        *out++ = digits[v & 0xF];
    }
    return out;
}

} // namespace detail
/// @endcond

} // namespace idfxx

/** @} */ // end of idfxx_core_hex

#ifdef CONFIG_IDFXX_STD_FORMAT
/// @cond INTERNAL
#include <cstdlib>
#include <format>
#include <iterator>
namespace std {

// Accepts the standard fill, align and width spec for strings, applied to the whole text, and
// rejects anything else (precision or '?' would cut or quote the digits). Without a spec, the
// digits are written straight to the output.
template<>
struct formatter<idfxx::hex> {
    std::formatter<std::string_view> _underlying;
    bool _has_spec = false;

    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin();
        const auto end = ctx.end();
        _has_spec = it != end && *it != '}';
        if (!_has_spec) {
            return it;
        }
        // Accept exactly [[fill]align][width], where width is digits or a nested {arg-id}.
        auto is_align = [](char c) { return c == '<' || c == '^' || c == '>'; };
        auto fill_end = std::next(it);
        while (fill_end != end && (static_cast<unsigned char>(*fill_end) & 0xC0) == 0x80) {
            ++fill_end; // continuation bytes of a multi-byte fill character
        }
        if (fill_end != end && is_align(*fill_end)) {
            it = std::next(fill_end);
        } else if (is_align(*it)) {
            ++it;
        }
        const bool nested = it != end && *it == '{';
        if (nested) {
            ++it;
        }
        while (it != end && *it >= '0' && *it <= '9') {
            ++it;
        }
        if (nested) {
            if (it == end || *it != '}') {
                _reject(_spec_error);
            }
            ++it;
        }
        if (it != end && *it != '}') {
            _reject(_spec_error);
        }
        return _underlying.parse(ctx);
    }

    template<typename FormatContext>
    auto format(idfxx::hex h, FormatContext& ctx) const {
        if (!_has_spec) {
            return idfxx::detail::write_hex(h, ctx.out());
        }
        return _underlying.format(idfxx::to_string(h), ctx);
    }

private:
    static constexpr const char* _spec_error =
        "idfxx::hex accepts only fill, align and width in its format spec";

    // Not constexpr: reaching it while checking a format string at compile time is a compile
    // error.
    [[noreturn]] static void _reject(const char* message) {
#if __cpp_exceptions
        throw std::format_error(message);
#else
        (void)message;
        std::abort();
#endif
    }
};

} // namespace std
/// @endcond
#endif // CONFIG_IDFXX_STD_FORMAT
