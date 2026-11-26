// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "flash_log.hpp"

#include "bytes.hpp"
#include "log_line.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace idfxx::persistent_log {

namespace {

using namespace detail;

// The magic and the layout version that a sector's header starts with. A sector with this magic
// and a higher version has a newer layout, and it reads as unused.
constexpr char sector_magic[5] = "PLOG";
constexpr uint16_t sector_version = 1;

// The offset of each field in the header block, which is the sector's first block.
constexpr size_t at_magic = 0;
constexpr size_t at_version = 4;
constexpr size_t at_flags = 6;
constexpr size_t at_seq = 8;
constexpr size_t at_crc = 12;
static_assert(at_crc + 4 == block_size);

// The retire block, which is the sector's second block. It is erased while the sector is in use.
// It is written with zeros just before the sector is erased, so that a sector whose erase was
// stopped by a power cut reads as unused.
constexpr size_t at_retire = block_size;
static_assert(at_retire + block_size == sector_header_size);

// The smallest sector size, which is the header and three blocks of records.
constexpr size_t min_sector_size = sector_header_size + 3 * block_size;

// The sector is the log's start. Readers begin here.
constexpr uint16_t flag_start = 1U << 0;

// The size of the buffer used to check CRCs and erased bytes when the caller's buffer is smaller.
constexpr size_t scratch_size = 128;
static_assert(scratch_size % block_size == 0);

// The number of bytes that a record with `length` bytes of text takes. That is its header and
// text, padded to a whole block.
constexpr size_t record_span(size_t length) {
    return round_up_block(record_header_size + length);
}

// A sector's header, as read. It is valid only when its magic, version and CRC are right.
struct sector_header {
    bool valid = false;
    // `sector_magic` and a good CRC, but a higher layout version.
    bool newer = false;
    uint32_t seq = 0;
    uint16_t flags = 0;
};

std::array<std::byte, block_size> encode_header(uint32_t seq, uint16_t flags) {
    std::array<std::byte, block_size> h{};
    put_magic(&h[at_magic], sector_magic);
    put_u16(&h[at_version], sector_version);
    put_u16(&h[at_flags], flags);
    put_u32(&h[at_seq], seq);
    put_u32(&h[at_crc], crc32(0, std::span(h).first(at_crc)));
    return h;
}

// Reads a sector's header. A sector whose retire block isn't erased as stored (read without
// decryption) is unused, as is a sector with a bad header. Such a sector was about to be erased,
// and a power cut may have stopped the erase in the middle. A retire mark that the power cut tore
// also counts.
idfxx::result<sector_header> read_header(const sector_store& store, size_t sector) {
    const size_t at = sector * store.sector_size();
    std::array<std::byte, block_size> h;
    if (auto r = store.try_read(at, h); !r) {
        return idfxx::error(r.error());
    }
    // A header that a power cut tore fails its CRC check, so a torn version field can't be taken
    // for a newer layout.
    if (!has_magic(h, sector_magic) ||
        get_u32(&h[at_crc]) != crc32(0, std::span(h).first(at_crc))) {
        return sector_header{};
    }
    uint16_t version = get_u16(&h[at_version]);
    if (version != sector_version) {
        return sector_header{.newer = version > sector_version};
    }
    std::array<std::byte, block_size> retire;
    if (auto r = store.try_read_raw(at + at_retire, retire); !r) {
        return idfxx::error(r.error());
    }
    if (!all_erased(retire)) {
        return sector_header{};
    }
    return sector_header{.valid = true, .seq = get_u32(&h[at_seq]), .flags = get_u16(&h[at_flags])};
}

enum class record_kind : uint8_t {
    // A whole record whose CRC matches.
    good,
    // An erased block. Nothing was written here.
    blank,
    // Anything else, such as what a power cut during a write leaves.
    bad,
};

struct record {
    record_kind kind = record_kind::bad;
    // The last byte of a good record's text.
    char last = 0;
    size_t length = 0;
};

// Checks the record at an offset in a sector, reading its text through scratch to check its CRC.
idfxx::result<record> check_record(
    const sector_store& store,
    size_t sector,
    size_t offset,
    std::span<std::byte> scratch
) {
    size_t sector_size = store.sector_size();
    size_t at = sector * sector_size + offset;
    // Erased bytes are recognized from the bytes as stored. Decrypted, they would read as noise.
    std::array<std::byte, block_size> first;
    if (auto r = store.try_read_raw(at, first); !r) {
        return idfxx::error(r.error());
    }
    if (all_erased(first)) {
        return record{.kind = record_kind::blank};
    }
    if (auto r = store.try_read(at, first); !r) {
        return idfxx::error(r.error());
    }
    size_t length = get_u16(&first[0]);
    if (length == 0 || length > sector_size - offset - record_header_size) {
        return record{};
    }
    // The first block holds the header and the start of the text.
    size_t in_first = std::min(length, block_size - record_header_size);
    uint32_t crc = crc32(0, std::span(first).first(2));
    crc = crc32(crc, std::span(first).subspan(record_header_size, in_first));
    std::byte last = first[record_header_size + in_first - 1];
    at += block_size;
    for (size_t left = length - in_first; left > 0;) {
        size_t n = std::min(left, scratch.size());
        if (auto r = store.try_read(at, scratch.first(n)); !r) {
            return idfxx::error(r.error());
        }
        crc = crc32(crc, scratch.first(n));
        last = scratch[n - 1];
        at += n;
        left -= n;
    }
    if (crc != get_u32(&first[2])) {
        return record{};
    }
    return record{.kind = record_kind::good, .last = static_cast<char>(last), .length = length};
}

// A summary of a sector's records.
struct sector_scan {
    // Where the good records end. This is the offset of the first record that is blank or bad.
    size_t end = sector_header_size;
    // The text in the good records.
    size_t text = 0;
    // The last byte of text in the good records, when there is any. It is a word rather than a
    // char, because a smaller sector_scan gives scan_sector() a larger stack frame at -Og.
    uint32_t last = 0;
    // The good records end at a bad record, rather than at a blank record or the end of the
    // sector.
    bool torn = false;
    // True when everything from `end` to the end of the sector is erased.
    bool clean = false;
};

// Reads the records of a sector through @p scratch.
idfxx::result<sector_scan> scan_sector(
    const sector_store& store,
    size_t sector,
    bool check_clean,
    std::span<std::byte> scratch
) {
    size_t sector_size = store.sector_size();
    sector_scan s;
    while (s.end < sector_size) {
        auto rec = check_record(store, sector, s.end, scratch);
        if (!rec) {
            return idfxx::error(rec.error());
        }
        if (rec->kind != record_kind::good) {
            s.torn = rec->kind == record_kind::bad;
            break;
        }
        s.last = static_cast<unsigned char>(rec->last);
        s.end += record_span(rec->length);
        s.text += rec->length;
    }
    if (check_clean) {
        size_t at = sector * sector_size + s.end;
        for (size_t left = sector_size - s.end; left > 0;) {
            size_t n = std::min(left, scratch.size());
            if (auto r = store.try_read_raw(at, scratch.first(n)); !r) {
                return idfxx::error(r.error());
            }
            if (!all_erased(scratch.first(n))) {
                return s;
            }
            at += n;
            left -= n;
        }
        s.clean = true;
    }
    return s;
}

// Whether the log ends partway through a line, from the scan of one of its sectors and the
// sector's flags, when the log holds nothing after that sector. It is true when the last byte of
// text isn't a newline. It is false when the log ends at a torn record, because a reader then ends
// the line itself, and when the sector is an empty start, because the log then holds no text. It
// is `std::nullopt` when the sector holds no text and isn't a start, because the sectors before it
// then decide.
std::optional<bool> ends_in_line(const sector_scan& s, uint16_t flags) {
    if (s.torn) {
        return false;
    }
    if (s.text > 0) {
        return s.last != '\n';
    }
    if ((flags & flag_start) != 0) {
        return false;
    }
    return std::nullopt;
}

// Whether the log ends partway through a line, found by searching back from the head. It is false
// when the log holds no text. The sectors are scanned through @p scratch.
idfxx::result<bool> find_ends_in_line(
    const sector_store& store,
    uint32_t head,
    uint32_t seq,
    std::span<std::byte> scratch
) {
    const size_t sectors = store.sector_count();
    uint32_t sector = head;
    for (size_t i = 0; i < sectors; ++i) {
        auto h = read_header(store, sector);
        if (!h) {
            return idfxx::error(h.error());
        }
        if (!h->valid || h->seq != seq) {
            return false;
        }
        auto scanned = scan_sector(store, sector, false, scratch);
        if (!scanned) {
            return idfxx::error(scanned.error());
        }
        if (auto open = ends_in_line(*scanned, h->flags)) {
            return *open;
        }
        sector = static_cast<uint32_t>((sector + sectors - 1) % sectors);
        --seq;
    }
    return false;
}

} // namespace

