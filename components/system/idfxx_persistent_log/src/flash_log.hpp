// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @file flash_log.hpp
 * @brief An append-only ring of text in flash that survives power cuts.
 *
 * @defgroup idfxx_persistent_log_flash_log Flash log
 * @brief The log as it is stored in flash. The log is appended a record at a time, and the oldest
 *        sector is overwritten.
 *
 * A @ref idfxx::persistent_log::flash_log writes text to a @ref idfxx::persistent_log::sector_store
 * a sector at a time, in order around the region. Each sector starts with a header and a block
 * that is left erased while the sector is in use. The header carries a sequence number that is one
 * more than the sequence number of the sector before it. Records follow. Each record holds a
 * length, a CRC and up to a sector's worth of text. It starts on a 16-byte block and is padded to
 * the next block. Every write is made of whole, aligned blocks, so the log also works on an
 * encrypted partition. A record never spans sectors. Text that doesn't fit is split, and the reader
 * joins the pieces again. Starting a sector erases it, so once the region is full, the oldest
 * sector's text is dropped.
 *
 * A power cut at any point loses at most the record being written. That record's CRC fails, and
 * the reader stops reading the sector there. @ref idfxx::persistent_log::flash_log::open moves the
 * writer on to a fresh sector rather than writing after the record. Before a sector is erased, a
 * retire mark is written to its second block. A sector with the mark is unused. A power cut can
 * stop an erase in the middle, which may leave the header and part of the old text. Because of the
 * mark, that text never comes back into the log.
 *
 * @code
 * auto flash = idfxx::persistent_log::flash_log::open(region, saved_hint);
 * flash.append("12.345 +1234 - wake reason=timer\n");
 * saved_hint = flash.head();
 *
 * auto r = flash.read();
 * std::array<char, 1024> chunk;
 * while (size_t n = r.next(chunk)) {
 *     send(std::span(chunk).first(n));
 * }
 * @endcode
 * @{
 */

#include "sdkconfig.h"
#include "sector_store.hpp"

#include <idfxx/error>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace idfxx::persistent_log {

/// The size of a sector's header, which is two blocks. The sector's records start after it. The
/// first block is the header block, which holds the magic, layout version, flags, sequence number
/// and CRC. The second is the retire block. It is erased while the sector is in use, and written
/// just before the sector is erased.
inline constexpr size_t sector_header_size = 32;

/// The size of a record's header: a 2-byte length and a 4-byte CRC.
inline constexpr size_t record_header_size = 6;

/**
 * @brief The position of the log's writer, as the sector that it writes to, the next byte, and
 *        the sector's sequence number.
 *
 * The caller can keep it between opens, for example in RTC memory. It lets @ref flash_log::open
 * skip the scan of every sector's header.
 */
struct position {
    /// The sector being written, from 0.
    uint32_t sector = 0;
    /// The byte offset in that sector where the next record goes. It is a multiple of
    /// @ref block_size. It is the sector's size when the next record goes in a new sector.
    uint32_t offset = 0;
    /// The sector's sequence number.
    uint32_t seq = 0;

    [[nodiscard]] bool operator==(const position&) const = default;
};

/**
 * @brief The log in a sector store, appended to at its head and read back oldest first.
 *
 * It is not thread-safe. Append, clear and read from one task, or under one lock. A reader may be
 * kept while the log is appended to or cleared, as long as each of the reader's calls is made
 * under that lock too (see @ref reader). The sector store must outlive the log and its readers.
 */
class flash_log {
public:
    class reader;
    class tail_reader;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Opens the log in a store, finding where to write next.
     *
     * With a hint, it reads only the hinted sector, the sector after it, and the hinted sector's
     * records. It uses the hint when the hinted sector carries the hinted sequence number, the
     * next sector doesn't carry the sequence number after it, and the hinted sector's records end
     * at the hinted offset with only erased bytes after them. Otherwise, or with no hint, it reads
     * every sector's header, and the head is the sector with the highest sequence number.
     *
     * If the head sector holds anything other than erased bytes after its last good record, as a
     * power cut during a write can leave it, the next append starts a new sector.
     *
     * @param store The region holding the log.
     * @param hint  Where the writer was when the log was last open, as returned by @ref head.
     *
     * @return The log.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @note If a read fails, the open fails, rather than treating the sector as unused.
     * @note A sector with a newer layout reads as unused. See @ref found_newer_format.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the store has fewer than two
     *         sectors, sectors smaller than 80 bytes, larger than 64 KiB or not a multiple of
     *         @ref block_size, or a @ref sector_store::write_size that doesn't divide
     *         @ref block_size. Throws std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] static flash_log
    open(sector_store& store, std::optional<position> hint = std::nullopt) {
        return unwrap(try_open(store, hint));
    }
#endif

    /**
     * @brief Opens the log in a store, finding where to write next.
     *
     * With a hint, it reads only the hinted sector, the sector after it, and the hinted sector's
     * records. It uses the hint when the hinted sector carries the hinted sequence number, the
     * next sector doesn't carry the sequence number after it, and the hinted sector's records end
     * at the hinted offset with only erased bytes after them. Otherwise, or with no hint, it reads
     * every sector's header, and the head is the sector with the highest sequence number.
     *
     * If the head sector holds anything other than erased bytes after its last good record, as a
     * power cut during a write can leave it, the next append starts a new sector.
     *
     * @param store The region holding the log.
     * @param hint  Where the writer was when the log was last open, as returned by @ref head.
     *
     * @return The log, or an error.
     * @retval idfxx::errc::invalid_arg The store has fewer than two sectors, sectors smaller than
     *         80 bytes, larger than 64 KiB or not a multiple of @ref block_size, or a
     *         @ref sector_store::write_size that doesn't divide @ref block_size.
     * @note If a read fails, the open fails, rather than treating the sector as unused.
     * @note A sector with a newer layout reads as unused. See @ref found_newer_format.
     */
    [[nodiscard]] static idfxx::result<flash_log>
    try_open(sector_store& store, std::optional<position> hint = std::nullopt);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Appends text to the log.
     *
     * The text is written as one record, or as several when it doesn't fit in the space left in
     * the head sector. Each record starts on a new block, so many short appends take more room
     * than a few long ones. Starting a sector erases it, so once the region is full, starting a
     * sector drops the oldest text.
     *
     * @param text The bytes to append. They may be any bytes, though the log is meant for text.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @note On failure, the first part of @p text may already be in the log, in whole records. So
     *       appending the text again may show that part twice. After a failed write, the next
     *       append starts a new sector. After any failed append, the next append first ends the
     *       last line, as @ref try_end_line does, if the log ends partway through a line.
     * @throws std::system_error with the store's error if a read, write or erase fails.
     */
    void append(std::string_view text) { unwrap(try_append(text)); }
#endif

    /**
     * @brief Appends text to the log.
     *
     * The text is written as one record, or as several when it doesn't fit in the space left in
     * the head sector. Each record starts on a new block, so many short appends take more room
     * than a few long ones. Starting a sector erases it, so once the region is full, starting a
     * sector drops the oldest text.
     *
     * @param text The bytes to append. They may be any bytes, though the log is meant for text.
     *
     * @return Success, or the store's error.
     * @note On failure, the first part of @p text may already be in the log, in whole records. So
     *       appending the text again may show that part twice. After a failed write, the next
     *       append starts a new sector. After any failed append, the next append first ends the
     *       last line, as @ref try_end_line does, if the log ends partway through a line.
     */
    [[nodiscard]] idfxx::result<void> try_append(std::string_view text);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Ends the last line, if it was cut short.
     *
     * A power cut can come between the appends that make up a line. It can also tear the header
     * of the sector that a line was continuing into. Either way, the log's last byte is left in
     * the middle of a line, and the next text appended would continue that line. If the log holds
     * text and its last byte isn't `'\n'`, this appends a `'\n'`. After a torn record, nothing is
     * appended, because a reader ends the line there itself (see @ref reader). Call it after
     * @ref open, before the first new line.
     *
     * @code
     * auto flash = idfxx::persistent_log::flash_log::open(store, hint);
     * flash.end_line();
     * @endcode
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read, write or erase fails.
     */
    void end_line() { unwrap(try_end_line()); }
#endif

    /**
     * @brief Ends the last line, if it was cut short.
     *
     * A power cut can come between the appends that make up a line. It can also tear the header
     * of the sector that a line was continuing into. Either way, the log's last byte is left in
     * the middle of a line, and the next text appended would continue that line. If the log holds
     * text and its last byte isn't `'\n'`, this appends a `'\n'`. After a torn record, nothing is
     * appended, because a reader ends the line there itself (see @ref reader). Call it after
     * @ref open, before the first new line.
     *
     * @return Success, or the store's error.
     */
    [[nodiscard]] idfxx::result<void> try_end_line();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Empties the log, in the time it takes to start one sector.
     *
     * It starts a new sector marked as the log's start. Readers begin there, and the older sectors
     * are left to be overwritten when the ring comes around to them.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read, write or erase fails.
     */
    void clear() { unwrap(try_clear()); }
#endif

    /**
     * @brief Empties the log, in the time it takes to start one sector.
     *
     * It starts a new sector marked as the log's start. Readers begin there, and the older sectors
     * are left to be overwritten when the ring comes around to them.
     *
     * @return Success, or the store's error.
     */
    [[nodiscard]] idfxx::result<void> try_clear();

    /**
     * @brief Returns where the writer is, as a hint for the next @ref open.
     *
     * @return The position, or `std::nullopt` while nothing has been written to the store.
     */
    [[nodiscard]] std::optional<position> head() const;

    /** @brief Returns true when @ref open used its hint rather than scanning every sector. */
    [[nodiscard]] bool opened_by_hint() const { return _hinted; }

    /**
     * @brief Returns true when @ref open read a sector header with a newer layout than this one.
     *
     * Firmware that was rolled back after an update may find such a sector. The sector reads as
     * unused, and it is overwritten when the ring comes around to it. When @ref open scanned, it
     * read every sector's header. When it opened by its hint, it read only the headers of the
     * hinted sector and the sector after it, so this function reports only what those two headers
     * showed. A header with some other magic, or a header that a power cut tore, doesn't count.
     */
    [[nodiscard]] bool found_newer_format() const { return _newer; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns a reader for the whole log, oldest first.
     *
     * The log starts at the newest sector that @ref clear marked as a start (or the first sector
     * ever written). If that sector has been overwritten, the log starts at the oldest sector
     * still in the ring. The log ends where the head is now, so text appended later is not read.
     *
     * The reader begins on an entry (see @ref idfxx_persistent_log_line). From a start that
     * @ref clear made (or the first sector ever written), it returns the text as it was appended.
     * The oldest sector of a ring that has wrapped may begin in the middle of an entry. From such
     * a sector, the reader leaves out the rest of the line that it begins in, and the lines after
     * it that start with a tab. After a gap, as when a sector is erased while the reader reads it,
     * the reader ends the line that it was in and resumes at the next entry. After a torn or bad
     * record, it resumes at the next record, and takes that record as the start of a line (see
     * @ref reader).
     *
     * @return The reader.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] reader read() const;
#endif

    /**
     * @brief Returns a reader for the whole log, oldest first.
     *
     * The log starts at the newest sector that @ref clear marked as a start (or the first sector
     * ever written). If that sector has been overwritten, the log starts at the oldest sector
     * still in the ring. The log ends where the head is now, so text appended later is not read.
     *
     * The reader begins on an entry (see @ref idfxx_persistent_log_line). From a start that
     * @ref clear made (or the first sector ever written), it returns the text as it was appended.
     * The oldest sector of a ring that has wrapped may begin in the middle of an entry. From such
     * a sector, the reader leaves out the rest of the line that it begins in, and the lines after
     * it that start with a tab. After a gap, as when a sector is erased while the reader reads it,
     * the reader ends the line that it was in and resumes at the next entry. After a torn or bad
     * record, it resumes at the next record, and takes that record as the start of a line (see
     * @ref reader).
     *
     * @return The reader, or the store's error.
     */
    [[nodiscard]] idfxx::result<reader> try_read() const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Copies the end of the log into a buffer.
     *
     * It fills @p out with the log's newest whole entries, up to the size of @p out. The newest
     * bytes might begin in the middle of a line, or on a line that starts with a tab and so
     * continues the entry before it. In that case, the bytes copied begin instead at the next line
     * that doesn't start with a tab (see @ref idfxx_persistent_log_line). The log's start counts
     * as the start of a line only when it is a start that @ref clear made (or the first sector
     * ever written).
     * This is because the oldest sector of a full ring may begin in the middle of a line.
     *
     * @param out The buffer.
     *
     * @return The number of bytes copied to the front of @p out.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] size_t tail(std::span<char> out) const { return unwrap(try_tail(out)); }
