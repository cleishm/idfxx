// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Seeded randomized tests for idfxx_persistent_log's service. Each read is checked against a model
// of the entries that were written. A run starts the log on a RAM store with a random geometry. It
// then appends entries (and, when capture receives ESP-IDF's log lines, logs lines), flushes,
// reads, copies the newest entries, clears, restarts and injects faults, all chosen by the run's
// seed. In each read, every entry written since the last clear must be exactly one of these:
// - kept: complete, once, and in order;
// - evicted: before the first kept entry;
// - dropped: in a gap that a marker counts to the byte;
// - lost to a fault that the run injected: pending in the flush that the fault interrupted.
// The tests also show that the checker rejects a read with an entry taken out, an entry doubled, or
// a marker's count off by one, both on reads built by hand and on the runs' own reads. Each read
// also reads the same log with an entry reader. Its entries, formatted and joined with newlines,
// must be the text that the read returned, without the text's empty lines, and must be the entries
// that next_entry() takes from that text, one for one. Some reads also check that read_newest()
// returns the last entries of the read.
//
// Most runs also have fault slots. Among their other operations, these runs record faults, some
// with their repeats suppressed, and lift the suppressions. The lines that the faults log are model
// entries like any other. The lines of a fault with its repeats suppressed carry the id of the
// fault's kind rather than an id of their own, and the checker tells them apart by their text and
// their stamps. The faults listed are the most recent faults recorded since the last clear, as many
// as the slots hold, each with its kind, detail, boot, wake and time. Under any cut or reset, a
// fault's context is no longer than its room, begins on a stamped line, and ends with the fault's
// own entry. No cut comes while a fault is being recorded, so the context is also exactly the
// newest complete entries of the log in flash that fit (as copy_newest_in_flash copies them, and
// checked as such), followed by the ring's unflushed bytes. The context reads back the same through
// buffers of any size. A fault with its repeats suppressed is recorded unless its kind is
// remembered, whatever its detail. Once the fault is recorded, its kind is remembered, and when the
// memory is full, it replaces the kind recorded longest ago. A lift forgets the lifted kind, and a
// clear of the faults and a power cut forget every remembered kind, while a restart or a reset
// keeps them.

#include "../src/entry_buffer.hpp"
#include "../src/rtc.hpp"
#include "../src/sector_store.hpp"
#include "../src/service.hpp"
#include "entry_helpers.hpp"
#include "idfxx/persistent_log"
#include "qemu_log_cache.hpp"
#include "sdkconfig.h"
#include "test_cleanup.hpp"
#include "unity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pl = idfxx::persistent_log;
namespace internal = idfxx::persistent_log::internal;

namespace {

// =============================================================================
// Settings
// =============================================================================

// The seeds of the randomized case. Each seed is one run, on a store of its own. When running
// locally, raise extra_seeds to run that many more seeds (from seed 1000 on), and raise ops_per_run
// for longer runs.
constexpr std::array<uint32_t, 48> seeds = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48
};
constexpr uint32_t extra_seeds = 0;
constexpr int ops_per_run = 1500;

constexpr size_t max_line = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;
constexpr size_t ring_size = CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE;
constexpr std::string_view marker_head = "W persistent_log: dropped ";
constexpr std::string_view marker_tail = " bytes";

// =============================================================================
// Random numbers
// =============================================================================

// splitmix64, which gives the same numbers from a seed on every toolchain.
class prng {
public:
    explicit prng(uint64_t seed)
        : _s(seed) {}

    uint32_t next() {
        uint64_t z = (_s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return static_cast<uint32_t>((z ^ (z >> 31)) >> 32);
    }

    // Returns a number from 0 to n - 1, or 0 when n is 0.
    uint32_t below(uint32_t n) { return static_cast<uint32_t>((uint64_t{next()} * n) >> 32); }

    bool chance(uint32_t percent) { return below(100) < percent; }

private:
    uint64_t _s;
};

// =============================================================================
// The model
// =============================================================================

// The milliseconds since boot, as a stamp counts them.
int64_t now_ms() {
    return esp_timer_get_time() / 1000;
}

size_t digits(uint64_t v) {
    size_t n = 1;
    while (v >= 10) {
        v /= 10;
        ++n;
    }
    return n;
}

// An entry that was written: its text after the stamp, and the values that its stamp can hold.
struct entry {
    // The entry's id. Its text begins with the id, as "<#run.n>".
    uint32_t n = 0;
    // The text after the stamp, without the final newline. Each line after the first follows
    // "\n\t".
    std::string body;
    uint32_t boot = 0;
    uint32_t wake = 0;
    // The milliseconds since boot before and after the entry was written. The milliseconds in its
    // stamp are between the two.
    int64_t ms_lo = 0;
    int64_t ms_hi = 0;
    // The flush attempt that takes it from the ring.
    uint32_t flush = 0;
    bool captured = false;
    // The id that the entry's text carries, when that isn't its own `n`. This is the kind of a
    // suppressed fault. All of that fault's lines share the id, and the checker tells them apart
    // by their text and the windows of their stamps.
    std::optional<uint32_t> carried = std::nullopt;
};

// How a flush attempt ended.
enum class outcome : uint8_t {
    // Written completely.
    ok,
    // A store write failed partway through, or a reset came during a write. A leading part of what
    // the flush wrote is in flash, in whole records.
    cut,
    // Nothing was written, because the power was lost while the entries were in the ring.
    lost,
    // The ring was found not valid, but its indices were intact. A marker counts its entries.
    corrupt,
    // The ring was found not valid, and its indices spanned more than its size. A marker counts
    // the ring's size.
    corrupt_full,
};

struct flush_attempt {
    outcome out = outcome::ok;
    // The ring had lost bytes before the flush, so the flush writes a marker first.
    bool marker = false;
    uint32_t boot = 0;
    uint32_t wake = 0;
    int64_t ms_lo = 0;
    int64_t ms_hi = 0;
};

struct model {
    // The run number that each entry's id carries.
    uint32_t run = 0;
    // The entries since the last clear, except those that no read can return again. They are in
    // order of `n` and of `flush`.
    std::vector<entry> entries;
    // The flush attempts of the run from `base` on, indexed by attempt number minus `base`. The
    // entries since the last flush attempt are in the ring.
    std::vector<flush_attempt> flushes;
    uint32_t base = 0;
    // The first flush attempt that a read may still return any part of. It is no earlier than the
    // last clear, and no earlier than the start of the last full read.
    uint32_t floor = 0;

    // The number of flush attempts so far.
    [[nodiscard]] uint32_t attempts() const { return base + static_cast<uint32_t>(flushes.size()); }

    [[nodiscard]] const flush_attempt& at(size_t g) const { return flushes[g - base]; }

    // Forgets the flush attempts before `floor`.
    void forget() {
        if (floor > base) {
            flushes.erase(flushes.begin(), flushes.begin() + (floor - base));
            base = floor;
        }
    }
};

// Returns the text after the stamp of the entry that is kept from `text`. This is its first line,
// then each later line that isn't empty, after a tab.
std::string entry_body(std::string_view text) {
    const size_t nl = text.find('\n');
    std::string body(text.substr(0, nl));
    if (nl == std::string_view::npos) {
        return body;
    }
    text.remove_prefix(nl + 1);
    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (!line.empty()) {
            body += "\n\t";
            body += line;
        }
    }
    return body;
}

// Returns the body of the entry that append() keeps from `text`, if it keeps one. Leading newlines
// are dropped, and then at most max_line bytes are kept.
std::optional<std::string> append_body(std::string_view text) {
    text.remove_prefix(std::min(text.find_first_not_of('\n'), text.size()));
    if (text.empty()) {
        return std::nullopt;
    }
    return entry_body(text.substr(0, max_line));
}

size_t fixed_size(const entry& e) {
    // "<boot>.<wake> +<ms> - ", the body and its newline, except for the ms digits.
    return digits(e.boot) + digits(e.wake) + 6 + e.body.size() + 1;
}

struct size_range {
    uint64_t lo = 0;
    uint64_t hi = 0;
    bool valid = true;
};

// Returns the range of bytes that entries [from, to) took in the ring. Each entry's ms is taken to
// be at least `min_digits` and at most `max_digits` digits long, as well as within the entry's own
// bounds.
size_range sizes(const model& m, size_t from, size_t to, size_t min_digits, size_t max_digits) {
    size_range r;
    for (size_t i = from; i < to; ++i) {
        const entry& e = m.entries[i];
        const size_t lo = std::max(digits(static_cast<uint64_t>(e.ms_lo)), min_digits);
        const size_t hi = std::min(digits(static_cast<uint64_t>(e.ms_hi)), max_digits);
        if (lo > hi) {
            r.valid = false;
        }
        r.lo += fixed_size(e) + lo;
        r.hi += fixed_size(e) + hi;
    }
    return r;
}

bool in(const size_range& r, uint64_t v) {
    return r.valid && v >= r.lo && v <= r.hi;
}

// =============================================================================
// Reading a read
// =============================================================================

// An entry of a read: a stamped line and the lines after it that start with a tab.
struct token {
    enum kind_t : uint8_t {
        // A complete model entry.
        kept,
        // A line counting dropped bytes.
        marker,
        // The leading part of an entry or a marker, cut at a record boundary.
        fragment,
    };
    kind_t kind = fragment;
    // The token's offset in the read, and its size there, including its final newline.
    size_t at = 0;
    size_t size = 0;
    // Its text without the final newline.
    std::string_view text = {};
    // The stamp, when it is complete.
    bool stamped = false;
    uint32_t boot = 0;
    uint32_t wake = 0;
    int64_t ms = 0;
    std::string_view body = {};
    // For a kept entry, or a fragment with a complete id, the model entry.
    bool has_index = false;
    size_t index = 0;
    // For a marker, the number of bytes it counts.
    uint64_t count = 0;
    // The flush attempt matched with it.
    uint32_t flush = 0;
};

bool parse_number(std::string_view s, size_t& at, uint64_t& out) {
    const size_t from = at;
    out = 0;
    while (at < s.size() && s[at] >= '0' && s[at] <= '9' && at - from < 19) {
        out = out * 10 + static_cast<uint64_t>(s[at] - '0');
        ++at;
    }
    return at > from;
}

// Parses "<boot>.<wake> +<ms> - " at the start of t.text.
void parse_stamp(token& t) {
    size_t at = 0;
    uint64_t boot = 0;
    uint64_t wake = 0;
    uint64_t ms = 0;
    const std::string_view s = t.text;
    if (!parse_number(s, at, boot) || at >= s.size() || s[at] != '.') {
        return;
    }
    ++at;
    if (!parse_number(s, at, wake) || !s.substr(at).starts_with(" +")) {
        return;
    }
    at += 2;
    if (!parse_number(s, at, ms) || !s.substr(at).starts_with(" - ")) {
        return;
    }
    at += 3;
    t.stamped = true;
    t.boot = static_cast<uint32_t>(boot);
    t.wake = static_cast<uint32_t>(wake);
    t.ms = static_cast<int64_t>(ms);
    t.body = s.substr(at);
}

// Checks whether `text`, the visible part of a line that was cut short, could begin with a stamp
// that has this boot and wake and an ms in [ms_lo, ms_hi], followed by any text after the stamp.
// Returns where the stamp ends in `text`, or npos when `text` ends within the stamp. Returns
// nullopt when `text` can't begin that way.
std::optional<size_t>
stamp_prefix(std::string_view text, uint32_t boot, uint32_t wake, int64_t ms_lo, int64_t ms_hi) {
    const std::string head = std::to_string(boot) + "." + std::to_string(wake) + " +";
    if (text.size() <= head.size()) {
        return std::string_view(head).starts_with(text)
            ? std::optional<size_t>(std::string_view::npos)
            : std::nullopt;
    }
    if (!text.starts_with(head)) {
        return std::nullopt;
    }
    size_t at = head.size();
    uint64_t ms = 0;
    if (!parse_number(text, at, ms)) {
        return std::nullopt;
    }
    if (at == text.size()) {
        return digits(ms) <= digits(static_cast<uint64_t>(ms_hi))
            ? std::optional<size_t>(std::string_view::npos)
            : std::nullopt;
    }
    const std::string_view rest = text.substr(at);
    if (rest.size() < 3) {
        return std::string_view(" - ").starts_with(rest)
            ? std::optional<size_t>(std::string_view::npos)
            : std::nullopt;
    }
    if (!rest.starts_with(" - ") || static_cast<int64_t>(ms) < ms_lo ||
        static_cast<int64_t>(ms) > ms_hi) {
        return std::nullopt;
    }
    return at + 3;
}

