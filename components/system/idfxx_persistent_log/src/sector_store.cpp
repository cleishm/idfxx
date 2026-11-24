// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "sector_store.hpp"

#include <algorithm>
#include <array>

namespace idfxx::persistent_log {

namespace {

constexpr std::byte erased{0xFF};

// The size of the block that an encrypted RAM store turns entirely to noise when the block is
// partly written.
constexpr size_t cipher_block = 16;

// True when [offset, offset + length) lies within [0, size), without overflowing.
bool within(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}

// Mixes a byte's address with a salt. The RAM store uses it for its keystream and its noise.
uint8_t mix(size_t at, uint32_t salt) {
    uint32_t x = static_cast<uint32_t>(at / cipher_block) * 0x9E3779B1U +
        static_cast<uint32_t>(at % cipher_block) * 0x85EBCA6BU + salt;
    x ^= x >> 15;
    x *= 0x2C1B3C6DU;
    x ^= x >> 12;
    x *= 0x297A2D39U;
    x ^= x >> 15;
    auto b = static_cast<uint8_t>(x);
    // The result is never 0. So an erased byte never decrypts to 0xFF, and a byte of a torn block
    // never reads as its decrypted value.
    return b == 0 ? 0xA5 : b;
}

// The keystream byte that an encrypted RAM store XORs with the byte at an address.
std::byte keystream(size_t at) {
    return std::byte{mix(at, 0x6A09E667U)};
}

// The noise that a read XORs into a byte of a torn block, after decrypting the byte.
std::byte noise(size_t at) {
    return std::byte{mix(at, 0xBB67AE85U)};
}

bool all_erased(std::span<const std::byte> bytes) {
    return std::ranges::all_of(bytes, [](std::byte b) { return b == erased; });
}

} // namespace

// =============================================================================
// sector_store
// =============================================================================

idfxx::result<void> sector_store::try_read(size_t offset, idfxx::byte_span dst) const {
    if (!within(offset, dst.size(), size())) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    return do_read(offset, dst);
}

idfxx::result<void> sector_store::try_read_raw(size_t offset, idfxx::byte_span dst) const {
    if (!within(offset, dst.size(), size())) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    return do_read_raw(offset, dst);
}

idfxx::result<void> sector_store::try_write(size_t offset, idfxx::byte_view src) {
    size_t unit = write_size();
    if (!within(offset, src.size(), size()) || unit == 0 || offset % unit != 0 ||
        src.size() % unit != 0) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    return do_write(offset, src);
}

idfxx::result<void> sector_store::try_erase_sector(size_t index) {
    if (index >= sector_count()) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    return do_erase_sector(index);
}

// =============================================================================
// partition_sector_store
// =============================================================================

idfxx::result<partition_sector_store> partition_sector_store::make(const idfxx::partition& part) {
    return make(part, 0, part.size());
}

idfxx::result<partition_sector_store>
partition_sector_store::make(const idfxx::partition& part, size_t offset, size_t length) {
    size_t unit = part.erase_size();
    if (unit == 0 || length == 0 || offset % unit != 0 || length % unit != 0 ||
        !within(offset, length, part.size())) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    return partition_sector_store(checked{}, part, offset, length);
}

idfxx::result<void> partition_sector_store::do_read(size_t offset, idfxx::byte_span dst) const {
    return _part.try_read(_offset + offset, idfxx::byte_span(dst.data(), dst.size()));
}

idfxx::result<void> partition_sector_store::do_read_raw(size_t offset, idfxx::byte_span dst) const {
    return _part.try_read_raw(_offset + offset, idfxx::byte_span(dst.data(), dst.size()));
}

idfxx::result<void> partition_sector_store::do_write(size_t offset, idfxx::byte_view src) {
    return _part.try_write(_offset + offset, idfxx::byte_view(src.data(), src.size()));
}

idfxx::result<void> partition_sector_store::do_erase_sector(size_t index) {
    size_t unit = sector_size();
    return _part.try_erase_range(_offset + index * unit, unit);
}

// =============================================================================
// ram_sector_store
// =============================================================================

ram_sector_store::ram_sector_store(size_t sectors, size_t sector_size)
    : sector_store(sectors * sector_size, sector_size, 1)
    , _bytes(sectors * sector_size, erased) {}

ram_sector_store::ram_sector_store(encrypted_t, size_t sectors, size_t sector_size)
    : sector_store(sectors * sector_size, sector_size, cipher_block)
    , _bytes(sectors * sector_size, erased)
    , _torn((sectors * sector_size + cipher_block - 1) / cipher_block, false)
    , _encrypted(true) {}

idfxx::result<void> ram_sector_store::do_read(size_t offset, idfxx::byte_span dst) const {
    if (!_encrypted) {
        return do_read_raw(offset, dst);
    }
    if (_cut) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    ++_reads;
    for (size_t i = 0; i < dst.size(); ++i) {
        size_t at = offset + i;
        std::byte b = _bytes[at] ^ keystream(at);
        if (_torn[at / cipher_block]) {
            b ^= noise(at);
        }
        dst[i] = b;
    }
    return {};
}

idfxx::result<void> ram_sector_store::do_read_raw(size_t offset, idfxx::byte_span dst) const {
    if (_cut) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    ++_reads;
    std::copy_n(_bytes.begin() + offset, dst.size(), dst.begin());
    return {};
}

idfxx::result<void> ram_sector_store::do_write(size_t offset, idfxx::byte_view src) {
    if (_cut) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    size_t landed = src.size();
    if (_armed && landed > _budget) {
        landed = _budget;
    }
    if (!_encrypted) {
        for (size_t i = 0; i < landed; ++i) {
            std::byte& b = _bytes[offset + i];
            if (b != erased) {
                ++_overwrites;
            }
            b &= src[i];
        }
    } else {
        // The write is aligned, so it is applied one block at a time. Each block is written whole,
        // except that the block at the cut may be written in part.
        for (size_t done = 0; done < landed; done += cipher_block) {
            size_t at = offset + done;
            size_t n = std::min(cipher_block, landed - done);
            std::array<std::byte, cipher_block> cipher;
            for (size_t i = 0; i < cipher_block; ++i) {
                cipher[i] = src[done + i] ^ keystream(at + i);
            }
            auto stored = std::span(_bytes).subspan(at, cipher_block);
            // Writing the same bytes again changes nothing. The ESP32 driver does this when it
            // writes 16 bytes, because it also writes the other half of the 32-byte block.
            if (n == cipher_block && std::ranges::equal(stored, cipher)) {
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                if (stored[i] != erased) {
                    ++_overwrites;
                }
                stored[i] &= cipher[i];
            }
            // The block decrypts to noise unless it holds only erased bytes or exactly this
            // block's ciphertext.
            _torn[at / cipher_block] = !all_erased(stored) && !std::ranges::equal(stored, cipher);
        }
    }
    if (_armed) {
        _budget -= landed;
        if (landed < src.size()) {
            _cut = true;
            return idfxx::error(idfxx::errc::invalid_state);
        }
    }
    return {};
}

void ram_sector_store::_erase(size_t from, size_t length) {
    std::fill_n(_bytes.begin() + from, length, erased);
    if (!_encrypted) {
        return;
    }
    // A block that is erased only in part keeps some of its stored bytes, so it decrypts to noise.
    for (size_t b = from / cipher_block; b * cipher_block < from + length; ++b) {
        auto block = std::span(_bytes).subspan(b * cipher_block, cipher_block);
        bool whole = b * cipher_block >= from && (b + 1) * cipher_block <= from + length;
        _torn[b] = !whole && !all_erased(block);
    }
}

idfxx::result<void> ram_sector_store::do_erase_sector(size_t index) {
    if (_cut) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    size_t first = index * sector_size();
    ++_erases;
    if (_armed && _budget == 0) {
        // The power is cut halfway through the erase. The second half of the sector is erased. The
        // first half, which holds the sector's header, keeps what it held.
        _erase(first + sector_size() / 2, sector_size() - sector_size() / 2);
        _cut = true;
        _cut_erasing = true;
        return idfxx::error(idfxx::errc::invalid_state);
    }
    _erase(first, sector_size());
    return {};
}

void ram_sector_store::cut_after(size_t budget) {
    _armed = true;
    _budget = budget;
    _cut = false;
    _cut_erasing = false;
}

void ram_sector_store::restore_power() {
    _armed = false;
    _budget = 0;
    _cut = false;
    _cut_erasing = false;
}

void ram_sector_store::reset_counts() {
    _overwrites = 0;
    _reads = 0;
    _erases = 0;
}

} // namespace idfxx::persistent_log