// The log's sectors. They run back from the head through each sector whose sequence number is one
// less than the next sector's. They stop at the first sector marked as a start, which is included,
// or before the first sector whose sequence number isn't one less.
struct flash_log::chain {
    uint32_t first = 0;
    uint32_t first_seq = 0;
    size_t sectors = 0;
    bool starts = false;
};

flash_log::flash_log(sector_store& store)
    : _store(&store)
    , _sectors(store.sector_count())
    , _sector_size(store.sector_size())
    , _scratch(std::make_unique_for_overwrite<std::byte[]>(scratch_size)) {}

std::span<std::byte> flash_log::_scan_buffer() const {
    return {_scratch.get(), scratch_size};
}

idfxx::result<flash_log> flash_log::try_open(sector_store& store, std::optional<position> hint) {
    size_t sector_size = store.sector_size();
    if (sector_size < min_sector_size || sector_size > 0x10000 || store.size() % sector_size != 0 ||
        store.sector_count() < 2 || !takes_blocks(store)) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    flash_log flash(store);
    if (hint) {
        auto held = flash._try_hint(*hint);
        if (!held) {
            return idfxx::error(held.error());
        }
        if (*held) {
            return flash;
        }
    }
    if (auto r = flash._scan(); !r) {
        return idfxx::error(r.error());
    }
    return flash;
}

