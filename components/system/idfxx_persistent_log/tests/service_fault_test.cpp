// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Tests for idfxx_persistent_log's service: recording faults through it and reading them back. The
// tests run on the test app's "log" partition, with capture off, so that the log holds only the
// lines that the tests append.

#include "sdkconfig.h"

#include "../src/log_line.hpp"
#include "../src/rtc.hpp"
#include "../src/sector_store.hpp"
#include "counting_store.hpp"
#include "idfxx/persistent_log"
#include "test_cleanup.hpp"
#include "unity.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_app_desc.h>
#include <esp_rom_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace pl = idfxx::persistent_log;
namespace internal = idfxx::persistent_log::internal;

// An empty braced detail selects the overload without the tag, because the tag can't be
// constructed from `{}`.
static_assert(requires { pl::try_record_fault("pl_k", {}); });
static_assert(!std::is_default_constructible_v<pl::suppress_repeats_t>);

namespace {

// =============================================================================
// Helpers
// =============================================================================

template<typename T>
bool fails_with(const idfxx::result<T>& r, idfxx::errc e) {
    return !r.has_value() && r.error() == e;
}

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

size_t occurrences(std::string_view hay, std::string_view needle) {
    size_t n = 0;
    for (size_t at = 0; (at = hay.find(needle, at)) != std::string_view::npos; ++at) {
        ++n;
    }
    return n;
}

void stop_log() {
    (void)pl::try_stop();
}

std::optional<std::chrono::system_clock::time_point> fixed_clock() {
    using namespace std::chrono;
    return sys_days{year{2026} / 1 / 2} + hours{3} + minutes{4} + seconds{5};
}

// A config with no writer, boot 7, a fixed wall clock, and no capture.
pl::config fault_config() {
    pl::config c;
    c.boot_count = 7;
    c.wall_clock = fixed_clock;
    c.writer = std::nullopt;
#ifdef CONFIG_IDFXX_STD_FORMAT
    c.capture = std::nullopt;
#endif
    return c;
}

// Starts the log, then empties the log and its faults, so that a test sees only its own lines and
// faults.
void start_clean(pl::config c = fault_config()) {
    idfxx::test::run_after_each_test(stop_log);
    TEST_ASSERT_TRUE_MESSAGE(pl::try_start(std::move(c)).has_value(), "try_start failed");
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
}

void record(std::string_view kind, std::string_view detail = {}) {
    auto r = pl::try_record_fault(kind, detail);
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), "try_record_fault failed");
}

size_t count() {
    auto n = pl::try_fault_count();
    TEST_ASSERT_TRUE(n.has_value());
    return *n;
}

std::string context_of(pl::fault_reader& r) {
    std::string s;
    std::array<char, 256> chunk;
    while (true) {
        auto n = r.try_next_context_text(chunk);
        TEST_ASSERT_TRUE(n.has_value());
        if (n->empty()) {
            return s;
        }
        s.append(*n);
    }
}

struct listed {
    pl::fault fault;
    std::string context;
};

std::vector<listed> list_faults() {
    std::vector<listed> out;
    auto r = pl::try_read_faults();
    TEST_ASSERT_TRUE(r.has_value());
    while (true) {
        listed l;
        auto more = r->try_next(l.fault);
        TEST_ASSERT_TRUE(more.has_value());
        if (!*more) {
            return out;
        }
        l.context = context_of(*r);
        out.push_back(std::move(l));
    }
}

std::string read_all() {
    auto r = pl::try_read_text();
    TEST_ASSERT_TRUE(r.has_value());
    std::string all;
    std::array<char, 256> chunk;
    while (true) {
        auto n = r->try_next(chunk);
        TEST_ASSERT_TRUE(n.has_value());
        if (n->empty()) {
            return all;
        }
        all.append(*n);
    }
}

// The last line of `text`, without its newline.
std::string_view last_line(std::string_view text) {
    if (text.ends_with('\n')) {
        text.remove_suffix(1);
    }
    const size_t nl = text.rfind('\n');
    return nl == std::string_view::npos ? text : text.substr(nl + 1);
}

} // namespace

// =============================================================================
// Errors
// =============================================================================

TEST_CASE("persistent_log fault calls need the log started", "[persistent_log]") {
    stop_log();
    TEST_ASSERT_TRUE(fails_with(pl::try_record_fault("pl_kind"), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_read_faults(), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_clear_faults(), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_fault_count(), idfxx::errc::invalid_state));
}

TEST_CASE("persistent_log fault calls need fault slots", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    auto c = fault_config();
    c.faults = std::nullopt;
    TEST_ASSERT_TRUE(pl::try_start(std::move(c)).has_value());
    TEST_ASSERT_TRUE(fails_with(pl::try_record_fault("pl_kind"), idfxx::errc::not_supported));
    // A bad kind still fails with not_supported, because the fault slots are checked first.
    TEST_ASSERT_TRUE(fails_with(pl::try_record_fault(""), idfxx::errc::not_supported));
    TEST_ASSERT_TRUE(fails_with(pl::try_read_faults(), idfxx::errc::not_supported));
    TEST_ASSERT_TRUE(fails_with(pl::try_clear_faults(), idfxx::errc::not_supported));
    TEST_ASSERT_TRUE(fails_with(pl::try_fault_count(), idfxx::errc::not_supported));
}

