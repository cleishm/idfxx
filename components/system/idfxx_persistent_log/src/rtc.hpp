// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// The state that the service keeps in RTC memory. The tests can reach it, so that they can leave it
// as a reset or a power-on would.

#include "fault_memory.hpp"
#include "line_ring.hpp"
#include "sdkconfig.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <esp_rom_crc.h>
#include <type_traits>

namespace idfxx::persistent_log::internal {

/// The ring that holds the lines waiting to be written to flash.
using ring_type = line_ring<CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE>;

/// The service's ring in RTC memory. A change to it races with the log's use of it, unless nothing
/// else adds lines during the change. The tests change it while the log is stopped, or while
/// capture is off.
ring_type& rtc_ring() noexcept;

// The state, other than the ring, that survives deep sleep and a reset. It is held in records that
// are each sealed by a magic and a CRC, so that a reset in the middle of a change to one record
// leaves the others valid. The records hold only words, so that the CRC covers no padding. The
// magic and the CRC distinguish a record from what the memory holds after a power-on, or after an
// update that lays the memory out differently.

/// The hint for flash_log::open, which saves the next open from reading every sector's header. The
/// record also holds the geometry (sector size and size) of the log range that the hint was taken
/// on. The hint is used only on a range of the same geometry, and flash_log checks it against what
/// the range holds.
struct hint_record {
    static constexpr uint32_t record_magic = 0x31484c50; // "PLH1"

    uint32_t magic;
    uint32_t has_hint;
    uint32_t sector_size;
    uint32_t size;
    uint32_t sector;
    uint32_t offset;
    uint32_t seq;
    // The CRC of the words before it.
    uint32_t crc;
};

/// The wake count.
struct wake_record {
    static constexpr uint32_t record_magic = 0x31574c50; // "PLW1"

    uint32_t magic;
    // The number of wakes from deep sleep since the last reset of another kind.
    uint32_t wake;
    // The CRC of the words before it.
    uint32_t crc;
};

/// The state for the fault layer. It holds the CRC of the image of the last core dump that was
/// taken, and the memory of the faults whose repeats are suppressed, with room for
/// CONFIG_IDFXX_PERSISTENT_LOG_SUPPRESSED_FAULTS kinds.
///
/// The CRC is one of two notes of the last dump taken. The fault store's entries in flash hold the
/// other, which is kept across a power cycle. A fault that is suppressed as a repeat writes nothing
/// to flash, so a dump taken only by a suppressed repeat is noted only here.
struct fault_record {
    static constexpr uint32_t record_magic = 0x31464c50; // "PLF1"

    uint32_t magic;
    uint32_t has_core_dump_crc;
    uint32_t core_dump_crc;
    fault_memory<CONFIG_IDFXX_PERSISTENT_LOG_SUPPRESSED_FAULTS> suppressed;
    // The CRC of the words before it.
    uint32_t crc;
};

/// The records, in RTC memory.
struct rtc_state {
    hint_record hint;
    wake_record wake;
    fault_record faults;
};

// A dynamic initializer would wipe them on every boot.
static_assert(std::is_trivially_default_constructible_v<rtc_state>);
static_assert(std::has_unique_object_representations_v<hint_record>);
static_assert(std::has_unique_object_representations_v<wake_record>);
static_assert(std::has_unique_object_representations_v<fault_record>);
static_assert(check_fault_memory<CONFIG_IDFXX_PERSISTENT_LOG_SUPPRESSED_FAULTS>());

/// The CRC of a record's words before its `crc`.
template<typename Record>
uint32_t record_crc(const Record& r) noexcept {
    return esp_rom_crc32_le(
        0, reinterpret_cast<const uint8_t*>(&r), static_cast<uint32_t>(offsetof(Record, crc))
    );
}

/// Seals a record with its current contents.
template<typename Record>
void seal(Record& r) noexcept {
    r.magic = Record::record_magic;
    r.crc = record_crc(r);
}

template<typename Record>
bool sealed(const Record& r) noexcept {
    return r.magic == Record::record_magic && r.crc == record_crc(r);
}

/// Whether a record holds only what sealing it could have left.
inline bool valid(const hint_record& r) noexcept {
    return sealed(r) && r.has_hint <= 1;
}

inline bool valid(const wake_record& r) noexcept {
    return sealed(r);
}

inline bool valid(const fault_record& r) noexcept {
    return sealed(r) && r.has_core_dump_crc <= 1;
}

/// The service's records in RTC memory. The tests change them while the log is stopped.
rtc_state& rtc() noexcept;

/// Checks each record separately, and zeroes and seals any record that isn't valid. Then it counts
/// the wake. After a wake from deep sleep, the count is one more than before. Otherwise, it is 0.
/// Start runs it once per boot, and the tests run it as a boot would.
///
/// @param deep_sleep_wake Whether the boot is a wake from deep sleep.
void check_rtc(bool deep_sleep_wake) noexcept;

} // namespace idfxx::persistent_log::internal
