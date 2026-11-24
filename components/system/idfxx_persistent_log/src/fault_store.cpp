// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "fault_store.hpp"

#include "bytes.hpp"
#include "log_line.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>

namespace idfxx::persistent_log {

namespace {

using namespace detail;

// The magic and the layout version that a slot's header starts with. A header with this magic and
// a higher version has a newer layout, and its slot reads as empty.
constexpr char fault_magic[5] = "PFLT";
constexpr uint16_t fault_version = 1;

// The wall time field's value when the clock wasn't set.
constexpr int64_t no_wall = std::numeric_limits<int64_t>::min();

// The offset of each field in the header. Each field is aligned to its size. The bytes between the
// fields, and after the last field, are written as 0 and ignored when reading.
constexpr size_t at_magic = 0;
constexpr size_t at_layout = 4;
constexpr size_t at_seq = 8;
constexpr size_t at_boot = 12;
constexpr size_t at_wake = 16;
constexpr size_t at_wall = 24;
constexpr size_t at_uptime = 32;
constexpr size_t at_context_size = 36;
constexpr size_t at_context_crc = 40;
constexpr size_t at_kind = 44;
constexpr size_t at_version = at_kind + fault_kind_size;
constexpr size_t at_detail = at_version + fault_version_size;
constexpr size_t at_elf_sha256 = at_detail + fault_detail_size;
constexpr size_t at_task = at_elf_sha256 + fault_elf_sha256_size;
constexpr size_t at_exception_pc = at_task + fault_task_size;
constexpr size_t at_frame_count = at_exception_pc + 4;
constexpr size_t at_frames = at_frame_count + 4;
// The last core dump taken as of this entry: 1 when there is one, with the CRC of its image after
// it. Any other value means no dump has been taken. Every entry carries the value forward from
// the entry before it, unless the entry's own fault took a newer dump.
constexpr size_t at_has_dump_crc = at_frames + 4 * max_fault_frames;
constexpr size_t at_dump_crc = at_has_dump_crc + 4;
constexpr size_t at_reserved = at_dump_crc + 4;
constexpr size_t at_header_crc = fault_header_size - 4;
static_assert(at_version == 68 && at_detail == 100 && at_elf_sha256 == 260 && at_task == 268);
static_assert(at_exception_pc == 284 && at_frame_count == 288 && at_frames == 292);
static_assert(at_has_dump_crc == 356 && at_dump_crc == 360);
static_assert(at_reserved == 364 && at_header_crc == 508);
static_assert(at_exception_pc % 4 == 0 && at_frames % 4 == 0 && at_wall % 8 == 0);
static_assert(at_has_dump_crc % 4 == 0 && at_dump_crc % 4 == 0);
static_assert(fault_header_size % block_size == 0);

// The size of the buffer that the context is copied through when it is written to or read from
// flash.
constexpr size_t scratch_size = 256;
static_assert(scratch_size % block_size == 0);

using header_bytes = std::array<std::byte, fault_header_size>;

// The longest prefix of `s` that fits, with its NUL, in a field of `size` bytes, and that doesn't
// end in the middle of a UTF-8 character.
std::string_view fit(std::string_view s, size_t size) {
    return utf8_prefix(s, size - 1);
}

void get_text(const header_bytes& h, size_t at, std::span<char> field) {
    std::memcpy(field.data(), &h[at], field.size());
    field.back() = '\0';
}

bool valid_kind(std::string_view kind) {
    return !kind.empty() && kind.size() < fault_kind_size &&
        std::ranges::all_of(kind, [](char c) { return c > ' ' && c < 0x7F; });
}

uint32_t clamp_ms(std::chrono::milliseconds ms) {
    auto n = ms.count();
    if (n <= 0) {
        return 0;
    }
    return n > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                    : static_cast<uint32_t>(n);
}

// What a header's magic, layout version and CRC show about the header.
enum class header_state {
    // Not an entry. The header is erased or torn, or it has another magic or an older layout.
    none,
    // `fault_magic` and a good CRC, but a higher layout version.
    newer,
    // An entry that this layout can read.
    entry,
};

header_state check_header(const header_bytes& h) {
    // A header that a power cut tore fails its CRC check, so a torn version field can't be taken
    // for a newer layout.
    if (!has_magic(h, fault_magic) ||
        get_u32(&h[at_header_crc]) != crc32(0, std::span(h).first(at_header_crc))) {
        return header_state::none;
    }
    if (uint16_t layout = get_u16(&h[at_layout]); layout != fault_version) {
        return layout > fault_version ? header_state::newer : header_state::none;
    }
    return header_state::entry;
}

// A header as read. It holds an entry only when its magic, version and CRC are right.
struct decoded {
    bool valid = false;
    // `fault_magic` and a good CRC, but a higher layout version.
    bool newer = false;
    fault entry;
    uint32_t context_crc = 0;
};

idfxx::result<decoded> read_header(const sector_store& store, size_t offset, size_t room) {
    header_bytes h;
    if (auto r = store.try_read(offset, h); !r) {
        return idfxx::error(r.error());
    }
    decoded d;
    switch (check_header(h)) {
    case header_state::none:
        return d;
    case header_state::newer:
        d.newer = true;
        return d;
    case header_state::entry:
        break;
    }
    d.valid = true;
    auto& e = fault_access::fields(d.entry);
    e.seq = get_u32(&h[at_seq]);
    e.boot = get_u32(&h[at_boot]);
    e.wake = get_u32(&h[at_wake]);
    auto wall = static_cast<int64_t>(get_u64(&h[at_wall]));
    // A clock that counts in nanoseconds spans only the years 1678 to 2262. A time that the clock
    // can't hold reads as no wall-clock time, rather than overflowing. This store never writes
    // such a time.
    using namespace std::chrono;
    constexpr auto lowest = ceil<seconds>(system_clock::time_point::min().time_since_epoch());
    constexpr auto highest = floor<seconds>(system_clock::time_point::max().time_since_epoch());
    if (wall != no_wall && wall >= lowest.count() && wall <= highest.count()) {
        e.wall = std::chrono::sys_seconds(std::chrono::seconds(wall));
    }
    e.uptime = std::chrono::milliseconds(get_u32(&h[at_uptime]));
    uint32_t size = get_u32(&h[at_context_size]);
    // The size is larger than the slot's room only when the store was opened with a different
    // slot size from the one that the entry was written with. Return only what the slot holds,
    // and mark the entry damaged.
    if (size > room) {
        size = static_cast<uint32_t>(room);
        e.damaged = true;
    }
    e.context_size = size;
    d.context_crc = get_u32(&h[at_context_crc]);
    get_text(h, at_kind, e.kind);
    get_text(h, at_version, e.version);
    get_text(h, at_detail, e.detail);
    std::memcpy(e.elf_sha256.data(), &h[at_elf_sha256], fault_elf_sha256_size);
    get_text(h, at_task, e.task);
    e.exception_pc = get_u32(&h[at_exception_pc]);
    e.frame_count = std::min<uint32_t>(get_u32(&h[at_frame_count]), max_fault_frames);
    for (size_t i = 0; i < e.frame_count; ++i) {
        e.frames[i] = get_u32(&h[at_frames + 4 * i]);
    }
    return d;
}

// Copies the overlap between a header field at `field_at` and a piece of the header at
// `piece_at`. take() copies it from the piece into the field, and give() copies it from the field
// into the piece.
void take(
    std::span<std::byte> field,
    size_t field_at,
    std::span<const std::byte> piece,
    size_t piece_at
) {
    const size_t lo = std::max(field_at, piece_at);
    const size_t hi = std::min(field_at + field.size(), piece_at + piece.size());
    if (lo < hi) {
        std::memcpy(field.data() + (lo - field_at), piece.data() + (lo - piece_at), hi - lo);
    }
}

void give(
    std::span<const std::byte> field,
    size_t field_at,
    std::span<std::byte> piece,
    size_t piece_at
) {
    const size_t lo = std::max(field_at, piece_at);
    const size_t hi = std::min(field_at + field.size(), piece_at + piece.size());
    if (lo < hi) {
        std::memcpy(piece.data() + (lo - piece_at), field.data() + (lo - field_at), hi - lo);
    }
}

// The parts of a slot's header that a scan needs. These are whether the slot holds an entry, the
// entry's sequence number, and whether the entry is a clear. read_header_id() reads the header a
// piece at a time, so that no whole header is on the stack.
struct header_id {
    header_state state = header_state::none;
    uint32_t seq = 0;
    bool cleared = false;
};

idfxx::result<header_id> read_header_id(const sector_store& store, size_t offset) {
    std::array<std::byte, 64> piece;
    static_assert(fault_header_size % piece.size() == 0);
    std::array<std::byte, 4> magic;
    std::array<std::byte, 2> layout;
    std::array<std::byte, 4> seq;
    std::array<std::byte, fault_kind_size> kind;
    std::array<std::byte, 4> stored_crc;
    uint32_t crc = 0;
    for (size_t at = 0; at < fault_header_size; at += piece.size()) {
        if (auto r = store.try_read(offset + at, piece); !r) {
            return idfxx::error(r.error());
        }
        take(magic, at_magic, piece, at);
        take(layout, at_layout, piece, at);
        take(seq, at_seq, piece, at);
        take(kind, at_kind, piece, at);
        take(stored_crc, at_header_crc, piece, at);
        if (at < at_header_crc) {
            crc = crc32(crc, std::span(piece).first(std::min(piece.size(), at_header_crc - at)));
        }
    }
    header_id id;
    // A header that a power cut tore fails its CRC check, so a torn version field can't be taken
    // for a newer layout.
    if (!has_magic(magic, fault_magic) || get_u32(stored_crc.data()) != crc) {
        return id;
    }
    if (uint16_t v = get_u16(layout.data()); v != fault_version) {
        id.state = v > fault_version ? header_state::newer : header_state::none;
        return id;
    }
    id.state = header_state::entry;
    id.seq = get_u32(seq.data());
    kind.back() = std::byte{0};
    const auto* text = reinterpret_cast<const char*>(kind.data());
    id.cleared = std::string_view(text, strnlen(text, kind.size())) == cleared_kind;
    return id;
}

// The sequence number of the entry that a slot's header holds, or std::nullopt when the header
// holds no entry.
idfxx::result<std::optional<uint32_t>> read_seq(const sector_store& store, size_t offset) {
    auto id = read_header_id(store, offset);
    if (!id) {
        return idfxx::error(id.error());
    }
    if (id->state != header_state::entry) {
        return std::optional<uint32_t>();
    }
    return std::optional<uint32_t>(id->seq);
}

// The last core dump taken as of the entry whose slot starts at `offset`. The caller has checked
// that the slot holds an entry. It reads only the two fields, and it is never inlined, so that its
// buffer isn't on the stack while the scan reads each header.
[[gnu::noinline]] idfxx::result<std::optional<uint32_t>>
read_dump_crc(const sector_store& store, size_t offset) {
    std::array<std::byte, at_reserved - at_has_dump_crc> fields;
    if (auto r = store.try_read(offset + at_has_dump_crc, fields); !r) {
        return idfxx::error(r.error());
    }
    if (get_u32(&fields[0]) != 1) {
        return std::optional<uint32_t>();
    }
    return std::optional<uint32_t>(get_u32(&fields[at_dump_crc - at_has_dump_crc]));
}

// The context as one run of bytes, made by joining two runs.
struct joined {
    std::string_view a;
    std::string_view b;

