// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @file entry_buffer.hpp
 * @brief The buffer in which the entry readers split text into entries.
 */

#include "idfxx/persistent_log.hpp"
#include "log_line.hpp"
#include "sdkconfig.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>

namespace idfxx::persistent_log {

/// The size of the longest entry that the log stores. It is the stamp, then at most
/// `CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE` bytes of text with a tab after each newline that remains
/// in the text, then a newline at the end. Each remaining newline is followed by a byte of text, so
/// there are at most half as many tabs as bytes of text.
inline constexpr size_t max_entry_size = max_prefix_size + CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE +
    CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE / 2 + 1;

/// The size of an entry reader's buffer.
///
/// The buffer must hold the longest entry and the byte after it, because an entry ends only at a
/// newline that isn't followed by a tab. The buffer is also large enough that each refill asks
/// for at least `CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE` bytes, since the text kept from before
/// a refill is at most the longest entry. A flush appends at most that many bytes, so a record in
/// flash holds at most that many bytes of text. The flash reader checks a record's CRC again on
/// every call that continues the record. With refills of at least a record's size, the reader
/// continues each record in at most one more call, and so checks the CRC of each record at most
/// twice.
inline constexpr size_t entry_buffer_size =
    CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE + max_entry_size;

/**
 * @brief Holds text read from a source, and splits it into entries as @ref next_entry does.
 *
 * The buffer is allocated when the object is made. An entry that @ref next returns refers to text
 * in the buffer, and is valid until the next call to @ref next or @ref reset.
 *
 * An entry ends at a newline that isn't followed by a tab. When the buffer is full and holds no
 * whole entry, the buffer returns the whole lines that it holds as an entry, and the next line,
 * which starts with a tab, begins an entry that isn't stamped. When the buffer is full and holds
 * no whole line, it returns all that it holds as an entry that isn't stamped. It returns the rest
 * of that line in the same way, ending with the part up to the line's newline.
 */
class entry_buffer {
public:
    /**
     * @brief Makes an empty buffer.
     *
     * @param size The size of the buffer in bytes, at least 1.
     */
    explicit entry_buffer(size_t size = entry_buffer_size)
        : _data(std::make_unique_for_overwrite<char[]>(size))
        , _size(size) {}

    /** @brief Returns the whole buffer, for a caller to use as scratch space before @ref next. */
    [[nodiscard]] std::span<char> scratch() noexcept { return {_data.get(), _size}; }

    /** @brief Forgets the text held, so that the next call to @ref next reads a new source. */
    void reset() noexcept {
        _begin = 0;
        _end = 0;
        _done = false;
        _in_line = false;
    }

    /**
     * @brief Returns the next entry, reading more text from a source when it needs to.
     *
     * @param out  Set to the entry if there is one, and left unchanged otherwise.
     * @param fill Called as `fill(std::span<char>)` to read the next bytes of the source into the
     *             span. It returns the number of bytes read, or an error. It returns fewer bytes
     *             than the span holds only at the end of the source.
     *
     * @return True if @p out was set, or false at the end of the source. Or the error from
     *         @p fill. After an error, the source may have lost bytes that it had already read,
     *         so the caller must not call this function again before @ref reset.
     */
    template<typename Fill>
    [[nodiscard]] idfxx::result<bool> next(entry& out, Fill&& fill) {
        while (!_take(out)) {
            if (_done) {
                return false;
            }
            if (_begin > 0) {
                std::memmove(_data.get(), _data.get() + _begin, _end - _begin);
                _end -= _begin;
                _begin = 0;
            }
            const std::span<char> room(_data.get() + _end, _size - _end);
            auto n = fill(room);
            if (!n) {
                return idfxx::error(n.error());
            }
            _end += *n;
            _done = *n < room.size();
        }
        return true;
    }

private:
    // Takes the next entry from the text held, and returns true. Returns false when the text held
    // holds no more entries. Unless the source has ended, there is then room in the buffer for
    // more text.
    [[nodiscard]] bool _take(entry& out) noexcept;

    std::unique_ptr<char[]> _data;
    size_t _size;
    // The text held, from _begin to _end.
    size_t _begin = 0;
    size_t _end = 0;
    // The source has ended.
    bool _done = false;
    // The text held begins partway through a line that didn't fit in the buffer.
    bool _in_line = false;
};

} // namespace idfxx::persistent_log