idfxx::result<bool> flash_log::_try_hint(const position& hint) {
    if (hint.sector >= _sectors || hint.offset < sector_header_size || hint.offset > _sector_size ||
        hint.offset % block_size != 0) {
        return false;
    }
    auto here = read_header(*_store, hint.sector);
    if (!here) {
        return idfxx::error(here.error());
    }
    _newer = _newer || here->newer;
    if (!here->valid || here->seq != hint.seq) {
        return false;
    }
    // A writer that moved on wrote the next sector with the next sequence number. So an
    // out-of-date hint fails here, even when its sector's records end where the hint says.
    auto next = read_header(*_store, (hint.sector + 1) % _sectors);
    if (!next) {
        return idfxx::error(next.error());
    }
    _newer = _newer || next->newer;
    if (next->valid && next->seq == hint.seq + 1) {
        return false;
    }
    // When the offset is the sector's size, the next record goes in a new sector, so what this
    // sector holds doesn't matter.
    if (hint.offset < _sector_size) {
        auto s = scan_sector(*_store, hint.sector, true, _scan_buffer());
        if (!s) {
            return idfxx::error(s.error());
        }
        if (!s->clean || s->end != hint.offset) {
            return false;
        }
        _ends_in_line = ends_in_line(*s, here->flags);
    }
    _has_head = true;
    _head = hint.sector;
    _offset = hint.offset;
    _seq = hint.seq;
    _hinted = true;
    return true;
}