    [[nodiscard]] size_t size() const { return a.size() + b.size(); }
    [[nodiscard]] char operator[](size_t i) const { return i < a.size() ? a[i] : b[i - a.size()]; }

    // Copies the bytes that start at `from` into `out`. There must be enough bytes to fill `out`.
    void copy(size_t from, std::span<char> out) const {
        size_t n = 0;
        if (from < a.size()) {
            n = std::min(out.size(), a.size() - from);
            std::memcpy(out.data(), a.data() + from, n);
        }
        if (n < out.size()) {
            std::memcpy(out.data() + n, b.data() + (from + n - a.size()), out.size() - n);
        }
    }
};

// Writes a line in pieces to a sink, through a small buffer.
class line_writer {
public:
    line_writer(void (*emit)(void*, std::string_view), void* sink)
        : _emit(emit)
        , _sink(sink) {}

    line_writer(const line_writer&) = delete;
    line_writer& operator=(const line_writer&) = delete;

    void put(char c) {
        if (_n == _buf.size()) {
            flush();
        }
        _buf[_n++] = c;
    }

    void put(std::string_view s) {
        if (s.size() > _buf.size() - _n) {
            flush();
            if (s.size() > _buf.size()) {
                _emit(_sink, s);
                return;
            }
        }
        std::memcpy(_buf.data() + _n, s.data(), s.size());
        _n += s.size();
    }