TEST_CASE("persistent_log record_fault refuses a bad kind, and logs nothing", "[persistent_log]") {
    start_clean();
    for (std::string_view kind :
         {std::string_view(""),
          std::string_view("with space"),
          std::string_view("123456789012345678901234"),
          std::string_view("caf\xC3\xA9"),
          std::string_view("tab\t"),
          std::string_view("cleared")}) {
        TEST_ASSERT_TRUE(
            fails_with(pl::try_record_fault(kind, "detail"), idfxx::errc::invalid_arg)
        );
    }
    TEST_ASSERT_EQUAL_size_t(0, count());
    TEST_ASSERT_FALSE(contains(read_all(), "fault kind="));
    // The longest valid kind is accepted.
    record("12345678901234567890123");
    TEST_ASSERT_EQUAL_size_t(1, count());
}

// =============================================================================
// Recording
// =============================================================================

TEST_CASE("persistent_log record_fault keeps what it was, when and where", "[persistent_log]") {
    start_clean();
    const std::string detail(200, 'd');
    record("pl_fields", detail);
    auto got = list_faults();
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    const auto& f = got[0].fault;
    TEST_ASSERT_TRUE(f.kind() == "pl_fields");
    TEST_ASSERT_TRUE(f.detail() == std::string(159, 'd'));
    TEST_ASSERT_EQUAL_UINT32(7, f.boot());
    TEST_ASSERT_EQUAL_UINT32(internal::rtc().wake.wake, f.wake());
    TEST_ASSERT_TRUE(f.wall_time() == fixed_clock());
    TEST_ASSERT_TRUE(f.task() == pcTaskGetName(nullptr));
    const esp_app_desc_t* app = esp_app_get_description();
    const std::string_view version(app->version, strnlen(app->version, sizeof app->version));
    TEST_ASSERT_TRUE(f.version() == version);
    TEST_ASSERT_EQUAL_size_t(8, f.elf_sha256().size());
    TEST_ASSERT_EQUAL_MEMORY(app->app_elf_sha256, f.elf_sha256().data(), 8);
    TEST_ASSERT_EQUAL_HEX32(0, f.exception_pc());
    TEST_ASSERT_EQUAL_size_t(0, f.frames().size());
    TEST_ASSERT_FALSE(f.damaged());

    // The entry that record_fault logged is the last in the fault's context, and has the fault's
    // stamp.
    std::array<char, pl::max_prefix_size> st;
    const size_t sn = pl::format_prefix(st, f.boot(), f.wake(), f.uptime(), f.wall_time());
    const std::string entry = std::string(st.data(), sn) +
        "E persistent_log: fault kind=pl_fields " + std::string(159, 'd');
    TEST_ASSERT_TRUE(last_line(got[0].context) == entry);
    TEST_ASSERT_TRUE(got[0].context.ends_with("\n"));
}

TEST_CASE("persistent_log record_fault logs no space after an empty detail", "[persistent_log]") {
    start_clean();
    record("pl_bare");
    auto got = list_faults();
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    TEST_ASSERT_TRUE(got[0].fault.detail().empty());
    TEST_ASSERT_TRUE(last_line(got[0].context).ends_with(" E persistent_log: fault kind=pl_bare"));
}

TEST_CASE(
    "persistent_log record_fault keeps the lines in flash and in the ring",
    "[persistent_log]"
) {
    start_clean();
    pl::append("pl in flash one");
    pl::append("pl in flash two\nand its next line");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    pl::append("pl in the ring");
    record("pl_context", "why");

    auto got = list_faults();
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    const std::string& context = got[0].context;
    TEST_ASSERT_EQUAL_size_t(got[0].fault.context_size(), context.size());
    // The context holds every line once, in order, and ends with the fault's own entry.
    const size_t one = context.find(" pl in flash one\n");
    const size_t two = context.find(" pl in flash two\n\tand its next line\n");
    const size_t ring = context.find(" pl in the ring\n");
    TEST_ASSERT_TRUE(
        one != std::string::npos && two != std::string::npos && ring != std::string::npos
    );
    TEST_ASSERT_TRUE(one < two && two < ring);
    TEST_ASSERT_EQUAL_size_t(1, occurrences(context, "pl in the ring"));
    TEST_ASSERT_TRUE(last_line(context).ends_with(" E persistent_log: fault kind=pl_context why"));
    TEST_ASSERT_TRUE(context[0] >= '0' && context[0] <= '9');

    // The ring's lines, including the fault's entry, haven't been flushed yet. Once the log is
    // flushed, it is the same as the context.
    const std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " pl in the ring\n"));
    TEST_ASSERT_TRUE(all == context);
}