// Whether `text` is the leading part, cut short, of entry e's line.
bool truncates(std::string_view text, const entry& e) {
    const auto end = stamp_prefix(text, e.boot, e.wake, e.ms_lo, e.ms_hi);
    if (!end) {
        return false;
    }
    if (*end == std::string_view::npos) {
        return true;
    }
    const std::string_view rest = text.substr(*end);
    return rest.size() < e.body.size() && std::string_view(e.body).starts_with(rest);
}

// Whether `text` is the leading part, cut short, of the marker that flush attempt f writes.
bool truncates_marker(std::string_view text, const flush_attempt& f) {
    const auto end = stamp_prefix(text, f.boot, f.wake, f.ms_lo, f.ms_hi);
    if (!end) {
        return false;
    }
    if (*end == std::string_view::npos) {
        return true;
    }
    std::string_view rest = text.substr(*end);
    if (rest.size() <= marker_head.size()) {
        return marker_head.starts_with(rest);
    }
    if (!rest.starts_with(marker_head)) {
        return false;
    }
    size_t at = marker_head.size();
    uint64_t n = 0;
    if (!parse_number(rest, at, n)) {
        return false;
    }
    rest.remove_prefix(at);
    return rest.size() < marker_tail.size() && marker_tail.starts_with(rest);
}

// Looks up an entry by the id at the start of its text. The id "<#run.n>" is at the start, after
// "L tag: " for a captured line, or after the head of a fault's line. The longest head of a fault's
// line is 48 characters ("I persistent_log: fault suppression lifted kind=").
std::optional<uint32_t> id_in(std::string_view body, uint32_t run) {
    const size_t at = body.find("<#");
    if (at == std::string_view::npos || at > 48) {
        return std::nullopt;
    }
    size_t i = at + 2;
    uint64_t r = 0;
    uint64_t n = 0;
    if (!parse_number(body, i, r) || i >= body.size() || body[i] != '.') {
        return std::nullopt;
    }
    ++i;
    if (!parse_number(body, i, n) || i >= body.size() || body[i] != '>' || r != run) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(n);
}

// The result of checking a read against the model.
struct verdict {
    // Empty when the model allows the read. Otherwise, what is wrong with the read.
    std::string error;
    size_t kept = 0;
    size_t evicted = 0;
    size_t dropped = 0;
    size_t lost = 0;
    size_t truncated = 0;
    size_t markers = 0;
    size_t continued = 0;
    size_t captured = 0;
    // The model entry of the last kept entry, or -1.
    int64_t last_n = -1;
    // The lowest flush attempt that any part of the read came from, or the first flush attempt
    // not yet done.
    uint32_t lowest_flush = 0;
    std::vector<token> tokens;
    // Kept entries that are between two kept entries of their own flush. If one of them is taken
    // out, the read must fail.
    std::vector<size_t> removable;
    // Markers whose count was matched exactly. If one of those counts is changed, the read must
    // fail.
    std::vector<size_t> exact;
};

bool is(const token& t, token::kind_t k) {
    return t.kind == k;
}

// Matches the model entries [lo, hi) that are missing between kept entries a and b with `items`,
// the markers and fragments between a and b. At the read's start a is absent, and at its end b is
// absent. Works back from b, one flush attempt at a time.
bool explain(
    const model& m,
    uint32_t done,
    const token* a,
    const token* b,
    size_t lo,
    size_t hi,
    std::vector<size_t>& items,
    verdict& v
) {
    auto& ts = v.tokens;
    const bool started = a != nullptr;
    const auto& E = m.entries;
    if (a != nullptr && b != nullptr && E[a->index].flush == E[b->index].flush) {
        if (lo < hi) {
            v.error = "entries missing between two kept entries of one flush";
            return false;
        }
        if (!items.empty()) {
            v.error = "a marker or fragment between two kept entries of one flush";
            return false;
        }
        return true;
    }
    if (done == 0) {
        if (lo < hi || !items.empty()) {
            v.error = "text before any flush";
            return false;
        }
        return true;
    }
    const int64_t top = b != nullptr ? E[b->index].flush : done - 1;
    const int64_t bottom = a != nullptr ? E[a->index].flush : m.floor;
    const size_t min_digits = a != nullptr ? digits(static_cast<uint64_t>(a->ms)) : 1;
    size_t qi = items.size();
    size_t e = hi;
    bool evicting = false;
    const auto back_is = [&](size_t from_back, token::kind_t k) {
        return qi >= from_back && is(ts[items[qi - from_back]], k);
    };
    const auto back = [&](size_t from_back) -> token& { return ts[items[qi - from_back]]; };
    const auto marker_fits = [&](const token& t, const flush_attempt& f) {
        return t.boot == f.boot && t.wake == f.wake && t.ms >= f.ms_lo && t.ms <= f.ms_hi;
    };
    const auto count_missing = [&](size_t n) {
        if (started) {
            v.lost += n;
        } else {
            v.evicted += n;
        }
    };
    for (int64_t g = top; g >= bottom && !evicting; --g) {
        size_t s = e;
        while (s > lo && E[s - 1].flush == static_cast<uint32_t>(g)) {
            --s;
        }
        const flush_attempt& f = m.at(static_cast<size_t>(g));
        const auto take_marker = [&](token& t, size_t from, size_t to, bool record_exact) {
            t.flush = static_cast<uint32_t>(g);
            ++v.markers;
            const size_range r = f.out == outcome::corrupt_full
                ? size_range{ring_size, ring_size}
                : sizes(m, from, to, min_digits, digits(static_cast<uint64_t>(t.ms)));
            if (!in(r, t.count)) {
                return false;
            }
            if (record_exact && r.lo == r.hi) {
                v.exact.push_back(items[qi - 1]);
            }
            return true;
        };
        if (b != nullptr && g == top) {
            // The entries of b's flush that come before b were dropped, and the flush's marker
            // counts them.
            if (f.marker) {
                if (back_is(1, token::marker) && marker_fits(back(1), f)) {
                    if (!take_marker(back(1), s, e, true)) {
                        v.error = "a marker's count isn't the size of the entries dropped "
                                  "before a kept entry";
                        return false;
                    }
                    v.dropped += e - s;
                    --qi;
                } else if (!started) {
                    v.evicted += e - s;
                    evicting = true;
                } else {
                    v.error = "no marker for a flush that dropped entries";
                    return false;
                }
            } else if (s < e) {
                if (!started) {
                    v.evicted += e - s;
                    evicting = true;
                } else {
                    v.error = "entries missing before a kept entry of their flush, with no marker";
                    return false;
                }
            }
            e = s;
            continue;
        }
        if (a != nullptr && g == bottom) {
            // The entries of a's flush that come after a were lost to a fault, and the first of
            // them may be cut short.
            if (s < e || back_is(1, token::fragment)) {
                if (f.out != outcome::cut) {
                    v.error = s < e ? "entries missing after a kept entry of their flush"
                                    : "a fragment after a flush that wasn't cut";
                    return false;
                }
                if (s < e && back_is(1, token::fragment) &&
                    (!back(1).has_index || back(1).index == s) && truncates(back(1).text, E[s])) {
                    back(1).flush = static_cast<uint32_t>(g);
                    ++v.truncated;
                    --qi;
                    v.lost += e - s - 1;
                } else {
                    v.lost += e - s;
                }
            }
            e = s;
            continue;
        }
        // A flush that has no part kept in the read.
        switch (f.out) {
        case outcome::lost:
            count_missing(e - s);
            break;
        case outcome::ok:
        case outcome::corrupt:
        case outcome::corrupt_full:
            if (f.marker || f.out == outcome::corrupt_full) {
                if (back_is(1, token::marker) && marker_fits(back(1), f)) {
                    if (!take_marker(back(1), s, e, true)) {
                        v.error = "a marker's count isn't the size of its flush's entries";
                        return false;
                    }
                    v.dropped += e - s;
                    --qi;
                } else if (!started) {
                    v.evicted += e - s;
                    evicting = true;
                } else {
                    v.error = "no marker for a flush whose entries are all missing";
                    return false;
                }
            } else if (s < e) {
                if (!started) {
                    v.evicted += e - s;
                    evicting = true;
                } else {
                    v.error = "entries of a flush missing, with no marker";
                    return false;
                }
            }
            break;
        case outcome::cut: {
            // The read holds a leading part of [marker][entries not dropped], with the last part
            // cut short.
            const bool frag = back_is(1, token::fragment);
            if (f.marker && frag && back_is(2, token::marker) && marker_fits(back(2), f)) {
                token& t = back(2);
                bool found = false;
                for (size_t k = s; k < e && !found; ++k) {
                    if ((!back(1).has_index || back(1).index == k) &&
                        truncates(back(1).text, E[k]) &&
                        in(sizes(m, s, k, min_digits, digits(static_cast<uint64_t>(t.ms))),
                           t.count)) {
                        found = true;
                        back(1).flush = static_cast<uint32_t>(g);
                        t.flush = static_cast<uint32_t>(g);
                                    ++v.markers;
                        ++v.truncated;
                        v.dropped += k - s;
                        v.lost += e - k - 1;
                    }
                }
                if (!found) {
                    v.error = "a marker and fragment of a cut flush that don't match its entries";
                    return false;
                }
                qi -= 2;
            } else if (f.marker && back_is(1, token::marker) && marker_fits(back(1), f)) {
                token& t = back(1);
                bool found = false;
                for (size_t p = s; p <= e && !found; ++p) {
                    if (in(sizes(m, s, p, min_digits, digits(static_cast<uint64_t>(t.ms))),
                           t.count)) {
                        found = true;
                        v.dropped += p - s;
                        v.lost += e - p;
                    }
                }
                if (!found) {
                    v.error = "a marker of a cut flush that counts no leading run of its entries";
                    return false;
                }
                t.flush = static_cast<uint32_t>(g);
                    ++v.markers;
                --qi;
            } else if (f.marker && frag && !back(1).has_index &&
                       truncates_marker(back(1).text, f)) {
                back(1).flush = static_cast<uint32_t>(g);
                ++v.truncated;
                --qi;
                count_missing(e - s);
            } else if (!f.marker && frag && s < e && (!back(1).has_index || back(1).index == s) &&
                       truncates(back(1).text, E[s])) {
                back(1).flush = static_cast<uint32_t>(g);
                ++v.truncated;
                --qi;
                count_missing(e - s - 1);
            } else if (!started && frag && qi == 1 && s < e) {
                // The read begins partway through what the flush wrote, and the flush's marker (if
                // any) and first entries were evicted. The fragment is one of the flush's entries,
                // cut short. This is allowed only when nothing older is in the read, because
                // nothing older survives the eviction.
                bool found = false;
                for (size_t k = s; k < e && !found; ++k) {
                    if ((!back(1).has_index || back(1).index == k) &&
                        truncates(back(1).text, E[k])) {
                        found = true;
                        back(1).flush = static_cast<uint32_t>(g);
                        ++v.truncated;
                        --qi;
                        v.evicted += e - s - 1;
                        evicting = true;
                    }
                }
                if (!found) {
                    count_missing(e - s);
                }
            } else {
                count_missing(e - s);
            }
            break;
        }
        }
        e = s;
    }
    if (evicting) {
        v.evicted += e - lo;
        e = lo;
    }
    if (qi > 0) {
        v.error = "a marker or fragment the model doesn't explain";
        return false;
    }
    if (e != lo) {
        v.error = "entries of an earlier flush missing";
        return false;
    }
    return true;
}

