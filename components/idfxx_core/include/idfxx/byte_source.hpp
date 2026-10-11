// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/byte_source>
 * @file byte_source.hpp
 * @brief Sequential byte sources.
 *
 * @defgroup idfxx_core_byte_source Byte Sources
 * @ingroup idfxx_core
 * @brief A common interface for reading a stream of bytes, with in-memory and file sources.
 *
 * The `idfxx::byte_source` concept lets code that consumes a byte stream — a
 * parser, a decoder, a copy loop — accept any producer of bytes: an in-memory
 * buffer, a file, a flash partition, a network connection, or an HTTP response
 * body.
 *
 * @code
 * const std::array<uint8_t, 4> data{1, 2, 3, 4};
 * idfxx::memory_source src{data};
 *
 * std::array<uint8_t, 3> buf{};
 * size_t n = src.read(buf); // n == 3
 * n = src.read(buf);        // n == 1
 * n = src.read(buf);        // n == 0: end of stream
 * @endcode
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/error>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace idfxx {

/**
 * @headerfile <idfxx/byte_source>
 * @brief A sequential source of bytes.
 *
 * A type models `byte_source` if it provides
 * `try_read(std::span<uint8_t> buf)` returning `idfxx::result<size_t>`. Each
 * call reads the next bytes of the stream into the front of @p buf:
 *
 * - It reads at most `buf.size()` bytes and returns how many it read.
 * - For a non-empty @p buf, a return of 0 means the end of the stream has been
 *   reached.
 * - Once 0 has been returned for a non-empty @p buf, the stream is finished
 *   and callers should stop reading. The result of further reads is
 *   unspecified: a source may keep returning 0, or may return an error.
 * - A return smaller than `buf.size()` (a short read) is allowed at any point
 *   and does not imply the end of the stream; keep reading until 0 is returned.
 * - For an empty @p buf, the return is 0 and says nothing about whether the
 *   end of the stream has been reached.
 * - A failure is returned as an error; the source's error codes are passed
 *   through unchanged.
 *
 * Sources typically also provide a throwing `read(std::span<uint8_t>)` with
 * the same semantics, but generic code that accepts any `byte_source` can rely
 * only on `try_read`.
 *
 * @tparam T The type to check.
 *
 * @code
 * // Counts the bytes in any byte source.
 * idfxx::result<size_t> count_bytes(idfxx::byte_source auto& src) {
 *     std::array<uint8_t, 64> buf{};
 *     size_t total = 0;
 *     while (true) {
 *         auto n = src.try_read(buf);
 *         if (!n) {
 *             return idfxx::error(n.error());
 *         }
 *         if (*n == 0) {
 *             return total;
 *         }
 *         total += *n;
 *     }
 * }
 * @endcode
 */
template<typename T>
concept byte_source = requires(T& s, std::span<uint8_t> buf) {
    { s.try_read(buf) } -> std::same_as<result<size_t>>;
};

/**
 * @headerfile <idfxx/byte_source>
 * @brief A byte source that reads from a buffer in memory.
 *
 * Reads the bytes of a caller-owned buffer in order, from the start to the
 * end. The source does not copy or own the buffer: the buffer must remain
 * valid, and unchanged, for as long as the source is read from.
 *
 * Sources are copyable; a copy reads independently from the same position in
 * the same buffer.
 *
 * @code
 * static const uint8_t blob[] = {0xDE, 0xAD, 0xBE, 0xEF};
 * idfxx::memory_source src{blob};
 *
 * std::array<uint8_t, 16> buf{};
 * size_t n = src.read(buf); // n == 4, src.remaining() == 0
 * @endcode
 */
class memory_source {
public:
    /**
     * @brief Creates a source that reads the bytes of @p data.
     *
     * @param data The bytes to read. Must remain valid while the source is in use.
     */
    explicit memory_source(std::span<const uint8_t> data) noexcept
        : _data(data) {}

    /**
     * @brief Returns the number of bytes not yet read.
     *
     * @return The number of bytes remaining before the end of the stream.
     */
    [[nodiscard]] size_t remaining() const noexcept { return _data.size(); }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the next bytes into a buffer.
     *
     * Copies `min(buf.size(), remaining())` bytes into the front of @p buf and
     * advances past them.
     *
     * @param buf Buffer to read into.
     *
     * @return The number of bytes read; 0 for a non-empty @p buf once the end
     *         of the data has been reached.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error Never thrown: reading from memory cannot fail.
     */
    [[nodiscard]] size_t read(std::span<uint8_t> buf) { return unwrap(try_read(buf)); }
#endif

    /**
     * @brief Reads the next bytes into a buffer.
     *
     * Copies `min(buf.size(), remaining())` bytes into the front of @p buf and
     * advances past them.
     *
     * @param buf Buffer to read into.
     *
     * @return The number of bytes read; 0 for a non-empty @p buf once the end
     *         of the data has been reached. Never returns an error.
     */
    [[nodiscard]] result<size_t> try_read(std::span<uint8_t> buf) noexcept {
        const size_t n = std::min(buf.size(), _data.size());
        std::copy_n(_data.begin(), n, buf.begin());
        _data = _data.subspan(n);
        return n;
    }

private:
    std::span<const uint8_t> _data;
};

/**
 * @headerfile <idfxx/byte_source>
 * @brief A byte source that reads from a C stdio stream.
 *
 * Reads from an open `FILE*` — a file on a mounted filesystem, or any other
 * stdio stream — starting at the stream's current position. The source does
 * not own the stream: the caller opens it before creating the source, and
 * closes it after the source is no longer used.
 *
 * Sources are copyable; copies share the stream and its position.
 *
 * @code
 * FILE* f = std::fopen("/spiffs/config.json", "rb");
 * if (f != nullptr) {
 *     idfxx::file_source src{f};
 *     std::array<uint8_t, 128> buf{};
 *     while (size_t n = src.read(buf)) {
 *         consume(std::span{buf}.first(n));
 *     }
 *     std::fclose(f);
 * }
 * @endcode
 */
class file_source {
public:
    /**
     * @brief Creates a source that reads from @p file.
     *
     * @param file An open stream to read from. Must not be null, and must stay
     *             open while the source is in use.
     *
     * @pre @p file is not null.
     */
    explicit file_source(FILE* file) noexcept
        : _file(file) {}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the next bytes into a buffer.
     *
     * A read error is reported once no bytes could be read: bytes read
     * before the error are returned as a short read, and since the stream's
     * error indicator stays set, the error is reported by a following read.
     *
     * @param buf Buffer to read into.
     *
     * @return The number of bytes read; 0 for a non-empty @p buf at the end
     *         of the file.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the stream reports a read error and no
     *         bytes could be read.
     */
    [[nodiscard]] size_t read(std::span<uint8_t> buf) { return unwrap(try_read(buf)); }
#endif

    /**
     * @brief Reads the next bytes into a buffer.
     *
     * A read error is reported once no bytes could be read: bytes read
     * before the error are returned as a short read, and since the stream's
     * error indicator stays set, the error is reported by a following read.
     *
     * @param buf Buffer to read into.
     *
     * @return The number of bytes read; 0 for a non-empty @p buf at the end
     *         of the file. On a read error with no bytes read, an error code
     *         in `std::generic_category()` holding the `errno` value the
     *         stream reported, or `idfxx::errc::fail` if it reported none.
     */
    [[nodiscard]] result<size_t> try_read(std::span<uint8_t> buf);

private:
    FILE* _file;
};

/** @cond INTERNAL */
static_assert(byte_source<memory_source>);
static_assert(byte_source<file_source>);
/** @endcond */

} // namespace idfxx

/** @} */ // end of idfxx_core_byte_source