TEST_CASE(
    "persistent_log record_fault keeps the newest whole entries that fit",
    "[persistent_log]"
) {
    start_clean();
    // Write more than a slot holds to flash, then a few lines to the ring.
    char line[128];
    for (int i = 0; i < 90; ++i) {
        std::snprintf(line, sizeof line, "pl filler %03d %s", i, std::string(80, 'f').c_str());
        pl::append(line);
        if (i % 10 == 9) {
            TEST_ASSERT_TRUE(pl::try_flush().has_value());
        }
    }
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    for (int i = 0; i < 3; ++i) {
        std::snprintf(line, sizeof line, "pl ring %d\ncontinued", i);
        pl::append(line);
    }
    record("pl_full");

    auto got = list_faults();
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    const std::string& context = got[0].context;
    const size_t room = pl::fault_config{}.slot_size - 512;
    TEST_ASSERT_LESS_OR_EQUAL_size_t(room, context.size());
    // The context holds only complete entries, and fills the room to within one entry.
    TEST_ASSERT_GREATER_THAN_size_t(room - 200, context.size());
    TEST_ASSERT_TRUE(context[0] >= '0' && context[0] <= '9');
    TEST_ASSERT_FALSE(contains(context, "pl filler 000 "));
    TEST_ASSERT_TRUE(contains(context, "pl filler 089 "));
    TEST_ASSERT_TRUE(contains(context, " pl ring 2\n\tcontinued\n"));
    TEST_ASSERT_TRUE(last_line(context).ends_with(" E persistent_log: fault kind=pl_full"));
    // Once flushed, the log ends with exactly the bytes of the context.
    TEST_ASSERT_TRUE(read_all().ends_with(context));
}

TEST_CASE("persistent_log record_fault reads the end of the log a few times", "[persistent_log]") {
    // The store has the test partition's geometry: 16 sectors of 4 KiB, of which the fault slots
    // take 8. The log is filled with lines of about 150 bytes, flushed 12 at a time, so that it
    // wraps and holds records of about 1.8 KiB. The context holds about 7.5 KiB of the log in
    // flash, which is in about 3 sectors. The fault store passes the context in pieces of 256
    // bytes. Reading those sectors a few times takes about 40 KiB. Checking a record again for
    // each piece would take more than 150 KiB.
    auto store = std::make_unique<pl::testing::counting_store>(16, 4096);
    auto* raw = store.get();
    auto c = fault_config();
    c.storage = std::move(store);
    start_clean(std::move(c));
    char line[160];
    for (int i = 0; i < 300; ++i) {
        std::snprintf(line, sizeof line, "pl filler %03d %s", i, std::string(100, 'f').c_str());
        pl::append(line);
        if (i % 12 == 11) {
            TEST_ASSERT_TRUE(pl::try_flush().has_value());
        }
    }
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    pl::append("pl in the ring");
    raw->reset_count();
    record("pl_reads");
    const size_t read = raw->bytes_read();
    std::printf("persistent_log record_fault read %u bytes\n", unsigned(read));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(49152, read);

    auto got = list_faults();
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    const std::string& context = got[0].context;
    const size_t room = pl::fault_config{}.slot_size - 512;
    TEST_ASSERT_LESS_OR_EQUAL_size_t(room, context.size());
    TEST_ASSERT_GREATER_THAN_size_t(room - 200, context.size());
    TEST_ASSERT_TRUE(read_all().ends_with(context));
}

TEST_CASE(
    "persistent_log record_fault keeps the ring's newest entries when they fill a slot",
    "[persistent_log]"
) {
    // A slot of one 1 KiB sector holds 512 bytes of context, which is less than the ring holds.
    static_assert(CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE > 1024 - 512);
    auto c = fault_config();
    c.storage = std::make_unique<pl::ram_sector_store>(8, 1024);
    c.faults = pl::fault_config{.slot_size = 1024, .slot_count = 2};
    start_clean(std::move(c));
    pl::append("pl in flash");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    char line[64];
    for (int i = 0; i < 12; ++i) {
        std::snprintf(line, sizeof line, "pl ring %02d %s", i, std::string(30, 'r').c_str());
        pl::append(line);
    }
    record("pl_ring_only", "full");

    auto got = list_faults();
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    const std::string& context = got[0].context;
    TEST_ASSERT_LESS_OR_EQUAL_size_t(512, context.size());
    // The context holds only complete entries from the ring's unflushed bytes. It begins at the
    // start of an entry and ends with the fault's entry.
    TEST_ASSERT_GREATER_THAN_size_t(512 - 100, context.size());
    TEST_ASSERT_TRUE(context[0] >= '0' && context[0] <= '9');
    TEST_ASSERT_FALSE(contains(context, "pl in flash"));
    TEST_ASSERT_FALSE(contains(context, "pl ring 00 "));
    TEST_ASSERT_TRUE(contains(context, " pl ring 11 "));
    TEST_ASSERT_TRUE(
        last_line(context).ends_with(" E persistent_log: fault kind=pl_ring_only full")
    );
    std::string ring(4096, '\0');
    ring.resize(pl::copy_newest_in_memory(ring).size());
    TEST_ASSERT_TRUE(ring.ends_with(context));
    // Once flushed, the log also ends with the context.
    TEST_ASSERT_TRUE(read_all().ends_with(context));
}

TEST_CASE("persistent_log record_fault works with a writer task", "[persistent_log]") {
    auto c = fault_config();
    c.writer = pl::writer_config{};
    start_clean(std::move(c));
    pl::append("pl written");
    record("pl_writer");
    auto got = list_faults();
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    TEST_ASSERT_TRUE(contains(got[0].context, " pl written\n"));
    TEST_ASSERT_TRUE(
        last_line(got[0].context).ends_with(" E persistent_log: fault kind=pl_writer")
    );
}