// Checks a read (or a copy of the newest entries) against the model, given that the flush attempts
// before `done` have taken their entries. The check is a pure function of its arguments.
verdict check(std::string_view text, const model& m, uint32_t done) {
    verdict v;
    // The read must be complete lines, each starting with a stamp or a tab, and the first line
    // must start with a stamp.
    if (!text.empty() && text.back() != '\n') {
        v.error = "the read doesn't end with a newline";
        return v;
    }
    for (size_t at = 0; at < text.size();) {
        const size_t end = text.find('\n', at);
        const char c = text[at];
        const bool stamp = c >= '0' && c <= '9';
        if (!stamp && c != '\t') {
            v.error = "a line that starts with neither a stamp nor a tab";
            return v;
        }
        if (stamp || v.tokens.empty()) {
            if (!stamp) {
                v.error = "the read begins on a continuation";
                return v;
            }
            v.tokens.push_back(token{.at = at});
        }
        token& t = v.tokens.back();
        t.size = end + 1 - t.at;
        t.text = text.substr(t.at, t.size - 1);
        at = end + 1;
    }
    for (token& t : v.tokens) {
        parse_stamp(t);
        if (t.stamped && t.body.starts_with(marker_head)) {
            size_t at = marker_head.size();
            uint64_t n = 0;
            if (parse_number(t.body, at, n) && t.body.substr(at) == marker_tail) {
                t.kind = token::marker;
                t.count = n;
                continue;
            }
        }
        // Without a complete id, the token can only be a fragment. explain() matches it with the
        // entry or marker that it would be the start of.
        const auto n = t.stamped ? id_in(t.body, m.run) : std::nullopt;
        if (!n) {
            t.kind = token::fragment;
            continue;
        }
        const auto it = std::ranges::lower_bound(m.entries, *n, {}, &entry::n);
        if (it == m.entries.end() || it->n != *n) {
            // The id isn't any entry's own. The line is then the one entry that carries the id and
            // has the line's text and a window that holds its stamp, or a fragment of an entry
            // that carries the id.
            bool carried = false;
            bool prefix = false;
            for (size_t i = 0; i < m.entries.size() && !t.has_index; ++i) {
                const entry& e = m.entries[i];
                if (e.carried != *n) {
                    continue;
                }
                carried = true;
                if (t.body == e.body && t.boot == e.boot && t.wake == e.wake && t.ms >= e.ms_lo &&
                    t.ms <= e.ms_hi) {
                    t.has_index = true;
                    t.index = i;
                    t.kind = token::kept;
                }
                prefix = prefix ||
                    (t.body.size() < e.body.size() && std::string_view(e.body).starts_with(t.body));
            }
            if (t.has_index) {
                continue;
            }
            if (!carried) {
                v.error = "an entry that isn't in the model, or no longer can be read";
                return v;
            }
            if (!prefix) {
                v.error = "a line of a suppressed fault that none of its entries is";
                return v;
            }
            t.kind = token::fragment;
            continue;
        }
        t.has_index = true;
        t.index = static_cast<size_t>(it - m.entries.begin());
        if (t.body == it->body) {
            t.kind = token::kept;
        } else if (t.body.size() < it->body.size() &&
                   std::string_view(it->body).starts_with(t.body)) {
            t.kind = token::fragment;
        } else {
            v.error = "an entry whose text was altered";
            return v;
        }
    }

    // The entries that a flush has taken.
    size_t eligible = 0;
    while (eligible < m.entries.size() && m.entries[eligible].flush < done) {
        ++eligible;
    }
    size_t next = 0;
    const token* prev = nullptr;
    std::vector<size_t> items;
    for (size_t i = 0; i < v.tokens.size(); ++i) {
        token& t = v.tokens[i];
        if (!is(t, token::kept)) {
            items.push_back(i);
            continue;
        }
        const entry& e = m.entries[t.index];
        if (t.index >= eligible) {
            v.error = "an entry no flush has taken";
            return v;
        }
        if (t.index < next) {
            v.error = "an entry out of order, or twice";
            return v;
        }
        if (t.boot != e.boot || t.wake != e.wake || t.ms < e.ms_lo || t.ms > e.ms_hi) {
            v.error = "an entry whose stamp isn't when and where it was written";
            return v;
        }
        const outcome out = m.at(e.flush).out;
        if (out == outcome::lost || out == outcome::corrupt || out == outcome::corrupt_full) {
            v.error = "an entry of a flush that wrote none";
            return v;
        }
        if (!explain(m, done, prev, &t, next, t.index, items, v)) {
            return v;
        }
        t.flush = e.flush;
        ++v.kept;
        v.continued += e.body.find("\n\t") != std::string::npos ? 1 : 0;
        v.captured += e.captured ? 1 : 0;
        v.last_n = e.n;
        if (prev != nullptr && i > 0 && &v.tokens[i - 1] == prev &&
            m.entries[prev->index].flush == e.flush && i + 1 < v.tokens.size() &&
            is(v.tokens[i + 1], token::kept) && m.entries[v.tokens[i + 1].index].flush == e.flush) {
            v.removable.push_back(i);
        }
        prev = &t;
        next = t.index + 1;
        items.clear();
    }
    if (!explain(m, done, prev, nullptr, next, eligible, items, v)) {
        return v;
    }
    v.lowest_flush = done;
    for (const token& t : v.tokens) {
        v.lowest_flush = std::min(v.lowest_flush, t.flush);
    }
    return v;
}

// The read with a token taken out, doubled, or, for a marker, with its count changed by `by`.
std::string without_token(std::string_view text, const token& t) {
    return std::string(text.substr(0, t.at)) + std::string(text.substr(t.at + t.size));
}

std::string doubled(std::string_view text, const token& t) {
    return std::string(text.substr(0, t.at + t.size)) + std::string(text.substr(t.at, t.size)) +
        std::string(text.substr(t.at + t.size));
}

std::string recounted(std::string_view text, const token& t, int by) {
    const std::string line = std::string(t.text.substr(0, t.text.size() - t.body.size())) +
        std::string(marker_head) + std::to_string(static_cast<int64_t>(t.count) + by) +
        std::string(marker_tail) + "\n";
    return std::string(text.substr(0, t.at)) + line + std::string(text.substr(t.at + t.size));
}

// =============================================================================
// The store, and the faults it injects
// =============================================================================

struct control {
    // Every call fails, as with the power off.
    bool fail_all = false;
    // At store write number `reset_at` after arming, the ring and the hint are saved as a reset
    // would leave them, and the power is cut `reset_permille` thousandths of the way into that
    // write.
    bool reset_armed = false;
    size_t reset_at = 0;
    // The power is cut during the next erase, so only the second half of the sector is erased.
    bool erase_cut_armed = false;
    uint32_t reset_permille = 0;
    size_t writes = 0;
    bool reset_taken = false;
    internal::ring_type ring;
    internal::hint_record hint;
};

class model_store final : public pl::sector_store {
public:
    model_store(std::shared_ptr<pl::ram_sector_store> ram, std::shared_ptr<control> ctl)
        : sector_store(ram->size(), ram->sector_size(), ram->write_size())
        , _ram(std::move(ram))
        , _ctl(std::move(ctl)) {}

private:
    idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
        if (_ctl->fail_all) {
            return idfxx::error(idfxx::errc::invalid_state);
        }
        return _ram->try_read(offset, dst);
    }
    idfxx::result<void> do_read_raw(size_t offset, idfxx::byte_span dst) const override {
        if (_ctl->fail_all) {
            return idfxx::error(idfxx::errc::invalid_state);
        }
        return _ram->try_read_raw(offset, dst);
    }
    idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
        if (_ctl->fail_all) {
            return idfxx::error(idfxx::errc::invalid_state);
        }
        if (_ctl->reset_armed && !_ctl->reset_taken && _ctl->writes++ == _ctl->reset_at) {
            _ctl->ring = internal::rtc_ring();
            _ctl->hint = internal::rtc().hint;
            _ctl->reset_taken = true;
            _ram->cut_after(src.size() * _ctl->reset_permille / 1000);
        }
        return _ram->try_write(offset, src);
    }
    idfxx::result<void> do_erase_sector(size_t index) override {
        if (_ctl->fail_all) {
            return idfxx::error(idfxx::errc::invalid_state);
        }
        if (_ctl->erase_cut_armed) {
            _ctl->erase_cut_armed = false;
            _ram->cut_after(0);
        }
        return _ram->try_erase_sector(index);
    }

    std::shared_ptr<pl::ram_sector_store> _ram;
    std::shared_ptr<control> _ctl;
};

// =============================================================================
// A run
// =============================================================================

// Counts of what the runs did, across all runs.
struct tally {
    size_t runs = 0;
    size_t ops = 0;
    size_t reads = 0;
    size_t newest_copies = 0;
    size_t clears = 0;
    size_t restarts = 0;
    size_t appends = 0;
    size_t near_max = 0;
    size_t captures = 0;
    size_t kept = 0;
    size_t evicted = 0;
    size_t dropped = 0;
    size_t lost = 0;
    size_t truncated = 0;
    size_t markers = 0;
    size_t continued = 0;
    size_t captured = 0;
    size_t overruns = 0;
    size_t fault_write = 0;
    size_t fault_erase = 0;
    size_t fault_reset = 0;
    size_t fault_power = 0;
    size_t corrupt_start = 0;
    size_t corrupt_flush = 0;
    size_t corrupt_full = 0;
    size_t mutations = 0;
    size_t final_kept = 0;
    size_t fault_runs = 0;
    size_t faults_recorded = 0;
    size_t faults_checked = 0;
    size_t bad_kinds = 0;
    size_t not_supported = 0;
    size_t context_ring_only = 0;
    size_t context_with_flash = 0;
    size_t fault_reads = 0;
    size_t fault_counts = 0;
    size_t fault_clears = 0;
    size_t overwritten = 0;
    size_t suppressed_recorded = 0;
    size_t suppressed_repeats = 0;
    size_t other_detail_repeats = 0;
    size_t lifted = 0;
    size_t lift_noop = 0;
    size_t evictions = 0;
    size_t kept_across_reset = 0;
    size_t forgotten_by_power = 0;
    size_t entry_reads = 0;
    size_t entries = 0;
    size_t empty_lines = 0;
    size_t newest_reads = 0;
};

tally s_tally;

#ifdef CONFIG_IDFXX_STD_FORMAT
constexpr std::array<const char*, 3> tags = {"pl_m0", "pl_m1", "pl_m2"};
#endif

// A recorded fault, as the log lists it.
struct recorded_fault {
    uint32_t seq = 0;
    std::string kind;
    std::string detail;
    uint32_t boot = 0;
    uint32_t wake = 0;
    int64_t ms_lo = 0;
    int64_t ms_hi = 0;
    // The body of the fault's own entry. The fault's context ends with this entry.
    std::string body;
    // The fault's context, when the record was checked in full.
    std::optional<std::string> context;
};

// A kind that the log remembers as suppressed, with the detail of the fault that suppressed it.
struct remembered {
    std::string kind;
    std::string detail;
    uint64_t order = 0;
    // The number of restarts and resets so far when the kind was remembered.
    uint32_t epoch = 0;
};

struct faults_model {
    bool on = false;
    size_t slot_size = 0;
    size_t slots = 0;
    // The sequence number that the next record or clear takes.
    uint32_t next_seq = 0;
    // The faults recorded since the last clear, oldest first. The store lists the last `slots` of
    // them.
    std::vector<recorded_fault> records;
    // The remembered suppressed faults, at most CONFIG_IDFXX_PERSISTENT_LOG_SUPPRESSED_FAULTS.
    std::vector<remembered> memory;
    uint64_t next_order = 0;
    // The number of restarts and resets. A kind remembered before one of them and repeated after
    // it was kept across it.
    uint32_t epoch = 0;
    // The kinds that a power cut forgot, so that the model can tell when the same fault is
    // recorded again after the cut.
    std::vector<remembered> forgotten;
    // For each suppressed kind in the pool, the end of the window of its last line, so that its
    // next line is stamped later.
    std::vector<int64_t> last_hi;
    // For each suppressed kind in the pool, its two details.
    std::vector<std::array<std::string, 2>> details;

    [[nodiscard]] size_t room() const { return slot_size - 512; }
};

struct run {
    uint32_t seed = 0;
    prng rng{0};
    model m;
    std::shared_ptr<pl::ram_sector_store> ram;
    std::shared_ptr<control> ctl;
    size_t sectors = 0;
    size_t sector_size = 0;
    bool encrypted = false;
    uint32_t boot = 0;
    uint32_t wake = 0;
    uint32_t next_n = 0;
    int op = 0;
    const char* op_name = "";
    // The fewest and the most bytes that the entries in the ring since the last flush attempt
    // can take.
    uint64_t pending_lo = 0;
    uint64_t pending_hi = 0;
    // The last full read checked, for a failure's report.
    std::string last_read;
    // The number of sectors that the log has. The fault slots, if any, take the rest.
    size_t log_sectors = 0;
    faults_model f;
};

