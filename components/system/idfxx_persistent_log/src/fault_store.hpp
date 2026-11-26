// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @file fault_store.hpp
 * @brief A ring of fault entries in flash, each with the log lines that led up to it.
 *
 * @defgroup idfxx_persistent_log_fault_store Fault store
 * @brief Faults stored separately from the log. Each fault has its own copy of the log lines that
 *        came before it.
 *
 * A @ref idfxx::persistent_log::fault_store divides a @ref idfxx::persistent_log::sector_store into
 * fixed-size slots. It writes one entry per slot, in order around the region, so the newest entry
 * overwrites the oldest. Each entry is a header followed by context. The header holds when and
 * where the fault happened, its kind, a short detail, and where the code was. The context is the
 * log text before the fault. Every write is made of whole, aligned 16-byte blocks, so the store
 * also works on an encrypted partition.
 *
 * A record erases the sectors of the slot that the entry needs, starting with the sector that holds
 * the header. It then writes the context, and writes the header last. The header holds a CRC of
 * itself and a CRC of the context. A power cut before the whole header is written leaves the
 * slot without a good header. The slot then reads as empty, and the next record goes in the same
 * slot.
 *
 * @code
 * auto faults = idfxx::persistent_log::fault_store::open(region);
 * auto tail = ring.tail(faults.room());
 * faults.record(
 *     {.kind = "panic", .detail = "LoadProhibited pc=0x42001234", .version = "1.2.0",
 *      .boot = boot, .wake = wake, .wall = wall, .uptime = uptime},
 *     {tail.first.data(), tail.first.size()},
 *     {tail.second.data(), tail.second.size()}
 * );
 *
 * auto r = faults.read();
 * idfxx::persistent_log::fault f;
 * std::array<char, 256> chunk;
 * while (r.next(f)) {
 *     send(std::format("{}\n", f));
 *     while (size_t n = r.next_context(chunk)) {
 *         send(std::span(chunk).first(n));
 *     }
 * }
 * @endcode
 * @{
 */

#include "sdkconfig.h"
#include "sector_store.hpp"

#include <idfxx/error>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace idfxx::persistent_log {

/// The size of an entry's header at the start of its slot.
inline constexpr size_t fault_header_size = 512;

// The sizes of the fields that the public fault type holds.
using detail::fault_detail_size;
using detail::fault_elf_sha256_size;
using detail::fault_kind_size;
using detail::fault_task_size;
using detail::fault_version_size;
using detail::max_fault_frames;

/// The slot size that the fault store uses by default, which is two 4 KiB sectors.
inline constexpr size_t default_fault_slot_size = 8 * 1024;

/// The kind of the entry that @ref fault_store::clear records. The kind is reserved, and
/// @ref fault_store::record refuses it.
inline constexpr std::string_view cleared_kind = "cleared";

/**
 * @brief Returns true when a kind can be recorded.
 *
 * A kind can be recorded when it is 1 to 23 printable ASCII characters without spaces, and it is
 * not @ref cleared_kind.
 *
 * @param kind The kind.
 */
[[nodiscard]] bool recordable_kind(std::string_view kind);

/**
 * @brief Returns the part of a detail that an entry keeps.
 *
 * That part is up to 159 bytes long, and it doesn't end in the middle of a UTF-8 character.
 *
 * @param detail The detail.
 */
[[nodiscard]] std::string_view kept_detail(std::string_view detail);

/**
 * @brief A fault to record. It holds what the fault was, and when and where it happened.
 *
 * The strings are copied into the entry's fixed fields. The version, detail and task name are cut
 * to fit.
 */
struct fault_input {
    /// The kind of fault, such as `panic`, `task_wdt` or `connect_start`. It is 1 to 23 printable
    /// ASCII characters without spaces, and it is not @ref cleared_kind.
    std::string_view kind;
    /// A short description. It is cut to 159 bytes.
    std::string_view detail;
    /// The firmware version. It is cut to 31 bytes.
    std::string_view version;
    /// The boot count.
    uint32_t boot = 0;
    /// The wake count within the boot.
    uint32_t wake = 0;
    /// The wall-clock time, or `std::nullopt` when the clock isn't set. The entry keeps it to the
    /// second.
    std::optional<std::chrono::system_clock::time_point> wall;
    /// The time since the boot or the deep-sleep wake. The entry keeps it in milliseconds, up to
    /// about 49 days.
    std::chrono::milliseconds uptime{0};
    /// The first bytes of the SHA-256 of the running app's ELF file, which match the entry to its
    /// firmware build. It is zero when not known.
    std::array<std::byte, fault_elf_sha256_size> elf_sha256{};
    /// The name of the task that the fault happened in. It is cut to 15 bytes, and it is empty
    /// when not known.
    std::string_view task = {};
    /// The program counter where the exception happened, or 0 when not known.
    uint32_t exception_pc = 0;
    /// Backtrace frames, as program counters, innermost first. The entry keeps up to
    /// @ref max_fault_frames of them.
    std::span<const uint32_t> frames = {};
    /// The CRC of the core dump that the fault's details came from, if any. When it is
    /// `std::nullopt`, the entry keeps the CRC that the entry before it held (see
    /// @ref fault_store::last_dump_crc).
    std::optional<uint32_t> dump_crc = std::nullopt;
};

/**
 * @brief The context of a fault to record, supplied a piece at a time.
 *
 * It lets the context come from somewhere other than memory, such as the log in flash, without a
 * buffer that holds the whole context.
 */
class fault_context {
public:
    virtual ~fault_context() = default;

    /** @brief Returns the total number of bytes in the context. */
    [[nodiscard]] virtual size_t size() const = 0;

    /**
     * @brief Fills a buffer with the context's next bytes.
     *
     * The fault store calls it with pieces that together are @ref size bytes, in order.
     *
     * @param out The buffer. The bytes must fill it.
     *
     * @return Success, or an error. An error fails the record.
     */
    [[nodiscard]] virtual idfxx::result<void> fill(std::span<char> out) = 0;

protected:
    fault_context() = default;
    fault_context(const fault_context&) = default;
    fault_context& operator=(const fault_context&) = default;
};

/// @cond INTERNAL
/// Fills and reads a fault's fields.
struct detail::fault_access {
    [[nodiscard]] static fault_fields& fields(fault& f) noexcept { return f._f; }
    [[nodiscard]] static const fault_fields& fields(const fault& f) noexcept { return f._f; }
};
/// @endcond

/**
 * @brief The fault entries in a sector store, recorded around a ring of slots and read back oldest
 *        first.
 *
 * It is not thread-safe. Record, clear and read from one task, or under one lock. A reader may be
 * kept while entries are recorded or the store is cleared, as long as each of the reader's calls
 * is made under that lock too (see @ref reader). The sector store must outlive the fault store and
 * its readers.
 */
class fault_store {
public:
    class reader;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Opens the fault store in a sector store, finding where the next entry goes.
     *
     * It reads every slot's header, and nothing else. The next entry goes in the slot after the
     * entry with the highest sequence number, and it takes the next sequence number.
     *
     * @param store     The region that holds the entries. Bytes past the last whole slot are
     *                  unused.
     * @param slot_size The size of each entry's slot. It must be a multiple of the store's sector
     *                  size, larger than @ref fault_header_size, and no larger than the store.
     *
     * @return The fault store.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @note If a read fails, the open fails, rather than treating the slot as empty.
     * @note A slot that holds an entry of a newer layout reads as empty. See
     *       @ref found_newer_format.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the slot size doesn't fit the
     *         store, if the store's sectors aren't a multiple of @ref block_size, or if its
     *         @ref sector_store::write_size doesn't divide @ref block_size. Throws
     *         std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] static fault_store
    open(sector_store& store, size_t slot_size = default_fault_slot_size) {
        return unwrap(try_open(store, slot_size));
    }
#endif

    /**
     * @brief Opens the fault store in a sector store, finding where the next entry goes.
     *
     * It reads every slot's header, and nothing else. The next entry goes in the slot after the
     * entry with the highest sequence number, and it takes the next sequence number.
     *
     * @param store     The region that holds the entries. Bytes past the last whole slot are
     *                  unused.
     * @param slot_size The size of each entry's slot. It must be a multiple of the store's sector
     *                  size, larger than @ref fault_header_size, and no larger than the store.
     *
     * @return The fault store, or an error.
     * @retval idfxx::errc::invalid_arg The slot size doesn't fit the store, the store's sectors
     *         aren't a multiple of @ref block_size, or its @ref sector_store::write_size doesn't
     *         divide @ref block_size.
     * @note If a read fails, the open fails, rather than treating the slot as empty.
     * @note A slot that holds an entry of a newer layout reads as empty. See
     *       @ref found_newer_format.
     */
    [[nodiscard]] static idfxx::result<fault_store>
    try_open(sector_store& store, size_t slot_size = default_fault_slot_size);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Records a fault, with the log text before it.
     *
     * The context is @p context followed by @p more, so that both runs of a ring's bytes can be
     * passed as they are. It should begin at the start of an entry. When it is longer than
     * @ref room, its oldest bytes are left out. The rest of the entry that the cut falls in is
     * also left out, so that the context keeps the newest whole entries (see
     * @ref idfxx_persistent_log_line).
     *
     * The bytes are copied out a small piece at a time. The CRC of each piece and the write of
     * that piece both use the copy. So the context may be in memory that other tasks write to
     * during the record.
     *
     * @param f       The fault.
     * @param context The context, or its first part.
     * @param more    The rest of the context.
     *
     * @return The entry's sequence number.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @note On failure, the slot that the entry was going to may have been erased, and the oldest
     *       entry lost. The next record goes in the same slot, with the same sequence number.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the kind is empty, longer than
     *         23 characters, holds a character that isn't printable ASCII or is a space, or is
     *         @ref cleared_kind. Throws std::system_error with the store's error if a read, write
     *         or erase fails.
     */
    [[nodiscard]] uint32_t
    record(const fault_input& f, std::string_view context = {}, std::string_view more = {}) {
        return unwrap(try_record(f, context, more));
    }
#endif

    /**
     * @brief Records a fault, with the log text before it.
     *
     * The context is @p context followed by @p more, so that both runs of a ring's bytes can be
     * passed as they are. It should begin at the start of an entry. When it is longer than
     * @ref room, its oldest bytes are left out. The rest of the entry that the cut falls in is
     * also left out, so that the context keeps the newest whole entries (see
     * @ref idfxx_persistent_log_line).
     *
     * The bytes are copied out a small piece at a time. The CRC of each piece and the write of
     * that piece both use the copy. So the context may be in memory that other tasks write to
     * during the record.
     *
     * @param f       The fault.
     * @param context The context, or its first part.
     * @param more    The rest of the context.
     *
     * @return The entry's sequence number, or an error.
     * @retval idfxx::errc::invalid_arg The kind is empty, longer than 23 characters, holds a
     *         character that isn't printable ASCII or is a space, or is @ref cleared_kind.
     * @note On failure, the slot that the entry was going to may have been erased, and the oldest
     *       entry lost. The next record goes in the same slot, with the same sequence number.
     */
    [[nodiscard]] idfxx::result<uint32_t>
    try_record(const fault_input& f, std::string_view context = {}, std::string_view more = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Records a fault, with context supplied a piece at a time.
     *
     * It takes the context as it is. The context should begin at the start of an entry and be no
     * longer than @ref room. Each piece of the context, of up to 256 bytes, is written from a
     * copy, and its CRC is taken from the same copy.
     *
     * @param f       The fault.
     * @param context The context.
     *
     * @return The entry's sequence number.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @note On failure, the slot that the entry was going to may have been erased, and the oldest
     *       entry lost. The next record goes in the same slot, with the same sequence number.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the kind can't be recorded (see
     *         @ref recordable_kind) or the context is longer than @ref room. Throws
     *         std::system_error with the context's error, or with the store's error if a read,
     *         write or erase fails.
     */
    [[nodiscard]] uint32_t record(const fault_input& f, fault_context& context) {
        return unwrap(try_record(f, context));
    }
#endif

    /**
     * @brief Records a fault, with context supplied a piece at a time.
     *
     * It takes the context as it is. The context should begin at the start of an entry and be no
     * longer than @ref room. Each piece of the context, of up to 256 bytes, is written from a
     * copy, and its CRC is taken from the same copy.
     *
     * @param f       The fault.
     * @param context The context.
     *
     * @return The entry's sequence number, or the context's error, or the store's error.
     * @retval idfxx::errc::invalid_arg The kind can't be recorded (see @ref recordable_kind), or
     *         the context is longer than @ref room.
     * @note On failure, the slot that the entry was going to may have been erased, and the oldest
     *       entry lost. The next record goes in the same slot, with the same sequence number.
     */
    [[nodiscard]] idfxx::result<uint32_t> try_record(const fault_input& f, fault_context& context);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Hides every entry recorded so far, in the time it takes to record one entry.
     *
     * Records an entry of kind @ref cleared_kind. Readers list only the entries newer than the
     * newest such entry, and never list it.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read, write or erase fails.
     */
    void clear() { unwrap(try_clear()); }
#endif

    /**
     * @brief Hides every entry recorded so far, in the time it takes to record one entry.
     *
     * Records an entry of kind @ref cleared_kind. Readers list only the entries newer than the
     * newest such entry, and never list it.
     *
     * @return Success, or the store's error.
     */
    [[nodiscard]] idfxx::result<void> try_clear();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Counts the entries that a reader would list.
     *
     * It reads every slot's header.
     *
     * @return The count.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] size_t count() const { return unwrap(try_count()); }