#endif

    /**
     * @brief Copies the end of the log into a buffer.
     *
     * It fills @p out with the log's newest whole entries, up to the size of @p out. The newest
     * bytes might begin in the middle of a line, or on a line that starts with a tab and so
     * continues the entry before it. In that case, the bytes copied begin instead at the next line
     * that doesn't start with a tab (see @ref idfxx_persistent_log_line). The log's start counts
     * as the start of a line only when it is a start that @ref clear made (or the first sector
     * ever written).
     * This is because the oldest sector of a full ring may begin in the middle of a line.
     *
     * @param out The buffer.
     *
     * @return The number of bytes copied to the front of @p out, or the store's error.
     */
    [[nodiscard]] idfxx::result<size_t> try_tail(std::span<char> out) const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Makes, in @p out, a reader for the end of the log. The reader returns, in pieces, the
     *        bytes that @ref tail would copy for a buffer of @p want bytes.
     *
     * It reads the end of the log twice. The first pass counts the bytes, and the second finds
     * where the newest whole entries within @p want begin. So the reader's @ref tail_reader::size
     * is known before the reader returns any bytes. The reader starts at the last piece that the
     * second pass read. When it returns its first bytes, it reads again the part of that piece
     * before them. The log must not be appended to or cleared while the reader is used, because
     * the reader counts the bytes to read past and the bytes to return when it is made, and it
     * doesn't count them again. The reader is made where @p out holds it, so that it is never
     * copied.
     *
     * @param want The largest number of bytes to return.
     * @param out  Where the reader is made. Whatever it held is replaced.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read fails.
     */
    void read_tail(size_t want, std::optional<tail_reader>& out) const {
        unwrap(try_read_tail(want, out));
    }
