// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/persistent_log>
 * @file persistent_log.hpp
 * @brief A log that stores ESP-IDF's warnings and errors, and lines from the app, across resets.
 *
 * @defgroup idfxx_persistent_log Persistent log
 * @brief A text log in flash that stores lines from ESP-IDF's log and from the app, to be read
 *        back later.
 *
 * Once started, the log captures the log lines at or above its own level threshold. By default, it
 * captures warnings and errors (see @ref idfxx::persistent_log::capture_config). It captures these
 * lines with a sink that it adds to idfxx::log. With
 * `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES` enabled (the default), the log adds the sink with
 * @ref idfxx::log::all_lines, and it captures the lines logged through ESP-IDF's log as well as the
 * lines logged through idfxx::log. With the option disabled, the log captures only the lines
 * logged through idfxx::log. The app adds its own lines with @ref idfxx::persistent_log::append.
 * Each entry has a stamp with the boot count and wake count when the entry was logged, the time
 * since that boot or wake, and the wall-clock time if the clock is set. Each line of a message
 * after the first starts with a tab (shown here as `\t`) and belongs to the same entry:
 * @code{.unparsed}
 * 12.0 +1234 2026-11-20T10:31:02Z W wifi: beacon timeout
 * 12.1 +80 - wake reason=button
 * 12.1 +95 - E app: config rejected
 * \tssid=home
 * @endcode
 * Lines are first added to a ring in RTC memory, which survives deep sleep and any reset other
 * than a power cut. They are then written to flash by a writer task, within 5 seconds by default,
 * or sooner when enough bytes are waiting. Without a writer task, they are written only when the
 * app calls @ref idfxx::persistent_log::flush. Lines that are still in the ring at a reset are
 * written to flash after the next start, except for the lines that a flush was writing when the
 * reset occurred. The log is stored in a flash partition, named `log` unless the app names another
 * one, or in a @ref idfxx::persistent_log::sector_store that the app passes to it (see
 * @ref idfxx::persistent_log::config::storage).
 *
 * @code
 * idfxx::persistent_log::start({.boot_count = boot_count});
 * idfxx::persistent_log::append("wake reason=timer");
 *
 * // Before deep sleep
 * idfxx::persistent_log::flush();
 *
 * // Reading it back
 * for (const auto& e : idfxx::persistent_log::read()) {
 *     send(std::format("{}\n", e));
 * }
 * @endcode
 *
 * The log stores every line. When it is full, the oldest lines are overwritten to make room for new
 * ones. Faults are stored separately. @ref idfxx::persistent_log::record_fault writes a fault to
 * its own slot, with a copy of the newest lines in the log, so that the lines that led up to the
 * fault survive after the log has overwritten them. @ref idfxx::persistent_log::start does the
 * same for a reset that is a fault, such as a panic or a watchdog reset. There are only a few slots
 * (4 by default, see @ref idfxx::persistent_log::fault_config). A stored fault is only overwritten
 * by a new fault, which replaces the oldest one. A fault that keeps happening can be recorded with
 * its repeats suppressed (see @ref idfxx::persistent_log::suppress_repeats), so that its repeats
 * don't fill the slots.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/byte_view>
#include <idfxx/cpu>
#include <idfxx/error>
#ifdef CONFIG_IDFXX_STD_FORMAT
#include <idfxx/log>
#endif
#include <idfxx/partition>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#ifdef CONFIG_IDFXX_STD_FORMAT
#include <format>
#endif
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

/**
 * @defgroup idfxx_persistent_log_line Log lines
 * @ingroup idfxx_persistent_log
 * @brief What is stored at the start of each line.
 *
 * Each entry in the log has a stamp with the boot count and wake count when the entry was logged,
 * the time since that boot or wake, and the wall-clock time if the clock is set. A stamp starts
 * with a digit. Each line of a message after the first starts with a tab instead, and belongs to
 * the same entry:
 * @code{.unparsed}
 * 12.345 +1234 2026-11-20T10:31:02Z wake reason=timer
 * 12.346 +80 - wake reason=button
 * @endcode
 * A stamp is `<boot>.<wake> +<ms> <wall> `, with a space at the end. `<boot>` and `<wake>` are the
 * boot count and wake count, and `<ms>` is the time since that boot or wake in milliseconds, all
 * in decimal. `<wall>` is the wall-clock time in UTC to the second (`2026-11-20T10:31:02Z`), or
 * `-` when the clock isn't set. Lines on the serial console are not stamped.
 *
 * An @ref idfxx::persistent_log::entry holds one entry's stamp and text.
 * @ref idfxx::persistent_log::entries and @ref idfxx::persistent_log::next_entry take entries
 * from text that the caller holds, an @ref idfxx::persistent_log::entry_reader returns the entries
 * in flash, and a @ref idfxx::persistent_log::fault_reader returns the entries of a fault's
 * context.
 *
 * These operations return or write the log starting partway through:
 * - a read of flash after the oldest text was overwritten,
 * - a copy of the newest lines in RTC memory or in flash,
 * - a fault's context, shortened to fit its slot,
 * - a flush after bytes were dropped.
 *
 * Each of them starts at the beginning of an entry. It leaves out the rest of the line that it
 * would start in, and the lines after that line that start with a tab, so that its first line is
 * stamped. If no stamped line is left, its result is empty. If a read of flash skips text partway
 * through, for example because a sector is overwritten while it is being read, the read ends the
 * line that it was in and resumes in the same way.
 */

namespace idfxx::persistent_log {

// =============================================================================
// Storage
// =============================================================================

/**
 * @headerfile <idfxx/persistent_log>
 * @brief A region of NOR flash, erased one sector at a time, in which the log is stored.
 *
 * The log is stored in a flash partition unless the app passes it a store (see
 * @ref config::storage). To store the log somewhere else, such as a flash chip on its own bus or a
 * range of a partition that also holds other data, pass a store to @ref start. The store can be a
 * @ref partition_sector_store, or an implementation of this interface. The log takes ownership of
 * the store, and owns it until @ref stop destroys it.
 *
 * Offsets are bytes from the start of the region. An erase sets every stored byte of a sector to
 * `0xFF`. The log only writes to bytes that have been erased since they were last written. A store
 * therefore does not need to clear bits the way NOR flash does, and a store over memory that is
 * simply overwritten also works. A write starts at a multiple of @ref write_size, and its length
 * is a multiple of @ref write_size. On an encrypted region, the bytes are stored encrypted.
 * @ref try_read returns them decrypted, and @ref try_read_raw returns them as stored, so that
 * erased bytes can be recognized. On a region that isn't encrypted, the two functions read the
 * same bytes.
 *
 * The log requires a store with:
 * - a size that is a whole number of sectors,
 * - a sector size that is a multiple of 16 bytes, from 80 to 65536,
 * - a write size that divides 16,
 * - at least two sectors left for the log after the fault slots (see @ref fault_config) are taken
 *   from its end.
 *
 * The log calls the store from the tasks that use the log, including its writer task, one call at
 * a time. Lines that the store logs during those calls are not captured.
 *
 * The public interface is non-virtual: implementations override the protected `do_*` hooks, as
 * with the standard library's non-virtual-interface pattern (cf. `std::pmr::memory_resource`).
 * The public methods check ranges and alignment once, so the hooks are called only with ranges
 * inside the region and, for writes, aligned to @ref write_size.
 *
 * @code
 * // A store over a flash chip on its own bus.
 * class chip_store final : public idfxx::persistent_log::sector_store {
 * public:
 *     explicit chip_store(my_flash_chip& chip)
 *         : sector_store(chip.size(), 4096, 1)
 *         , _chip(chip) {}
 *
 * private:
 *     idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
 *         return _chip.read(offset, dst);
 *     }
 *     idfxx::result<void> do_read_raw(size_t offset, idfxx::byte_span dst) const override {
 *         return _chip.read(offset, dst);
 *     }
 *     idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
 *         return _chip.program(offset, src);
 *     }
 *     idfxx::result<void> do_erase_sector(size_t index) override {
 *         return _chip.erase_4k(index * sector_size());
 *     }
 *
 *     my_flash_chip& _chip;
 * };
 *
 * idfxx::persistent_log::start({.storage = std::make_unique<chip_store>(chip)});
 * @endcode
 */
class sector_store {
public:
    virtual ~sector_store() = default;

    // =========================================================================
    // Geometry
    // =========================================================================

    /** @brief Returns the size of the region in bytes, a whole number of sectors. */
    [[nodiscard]] size_t size() const { return _size; }

    /** @brief Returns the size of a sector, the unit of erase, in bytes. */
    [[nodiscard]] size_t sector_size() const { return _sector_size; }

    /**
     * @brief Returns the alignment and granularity of a write, in bytes.
     *
     * A write starts at a multiple of this size, and its length is a multiple of this size. For a
     * @ref partition_sector_store, it is 16 on an encrypted partition and 1 on any other partition.
     */
    [[nodiscard]] size_t write_size() const { return _write_size; }

    /** @brief Returns the number of sectors in the region. */
    [[nodiscard]] size_t sector_count() const { return size() / sector_size(); }

    // =========================================================================
    // Access
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads bytes from the region, decrypted when it is encrypted.
     *
     * @param offset Byte offset within the region.
     * @param dst    Buffer to fill; its size is the number of bytes read.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the range lies outside the
     *         region, or with the store's error if the read fails.
     */
    void read(size_t offset, idfxx::byte_span dst) const { unwrap(try_read(offset, dst)); }

    /**
     * @brief Reads bytes from the region as they are stored, without decryption.
     *
     * Erased bytes read as `0xFF` whether the region is encrypted or not.
     *
     * @param offset Byte offset within the region.
     * @param dst    Buffer to fill; its size is the number of bytes read.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the range lies outside the
     *         region, or with the store's error if the read fails.
     */
    void read_raw(size_t offset, idfxx::byte_span dst) const { unwrap(try_read_raw(offset, dst)); }

    /**
     * @brief Writes bytes to the region, encrypted when it is encrypted.
     *
     * @param offset Byte offset within the region; a multiple of @ref write_size.
     * @param src    Bytes to write. Its length must be a multiple of @ref write_size.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the range lies outside the
     *         region or isn't aligned to @ref write_size, or with the store's error if the write
     *         fails.
     */
    void write(size_t offset, idfxx::byte_view src) { unwrap(try_write(offset, src)); }

    /**
     * @brief Erases one sector, setting every stored byte to `0xFF`.
     *
     * @param index The sector, from 0 to `sector_count() - 1`.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the sector lies outside the
     *         region, or with the store's error if the erase fails.
     */
    void erase_sector(size_t index) { unwrap(try_erase_sector(index)); }
#endif

    /**
     * @brief Reads bytes from the region, decrypted when it is encrypted.
     *
     * @param offset Byte offset within the region.
     * @param dst    Buffer to fill; its size is the number of bytes read.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg The range lies outside the region.
     * @retval other The store's error, if the read fails.
     */
    [[nodiscard]] idfxx::result<void> try_read(size_t offset, idfxx::byte_span dst) const;

    /**
     * @brief Reads bytes from the region as they are stored, without decryption.
     *
     * Erased bytes read as `0xFF` whether the region is encrypted or not.
     *
     * @param offset Byte offset within the region.
     * @param dst    Buffer to fill; its size is the number of bytes read.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg The range lies outside the region.
     * @retval other The store's error, if the read fails.
     */
    [[nodiscard]] idfxx::result<void> try_read_raw(size_t offset, idfxx::byte_span dst) const;

    /**
     * @brief Writes bytes to the region, encrypted when it is encrypted.
     *
     * @param offset Byte offset within the region; a multiple of @ref write_size.
     * @param src    Bytes to write. Its length must be a multiple of @ref write_size.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg The range lies outside the region, or isn't aligned to
     *         @ref write_size.
     * @retval other The store's error, if the write fails.
     */
    [[nodiscard]] idfxx::result<void> try_write(size_t offset, idfxx::byte_view src);

    /**
     * @brief Erases one sector, setting every stored byte to `0xFF`.
     *
     * @param index The sector, from 0 to `sector_count() - 1`.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg The sector lies outside the region.
     * @retval other The store's error, if the erase fails.
     */
    [[nodiscard]] idfxx::result<void> try_erase_sector(size_t index);

protected:
    /**
     * @brief Constructs the base with the region's geometry.
     *
     * The constructor doesn't check the geometry. @ref start checks that the log supports it.
     *
     * @param size        The size of the region in bytes. Must be a whole number of sectors.
     * @param sector_size The size of a sector, the unit of erase, in bytes.
     * @param write_size  The alignment and granularity of a write in bytes. Use 16 for a region
     *                    whose bytes are stored encrypted in 16-byte blocks, and 1 for a region
     *                    that is written one byte at a time.
     */
    sector_store(size_t size, size_t sector_size, size_t write_size) noexcept
        : _size(size)
        , _sector_size(sector_size)
        , _write_size(write_size) {}

    sector_store(const sector_store&) = default;
    sector_store(sector_store&&) noexcept = default;
    sector_store& operator=(const sector_store&) = default;
    sector_store& operator=(sector_store&&) noexcept = default;

    // =========================================================================
    // Customization hooks
    //
    // Implementations override these (typically privately). Each implements the correspondingly
    // named public method, which has already checked that the range lies inside the region and,
    // for a write, that it is aligned to write_size().
    // =========================================================================

    /**
     * @brief Reads bytes, decrypted when the region is encrypted; the hook for @ref try_read.
     *
     * @param offset Byte offset within the region; the range lies inside it.
     * @param dst    Buffer to fill completely.
     *
     * @return Success, or the store's error.
     */
    [[nodiscard]] virtual idfxx::result<void>
    do_read(size_t offset, idfxx::byte_span dst) const = 0;