[[noreturn]] void fail(const run& r, const std::string& why, std::string_view read = {}) {
    std::printf(
        "persistent_log model: seed %u op %d (%s), %s store of %u x %u: %s\n",
        unsigned(r.seed),
        r.op,
        r.op_name,
        r.encrypted ? "encrypted" : "plain",
        unsigned(r.sectors),
        unsigned(r.sector_size),
        why.c_str()
    );
    if (r.f.on) {
        std::printf(
            "log sectors %u, %u fault slots of %u\n",
            unsigned(r.log_sectors),
            unsigned(r.f.slots),
            unsigned(r.f.slot_size)
        );
    }
    if (!read.empty()) {
        std::printf(
            "---- read (%u bytes) ----\n%.*s---- end ----\n",
            unsigned(read.size()),
            int(read.size()),
            read.data()
        );
    }
    std::printf(
        "model: %u entries, flushes from %u to %u\n",
        unsigned(r.m.entries.size()),
        unsigned(r.m.floor),
        unsigned(r.m.attempts())
    );
    for (const entry& e : r.m.entries) {
        const flush_attempt& f =
            e.flush >= r.m.base && e.flush < r.m.attempts() ? r.m.at(e.flush) : flush_attempt{};
        std::printf(
            "  #%u flush %u (%d%s) %u.%u +[%lld,%lld] %.40s\n",
            unsigned(e.n),
            unsigned(e.flush),
            int(f.out),
            f.marker ? " marker" : "",
            unsigned(e.boot),
            unsigned(e.wake),
            static_cast<long long>(e.ms_lo),
            static_cast<long long>(e.ms_hi),
            e.body.c_str()
        );
    }
    const size_t from_flush = r.m.entries.empty()
        ? r.m.floor
        : std::max<size_t>(
              r.m.floor, r.m.entries.front().flush > 12 ? r.m.entries.front().flush - 12 : 0
          );
    for (size_t i = std::max<size_t>(from_flush, r.m.base); i < r.m.attempts(); ++i) {
        const flush_attempt& f = r.m.at(i);
        std::printf(
            "  flush %u: %d%s %u.%u +[%lld,%lld]\n",
            unsigned(i),
            int(f.out),
            f.marker ? " marker" : "",
            unsigned(f.boot),
            unsigned(f.wake),
            static_cast<long long>(f.ms_lo),
            static_cast<long long>(f.ms_hi)
        );
    }
    std::printf(
        "---- last full read (%u bytes) ----\n%s---- end ----\n",
        unsigned(r.last_read.size()),
        r.last_read.c_str()
    );
    // The store's sectors as stored, when they aren't encrypted.
    if (!r.encrypted) {
        const auto bytes = r.ram->bytes();
        for (size_t sec = 0; sec < r.sectors; ++sec) {
            std::printf("== sector %u:", unsigned(sec));
            for (size_t i = 0; i < r.sector_size; ++i) {
                const auto c = static_cast<unsigned char>(bytes[sec * r.sector_size + i]);
                if (c >= 32 && c < 127) {
                    std::printf("%c", c);
                } else if (c == '\n') {
                    std::printf("\\n");
                } else if (c == '\t') {
                    std::printf("\\t");
                } else {
                    std::printf("<%02x>", c);
                }
            }
            std::printf("\n");
        }
    }
    TEST_FAIL_MESSAGE("persistent_log model check failed");
    std::abort();
}

void expect(const run& r, bool ok, const char* why) {
    if (!ok) {
        fail(r, why);
    }
}

pl::config run_config(const run& r) {
    pl::config c;
    c.storage = std::make_unique<model_store>(r.ram, r.ctl);
    c.boot_count = r.boot;
    c.wall_clock = [] { return std::optional<std::chrono::system_clock::time_point>{}; };
    c.writer = std::nullopt;
#ifdef CONFIG_IDFXX_STD_FORMAT
    c.capture = pl::capture_config{
        .level = idfxx::log::level::none,
        .tag_levels = {
            {tags[0], idfxx::log::level::warn},
            {tags[1], idfxx::log::level::warn},
            {tags[2], idfxx::log::level::warn}
        },
    };
#endif
    c.faults = r.f.on
        ? std::optional(pl::fault_config{.slot_size = r.f.slot_size, .slot_count = r.f.slots})
        : std::nullopt;
    return c;
}

void start(run& r) {
    expect(r, pl::try_start(run_config(r)).has_value(), "try_start failed");
    r.wake = internal::rtc().wake.wake;
    ++r.f.epoch;
}

std::string random_chars(run& r, size_t n, uint32_t newline_percent) {
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789 ";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += r.rng.chance(newline_percent) ? '\n' : alphabet[r.rng.below(sizeof alphabet - 1)];
    }
    return s;
}

std::string id_of(const run& r, uint32_t n) {
    return "<#" + std::to_string(r.m.run) + "." + std::to_string(n) + ">";
}

void add_entry(
    run& r,
    uint32_t n,
    std::string body,
    int64_t lo,
    int64_t hi,
    bool captured,
    std::optional<uint32_t> carried = std::nullopt
) {
    entry e{
        .n = n,
        .body = std::move(body),
        .boot = r.boot,
        .wake = r.wake,
        .ms_lo = lo,
        .ms_hi = hi,
        .flush = r.m.attempts(),
        .captured = captured,
        .carried = carried,
    };
    r.pending_lo += fixed_size(e) + digits(static_cast<uint64_t>(lo));
    r.pending_hi += fixed_size(e) + digits(static_cast<uint64_t>(hi));
    r.m.entries.push_back(std::move(e));
}

void append_entry(run& r, size_t length) {
    const uint32_t n = r.next_n++;
    std::string text;
    if (r.rng.chance(10)) {
        text.append(1 + r.rng.below(2), '\n');
    }
    text += id_of(r, n);
    const uint32_t newlines = r.rng.chance(30) ? 1 + r.rng.below(10) : 0;
    text += random_chars(r, length > text.size() ? length - text.size() : 0, newlines);
    if (r.rng.chance(10)) {
        text += '\n';
    }
    const auto body = append_body(text);
    const int64_t lo = now_ms();
    pl::append(text);
    const int64_t hi = now_ms();
    ++s_tally.appends;
    s_tally.near_max += text.size() + 8 >= max_line ? 1 : 0;
    if (body) {
        add_entry(r, n, *body, lo, hi, false);
    }
}

size_t random_length(run& r) {
    const uint32_t kind = r.rng.below(100);
    if (kind < 60) {
        return r.rng.below(48);
    }
    if (kind < 85) {
        return 40 + r.rng.below(160);
    }
    return max_line - 8 + r.rng.below(32);
}

#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
void capture_entry(run& r) {
    const uint32_t n = r.next_n++;
    const char* tag = tags[r.rng.below(tags.size())];
    std::string msg =
        id_of(r, n) + " " + random_chars(r, r.rng.below(120), r.rng.chance(25) ? 6 : 0);
    // The message never ends with a newline, because only the last of several calls may end with
    // one.
    while (msg.ends_with('\n')) {
        msg.back() = 'z';
    }
    const bool warn = r.rng.chance(50);
    const esp_log_level_t level = warn ? ESP_LOG_WARN : ESP_LOG_ERROR;
    const char letter = warn ? 'W' : 'E';
    const uint32_t form = r.rng.below(4);
    const int64_t lo = now_ms();
    switch (form) {
    case 0:
        // The line as ESP_LOGx makes it, which is a v2 line under log v2.
        if (warn) {
            ESP_LOGW(tag, "%s", msg.c_str());
        } else {
            ESP_LOGE(tag, "%s", msg.c_str());
        }
        break;
    case 1: {
        // A line written in several calls: its prefix, then the message in pieces, then its
        // newline.
        esp_log_write(level, tag, "%c (%d) %s: ", letter, int(r.rng.below(100000)), tag);
        size_t from = 0;
        const uint32_t pieces = 1 + r.rng.below(3);
        for (uint32_t p = 0; p < pieces && from < msg.size(); ++p) {
            size_t to = p + 1 == pieces
                ? msg.size()
                : from + 1 + r.rng.below(static_cast<uint32_t>(msg.size() - from));
            while (to < msg.size() && msg[to - 1] == '\n') {
                ++to;
            }
            const std::string piece = msg.substr(from, to - from);
            esp_log_write(level, tag, "%s", piece.c_str());
            from = to;
        }
        if (from < msg.size()) {
            const std::string piece = msg.substr(from);
            esp_log_write(level, tag, "%s", piece.c_str());
        }
        esp_log_write(level, tag, "%s", "\n");
        break;
    }
    case 2:
        // A line in log v2's form, which esp_log() logs with ESP_LOG_CONFIG_REQUIRE_FORMATTING. Its
        // format makes only the message, because log v2 adds the prefix and the newline itself.
        esp_log(
            ESP_LOG_CONFIG_INIT(level | ESP_LOG_CONFIG_REQUIRE_FORMATTING), tag, "%s", msg.c_str()
        );
        break;
    default:
        // A complete line in one esp_log_write call.
        esp_log_write(
            level, tag, "%c (%d) %s: %s\n", letter, int(r.rng.below(100000)), tag, msg.c_str()
        );
        break;
    }
    const int64_t hi = now_ms();
    ++s_tally.captures;
    add_entry(r, n, entry_body(std::string(1, letter) + " " + tag + ": " + msg), lo, hi, true);
}
#endif

// Begins a flush attempt. Records whether the ring reports a loss, and checks that against the
// bytes that the model has in the ring.
flush_attempt begin_flush(run& r, bool check_overrun = true) {
    flush_attempt f;
    f.marker = internal::rtc_ring().pending().lost > 0;
    if (check_overrun) {
        expect(
            r,
            !(r.pending_lo > ring_size && !f.marker),
            "the ring held more than its size and dropped nothing"
        );
        expect(
            r,
            !(r.pending_hi <= ring_size && f.marker),
            "the ring dropped bytes though it had room for them"
        );
    }
    s_tally.overruns += f.marker ? 1 : 0;
    f.boot = r.boot;
    f.wake = r.wake;
    f.ms_lo = now_ms();
    return f;
}

void end_flush(run& r, flush_attempt f, outcome out) {
    f.out = out;
    f.ms_hi = now_ms();
    r.m.flushes.push_back(f);
    r.pending_lo = 0;
    r.pending_hi = 0;
}

void flush_op(run& r) {
    auto f = begin_flush(r);
    const bool ok = pl::try_flush().has_value();
    end_flush(r, f, outcome::ok);
    expect(r, ok, "try_flush failed");
}

void flush_if_pending_op(run& r) {
    const size_t threshold = 1 + r.rng.below(ring_size);
    const size_t waiting = internal::rtc_ring().unflushed();
    const bool flushes = waiting >= threshold;
    flush_attempt f;
    if (flushes) {
        f = begin_flush(r);
    }
    const bool ok = pl::try_flush_if_pending(threshold).has_value();
    if (flushes) {
        end_flush(r, f, outcome::ok);
    }
    expect(r, ok, "try_flush_if_pending failed");
    expect(
        r,
        internal::rtc_ring().unflushed() == (flushes ? 0 : waiting),
        "try_flush_if_pending flushed against its threshold"
    );
}

void stop_op(run& r, outcome out = outcome::ok) {
    auto f = begin_flush(r);
    const bool ok = pl::try_stop().has_value();
    end_flush(r, f, out);
    expect(r, ok || out == outcome::lost, "try_stop failed");
}

// Checks a read, then checks that the checker rejects it with a kept entry taken out, doubled, or
// a marker's count off by one. Returns the id of the last entry kept, or -1.
int64_t check_read(run& r, const std::string& text, bool full) {
    const uint32_t done = r.m.attempts();
    verdict v = check(text, r.m, done);
    if (!v.error.empty()) {
        fail(r, v.error, text);
    }
    s_tally.kept += v.kept;
    s_tally.evicted += v.evicted;
    s_tally.dropped += v.dropped;
    s_tally.lost += v.lost;
    s_tally.truncated += v.truncated;
    s_tally.markers += v.markers;
    s_tally.continued += v.continued;
    s_tally.captured += v.captured;

    if (r.rng.chance(50)) {
        const auto rejects = [&](const std::string& mutated, const char* what) {
            if (check(mutated, r.m, done).error.empty()) {
                fail(r, std::string("the checker took a read with ") + what, mutated);
            }
            ++s_tally.mutations;
        };
        if (!v.removable.empty()) {
            rejects(
                without_token(text, v.tokens[v.removable[r.rng.below(v.removable.size())]]),
                "a kept entry taken out"
            );
        }
        if (v.kept > 0) {
            std::vector<size_t> kept;
            for (size_t i = 0; i < v.tokens.size(); ++i) {
                if (is(v.tokens[i], token::kept)) {
                    kept.push_back(i);
                }
            }
            rejects(
                doubled(text, v.tokens[kept[r.rng.below(kept.size())]]), "a kept entry doubled"
            );
        }
        if (!v.exact.empty()) {
            const token& t = v.tokens[v.exact[r.rng.below(v.exact.size())]];
            rejects(recounted(text, t, 1), "a marker counting a byte more");
            if (t.count > 0) {
                rejects(recounted(text, t, -1), "a marker counting a byte less");
            }
        }
    }

    // Nothing earlier than the read's start can be read again.
    if (full) {
        r.last_read = text;
        const uint32_t below = v.lowest_flush;
        std::erase_if(r.m.entries, [below](const entry& e) { return e.flush < below; });
        r.m.floor = std::max(r.m.floor, below);
        r.m.forget();
    }
    return v.last_n;
}

// An entry as std::format writes it: as it is stored, without its final newline.
std::string formatted(const pl::entry& e) {
#ifdef CONFIG_IDFXX_STD_FORMAT
    return std::format("{}", e);
#else
    return std::string(pl::detail::entry_access::stored(e));
#endif
}

// The stored text of each entry that a reader returns.
std::vector<std::string> stored_entries(run& r, pl::entry_reader& entries) {
    std::vector<std::string> out;
    pl::entry e;
    while (true) {
        auto more = entries.try_next(e);
        expect(r, more.has_value(), "the entry reader's try_next failed");
        if (!*more) {
            return out;
        }
        out.emplace_back(pl::detail::entry_access::stored(e));
    }
}