#endif

    /**
     * @brief Counts the entries that a reader would list.
     *
     * It reads every slot's header.
     *
     * @return The count, or the store's error.
     */
    [[nodiscard]] idfxx::result<size_t> try_count() const;

    /**
     * @brief Returns a reader for the entries, oldest first.
     *
     * The reader lists the entries that exist now, minus any that are overwritten before the
     * reader reaches them.
     */
    [[nodiscard]] reader read() const;

    /**
     * @brief Returns the largest number of context bytes that an entry holds, which is the slot
     *        size minus the header size.
     */
    [[nodiscard]] size_t room() const { return _slot_size - fault_header_size; }

    /** @brief Returns the number of slots, which is the most entries that the store holds. */
    [[nodiscard]] size_t slots() const { return _slots; }

    /**
     * @brief Returns true when @ref open read an entry header with a newer layout than this one.
     *
     * Firmware that was rolled back after an update may find such an entry. The entry reads as an
     * empty slot, and it is overwritten when the ring comes around to it. A header with some other
     * magic, or a header that a power cut tore, doesn't count.
     */
    [[nodiscard]] bool found_newer_format() const { return _newer; }

    /**
     * @brief Returns the CRC of the last core dump taken, as of the newest entry.
     *
     * Each entry holds the CRC of the last dump that any fault took up to and including that entry,
     * not only a dump that the entry's own fault took. An entry whose fault took no dump, and an
     * entry that @ref clear records, keep the CRC that the entry before them held. So the CRC is
     * kept across a clear, a reopen and the ring wrapping. @ref open reads it from the entry with
     * the highest sequence number, whether or not a clear hides that entry.
     *
     * @return The CRC, or `std::nullopt` when no entry holds one.
     */
    [[nodiscard]] std::optional<uint32_t> last_dump_crc() const { return _last_dump_crc; }

