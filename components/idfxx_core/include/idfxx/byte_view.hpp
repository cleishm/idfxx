// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/byte_view>
 * @file byte_view.hpp
 * @brief Non-owning views of byte buffers.
 *
 * @defgroup idfxx_core_byte_view Byte Views
 * @ingroup idfxx_core
 * @brief Views of contiguous bytes that accept both `std::byte` and `uint8_t` buffers.
 *
 * `idfxx::byte_view` and `idfxx::mutable_byte_view` refer to a contiguous run of bytes owned by
 * someone else, like a `std::span` of `std::byte`. They are the parameter types of APIs that move
 * bytes without interpreting them — payloads, file and flash contents, bus transfers — so callers
 * can pass whatever holds their bytes:
 *
 * - any contiguous range of `std::byte` or `uint8_t`: `std::array`, `std::vector`, `std::span`, a
 *   built-in array, or another byte view
 * - the bytes of any object, through `std::as_bytes` / `std::as_writable_bytes`
 * - a pointer and a length, as `{ptr, len}`
 *
 * Ranges of `char` are not accepted: text is passed through an API's `std::string_view` overload
 * where it has one, and otherwise as `std::as_bytes(std::span{str})`. This keeps a string literal
 * from silently including its terminating NUL, and lets an API offer a `std::string_view` overload
 * beside a byte-view one without ambiguity.
 *
 * @code
 * void send(idfxx::byte_view data);
 *
 * std::array<uint8_t, 4> header{0xDE, 0xAD, 0xBE, 0xEF};
 * std::vector<std::byte> payload = load_payload();
 * struct reading { uint32_t time; int16_t value; } r{};
 *
 * send(header);
 * send(payload);
 * send(std::as_bytes(std::span{&r, 1}));
 * send({header.data() + 2, 2});
 * @endcode
 *
 * Like a `std::span`, a view does not extend the lifetime of the buffer it refers to: the buffer
 * must outlive every use of the view. Views are not constructible from a braced list of values,
 * whose storage would end with the full expression; APIs where callers commonly pass literal bytes
 * offer a separate `std::initializer_list<uint8_t>` overload instead.
 * @{
 */

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>

namespace idfxx {

/** @cond INTERNAL */
namespace detail {

template<typename T>
concept byte_element = std::same_as<std::remove_const_t<T>, std::byte> ||
    std::same_as<std::remove_const_t<T>, unsigned char>;

template<typename R>
concept byte_range = std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
    byte_element<std::ranges::range_value_t<R>>;

template<typename T>
concept byte_pointee = std::same_as<std::remove_const_t<T>, void> || byte_element<T>;

template<typename R>
concept mutable_byte_range = byte_range<R> && std::ranges::borrowed_range<R> &&
    !std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<R>>>;

} // namespace detail
/** @endcond */

/**
 * @headerfile <idfxx/byte_view>
 * @brief A non-owning, read-only view of contiguous bytes.
 *
 * Constructible from any contiguous range of `std::byte` or `uint8_t`, and from a pointer and a
 * length. Converts implicitly to
 * `std::span<const std::byte>` and to `std::span<const uint8_t>`.
 *
 * Views are cheap to copy (a pointer and a size) and are usually passed by value. Besides the
 * members below, the view provides `empty()`, `operator[]`, `front()` and `back()` through
 * `std::ranges::view_interface`.
 *
 * @code
 * void write(idfxx::byte_view data);
 *
 * std::vector<uint8_t> buf = ...;
 * write(buf);                     // the whole vector
 * write({buf.data() + 4, 8});     // 8 bytes from offset 4
 * write(idfxx::byte_view{buf}.subspan(4, 8));
 * @endcode
 */
class byte_view : public std::ranges::view_interface<byte_view> {
public:
    /** @brief The element type: a read-only byte. */
    using element_type = const std::byte;
    /** @brief The value type of the elements. */
    using value_type = std::byte;
    /** @brief The type of sizes and offsets. */
    using size_type = std::size_t;
    /** @brief A pointer to an element. */
    using pointer = const std::byte*;
    /** @brief The iterator type: a pointer to an element. */
    using iterator = const std::byte*;

    /** @brief Creates an empty view. */
    constexpr byte_view() noexcept = default;

    /**
     * @brief Creates a view of the bytes of a contiguous range.
     *
     * @tparam R A contiguous, sized range of `std::byte` or `uint8_t` (const or not), such as
     *           `std::array`, `std::vector`, `std::span`, a built-in array or another byte view.
     * @param r The range to view. Must outlive every use of the view.
     */
    template<typename R>
        requires(detail::byte_range<R> && !std::same_as<std::remove_cvref_t<R>, byte_view>)
    constexpr byte_view(R&& r) noexcept
        : _data(_as_bytes(std::ranges::data(r)))
        , _size(std::ranges::size(r)) {}

    /**
     * @brief Creates a view of @p size bytes starting at @p data.
     *
     * @tparam T `void`, `std::byte` or `uint8_t`, const or not. Pointers to other types are
     *           rejected: view an object's bytes with `std::as_bytes` instead.
     * @param data The first byte. Must not be null unless @p size is 0.
     * @param size The number of bytes.
     */
    template<typename T>
        requires detail::byte_pointee<T>
    constexpr byte_view(T* data, size_type size) noexcept
        : _data(_as_bytes(data))
        , _size(size) {}

    /**
     * @brief Returns a pointer to the first byte.
     * @return The first byte, or a pointer that must not be dereferenced if the view is empty.
     */
    [[nodiscard]] constexpr const std::byte* data() const noexcept { return _data; }

    /**
     * @brief Returns the number of bytes in the view.
     * @return The size in bytes.
     */
    [[nodiscard]] constexpr size_type size() const noexcept { return _size; }

    /**
     * @brief Returns an iterator to the first byte.
     * @return The first byte.
     */
    [[nodiscard]] constexpr iterator begin() const noexcept { return _data; }

    /**
     * @brief Returns an iterator past the last byte.
     * @return One past the last byte.
     */
    [[nodiscard]] constexpr iterator end() const noexcept { return _data + _size; }

    /**
     * @brief Returns a view of the first @p count bytes.
     * @param count The number of bytes. Must not exceed size().
     * @return The first @p count bytes.
     */
    [[nodiscard]] constexpr byte_view first(size_type count) const noexcept {
        return byte_view{std::span<const std::byte>{_data, count}};
    }

    /**
     * @brief Returns a view of the last @p count bytes.
     * @param count The number of bytes. Must not exceed size().
     * @return The last @p count bytes.
     */
    [[nodiscard]] constexpr byte_view last(size_type count) const noexcept {
        return byte_view{std::span<const std::byte>{_data + (_size - count), count}};
    }

    /**
     * @brief Returns a view of @p count bytes starting at @p offset.
     * @param offset The offset of the first byte. Must not exceed size().
     * @param count The number of bytes, or `std::dynamic_extent` for all bytes from @p offset to
     *              the end. Must not exceed `size() - offset`.
     * @return The selected bytes.
     */
    [[nodiscard]] constexpr byte_view
    subspan(size_type offset, size_type count = std::dynamic_extent) const noexcept {
        return byte_view{std::span<const std::byte>{_data, _size}.subspan(offset, count)};
    }

    /**
     * @brief Converts to a span of `uint8_t`.
     * @return A span of the same bytes.
     */
    operator std::span<const uint8_t>() const noexcept {
        return {reinterpret_cast<const uint8_t*>(_data), _size};
    }

private:
    template<typename T>
    static constexpr const std::byte* _as_bytes(T* p) noexcept {
        if constexpr (std::same_as<std::remove_const_t<T>, std::byte>) {
            return p;
        } else if constexpr (std::is_void_v<T>) {
            return static_cast<const std::byte*>(p);
        } else {
            return reinterpret_cast<const std::byte*>(p);
        }
    }

    const std::byte* _data = nullptr;
    size_type _size = 0;
};

/**
 * @headerfile <idfxx/byte_view>
 * @brief A non-owning view of contiguous, writable bytes.
 *
 * Constructible from any contiguous range of non-const `std::byte` or `uint8_t` that outlives the
 * call (an lvalue container, or a span), and from a pointer and a length. Converts implicitly to
 * `byte_view`, and to spans of `std::byte` or `uint8_t`, const or not.
 *
 * Temporary containers are rejected: bytes written into them would be lost. Besides the members
 * below, the view provides `empty()`, `operator[]`, `front()` and `back()` through
 * `std::ranges::view_interface`.
 *
 * @code
 * size_t read(idfxx::mutable_byte_view buf);
 *
 * std::array<uint8_t, 64> buf{};
 * size_t n = read(buf);
 * size_t m = read({buf.data() + n, buf.size() - n});
 * @endcode
 */
class mutable_byte_view : public std::ranges::view_interface<mutable_byte_view> {
public:
    /** @brief The element type: a writable byte. */
    using element_type = std::byte;
    /** @brief The value type of the elements. */
    using value_type = std::byte;
    /** @brief The type of sizes and offsets. */
    using size_type = std::size_t;
    /** @brief A pointer to an element. */
    using pointer = std::byte*;
    /** @brief The iterator type: a pointer to an element. */
    using iterator = std::byte*;

    /** @brief Creates an empty view. */
    constexpr mutable_byte_view() noexcept = default;

    /**
     * @brief Creates a view of the bytes of a contiguous range.
     *
     * @tparam R A contiguous, sized range of non-const `std::byte` or `uint8_t`, such as an
     *           lvalue `std::array`, `std::vector` or built-in array, or a `std::span`.
     * @param r The range to view. Must outlive every use of the view.
     */
    template<typename R>
        requires(detail::mutable_byte_range<R> &&
                 !std::same_as<std::remove_cvref_t<R>, mutable_byte_view>)
    constexpr mutable_byte_view(R&& r) noexcept
        : _data(_as_bytes(std::ranges::data(r)))
        , _size(std::ranges::size(r)) {}

    /**
     * @brief Creates a view of @p size bytes starting at @p data.
     *
     * @tparam T `void`, `std::byte` or `uint8_t`, not const. Pointers to other types are
     *           rejected: view an object's bytes with `std::as_writable_bytes` instead.
     * @param data The first byte. Must not be null unless @p size is 0.
     * @param size The number of bytes.
     */
    template<typename T>
        requires(detail::byte_pointee<T> && !std::is_const_v<T>)
    constexpr mutable_byte_view(T* data, size_type size) noexcept
        : _data(_as_bytes(data))
        , _size(size) {}

    /**
     * @brief Returns a pointer to the first byte.
     * @return The first byte, or a pointer that must not be dereferenced if the view is empty.
     */
    [[nodiscard]] constexpr std::byte* data() const noexcept { return _data; }

    /**
     * @brief Returns the number of bytes in the view.
     * @return The size in bytes.
     */
    [[nodiscard]] constexpr size_type size() const noexcept { return _size; }

    /**
     * @brief Returns an iterator to the first byte.
     * @return The first byte.
     */
    [[nodiscard]] constexpr iterator begin() const noexcept { return _data; }

    /**
     * @brief Returns an iterator past the last byte.
     * @return One past the last byte.
     */
    [[nodiscard]] constexpr iterator end() const noexcept { return _data + _size; }

    /**
     * @brief Returns a view of the first @p count bytes.
     * @param count The number of bytes. Must not exceed size().
     * @return The first @p count bytes.
     */
    [[nodiscard]] constexpr mutable_byte_view first(size_type count) const noexcept {
        return mutable_byte_view{std::span<std::byte>{_data, count}};
    }

    /**
     * @brief Returns a view of the last @p count bytes.
     * @param count The number of bytes. Must not exceed size().
     * @return The last @p count bytes.
     */
    [[nodiscard]] constexpr mutable_byte_view last(size_type count) const noexcept {
        return mutable_byte_view{std::span<std::byte>{_data + (_size - count), count}};
    }

    /**
     * @brief Returns a view of @p count bytes starting at @p offset.
     * @param offset The offset of the first byte. Must not exceed size().
     * @param count The number of bytes, or `std::dynamic_extent` for all bytes from @p offset to
     *              the end. Must not exceed `size() - offset`.
     * @return The selected bytes.
     */
    [[nodiscard]] constexpr mutable_byte_view
    subspan(size_type offset, size_type count = std::dynamic_extent) const noexcept {
        return mutable_byte_view{std::span<std::byte>{_data, _size}.subspan(offset, count)};
    }

    /**
     * @brief Converts to a span of `uint8_t`.
     * @return A span of the same bytes.
     */
    operator std::span<uint8_t>() const noexcept {
        return {reinterpret_cast<uint8_t*>(_data), _size};
    }

    /**
     * @brief Converts to a read-only span of `uint8_t`.
     * @return A span of the same bytes.
     */
    operator std::span<const uint8_t>() const noexcept {
        return {reinterpret_cast<const uint8_t*>(_data), _size};
    }

private:
    template<typename T>
    static constexpr std::byte* _as_bytes(T* p) noexcept {
        if constexpr (std::same_as<T, std::byte>) {
            return p;
        } else if constexpr (std::is_void_v<T>) {
            return static_cast<std::byte*>(p);
        } else {
            return reinterpret_cast<std::byte*>(p);
        }
    }

    std::byte* _data = nullptr;
    size_type _size = 0;
};

} // namespace idfxx

/** @cond INTERNAL */
template<>
inline constexpr bool std::ranges::enable_borrowed_range<idfxx::byte_view> = true;
template<>
inline constexpr bool std::ranges::enable_borrowed_range<idfxx::mutable_byte_view> = true;
/** @endcond */

/** @} */ // end of idfxx_core_byte_view