idfxx::result<void> flash_log::_scan() {
    _has_head = false;
    uint16_t flags = 0;
    for (uint32_t i = 0; i < _sectors; ++i) {
        auto h = read_header(*_store, i);
        if (!h) {
            return idfxx::error(h.error());
        }
        _newer = _newer || h->newer;
        // Sequence numbers rise by one per sector started, so the flash wears out long before
        // they would wrap.
        if (h->valid && (!_has_head || h->seq > _seq)) {
            _has_head = true;
            _head = i;
            _seq = h->seq;
            flags = h->flags;
        }
    }
    if (!_has_head) {
        return {};
    }
    auto s = scan_sector(*_store, _head, true, _scan_buffer());
    if (!s) {
        return idfxx::error(s.error());
    }
    // Never write after bytes that aren't erased. They may be a record that a power cut tore, and
    // a write over them would be ANDed into them.
    _offset = s->clean ? s->end : _sector_size;
    _ends_in_line = ends_in_line(*s, flags);
    return {};
}

idfxx::result<void> flash_log::_start_sector(uint16_t flags) {
    _ends_in_line.reset();
    uint32_t sector = _has_head ? (_head + 1) % _sectors : 0;
    uint32_t seq = _has_head ? _seq + 1 : 0;
    // On failure, the state stays as it was, so the next try starts the same sector again with
    // the same sequence number. So the chain of sequence numbers never has a gap.
    //
    // A sector in use is retired before it is erased. A power cut can stop an erase in the middle
    // and leave the sector's header and some of its text. Without the retire mark, that sector
    // would read as the log's oldest sector, cut off where the erase stopped. A sector that is
    // already unused (its header is bad or its retire block is written) is erased without a mark,
    // so that nothing is written over bytes that aren't erased.
    auto h = read_header(*_store, sector);
    if (!h) {
        return idfxx::error(h.error());
    }
    if (h->valid) {
        std::array<std::byte, block_size> mark{};
        if (auto r = _store->try_write(sector * _sector_size + at_retire, mark); !r) {
            return r;
        }
    }
    if (auto r = _store->try_erase_sector(sector); !r) {
        return r;
    }
    if (auto r = _store->try_write(sector * _sector_size, encode_header(seq, flags)); !r) {
        return r;
    }
    _has_head = true;
    _head = sector;
    _seq = seq;
    _offset = sector_header_size;
    return {};
}

idfxx::result<void> flash_log::_write_record(size_t at, std::string_view text) {
    // Write whole blocks, starting with the first block, which holds the header and the start of
    // the text. Then write the whole blocks of text directly from the caller's bytes, and then the
    // rest of the text, padded with 0xFF. If writing stops before all the text is written, the
    // record fails its CRC.
    _ends_in_line.reset();
    auto payload = std::as_bytes(std::span(text.data(), text.size()));
    std::array<std::byte, block_size> block;
    block.fill(erased);
    size_t in_first = std::min(payload.size(), block_size - record_header_size);
    put_u16(&block[0], static_cast<uint16_t>(payload.size()));
    put_u32(&block[2], crc32(crc32(0, std::span(block).first(2)), payload));
    std::ranges::copy(payload.first(in_first), block.begin() + record_header_size);
    if (auto r = _store->try_write(at, block); !r) {
        return r;
    }
    auto rest = payload.subspan(in_first);
    size_t whole = rest.size() / block_size * block_size;
    if (whole > 0) {
        if (auto r = _store->try_write(at + block_size, rest.first(whole)); !r) {
            return r;
        }
    }
    if (whole < rest.size()) {
        block.fill(erased);
        std::ranges::copy(rest.subspan(whole), block.begin());
        if (auto r = _store->try_write(at + block_size + whole, block); !r) {
            return r;
        }
    }
    return {};
}