private:
    explicit fault_store(sector_store& store, size_t slot_size);

    [[nodiscard]] idfxx::result<void> _scan();
    [[nodiscard]] idfxx::result<uint32_t>
    _write_entry(std::string_view kind, const fault_input& f, fault_context* context);

    sector_store* _store;
    size_t _slot_size;
    size_t _slots;
    bool _has_head = false;
    uint32_t _head = 0;
    uint32_t _seq = 0;
    // The sequence number of the newest cleared entry seen, if any. Entries at or below it are
    // hidden.
    std::optional<uint32_t> _floor;
    bool _newer = false;
    // The CRC of the last core dump taken, as the newest entry holds it. The next entry carries it
    // forward unless its fault took a newer dump.
    std::optional<uint32_t> _last_dump_crc;
};

/**
 * @brief Steps through the entries oldest first, each header and then its context.
 *
 * Entries may be recorded, or the store cleared, between calls, and the reader stays valid across
 * them. The records are made under a lock, and each call of the reader must hold that lock too.
 * The reader lists the entries that existed at @ref fault_store::read. An entry recorded since
 * then has a higher sequence number and is left out. An entry that such a record overwrote before
 * the reader reached it is gone. A clear made since then hides nothing from the reader.
 *
 * @ref next returns an entry's header, and checks the entry's context against its CRC, a small
 * piece at a time. @ref next_context then returns the context in pieces of the caller's size. It
 * returns each piece of context only while the slot still holds the same entry. Once a record
 * overwrites the entry, the entry's context ends before @ref fault::context_size bytes have been
 * returned.
 */