#endif

    /**
     * @brief Makes, in @p out, a reader for the end of the log. The reader returns, in pieces, the
     *        bytes that @ref try_tail would copy for a buffer of @p want bytes.
     *
     * It reads the end of the log twice. The first pass counts the bytes, and the second finds
     * where the newest whole entries within @p want begin. So the reader's @ref tail_reader::size
     * is known before the reader returns any bytes. The reader starts at the last piece that the
     * second pass read. When it returns its first bytes, it reads again the part of that piece
     * before them. The log must not be appended to or cleared while the reader is used, because
     * the reader counts the bytes to read past and the bytes to return when it is made, and it
     * doesn't count them again. The reader is made where @p out holds it, so that it is never
     * copied.
     *
     * @param want The largest number of bytes to return.
     * @param out  Where the reader is made. Whatever it held is replaced. On an error, it is left
     *             empty.
     *
     * @return Success, or the store's error.
     */
    [[nodiscard]] idfxx::result<void>
    try_read_tail(size_t want, std::optional<tail_reader>& out) const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns a reader for the newest entries of the log, the oldest of them first.
     *
     * The reader returns the newest @p count entries, or every entry when the log holds fewer.
     * The entries are split as @ref next_entry splits text. This function walks back from the head
     * until the sectors hold more than `count + 1` times @p largest bytes of text, or until it
     * reaches the log's start. So it reads these sectors three times: once to find them, once to
     * count the entries, and once up to the first entry to return. The reader that it returns has
     * read up to that entry, so the reader can be used while the log is appended to, as a reader
     * from
     * @ref read can.
     *
     * @param count   The number of entries.
     * @param largest The size of the longest entry, at least 1.
     * @param scratch A buffer for the text read, at least 1 byte. What it holds afterward is not
     *                used. A larger buffer reads the text in fewer calls.
     *
     * @return The reader.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] reader read_newest(size_t count, size_t largest, std::span<char> scratch) const;