    /**
     * @brief Reads bytes as they are stored; the hook for @ref try_read_raw.
     *
     * Erased bytes must read as `0xFF`. On a region that isn't encrypted, it reads the same bytes
     * as @ref do_read.
     *
     * @param offset Byte offset within the region; the range lies inside it.
     * @param dst    Buffer to fill completely.
     *
     * @return Success, or the store's error.
     */
    [[nodiscard]] virtual idfxx::result<void>
    do_read_raw(size_t offset, idfxx::byte_span dst) const = 0;

    /**
     * @brief Writes bytes, encrypted when the region is encrypted; the hook for @ref try_write.
     *
     * The log only writes to bytes that have been erased since they were last written.
     *
     * @param offset Byte offset within the region, a multiple of @ref write_size; the range lies
     *               inside it.
     * @param src    Bytes to write. Its length must be a multiple of @ref write_size.
     *
     * @return Success, or the store's error.
     */
    [[nodiscard]] virtual idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) = 0;

    /**
     * @brief Erases one sector, setting every stored byte to `0xFF`; the hook for
     *        @ref try_erase_sector.
     *
     * @param index The sector, less than @ref sector_count.
     *
     * @return Success, or the store's error.
     */
    [[nodiscard]] virtual idfxx::result<void> do_erase_sector(size_t index) = 0;

private:
    size_t _size;
    size_t _sector_size;
    size_t _write_size;
};

/**
 * @headerfile <idfxx/persistent_log>
 * @brief A flash partition, or a sector-aligned range of one, as a store for the log.
 *
 * The partition is read and written through the partition API, so it doesn't need to be
 * memory-mapped. On an encrypted partition, writes are encrypted and must be aligned to 16 bytes.
 * On any other partition, writes can start at any byte. A range lets the log share a partition
 * with other data.
 *
 * @code
 * // The log in the first 256 KiB of the "spare" partition.
 * auto spare = idfxx::partition::find("spare");
 * idfxx::persistent_log::start({
 *     .storage = std::make_unique<idfxx::persistent_log::partition_sector_store>(
 *         spare, 0, 256 * 1024
 *     ),
 * });
 * @endcode
 */
class partition_sector_store final : public sector_store {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a store over a whole partition.
     *
     * @param part The partition. Its size must be a non-zero multiple of its erase size.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the partition is empty or its
     *         size isn't a multiple of its erase size.
     */
    [[nodiscard]] explicit partition_sector_store(const idfxx::partition& part)
        : partition_sector_store(unwrap(make(part))) {}

    /**
     * @brief Creates a store over part of a partition.
     *
     * @param part   The partition.
     * @param offset Where the range starts in the partition. Must be a multiple of its erase size.
     * @param length The size of the range. Must be a non-zero multiple of its erase size.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_arg` if the range isn't aligned to the
     *         erase size, is empty, or extends past the end of the partition.
     */
    [[nodiscard]] explicit partition_sector_store(
        const idfxx::partition& part,
        size_t offset,
        size_t length
    )
        : partition_sector_store(unwrap(make(part, offset, length))) {}
#endif

    /**
     * @brief Makes a store over a whole partition.
     *
     * @param part The partition. Its size must be a non-zero multiple of its erase size.
     *
     * @return The store, or an error.
     * @retval idfxx::errc::invalid_arg The partition is empty or its size isn't a multiple of its
     *         erase size.
     */
    [[nodiscard]] static idfxx::result<partition_sector_store> make(const idfxx::partition& part);

    /**
     * @brief Makes a store over part of a partition.
     *
     * @param part   The partition.
     * @param offset Where the range starts in the partition. Must be a multiple of its erase size.
     * @param length The size of the range. Must be a non-zero multiple of its erase size.
     *
     * @return The store, or an error.
     * @retval idfxx::errc::invalid_arg The range isn't aligned to the erase size, is empty, or
     *         extends past the end of the partition.
     */
    [[nodiscard]] static idfxx::result<partition_sector_store>
    make(const idfxx::partition& part, size_t offset, size_t length);

private:
    struct checked {};

    partition_sector_store(checked, const idfxx::partition& part, size_t offset, size_t length)
        : sector_store(length, part.erase_size(), part.encrypted() ? 16 : 1)
        , _part(part)
        , _offset(offset) {}

    [[nodiscard]] idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override;
    [[nodiscard]] idfxx::result<void>
    do_read_raw(size_t offset, idfxx::byte_span dst) const override;
    [[nodiscard]] idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override;
    [[nodiscard]] idfxx::result<void> do_erase_sector(size_t index) override;

    idfxx::partition _part;
    size_t _offset;
};

/**
 * @headerfile <idfxx/persistent_log>
 * @brief When the writer task moves the lines waiting in RTC memory to flash.
 */
struct writer_config {
    /// The longest time that a line waits in RTC memory. The writer flushes at least this often.
    /// Must be at least one tick (`portTICK_PERIOD_MS` milliseconds) and at most
    /// `UINT32_MAX / configTICK_RATE_HZ` milliseconds, the longest interval that can be converted
    /// to ticks.
    std::chrono::milliseconds interval = std::chrono::seconds{5};
    /// The number of waiting bytes that wakes the writer before the interval ends. Defaults to
    /// half the RTC ring (`CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE`), so that lines are written
    /// before the ring fills and drops the oldest lines. Must be at least 1 and less than the
    /// ring's size.
    size_t threshold = CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE / 2;
    /// The writer task's stack size in bytes. Must be at least `configMINIMAL_STACK_SIZE`, the
    /// smallest stack that FreeRTOS accepts. A flush needs far more than that, and the default
    /// allows for it.
    size_t stack_size = 4096;
    /// The writer task's priority. Must be less than `configMAX_PRIORITIES`.
    idfxx::task_priority priority = 1;
};

#if defined(CONFIG_IDFXX_STD_FORMAT) || defined(__DOXYGEN__)
/**
 * @headerfile <idfxx/persistent_log>
 * @brief Which log lines are captured.
 *
 * A line is captured when its level is at or above the threshold for its tag. The threshold is the
 * tag's entry in @ref tag_levels if it has one, or @ref level otherwise. These thresholds are
 * independent of the console's log levels. A line that `esp_log_level_set()` hides from the
 * console is still captured if it meets the threshold. Lines above the compile-time maximum level
 * (`CONFIG_LOG_MAXIMUM_LEVEL`, or `LOG_LOCAL_LEVEL` in a source file) are never logged at all. To
 * store such lines, use @ref append instead. Lines logged through idfxx::log are captured by the
 * same rules as `ESP_LOGx` lines. A line that the console's level for its tag hides is still
 * formatted and captured if it meets the threshold.
 *
 * The log captures lines with a sink that it adds to idfxx::log (see @ref idfxx::log::add_sink).
 * Which lines the sink receives depends on `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES`:
 * - With the option enabled (the default), the log adds the sink with
 *   @ref idfxx::log::all_lines. The sink receives the lines logged through ESP-IDF's log
 *   (`ESP_LOGx`, `ESP_LOG_BUFFER_*`, `esp_log_write` and the libraries that log through them),
 *   including the lines logged through idfxx::log.
 * - With the option disabled, the log adds the sink without @ref idfxx::log::all_lines. The sink
 *   receives only the lines logged with idfxx::log's @ref idfxx::log::log, a
 *   @ref idfxx::log::logger and the `IDFXX_LOGx` macros, and it receives each of these lines
 *   complete. The sink does not receive buffer dumps (@ref idfxx::log::buffer_hex and the related
 *   functions). It also does not receive the lines that ESP-IDF, libraries and other idfxx
 *   components log through ESP-IDF's log.
 *
 * With the option enabled, the sink receives the lines in the form that
 * `CONFIG_IDFXX_LOG_ALL_LINES` passes them. As a result:
 * - Early and DRAM lines are not captured.
 * - Lines logged in ESP-IDF's binary log mode are not captured.
 * - A line logged in several calls (such as the Wi-Fi libraries' prefix, message and newline) is
 *   captured as one line.
 * - The captured message excludes the prefix that the line's caller formatted (log v1's level,
 *   timestamp and tag).
 *
 * With the option enabled or disabled, lines logged from an interrupt, while the flash cache is
 * disabled, or before the scheduler starts are not captured. Lines that a task logs while it is
 * inside the log itself, for example while writing to flash, are not captured either.
 *
 * Each captured line is stored as `L tag: message`, using the level and tag that the line was
 * logged with. A line without a tag is stored as `L message`. A message that contains newlines is
 * stored as one entry. The entry's first line has the stamp, then the level and tag (`L tag: `).
 * Each later line of the message starts with a tab and belongs to the same entry. Empty lines
 * after the first line are skipped. An entry (`L tag: ` and the message) longer than
 * `CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE` bytes is shortened to that limit at a UTF-8 character
 * boundary, before it is split into lines. A line logged through ESP-IDF's log may already have
 * been shortened (see `CONFIG_IDFXX_LOG_SINK_MAX_LINE`). Lines logged through idfxx::log are
 * received complete.
 *
 * Text is stored as it was logged. Don't log passwords, keys or other secrets, either with
 * @ref append or in a log line while capture is on.
 *
 * Capture uses stack from the task that logs the line:
 * - With `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES` enabled, capture uses the stack that
 *   idfxx::log uses for a sink added with @ref idfxx::log::all_lines (see
 *   `CONFIG_IDFXX_LOG_SINK_MAX_LINE`), plus up to about 768 bytes.
 * - With the option disabled, capture uses up to about 768 bytes more than the task uses to log
 *   the line through idfxx::log without capture.
 *
 * Both figures were measured on esp32s3 in QEMU. A task with a small stack that logs while capture
 * is on may need a larger stack. ESP-IDF's system event task
 * (`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`) is one such task.
 *
 * @note Only available when `CONFIG_IDFXX_STD_FORMAT` is enabled in menuconfig. Which lines are
 *       captured depends on `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES`, as described above.
 *       That option turns on `CONFIG_IDFXX_LOG_ALL_LINES`, so idfxx::log wraps `esp_log` and
 *       `esp_log_va` at link time (`-Wl,--wrap`), and nothing else in the app may wrap them.
 */
struct capture_config {
    /// The threshold for tags not in @ref tag_levels.
    idfxx::log::level level = idfxx::log::level::warn;
    /// Tags with their own threshold. If a tag is listed more than once, the last entry is used.
    std::vector<std::pair<std::string, idfxx::log::level>> tag_levels = {};
};
#endif

/**
 * @headerfile <idfxx/persistent_log>
 * @brief The fault slots at the end of the log's storage.
 *
 * The slots use `slot_size * slot_count` bytes at the end of the log's partition or store (see
 * @ref config::storage). The log uses the rest.
 */
struct fault_config {
    /// The size of each slot. Must be a multiple of the storage's sector size, and more than 512
    /// bytes.
    size_t slot_size = 8 * 1024;
    /// The number of slots. Must be at least 1.
    size_t slot_count = 4;
};

/**
 * @headerfile <idfxx/persistent_log>
 * @brief The log's configuration, for @ref start.
 */
struct config {
    /// Where the log is stored: either the label of a flash partition, which @ref start looks up,
    /// or a store (see @ref sector_store), which start takes ownership of. The log owns the store
    /// until @ref stop destroys it, after the log is closed. If start fails, it destroys the store
    /// before returning. The fault slots, if any, use the end of the partition or store.
    std::variant<std::string, std::unique_ptr<sector_store>> storage = std::string("log");
    /// The boot count for each line's stamp. The app maintains the count, for example in NVS.
    uint32_t boot_count = 0;
    /// Returns the wall-clock time for each line's stamp, or `std::nullopt` while the time isn't
    /// known. If this function is empty, the log uses the system clock, and treats the clock as
    /// unset while it reads a time before 2020. The function is called from any task that logs,
    /// and must not throw or block.
    std::function<std::optional<std::chrono::system_clock::time_point>()> wall_clock = {};
    /// The writer task's settings, or `std::nullopt` for no writer task. Without a writer task,
    /// lines are written to flash only when the app calls @ref flush or @ref flush_if_pending.
    std::optional<writer_config> writer = writer_config{};
#if defined(CONFIG_IDFXX_STD_FORMAT) || defined(__DOXYGEN__)
    /// Which log lines are captured, or `std::nullopt` to capture none, so that only @ref append
    /// adds lines. Which lines capture receives depends on
    /// `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES` (see @ref capture_config). Only available
    /// when `CONFIG_IDFXX_STD_FORMAT` is enabled in menuconfig. Without that option, only
    /// @ref append adds lines.
    std::optional<capture_config> capture = capture_config{};
#endif
    /// The fault slots, or `std::nullopt` for none. Without fault slots, the log uses the whole
    /// partition or store.
    std::optional<fault_config> faults = fault_config{};
};

class entry;

/// @cond INTERNAL
namespace detail {

// Makes entries from stored text, and returns an entry's stored text, for the code that splits text
// into entries.
struct entry_access;

} // namespace detail
/// @endcond

/**
 * @headerfile <idfxx/persistent_log>
 * @brief One entry of the log: its stamp, if it has one, and its text.
 *
 * An entry is a view of text that is held elsewhere, and it is valid only while that text is
 * unchanged. @ref entries and @ref next_entry take entries from text that the caller holds, such as
 * the text that @ref copy_newest_in_memory or @ref copy_newest copies. An @ref entry_reader returns
 * the entries of the log in flash, and a @ref fault_reader returns the entries of a fault's
 * context.
 *
 * An entry is stamped when its text starts with a complete, valid stamp (see
 * @ref idfxx_persistent_log_line). Text that was cut short or damaged may not start with one. The
 * entry is then not stamped, its stamp's fields are 0 or empty, and @ref text returns all of its
 * text. An entry has no level or tag, because a line that @ref append added can't be told apart
 * from a captured line.
 *
 * A default-constructed entry is empty and not stamped.
 *
 * When `CONFIG_IDFXX_STD_FORMAT` is enabled in menuconfig, `std::format` writes an entry exactly as
 * it is stored, with its stamp and without its final newline. It accepts only an empty format spec.
 *
 * @code
 * for (const auto& e : idfxx::persistent_log::read()) {
 *     if (e.stamped() && e.boot() == boot) {
 *         send(std::format("{}\n", e));
 *     }
 * }
 * @endcode
 */
