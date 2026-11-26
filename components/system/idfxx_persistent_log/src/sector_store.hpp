// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @file sector_store.hpp
 * @brief The block size that the log writes in, and a sector store in RAM that follows NOR flash
 *        rules.
 *
 * @defgroup idfxx_persistent_log_sector_store Sector store
 * @brief The flash that the log is written to, behind an interface that tests can replace.
 *
 * @ref idfxx::persistent_log::sector_store and @ref idfxx::persistent_log::partition_sector_store
 * are public, in `<idfxx/persistent_log>`. @ref idfxx::persistent_log::ram_sector_store is for
 * tests. It keeps the bytes in RAM and follows the same rules as flash. It can stop in the middle
 * of a write, as a power cut would, and it can behave as an encrypted partition does.
 * @{
 */

#include "idfxx/persistent_log.hpp"
#include "sdkconfig.h"

#include <idfxx/byte_view>
#include <idfxx/error>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace idfxx::persistent_log {

/// The unit that the log and the fault store write in. Every write that they make starts at a
/// multiple of this size and is a whole number of blocks long, as an encrypted partition requires.
inline constexpr size_t block_size = 16;

/**
 * @brief A sector store in RAM with NOR flash rules, for tests.
 *
 * The store starts erased. A write stores the AND of the old and new bytes, as flash does. The
 * store counts every byte that a write stores over a byte that wasn't `0xFF` (@ref overwrites). So
 * a missing erase, or a second write to the same bytes, shows up in that count.
 *
 * @ref cut_after simulates a power cut. It takes a number of bytes, counted across every write from
 * then on. Once that many bytes have been written, the write in progress stops there and fails.
 * Every read, write and erase then fails until @ref restore_power. An erase that comes after the
 * cut fails without changing anything. An erase that comes exactly at the cut, when no bytes are
 * left, is also cut, and it erases only the second half of the sector.
 *
 * A store made with @ref encrypted behaves as an encrypted partition. Its @ref write_size is 16.
 * The stored bytes are the written bytes XORed with a keystream that depends on their address. So
 * @ref try_read returns noise for erased bytes rather than `0xFF`, and only @ref try_read_raw shows
 * them as erased. A power cut, or a write over bytes that weren't erased, can leave a 16-byte block
 * partly written. Such a block reads back as noise across all 16 bytes, as a block cipher's output
 * would, until it is erased. Writing the same bytes to a block a second time stores the same bytes
 * and changes nothing.
 */
class ram_sector_store final : public sector_store {
public:
    /// The type of @ref encrypted.
    struct encrypted_t {
        explicit encrypted_t() = default;
    };

    /// Selects the constructor that makes a store behaving as an encrypted partition.
    static const encrypted_t encrypted;

    /**
     * @brief Makes an erased store.
     *
     * @param sectors     The number of sectors.
     * @param sector_size The size of each sector in bytes.
     */
    explicit ram_sector_store(size_t sectors, size_t sector_size = 4096);

    /**
     * @brief Makes an erased store that behaves as an encrypted partition.
     *
     * @code
     * idfxx::persistent_log::ram_sector_store store(
     *     idfxx::persistent_log::ram_sector_store::encrypted, 8, 256
     * );
     * @endcode
     *
     * @param sectors     The number of sectors.
     * @param sector_size The size of each sector in bytes. It must be a multiple of 16.
     */
    ram_sector_store(encrypted_t, size_t sectors, size_t sector_size = 4096);

    /**
     * @brief Returns the stored bytes, so that a test can check or damage them directly.
     *
     * For an encrypted store, these are the encrypted bytes.
     */
    [[nodiscard]] std::span<std::byte> bytes() { return _bytes; }

    /**
     * @brief Cuts the power once @p budget more bytes have been written.
     *
     * With a budget of 0, the next write stores no bytes, and the next erase erases only the
     * second half of its sector.
     */
    void cut_after(size_t budget);

    /** @brief Ends a power cut, or disarms a power cut that hasn't been reached yet. */
    void restore_power();

    /** @brief Returns true when a power cut has happened and power isn't restored. */
    [[nodiscard]] bool cut() const { return _cut; }

    /** @brief Returns true when the last power cut came during an erase. */
    [[nodiscard]] bool cut_erasing() const { return _cut_erasing; }

    /** @brief Returns the number of bytes written over a byte that wasn't `0xFF`. */
    [[nodiscard]] size_t overwrites() const { return _overwrites; }

    /**
     * @brief Returns the number of reads, raw or not, since the store was made or since the last
     *        call to @ref reset_counts.
     */
    [[nodiscard]] size_t reads() const { return _reads; }

    /**
     * @brief Returns the number of erases since the store was made or since the last call to
     *        @ref reset_counts.
     */
    [[nodiscard]] size_t erases() const { return _erases; }

    /** @brief Sets the overwrite, read and erase counts to zero. */
    void reset_counts();

private:
    [[nodiscard]] idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override;
    [[nodiscard]] idfxx::result<void>
    do_read_raw(size_t offset, idfxx::byte_span dst) const override;
    [[nodiscard]] idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override;
    [[nodiscard]] idfxx::result<void> do_erase_sector(size_t index) override;

    void _erase(size_t from, size_t length);

    std::vector<std::byte> _bytes;
    // For an encrypted store, one flag per 16-byte block. A flag is true when its block reads as
    // noise.
    std::vector<bool> _torn;
    bool _encrypted = false;
    bool _armed = false;
    size_t _budget = 0;
    bool _cut = false;
    bool _cut_erasing = false;
    size_t _overwrites = 0;
    mutable size_t _reads = 0;
    size_t _erases = 0;
};

inline constexpr ram_sector_store::encrypted_t ram_sector_store::encrypted{};

} // namespace idfxx::persistent_log

/** @} */