idfxx::result<void> flash_log::try_append(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    // After a failed append, the log may end partway through a line. That line is ended first, so
    // that this text doesn't continue it.
    if (_maybe_open) {
        _maybe_open = false;
        if (auto r = try_end_line(); !r) {
            _maybe_open = true;
            return r;
        }
    }
    while (!text.empty()) {
        if (!_has_head || _sector_size - _offset < block_size) {
            // The first sector ever written is the log's start, as the first sector of a cleared
            // log is. If starting the sector fails, the text before it may have ended partway
            // through a line.
            if (auto r = _start_sector(_has_head ? 0 : flag_start); !r) {
                _maybe_open = true;
                return r;
            }
        }
        size_t room = _sector_size - _offset - record_header_size;
        size_t n = std::min(text.size(), room);
        if (auto r = _write_record(_head * _sector_size + _offset, text.substr(0, n)); !r) {
            // The sector may now hold part of a record. The next append starts a new sector. If
            // nothing of the record was written, the text before it may have ended partway
            // through a line.
            _offset = static_cast<uint32_t>(_sector_size);
            _maybe_open = true;
            return r;
        }
        _offset += static_cast<uint32_t>(record_span(n));
        text.remove_prefix(n);
    }
    return {};
}

idfxx::result<void> flash_log::try_end_line() {
    if (!_has_head) {
        return {};
    }
    bool open = false;
    if (_ends_in_line) {
        open = *_ends_in_line;
    } else {
        auto found = find_ends_in_line(*_store, _head, _seq, _scan_buffer());
        if (!found) {
            return idfxx::error(found.error());
        }
        open = *found;
    }
    if (!open) {
        return {};
    }
    return try_append("\n");
}

idfxx::result<void> flash_log::try_clear() {
    auto r = _start_sector(flag_start);
    if (r) {
        // Readers begin at the cleared start, so a line left open before it doesn't matter.
        _maybe_open = false;
    }
    return r;
}

std::optional<position> flash_log::head() const {
    if (!_has_head) {
        return std::nullopt;
    }
    return position{.sector = _head, .offset = _offset, .seq = _seq};
}

idfxx::result<flash_log::reader> flash_log::try_read() const {
    if (!_has_head) {
        return reader(*_store, 0, 0, 0, 0, true);
    }
    // Walk back from the head to the log's start.
    uint32_t first = _head;
    uint32_t seq = _seq;
    size_t sectors = 1;
    auto h = read_header(*_store, first);
    if (!h) {
        return idfxx::error(h.error());
    }
    bool start = h->valid && (h->flags & flag_start) != 0;
    while (!start && sectors < _sectors) {
        uint32_t prev = (first + _sectors - 1) % _sectors;
        auto p = read_header(*_store, prev);
        if (!p) {
            return idfxx::error(p.error());
        }
        if (!p->valid || p->seq != seq - 1) {
            break;
        }
        first = prev;
        seq = p->seq;
        ++sectors;
        start = (p->flags & flag_start) != 0;
    }
    // Only a start that clear() made (or the first sector ever written) is known to begin an
    // entry. The oldest sector left in a ring that has wrapped may begin in the middle of an entry.
    return reader(*_store, first, seq, sectors, _offset, start);
}

idfxx::result<flash_log::chain> flash_log::_tail_chain(size_t want) const {
    // Walk back from the head, as read() does, until the sectors hold more than the bytes wanted.
    chain c{.first = _head, .first_seq = _seq};
    size_t total = 0;
    while (c.sectors < _sectors) {
        uint32_t s = c.first;
        if (c.sectors > 0) {
            s = (c.first + _sectors - 1) % _sectors;
        }
        auto h = read_header(*_store, s);
        if (!h) {
            return idfxx::error(h.error());
        }
        if (!h->valid || (c.sectors > 0 && h->seq != c.first_seq - 1)) {
            break;
        }
        auto scanned = scan_sector(*_store, s, false, _scan_buffer());
        if (!scanned) {
            return idfxx::error(scanned.error());
        }
        c.first = s;
        c.first_seq = h->seq;
        ++c.sectors;
        total += scanned->text;
        c.starts = (h->flags & flag_start) != 0;
        if (c.starts || total > want) {
            break;
        }
    }
    return c;
}