class entry {
public:
    /** @brief Constructs an empty entry, which is not stamped. */
    entry() = default;

    /** @brief Returns true when the entry starts with a complete, valid stamp. */
    [[nodiscard]] bool stamped() const noexcept { return _stamp_size > 0; }

    /** @brief Returns the boot count in the entry's stamp, or 0 when the entry isn't stamped. */
    [[nodiscard]] uint32_t boot() const noexcept { return _boot; }

    /**
     * @brief Returns the wake count within the boot in the entry's stamp, or 0 when the entry isn't
     *        stamped.
     */
    [[nodiscard]] uint32_t wake() const noexcept { return _wake; }

    /**
     * @brief Returns the time since the boot or the deep-sleep wake when the entry was logged, from
     *        the entry's stamp. Returns zero when the entry isn't stamped.
     */
    [[nodiscard]] std::chrono::milliseconds uptime() const noexcept { return _uptime; }

    /**
     * @brief Returns the wall-clock time in the entry's stamp, to the second.
     *
     * Returns `std::nullopt` when the entry isn't stamped, or when the clock wasn't set when the
     * entry was logged.
     */
    [[nodiscard]] std::optional<std::chrono::sys_seconds> wall_time() const noexcept {
        return _wall;
    }

    /**
     * @brief Returns the entry's text.
     *
     * For a stamped entry, the text is the stored text after the stamp, without the final newline.
     * Each line of the text after the first starts with a tab, as it is stored. For an entry that
     * isn't stamped, the text is all of the entry's stored text, without the final newline.
     */
    [[nodiscard]] std::string_view text() const noexcept { return _stored.substr(_stamp_size); }

private:
    /// @cond INTERNAL
    friend struct detail::entry_access;
    /// @endcond

    std::string_view _stored;
    std::chrono::milliseconds _uptime{0};
    std::optional<std::chrono::sys_seconds> _wall;
    uint32_t _boot = 0;
    uint32_t _wake = 0;
    // The size of the stamp at the start of _stored, or 0 when the entry isn't stamped. A stamp is
    // at most 72 bytes.
    uint8_t _stamp_size = 0;
};

/// @cond INTERNAL
namespace detail {

struct entry_access {
    // Returns the entry for `stored`, stamped if `stored` starts with a complete, valid stamp.
    [[nodiscard]] static entry parsed(std::string_view stored) noexcept;
    // Returns an entry for `stored` that isn't stamped.
    [[nodiscard]] static entry unstamped(std::string_view stored) noexcept {
        entry e;
        e._stored = stored;
        return e;
    }
    // Returns the entry's text as it is stored, with its stamp.
    [[nodiscard]] static std::string_view stored(const entry& e) noexcept { return e._stored; }
};

} // namespace detail
/// @endcond

/**
 * @brief Takes the first entry from text that the caller holds.
 *
 * Sets @p out to the first entry in @p text, and removes that entry and its final newline from the
 * front of @p text. @p out refers to the same characters as @p text, so it is valid while those
 * characters are unchanged. Empty lines are skipped. The entry ends at the first newline that isn't
 * followed by a tab, or at the end of @p text.
 *
 * @p text should start at the beginning of an entry and end at the end of one, as the text that
 * @ref copy_newest_in_memory and @ref copy_newest copy does. Otherwise, the entries at the ends of
 * @p text are returned as they are:
 * - When the first line starts with a tab, that line and the lines after it that start with a tab
 *   are returned as one entry, which isn't stamped.
 * - When the last entry has no final newline, it is returned without one.
 *
 * Whether an entry is stamped is decided only by parsing the start of its first line (see
 * @ref entry::stamped). So an entry whose stamp was cut short isn't stamped.
 *
 * @ref entries splits text in the same way, as a range.
 *
 * @code
 * std::vector<char> buffer(CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE);
 * std::string_view text = idfxx::persistent_log::copy_newest_in_memory(buffer);
 * idfxx::persistent_log::entry e;
 * while (idfxx::persistent_log::next_entry(text, e)) {
 *     send(std::format("{}\n", e));
 * }
 * @endcode
 *
 * @param text The text. On return, it holds the text after the entry.
 * @param out  Set to the entry if there is one, and left unchanged otherwise.
 *
 * @return True if @p out was set. False when @p text holds no entry, because it is empty or holds
 *         only empty lines. @p text is then left empty.
 */
[[nodiscard]] bool next_entry(std::string_view& text, entry& out) noexcept;

/**
 * @headerfile <idfxx/persistent_log>
 * @brief A view of text that the caller holds, as a sequence of entries.
 *
 * The view splits the text into entries exactly as @ref next_entry does. Each entry refers to the
 * same characters as the text, so it is valid while those characters are unchanged. The view does
 * not own the text. Iterators refer to the text directly, not to the view, so they stay valid after
 * the view is destroyed (the view is a borrowed range). The view can be iterated more than once.
 *
 * @code
 * std::vector<char> buffer(CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE);
 * const std::string_view text = idfxx::persistent_log::copy_newest_in_memory(buffer);
 * for (idfxx::persistent_log::entry e : idfxx::persistent_log::entries(text)) {
 *     send(std::format("{}\n", e));
 * }
 * @endcode
 */
class entries : public std::ranges::view_interface<entries> {
public:
    /** @brief A forward iterator over the entries of the text. */
    class iterator {
    public:
        /** @brief The iterator's category: a forward iterator. */
        using iterator_concept = std::forward_iterator_tag;
        /** @brief The type of the entries. */
        using value_type = entry;
        /** @brief Distance type (required by the iterator concepts). */
        using difference_type = std::ptrdiff_t;

        /** @brief Creates an iterator at the end of the text. */
        iterator() noexcept = default;

        /** @brief Returns the entry at the current position. */
        [[nodiscard]] entry operator*() const noexcept { return _entry; }

        /** @brief Advances to the next entry. */
        iterator& operator++() noexcept {
            _at_end = !next_entry(_rest, _entry);
            return *this;
        }

        /** @brief Advances to the next entry, and returns the previous position. */
        iterator operator++(int) noexcept {
            iterator prev = *this;
            ++*this;
            return prev;
        }

        /** @brief Returns true if both iterators are at the same position in the same text. */
        [[nodiscard]] bool operator==(const iterator& other) const noexcept {
            return _at_end == other._at_end && (_at_end || _rest.data() == other._rest.data());
        }

        /** @brief Returns true if the iterator has passed the last entry of the text. */
        [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept { return _at_end; }

    private:
        friend class entries;

        explicit iterator(std::string_view text) noexcept
            : _rest(text) {
            ++*this;
        }

        // The text after the current entry.
        std::string_view _rest;
        entry _entry;
        bool _at_end = true;
    };

    /** @brief Creates an empty view. */
    entries() noexcept = default;

    /**
     * @brief Creates a view of the entries in @p text.
     *
     * @param text The text, as @ref next_entry takes it. It must outlive the view's entries and
     *             iterators.
     */
    explicit entries(std::string_view text) noexcept
        : _text(text) {}

    /** @brief Returns an iterator at the first entry. */
    [[nodiscard]] iterator begin() const noexcept { return iterator(_text); }

    /** @brief Returns the sentinel for the end of the text. */
    [[nodiscard]] std::default_sentinel_t end() const noexcept { return std::default_sentinel; }

private:
    std::string_view _text;
};

/**
 * @headerfile <idfxx/persistent_log>
 * @brief Streams the log in flash as text, oldest first, in chunks the size of the caller's
 *        buffer.
 *
 * A reader returned by @ref read_text stops where the log in flash ended when the reader was
 * created. It doesn't read lines added later. The reader can be kept while the log is written to,
 * and any task can call it. If text is overwritten before the reader reaches it, the reader skips
 * that text. It ends the line that it was in and resumes at the next entry. After the log is
 * stopped, the reader reads nothing more.
 */
class text_reader {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the next part of the log.
     *
     * After a call fails, every later call fails with the same error.
     *
     * @param out The buffer to fill.
     *
     * @return The text put at the front of @p out, as a view of @p out. It is shorter than @p out
     *         only at the end of the log, and empty once the whole log has been read.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_state` if the log has been stopped
     *         since the reader was made, or with the flash error if a read fails.
     */
    [[nodiscard]] std::string_view next(std::span<char> out) { return unwrap(try_next(out)); }
#endif

    /**
     * @brief Reads the next part of the log.
     *
     * After a call fails, every later call fails with the same error.
     *
     * @param out The buffer to fill.
     *
     * @return The text put at the front of @p out, as a view of @p out. It is shorter than @p out
     *         only at the end of the log, and empty once the whole log has been read. Or an error.
     * @retval idfxx::errc::invalid_state The log has been stopped since the reader was made.
     * @retval other The flash error, if a read fails.
     */
    [[nodiscard]] idfxx::result<std::string_view> try_next(std::span<char> out);

    /** @brief Destroys the reader. The log is left as it is. */
    ~text_reader();

    text_reader(const text_reader&) = delete;
    text_reader& operator=(const text_reader&) = delete;

    /**
     * @brief Move constructor. Transfers the reader's place in the log.
     *
     * A moved-from reader must not be used: any operation other than destruction or assignment
     * is undefined behavior.
     *
     * @param other The reader to move from.
     */
    text_reader(text_reader&& other) noexcept;

    /**
     * @brief Move assignment. Transfers the other reader's place in the log, replacing this
     *        reader's place.
     *
     * A moved-from reader must not be used: any operation other than destruction or assignment
     * is undefined behavior.
     *
     * @param other The reader to move from.
     *
     * @return This reader.
     */
    text_reader& operator=(text_reader&& other) noexcept;

private:
    friend idfxx::result<text_reader> try_read_text();

    /// @cond INTERNAL
    struct state;
    explicit text_reader(std::unique_ptr<state> s) noexcept;
    /// @endcond

    std::unique_ptr<state> _state;
};

/**
 * @headerfile <idfxx/persistent_log>
 * @brief Steps through the entries of the log in flash, one entry at a time.
 *
 * A reader returned by @ref read returns every entry of the log in flash, oldest first. A reader
 * returned by @ref read_newest returns the newest entries, the oldest of them first. The entries
 * are returned in the order in which they were added to the log.
 *
 * The reader stops where the log in flash ended when the reader was created. It doesn't read lines
 * added later. The reader can be kept while the log is written to, and any task can call it. After
 * the log is stopped, the reader reads nothing more.
 *
 * The reader holds the text that it reads in a buffer of its own, which it allocates when it is
 * created. The buffer's size is `CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE` plus the size of the
 * longest entry that the log stores. The reader reads more text into its buffer than it returns.
 * So it can return entries whose text in flash was overwritten after the reader read it. When the
 * reader reaches text that was overwritten before the reader read it, the reader skips that text.
 * The entry that the reader was in is cut short at that point, and the reader resumes at the next
 * entry. An entry that is cut short can still be stamped, with its text shortened.
 *
 * Each entry that the reader returns refers to text in this buffer. The entry is valid until the
 * next call to the reader, or until the reader is destroyed. An entry that doesn't fit in the
 * buffer is returned as several entries. Only the first of them can be stamped, and none of them
 * is stamped when the entry's first line doesn't fit in the buffer on its own.
 * An entry of the longest size that the log stores always fits.
 *
 * When `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled in menuconfig, the reader is also a range of
 * entries, which a range-based `for` loop reads:
 * @code
 * for (const auto& e : idfxx::persistent_log::read_newest(20)) {
 *     send(std::format("{}\n", e));
 * }
 * @endcode
 */
class entry_reader {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @headerfile <idfxx/persistent_log>
     * @brief A single-pass input iterator over the entries that an @ref entry_reader returns.
     *
     * The iterator refers to its reader, and the reader holds the current entry. The entry is
     * valid until the iterator is incremented, or until the reader is destroyed. The iterator can
     * be moved but not copied.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     */
    class iterator {
    public:
        /** @brief The iterator's category: a single-pass input iterator. */
        using iterator_concept = std::input_iterator_tag;
        /** @brief The type of the entries. */
        using value_type = entry;
        /** @brief Distance type (required by the iterator concepts). */
        using difference_type = std::ptrdiff_t;

        iterator(const iterator&) = delete;
        iterator& operator=(const iterator&) = delete;

        /**
         * @brief Move constructor.
         *
         * @param other The iterator to move from.
         */
        iterator(iterator&& other) noexcept = default;

        /**
         * @brief Move assignment.
         *
         * @param other The iterator to move from.
         *
         * @return This iterator.
         */
        iterator& operator=(iterator&& other) noexcept = default;

        /**
         * @brief Returns the current entry.
         *
         * The entry is valid until the iterator is incremented, or until the reader is destroyed.
         */
        [[nodiscard]] const entry& operator*() const noexcept;

        /**
         * @brief Reads the next entry.
         *
         * @return This iterator, which is equal to `end()` once every entry has been returned.
         * @throws std::system_error as @ref entry_reader::next does. After it throws, every later
         *         call to the reader throws the same error.
         */
        iterator& operator++();

        /**
         * @brief Reads the next entry.
         *
         * @throws std::system_error as @ref entry_reader::next does. After it throws, every later
         *         call to the reader throws the same error.
         */
        void operator++(int) { ++*this; }