TEST_CASE(
    "persistent_log record_fault logs that a fault it fails to store is not recorded",
    "[persistent_log]"
) {
    auto store = std::make_unique<pl::ram_sector_store>(8, 1024);
    auto* raw = store.get();
    auto c = fault_config();
    c.storage = std::move(store);
    c.faults = pl::fault_config{.slot_size = 1024, .slot_count = 2};
    start_clean(std::move(c));
    // The power is cut at the record's first erase, so storing the fault fails.
    raw->cut_after(0);
    TEST_ASSERT_TRUE(
        fails_with(pl::try_record_fault("pl_lost", "why"), idfxx::errc::invalid_state)
    );
    raw->restore_power();
    TEST_ASSERT_EQUAL_size_t(0, count());
    // The fault's entry is followed by an entry, without the detail, that says that the fault is
    // not recorded.
    std::string all = read_all();
    const size_t fault = all.find(" E persistent_log: fault kind=pl_lost why\n");
    const size_t lost = all.find(" W persistent_log: fault not recorded kind=pl_lost\n");
    TEST_ASSERT_TRUE(fault != std::string::npos && lost != std::string::npos);
    TEST_ASSERT_TRUE(fault < lost);
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, "not recorded"));
    // A suppressing record that fails adds the same entry, and doesn't suppress the kind.
    raw->cut_after(0);
    TEST_ASSERT_TRUE(
        fails_with(
            pl::try_record_fault("pl_lost", "why", pl::suppress_repeats), idfxx::errc::invalid_state
        )
    );
    raw->restore_power();
    TEST_ASSERT_EQUAL_size_t(
        2, occurrences(read_all(), " W persistent_log: fault not recorded kind=pl_lost\n")
    );
    // A record that stores its fault adds no such entry.
    TEST_ASSERT_TRUE(pl::try_record_fault("pl_lost", "why", pl::suppress_repeats).value_or(false));
    TEST_ASSERT_EQUAL_size_t(1, count());
    TEST_ASSERT_EQUAL_size_t(2, occurrences(read_all(), "not recorded"));
}

// =============================================================================
// Reading, counting and clearing
// =============================================================================

TEST_CASE(
    "persistent_log faults are counted, listed oldest first and cleared",
    "[persistent_log]"
) {
    start_clean();
    TEST_ASSERT_EQUAL_size_t(0, count());
    record("pl_a");
    record("pl_b");
    TEST_ASSERT_EQUAL_size_t(2, count());
    auto got = list_faults();
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(got[0].fault.kind() == "pl_a");
    TEST_ASSERT_TRUE(got[1].fault.kind() == "pl_b");
    TEST_ASSERT_EQUAL_UINT32(got[0].fault.sequence() + 1, got[1].fault.sequence());
    // Each context ends with its own fault's entry, and the second context also holds the first
    // fault's entry.
    TEST_ASSERT_TRUE(contains(got[1].context, "fault kind=pl_a\n"));
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    TEST_ASSERT_EQUAL_size_t(0, count());
    TEST_ASSERT_EQUAL_size_t(0, list_faults().size());
    record("pl_c");
    TEST_ASSERT_EQUAL_size_t(1, count());
}

TEST_CASE("persistent_log fault reader lists on across a record and a clear", "[persistent_log]") {
    start_clean();
    record("pl_a");
    record("pl_b");
    auto r = pl::try_read_faults();
    TEST_ASSERT_TRUE(r.has_value());
    pl::fault f;
    TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
    TEST_ASSERT_TRUE(f.kind() == "pl_a");
    record("pl_late");
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
    TEST_ASSERT_TRUE(f.kind() == "pl_b");
    TEST_ASSERT_TRUE(last_line(context_of(*r)).ends_with("fault kind=pl_b"));
    TEST_ASSERT_FALSE(r->try_next(f).value_or(true));
}

TEST_CASE(
    "persistent_log fault reader leaves out the oldest fault that a record or a clear overwrites",
    "[persistent_log]"
) {
    const size_t slots = pl::fault_config{}.slot_count;
    char kind[16];
    // Once every slot holds a fault, a record or a clear writes over the oldest fault. The first
    // pass records a fault, and the second pass clears the faults.
    for (const bool clear : {false, true}) {
        start_clean();
        for (size_t i = 0; i < slots; ++i) {
            std::snprintf(kind, sizeof kind, "pl_full%u", unsigned(i));
            record(kind);
        }
        TEST_ASSERT_EQUAL_size_t(slots, count());
        auto r = pl::try_read_faults();
        TEST_ASSERT_TRUE(r.has_value());
        if (clear) {
            TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
        } else {
            record("pl_late");
        }
        // The reader lists the faults that it started with, oldest first, except the fault that
        // was overwritten. Each context is whole, and ends with its own fault's entry.
        pl::fault f;
        for (size_t i = 1; i < slots; ++i) {
            TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
            std::snprintf(kind, sizeof kind, "pl_full%u", unsigned(i));
            TEST_ASSERT_TRUE(f.kind() == kind);
            TEST_ASSERT_FALSE(f.damaged());
            const std::string context = context_of(*r);
            TEST_ASSERT_EQUAL_size_t(f.context_size(), context.size());
            TEST_ASSERT_TRUE(last_line(context).ends_with(std::string("fault kind=") + kind));
        }
        TEST_ASSERT_FALSE(r->try_next(f).value_or(true));
        TEST_ASSERT_EQUAL_size_t(clear ? 0 : slots, count());
        stop_log();
    }
}