// The entry stored as `stored`.
pl::entry parsed(const std::string& stored) {
    std::string_view rest = stored;
    pl::entry e;
    (void)pl::next_entry(rest, e);
    return e;
}

// Checks the entries that an entry reader returned for the same log as a text reader:
// - Each entry, formatted, and the entries joined with newlines, are the text that the text reader
//   returned, byte for byte. The text's empty lines are left out, because the entry reader skips
//   them, and so is the newline at the text's end.
// - One for one, the entries are those that next_entry() takes from the text, with the same stamp
//   and text.
// An entry that doesn't fit in the entry reader's buffer would be returned in parts, joined by
// newlines that the text doesn't have. No entry in a run is that long, and the check also checks
// that.
void check_entries(run& r, std::string_view text, const std::vector<std::string>& entries) {
    std::string joined;
    for (const auto& stored : entries) {
        const pl::entry e = parsed(stored);
        const std::string one = formatted(e);
        expect(r, one == stored, "an entry formats as other than it is stored");
        expect(r, one.size() < pl::max_entry_size, "an entry is longer than the longest entry");
        joined += one;
        joined += '\n';
        ++s_tally.entries;
    }
    if (!joined.empty()) {
        joined.pop_back();
    }
    std::string want;
    bool at_line = true;
    for (char c : text) {
        if (at_line && c == '\n') {
            ++s_tally.empty_lines;
            continue;
        }
        want += c;
        at_line = c == '\n';
    }
    if (!want.empty() && want.back() == '\n') {
        want.pop_back();
    }
    if (joined != want) {
        fail(r, "the entries, joined, differ from the text: " + joined, text);
    }

    std::string_view rest = text;
    pl::entry split;
    size_t i = 0;
    while (pl::next_entry(rest, split)) {
        expect(r, i < entries.size(), "the text holds more entries than the entry reader returned");
        const pl::entry e = parsed(entries[i]);
        const bool same = e.stamped() == split.stamped() && e.boot() == split.boot() &&
            e.wake() == split.wake() && e.uptime() == split.uptime() &&
            e.wall_time() == split.wall_time() && e.text() == split.text();
        if (!same) {
            fail(r, "entry " + std::to_string(i) + " differs from the text's: " + entries[i], text);
        }
        ++i;
    }
    expect(r, i == entries.size(), "the entry reader returned more entries than the text holds");
    ++s_tally.entry_reads;
}

// Checks that read_newest() returns the last entries of a read of the same log.
void check_newest(run& r, const std::vector<std::string>& all) {
    const std::array<size_t, 4> counts = {
        0, 1, r.rng.below(static_cast<uint32_t>(all.size()) + 2), all.size() + 1
    };
    for (size_t count : counts) {
        auto newest = pl::try_read_newest(count);
        expect(r, newest.has_value(), "try_read_newest failed");
        if (stored_entries(r, *newest) != pl::testing::last_of(all, count)) {
            fail(r, "read_newest(" + std::to_string(count) + ") isn't the newest entries");
        }
        ++s_tally.newest_reads;
    }
}

int64_t read_op(run& r) {
    auto f = begin_flush(r);
    auto reader = pl::try_read_text();
    end_flush(r, f, outcome::ok);
    expect(r, reader.has_value(), "try_read_text failed");
    std::string text;
    {
        std::vector<char> buf(512);
        bool short_seen = false;
        while (true) {
            const size_t want =
                1 + r.rng.below(r.rng.chance(30) ? 16 : static_cast<uint32_t>(buf.size()));
            auto got = reader->try_next(std::span<char>(buf.data(), want));
            expect(r, got.has_value(), "try_next failed");
            if (got->empty()) {
                break;
            }
            expect(r, got->data() == buf.data(), "try_next returned text outside its buffer");
            expect(r, !short_seen, "try_next gave more after giving less than asked");
            short_seen = got->size() < want;
            text.append(*got);
        }
    }
    ++s_tally.reads;
    // Nothing is waiting in the ring after the text reader's flush, and nothing has changed the
    // log since, so the entry readers read the same log. They and their entries are freed before
    // the text is checked, because the check needs memory of its own.
    {
        auto entries = pl::try_read();
        expect(r, entries.has_value(), "try_read failed");
        const auto all = stored_entries(r, *entries);
        check_entries(r, text, all);
        if (r.rng.chance(10)) {
            check_newest(r, all);
        }
    }
    return check_read(r, text, true);
}

void copy_newest_op(run& r) {
    std::string buf(1 + r.rng.below(4096), '\0');
    // copy_newest() flushes first.
    auto f = begin_flush(r);
    auto got = pl::try_copy_newest(buf);
    end_flush(r, f, outcome::ok);
    expect(r, got.has_value(), "try_copy_newest failed");
    expect(r, got->data() == buf.data(), "try_copy_newest returned text past its buffer");
    buf.resize(got->size());
    ++s_tally.newest_copies;
    check_read(r, buf, false);
}

void clear_op(run& r) {
    expect(r, pl::try_clear().has_value(), "try_clear failed");
    r.m.entries.clear();
    r.m.floor = r.m.attempts();
    r.m.forget();
    r.pending_lo = 0;
    r.pending_hi = 0;
    ++s_tally.clears;
}

void restart_op(run& r) {
    stop_op(r);
    if (r.rng.chance(50)) {
        r.boot = r.rng.below(100000);
    }
    start(r);
    ++s_tally.restarts;
}

// Makes sure the ring holds an entry, so that a flush has something to write.
void ensure_pending(run& r) {
    if (internal::rtc_ring().unflushed() == 0) {
        append_entry(r, random_length(r));
    }
}

// (a) A store write fails partway through a flush. Then the log is stopped and started again.
void write_fault_op(run& r) {
    ensure_pending(r);
    const size_t budget = r.rng.below(static_cast<uint32_t>(internal::rtc_ring().unflushed()));
    auto f = begin_flush(r);
    r.ram->cut_after(budget);
    const bool ok = pl::try_flush().has_value();
    r.ram->restore_power();
    end_flush(r, f, ok ? outcome::ok : outcome::cut);
    if (ok) {
        return;
    }
    ++s_tally.fault_write;
    stop_op(r);
    start(r);
}

// (a') A power cut comes during the erase of the sector that a flush starts. Then the log is
// stopped and started again. Enough bytes are waiting in the ring for the flush to fill the head
// sector and start the next sector.
void erase_fault_op(run& r) {
    ensure_pending(r);
    const size_t enough = std::min(r.ram->sector_size(), ring_size / 2);
    while (internal::rtc_ring().unflushed() < enough) {
        append_entry(r, random_length(r));
    }
    auto f = begin_flush(r);
    r.ctl->erase_cut_armed = true;
    const bool ok = pl::try_flush().has_value();
    r.ctl->erase_cut_armed = false;
    const bool erasing = r.ram->cut() && r.ram->cut_erasing();
    r.ram->restore_power();
    end_flush(r, f, ok ? outcome::ok : outcome::cut);
    if (ok) {
        return;
    }
    if (erasing) {
        ++s_tally.fault_erase;
    }
    stop_op(r);
    start(r);
}

// (b) A reset comes during a flush, and RTC memory is kept. The ring and the hint are as they were
// at one of the flush's store writes, and the power is cut partway through that write or just
// after it.
void reset_fault_op(run& r) {
    ensure_pending(r);
    r.ctl->reset_armed = true;
    r.ctl->reset_taken = false;
    r.ctl->writes = 0;
    r.ctl->reset_at = r.rng.below(3);
    r.ctl->reset_permille = r.rng.chance(30) ? 1000 : r.rng.below(1000);
    auto f = begin_flush(r);
    const bool ok = pl::try_flush().has_value();
    r.ctl->reset_armed = false;
    r.ram->restore_power();
    if (!r.ctl->reset_taken) {
        end_flush(r, f, outcome::ok);
        expect(r, ok, "try_flush failed");
        return;
    }
    end_flush(r, f, outcome::cut);
    ++s_tally.fault_reset;
    stop_op(r);
    internal::rtc_ring() = r.ctl->ring;
    internal::rtc().hint = r.ctl->hint;
    r.boot = r.boot + 1;
    start(r);
}

void fill_random(run& r, void* p, size_t n) {
    auto* b = static_cast<uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        b[i] = static_cast<uint8_t>(r.rng.next());
    }
}

// (c) The power is lost. Nothing in the ring is flushed, and RTC memory holds noise at the next
// boot.
void power_fault_op(run& r) {
    r.ctl->fail_all = true;
    stop_op(r, outcome::lost);
    r.ctl->fail_all = false;
    auto& ring = internal::rtc_ring();
    fill_random(r, &ring, sizeof ring);
    if (ring.magic == internal::ring_type::ring_magic) {
        ring.magic = ~ring.magic;
    }
    fill_random(r, &internal::rtc().hint, sizeof internal::rtc().hint);
    // The remembered suppressed faults are lost with the rest of RTC memory.
    fill_random(r, &internal::rtc().faults, sizeof internal::rtc().faults);
    if (internal::rtc().faults.magic == internal::fault_record::record_magic) {
        internal::rtc().faults.magic = ~internal::fault_record::record_magic;
    }
    internal::check_rtc(false);
    r.f.forgotten = std::move(r.f.memory);
    r.f.memory.clear();
    r.boot = r.boot + 1;
    start(r);
    ++s_tally.fault_power;
}

uint32_t ring_index(uint32_t index, size_t by) {
    return static_cast<uint32_t>((uint64_t{index} + by) % internal::ring_type::modulus);
}

// (d) After a clean stop, start finds that the ring isn't valid. Nothing is lost.
void corrupt_start_op(run& r) {
    stop_op(r);
    auto& ring = internal::rtc_ring();
    if (r.rng.chance(50)) {
        // As a power-on can leave it, with the magic correct by chance and the indices spanning
        // more than the ring holds.
        std::memset(ring.data, 'g', sizeof ring.data);
        ring.flushed = ring.begin;
        ring.head = ring_index(ring.begin, sizeof ring.data + 1);
    } else {
        fill_random(r, &ring, sizeof ring);
        if (ring.magic == internal::ring_type::ring_magic) {
            ring.magic = ~ring.magic;
        }
    }
    start(r);
    ++s_tally.corrupt_start;
}

// (d) A flush finds that the ring isn't valid. A marker counts what the ring held.
void corrupt_flush_op(run& r) {
    auto& ring = internal::rtc_ring();
    const bool lossless = ring.pending().lost == 0;
    const size_t waiting = ring.unflushed();
    const bool full = !lossless || r.rng.chance(33);
    auto f = begin_flush(r, lossless);
    if (full) {
        // The indices span a byte more than the ring holds, so the marker counts the ring's size.
        ring.flushed = ring.begin;
        ring.head = ring_index(ring.begin, sizeof ring.data + 1);
        f.marker = true;
        ++s_tally.corrupt_full;
    } else {
        // The magic is wrong, or the oldest index is past the newest. The indices of the unflushed
        // bytes are intact.
        if (r.rng.chance(50)) {
            ring.magic ^= 1;
        } else {
            ring.begin = ring_index(ring.head, 1);
        }
        f.marker = waiting > 0;
        ++s_tally.corrupt_flush;
    }
    expect(r, !ring.valid(), "the corrupted ring is valid");
    const bool ok = pl::try_flush().has_value();
    end_flush(r, f, full ? outcome::corrupt_full : outcome::corrupt);
    expect(r, ok, "try_flush failed on a corrupted ring");
    expect(r, ring.valid() && ring.unflushed() == 0, "the flush left the ring not valid");
}

// =============================================================================
// Faults
// =============================================================================

template<typename T>
bool fails_with(const idfxx::result<T>& r, idfxx::errc e) {
    return !r.has_value() && r.error() == e;
}

constexpr std::string_view fault_head = "E persistent_log: fault kind=";
constexpr std::string_view repeat_head = "W persistent_log: fault repeat kind=";
constexpr std::string_view lifted_head = "I persistent_log: fault suppression lifted kind=";
constexpr size_t suppressed_capacity = CONFIG_IDFXX_PERSISTENT_LOG_SUPPRESSED_FAULTS;
// More suppressed kinds than the log can remember, so that some kinds are replaced by others.
constexpr size_t suppressed_pool = suppressed_capacity + 4;
// The ids that the suppressed kinds carry. These are never an entry's own id.
constexpr uint32_t pool_id = 1000000;

std::string kept_detail(std::string_view detail) {
    return std::string(detail.substr(0, 159));
}

