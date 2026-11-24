// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// How start finds out about the reset before it and about the core dump in flash. Start calls
// through a table of functions. The table holds ESP-IDF's functions unless the tests replace them,
// which lets the tests supply any reset and any dump.

#include <idfxx/error>
#include <idfxx/system>

#include <array>
#include <cstddef>
#include <cstdint>
#include <esp_err.h>
#include <span>

namespace idfxx::persistent_log::internal {

/// The parts of a core dump's summary that start uses. The fields for the architecture that the
/// dump came from are filled in, and the other architecture's fields are 0.
struct dump_summary {
    enum class arch : uint8_t {
        xtensa,
        riscv,
    };

    arch kind = arch::xtensa;
    /// The task in which the exception occurred, NUL-terminated.
    std::array<char, 16> task{};
    /// The program counter at which the exception occurred.
    uint32_t pc = 0;
    /// Xtensa: the backtrace, of `depth` frames. A depth larger than the array holds is clamped.
    std::array<uint32_t, 16> backtrace{};
    uint32_t depth = 0;
    /// Xtensa: the exception's cause and virtual address.
    uint32_t exc_cause = 0;
    uint32_t exc_vaddr = 0;
    /// RISC-V: the return address, and the trap's cause and value.
    uint32_t ra = 0;
    uint32_t mcause = 0;
    uint32_t mtval = 0;
    /// The SHA-256 of the crashing app's ELF file, in hex and NUL-terminated. It holds as many
    /// characters as the dump stores.
    std::array<char, 65> elf_sha256{};
};

/// The functions that start uses to read the reset and the core dump.
struct reset_source {
    /// The reason for the last reset.
    idfxx::reset_reason (*reason)();
    /// Checks whether flash holds a complete core dump. Returns ESP_OK if it does.
    esp_err_t (*image_check)();
    /// The CRC-32 of the entire core dump image as stored, read through its partition.
    idfxx::result<uint32_t> (*image_crc)();
    /// Fills `out` with the dump's summary. Returns false when there is no summary.
    bool (*summary)(dump_summary& out);
    /// Copies the dump's panic reason (from an abort or the task watchdog) into `out`,
    /// NUL-terminated and shortened to fit. Returns the reason's length, or 0 when there is no
    /// panic reason.
    size_t (*panic_reason)(std::span<char> out);
};

/// The source that uses ESP-IDF. It reads the reset reason, and the core dump when the app stores
/// one in flash in ELF form (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH). When the app doesn't,
/// image_check returns ESP_ERR_NOT_SUPPORTED.
reset_source esp_reset_source() noexcept;

/// The source that start uses. It is esp_reset_source() until a test changes it.
reset_source& reset_hooks() noexcept;

/// Start takes the reset into account once per boot: it counts the wake, and it handles the reset.
/// This function makes the next start do both again, as it would after a reset. The tests call it
/// between starts.
void rearm_reset() noexcept;

} // namespace idfxx::persistent_log::internal