#endif

    /**
     * @brief Returns a reader for the newest entries of the log, the oldest of them first.
     *
     * The reader returns the newest @p count entries, or every entry when the log holds fewer.
     * The entries are split as @ref next_entry splits text. This function walks back from the head
     * until the sectors hold more than `count + 1` times @p largest bytes of text, or until it
     * reaches the log's start. So it reads these sectors three times: once to find them, once to
     * count the entries, and once up to the first entry to return. The reader that it returns has
     * read up to that entry, so the reader can be used while the log is appended to, as a reader
     * from
     * @ref try_read can.
     *
     * @param count   The number of entries.
     * @param largest The size of the longest entry, at least 1.
     * @param scratch A buffer for the text read, at least 1 byte. What it holds afterward is not
     *                used. A larger buffer reads the text in fewer calls.
     *
     * @return The reader, or the store's error.
     */
    [[nodiscard]] idfxx::result<reader>
    try_read_newest(size_t count, size_t largest, std::span<char> scratch) const;

private:
    struct chain;

    // The sectors at the end of the log that hold more than `want` bytes of text. They run back
    // from the head, and stop at the log's start or at the oldest sector left in the ring.
    [[nodiscard]] idfxx::result<chain> _tail_chain(size_t want) const;

    explicit flash_log(sector_store& store);

    [[nodiscard]] idfxx::result<bool> _try_hint(const position& hint);
    [[nodiscard]] idfxx::result<void> _scan();
    [[nodiscard]] idfxx::result<void> _start_sector(uint16_t flags);
    [[nodiscard]] idfxx::result<void> _write_record(size_t at, std::string_view text);
    // The buffer in _scratch, for a scan of a sector.
    [[nodiscard]] std::span<std::byte> _scan_buffer() const;

    sector_store* _store;
    size_t _sectors;
    size_t _sector_size;
    uint32_t _head = 0;
    uint32_t _offset = 0;
    uint32_t _seq = 0;
    bool _has_head = false;
    bool _hinted = false;
    bool _newer = false;
    // An append failed since the log was opened or cleared, so the log may end partway through
    // a line. The next append ends the line first.
    bool _maybe_open = false;
    // Whether the log ends partway through a line, as the scan of the head sector at open showed.
    // It is `std::nullopt` when the scan didn't show it, and after any write. try_end_line() then
    // searches back from the head.
    std::optional<bool> _ends_in_line;
    // The buffer through which a scan of a sector reads its records. It is on the heap rather than
    // on the stack of the function that scans, because opening the log scans a sector at the
    // deepest point of persistent_log::start(). Only one call into the log may run at a time (see
    // the class description), so the calls never use the buffer at the same time. This includes
    // the const calls, which scan sectors too.
    std::unique_ptr<std::byte[]> _scratch;
};