TEST_CASE("persistent_log fault reader reads no more once the log stops", "[persistent_log]") {
    start_clean();
    record("pl_a");
    auto r = pl::try_read_faults();
    TEST_ASSERT_TRUE(r.has_value());
    pl::fault f;
    TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    std::array<char, 16> buf;
    TEST_ASSERT_TRUE(fails_with(r->try_next_context_text(buf), idfxx::errc::invalid_state));
    pl::entry e;
    TEST_ASSERT_TRUE(fails_with(r->try_next_context(e), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(r->try_next(f), idfxx::errc::invalid_state));
    // After the log is started again, the reader still fails.
    TEST_ASSERT_TRUE(pl::try_start(fault_config()).has_value());
    TEST_ASSERT_TRUE(fails_with(r->try_next(f), idfxx::errc::invalid_state));
}

namespace {

// The rest of the reader's current fault's context, read as entries, each followed by a newline.
std::string context_entries_of(pl::fault_reader& r, size_t most = SIZE_MAX) {
    std::string s;
    pl::entry e;
    for (size_t i = 0; i < most; ++i) {
        auto more = r.try_next_context(e);
        TEST_ASSERT_TRUE(more.has_value());
        if (!*more) {
            break;
        }
        s += pl::detail::entry_access::stored(e);
        s += '\n';
    }
    return s;
}

} // namespace

TEST_CASE("persistent_log fault context reads back the same as entries", "[persistent_log]") {
    start_clean();
    pl::append("pl ctx one");
    pl::append("pl ctx two\npl ctx three");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    pl::append("pl ctx waiting");
    record("pl_ctx_a", "detail");
    pl::append("pl ctx after");
    record("pl_ctx_b");
    const auto raw = list_faults();
    TEST_ASSERT_EQUAL_size_t(2, raw.size());

    // Each context, read as entries and joined with newlines, is the context read as text.
    auto r = pl::try_read_faults();
    TEST_ASSERT_TRUE(r.has_value());
    pl::fault f;
    pl::entry e;
    // Before the first fault, there is no context.
    TEST_ASSERT_FALSE(r->try_next_context(e).value_or(true));
    for (const auto& l : raw) {
        TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
        TEST_ASSERT_EQUAL_UINT32(l.fault.sequence(), f.sequence());
        TEST_ASSERT_TRUE(context_entries_of(*r) == l.context);
    }
    TEST_ASSERT_FALSE(r->try_next(f).value_or(true));
    TEST_ASSERT_FALSE(r->try_next_context(e).value_or(true));

    // A context read only in part leaves nothing behind for the next fault's context.
    r = pl::try_read_faults();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
    const std::string first = context_entries_of(*r, 1);
    TEST_ASSERT_TRUE(raw[0].context.starts_with(first));
    TEST_ASSERT_TRUE(first.size() < raw[0].context.size());
    TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
    TEST_ASSERT_TRUE(context_entries_of(*r) == raw[1].context);

    // The last entry of a context is the entry that recording the fault logged, with the fault's
    // stamp.
    r = pl::try_read_faults();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
    // The entry is valid only until the next call, so its text is copied.
    std::string last_text;
    while (r->try_next_context(e).value_or(false)) {
        TEST_ASSERT_TRUE(e.stamped());
        last_text = pl::detail::entry_access::stored(e);
    }
    std::string_view rest = last_text;
    pl::entry last;
    TEST_ASSERT_TRUE(pl::next_entry(rest, last));
    TEST_ASSERT_TRUE(last.text() == "E persistent_log: fault kind=pl_ctx_a detail");
    TEST_ASSERT_EQUAL_UINT32(f.boot(), last.boot());
    TEST_ASSERT_EQUAL_UINT32(f.wake(), last.wake());
    TEST_ASSERT_TRUE(last.uptime() == f.uptime());
    TEST_ASSERT_TRUE(last.wall_time() == f.wall_time());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
// The iterators hold only a pointer to their reader.
static_assert(sizeof(pl::fault_reader::iterator) == sizeof(void*));
static_assert(sizeof(pl::fault_reader::context_range::iterator) == sizeof(void*));
// The context range refers to its reader, so only a reader that isn't a temporary gives one.
template<typename Reader>
concept has_context = requires(Reader&& r) { std::forward<Reader>(r).context(); };
static_assert(has_context<pl::fault_reader&>);
static_assert(!has_context<pl::fault_reader>);

TEST_CASE("persistent_log fault reader is a range of faults and contexts", "[persistent_log]") {
    start_clean();
    // With no faults, the reader and the context are empty.
    {
        auto faults = pl::read_faults();
        size_t n = 0;
        for (const auto& f : faults) {
            (void)f;
            ++n;
        }
        TEST_ASSERT_EQUAL_size_t(0, n);
        TEST_ASSERT_TRUE(faults.begin() == faults.end());
        TEST_ASSERT_TRUE(faults.context().begin() == std::default_sentinel);
    }
    pl::append("pl range one");
    record("pl_range_a", "first");
    pl::append("pl range two");
    record("pl_range_b");
    pl::append("pl range three\npl range four");
    record("pl_range_c", "third");
    const auto raw = list_faults();
    TEST_ASSERT_EQUAL_size_t(3, raw.size());

    // Before the first fault, there is no context.
    auto faults = pl::read_faults();
    TEST_ASSERT_TRUE(faults.context().begin() == std::default_sentinel);
    // The faults and their contexts are what the calls without ranges read. The second fault's
    // context is skipped, and the third fault's context is read only in part, so each increment
    // moves to the next fault whether or not its context was read.
    size_t i = 0;
    for (const auto& f : faults) {
        TEST_ASSERT_TRUE(i < raw.size());
        TEST_ASSERT_EQUAL_UINT32(raw[i].fault.sequence(), f.sequence());
        TEST_ASSERT_TRUE(f.kind() == raw[i].fault.kind());
        TEST_ASSERT_TRUE(f.detail() == raw[i].fault.detail());
        if (i == 0) {
            std::string context;
            for (const auto& e : faults.context()) {
                context += pl::detail::entry_access::stored(e);
                context += '\n';
            }
            TEST_ASSERT_TRUE(context == raw[i].context);
            // The whole context has been read, so a second range is empty.
            TEST_ASSERT_TRUE(faults.context().begin() == std::default_sentinel);
        } else if (i == 2) {
            std::string first;
            for (const auto& e : faults.context()) {
                first = pl::detail::entry_access::stored(e);
                break;
            }
            TEST_ASSERT_TRUE(raw[i].context.starts_with(first + "\n"));
        }
        ++i;
    }
    TEST_ASSERT_EQUAL_size_t(raw.size(), i);
    // Once every fault has been returned, a second range is empty, and there is no context.
    TEST_ASSERT_TRUE(faults.begin() == faults.end());
    TEST_ASSERT_TRUE(faults.context().begin() == std::default_sentinel);

    // A second loop over a reader continues after the fault that the first loop ended at.
    auto again = pl::read_faults();
    for (const auto& f : again) {
        TEST_ASSERT_EQUAL_UINT32(raw[0].fault.sequence(), f.sequence());
        break;
    }
    std::vector<uint32_t> rest;
    for (const auto& f : again) {
        rest.push_back(f.sequence());
    }
    TEST_ASSERT_EQUAL_size_t(2, rest.size());
    TEST_ASSERT_EQUAL_UINT32(raw[1].fault.sequence(), rest[0]);
    TEST_ASSERT_EQUAL_UINT32(raw[2].fault.sequence(), rest[1]);
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}
#endif

// =============================================================================
// Suppressed repeats
// =============================================================================

namespace {

bool record_suppressing(std::string_view kind, std::string_view detail = {}) {
    auto r = pl::try_record_fault(kind, detail, pl::suppress_repeats);
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), "try_record_fault failed");
    return *r;
}

// Records with the overload that takes no detail.
bool record_suppressing_kind(std::string_view kind) {
    auto r = pl::try_record_fault(kind, pl::suppress_repeats);
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), "try_record_fault failed");
    return *r;
}