idfxx::result<void> flash_log::try_read_tail(size_t want, std::optional<tail_reader>& out) const {
    out.reset();
    if (!_has_head || want == 0) {
        out.emplace(tail_reader::key{}, reader(*_store, 0, 0, 0, 0, true), 0, 0);
        return {};
    }
    // The sectors hold the bytes wanted and one more byte. That extra byte shows whether the
    // wanted bytes begin a line.
    auto c = _tail_chain(want);
    if (!c) {
        return idfxx::error(c.error());
    }
    const uint32_t first = c->first;
    const uint32_t seq = c->first_seq;
    const size_t sectors = c->sectors;
    const bool start = c->starts;

    // Work with the bytes as the reader returns them. After a gap, they differ from the records'
    // text, because the reader ends a line that was cut short and skips the lines after it that
    // start with a tab. So the bytes kept are the last of the bytes that the reader returns. Count
    // them, then read up to the last `want` of them, and on to the first entry that begins within
    // them. The last `want` bytes begin a line when the byte before them ends a line, or when they
    // begin at a start that clear() made. The tail reader starts at the chunk that the tail begins
    // in, and reads again the part of that chunk before the tail. The log doesn't change while
    // these readers are used, so they don't check again on each call what they read.
    reader from(*_store, first, seq, sectors, _offset, true);
    from._recheck = false;
    std::array<char, scratch_size> chunk;
    size_t given = 0;
    {
        reader count = from;
        for (;;) {
            auto n = count.try_next(chunk);
            if (!n) {
                return idfxx::error(n.error());
            }
            if (*n == 0) {
                break;
            }
            given += *n;
        }
    }
    const size_t skip = given > want ? given - want : 0;
    // From the byte at `skip` on, this is true when the next byte begins a line.
    bool at_line = start;
    size_t at = 0;
    reader find = from;
    while (at < given) {
        const reader before = find;
        auto n = find.try_next(chunk);
        if (!n) {
            return idfxx::error(n.error());
        }
        if (*n == 0) {
            break;
        }
        if (at + *n >= skip) {
            const size_t begin = skip > at ? skip - at : 0;
            if (begin > 0) {
                at_line = chunk[begin - 1] == '\n';
            }
            const size_t entry =
                entry_start([&chunk](size_t i) { return chunk[i]; }, begin, *n, at_line);
            if (entry < *n) {
                out.emplace(tail_reader::key{}, before, entry, given - at - entry);
                return {};
            }
            at_line = chunk[*n - 1] == '\n';
        }
        at += *n;
    }
    out.emplace(tail_reader::key{}, reader(*_store, 0, 0, 0, 0, true), 0, 0);
    return {};
}

idfxx::result<flash_log::reader>
flash_log::try_read_newest(size_t count, size_t largest, std::span<char> scratch) const {
    if (!_has_head || count == 0) {
        return reader(*_store, 0, 0, 0, 0, true);
    }
    // The newest `count` entries, and the part of an entry that the oldest sector may begin with,
    // are at most `count + 1` entries of the largest size.
    const size_t want = count < SIZE_MAX / largest ? (count + 1) * largest : SIZE_MAX;
    auto c = _tail_chain(want);
    if (!c) {
        return idfxx::error(c.error());
    }
    // As in try_read(), only a start that clear() made (or the first sector ever written) is known
    // to begin an entry. The passes below run while the log can't change, so their readers don't
    // check again on each call what they read. The reader returned does, because the caller may
    // use it while the log is appended to.
    reader from(*_store, c->first, c->first_seq, c->sectors, _offset, c->starts);
    from._recheck = false;

    // Count the entries.
    detail::entry_counter counted;
    {
        reader r = from;
        for (;;) {
            auto n = r.try_next(scratch);
            if (!n) {
                return idfxx::error(n.error());
            }
            if (*n == 0) {
                break;
            }
            counted.add({scratch.data(), *n});
        }
    }
    if (counted.count() <= count) {
        from._recheck = true;
        return from;
    }

    // Read up to the first entry to return. The chunk that it begins in is read again from where
    // the chunk began, up to the entry, so that the reader stops exactly there.
    const size_t first = counted.count() - count;
    detail::entry_counter seen;
    reader r = from;
    for (;;) {
        const reader before = r;
        auto n = r.try_next(scratch);
        if (!n) {
            return idfxx::error(n.error());
        }
        if (*n == 0) {
            r._recheck = true;
            return r;
        }
        const size_t at = seen.add({scratch.data(), *n}, first);
        if (at == std::string_view::npos) {
            continue;
        }
        r = before;
        for (size_t left = at; left > 0;) {
            auto m = r.try_next(scratch.first(left));
            if (!m) {
                return idfxx::error(m.error());
            }
            if (*m == 0) {
                break;
            }
            left -= *m;
        }
        r._recheck = true;
        return r;
    }
}