/**
 * @brief Streams the log's text, oldest first, in chunks of the caller's size.
 *
 * The reader checks each record's CRC before it returns any of the record's text. It reads a
 * sector only up to the first record that is unwritten (its first block is erased) or that fails
 * its CRC.
 *
 * The log may be appended to between calls of @ref next. The appends are made under a lock, and
 * each call must hold that lock too. Nothing read in an earlier call is trusted. Each call reads
 * the sector header again, and goes on in the sector only if the sector still carries the
 * sequence number that it had. If a record has been partly returned, the call checks it against
 * its CRC again before returning the rest of it. So the text that a call returns is always text
 * whose CRC was checked in that call. An append that starts a sector erases the oldest sector. A
 * reader that is in that sector, or hasn't reached it yet, skips it (and the rest of any partly
 * returned record) and goes on with the oldest sector left. A reader that hasn't returned anything
 * yet begins on an entry there, as @ref flash_log::read describes. Text appended after
 * @ref flash_log::read is never returned.
 *
 * A reader that has returned text may then have to skip some. This happens when a sector is
 * erased before the reader finishes reading it, or when a partly returned record no longer passes
 * its CRC. The reader then ends the line that it was in with a newline, unless the last byte that
 * it returned ended a line. If the buffer is full, the newline begins the next call instead. The
 * reader then resumes at the next entry. It leaves out the rest of the line that it lands in, and
 * the lines after it that start with a tab. If a sector is erased after the reader has returned
 * all of its records, nothing unread is lost.
 *
 * A record that a power cut or a failed write tore, or that fails its CRC when the reader first
 * reaches it, ends its sector's records. The writer went on in a new sector. The reader resumes at
 * the next record and takes it as the start of a line. If the reader has returned text, it ends
 * the line that it was in, as described above. It then leaves out only the lines that start with
 * a tab. When each append begins a line, as the service's whole entries do, nothing is lost at
 * that point. A caller whose appends don't begin lines can get a fragment of a line there.
 */
class flash_log::reader {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the next part of the log.
     *
     * @param out The buffer to fill.
     *
     * @return The number of bytes put at the front of @p out. It is less than the size of @p out
     *         only at the end of the log, and it is 0 once the whole log has been read.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] size_t next(std::span<char> out) { return unwrap(try_next(out)); }