class fault_store::reader {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the next entry's header, and checks its context.
     *
     * It reads the entry's header, then reads the entry's context a small piece at a time and
     * checks it against its CRC. An entry whose context fails the check is still returned, marked
     * as damaged. @ref next_context then returns the entry's context, from its start.
     *
     * @param out Filled with the entry when there is one. Otherwise it is left as it was.
     *
     * @return True when @p out is filled, or false once every entry has been returned.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] bool next(fault& out) { return unwrap(try_next(out)); }
#endif

    /**
     * @brief Reads the next entry's header, and checks its context.
     *
     * It reads the entry's header, then reads the entry's context a small piece at a time and
     * checks it against its CRC. An entry whose context fails the check is still returned, marked
     * as damaged. @ref try_next_context then returns the entry's context, from its start.
     *
     * @param out Filled with the entry when there is one. Otherwise it is left as it was.
     *
     * @return True when @p out is filled, false once every entry has been returned, or the store's
     *         error.
     */
    [[nodiscard]] idfxx::result<bool> try_next(fault& out);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the next part of the context of the entry that @ref next returned last.
     *
     * It first reads the entry's header again. If the slot no longer holds the entry, as when a
     * record has overwritten it since, the context ends there.
     *
     * @param out The buffer to fill.
     *
     * @return The number of bytes put at the front of @p out. It is less than the size of @p out
     *         only at the end of the context. It is 0 once the whole context has been returned,
     *         once the context has ended early, or when there is no entry (before the first
     *         @ref next, or after it returned false).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the store's error if a read fails.
     */
    [[nodiscard]] size_t next_context(std::span<char> out) { return unwrap(try_next_context(out)); }