idfxx::result<size_t> flash_log::try_tail(std::span<char> out) const {
    std::optional<tail_reader> t;
    if (auto r = try_read_tail(out.size(), t); !r) {
        return idfxx::error(r.error());
    }
    size_t got = 0;
    while (got < out.size()) {
        auto n = t->try_next(out.subspan(got));
        if (!n) {
            return idfxx::error(n.error());
        }
        if (*n == 0) {
            break;
        }
        got += *n;
    }
    return got;
}

// =============================================================================
// tail_reader
// =============================================================================

idfxx::result<size_t> flash_log::tail_reader::try_next(std::span<char> out) {
    const size_t n = std::min(out.size(), _left);
    if (n == 0) {
        return size_t{0};
    }
    // First, read past the bytes before the tail. They are read into the caller's buffer and
    // dropped.
    while (_skip > 0) {
        auto r = _reader.try_next(out.first(std::min(n, _skip)));
        if (!r) {
            return idfxx::error(r.error());
        }
        if (*r == 0) {
            // The reader returns fewer bytes than were counted only if the log changed while the
            // reader was in use.
            _left = 0;
            return size_t{0};
        }
        _skip -= *r;
    }
    size_t got = 0;
    while (got < n) {
        auto r = _reader.try_next(out.subspan(got, n - got));
        if (!r) {
            return idfxx::error(r.error());
        }
        if (*r == 0) {
            // The reader returns fewer bytes than were counted only if the log changed while the
            // reader was in use.
            _left = got;
            break;
        }
        got += *r;
    }
    _left -= got;
    return got;
}

// =============================================================================
// reader
// =============================================================================

void flash_log::reader::_leave_gap(bool recycled, bool at_line) {
    // The rest of the sector is left unread. A reader that has returned text ends the line that it
    // was in, and goes on from the next entry. When `at_line` is set, it takes what it reads next
    // as the start of a line. A reader that hasn't returned anything yet begins on an entry after
    // a recycled sector. After a bad record, it also takes what it reads next as the start of a
    // line.
    if (_given) {
        _newline_due = _newline_due || _line_open;
        _line_open = false;
        _seeking = true;
        _at_line = at_line;
    } else if (recycled || at_line) {
        _seeking = true;
        _at_line = at_line;
    }
    _leave_sector();
}

bool flash_log::reader::_past_records() const {
    // The head sector is read as it was when read() was called. Records appended to it since then
    // are left out.
    const size_t next = round_up_block(_offset);
    return next >= _store->sector_size() || (_left == 1 && next >= _end_offset);
}

void flash_log::reader::_leave_sector() {
    _in_sector = false;
    _record_at = 0;
    _record_length = 0;
    _record_left = 0;
    _sector = static_cast<uint32_t>((_sector + 1) % _store->sector_count());
    ++_seq;
    --_left;
}

idfxx::result<void> flash_log::reader::_resume(std::span<std::byte> scratch) {
    // A sector keeps its sequence number until it is erased and started again with a higher one.
    // So the same number means the same records. A CRC alone wouldn't be enough. A sector started
    // again with lines of the same lengths has records at the same offsets, each with a good CRC.
    auto h = read_header(*_store, _sector);
    if (!h) {
        return idfxx::error(h.error());
    }
    if (!h->valid || h->seq != _seq) {
        // If the sector was recycled after the reader passed its last record, nothing unread was
        // lost.
        if (_record_left == 0 && _past_records()) {
            _leave_sector();
        } else {
            _leave_gap(true, false);
        }
        return {};
    }
    // Check the partly returned record again, so that the part of it that this call returns was
    // checked in this call.
    if (_record_left > 0) {
        auto rec = check_record(*_store, _sector, _record_at, scratch);
        if (!rec) {
            return idfxx::error(rec.error());
        }
        if (rec->kind != record_kind::good || rec->length != _record_length) {
            _leave_gap(false, false);
        }
    }
    return {};
}