        /** @brief Returns true once every entry has been returned. */
        [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept {
            return _reader == nullptr;
        }

    private:
        friend class entry_reader;

        explicit iterator(entry_reader* reader) noexcept
            : _reader(reader) {}

        // The reader, or null at the end.
        entry_reader* _reader;
    };

    /**
     * @brief Reads the next entry, and returns an iterator at it.
     *
     * The reader keeps its place in the log. A second call continues after the last entry that
     * the reader returned, through an iterator or through @ref next, as a range over an input
     * stream does. It doesn't start again from the first entry. So after a loop that ends early
     * (with `break`), a later loop starts at the entry after the last entry that the first loop
     * received.
     *
     * @return An iterator at the entry, or an iterator equal to `end()` once every entry has been
     *         returned.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error as @ref next does. After it throws, every later call to the reader
     *         throws the same error.
     */
    [[nodiscard]] iterator begin();

    /**
     * @brief Returns the sentinel that an iterator equals once every entry has been returned.
     *
     * @return The sentinel.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     */
    [[nodiscard]] std::default_sentinel_t end() const noexcept { return std::default_sentinel; }

    /**
     * @brief Reads the next entry.
     *
     * After a call fails, every later call fails with the same error.
     *
     * @param out Set to the entry if there is one, and left unchanged otherwise. The entry refers
     *            to text in the reader's buffer. It is valid until the next call to the reader, or
     *            until the reader is destroyed.
     *
     * @return True if @p out was set, or false once every entry has been returned.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_state` if the log has been stopped
     *         since the reader was made, or with the flash error if a read fails.
     */
    [[nodiscard]] bool next(entry& out) { return unwrap(try_next(out)); }
#endif

    /**
     * @brief Reads the next entry.
     *
     * After a call fails, every later call fails with the same error.
     *
     * @param out Set to the entry if there is one, and left unchanged otherwise. The entry refers
     *            to text in the reader's buffer. It is valid until the next call to the reader, or
     *            until the reader is destroyed.
     *
     * @return True if @p out was set, or false once every entry has been returned. Or an error.
     * @retval idfxx::errc::invalid_state The log has been stopped since the reader was made.
     * @retval other The flash error, if a read fails.
     */
    [[nodiscard]] idfxx::result<bool> try_next(entry& out);

    /** @brief Destroys the reader and its buffer. The log is left as it is. */
    ~entry_reader();

    entry_reader(const entry_reader&) = delete;
    entry_reader& operator=(const entry_reader&) = delete;

    /**
     * @brief Move constructor. Transfers the reader's place in the log and its buffer.
     *
     * The entries that the other reader returned stay valid. A moved-from reader must not be used:
     * any operation other than destruction or assignment is undefined behavior.
     *
     * @param other The reader to move from.
     */
    entry_reader(entry_reader&& other) noexcept;

    /**
     * @brief Move assignment. Transfers the other reader's place in the log and its buffer,
     *        replacing this reader's place and buffer.
     *
     * The entries that the other reader returned stay valid, and the entries that this reader
     * returned are no longer valid. A moved-from reader must not be used: any operation other than
     * destruction or assignment is undefined behavior.
     *
     * @param other The reader to move from.
     *
     * @return This reader.
     */
    entry_reader& operator=(entry_reader&& other) noexcept;

private:
    friend idfxx::result<entry_reader> try_read();
    friend idfxx::result<entry_reader> try_read_newest(size_t count);

    /// @cond INTERNAL
    struct state;
    explicit entry_reader(std::unique_ptr<state> s) noexcept;
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    // Reads the next entry into the state's current entry. Returns false once every entry has been
    // returned.
    bool _advance();
#endif
    /// @endcond