bool record_plain(std::string_view kind, std::string_view detail = {}) {
    auto r = pl::try_record_fault(kind, detail);
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), "try_record_fault failed");
    return *r;
}

} // namespace

TEST_CASE("persistent_log suppressing records fail as other records do", "[persistent_log]") {
    stop_log();
    TEST_ASSERT_TRUE(
        fails_with(
            pl::try_record_fault("pl_sd", "x", pl::suppress_repeats), idfxx::errc::invalid_state
        )
    );
    TEST_ASSERT_TRUE(
        fails_with(pl::try_record_fault("pl_sd", pl::suppress_repeats), idfxx::errc::invalid_state)
    );
    // Before the log starts, lifting a suppression does nothing.
    pl::lift_suppression("pl_sd");
    start_clean();
    TEST_ASSERT_TRUE(
        fails_with(
            pl::try_record_fault("bad kind", "x", pl::suppress_repeats), idfxx::errc::invalid_arg
        )
    );
    TEST_ASSERT_TRUE(
        fails_with(pl::try_record_fault("cleared", pl::suppress_repeats), idfxx::errc::invalid_arg)
    );
    TEST_ASSERT_EQUAL_size_t(0, count());
    stop_log();
    auto c = fault_config();
    c.faults = std::nullopt;
    TEST_ASSERT_TRUE(pl::try_start(std::move(c)).has_value());
    TEST_ASSERT_TRUE(
        fails_with(
            pl::try_record_fault("pl_sd", "x", pl::suppress_repeats), idfxx::errc::not_supported
        )
    );
    TEST_ASSERT_TRUE(
        fails_with(pl::try_record_fault("pl_sd", pl::suppress_repeats), idfxx::errc::not_supported)
    );
}

TEST_CASE("persistent_log suppressed repeats are only logged", "[persistent_log]") {
    start_clean();
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
    TEST_ASSERT_FALSE(record_suppressing("pl_sd", "no card"));
    TEST_ASSERT_FALSE(record_suppressing("pl_sd", "no card"));
    TEST_ASSERT_EQUAL_size_t(1, count());
    const std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " E persistent_log: fault kind=pl_sd no card\n"));
    TEST_ASSERT_EQUAL_size_t(
        2, occurrences(all, " W persistent_log: fault repeat kind=pl_sd no card\n")
    );
    // A fault of another kind isn't a repeat.
    TEST_ASSERT_TRUE(record_suppressing("pl_panel"));
    TEST_ASSERT_FALSE(record_suppressing("pl_panel"));
    TEST_ASSERT_TRUE(contains(read_all(), " W persistent_log: fault repeat kind=pl_panel\n"));
    TEST_ASSERT_EQUAL_size_t(2, count());
}