idfxx::result<bool> flash_log::reader::_next_record(std::span<std::byte> scratch) {
    while (true) {
        if (!_in_sector) {
            if (_left == 0) {
                return false;
            }
            auto h = read_header(*_store, _sector);
            if (!h) {
                return idfxx::error(h.error());
            }
            if (!h->valid || h->seq != _seq) {
                _leave_gap(true, false);
                continue;
            }
            _in_sector = true;
            _offset = sector_header_size;
        }
        if (_past_records()) {
            _leave_sector();
            continue;
        }
        // Move past the record just returned, to the block that the next record starts in.
        _offset = round_up_block(_offset);
        auto rec = check_record(*_store, _sector, _offset, scratch);
        if (!rec) {
            return idfxx::error(rec.error());
        }
        // A record that a power cut or a failed write tore. The writer went on in a new sector.
        // What the writer wrote there is taken as the start of a line, which it is when each
        // append begins a line (the service appends whole entries).
        if (rec->kind == record_kind::bad) {
            _leave_gap(false, true);
            continue;
        }
        if (rec->kind != record_kind::good) {
            _leave_sector();
            continue;
        }
        _record_at = _offset;
        _record_length = rec->length;
        _offset += record_header_size;
        _record_left = rec->length;
        return true;
    }
}

idfxx::result<size_t> flash_log::reader::try_next(std::span<char> out) {
    size_t produced = 0;
    std::array<std::byte, scratch_size> own_scratch;
    // When the caller's buffer is the larger, as it is when streaming, records' CRCs are checked
    // through it, so that the text is read in fewer pieces.
    const auto scratch_after = [&](size_t used) {
        auto rest = std::as_writable_bytes(out.subspan(used));
        return rest.size() >= own_scratch.size() ? rest : std::span(own_scratch);
    };
    // Ends the line that was partly returned before a gap, once there is room.
    const auto end_line = [&] {
        if (_newline_due && produced < out.size()) {
            out[produced++] = '\n';
            _newline_due = false;
        }
    };
    // The log may have been appended to since the last call.
    if (_in_sector && _recheck) {
        if (auto r = _resume(scratch_after(0)); !r) {
            return idfxx::error(r.error());
        }
    }
    end_line();
    while (produced < out.size()) {
        if (_record_left == 0) {
            auto more = _next_record(scratch_after(produced));
            if (!more) {
                return idfxx::error(more.error());
            }
            end_line();
            if (!*more) {
                break;
            }
            continue;
        }
        size_t n = std::min(_record_left, out.size() - produced);
        size_t at = _sector * _store->sector_size() + _offset;
        auto dst = std::as_writable_bytes(out.subspan(produced, n));
        if (auto r = _store->try_read(at, dst); !r) {
            return idfxx::error(r.error());
        }
        _offset += n;
        _record_left -= n;
        if (_seeking) {
            // Leave out the bytes up to the first entry that begins in them. When no entry begins
            // in them, the search carries on into the next bytes read.
            char* got = out.data() + produced;
            const size_t from = entry_start([got](size_t i) { return got[i]; }, 0, n, _at_line);
            if (from == n) {
                _at_line = got[n - 1] == '\n';
                continue;
            }
            _seeking = false;
            std::memmove(got, got + from, n - from);
            n -= from;
        }
        produced += n;
        // Update these as each piece is returned, because a gap later in this call needs them.
        _given = true;
        _line_open = out[produced - 1] != '\n';
    }
    return produced;
}

} // namespace idfxx::persistent_log