// Returns the body of the entry that a fault line keeps: the head, the kind and the kept detail,
// shortened to 219 bytes or to max_line, whichever is smaller, then folded as an entry's lines are.
// 219 bytes is the size of the longest fault line: the repeat head, a 23-character kind, a space
// and a 159-byte detail.
std::string fault_body(std::string_view head, std::string_view kind, std::string_view detail) {
    std::string raw = std::string(head) + std::string(kind);
    if (!detail.empty()) {
        raw += ' ';
        raw += detail;
    }
    return entry_body(std::string_view(raw).substr(0, std::min<size_t>(219, max_line)));
}

std::string pool_kind(const run& r, size_t k) {
    return "<#" + std::to_string(r.m.run) + "." + std::to_string(pool_id + k) + ">";
}

// Returns the newest complete entries of `text` that fit in `room` bytes. `text` must begin with
// an entry.
std::string newest_entries(std::string_view text, size_t room) {
    if (text.size() <= room) {
        return std::string(text);
    }
    size_t at = text.size() - room;
    bool at_line = text[at - 1] == '\n';
    while (at < text.size()) {
        if (at_line && text[at] != '\t') {
            break;
        }
        at_line = text[at] == '\n';
        ++at;
    }
    return std::string(text.substr(at));
}

// Returns the context that a fault recorded now keeps: the newest complete entries that fit its
// room, from the log in flash followed by the ring's unflushed bytes. It reads the log and the ring
// as the record left them, so call it before anything else.
std::string expected_context(run& r) {
    std::string pending;
    const auto& ring = internal::rtc_ring();
    if (ring.valid()) {
        const auto p = ring.pending();
        pending.assign(p.first.data(), p.first.size());
        pending.append(p.second.data(), p.second.size());
        if (p.lost > 0) {
            // After a loss, the pending bytes are used from the first entry that begins in them.
            size_t at = 0;
            bool at_line = false;
            while (at < pending.size() && !(at_line && pending[at] != '\t')) {
                at_line = pending[at] == '\n';
                ++at;
            }
            pending.erase(0, at);
        }
    }
    const size_t room = r.f.room();
    if (pending.size() >= room) {
        ++s_tally.context_ring_only;
        return newest_entries(pending, room);
    }
    ++s_tally.context_with_flash;
    std::string newest(room - pending.size(), '\0');
    auto got = internal::copy_newest_in_flash(newest);
    expect(r, got.has_value(), "copy_newest_in_flash failed");
    newest.resize(*got);
    check_read(r, newest, false);
    return newest + pending;
}

// Checks the rules that every fault's context follows. The context is no longer than the room,
// begins on a stamped line, and ends with the fault's own entry, stamped with the boot, wake and
// time of the fault.
void check_context_rule(const run& r, std::string_view context, const recorded_fault& f) {
    const std::string end = " - " + f.body + "\n";
    if (!context.ends_with(end) || context.size() > r.f.room() || context[0] < '0' ||
        context[0] > '9') {
        fail(
            r, "a fault's context isn't whole entries within its room, ending with its own", context
        );
    }
    const size_t line = context.rfind('\n', context.size() - end.size());
    const std::string_view last = context.substr(line == std::string_view::npos ? 0 : line + 1);
    token t{.text = last.substr(0, last.size() - 1)};
    parse_stamp(t);
    if (!t.stamped || t.body != f.body || t.boot != f.boot || t.wake != f.wake || t.ms < f.ms_lo ||
        t.ms > f.ms_hi) {
        fail(r, "a fault's context ends with an entry stamped other than its own", context);
    }
}

// Reads every listed fault, with each context in pieces of odd sizes, and checks them against the
// model. The listed faults must be the most recent faults recorded since the last clear, as many as
// the slots hold, each as it was recorded.
void check_faults(run& r) {
    auto reader = pl::try_read_faults();
    expect(r, reader.has_value(), "try_read_faults failed");
    const size_t listed = std::min(r.f.slots, r.f.records.size());
    const size_t first = r.f.records.size() - listed;
    std::array<char, 256> buf;
    pl::fault f;
    for (size_t i = first; i < r.f.records.size(); ++i) {
        const recorded_fault& want = r.f.records[i];
        auto more = reader->try_next(f);
        expect(r, more.has_value(), "fault_reader try_next failed");
        expect(r, *more, "a fault recorded isn't listed");
        expect(r, f.sequence() == want.seq, "a fault listed with the wrong sequence number");
        expect(
            r,
            f.kind() == want.kind && f.detail() == want.detail,
            "a fault listed with the wrong kind or detail"
        );
        expect(
            r,
            f.boot() == want.boot && f.wake() == want.wake,
            "a fault listed with the wrong boot or wake"
        );
        expect(
            r,
            f.uptime().count() >= want.ms_lo && f.uptime().count() <= want.ms_hi,
            "a fault listed with an uptime outside its window"
        );
        expect(r, !f.wall_time().has_value(), "a fault listed with a wall-clock time");
        expect(r, !f.damaged(), "a fault listed as damaged");
        std::string context;
        while (true) {
            // Mostly large pieces, because each piece reads the slot's header again, and sometimes
            // a few bytes.
            const size_t n =
                1 + r.rng.below(r.rng.chance(5) ? 7 : static_cast<uint32_t>(buf.size()));
            auto got = reader->try_next_context_text(std::span<char>(buf.data(), n));
            expect(r, got.has_value(), "fault_reader try_next_context_text failed");
            if (got->empty()) {
                break;
            }
            expect(r, got->data() == buf.data(), "try_next_context_text returned text past its buffer");
            context.append(*got);
        }
        expect(r, context.size() == f.context_size(), "a fault's context read back isn't its size");
        check_context_rule(r, context, want);
        if (want.context && context != *want.context) {
            fail(r, "a fault's context read back isn't what it recorded", context);
        }
    }
    auto more = reader->try_next(f);
    expect(r, more.has_value() && !*more, "a fault listed that the model doesn't have");
    auto n = pl::try_fault_count();
    expect(r, n.has_value() && *n == listed, "fault_count isn't the faults listed");
}

// Adds the fault just recorded with entry `e`'s text to the model. Sometimes, checks it in
// full: its context must be what the model expects, and the log must list it with that context.
void check_recorded(run& r, std::string_view kind, std::string_view detail, const entry& e) {
    recorded_fault f{
        .seq = r.f.next_seq++,
        .kind = std::string(kind),
        .detail = std::string(detail),
        .boot = e.boot,
        .wake = e.wake,
        .ms_lo = e.ms_lo,
        .ms_hi = e.ms_hi,
        .body = e.body,
        .context = std::nullopt,
    };
    const bool full = r.rng.chance(40);
    if (full) {
        f.context = expected_context(r);
        check_context_rule(r, *f.context, f);
    }
    r.f.records.push_back(std::move(f));
    s_tally.overwritten += r.f.records.size() > r.f.slots ? 1 : 0;
    ++s_tally.faults_recorded;
    if (full) {
        ++s_tally.faults_checked;
        check_faults(r);
    }
}

std::string random_detail(run& r) {
    const uint32_t kind = r.rng.below(10);
    const size_t length = kind < 2 ? 0 : kind < 8 ? r.rng.below(60) : 150 + r.rng.below(60);
    return random_chars(r, length, r.rng.chance(20) ? 5 : 0);
}

void record_fault_op(run& r) {
    const uint32_t n = r.next_n++;
    // A kind of its own, which starts with the entry's id.
    const std::string kind = id_of(r, n) + (r.rng.chance(50) ? "x" : "");
    const std::string detail = random_detail(r);
    const int64_t lo = now_ms();
    auto done = pl::try_record_fault(kind, detail);
    const int64_t hi = now_ms();
    expect(r, done.has_value(), "try_record_fault failed");
    add_entry(r, n, fault_body(fault_head, kind, kept_detail(detail)), lo, hi, false);
    check_recorded(r, kind, kept_detail(detail), r.m.entries.back());
}

void bad_kind_op(run& r) {
    static constexpr std::array<std::string_view, 4> bad = {
        "", "a kind", "cleared", "123456789012345678901234"
    };
    const std::string_view kind = bad[r.rng.below(bad.size())];
    const size_t waiting = internal::rtc_ring().unflushed();
    auto done = r.rng.chance(50) ? pl::try_record_fault(kind, "x", pl::suppress_repeats)
                                 : pl::try_record_fault(kind, "x");
    const bool refused = fails_with(done, idfxx::errc::invalid_arg);
    expect(r, refused, "a fault of a bad kind wasn't refused");
    expect(r, internal::rtc_ring().unflushed() == waiting, "a fault of a bad kind logged a line");
    ++s_tally.bad_kinds;
}

void not_supported_op(run& r) {
    const size_t waiting = internal::rtc_ring().unflushed();
    expect(
        r,
        fails_with(pl::try_record_fault("pl_off"), idfxx::errc::not_supported),
        "try_record_fault without slots"
    );
    expect(
        r,
        fails_with(
            pl::try_record_fault("pl_off", pl::suppress_repeats), idfxx::errc::not_supported
        ),
        "suppressing try_record_fault without slots"
    );
    expect(
        r,
        fails_with(pl::try_read_faults(), idfxx::errc::not_supported),
        "try_read_faults without slots"
    );
    expect(
        r,
        fails_with(pl::try_fault_count(), idfxx::errc::not_supported),
        "try_fault_count without slots"
    );
    expect(
        r,
        fails_with(pl::try_clear_faults(), idfxx::errc::not_supported),
        "try_clear_faults without slots"
    );
    expect(
        r, internal::rtc_ring().unflushed() == waiting, "a fault call without slots logged a line"
    );
    ++s_tally.not_supported;
}

// Waits until the time since boot is past the window of a suppressed kind's last line, so that
// lines with the same text have separate windows.
void after_last_line(run& r, size_t k) {
    while (now_ms() <= r.f.last_hi[k]) {
    }
}

remembered* remembered_kind(run& r, std::string_view kind) {
    for (auto& m : r.f.memory) {
        if (m.kind == kind) {
            return &m;
        }
    }
    return nullptr;
}

// Records a fault of pool kind k with its repeats suppressed, with one of its two details.
void suppressing_call(run& r, size_t k) {
    const std::string kind = pool_kind(r, k);
    auto& details = r.f.details[k];
    if (details[0].empty() && details[1].empty()) {
        details = {random_detail(r), random_detail(r)};
    }
    const std::string detail = details[r.rng.below(2)];
    const std::string kept = kept_detail(detail);
    remembered* was = remembered_kind(r, kind);
    after_last_line(r, k);
    const uint32_t n = r.next_n++;
    const int64_t lo = now_ms();
    auto done = pl::try_record_fault(kind, detail, pl::suppress_repeats);
    const int64_t hi = now_ms();
    r.f.last_hi[k] = hi;
    expect(r, done.has_value(), "suppressing try_record_fault failed");
    expect(r, internal::valid(internal::rtc().faults), "the RTC fault record isn't sealed");
    if (was != nullptr) {
        expect(r, !*done, "a fault of a suppressed kind was recorded");
        add_entry(r, n, fault_body(repeat_head, kind, kept), lo, hi, false, pool_id + k);
        ++s_tally.suppressed_repeats;
        s_tally.other_detail_repeats += was->detail != kept ? 1 : 0;
        s_tally.kept_across_reset += was->epoch != r.f.epoch ? 1 : 0;
        return;
    }
    expect(r, *done, "a suppressing record not remembered wasn't recorded");
    add_entry(r, n, fault_body(fault_head, kind, kept), lo, hi, false, pool_id + k);
    ++s_tally.suppressed_recorded;
    for (const auto& f : r.f.forgotten) {
        s_tally.forgotten_by_power += f.kind == kind ? 1 : 0;
    }
    std::erase_if(r.f.forgotten, [&](const remembered& f) { return f.kind == kind; });
    // The kind is remembered in a free place, or else in the place of the kind recorded longest
    // ago.
    if (r.f.memory.size() == suppressed_capacity) {
        auto oldest = std::ranges::min_element(r.f.memory, {}, &remembered::order);
        r.f.memory.erase(oldest);
        ++s_tally.evictions;
    }
    r.f.memory.push_back(
        remembered{.kind = kind, .detail = kept, .order = r.f.next_order++, .epoch = r.f.epoch}
    );
    check_recorded(r, kind, kept, r.m.entries.back());
}

// Makes a few suppressing records. Sometimes, it records one fault of as many kinds as there is
// room for, plus one more, so that the kind recorded longest ago is replaced.
void suppressing_op(run& r) {
    if (r.rng.chance(4)) {
        const size_t from = r.rng.below(suppressed_pool);
        for (size_t i = 0; i <= suppressed_capacity; ++i) {
            suppressing_call(r, (from + i) % suppressed_pool);
        }
        return;
    }
    for (uint32_t i = 1 + r.rng.below(2); i > 0; --i) {
        suppressing_call(r, r.rng.below(suppressed_pool));
    }
}