#endif

    /**
     * @brief Reads the next part of the context of the entry that @ref try_next returned last.
     *
     * It first reads the entry's header again. If the slot no longer holds the entry, as when a
     * record has overwritten it since, the context ends there.
     *
     * @param out The buffer to fill.
     *
     * @return The number of bytes put at the front of @p out. It is less than the size of @p out
     *         only at the end of the context. It is 0 once the whole context has been returned,
     *         once the context has ended early, or when there is no entry (before the first
     *         @ref try_next, or after it returned false). Or the store's error.
     */
    [[nodiscard]] idfxx::result<size_t> try_next_context(std::span<char> out);

    /**
     * @brief Returns true while @ref try_next_context may return more of the context of the entry
     *        that @ref try_next returned last.
     *
     * It reads only the reader itself, not the store.
     */
    [[nodiscard]] bool in_context() const noexcept { return _current; }

private:
    friend class fault_store;

    reader(
        const sector_store& store,
        size_t slot_size,
        size_t slots,
        uint32_t first,
        size_t left,
        uint32_t newest,
        std::optional<uint32_t> floor
    )
        : _store(&store)
        , _slot_size(slot_size)
        , _slots(slots)
        , _slot(first)
        , _left(left)
        , _newest(newest)
        , _floor(floor) {}

    const sector_store* _store;
    size_t _slot_size;
    size_t _slots;
    uint32_t _slot;
    size_t _left;
    // The newest entry's sequence number when read() was called. Entries above it were recorded
    // since then.
    uint32_t _newest;
    std::optional<uint32_t> _floor;

    // The entry that next() returned last, whose context next_context() returns. These fields
    // hold where its slot starts, its sequence number, its context's size, and how much of the
    // context has been returned.
    bool _current = false;
    size_t _current_base = 0;
    uint32_t _current_seq = 0;
    size_t _context_size = 0;
    size_t _context_at = 0;
};

} // namespace idfxx::persistent_log

/** @} */