#endif

    /**
     * @brief Reads the next part of the log.
     *
     * @param out The buffer to fill.
     *
     * @return The number of bytes put at the front of @p out. It is less than the size of @p out
     *         only at the end of the log, and it is 0 once the whole log has been read. Or the
     *         store's error.
     */
    [[nodiscard]] idfxx::result<size_t> try_next(std::span<char> out);

private:
    friend class flash_log;

    reader(
        const sector_store& store,
        uint32_t sector,
        uint32_t seq,
        size_t sectors,
        size_t end_offset,
        bool at_entry
    )
        : _store(&store)
        , _sector(sector)
        , _seq(seq)
        , _left(sectors)
        , _end_offset(end_offset)
        , _seeking(!at_entry) {}

    [[nodiscard]] idfxx::result<void> _resume(std::span<std::byte> scratch);
    [[nodiscard]] idfxx::result<bool> _next_record(std::span<std::byte> scratch);
    void _leave_gap(bool recycled, bool at_line);
    void _leave_sector();
    [[nodiscard]] bool _past_records() const;

    const sector_store* _store;
    uint32_t _sector;
    uint32_t _seq;
    // The number of sectors left to read, including this one. The last of them is the head as it
    // was when read() was called.
    size_t _left;
    // Where the head's records ended when read() was called. The reader reads the last sector no
    // further than this.
    size_t _end_offset;
    bool _in_sector = false;
    // Within the sector, where the next record's header is. In the middle of a record, it is where
    // the rest of the record's text is.
    size_t _offset = 0;
    // In the middle of a record, these hold where its header is, its length, and the number of
    // bytes not yet returned.
    size_t _record_at = 0;
    size_t _record_length = 0;
    size_t _record_left = 0;
    // Until the first entry, the bytes read are left out, up to the first line that doesn't start
    // with a tab. When `_at_line` is set, the next byte read begins a line.
    bool _seeking;
    bool _at_line = false;
    // Some text has been returned.
    bool _given = false;
    // The last byte returned wasn't a newline.
    bool _line_open = false;
    // A gap came after a line that was only partly returned. A newline is returned next, to end
    // that line.
    bool _newline_due = false;
    // Each call checks again what earlier calls read (see _resume()). flash_log clears this for a
    // reader that it uses only while the log can't change: the passes that its own calls make, and
    // the reader in a tail_reader.
    bool _recheck = true;
};

/**
 * @brief Returns the log's newest whole entries, up to a size fixed when it was made, in chunks of
 *        the caller's size.
 *
 * @ref flash_log::read_tail makes it. It returns the bytes that @ref flash_log::tail would copy.
 * The log must not change while the reader is used.
 */
class flash_log::tail_reader {
public:
    /**
     * @brief Returns the total number of bytes that the reader returns, including those already
     *        returned.
     */
    [[nodiscard]] size_t size() const { return _size; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the next part of the end of the log.
     *
     * @param out The buffer to fill.
     *
     * @return The number of bytes put at the front of @p out. It is less than the size of @p out
     *         only at the end, and it is 0 once every byte has been returned.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] size_t next(std::span<char> out) { return unwrap(try_next(out)); }
#endif

    /**
     * @brief Reads the next part of the end of the log.
     *
     * @param out The buffer to fill.
     *
     * @return The number of bytes put at the front of @p out. It is less than the size of @p out
     *         only at the end, and it is 0 once every byte has been returned. Or the store's
     *         error.
     */
    [[nodiscard]] idfxx::result<size_t> try_next(std::span<char> out);

    /// @cond INTERNAL
    /// Lets only flash_log make a reader, in place.
    class key {
        friend class flash_log;
        key() = default;
    };

    tail_reader(key, const reader& r, size_t skip, size_t size)
        : _reader(r)
        , _skip(skip)
        , _size(size)
        , _left(size) {}
    /// @endcond

private:
    reader _reader;
    // The number of bytes that the reader returns before the first byte of the tail. The first
    // call reads past them.
    size_t _skip = 0;
    // The total number of bytes that the reader returns, and the number still to return.
    size_t _size = 0;
    size_t _left = 0;
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
inline flash_log::reader flash_log::read() const {
    return unwrap(try_read());
}

inline flash_log::reader
flash_log::read_newest(size_t count, size_t largest, std::span<char> scratch) const {
    return unwrap(try_read_newest(count, largest, scratch));
}
#endif

} // namespace idfxx::persistent_log

/** @} */