    template<typename T>
    void number(T v) {
        // 20 digits and a sign hold any 64-bit value.
        std::array<char, 21> digits;
        auto end = std::to_chars(digits.data(), digits.data() + digits.size(), v).ptr;
        put(std::string_view(digits.data(), static_cast<size_t>(end - digits.data())));
    }

    void padded(unsigned v, int width) {
        std::array<char, 10> digits;
        for (int i = width - 1; i >= 0; --i) {
            digits[i] = static_cast<char>('0' + v % 10);
            v /= 10;
        }
        put(std::string_view(digits.data(), static_cast<size_t>(width)));
    }

    // Writes the low `digits` hex digits of a value, lowercase and zero-padded.
    void hex(uint32_t v, int digits) {
        static constexpr char hex_digits[] = "0123456789abcdef";
        std::array<char, 8> text;
        for (int i = digits - 1; i >= 0; --i) {
            text[i] = hex_digits[v & 0xF];
            v >>= 4;
        }
        put(std::string_view(text.data(), static_cast<size_t>(digits)));
    }

    // Writes text escaped, so that nothing in it can end the line or a quoted value. Each byte of
    // the text becomes at most 4 bytes.
    void escaped(std::string_view s) {
        static constexpr char hex[] = "0123456789abcdef";
        for (char c : s) {
            auto u = static_cast<unsigned char>(c);
            switch (c) {
            case '"':
            case '\\':
                put('\\');
                put(c);
                break;
            case '\n':
                put("\\n");
                break;
            case '\r':
                put("\\r");
                break;
            case '\t':
                put("\\t");
                break;
            default:
                if (u < 0x20 || u == 0x7F) {
                    put("\\x");
                    put(hex[u >> 4]);
                    put(hex[u & 0xF]);
                } else {
                    put(c);
                }
            }
        }
    }