TEST_CASE(
    "persistent_log suppressed kind's faults are repeats whatever their detail", "[persistent_log]"
) {
    start_clean();
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
    // A fault with another detail is also a repeat, whichever form records it, and writes no slot.
    TEST_ASSERT_FALSE(record_suppressing("pl_sd", "bad card"));
    TEST_ASSERT_FALSE(record_plain("pl_sd", "bad card"));
    TEST_ASSERT_FALSE(record_plain("pl_sd"));
    TEST_ASSERT_EQUAL_size_t(1, count());
    const std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, "fault kind=pl_sd"));
    // The repeat's line still shows its own detail.
    TEST_ASSERT_EQUAL_size_t(
        2, occurrences(all, " W persistent_log: fault repeat kind=pl_sd bad card\n")
    );
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " W persistent_log: fault repeat kind=pl_sd\n"));
    // Once its suppression is lifted, the kind is recorded again, whatever the detail.
    pl::lift_suppression("pl_sd");
    TEST_ASSERT_TRUE(record_plain("pl_sd", "bad card"));
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "worn card"));
    TEST_ASSERT_FALSE(record_suppressing("pl_sd", "no card"));
    TEST_ASSERT_EQUAL_size_t(3, count());
}

TEST_CASE(
    "persistent_log suppressing record without a detail suppresses the kind", "[persistent_log]"
) {
    start_clean();
    TEST_ASSERT_TRUE(record_suppressing_kind("pl_sd"));
    // Every record of the kind is a repeat, with or without a detail, whichever form records it.
    TEST_ASSERT_FALSE(record_suppressing_kind("pl_sd"));
    TEST_ASSERT_FALSE(record_plain("pl_sd"));
    TEST_ASSERT_FALSE(record_plain("pl_sd", "no card"));
    TEST_ASSERT_FALSE(record_suppressing("pl_sd", "no card"));
    TEST_ASSERT_EQUAL_size_t(1, count());
    const std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(2, occurrences(all, " W persistent_log: fault repeat kind=pl_sd\n"));
    TEST_ASSERT_EQUAL_size_t(
        2, occurrences(all, " W persistent_log: fault repeat kind=pl_sd no card\n")
    );
    pl::lift_suppression("pl_sd");
    TEST_ASSERT_TRUE(record_suppressing_kind("pl_sd"));
    TEST_ASSERT_EQUAL_size_t(2, count());
}

TEST_CASE("persistent_log plain record of a suppressed kind is only logged", "[persistent_log]") {
    start_clean();
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
    TEST_ASSERT_FALSE(record_plain("pl_sd", "no card"));
    TEST_ASSERT_EQUAL_size_t(1, count());
    const std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " E persistent_log: fault kind=pl_sd no card\n"));
    TEST_ASSERT_EQUAL_size_t(
        1, occurrences(all, " W persistent_log: fault repeat kind=pl_sd no card\n")
    );
}

TEST_CASE("persistent_log identical plain records are each recorded", "[persistent_log]") {
    start_clean();
    TEST_ASSERT_TRUE(record_plain("pl_sd", "no card"));
    TEST_ASSERT_TRUE(record_plain("pl_sd", "no card"));
    TEST_ASSERT_EQUAL_size_t(2, count());
    const std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(2, occurrences(all, " E persistent_log: fault kind=pl_sd no card\n"));
    TEST_ASSERT_FALSE(contains(all, "fault repeat kind="));
    // Neither record was remembered, so a suppressing record of the same fault is recorded.
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
    TEST_ASSERT_EQUAL_size_t(3, count());
}

TEST_CASE("persistent_log suppression lifted records the fault again", "[persistent_log]") {
    start_clean();
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
    pl::lift_suppression("pl_sd");
    TEST_ASSERT_EQUAL_size_t(
        1, occurrences(read_all(), " I persistent_log: fault suppression lifted kind=pl_sd\n")
    );
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
    TEST_ASSERT_EQUAL_size_t(2, count());
    // Lifting a kind that isn't remembered, or one that was already lifted, logs nothing.
    pl::lift_suppression("pl_never");
    pl::lift_suppression("pl_sd");
    pl::lift_suppression("pl_sd");
    const std::string all = read_all();
    TEST_ASSERT_FALSE(contains(all, "kind=pl_never"));
    TEST_ASSERT_EQUAL_size_t(
        2, occurrences(all, " I persistent_log: fault suppression lifted kind=pl_sd\n")
    );
}

TEST_CASE(
    "persistent_log suppressed kinds past the most held forget the oldest",
    "[persistent_log]"
) {
    start_clean();
    constexpr int most = CONFIG_IDFXX_PERSISTENT_LOG_SUPPRESSED_FAULTS;
    char kind[16];
    for (int i = 0; i <= most; ++i) {
        std::snprintf(kind, sizeof kind, "pl_k%d", i);
        TEST_ASSERT_TRUE(record_suppressing(kind));
    }
    // The last kind replaced the first, and the rest are still remembered.
    std::snprintf(kind, sizeof kind, "pl_k%d", most);
    TEST_ASSERT_FALSE(record_suppressing(kind));
    std::snprintf(kind, sizeof kind, "pl_k%d", 1);
    TEST_ASSERT_FALSE(record_suppressing(kind));
    TEST_ASSERT_TRUE(record_suppressing("pl_k0"));
}