    std::unique_ptr<state> _state;
};

// =============================================================================
// Starting and stopping
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Starts the log.
 *
 * Opens the log in the partition or the store that @ref config::storage specifies. If the log's
 * position stored in RTC memory still matches the storage, this function uses it. Otherwise, it
 * finds the position by reading every sector's header. It then counts the wake, and starts
 * capturing log lines (see @ref capture_config) and starts the writer task if @p cfg enables them.
 * After a wake from deep sleep, the wake count is one more than its last value in this boot. After
 * any other reset, the wake count starts at 0.
 *
 * Lines that were still in RTC memory at the reset are written at the next flush. After a firmware
 * update that changes `CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE`, this function discards those
 * lines instead. If the reset interrupted the last of these lines, this function ends that line.
 * Lines that a flush was writing when the reset occurred are lost. If a power cut left a partial
 * line in flash, that line is also ended, before anything new is written. If that write fails, a
 * warning is logged and the log starts anyway.
 *
 * If the storage holds log sectors or fault slots in a newer layout, as older firmware finds after
 * a rollback, they are treated as unused. This function logs a warning when it finds them while it
 * opens the log. It reads the header of every fault slot. After a reset that keeps RTC memory, it
 * usually opens the log from the position stored there. It then reads only the sectors at the end
 * of the log, so it may not find log sectors in a newer layout.
 *
 * Once the log is open, the first start in a boot also handles the reset that preceded it:
 * - A reset that is a fault (`panic`, `interrupt_watchdog`, `task_watchdog`, `watchdog`,
 *   `brownout`, `power_glitch` or `cpu_lockup`, as `idfxx::reset_reason` names them) is recorded
 *   as a fault of that kind, with its repeats suppressed (see @ref suppress_repeats). Another reset
 *   of the same kind, whatever its detail, is then only a repeat, until a reset that isn't a fault.
 *   The fault's boot count, wake count and times are those of the start that found the reset. The
 *   lines in its context keep their own stamps.
 *   After a panic or a watchdog reset, the fault can also take information from a core dump, if
 *   the app stores core dumps in flash (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`, in ELF form). The
 *   dump must be complete, from this build, and not used before. The log remembers the last dump
 *   it used in the fault store in flash, so this holds across a power cycle and a clear of the
 *   faults. The fault then takes its task, program counter and backtrace from the dump (on
 *   RISC-V, the program counter and return address). The dump also provides the fault's detail,
 *   which is the panic reason if there is one (an abort's or the task watchdog's). Otherwise, the
 *   detail is the exception's cause and address, and for a panic also its program counter
 *   (`exccause=<n> excvaddr=0x<hex> pc=0x<hex>`, or on RISC-V
 *   `mcause=0x<hex> mtval=0x<hex> pc=0x<hex>`). The dump is left in flash, and both the fault
 *   store in flash and RTC memory record that it has been used.
 *   A dump whose fault was suppressed as a repeat is recorded only in RTC memory. So after RTC
 *   memory is lost (a power cycle), a later panic or watchdog reset that wrote no new dump can
 *   take its details from that dump. The same can happen after RTC memory is lost if the fault
 *   store holds no entry. That is the case when the fault store was never written, when it was
 *   erased, or when it has a single slot and power was cut while that slot was rewritten.
 * - A wake from deep sleep adds no entry. The wake count in the stamps shows it.
 * - Any other reset adds the entry `I persistent_log: reset reason=<name>`, with `unknown_<n>` as
 *   the name of a value that `idfxx::reset_reason` doesn't name. It also lifts the suppression of
 *   the fault kinds of the resets listed above.
 *
 * Without fault slots (@ref config::faults), a reset that is a fault is also only logged by name.
 * If recording the fault fails, a warning is logged and the log starts anyway. Once the scheduler
 * is running, this function records a fault reset on a short-lived task of its own, at the
 * caller's priority, and waits for that task, so that recording the fault doesn't use the calling
 * task's stack. If memory for that task can't be allocated, this function throws `std::bad_alloc`,
 * or aborts when exceptions are disabled. Before the scheduler runs, this function records the
 * fault on the calling task's stack. Apart from that case, this function uses up to about 3 KiB
 * of the calling task's stack in ESP-IDF's default debug build (`-Og`), or 2.3 KiB in a build
 * optimized for size (`-Os`), as measured on esp32s3. That fits within ESP-IDF's default main task
 * stack (3584 bytes).
 *
 * The log takes ownership of a store given in @p cfg. The store is destroyed by @ref stop, or
 * before this function returns if the start fails.
 *
 * @param cfg The configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log is already started, or if
 *         capture is on and idfxx::log already has `CONFIG_IDFXX_LOG_MAX_SINKS` sinks added.
 *         The error is `idfxx::errc::not_found` if no partition has the label, or the flash error
 *         if a read fails. It is `idfxx::errc::invalid_arg` if any of the following is true:
 *         - The store is null, or the log doesn't support its geometry (see @ref sector_store).
 *         - The writer's threshold is 0, or not less than the RTC ring's size.
 *         - The writer's interval is less than a tick, or more than the longest allowed (see
 *           @ref writer_config::interval).
 *         - The writer's stack is smaller than `configMINIMAL_STACK_SIZE`.
 *         - The writer's priority is not less than `configMAX_PRIORITIES`.
 *         - The fault slot count is 0.
 *         - The fault slot size is 512 bytes or less, or isn't a multiple of the storage's sector
 *           size.
 *         - The fault slots don't fit in the storage with room for the log (at least two
 *           sectors).
 */
void start(config cfg = {});
#endif

/**
 * @brief Starts the log.
 *
 * Opens the log in the partition or the store that @ref config::storage specifies. If the log's
 * position stored in RTC memory still matches the storage, this function uses it. Otherwise, it
 * finds the position by reading every sector's header. It then counts the wake, and starts
 * capturing log lines (see @ref capture_config) and starts the writer task if @p cfg enables them.
 * After a wake from deep sleep, the wake count is one more than its last value in this boot. After
 * any other reset, the wake count starts at 0.
 *
 * Lines that were still in RTC memory at the reset are written at the next flush. After a firmware
 * update that changes `CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE`, this function discards those
 * lines instead. If the reset interrupted the last of these lines, this function ends that line.
 * Lines that a flush was writing when the reset occurred are lost. If a power cut left a partial
 * line in flash, that line is also ended, before anything new is written. If that write fails, a
 * warning is logged and the log starts anyway.
 *
 * If the storage holds log sectors or fault slots in a newer layout, as older firmware finds after
 * a rollback, they are treated as unused. This function logs a warning when it finds them while it
 * opens the log. It reads the header of every fault slot. After a reset that keeps RTC memory, it
 * usually opens the log from the position stored there. It then reads only the sectors at the end
 * of the log, so it may not find log sectors in a newer layout.
 *
 * Once the log is open, the first start in a boot also handles the reset that preceded it:
 * - A reset that is a fault (`panic`, `interrupt_watchdog`, `task_watchdog`, `watchdog`,
 *   `brownout`, `power_glitch` or `cpu_lockup`, as `idfxx::reset_reason` names them) is recorded
 *   as a fault of that kind, with its repeats suppressed (see @ref suppress_repeats). Another reset
 *   of the same kind, whatever its detail, is then only a repeat, until a reset that isn't a fault.
 *   The fault's boot count, wake count and times are those of the start that found the reset. The
 *   lines in its context keep their own stamps.
 *   After a panic or a watchdog reset, the fault can also take information from a core dump, if
 *   the app stores core dumps in flash (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`, in ELF form). The
 *   dump must be complete, from this build, and not used before. The log remembers the last dump
 *   it used in the fault store in flash, so this holds across a power cycle and a clear of the
 *   faults. The fault then takes its task, program counter and backtrace from the dump (on
 *   RISC-V, the program counter and return address). The dump also provides the fault's detail,
 *   which is the panic reason if there is one (an abort's or the task watchdog's). Otherwise, the
 *   detail is the exception's cause and address, and for a panic also its program counter
 *   (`exccause=<n> excvaddr=0x<hex> pc=0x<hex>`, or on RISC-V
 *   `mcause=0x<hex> mtval=0x<hex> pc=0x<hex>`). The dump is left in flash, and both the fault
 *   store in flash and RTC memory record that it has been used.
 *   A dump whose fault was suppressed as a repeat is recorded only in RTC memory. So after RTC
 *   memory is lost (a power cycle), a later panic or watchdog reset that wrote no new dump can
 *   take its details from that dump. The same can happen after RTC memory is lost if the fault
 *   store holds no entry. That is the case when the fault store was never written, when it was
 *   erased, or when it has a single slot and power was cut while that slot was rewritten.
 * - A wake from deep sleep adds no entry. The wake count in the stamps shows it.
 * - Any other reset adds the entry `I persistent_log: reset reason=<name>`, with `unknown_<n>` as
 *   the name of a value that `idfxx::reset_reason` doesn't name. It also lifts the suppression of
 *   the fault kinds of the resets listed above.
 *
 * Without fault slots (@ref config::faults), a reset that is a fault is also only logged by name.
 * If recording the fault fails, a warning is logged and the log starts anyway. Once the scheduler
 * is running, this function records a fault reset on a short-lived task of its own, at the
 * caller's priority, and waits for that task, so that recording the fault doesn't use the calling
 * task's stack. If memory for that task can't be allocated, this function throws `std::bad_alloc`,
 * or aborts when exceptions are disabled. Before the scheduler runs, this function records the
 * fault on the calling task's stack. Apart from that case, this function uses up to about 3 KiB
 * of the calling task's stack in ESP-IDF's default debug build (`-Og`), or 2.3 KiB in a build
 * optimized for size (`-Os`), as measured on esp32s3. That fits within ESP-IDF's default main task
 * stack (3584 bytes).
 *
 * The log takes ownership of a store given in @p cfg. The store is destroyed by @ref stop, or
 * before this function returns if the start fails.
 *
 * @param cfg The configuration.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state The log is already started, or capture is on and idfxx::log
 *         already has `CONFIG_IDFXX_LOG_MAX_SINKS` sinks added.
 * @retval idfxx::errc::not_found No partition has the label.
 * @retval idfxx::errc::invalid_arg Any of the following is true:
 *         - The store is null, or the log doesn't support its geometry (see @ref sector_store).
 *         - The writer's threshold is 0, or not less than the RTC ring's size.
 *         - The writer's interval is less than a tick, or more than the longest allowed (see
 *           @ref writer_config::interval).
 *         - The writer's stack is smaller than `configMINIMAL_STACK_SIZE`.
 *         - The writer's priority is not less than `configMAX_PRIORITIES`.
 *         - The fault slot count is 0.
 *         - The fault slot size is 512 bytes or less, or isn't a multiple of the storage's sector
 *           size.
 *         - The fault slots don't fit in the storage with room for the log (at least two
 *           sectors).
 * @retval other The flash error, if a read fails.
 */
[[nodiscard]] idfxx::result<void> try_start(config cfg = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Stops the log.
 *
 * Stops accepting lines, stops the writer task, and flushes the waiting lines. Until the next
 * start, nothing is stored: @ref append drops its text, and no log lines are captured. The
 * final flush writes the lines still in RTC memory. If the app passed a store to @ref start, the
 * store is destroyed after the log is closed.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started, or with
 *         the flash error if the final flush fails. The log is stopped even when the final flush
 *         fails.
 */
void stop();
#endif

/**
 * @brief Stops the log.
 *
 * Stops accepting lines, stops the writer task, and flushes the waiting lines. Until the next
 * start, nothing is stored: @ref append drops its text, and no log lines are captured. The
 * final flush writes the lines still in RTC memory. If the app passed a store to @ref start, the
 * store is destroyed after the log is closed.
 *
 * @return Success, or an error. The log is stopped even when the final flush fails.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval other The flash error, if the final flush fails.
 */
idfxx::result<void> try_stop();

// =============================================================================
// Adding lines
// =============================================================================

/**
 * @brief Adds lines from the app to the log.
 *
 * @p text is added to the ring in RTC memory as one complete entry. Its first `'\n'`-separated
 * line gets a stamp. Each later line starts with a tab and belongs to the same entry, so no line
 * in @p text can be mistaken for a separate entry. Empty lines are skipped. An empty @p text adds
 * nothing, and a newline at the end of @p text adds nothing either. If @p text is longer than
 * `CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE` bytes in total, counted from its first non-empty line, it
 * is shortened to that limit at a UTF-8 character boundary. This function doesn't write to flash,
 * so any task can call it, and its cost is a copy of the text. The text is dropped while the log
 * isn't started, and when this function is called from an interrupt.
 *
 * Text is stored as it was passed. Don't log passwords, keys or other secrets, either with this
 * function or in a log line while capture is on.
 *
 * @param text The lines.
 */
void append(std::string_view text) noexcept;

#if defined(CONFIG_IDFXX_STD_FORMAT) || defined(__DOXYGEN__)
/**
 * @brief Formats text and adds it to the log as lines from the app.
 *
 * Formats @p args with @p fmt, as `std::format` does, and adds the result as
 * @ref append(std::string_view) does: as one entry, shortened at a UTF-8 character boundary when it
 * is longer than `CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE` bytes, and dropped in the same cases. The
 * text is formatted even when it is then dropped.
 *
 * A call with only text and no arguments, such as `append("a {} b")`, uses
 * @ref append(std::string_view) instead. Braces in that text are stored as they are, and are not
 * format fields.
 *
 * @code
 * idfxx::persistent_log::append("readings={} highest={} C", readings.size(), highest);
 * @endcode
 *
 * @tparam Args The types of the arguments to format.
 * @param fmt  The format string, which is checked against @p args at compile time.
 * @param args The arguments to format.
 *
 * @note Only available when `CONFIG_IDFXX_STD_FORMAT` is enabled in menuconfig.
 * @note The text is formatted into a buffer of `CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE` bytes on
 *       the caller's stack. Formatting stops when the buffer is full.
 * @note An exception thrown by a formatter passes to the caller, and nothing is added to the log.
 */
template<typename... Args>
    requires(sizeof...(Args) > 0)
void append(std::format_string<Args...> fmt, Args&&... args) {
    // A longer result stops at the end of the buffer, possibly inside a UTF-8 character.
    // append(std::string_view) removes an incomplete character from the end of its text.
    std::array<char, CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE> buffer;
    const auto r = std::format_to_n(buffer.data(), buffer.size(), fmt, std::forward<Args>(args)...);
    append(std::string_view(buffer.data(), static_cast<size_t>(r.out - buffer.data())));
}
#endif

// =============================================================================
// Flushing
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Writes the lines waiting in RTC memory to flash.
 *
 * If the RTC ring dropped lines because more lines were added than it holds, the flush first writes
 * the line `W persistent_log: dropped <n> bytes`, with a stamp like any other line. The oldest
 * remaining line may have lost its start. The flush leaves that line out, together with the lines
 * after it that start with a tab and continue its entry, and includes its bytes in the count. The
 * lines are marked as written before they are written. If a reset or a failed write interrupts the
 * flush, the rest of the lines are lost. The log loses them rather than writing the part that is
 * already in flash a second time.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started, or with
 *         the flash error if the write fails.
 */
void flush();
#endif

/**
 * @brief Writes the lines waiting in RTC memory to flash.
 *
 * If the RTC ring dropped lines because more lines were added than it holds, the flush first writes
 * the line `W persistent_log: dropped <n> bytes`, with a stamp like any other line. The oldest
 * remaining line may have lost its start. The flush leaves that line out, together with the lines
 * after it that start with a tab and continue its entry, and includes its bytes in the count. The
 * lines are marked as written before they are written. If a reset or a failed write interrupts the
 * flush, the rest of the lines are lost. The log loses them rather than writing the part that is
 * already in flash a second time.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval other The flash error, if the write fails.
 */
[[nodiscard]] idfxx::result<void> try_flush();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Writes the lines waiting in RTC memory to flash, if there are enough of them.
 *
 * This suits a device that wakes briefly from deep sleep. Before going back to sleep, the device
 * can use it to write to flash only when enough bytes are waiting to be worth the cost of a flash
 * write. Otherwise, the lines stay in RTC memory for a later wake.
 *
 * @param threshold The smallest number of waiting bytes for which the lines are written.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started, or with
 *         the flash error if the write fails.
 */
void flush_if_pending(size_t threshold);
#endif

/**
 * @brief Writes the lines waiting in RTC memory to flash, if there are enough of them.
 *
 * This suits a device that wakes briefly from deep sleep. Before going back to sleep, the device
 * can use it to write to flash only when enough bytes are waiting to be worth the cost of a flash
 * write. Otherwise, the lines stay in RTC memory for a later wake.
 *
 * @param threshold The smallest number of waiting bytes for which the lines are written.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval other The flash error, if the write fails.
 */
[[nodiscard]] idfxx::result<void> try_flush_if_pending(size_t threshold);

// =============================================================================
// Reading
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Flushes, then returns a reader for the entries of the whole log in flash, oldest first.
 *
 * The reader starts at the beginning of an entry. If the oldest text in flash has been
 * overwritten, the reader leaves out the rest of the entry that it would start in (see
 * @ref idfxx_persistent_log_line). The reader allocates its buffer when it is created (see
 * @ref entry_reader).
 *
 * If the flush fails, this function still returns the reader. The reader then reads the log in
 * flash, without the lines that the flush could not write.
 *
 * @return The reader.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started, or with
 *         the flash error if a read fails.
 */
[[nodiscard]] entry_reader read();
#endif

/**
 * @brief Flushes, then returns a reader for the entries of the whole log in flash, oldest first.
 *
 * The reader starts at the beginning of an entry. If the oldest text in flash has been
 * overwritten, the reader leaves out the rest of the entry that it would start in (see
 * @ref idfxx_persistent_log_line). The reader allocates its buffer when it is created (see
 * @ref entry_reader).
 *
 * If the flush fails, this function still returns the reader. The reader then reads the log in
 * flash, without the lines that the flush could not write.
 *
 * @return The reader, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval other The flash error, if a read fails.
 */
[[nodiscard]] idfxx::result<entry_reader> try_read();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Flushes, then returns a reader for the newest entries in flash, the oldest of them first.
 *
 * The reader returns the newest @p count entries in flash, or every entry when the log in flash
 * holds fewer. The reader allocates its buffer when it is created (see @ref entry_reader).
 *
 * If the flush fails, this function still returns the reader. The reader then reads the log in
 * flash, without the lines that the flush could not write.
 *
 * To find the entries, this function reads the newest sectors of the log in flash three times: to
 * find the sectors that hold enough text, to count the entries in them, and to read up to the
 * first entry that the reader returns. It reads only as many sectors as are needed to hold
 * `count + 1` times the size of the longest entry. So a @p count larger than the log makes it read
 * the whole log three times. This function holds the log's lock while it flushes and reads. Until
 * it returns, other flushes, fault recording and other readers wait, and the lines that are
 * captured wait in the RTC ring.
 *
 * The log may hold an entry longer than the longest entry that this build stores, such as text
 * written by a build with a larger `CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE`, or damaged text. The
 * reader can then return fewer or more than @p count entries.
 *
 * @param count The number of entries. With 0, the reader returns no entries.
 *
 * @return The reader.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started, or with
 *         the flash error if a read fails.
 */
[[nodiscard]] entry_reader read_newest(size_t count);
#endif

/**
 * @brief Flushes, then returns a reader for the newest entries in flash, the oldest of them first.
 *
 * The reader returns the newest @p count entries in flash, or every entry when the log in flash
 * holds fewer. The reader allocates its buffer when it is created (see @ref entry_reader).
 *
 * If the flush fails, this function still returns the reader. The reader then reads the log in
 * flash, without the lines that the flush could not write.
 *
 * To find the entries, this function reads the newest sectors of the log in flash three times: to
 * find the sectors that hold enough text, to count the entries in them, and to read up to the
 * first entry that the reader returns. It reads only as many sectors as are needed to hold
 * `count + 1` times the size of the longest entry. So a @p count larger than the log makes it read
 * the whole log three times. This function holds the log's lock while it flushes and reads. Until
 * it returns, other flushes, fault recording and other readers wait, and the lines that are
 * captured wait in the RTC ring.
 *
 * The log may hold an entry longer than the longest entry that this build stores, such as text
 * written by a build with a larger `CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE`, or damaged text. The
 * reader can then return fewer or more than @p count entries.
 *
 * @param count The number of entries. With 0, the reader returns no entries.
 *
 * @return The reader, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval other The flash error, if a read fails.
 */
[[nodiscard]] idfxx::result<entry_reader> try_read_newest(size_t count);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Flushes, then returns a reader for the whole log in flash as text, oldest first.
 *
 * The reader starts at the beginning of an entry. If the oldest text in flash has been
 * overwritten, the reader leaves out the rest of the entry that it would start in (see
 * @ref idfxx_persistent_log_line).
 *
 * If the flush fails, this function still returns the reader. The reader then reads the log in
 * flash, without the lines that the flush could not write.
 *
 * @return The reader.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started, or with
 *         the flash error if a read fails.
 */
[[nodiscard]] text_reader read_text();
#endif

/**
 * @brief Flushes, then returns a reader for the whole log in flash as text, oldest first.
 *
 * The reader starts at the beginning of an entry. If the oldest text in flash has been
 * overwritten, the reader leaves out the rest of the entry that it would start in (see
 * @ref idfxx_persistent_log_line).
 *
 * If the flush fails, this function still returns the reader. The reader then reads the log in
 * flash, without the lines that the flush could not write.
 *
 * @return The reader, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval other The flash error, if a read fails.
 */
[[nodiscard]] idfxx::result<text_reader> try_read_text();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Flushes, then copies the newest lines in flash into a buffer.
 *
 * Fills @p out with as many of the newest complete entries in flash as fit (see
 * @ref idfxx_persistent_log_line). @ref entries splits the copied text into entries.
 *
 * If the flush fails, this function still copies the newest lines in flash, without the lines
 * that the flush could not write.
 *
 * @param out The buffer.
 *
 * @return The copied text, as a view of the front of @p out.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started, or with
 *         the flash error if a read fails.
 */
[[nodiscard]] std::string_view copy_newest(std::span<char> out);
#endif

/**
 * @brief Flushes, then copies the newest lines in flash into a buffer.
 *
 * Fills @p out with as many of the newest complete entries in flash as fit (see
 * @ref idfxx_persistent_log_line). @ref entries splits the copied text into entries.
 *
 * If the flush fails, this function still copies the newest lines in flash, without the lines
 * that the flush could not write.
 *
 * @param out The buffer.
 *
 * @return The copied text, as a view of the front of @p out, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval other The flash error, if a read fails.
 */
[[nodiscard]] idfxx::result<std::string_view> try_copy_newest(std::span<char> out);

/**
 * @brief Copies the newest lines in RTC memory into a buffer.
 *
 * Fills @p out with as many of the newest complete entries in the RTC ring as fit, whether or not
 * they have been written to flash (see @ref idfxx_persistent_log_line). Works whether or not the
 * log is started, as long as RTC memory holds a ring. After a reset, before @ref start is called,
 * it returns the lines that led up to the reset. @ref entries splits the copied text into entries
 * (see the example there).
 *
 * @param out The buffer.
 *
 * @return The copied text, as a view of the front of @p out. It is empty when RTC memory holds no
 *         ring. RTC memory holds no ring after a power-on, or after a firmware update that changes
 *         `CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE`, until @ref start is called.
 */
[[nodiscard]] std::string_view copy_newest_in_memory(std::span<char> out) noexcept;

// =============================================================================
// Clearing
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Empties the log, both the lines in flash and the lines in RTC memory.
 *
 * The RTC ring is emptied whether or not its lines were written to flash, so that
 * @ref copy_newest_in_memory returns only lines added after the clear. The ring is emptied first,
 * so its lines that were not yet written to flash are dropped even if clearing flash then fails.
 * Readers created before the clear continue to the end that they had when they were created.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started, or with
 *         the flash error if the write fails.
 */
void clear();
#endif

/**
 * @brief Empties the log, both the lines in flash and the lines in RTC memory.
 *
 * The RTC ring is emptied whether or not its lines were written to flash, so that
 * @ref copy_newest_in_memory returns only lines added after the clear. The ring is emptied first,
 * so its lines that were not yet written to flash are dropped even if clearing flash then fails.
 * Readers created before the clear continue to the end that they had when they were created.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval other The flash error, if the write fails.
 */
[[nodiscard]] idfxx::result<void> try_clear();

// =============================================================================
// Faults
// =============================================================================

/// The maximum number of characters that `std::format` writes for one @ref fault. A buffer of
/// this size always holds the complete line.
inline constexpr size_t max_fault_line_size = 1536;

class fault;

/// @cond INTERNAL
namespace detail {

// The sizes of a fault's fixed fields. The sizes of strings include the terminating NUL.
inline constexpr size_t fault_kind_size = 24;
inline constexpr size_t fault_version_size = 32;
inline constexpr size_t fault_detail_size = 160;
inline constexpr size_t fault_task_size = 16;
// The number of leading bytes of the app ELF file's SHA-256 that a fault stores.
inline constexpr size_t fault_elf_sha256_size = 8;
// The most backtrace frames that a fault stores.
inline constexpr size_t max_fault_frames = 16;

// What a fault holds.
struct fault_fields {
    uint32_t seq = 0;
    uint32_t boot = 0;
    uint32_t wake = 0;
    std::optional<std::chrono::sys_seconds> wall;
    std::chrono::milliseconds uptime{0};
    uint32_t context_size = 0;
    bool damaged = false;
    // NUL-terminated.
    std::array<char, fault_kind_size> kind{};
    std::array<char, fault_version_size> version{};
    std::array<char, fault_detail_size> detail{};
    std::array<char, fault_task_size> task{};
    std::array<std::byte, fault_elf_sha256_size> elf_sha256{};
    uint32_t exception_pc = 0;
    // The first frame_count of frames are used.
    uint32_t frame_count = 0;
    std::array<uint32_t, max_fault_frames> frames{};
};

// Fills and reads a fault's fields, for the code that reads faults back.
struct fault_access;

// Writes a fault as one line, in pieces. Each piece is passed to emit, together with sink.
void format_fault(const fault& f, void (*emit)(void* sink, std::string_view piece), void* sink);

} // namespace detail
/// @endcond

/**
 * @headerfile <idfxx/persistent_log>
 * @brief A fault read back from the log: what it was, when and where it occurred, and how much
 *        of the log before it is stored with it.
 *
 * A fault holds its strings and frames in fixed-size fields, so it needs no allocation. A
 * default-constructed fault is empty: every number is 0 and every string is empty.
 *
 * When `CONFIG_IDFXX_STD_FORMAT` is enabled in menuconfig, `std::format` writes a fault as one
 * line of text, without a newline. It accepts only an empty format spec:
 * @code{.unparsed}
 * fault seq=12 kind=panic boot=4 wake=17 utc=2026-11-20T10:31:02Z uptime=1234 version="1.2.0"
 * detail="LoadProhibited" elf=3a5f09c2e1b04d77 task="main" pc=0x42001234
 * frames=0x42001234,0x42005678 context=5120
 * @endcode
 * In this line:
 * - `utc=none` is written when the fault has no wall-clock time, or one outside the years 0 to
 *   9999.
 * - `elf` is the stored bytes of the ELF file's SHA-256, in hex.
 * - `frames` is left out when there are no frames.
 * - ` damaged=1` is added at the end when the fault's context is damaged.
 * - The version, detail and task name are quoted. `"` and `\` are escaped by a backslash, and
 *   newlines, tabs and other control characters are written as `\n`, `\r`, `\t` or `\xHH`, so
 *   that the line can hold any text that these fields contain.
 *
 * The line is at most @ref max_fault_line_size characters.
 *
 * @code
 * std::array<char, idfxx::persistent_log::max_fault_line_size> line;
 * auto r = std::format_to_n(line.data(), line.size(), "{}", f);
 * send(std::string_view(line.data(), r.out));
 * @endcode
 */
class fault {
public:
    /** @brief Constructs an empty fault. */
    fault() = default;

    /** @brief Returns the fault's sequence number: one more than that of the previous fault. */
    [[nodiscard]] uint32_t sequence() const noexcept { return _f.seq; }

    /** @brief Returns the boot count when the fault occurred. */
    [[nodiscard]] uint32_t boot() const noexcept { return _f.boot; }

    /** @brief Returns the wake count within the boot when the fault occurred. */
    [[nodiscard]] uint32_t wake() const noexcept { return _f.wake; }

    /**
     * @brief Returns the wall-clock time when the fault occurred, to the second, or `std::nullopt`
     *        if the clock wasn't set.
     */
    [[nodiscard]] std::optional<std::chrono::sys_seconds> wall_time() const noexcept {
        return _f.wall;
    }

    /**
     * @brief Returns the time since the boot or the deep-sleep wake when the fault occurred, up to
     *        about 49 days.
     */
    [[nodiscard]] std::chrono::milliseconds uptime() const noexcept { return _f.uptime; }

    /** @brief Returns what kind of fault it was, such as `panic`. */
    [[nodiscard]] std::string_view kind() const noexcept { return _f.kind.data(); }

    /** @brief Returns the short description recorded with the fault; it may be empty. */
    [[nodiscard]] std::string_view detail() const noexcept { return _f.detail.data(); }

    /** @brief Returns the firmware version that recorded the fault; empty when not known. */
    [[nodiscard]] std::string_view version() const noexcept { return _f.version.data(); }

    /** @brief Returns the name of the task in which the fault occurred; empty when not known. */
    [[nodiscard]] std::string_view task() const noexcept { return _f.task.data(); }

    /**
     * @brief Returns the leading bytes of the SHA-256 of the ELF file of the firmware that
     *        recorded the fault, to match the fault to its build; zero when not known.
     */
    [[nodiscard]] idfxx::byte_view elf_sha256() const noexcept { return _f.elf_sha256; }

    /** @brief Returns the program counter at which the exception occurred; 0 when not known. */
    [[nodiscard]] uint32_t exception_pc() const noexcept { return _f.exception_pc; }

    /**
     * @brief Returns the backtrace frames, as program counters, innermost first; empty when not
     *        known.
     */
    [[nodiscard]] std::span<const uint32_t> frames() const noexcept {
        return std::span(_f.frames).first(
            _f.frame_count < _f.frames.size() ? _f.frame_count : _f.frames.size()
        );
    }

    /**
     * @brief Returns true when the context stored with the fault doesn't read back as it was
     *        written. A power cut partway through recording a later fault can cause this. The rest
     *        of the fault is as recorded, and the context is returned as it is read from storage.
     */
    [[nodiscard]] bool damaged() const noexcept { return _f.damaged; }

    /**
     * @brief Returns the number of bytes of the log before the fault that are stored with it. This
     *        is the most that reading its context returns. Reading returns less if the fault is
     *        overwritten while its context is being read.
     */
    [[nodiscard]] size_t context_size() const noexcept { return _f.context_size; }

private:
    /// @cond INTERNAL
    friend struct detail::fault_access;
    /// @endcond

    detail::fault_fields _f;
};

/**
 * @headerfile <idfxx/persistent_log>
 * @brief Steps through the stored faults, oldest first, reading each fault's header and then its
 *        context.
 *
 * A reader returned by @ref read_faults lists the faults that were stored when the reader was
 * created. A fault recorded later is not listed. The reader can be kept while faults are recorded
 * or cleared. After a clear, the reader still lists the faults that it started with, except a
 * fault that has been overwritten. Recording a fault and clearing the faults each write a record to
 * the next slot. Once every slot is in use, that slot holds the oldest fault, and the record
 * overwrites it. The reader leaves out a fault that was overwritten before the reader reached it.
 * Any task can call the reader. After the log is stopped, the reader reads nothing more.
 *
 * @ref next returns a fault's header after checking its context. @ref next_context then returns
 * the context one entry at a time, or @ref next_context_text returns it as text in chunks the size
 * of the caller's buffer.
 * Each part of the context is read only while the fault's slot still holds it. If recording a new
 * fault or clearing the faults overwrites the fault, its context ends at that point, before
 * reaching @ref fault::context_size bytes. When the context is read one entry at a time, the
 * reader reads more of the context into its buffer than it returns. So it can return entries that
 * it read before the fault was overwritten, and the last of them may be cut short.
 *
 * When `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled in menuconfig, the reader is also a range of
 * faults, and @ref context returns the current fault's context as a range of entries:
 * @code
 * auto faults = idfxx::persistent_log::read_faults();
 * for (const auto& f : faults) {
 *     send(std::format("{}\n", f));
 *     for (const auto& e : faults.context()) {
 *         send(std::format("{}\n", e));
 *     }
 * }
 * @endcode
 */
class fault_reader {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @headerfile <idfxx/persistent_log>
     * @brief A single-pass input iterator over the faults that a @ref fault_reader returns.
     *
     * The iterator refers to its reader, and the reader holds the current fault. The fault is
     * valid until the iterator is incremented, or until the reader is destroyed. An increment moves
     * to the next fault, whether or not the current fault's context was read. The iterator can be
     * moved but not copied.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     */
    class iterator {
    public:
        /** @brief The iterator's category: a single-pass input iterator. */
        using iterator_concept = std::input_iterator_tag;
        /** @brief The type of the faults. */
        using value_type = fault;
        /** @brief Distance type (required by the iterator concepts). */
        using difference_type = std::ptrdiff_t;

        iterator(const iterator&) = delete;
        iterator& operator=(const iterator&) = delete;

        /**
         * @brief Move constructor.
         *
         * @param other The iterator to move from.
         */
        iterator(iterator&& other) noexcept = default;

        /**
         * @brief Move assignment.
         *
         * @param other The iterator to move from.
         *
         * @return This iterator.
         */
        iterator& operator=(iterator&& other) noexcept = default;

        /**
         * @brief Returns the current fault.
         *
         * The fault is valid until the iterator is incremented, or until the reader is destroyed.
         */
        [[nodiscard]] const fault& operator*() const noexcept;

        /**
         * @brief Reads the next fault's header, and checks its context.
         *
         * @return This iterator, which is equal to `end()` once every fault has been returned.
         * @throws std::system_error as @ref fault_reader::next does. After it throws, every later
         *         call to the reader throws the same error.
         */
        iterator& operator++();

        /**
         * @brief Reads the next fault's header, and checks its context.
         *
         * @throws std::system_error as @ref fault_reader::next does. After it throws, every later
         *         call to the reader throws the same error.
         */
        void operator++(int) { ++*this; }

        /** @brief Returns true once every fault has been returned. */
        [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept {
            return _reader == nullptr;
        }

    private:
        friend class fault_reader;

        explicit iterator(fault_reader* reader) noexcept
            : _reader(reader) {}

        // The reader, or null at the end.
        fault_reader* _reader;
    };

    /**
     * @headerfile <idfxx/persistent_log>
     * @brief The entries of the context of the fault that a @ref fault_reader returned last, as a
     *        range.
     *
     * @ref fault_reader::context returns the range. The range refers to its reader, and reads the
     * entries as @ref fault_reader::next_context(entry&) does. The reader holds the current entry.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     */
    class context_range {
    public:
        /**
         * @headerfile <idfxx/persistent_log>
         * @brief A single-pass input iterator over the entries of a fault's context.
         *
         * The iterator refers to its reader, and the reader holds the current entry. The entry is
         * valid until the iterator is incremented, until the reader moves to the next fault, or
         * until the reader is destroyed. The iterator can be moved but not copied.
         */
        class iterator {
        public:
            /** @brief The iterator's category: a single-pass input iterator. */
            using iterator_concept = std::input_iterator_tag;
            /** @brief The type of the entries. */
            using value_type = entry;
            /** @brief Distance type (required by the iterator concepts). */
            using difference_type = std::ptrdiff_t;

            iterator(const iterator&) = delete;
            iterator& operator=(const iterator&) = delete;

            /**
             * @brief Move constructor.
             *
             * @param other The iterator to move from.
             */
            iterator(iterator&& other) noexcept = default;

            /**
             * @brief Move assignment.
             *
             * @param other The iterator to move from.
             *
             * @return This iterator.
             */
            iterator& operator=(iterator&& other) noexcept = default;

            /**
             * @brief Returns the current entry.
             *
             * The entry is valid until the iterator is incremented, until the reader moves to the
             * next fault, or until the reader is destroyed.
             */
            [[nodiscard]] const entry& operator*() const noexcept;

            /**
             * @brief Reads the next entry of the context.
             *
             * @return This iterator, which is equal to `end()` once the whole context has been
             *         returned.
             * @throws std::system_error as @ref fault_reader::next_context(entry&) does. After it
             *         throws, every later call that reads the context throws the same error, until
             *         the reader moves on to the next fault.
             */
            iterator& operator++();

            /**
             * @brief Reads the next entry of the context.
             *
             * @throws std::system_error as @ref fault_reader::next_context(entry&) does. After it
             *         throws, every later call that reads the context throws the same error, until
             *         the reader moves on to the next fault.
             */
            void operator++(int) { ++*this; }

            /** @brief Returns true once the whole context has been returned. */
            [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept {
                return _reader == nullptr;
            }

        private:
            friend class context_range;

            explicit iterator(fault_reader* reader) noexcept
                : _reader(reader) {}

            // The reader, or null at the end.
            fault_reader* _reader;
        };

        /**
         * @brief Reads the next entry of the context, and returns an iterator at it.
         *
         * The reader keeps its place in the context. A second call continues after the last entry
         * that the reader returned, through an iterator or through
         * @ref fault_reader::next_context(entry&). It doesn't start again from the first entry.
         *
         * @return An iterator at the entry, or an iterator equal to `end()` once the whole context
         *         has been returned, or when there is no fault (before the first fault, or after
         *         the last).
         * @throws std::system_error as @ref fault_reader::next_context(entry&) does. After it
         *         throws, every later call that reads the context throws the same error, until the
         *         reader moves on to the next fault.
         */
        [[nodiscard]] iterator begin();

        /**
         * @brief Returns the sentinel that an iterator equals once the whole context has been
         *        returned.
         *
         * @return The sentinel.
         */
        [[nodiscard]] std::default_sentinel_t end() const noexcept { return std::default_sentinel; }

    private:
        friend class fault_reader;

        explicit context_range(fault_reader* reader) noexcept
            : _reader(reader) {}

        fault_reader* _reader;
    };

    /**
     * @brief Reads the next fault's header, checks its context, and returns an iterator at the
     *        fault.
     *
     * The reader keeps its place in the list of faults. A second call continues after the last
     * fault that the reader returned, through an iterator or through @ref next, as a range over an
     * input stream does. It doesn't start again from the first fault.
     *
     * @return An iterator at the fault, or an iterator equal to `end()` once every fault has been
     *         returned.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error as @ref next does. After it throws, every later call to the reader
     *         throws the same error.
     */
    [[nodiscard]] iterator begin();

    /**
     * @brief Returns the sentinel that an iterator equals once every fault has been returned.
     *
     * @return The sentinel.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     */
    [[nodiscard]] std::default_sentinel_t end() const noexcept { return std::default_sentinel; }

    /**
     * @brief Returns the entries of the context of the fault that the reader returned last, as a
     *        range.
     *
     * The range reads the entries as @ref next_context(entry&) does, so read each fault's context
     * either through this range or through @ref next_context_text, not through both.
     * The range refers to the reader, so it is available only from a reader that is not a
     * temporary.
     *
     * @return The range.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     */
    [[nodiscard]] context_range context() & { return context_range(this); }

    /** @brief Not available on a temporary reader, because the range would refer to it. */
    context_range context() && = delete;

    /**
     * @brief Reads the next fault's header, and checks its context.
     *
     * A fault whose context doesn't read back as it was written is still returned, marked as
     * @ref fault::damaged. @ref next_context then returns its context from the start. After this
     * function fails, every later call to the reader fails with the same error.
     *
     * @param out Filled with the fault if there is one, and left unchanged otherwise.
     *
     * @return True if @p out was filled, or false once every fault has been returned.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_state` if the log has been stopped
     *         since the reader was made, or with the flash error if a read fails.
     */
    [[nodiscard]] bool next(fault& out) { return unwrap(try_next(out)); }
#endif

    /**
     * @brief Reads the next fault's header, and checks its context.
     *
     * A fault whose context doesn't read back as it was written is still returned, marked as
     * @ref fault::damaged. @ref try_next_context then returns its context from the start. After
     * this function fails, every later call to the reader fails with the same error.
     *
     * @param out Filled with the fault if there is one, and left unchanged otherwise.
     *
     * @return True if @p out was filled, or false once every fault has been returned. Or an error.
     * @retval idfxx::errc::invalid_state The log has been stopped since the reader was made.
     * @retval other The flash error, if a read fails.
     */
    [[nodiscard]] idfxx::result<bool> try_next(fault& out);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the next part of the context of the fault that @ref next returned last, as
     *        text. The context is the log before the fault.
     *
     * After this function fails, every later call that reads the context fails with the same
     * error, until @ref next moves on to the next fault.
     *
     * @param out The buffer to fill.
     *
     * @return The text put at the front of @p out, as a view of @p out. It is shorter than @p out
     *         only at the end of the context. It is empty once the whole context has been
     *         returned, once recording a new fault or clearing the faults has overwritten the
     *         fault, or when there is no fault (before the first call to @ref next, or after it
     *         returned false).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_state` if the log has been stopped
     *         since the reader was made, or with the flash error if a read fails.
     */
    [[nodiscard]] std::string_view next_context_text(std::span<char> out) {
        return unwrap(try_next_context_text(out));
    }
#endif

    /**
     * @brief Reads the next part of the context of the fault that @ref try_next returned last,
     *        as text. The context is the log before the fault.
     *
     * After this function fails, every later call that reads the context fails with the same
     * error, until @ref try_next moves on to the next fault.
     *
     * @param out The buffer to fill.
     *
     * @return The text put at the front of @p out, as a view of @p out. It is shorter than @p out
     *         only at the end of the context. It is empty once the whole context has been
     *         returned, once recording a new fault or clearing the faults has overwritten the
     *         fault, or when there is no fault (before the first call to @ref try_next, or after
     *         it returned false). Or an error.
     * @retval idfxx::errc::invalid_state The log has been stopped since the reader was made.
     * @retval other The flash error, if a read fails.
     */
    [[nodiscard]] idfxx::result<std::string_view> try_next_context_text(std::span<char> out);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the next entry of the context of the fault that @ref next returned last. The
     *        context is the log before the fault.
     *
     * The entry refers to text in a buffer that the reader allocates the first time that this
     * function is called while there is a context to read. The entry is valid until the next call
     * to the reader, or until the reader is destroyed. The buffer is the size of an
     * @ref entry_reader's buffer, and an entry that doesn't fit in it is returned as
     * @ref entry_reader describes.
     *
     * This function reads more of the context into its buffer than it returns.
     * @ref next_context_text continues after the text that this function has read, so it skips the
     * entries that are still in the buffer. Read each fault's context with one of the two
     * functions, not with both.
     *
     * If recording a new fault or clearing the faults overwrites the fault, the context ends at the
     * point that the reader had read up to. This function then returns the entries that are still
     * in its buffer, and the last of them may be cut short. After this function fails, every later
     * call that reads the context fails with the same error, until @ref next moves on to the next
     * fault.
     *
     * @param out Set to the entry if there is one, and left unchanged otherwise.
     *
     * @return True if @p out was set. False once the whole context has been returned, once the
     *         context has ended because a new fault overwrote the fault, or when there is no fault
     *         (before the first call to @ref next, or after it returned false).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `idfxx::errc::invalid_state` if the log has been stopped
     *         since the reader was made, or with the flash error if a read fails.
     */
    [[nodiscard]] bool next_context(entry& out) { return unwrap(try_next_context(out)); }
#endif

    /**
     * @brief Reads the next entry of the context of the fault that @ref try_next returned last.
     *        The context is the log before the fault.
     *
     * The entry refers to text in a buffer that the reader allocates the first time that this
     * function is called while there is a context to read. The entry is valid until the next call
     * to the reader, or until the reader is destroyed. The buffer is the size of an
     * @ref entry_reader's buffer, and an entry that doesn't fit in it is returned as
     * @ref entry_reader describes.
     *
     * This function reads more of the context into its buffer than it returns.
     * @ref try_next_context_text continues after the text that this function has read, so it
     * skips the entries that are still in the buffer. Read each fault's context with one of the
     * two functions, not with both.
     *
     * If recording a new fault or clearing the faults overwrites the fault, the context ends at the
     * point that the reader had read up to. This function then returns the entries that are still
     * in its buffer, and the last of them may be cut short. After this function fails, every later
     * call that reads the context fails with the same error, until @ref try_next moves on to the
     * next fault.
     *
     * @param out Set to the entry if there is one, and left unchanged otherwise.
     *
     * @return True if @p out was set. False once the whole context has been returned, once the
     *         context has ended because a new fault overwrote the fault, or when there is no fault
     *         (before the first call to @ref try_next, or after it returned false). Or an error.
     * @retval idfxx::errc::invalid_state The log has been stopped since the reader was made.
     * @retval other The flash error, if a read fails.
     */
    [[nodiscard]] idfxx::result<bool> try_next_context(entry& out);

    /** @brief Destroys the reader. The log is left as it is. */
    ~fault_reader();

    fault_reader(const fault_reader&) = delete;
    fault_reader& operator=(const fault_reader&) = delete;

    /**
     * @brief Move constructor. Transfers the reader's place in the log.
     *
     * A moved-from reader must not be used: any operation other than destruction or assignment
     * is undefined behavior.
     *
     * @param other The reader to move from.
     */
    fault_reader(fault_reader&& other) noexcept;

    /**
     * @brief Move assignment. Transfers the other reader's place in the log, replacing this
     *        reader's place.
     *
     * A moved-from reader must not be used: any operation other than destruction or assignment
     * is undefined behavior.
     *
     * @param other The reader to move from.
     *
     * @return This reader.
     */
    fault_reader& operator=(fault_reader&& other) noexcept;

private:
    friend idfxx::result<fault_reader> try_read_faults();

    /// @cond INTERNAL
    struct state;
    explicit fault_reader(std::unique_ptr<state> s) noexcept;
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    // Reads the next fault into the state's current fault. Returns false once every fault has been
    // returned.
    bool _advance();
    // Reads the next entry of the current fault's context into the state's current entry. Returns
    // false once the whole context has been returned.
    bool _advance_context();
#endif
    /// @endcond

    std::unique_ptr<state> _state;
};

/** @cond INTERNAL */
// The type of `suppress_repeats`. It selects the record_fault() overloads that suppress repeats.
// Like std::nullopt_t, it can only be constructed from a key, so that `{}` can't create one. On
// GCC, an explicit default constructor is still viable for `{}` (CWG 1228), which would make
// `record_fault(kind, {})` ambiguous with the detail.
struct suppress_repeats_t {
    enum class _key { token };
    explicit constexpr suppress_repeats_t(_key) noexcept {}
};
/** @endcond */

/**
 * @headerfile <idfxx/persistent_log>
 * @brief Tag for recording a fault and then suppressing repeats of its kind, as in
 *        `record_fault("sensor", "no reply", suppress_repeats)`.
 *
 * A fault that keeps happening, such as a card that won't mount, occurs again at every attempt.
 * If it were recorded each time, it would fill the fault slots with copies of itself. When it is
 * recorded with this tag, it uses one slot. Its repeats are logged but not recorded, until the app
 * lifts the suppression. The app should do that once the fault has cleared:
 * @code
 * namespace pl = idfxx::persistent_log;
 *
 * if (sensor_answers()) {
 *     pl::lift_suppression("sensor");
 * } else {
 *     pl::record_fault("sensor", "no reply", pl::suppress_repeats);
 * }
 * @endcode
 *
 * Suppression applies to a fault kind, and the detail has no effect on it. Once a fault is
 * recorded with this tag, its kind is suppressed, and the suppression is stored in RTC memory.
 * Every later fault of that kind is then a repeat and is only logged, whatever its detail and
 * whichever @ref record_fault overload records it, until @ref lift_suppression lifts the
 * suppression of the kind. To record different failure modes separately, give each mode its own
 * kind (such as `sd_mount` and `sd_read`).
 *
 * Up to `CONFIG_IDFXX_PERSISTENT_LOG_SUPPRESSED_FAULTS` kinds can be suppressed at once. When that
 * many kinds are suppressed, a new kind replaces the kind that was recorded longest ago, and the
 * next fault of the replaced kind is recorded again. Suppressed kinds survive deep sleep and a
 * reset. All of them are forgotten after a power cut, when a start finds that the RTC memory
 * holding them is invalid, or when @ref clear_faults is called.
 */
inline constexpr suppress_repeats_t suppress_repeats{suppress_repeats_t::_key::token};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Records a fault, with the newest lines of the log before it, unless its kind is
 *        suppressed.
 *
 * First adds the entry `E persistent_log: fault kind=<kind> <detail>` to the log. Then it stores
 * the newest complete entries that fit in a fault slot, ending with that entry, as the fault's
 * context. These entries come from flash and from the lines still waiting in RTC memory. The fault
 * holds:
 * - the kind and detail,
 * - the boot count, wake count and time, as in the entry's stamp,
 * - the firmware version and the leading bytes of its ELF file's SHA-256,
 * - the name of the calling task.
 *
 * If storing the fault fails, this function then adds the entry
 * `W persistent_log: fault not recorded kind=<kind>`.
 *
 * Once every slot is in use, each new fault overwrites the oldest fault.
 *
 * A fault of a suppressed kind (see @ref suppress_repeats) is a repeat, whatever its detail. A
 * repeat only adds the entry `W persistent_log: fault repeat kind=<kind> <detail>`, and doesn't
 * write a slot. This overload never suppresses a kind: recording a fault with it leaves the kind's
 * suppression unchanged.
 *
 * Blocks while the slot is erased and written, which takes about 100 ms at the default slot size.
 * It holds the log's lock during that time, so flushes, reads and clears wait for it to finish.
 * Logging a line doesn't wait. It uses up to about 4.7 KiB of the calling task's stack in
 * ESP-IDF's default debug build (`-Og`), or 3.1 KiB in a build optimized for size (`-Os`), as
 * measured on esp32s3. At `-Og`, that is more than ESP-IDF's main task has by default (3584
 * bytes). A repeat doesn't access flash.
 *
 * @param kind   The kind of fault: 1 to 23 printable ASCII characters without spaces, such as
 *               `sd_mount` or `connect_start`. Must not be `cleared`.
 * @param detail A short description. If it is longer than 159 bytes, it is shortened to that
 *               limit at a UTF-8 character boundary.
 *
 * @return True if the fault was recorded; false if its kind is suppressed.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @note If recording fails partway, the oldest fault may have been erased.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started,
 *         `idfxx::errc::not_supported` if it was started without fault slots
 *         (@ref config::faults), `idfxx::errc::invalid_arg` if @p kind is invalid, or with
 *         the flash error if a read, write or erase fails.
 */
bool record_fault(std::string_view kind, std::string_view detail = {});
#endif

/**
 * @brief Records a fault, with the newest lines of the log before it, unless its kind is
 *        suppressed.
 *
 * First adds the entry `E persistent_log: fault kind=<kind> <detail>` to the log. Then it stores
 * the newest complete entries that fit in a fault slot, ending with that entry, as the fault's
 * context. These entries come from flash and from the lines still waiting in RTC memory. The fault
 * holds:
 * - the kind and detail,
 * - the boot count, wake count and time, as in the entry's stamp,
 * - the firmware version and the leading bytes of its ELF file's SHA-256,
 * - the name of the calling task.
 *
 * If storing the fault fails, this function then adds the entry
 * `W persistent_log: fault not recorded kind=<kind>`.
 *
 * Once every slot is in use, each new fault overwrites the oldest fault.
 *
 * A fault of a suppressed kind (see @ref suppress_repeats) is a repeat, whatever its detail. A
 * repeat only adds the entry `W persistent_log: fault repeat kind=<kind> <detail>`, and doesn't
 * write a slot. This overload never suppresses a kind: recording a fault with it leaves the kind's
 * suppression unchanged.
 *
 * Blocks while the slot is erased and written, which takes about 100 ms at the default slot size.
 * It holds the log's lock during that time, so flushes, reads and clears wait for it to finish.
 * Logging a line doesn't wait. It uses up to about 4.7 KiB of the calling task's stack in
 * ESP-IDF's default debug build (`-Og`), or 3.1 KiB in a build optimized for size (`-Os`), as
 * measured on esp32s3. At `-Og`, that is more than ESP-IDF's main task has by default (3584
 * bytes). A repeat doesn't access flash.
 *
 * @param kind   The kind of fault: 1 to 23 printable ASCII characters without spaces, such as
 *               `sd_mount` or `connect_start`. Must not be `cleared`.
 * @param detail A short description. If it is longer than 159 bytes, it is shortened to that
 *               limit at a UTF-8 character boundary.
 *
 * @return True if the fault was recorded; false if its kind is suppressed. Or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval idfxx::errc::not_supported The log was started without fault slots
 *         (@ref config::faults).
 * @retval idfxx::errc::invalid_arg @p kind is invalid.
 * @retval other The flash error, if a read, write or erase fails.
 * @note If recording fails partway, the oldest fault may have been erased.
 */
[[nodiscard]] idfxx::result<bool>
try_record_fault(std::string_view kind, std::string_view detail = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Records a fault unless its kind is suppressed, and then suppresses its kind.
 *
 * A fault of a suppressed kind is a repeat, whatever its detail. A repeat only adds the entry
 * `W persistent_log: fault repeat kind=<kind> <detail>`, and doesn't write a slot. Otherwise, the
 * fault is recorded as by the overload without the tag, and then its kind is suppressed (see
 * @ref suppress_repeats). Every later fault of that kind is then a repeat, whatever its detail and
 * whichever @ref record_fault overload records it, until @ref lift_suppression lifts the
 * suppression of the kind. The detail has no effect on the suppression.
 *
 * When it records a fault, it blocks and uses stack as the overload without the tag does. A
 * repeat doesn't access flash.
 *
 * @param kind   The kind of fault: 1 to 23 printable ASCII characters without spaces, such as
 *               `sd_mount` or `connect_start`. Must not be `cleared`.
 * @param detail A short description. If it is longer than 159 bytes, it is shortened to that
 *               limit at a UTF-8 character boundary.
 * @param tag    The @ref suppress_repeats tag.
 *
 * @return True if the fault was recorded; false if its kind is suppressed.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @note If recording fails partway, the oldest fault may have been erased, and the kind is left
 *       unsuppressed.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started,
 *         `idfxx::errc::not_supported` if it was started without fault slots
 *         (@ref config::faults), `idfxx::errc::invalid_arg` if @p kind is invalid, or with
 *         the flash error if a read, write or erase fails.
 */
bool record_fault(std::string_view kind, std::string_view detail, suppress_repeats_t tag);
#endif

/**
 * @brief Records a fault unless its kind is suppressed, and then suppresses its kind.
 *
 * A fault of a suppressed kind is a repeat, whatever its detail. A repeat only adds the entry
 * `W persistent_log: fault repeat kind=<kind> <detail>`, and doesn't write a slot. Otherwise, the
 * fault is recorded as by the overload without the tag, and then its kind is suppressed (see
 * @ref suppress_repeats). Every later fault of that kind is then a repeat, whatever its detail and
 * whichever @ref try_record_fault overload records it, until @ref lift_suppression lifts the
 * suppression of the kind. The detail has no effect on the suppression.
 *
 * When it records a fault, it blocks and uses stack as the overload without the tag does. A
 * repeat doesn't access flash.
 *
 * @param kind   The kind of fault: 1 to 23 printable ASCII characters without spaces, such as
 *               `sd_mount` or `connect_start`. Must not be `cleared`.
 * @param detail A short description. If it is longer than 159 bytes, it is shortened to that
 *               limit at a UTF-8 character boundary.
 * @param tag    The @ref suppress_repeats tag.
 *
 * @return True if the fault was recorded; false if its kind is suppressed. Or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval idfxx::errc::not_supported The log was started without fault slots
 *         (@ref config::faults).
 * @retval idfxx::errc::invalid_arg @p kind is invalid.
 * @retval other The flash error, if a read, write or erase fails.
 * @note If recording fails partway, the oldest fault may have been erased, and the kind is left
 *       unsuppressed.
 */
[[nodiscard]] idfxx::result<bool>
try_record_fault(std::string_view kind, std::string_view detail, suppress_repeats_t tag);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Records a fault with no detail unless its kind is suppressed, and then suppresses its
 *        kind.
 *
 * The same as the tagged overload with an empty detail.
 *
 * @param kind The kind of fault: 1 to 23 printable ASCII characters without spaces, such as
 *             `sd_mount` or `connect_start`. Must not be `cleared`.
 * @param tag  The @ref suppress_repeats tag.
 *
 * @return True if the fault was recorded; false if its kind is suppressed.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @note When it records a fault, it blocks and uses stack as the overload without the tag does.
 * @note If recording fails partway, the oldest fault may have been erased, and the kind is left
 *       unsuppressed.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started,
 *         `idfxx::errc::not_supported` if it was started without fault slots
 *         (@ref config::faults), `idfxx::errc::invalid_arg` if @p kind is invalid, or with
 *         the flash error if a read, write or erase fails.
 */
bool record_fault(std::string_view kind, suppress_repeats_t tag);
#endif

/**
 * @brief Records a fault with no detail unless its kind is suppressed, and then suppresses its
 *        kind.
 *
 * The same as the tagged overload with an empty detail.
 *
 * @param kind The kind of fault: 1 to 23 printable ASCII characters without spaces, such as
 *             `sd_mount` or `connect_start`. Must not be `cleared`.
 * @param tag  The @ref suppress_repeats tag.
 *
 * @return True if the fault was recorded; false if its kind is suppressed. Or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval idfxx::errc::not_supported The log was started without fault slots
 *         (@ref config::faults).
 * @retval idfxx::errc::invalid_arg @p kind is invalid.
 * @retval other The flash error, if a read, write or erase fails.
 * @note When it records a fault, it blocks and uses stack as the overload without the tag does.
 * @note If recording fails partway, the oldest fault may have been erased, and the kind is left
 *       unsuppressed.
 */
[[nodiscard]] idfxx::result<bool> try_record_fault(std::string_view kind, suppress_repeats_t tag);

/**
 * @brief Lifts the suppression of a fault kind, so that its next fault is recorded again.
 *
 * If recording a fault with @ref suppress_repeats suppressed the kind, this function forgets the
 * kind, and then adds the entry `I persistent_log: fault suppression lifted kind=<kind>` to the
 * log. It doesn't access flash. When the kind isn't suppressed, it returns immediately without
 * taking the log's lock, so it can be called after every success. When the kind is suppressed, it
 * takes the log's lock to forget the kind, so it may wait for a flush or a fault recording in
 * progress, for up to about the time of one slot erase. Does nothing before @ref start, or when
 * called from an interrupt.
 *
 * @param kind The kind of fault.
 */
void lift_suppression(std::string_view kind) noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Returns a reader for the stored faults, oldest first.
 *
 * @return The reader.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started, or
 *         `idfxx::errc::not_supported` if it was started without fault slots
 *         (@ref config::faults).
 */
[[nodiscard]] fault_reader read_faults();
#endif

/**
 * @brief Returns a reader for the stored faults, oldest first.
 *
 * @return The reader, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval idfxx::errc::not_supported The log was started without fault slots
 *         (@ref config::faults).
 */
[[nodiscard]] idfxx::result<fault_reader> try_read_faults();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Forgets every stored fault, and every suppressed fault kind (see
 *        @ref suppress_repeats).
 *
 * Forgetting the faults takes as long as recording one fault. The suppressed kinds are forgotten
 * first, so they are forgotten even if clearing the slots then fails. A reader created before the
 * clear still lists the faults that it started with, except a fault that is overwritten before the
 * reader reaches it. The clear writes a record to the next slot, and so does each fault recorded
 * later. Once every slot is in use, the next slot holds the oldest fault, which the record
 * overwrites.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started,
 *         `idfxx::errc::not_supported` if it was started without fault slots
 *         (@ref config::faults), or with the flash error if a read, write or erase fails.
 */
void clear_faults();
#endif

/**
 * @brief Forgets every stored fault, and every suppressed fault kind (see
 *        @ref suppress_repeats).
 *
 * Forgetting the faults takes as long as recording one fault. The suppressed kinds are forgotten
 * first, so they are forgotten even if clearing the slots then fails. A reader created before the
 * clear still lists the faults that it started with, except a fault that is overwritten before the
 * reader reaches it. The clear writes a record to the next slot, and so does each fault recorded
 * later. Once every slot is in use, the next slot holds the oldest fault, which the record
 * overwrites.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval idfxx::errc::not_supported The log was started without fault slots
 *         (@ref config::faults).
 * @retval other The flash error, if a read, write or erase fails.
 */
[[nodiscard]] idfxx::result<void> try_clear_faults();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Counts the stored faults: the faults that a reader created now would list.
 *
 * Reads only each slot's header.
 *
 * @return The count.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with `idfxx::errc::invalid_state` if the log isn't started,
 *         `idfxx::errc::not_supported` if it was started without fault slots
 *         (@ref config::faults), or with the flash error if a read fails.
 */
[[nodiscard]] size_t fault_count();
#endif

/**
 * @brief Counts the stored faults: the faults that a reader created now would list.
 *
 * Reads only each slot's header.
 *
 * @return The count, or an error.
 * @retval idfxx::errc::invalid_state The log isn't started.
 * @retval idfxx::errc::not_supported The log was started without fault slots
 *         (@ref config::faults).
 * @retval other The flash error, if a read fails.
 */
[[nodiscard]] idfxx::result<size_t> try_fault_count();

/** @cond INTERNAL */
inline idfxx::result<bool> try_record_fault(std::string_view kind, suppress_repeats_t tag) {
    return try_record_fault(kind, {}, tag);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
inline void stop() {
    unwrap(try_stop());
}

inline void flush() {
    unwrap(try_flush());
}

inline void flush_if_pending(size_t threshold) {
    unwrap(try_flush_if_pending(threshold));
}

inline entry_reader read() {
    return unwrap(try_read());
}

inline entry_reader read_newest(size_t count) {
    return unwrap(try_read_newest(count));
}

inline text_reader read_text() {
    return unwrap(try_read_text());
}

inline std::string_view copy_newest(std::span<char> out) {
    return unwrap(try_copy_newest(out));
}

inline void clear() {
    unwrap(try_clear());
}

inline bool record_fault(std::string_view kind, std::string_view detail) {
    return unwrap(try_record_fault(kind, detail));
}

inline bool record_fault(std::string_view kind, std::string_view detail, suppress_repeats_t tag) {
    return unwrap(try_record_fault(kind, detail, tag));
}

inline bool record_fault(std::string_view kind, suppress_repeats_t tag) {
    return record_fault(kind, {}, tag);
}

inline fault_reader read_faults() {
    return unwrap(try_read_faults());
}

inline void clear_faults() {
    unwrap(try_clear_faults());
}

inline size_t fault_count() {
    return unwrap(try_fault_count());
}
#endif

static_assert(std::ranges::view<entries> && std::ranges::forward_range<entries>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::ranges::input_range<entry_reader>);
static_assert(std::ranges::input_range<fault_reader>);
static_assert(std::ranges::input_range<fault_reader::context_range>);
#endif
/** @endcond */

} // namespace idfxx::persistent_log

/** @cond INTERNAL */
template<>
inline constexpr bool std::ranges::enable_borrowed_range<idfxx::persistent_log::entries> = true;
/** @endcond */

/** @} */

#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <cstdlib>
#include <format>
namespace std {
template<>
struct formatter<idfxx::persistent_log::entry> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            _reject("idfxx::persistent_log::entry accepts only an empty format spec");
        }
        return it;
    }

    template<typename FormatContext>
    auto format(const idfxx::persistent_log::entry& e, FormatContext& ctx) const {
        const std::string_view stored = idfxx::persistent_log::detail::entry_access::stored(e);
        return std::copy(stored.begin(), stored.end(), ctx.out());
    }

private:
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

template<>
struct formatter<idfxx::persistent_log::fault> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            _reject("idfxx::persistent_log::fault accepts only an empty format spec");
        }
        return it;
    }

    template<typename FormatContext>
    auto format(const idfxx::persistent_log::fault& f, FormatContext& ctx) const {
        auto out = ctx.out();
        idfxx::persistent_log::detail::format_fault(
            f,
            [](void* sink, std::string_view piece) {
                auto& o = *static_cast<decltype(out)*>(sink);
                o = std::copy(piece.begin(), piece.end(), o);
            },
            &out
        );
        return out;
    }

private:
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
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