    // Passes the buffered bytes to the sink. A destructor doesn't do this, because the sink may
    // throw.
    void flush() {
        if (_n > 0) {
            _emit(_sink, std::string_view(_buf.data(), _n));
            _n = 0;
        }
    }

private:
    void (*_emit)(void*, std::string_view);
    void* _sink;
    std::array<char, 64> _buf;
    size_t _n = 0;
};

} // namespace

fault_store::fault_store(sector_store& store, size_t slot_size)
    : _store(&store)
    , _slot_size(slot_size)
    , _slots(store.size() / slot_size) {}

idfxx::result<fault_store> fault_store::try_open(sector_store& store, size_t slot_size) {
    size_t sector_size = store.sector_size();
    if (sector_size == 0 || slot_size <= fault_header_size || slot_size % sector_size != 0 ||
        slot_size > store.size() || slot_size > std::numeric_limits<uint32_t>::max() ||
        !takes_blocks(store)) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    fault_store faults(store, slot_size);
    if (auto r = faults._scan(); !r) {
        return idfxx::error(r.error());
    }
    return faults;
}

idfxx::result<void> fault_store::_scan() {
    _has_head = false;
    _floor.reset();
    _last_dump_crc.reset();
    for (uint32_t i = 0; i < _slots; ++i) {
        auto h = read_header_id(*_store, i * _slot_size);
        if (!h) {
            return idfxx::error(h.error());
        }
        _newer = _newer || h->state == header_state::newer;
        if (h->state != header_state::entry) {
            continue;
        }
        uint32_t seq = h->seq;
        // Sequence numbers rise by one per entry, so they would wrap only after billions of
        // faults.
        if (!_has_head || seq > _seq) {
            _has_head = true;
            _head = i;
            _seq = seq;
        }
        if (h->cleared && (!_floor || seq > *_floor)) {
            _floor = seq;
        }
    }
    // The newest entry holds the last dump taken, whether or not a clear hides it. The scan has
    // checked its header's CRC.
    if (_has_head) {
        auto dump_crc = read_dump_crc(*_store, _head * _slot_size);
        if (!dump_crc) {
            return idfxx::error(dump_crc.error());
        }
        _last_dump_crc = *dump_crc;
    }
    return {};
}

namespace {

// A context made from a joined run of bytes, starting at a given byte.
class joined_context final : public fault_context {
public:
    joined_context(joined text, size_t from)
        : _text(text)
        , _at(from)
        , _size(text.size() - from) {}