TEST_CASE("persistent_log clear_faults forgets the suppressed kinds", "[persistent_log]") {
    start_clean();
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
}

TEST_CASE(
    "persistent_log suppressed kinds last across a restart, not a bad RTC record",
    "[persistent_log]"
) {
    start_clean();
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
    // A reset keeps the suppressed faults.
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    internal::check_rtc(false);
    TEST_ASSERT_TRUE(pl::try_start(fault_config()).has_value());
    TEST_ASSERT_FALSE(record_suppressing("pl_sd", "no card"));
    // An RTC record that fails its check, as after a power cut, is reset, and the suppressed faults
    // are gone.
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    internal::rtc().faults.crc ^= 1;
    internal::check_rtc(false);
    TEST_ASSERT_TRUE(pl::try_start(fault_config()).has_value());
    TEST_ASSERT_TRUE(record_suppressing("pl_sd", "no card"));
}

TEST_CASE(
    "persistent_log suppressed kind whose CRC is 0 is told from an empty slot",
    "[persistent_log]"
) {
    constexpr std::string_view kind = "6acguoAe$A";
    TEST_ASSERT_EQUAL_HEX32(
        0, esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(kind.data()), kind.size())
    );
    start_clean();
    // An empty slot holds kind 0. If a kind CRC of 0 weren't mapped to another value, the kind
    // would match an empty slot, and the first record would read as a repeat.
    TEST_ASSERT_TRUE(record_suppressing(kind));
    TEST_ASSERT_FALSE(record_suppressing(kind));
    pl::lift_suppression(kind);
    TEST_ASSERT_EQUAL_size_t(
        1, occurrences(read_all(), " I persistent_log: fault suppression lifted kind=6acguoAe$A\n")
    );
    TEST_ASSERT_TRUE(record_suppressing(kind));
    // After a clear, every slot is empty and the kind isn't held. Lifting its suppression logs
    // nothing, and the next record of the kind is recorded.
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    pl::lift_suppression(kind);
    TEST_ASSERT_EQUAL_size_t(1, occurrences(read_all(), "lifted kind=6acguoAe$A"));
    TEST_ASSERT_TRUE(record_suppressing(kind));
    TEST_ASSERT_EQUAL_size_t(1, count());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("persistent_log fault calls throw what the try_ calls return", "[persistent_log]") {
    auto code_of = [](auto&& call) {
        try {
            call();
        } catch (const std::system_error& e) {
            return e.code();
        }
        return std::error_code();
    };
    stop_log();
    TEST_ASSERT_TRUE(code_of([] { pl::record_fault("pl_kind"); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(
        code_of([] { pl::record_fault("pl_kind", "x", pl::suppress_repeats); })
        == idfxx::errc::invalid_state
    );
    TEST_ASSERT_TRUE(
        code_of([] { pl::record_fault("pl_kind", pl::suppress_repeats); })
        == idfxx::errc::invalid_state
    );
    TEST_ASSERT_TRUE(code_of([] { (void)pl::read_faults(); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([] { pl::clear_faults(); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([] { (void)pl::fault_count(); }) == idfxx::errc::invalid_state);

    start_clean();
    TEST_ASSERT_TRUE(code_of([] { pl::record_fault("bad kind"); }) == idfxx::errc::invalid_arg);
    pl::record_fault("pl_thrown", "detail");
    TEST_ASSERT_EQUAL_size_t(1, pl::fault_count());
    auto r = pl::read_faults();
    pl::fault f;
    TEST_ASSERT_TRUE(r.next(f));
    TEST_ASSERT_TRUE(f.kind() == "pl_thrown");
    std::array<char, 64> buf;
    const std::string_view part = r.next_context_text(buf);
    TEST_ASSERT_TRUE(part.data() == buf.data());
    TEST_ASSERT_EQUAL_size_t(64, part.size());
    TEST_ASSERT_FALSE(r.next(f));
    TEST_ASSERT_TRUE(pl::record_fault("pl_thrown_on", "on", pl::suppress_repeats));
    TEST_ASSERT_FALSE(pl::record_fault("pl_thrown_on", "on", pl::suppress_repeats));
    TEST_ASSERT_FALSE(pl::record_fault("pl_thrown_on", "on"));
    TEST_ASSERT_TRUE(pl::record_fault("pl_thrown_kind", pl::suppress_repeats));
    TEST_ASSERT_FALSE(pl::record_fault("pl_thrown_kind", pl::suppress_repeats));
    TEST_ASSERT_FALSE(pl::record_fault("pl_thrown_kind"));
    TEST_ASSERT_TRUE(
        code_of([] { pl::record_fault("", "on", pl::suppress_repeats); })
        == idfxx::errc::invalid_arg
    );
    TEST_ASSERT_TRUE(
        code_of([] { pl::record_fault("", pl::suppress_repeats); }) == idfxx::errc::invalid_arg
    );
    auto entries = pl::read_faults();
    TEST_ASSERT_TRUE(entries.next(f));
    pl::entry e;
    TEST_ASSERT_TRUE(entries.next_context(e));
    TEST_ASSERT_TRUE(e.stamped());
    pl::clear_faults();
    TEST_ASSERT_EQUAL_size_t(0, pl::fault_count());
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    TEST_ASSERT_TRUE(code_of([&] { (void)r.next(f); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([&] { (void)entries.next_context(e); }) == idfxx::errc::invalid_state);
}
#endif