void lift_op(run& r) {
    const size_t k = r.rng.below(suppressed_pool);
    const std::string kind = pool_kind(r, k);
    remembered* was = remembered_kind(r, kind);
    after_last_line(r, k);
    const uint32_t n = r.next_n++;
    const size_t waiting = internal::rtc_ring().unflushed();
    const int64_t lo = now_ms();
    pl::lift_suppression(kind);
    const int64_t hi = now_ms();
    r.f.last_hi[k] = hi;
    expect(r, internal::valid(internal::rtc().faults), "the RTC fault record isn't sealed");
    if (was == nullptr) {
        expect(
            r,
            internal::rtc_ring().unflushed() == waiting,
            "lifting the suppression of a kind not remembered logged a line"
        );
        ++s_tally.lift_noop;
        return;
    }
    add_entry(r, n, fault_body(lifted_head, kind, {}), lo, hi, false, pool_id + k);
    std::erase_if(r.f.memory, [&](const remembered& m) { return m.kind == kind; });
    ++s_tally.lifted;
}

void clear_faults_op(run& r) {
    expect(r, pl::try_clear_faults().has_value(), "try_clear_faults failed");
    r.f.records.clear();
    ++r.f.next_seq;
    r.f.memory.clear();
    r.f.forgotten.clear();
    ++s_tally.fault_clears;
    check_faults(r);
}

void fault_op(run& r, uint32_t pick) {
    if (!r.f.on) {
        if (pick < 3) {
            r.op_name = "faults without slots";
            not_supported_op(r);
        } else {
            r.op_name = "append";
            append_entry(r, random_length(r));
        }
        return;
    }
    if (pick < 6) {
        r.op_name = "record fault";
        record_fault_op(r);
    } else if (pick < 20) {
        r.op_name = "record suppressing";
        suppressing_op(r);
    } else if (pick < 28) {
        r.op_name = "lift suppression";
        lift_op(r);
    } else if (pick < 32) {
        r.op_name = "read faults";
        check_faults(r);
        ++s_tally.fault_reads;
    } else if (pick < 36) {
        r.op_name = "count faults";
        auto n = pl::try_fault_count();
        expect(
            r,
            n.has_value() && *n == std::min(r.f.slots, r.f.records.size()),
            "fault_count isn't the faults the model has"
        );
        ++s_tally.fault_counts;
    } else if (pick < 38) {
        r.op_name = "clear faults";
        clear_faults_op(r);
    } else if (pick < 40) {
        r.op_name = "bad fault kind";
        bad_kind_op(r);
    } else {
        r.op_name = "append";
        append_entry(r, random_length(r));
    }
}

void burst_op(run& r) {
    const uint64_t target = ring_size + r.rng.below(ring_size);
    for (int i = 0; i < 200 && r.pending_lo <= target; ++i) {
        append_entry(r, random_length(r));
    }
}

void one_run(uint32_t seed) {
    run r;
    r.seed = seed;
    r.rng = prng(0x5eed0000ull + seed);
    r.m.run = seed;
    // A store that the log accepts, of at most 16 KiB.
    static constexpr std::array<size_t, 9> sector_sizes = {
        80, 96, 128, 160, 256, 512, 1024, 2048, 4096
    };
    r.sector_size = sector_sizes[r.rng.below(sector_sizes.size())];
    const size_t most = std::min<size_t>(32, 16384 / r.sector_size);
    r.log_sectors = 2 + r.rng.below(static_cast<uint32_t>(most - 1));
    // Most runs have fault slots after the log's sectors. Each slot is the fewest sectors that
    // hold 512 bytes of context, so that the ring alone can fill a slot.
    r.f = {};
    r.f.on = r.rng.chance(75);
    r.sectors = r.log_sectors;
    if (r.f.on) {
        r.f.slot_size = (1024 + r.sector_size - 1) / r.sector_size * r.sector_size;
        r.f.slots = 1 + r.rng.below(3);
        r.sectors += r.f.slot_size / r.sector_size * r.f.slots;
        r.f.last_hi.assign(suppressed_pool, -1);
        r.f.details.assign(suppressed_pool, {});
        ++s_tally.fault_runs;
    }
    r.encrypted = r.rng.chance(50);
    r.ram = r.encrypted ? std::make_shared<pl::ram_sector_store>(
                              pl::ram_sector_store::encrypted, r.sectors, r.sector_size
                          )
                        : std::make_shared<pl::ram_sector_store>(r.sectors, r.sector_size);
    r.ctl = std::make_shared<control>();
    r.boot = r.rng.below(100000);
    r.op_name = "start";
    start(r);
    // Start from empty, so that the ring and the store hold nothing from earlier tests.
    expect(r, pl::try_clear().has_value(), "try_clear failed");

    for (r.op = 0; r.op < ops_per_run; ++r.op) {
        const uint32_t pick = r.rng.below(1000);
        if (pick < 270) {
            r.op_name = "append";
            append_entry(r, random_length(r));
        } else if (pick < 330) {
            fault_op(r, pick - 270);
        } else if (pick < 470) {
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
            r.op_name = "capture";
            capture_entry(r);
#else
            r.op_name = "append";
            append_entry(r, random_length(r));
#endif
        } else if (pick < 500) {
            r.op_name = "burst";
            burst_op(r);
        } else if (pick < 600) {
            r.op_name = "flush";
            flush_op(r);
        } else if (pick < 650) {
            r.op_name = "flush_if_pending";
            flush_if_pending_op(r);
        } else if (pick < 750) {
            r.op_name = "read";
            (void)read_op(r);
        } else if (pick < 800) {
            r.op_name = "copy_newest";
            copy_newest_op(r);
        } else if (pick < 815) {
            r.op_name = "clear";
            clear_op(r);
        } else if (pick < 835) {
            r.op_name = "restart";
            restart_op(r);
        } else if (pick < 855) {
            r.op_name = "write fault";
            write_fault_op(r);
        } else if (pick < 870) {
            r.op_name = "erase fault";
            erase_fault_op(r);
        } else if (pick < 905) {
            r.op_name = "reset fault";
            reset_fault_op(r);
        } else if (pick < 935) {
            r.op_name = "power fault";
            power_fault_op(r);
        } else if (pick < 955) {
            r.op_name = "corrupt at start";
            corrupt_start_op(r);
        } else {
            r.op_name = "corrupt at flush";
            corrupt_flush_op(r);
        }
        ++s_tally.ops;
    }

    // Last, write a short entry and read the log. The entry must be kept on a store with room for
    // it.
    r.op_name = "final read";
    const uint32_t last = r.next_n;
    append_entry(r, 0);
    const int64_t last_kept = read_op(r);
    if ((r.log_sectors - 1) * (r.sector_size - 32) >= 128) {
        expect(r, last_kept == last, "the newest entry, flushed, isn't in the read");
        ++s_tally.final_kept;
    }
    stop_op(r);
    ++s_tally.runs;
}

// =============================================================================
// Test setup
// =============================================================================

void stop_log() {
    (void)pl::try_stop();
}

internal::rtc_state s_saved_rtc;
bool s_rtc_saved = false;

void restore_rtc() {
    if (s_rtc_saved) {
        internal::rtc() = s_saved_rtc;
        s_rtc_saved = false;
    }
}

#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
vprintf_like_t s_previous_vprintf = nullptr;
bool s_vprintf_installed = false;

int silent_vprintf(const char*, va_list) {
    return 0;
}

void restore_vprintf() {
    if (s_vprintf_installed) {
        esp_log_set_vprintf(s_previous_vprintf);
        s_vprintf_installed = false;
    }
}
#endif

} // namespace

// =============================================================================
// The checker, on reads built by hand
// =============================================================================

namespace {

// Six entries over two flushes. The second flush dropped the first two of its three entries.
model hand_model() {
    model m;
    m.run = 0;
    const auto add = [&m](uint32_t n, std::string body, uint32_t flush) {
        m.entries.push_back(
            entry{
                .n = n,
                .body = std::move(body),
                .boot = 7,
                .wake = 0,
                .ms_lo = 1000,
                .ms_hi = 1000,
                .flush = flush
            }
        );
    };
    add(0, "<#0.0> a", 0);
    add(1, "<#0.1> b", 0);
    add(2, "<#0.2> c\n\tc2", 0);
    add(3, "<#0.3> d", 1);
    add(4, "<#0.4> e", 1);
    add(5, "<#0.5> f", 1);
    m.flushes.push_back(
        flush_attempt{
            .out = outcome::ok, .marker = false, .boot = 7, .wake = 0, .ms_lo = 1000, .ms_hi = 1000
        }
    );
    m.flushes.push_back(
        flush_attempt{
            .out = outcome::ok, .marker = true, .boot = 7, .wake = 0, .ms_lo = 2000, .ms_hi = 2000
        }
    );
    return m;
}

const char* error_of(std::string_view text, const model& m) {
    static std::string error;
    error = check(text, m, m.attempts()).error;
    return error.c_str();
}

// Whether the check rejects `text` against `m`.
bool rejected(std::string_view text, const model& m) {
    return std::strlen(error_of(text, m)) != 0;
}

} // namespace

TEST_CASE(
    "persistent_log model check takes what the model allows and nothing else",
    "[persistent_log]"
) {
    model m = hand_model();
    // Entries d and e are 21 bytes each, as in "7.0 +1000 - <#0.3> d\n".
    const std::string a = "7.0 +1000 - <#0.0> a\n";
    const std::string b = "7.0 +1000 - <#0.1> b\n";
    const std::string c = "7.0 +1000 - <#0.2> c\n\tc2\n";
    const std::string mark = "7.0 +2000 - W persistent_log: dropped 42 bytes\n";
    const std::string f = "7.0 +1000 - <#0.5> f\n";
    const std::string whole = a + b + c + mark + f;
    TEST_ASSERT_EQUAL_STRING("", error_of(whole, m));
    const verdict v = check(whole, m, 2);
    TEST_ASSERT_EQUAL(4, v.kept);
    TEST_ASSERT_EQUAL(2, v.dropped);
    TEST_ASSERT_EQUAL(1, v.markers);
    TEST_ASSERT_EQUAL(1, v.continued);
    TEST_ASSERT_EQUAL(1, v.removable.size());
    TEST_ASSERT_EQUAL(1, v.exact.size());
    // Entries evicted from the front, as far as the marker, are allowed.
    TEST_ASSERT_EQUAL_STRING("", error_of(b + c + mark + f, m));
    TEST_ASSERT_EQUAL_STRING("", error_of(mark + f, m));
    TEST_ASSERT_EQUAL_STRING("", error_of(f, m));
    TEST_ASSERT_EQUAL_STRING("", error_of("", m));

    // (i) A kept entry taken out, from the middle or the end.
    TEST_ASSERT_TRUE(rejected(a + c + mark + f, m));
    TEST_ASSERT_TRUE(rejected(a + b + c + mark, m));
    TEST_ASSERT_TRUE(rejected(without_token(whole, v.tokens[v.removable[0]]), m));
    // (ii) An entry doubled, or two swapped.
    TEST_ASSERT_TRUE(rejected(a + b + b + c + mark + f, m));
    TEST_ASSERT_TRUE(rejected(a + c + b + mark + f, m));
    // (iii) The marker's count off by one either way, or the marker missing.
    TEST_ASSERT_TRUE(
        rejected(a + b + c + "7.0 +2000 - W persistent_log: dropped 43 bytes\n" + f, m)
    );
    TEST_ASSERT_TRUE(
        rejected(a + b + c + "7.0 +2000 - W persistent_log: dropped 41 bytes\n" + f, m)
    );
    TEST_ASSERT_TRUE(rejected(recounted(whole, v.tokens[v.exact[0]], 1), m));
    TEST_ASSERT_TRUE(rejected(a + b + c + f, m));
    // Altered text, a wrong stamp, an unknown entry, a fragment of a flush that wasn't cut, a line
    // that is neither a stamp nor a tab, a read that begins on a continuation.
    TEST_ASSERT_TRUE(rejected(a + "7.0 +1000 - <#0.1> B\n" + c + mark + f, m));
    TEST_ASSERT_TRUE(rejected(a + "7.1 +1000 - <#0.1> b\n" + c + mark + f, m));
    TEST_ASSERT_TRUE(rejected(a + "7.0 +1001 - <#0.1> b\n" + c + mark + f, m));
    TEST_ASSERT_TRUE(rejected(whole + "7.0 +1000 - <#0.9> z\n", m));
    TEST_ASSERT_TRUE(rejected(whole + "7.0 +1000 - <#0.6\n", m));
    TEST_ASSERT_TRUE(rejected(a + "b\n" + c + mark + f, m));
    TEST_ASSERT_TRUE(rejected("\tc2\n" + mark + f, m));

    // When the second flush is cut, its last entry may be missing, or cut short at a record
    // boundary.
    m.flushes[1].out = outcome::cut;
    TEST_ASSERT_EQUAL_STRING("", error_of(whole, m));
    TEST_ASSERT_EQUAL_STRING("", error_of(a + b + c + mark, m));
    TEST_ASSERT_EQUAL_STRING("", error_of(a + b + c + mark + "7.0 +1000 - <#0.5>\n", m));
    TEST_ASSERT_EQUAL_STRING("", error_of(a + b + c + mark + "7.0 +10\n", m));
    TEST_ASSERT_EQUAL_STRING("", error_of(a + b + c + "7.0 +2000 - W persistent_log: drop\n", m));
    TEST_ASSERT_EQUAL_STRING("", error_of(a + b + c, m));
    // Or, when its marker and first entries were evicted, the read may begin on a fragment of what
    // the flush wrote.
    TEST_ASSERT_EQUAL_STRING("", error_of("7.0 +1000 - <#0.4>\n", m));
    // A fragment after a kept entry of the first flush could be the start of an entry of either
    // cut flush. The checker takes it as the first flush's fragment, with the second flush having
    // written nothing. But a flush leaves only one fragment.
    m.flushes[0].out = outcome::cut;
    TEST_ASSERT_EQUAL_STRING("", error_of(a + "7.0 +1000 - <#0.\n", m));
    TEST_ASSERT_TRUE(rejected("7.0 +10\n7.0 +1000 - <#0.\n", m));
    m.flushes[0].out = outcome::ok;
    // But the checker rejects a fragment that isn't the start of the flush's next entry, and
    // anything after a fragment.
    TEST_ASSERT_TRUE(rejected(a + b + c + mark + "7.0 +1000 - <#0.4> e\n", m));
    TEST_ASSERT_TRUE(rejected(a + b + c + mark + "7.0 +1000 - <#0.5> g\n", m));
    TEST_ASSERT_TRUE(rejected(a + b + c + "7.0 +10\n" + f, m));
    // The checker also rejects a marker that doesn't count a leading run of the flush's entries.
    TEST_ASSERT_TRUE(rejected(a + b + c + "7.0 +2000 - W persistent_log: dropped 43 bytes\n", m));
    TEST_ASSERT_EQUAL_STRING(
        "", error_of(a + b + c + "7.0 +2000 - W persistent_log: dropped 21 bytes\n", m)
    );
}