    [[nodiscard]] size_t size() const override { return _size; }

    [[nodiscard]] idfxx::result<void> fill(std::span<char> out) override {
        _text.copy(_at, out);
        _at += out.size();
        return {};
    }

private:
    joined _text;
    size_t _at;
    size_t _size;
};

} // namespace

bool recordable_kind(std::string_view kind) {
    return valid_kind(kind) && kind != cleared_kind;
}

std::string_view kept_detail(std::string_view detail) {
    return fit(detail, fault_detail_size);
}

idfxx::result<uint32_t>
fault_store::try_record(const fault_input& f, std::string_view context, std::string_view more) {
    if (!recordable_kind(f.kind)) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    // Keep the newest bytes that fit, beginning at the start of an entry.
    joined text{context, more};
    size_t from = 0;
    if (text.size() > room()) {
        const size_t cut = text.size() - room();
        from = detail::entry_start(
            [&text](size_t i) { return text[i]; }, cut, text.size(), text[cut - 1] == '\n'
        );
    }
    joined_context source(text, from);
    return _write_entry(f.kind, f, &source);
}

idfxx::result<uint32_t> fault_store::try_record(const fault_input& f, fault_context& context) {
    if (!recordable_kind(f.kind) || context.size() > room()) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    return _write_entry(f.kind, f, &context);
}

idfxx::result<void> fault_store::try_clear() {
    auto r = _write_entry(cleared_kind, fault_input{}, nullptr);
    if (!r) {
        return idfxx::error(r.error());
    }
    return {};
}

idfxx::result<uint32_t>
fault_store::_write_entry(std::string_view kind, const fault_input& f, fault_context* context) {
    // The entry always goes in the slot after the newest entry. On failure, nothing here changes,
    // so the next try takes the same slot and sequence number. So no slot is ever skipped, and
    // entries leave the ring in the order that they were recorded. That order is what keeps a
    // clear in force (see read()).
    uint32_t slot = _has_head ? static_cast<uint32_t>((_head + 1) % _slots) : 0;
    uint32_t seq = _has_head ? _seq + 1 : 0;
    const size_t length = context != nullptr ? context->size() : 0;

    // Erase the sectors that the entry needs, starting with the header's sector, so that the old
    // entry's header is erased before any other part of the old entry. The sectors beyond them
    // may keep the old entry's context. Nothing reads past the end of the new entry's context.
    size_t sector_size = _store->sector_size();
    size_t base = slot * _slot_size;
    size_t first_sector = base / sector_size;
    size_t sectors = (fault_header_size + round_up_block(length) + sector_size - 1) / sector_size;
    for (size_t i = 0; i < sectors; ++i) {
        if (auto r = _store->try_erase_sector(first_sector + i); !r) {
            return idfxx::error(r.error());
        }
    }

    // Write the context through a copy, so that the CRC covers the same bytes that are written,
    // even when the caller's memory changes during the write. Each piece except the last fills
    // the copy, which is a whole number of blocks. The last piece is padded with 0xFF to a whole
    // block.
    std::array<char, scratch_size> scratch;
    uint32_t crc = 0;
    for (size_t done = 0; done < length;) {
        size_t n = std::min(scratch.size(), length - done);
        auto piece = std::span(scratch).first(n);
        if (auto r = context->fill(piece); !r) {
            return idfxx::error(r.error());
        }
        crc = crc32(crc, std::as_bytes(piece));
        size_t padded = round_up_block(n);
        std::fill(scratch.begin() + n, scratch.begin() + padded, static_cast<char>(0xFF));
        auto bytes = std::as_bytes(std::span(scratch).first(padded));
        if (auto r = _store->try_write(base + fault_header_size + done, bytes); !r) {
            return idfxx::error(r.error());
        }
        done += n;
    }

    // Write the header last, a block at a time, and compute its CRC as the blocks are written.
    // Until the whole header is written, its CRC check fails and the slot reads as empty.
    int64_t wall = no_wall;
    if (f.wall) {
        wall = std::chrono::floor<std::chrono::seconds>(*f.wall).time_since_epoch().count();
    }
    const auto frames = f.frames.first(std::min(f.frames.size(), max_fault_frames));
    const std::optional<uint32_t> dump_crc = f.dump_crc ? f.dump_crc : _last_dump_crc;
    const auto fill = [&](std::span<std::byte> block, size_t at) {
        std::ranges::fill(block, std::byte{0});
        const auto u16 = [&](size_t field, uint16_t v) {
            std::array<std::byte, 2> b;
            put_u16(b.data(), v);
            give(b, field, block, at);
        };
        const auto u32 = [&](size_t field, uint32_t v) {
            std::array<std::byte, 4> b;
            put_u32(b.data(), v);
            give(b, field, block, at);
        };
        const auto text = [&](size_t field, size_t size, std::string_view v) {
            // The rest of the field is already 0, so the NUL is there.
            give(std::as_bytes(std::span(fit(v, size))), field, block, at);
        };
        std::array<std::byte, 4> magic;
        put_magic(magic.data(), fault_magic);
        give(magic, at_magic, block, at);
        u16(at_layout, fault_version);
        u32(at_seq, seq);
        u32(at_boot, f.boot);
        u32(at_wake, f.wake);
        std::array<std::byte, 8> w;
        put_u64(w.data(), static_cast<uint64_t>(wall));
        give(w, at_wall, block, at);
        u32(at_uptime, clamp_ms(f.uptime));
        u32(at_context_size, static_cast<uint32_t>(length));
        u32(at_context_crc, crc);
        text(at_kind, fault_kind_size, kind);
        text(at_version, fault_version_size, f.version);
        text(at_detail, fault_detail_size, f.detail);
        give(f.elf_sha256, at_elf_sha256, block, at);
        text(at_task, fault_task_size, f.task);
        u32(at_exception_pc, f.exception_pc);
        u32(at_frame_count, static_cast<uint32_t>(frames.size()));
        for (size_t i = 0; i < frames.size(); ++i) {
            u32(at_frames + 4 * i, frames[i]);
        }
        u32(at_has_dump_crc, dump_crc ? 1 : 0);
        u32(at_dump_crc, dump_crc.value_or(0));
    };
    std::array<std::byte, block_size> block;
    uint32_t header_crc = 0;
    for (size_t at = 0; at < fault_header_size; at += block.size()) {
        fill(block, at);
        if (at < at_header_crc) {
            header_crc = crc32(
                header_crc, std::span(block).first(std::min(block.size(), at_header_crc - at))
            );
        }
        if (at + block.size() > at_header_crc) {
            put_u32(&block[at_header_crc - at], header_crc);
        }
        if (auto r = _store->try_write(base + at, block); !r) {
            return idfxx::error(r.error());
        }
    }

    _has_head = true;
    _head = slot;
    _seq = seq;
    _last_dump_crc = dump_crc;
    if (kind == cleared_kind) {
        _floor = seq;
    }
    return seq;
}

idfxx::result<size_t> fault_store::try_count() const {
    size_t n = 0;
    for (uint32_t i = 0; i < _slots; ++i) {
        auto h = read_header_id(*_store, i * _slot_size);
        if (!h) {
            return idfxx::error(h.error());
        }
        if (h->state == header_state::entry && (!_floor || h->seq > *_floor)) {
            ++n;
        }
    }
    return n;
}

fault_store::reader fault_store::read() const {
    // List the oldest entry first, by going through the slots in order from the one after the
    // newest entry. Entries are written in slot order and no slot is skipped. So slot order from
    // there is the order in which the entries were recorded.
    //
    // Entries at or below the newest cleared entry's sequence number are hidden. This still holds
    // after the cleared entry itself is overwritten and no record of it is left. The cleared entry
    // was then the oldest entry, so every entry older than it was overwritten before it.
    if (!_has_head) {
        return reader(*_store, _slot_size, _slots, 0, 0, 0, _floor);
    }
    return reader(
        *_store,
        _slot_size,
        _slots,
        static_cast<uint32_t>((_head + 1) % _slots),
        _slots,
        _seq,
        _floor
    );
}

// =============================================================================
// reader
// =============================================================================

idfxx::result<bool> fault_store::reader::try_next(fault& out) {
    const size_t room = _slot_size - fault_header_size;
    _current = false;
    while (_left > 0) {
        size_t base = _slot * _slot_size;
        _slot = static_cast<uint32_t>((_slot + 1) % _slots);
        --_left;
        auto h = read_header(*_store, base, room);
        if (!h) {
            return idfxx::error(h.error());
        }
        // An entry recorded since read() is newer than every entry that this reader lists. It
        // took the oldest slot, which may be a slot that the reader hasn't reached yet.
        const uint32_t seq = h->entry.sequence();
        if (!h->valid || (_floor && seq <= *_floor) || seq > _newest) {
            continue;
        }
        // Check the context's CRC, a piece at a time. This runs under the lock, so the bytes that
        // next_context returns are these bytes, as long as the header that it checks again is
        // still this entry's header.
        const size_t size = h->entry.context_size();
        std::array<std::byte, scratch_size> scratch;
        uint32_t crc = 0;
        for (size_t done = 0; done < size;) {
            auto piece = std::span(scratch).first(std::min(scratch.size(), size - done));
            if (auto r = _store->try_read(base + fault_header_size + done, piece); !r) {
                return idfxx::error(r.error());
            }
            crc = crc32(crc, piece);
            done += piece.size();
        }
        out = h->entry;
        auto& fields = fault_access::fields(out);
        fields.damaged = fields.damaged || crc != h->context_crc;
        _current = true;
        _current_base = base;
        _current_seq = seq;
        _context_size = size;
        _context_at = 0;
        return true;
    }
    return false;
}

idfxx::result<size_t> fault_store::reader::try_next_context(std::span<char> out) {
    if (!_current || out.empty()) {
        return size_t{0};
    }
    if (_context_at == _context_size) {
        _current = false;
        return size_t{0};
    }
    // A record made since next() may have overwritten the entry. The slot then holds no entry, or
    // a newer one.
    auto seq = read_seq(*_store, _current_base);
    if (!seq) {
        return idfxx::error(seq.error());
    }
    if (*seq != _current_seq) {
        _current = false;
        return size_t{0};
    }
    const size_t n = std::min(out.size(), _context_size - _context_at);
    auto bytes = std::as_writable_bytes(out.first(n));
    if (auto r = _store->try_read(_current_base + fault_header_size + _context_at, bytes); !r) {
        return idfxx::error(r.error());
    }
    _context_at += n;
    return n;
}

// =============================================================================
// Formatting
// =============================================================================

void detail::format_fault(
    const fault& f,
    void (*emit)(void* sink, std::string_view piece),
    void* sink
) {
    // The line is at most 10 + 10 ("fault seq=" and the number), 6 + 92 (the kind, every byte
    // escaped), 6 + 10, 6 + 10, 5 + 20, 8 + 10, 10 + 124 + 1 (the version), 9 + 636 + 1 (the
    // detail), 5 + 16 (the ELF SHA-256), 7 + 60 + 1 (the task), 6 + 8 (the PC), 8 + 16 * 10 + 15
    // (the frames), 9 + 10 and 10 characters. That is 1289 characters, which is within
    // max_fault_line_size.
    line_writer w(emit, sink);
    w.put("fault seq=");
    w.number(f.sequence());
    w.put(" kind=");
    w.escaped(f.kind());
    w.put(" boot=");
    w.number(f.boot());
    w.put(" wake=");
    w.number(f.wake());
    w.put(" utc=");
    const auto wall = f.wall_time();
    if (!wall || !put_utc(w, *wall)) {
        w.put("none");
    }
    w.put(" uptime=");
    w.number(f.uptime().count());
    w.put(" version=\"");
    w.escaped(f.version());
    w.put("\" detail=\"");
    w.escaped(f.detail());
    w.put("\" elf=");
    for (std::byte b : f.elf_sha256()) {
        w.hex(std::to_integer<uint32_t>(b), 2);
    }
    w.put(" task=\"");
    w.escaped(f.task());
    w.put("\" pc=0x");
    w.hex(f.exception_pc(), 8);
    auto frames = f.frames();
    for (size_t i = 0; i < frames.size(); ++i) {
        w.put(i == 0 ? " frames=0x" : ",0x");
        w.hex(frames[i], 8);
    }
    w.put(" context=");
    w.number(f.context_size());
    if (f.damaged()) {
        w.put(" damaged=1");
    }
    w.flush();
}

} // namespace idfxx::persistent_log