TEST_CASE(
    "persistent_log model check tells a suppressed kind's entries apart by text and stamp",
    "[persistent_log]"
) {
    // Lines that carry one kind's id, between two entries. The repeats are the same except for
    // their stamps.
    model m;
    m.run = 0;
    const std::string x = "W persistent_log: fault repeat kind=<#0.99> d";
    const std::string y = "I persistent_log: fault suppression lifted kind=<#0.99>";
    const auto add =
        [&m](uint32_t n, std::string body, int64_t ms, std::optional<uint32_t> carried) {
            m.entries.push_back(
                entry{
                    .n = n,
                    .body = std::move(body),
                    .boot = 7,
                    .wake = 0,
                    .ms_lo = ms,
                    .ms_hi = ms,
                    .flush = 0,
                    .carried = carried
                }
            );
        };
    add(0, "<#0.0> a", 1000, std::nullopt);
    add(1, x, 1000, 99);
    add(2, x, 1001, 99);
    add(3, y, 1002, 99);
    add(4, "<#0.4> b", 1003, std::nullopt);
    m.flushes.push_back(
        flush_attempt{
            .out = outcome::ok, .marker = false, .boot = 7, .wake = 0, .ms_lo = 1004, .ms_hi = 1004
        }
    );
    const std::string a = "7.0 +1000 - <#0.0> a\n";
    const std::string x0 = "7.0 +1000 - " + x + "\n";
    const std::string x1 = "7.0 +1001 - " + x + "\n";
    const std::string yl = "7.0 +1002 - " + y + "\n";
    const std::string b = "7.0 +1003 - <#0.4> b\n";
    TEST_ASSERT_EQUAL_STRING("", error_of(a + x0 + x1 + yl + b, m));
    const verdict v = check(a + x0 + x1 + yl + b, m, 1);
    TEST_ASSERT_EQUAL(5, v.kept);
    // The checker rejects a line that is doubled, swapped, stamped outside every window, taken
    // out, altered, or that has an unknown id.
    TEST_ASSERT_TRUE(rejected(a + x0 + x0 + x1 + yl + b, m));
    TEST_ASSERT_TRUE(rejected(a + x1 + x0 + yl + b, m));
    TEST_ASSERT_TRUE(rejected(a + x0 + "7.0 +1004 - " + x + "\n" + yl + b, m));
    TEST_ASSERT_TRUE(rejected(a + x0 + yl + b, m));
    const std::string lifted = "7.0 +1002 - I persistent_log: fault suppression lifted kind=";
    TEST_ASSERT_TRUE(rejected(a + x0 + x1 + lifted + "<#0.99> z\n" + b, m));
    TEST_ASSERT_TRUE(rejected(a + x0 + x1 + lifted + "<#0.98>\n" + b, m));
    // With a cut flush, the checker allows one of the lines to be cut short and the rest to be
    // lost.
    m.flushes[0].out = outcome::cut;
    TEST_ASSERT_EQUAL_STRING(
        "", error_of(a + x0 + "7.0 +1001 - W persistent_log: fault repeat kind=<#0.99>\n", m)
    );
    TEST_ASSERT_EQUAL_STRING("", error_of(a + x0 + x1, m));
}

// =============================================================================
// The randomized runs
// =============================================================================

TEST_CASE(
    "persistent_log keeps every entry, or accounts for it, over seeded random runs",
    "[persistent_log]"
) {
    pl::testing::report_cache_enabled_until_test_end();
    idfxx::test::run_after_each_test(stop_log);
    idfxx::test::run_after_each_test(restore_rtc);
    s_saved_rtc = internal::rtc();
    s_rtc_saved = true;
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
    // The lines that the runs log, and the lines that the log itself logs when a write fails,
    // don't need to reach the console.
    idfxx::test::run_after_each_test(restore_vprintf);
    s_previous_vprintf = esp_log_set_vprintf(silent_vprintf);
    s_vprintf_installed = true;
#endif
    s_tally = {};
    const int64_t began = esp_timer_get_time();
    for (uint32_t seed : seeds) {
        one_run(seed);
    }
    for (uint32_t i = 0; i < extra_seeds; ++i) {
        one_run(1000 + i);
    }
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
    restore_vprintf();
#endif
    restore_rtc();
    const auto& t = s_tally;
    std::printf(
        "persistent_log model: %u runs, %u ops in %lld ms; %u reads, %u newest copies, %u clears, "
        "%u restarts; %u appends (%u near the longest), %u captures; kept %u, evicted %u, "
        "dropped %u in %u markers, lost %u, cut short %u; %u continued, %u captured kept; "
        "%u overruns; faults: %u write, %u erase, %u reset, %u power, %u ring at start, "
        "%u ring at flush, %u ring past its size; %u mutations rejected; %u entry reads of %u "
        "entries, %u empty lines, %u newest reads\n",
        unsigned(t.runs),
        unsigned(t.ops),
        static_cast<long long>((esp_timer_get_time() - began) / 1000),
        unsigned(t.reads),
        unsigned(t.newest_copies),
        unsigned(t.clears),
        unsigned(t.restarts),
        unsigned(t.appends),
        unsigned(t.near_max),
        unsigned(t.captures),
        unsigned(t.kept),
        unsigned(t.evicted),
        unsigned(t.dropped),
        unsigned(t.markers),
        unsigned(t.lost),
        unsigned(t.truncated),
        unsigned(t.continued),
        unsigned(t.captured),
        unsigned(t.overruns),
        unsigned(t.fault_write),
        unsigned(t.fault_erase),
        unsigned(t.fault_reset),
        unsigned(t.fault_power),
        unsigned(t.corrupt_start),
        unsigned(t.corrupt_flush),
        unsigned(t.corrupt_full),
        unsigned(t.mutations),
        unsigned(t.entry_reads),
        unsigned(t.entries),
        unsigned(t.empty_lines),
        unsigned(t.newest_reads)
    );
    std::printf(
        "persistent_log model faults: %u runs with slots; %u recorded, %u checked in full "
        "(%u context from the ring alone, %u with flash), %u overwritten, %u bad kinds, "
        "%u without slots; %u reads, %u counts, %u clears; suppressed: %u recorded, %u repeats "
        "(%u of another detail, %u kept across a restart), %u lifted, %u lifts of none, "
        "%u made way, %u forgotten by a power cut\n",
        unsigned(t.fault_runs),
        unsigned(t.faults_recorded),
        unsigned(t.faults_checked),
        unsigned(t.context_ring_only),
        unsigned(t.context_with_flash),
        unsigned(t.overwritten),
        unsigned(t.bad_kinds),
        unsigned(t.not_supported),
        unsigned(t.fault_reads),
        unsigned(t.fault_counts),
        unsigned(t.fault_clears),
        unsigned(t.suppressed_recorded),
        unsigned(t.suppressed_repeats),
        unsigned(t.other_detail_repeats),
        unsigned(t.kept_across_reset),
        unsigned(t.lifted),
        unsigned(t.lift_noop),
        unsigned(t.evictions),
        unsigned(t.forgotten_by_power)
    );
    // Each class, operation and fault occurred at least once.
    TEST_ASSERT_GREATER_THAN(0, t.fault_runs);
    TEST_ASSERT_LESS_THAN(t.runs, t.fault_runs);
    TEST_ASSERT_GREATER_THAN(0, t.faults_recorded);
    TEST_ASSERT_GREATER_THAN(0, t.context_ring_only);
    TEST_ASSERT_GREATER_THAN(0, t.context_with_flash);
    TEST_ASSERT_GREATER_THAN(0, t.overwritten);
    TEST_ASSERT_GREATER_THAN(0, t.bad_kinds);
    TEST_ASSERT_GREATER_THAN(0, t.not_supported);
    TEST_ASSERT_GREATER_THAN(0, t.fault_reads);
    TEST_ASSERT_GREATER_THAN(0, t.fault_counts);
    TEST_ASSERT_GREATER_THAN(0, t.fault_clears);
    TEST_ASSERT_GREATER_THAN(0, t.suppressed_recorded);
    TEST_ASSERT_GREATER_THAN(0, t.other_detail_repeats);
    TEST_ASSERT_GREATER_THAN(0, t.suppressed_repeats);
    TEST_ASSERT_GREATER_THAN(0, t.kept_across_reset);
    TEST_ASSERT_GREATER_THAN(0, t.lifted);
    TEST_ASSERT_GREATER_THAN(0, t.lift_noop);
    TEST_ASSERT_GREATER_THAN(0, t.evictions);
    TEST_ASSERT_GREATER_THAN(0, t.forgotten_by_power);
    TEST_ASSERT_GREATER_THAN(0, t.reads);
    TEST_ASSERT_GREATER_THAN(0, t.newest_copies);
    TEST_ASSERT_GREATER_THAN(0, t.clears);
    TEST_ASSERT_GREATER_THAN(0, t.restarts);
    TEST_ASSERT_GREATER_THAN(0, t.near_max);
    TEST_ASSERT_GREATER_THAN(1000, t.kept);
    TEST_ASSERT_GREATER_THAN(0, t.evicted);
    TEST_ASSERT_GREATER_THAN(0, t.dropped);
    TEST_ASSERT_GREATER_THAN(0, t.markers);
    TEST_ASSERT_GREATER_THAN(0, t.lost);
    TEST_ASSERT_GREATER_THAN(0, t.truncated);
    TEST_ASSERT_GREATER_THAN(0, t.continued);
    TEST_ASSERT_GREATER_THAN(0, t.overruns);
    TEST_ASSERT_GREATER_THAN(0, t.fault_write);
    TEST_ASSERT_GREATER_THAN(0, t.fault_erase);
    TEST_ASSERT_GREATER_THAN(0, t.fault_reset);
    TEST_ASSERT_GREATER_THAN(0, t.fault_power);
    TEST_ASSERT_GREATER_THAN(0, t.corrupt_start);
    TEST_ASSERT_GREATER_THAN(0, t.corrupt_flush);
    TEST_ASSERT_GREATER_THAN(0, t.corrupt_full);
    TEST_ASSERT_GREATER_THAN(0, t.mutations);
    TEST_ASSERT_GREATER_THAN(0, t.final_kept);
    TEST_ASSERT_EQUAL(t.reads, t.entry_reads);
    TEST_ASSERT_GREATER_THAN(1000, t.entries);
    TEST_ASSERT_GREATER_THAN(100, t.newest_reads);
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
    TEST_ASSERT_GREATER_THAN(0, t.captures);
    TEST_ASSERT_GREATER_THAN(0, t.captured);
#endif
}
