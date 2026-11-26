// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Tests for idfxx_persistent_log's storage. The tests start the service on a store that they pass
// in, rather than on a partition. The stores are RAM stores, small enough that the log wraps after
// a few lines. The tests also cover the range of a store that the log uses and the range that the
// fault slots use.

#include "../src/entry_buffer.hpp"
#include "../src/rtc.hpp"
#include "../src/sector_store.hpp"
#include "../src/store_range.hpp"
#include "entry_helpers.hpp"
#include "idfxx/persistent_log"
#include "sdkconfig.h"
#include "test_cleanup.hpp"
#include "unity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pl = idfxx::persistent_log;
namespace internal = idfxx::persistent_log::internal;

namespace {

template<typename T>
bool fails_with(const idfxx::result<T>& r, idfxx::errc e) {
    return !r.has_value() && r.error() == e;
}

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

std::string_view text_of(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void stop_log() {
    (void)pl::try_stop();
}

// A store for the tests to pass to the log. It passes each call on to a RAM store that outlives
// it, so that the test can look at the bytes after the log has destroyed the store, and start
// again on them. It sets a flag when it is destroyed, and fails its erases while another flag is
// set. When a count of reads is set (0 or more), it lets that many reads succeed, fails the next
// one, and then lets every read succeed again. The constructor that takes a geometry of its own
// makes a store that the log can't use, so that store is never called.
class test_store final : public pl::sector_store {
public:
    explicit test_store(
        std::shared_ptr<pl::ram_sector_store> ram,
        std::shared_ptr<bool> destroyed = {},
        std::shared_ptr<bool> fail_erases = {},
        std::shared_ptr<int> reads_before_failure = {}
    )
        : sector_store(ram->size(), ram->sector_size(), ram->write_size())
        , _ram(std::move(ram))
        , _destroyed(std::move(destroyed))
        , _fail_erases(std::move(fail_erases))
        , _reads_before_failure(std::move(reads_before_failure)) {}

    test_store(
        size_t size,
        size_t sector_size,
        size_t write_size,
        std::shared_ptr<bool> destroyed = {}
    )
        : sector_store(size, sector_size, write_size)
        , _destroyed(std::move(destroyed)) {}

    ~test_store() override {
        if (_destroyed) {
            *_destroyed = true;
        }
    }

private:
    idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
        if (_failing()) {
            return idfxx::error(idfxx::errc::fail);
        }
        return _ram->try_read(offset, dst);
    }
    idfxx::result<void> do_read_raw(size_t offset, idfxx::byte_span dst) const override {
        if (_failing()) {
            return idfxx::error(idfxx::errc::fail);
        }
        return _ram->try_read_raw(offset, dst);
    }
    // Counts a read, and returns true for the read that fails.
    bool _failing() const {
        if (!_reads_before_failure || *_reads_before_failure < 0) {
            return false;
        }
        return (*_reads_before_failure)-- == 0;
    }
    idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
        return _ram->try_write(offset, src);
    }
    idfxx::result<void> do_erase_sector(size_t index) override {
        if (_fail_erases && *_fail_erases) {
            return idfxx::error(idfxx::errc::fail);
        }
        return _ram->try_erase_sector(index);
    }

    std::shared_ptr<pl::ram_sector_store> _ram;
    std::shared_ptr<bool> _destroyed;
    std::shared_ptr<bool> _fail_erases;
    std::shared_ptr<int> _reads_before_failure;
};

std::shared_ptr<pl::ram_sector_store> make_ram(size_t sectors, size_t sector_size) {
    return std::make_shared<pl::ram_sector_store>(sectors, sector_size);
}

// A configuration that keeps the log in @p store, with no writer, capture or fault slots, so that
// the log holds only the test's own lines.
pl::config store_config(std::unique_ptr<pl::sector_store> store) {
    pl::config c;
    c.storage = std::move(store);
    c.boot_count = 7;
    c.wall_clock = [] { return std::optional<std::chrono::system_clock::time_point>{}; };
    c.writer = std::nullopt;
#ifdef CONFIG_IDFXX_STD_FORMAT
    c.capture = std::nullopt;
#endif
    c.faults = std::nullopt;
    return c;
}

pl::config
store_config(std::shared_ptr<pl::ram_sector_store> ram, std::shared_ptr<bool> destroyed = {}) {
    return store_config(std::make_unique<test_store>(std::move(ram), std::move(destroyed)));
}

std::string read_all() {
    auto r = pl::try_read_text();
    TEST_ASSERT_TRUE(r.has_value());
    std::string all;
    std::array<char, 100> chunk;
    while (true) {
        auto n = r->try_next(chunk);
        TEST_ASSERT_TRUE(n.has_value());
        if (n->empty()) {
            return all;
        }
        all.append(*n);
    }
}

// Writes @p count numbered lines, starting with "<prefix> 000", and flushes after every ten.
void write_lines(const char* prefix, int count) {
    for (int i = 0; i < count; ++i) {
        std::array<char, 64> line;
        const int n = std::snprintf(line.data(), line.size(), "%s %03d", prefix, i);
        pl::append({line.data(), static_cast<size_t>(n)});
        if (i % 10 == 9) {
            TEST_ASSERT_TRUE(pl::try_flush().has_value());
        }
    }
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
}

} // namespace

TEST_CASE("store_range offsets into its base's sectors", "[persistent_log]") {
    pl::ram_sector_store base(8, 256);
    pl::store_range range(base, 2, 3);
    TEST_ASSERT_EQUAL_size_t(3 * 256, range.size());
    TEST_ASSERT_EQUAL_size_t(256, range.sector_size());
    TEST_ASSERT_EQUAL_size_t(base.write_size(), range.write_size());
    TEST_ASSERT_EQUAL_size_t(3, range.sector_count());

    const std::array<std::byte, 4> data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    TEST_ASSERT_TRUE(range.try_write(256 + 4, data).has_value());
    TEST_ASSERT_TRUE(std::ranges::equal(base.bytes().subspan(3 * 256 + 4, 4), data));
    std::array<std::byte, 4> back{};
    TEST_ASSERT_TRUE(range.try_read(256 + 4, back).has_value());
    TEST_ASSERT_TRUE(back == data);
    TEST_ASSERT_TRUE(range.try_read_raw(256 + 4, back).has_value());
    TEST_ASSERT_TRUE(back == data);

    // Erasing the range's sector 1 erases the base's sector 3, and no other sector.
    TEST_ASSERT_TRUE(base.try_write(2 * 256, data).has_value());
    TEST_ASSERT_TRUE(range.try_erase_sector(1).has_value());
    TEST_ASSERT_EQUAL(256, std::ranges::count(base.bytes().subspan(3 * 256, 256), std::byte{0xFF}));
    TEST_ASSERT_TRUE(std::ranges::equal(base.bytes().subspan(2 * 256, 4), data));

    // Nothing past the range's end can be read, written or erased, even though the base store
    // continues.
    TEST_ASSERT_TRUE(fails_with(range.try_read(3 * 256 - 2, back), idfxx::errc::invalid_arg));
    TEST_ASSERT_TRUE(fails_with(range.try_write(3 * 256, data), idfxx::errc::invalid_arg));
    TEST_ASSERT_TRUE(fails_with(range.try_erase_sector(3), idfxx::errc::invalid_arg));
}

TEST_CASE("persistent_log keeps the log in a store it is given", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    auto ram = make_ram(16, 1024);
    TEST_ASSERT_TRUE(pl::try_start(store_config(ram)).has_value());
    pl::append("pl in a store");
    const std::string all = read_all();
    TEST_ASSERT_TRUE(all.ends_with(" - pl in a store\n"));
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    TEST_ASSERT_TRUE(contains(text_of(ram->bytes()), "pl in a store\n"));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("persistent_log start takes a store given in place", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    auto ram = make_ram(16, 1024);
    pl::start({
        .storage = std::make_unique<test_store>(ram),
        .writer = std::nullopt,
#ifdef CONFIG_IDFXX_STD_FORMAT
        .capture = std::nullopt,
#endif
        .faults = std::nullopt,
    });
    pl::append("pl started in place");
    pl::stop();
    TEST_ASSERT_TRUE(contains(text_of(ram->bytes()), "pl started in place\n"));
}
#endif

TEST_CASE("persistent_log destroys a store it is given at stop", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    auto destroyed = std::make_shared<bool>(false);
    TEST_ASSERT_TRUE(pl::try_start(store_config(make_ram(16, 1024), destroyed)).has_value());
    pl::append("pl before stop");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    TEST_ASSERT_FALSE(*destroyed);
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    TEST_ASSERT_TRUE(*destroyed);
}

TEST_CASE("persistent_log destroys a store it is given when start fails", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    // A bad writer setting.
    auto destroyed = std::make_shared<bool>(false);
    auto c = store_config(make_ram(16, 1024), destroyed);
    c.writer = pl::writer_config{.threshold = 0};
    TEST_ASSERT_TRUE(fails_with(pl::try_start(std::move(c)), idfxx::errc::invalid_arg));
    TEST_ASSERT_TRUE(*destroyed);

    // Fault slots that don't fit.
    destroyed = std::make_shared<bool>(false);
    c = store_config(make_ram(16, 1024), destroyed);
    c.faults = pl::fault_config{.slot_size = 8 * 1024, .slot_count = 2};
    TEST_ASSERT_TRUE(fails_with(pl::try_start(std::move(c)), idfxx::errc::invalid_arg));
    TEST_ASSERT_TRUE(*destroyed);

    // A geometry that the log can't use, which is found once the store is opened.
    destroyed = std::make_shared<bool>(false);
    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(store_config(std::make_unique<test_store>(1024, 1024, 1, destroyed))),
        idfxx::errc::invalid_arg
    ));
    TEST_ASSERT_TRUE(*destroyed);

    // The log has already started.
    TEST_ASSERT_TRUE(pl::try_start(store_config(make_ram(16, 1024))).has_value());
    destroyed = std::make_shared<bool>(false);
    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(store_config(make_ram(16, 1024), destroyed)), idfxx::errc::invalid_state
    ));
    TEST_ASSERT_TRUE(*destroyed);
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

TEST_CASE(
    "persistent_log start rejects a null store or one the log can't take",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(store_config(std::unique_ptr<pl::sector_store>{})), idfxx::errc::invalid_arg
    ));

    auto rejects = [](size_t size, size_t sector_size, size_t write_size) {
        return fails_with(
            pl::try_start(
                store_config(std::make_unique<test_store>(size, sector_size, write_size))
            ),
            idfxx::errc::invalid_arg
        );
    };
    // No sectors at all, or a size that isn't a whole number of sectors.
    TEST_ASSERT_TRUE(rejects(4096, 0, 1));
    TEST_ASSERT_TRUE(rejects(4096 + 512, 1024, 1));
    // Sectors that are too small, too large, or not a whole number of blocks.
    TEST_ASSERT_TRUE(rejects(8 * 32, 32, 1));
    TEST_ASSERT_TRUE(rejects(2 * 0x20000, 0x20000, 1));
    TEST_ASSERT_TRUE(rejects(8 * 200, 200, 1));
    // A write size for which the log's blocks aren't a whole number of writes.
    TEST_ASSERT_TRUE(rejects(8 * 1024, 1024, 3));
    TEST_ASSERT_TRUE(rejects(8 * 1024, 1024, 32));
    TEST_ASSERT_TRUE(rejects(8 * 1024, 1024, 0));
    // A single sector leaves the log no sector to move on to.
    TEST_ASSERT_TRUE(rejects(1024, 1024, 1));

    // Two sectors are enough.
    TEST_ASSERT_TRUE(pl::try_start(store_config(make_ram(2, 1024))).has_value());
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

TEST_CASE("persistent_log start rejects fault slots that don't fit a store", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    // A store of sixteen sectors, which are 1 KiB unless another size is given.
    auto with_faults = [](pl::fault_config f, size_t sector = 1024) {
        auto c = store_config(make_ram(16, sector));
        c.faults = f;
        return c;
    };
    // A slot size that isn't a whole number of sectors.
    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(with_faults({.slot_size = 1536, .slot_count = 2})), idfxx::errc::invalid_arg
    ));
    // Slots that take the whole store, or more.
    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(with_faults({.slot_size = 4096, .slot_count = 4})), idfxx::errc::invalid_arg
    ));
    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(with_faults({.slot_size = 32 * 1024, .slot_count = 1})),
        idfxx::errc::invalid_arg
    ));
    // Slots that leave one sector over, which is too few for the log.
    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(with_faults({.slot_size = 5 * 1024, .slot_count = 3})),
        idfxx::errc::invalid_arg
    ));
    // A slot no larger than its header.
    TEST_ASSERT_TRUE(fails_with(
        pl::try_start(with_faults({.slot_size = 512, .slot_count = 2}, 256)),
        idfxx::errc::invalid_arg
    ));

    // Two sectors left over are enough.
    TEST_ASSERT_TRUE(
        pl::try_start(with_faults({.slot_size = 7 * 1024, .slot_count = 2})).has_value()
    );
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

TEST_CASE(
    "persistent_log keeps fault slots carved from a store out of the log",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    // Eight 256-byte sectors for the log, then two 1 KiB slots.
    constexpr size_t sector = 256;
    constexpr size_t log_bytes = 8 * sector;
    auto ram = make_ram(16, sector);
    auto c = store_config(ram);
    c.faults = pl::fault_config{.slot_size = 4 * sector, .slot_count = 2};
    TEST_ASSERT_TRUE(pl::try_start(std::move(c)).has_value());
    const std::vector<std::byte> slots(ram->bytes().begin() + log_bytes, ram->bytes().end());
    ram->reset_counts();

    // Write about 4 KiB, which is twice the log's room.
    write_lines("pl wrap", 160);
    const std::string all = read_all();
    TEST_ASSERT_TRUE(pl::try_stop().has_value());

    // The log wrapped. The oldest lines are gone, the newest are kept, and more sectors were erased
    // than the log has.
    TEST_ASSERT_FALSE(contains(all, "pl wrap 000\n"));
    TEST_ASSERT_TRUE(contains(all, "pl wrap 159\n"));
    TEST_ASSERT_TRUE(ram->erases() > 8);
    TEST_ASSERT_EQUAL_size_t(0, ram->overwrites());
    // The slots' bytes are as start left them, and the log's text is only in the sectors before
    // them.
    TEST_ASSERT_TRUE(std::ranges::equal(ram->bytes().subspan(log_bytes), slots));
    TEST_ASSERT_FALSE(contains(text_of(ram->bytes().subspan(log_bytes)), "pl wrap"));
    TEST_ASSERT_TRUE(contains(text_of(ram->bytes().first(log_bytes)), "pl wrap 159"));
}

TEST_CASE(
    "persistent_log doesn't trust a hint left on another store of the same geometry",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    constexpr size_t sector = 512;
    constexpr size_t sectors = 8;
    auto a = make_ram(sectors, sector);
    auto b = make_ram(sectors, sector);

    // B holds a few lines, in its first sector.
    TEST_ASSERT_TRUE(pl::try_start(store_config(b)).has_value());
    write_lines("pl B", 3);
    TEST_ASSERT_TRUE(pl::try_stop().has_value());

    // A holds more lines, so that its head, and the hint that start keeps for it, are several
    // sectors further on.
    TEST_ASSERT_TRUE(pl::try_start(store_config(a)).has_value());
    write_lines("pl A", 80);
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    const internal::hint_record hint = internal::rtc().hint;
    TEST_ASSERT_TRUE(internal::valid(hint));
    TEST_ASSERT_EQUAL_UINT32(1, hint.has_hint);
    TEST_ASSERT_EQUAL_UINT32(sector, hint.sector_size);
    TEST_ASSERT_EQUAL_UINT32(sectors * sector, hint.size);
    TEST_ASSERT_TRUE(hint.sector >= 2);

    // When the log is started on B with A's hint, it finds B's own head.
    b->reset_counts();
    TEST_ASSERT_TRUE(pl::try_start(store_config(b)).has_value());
    pl::append("pl B after");
    const std::string all = read_all();
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    TEST_ASSERT_FALSE(contains(all, "pl A"));
    const size_t last_b = all.find("pl B 002\n");
    const size_t after = all.find("pl B after\n");
    TEST_ASSERT_TRUE(last_b != std::string::npos);
    TEST_ASSERT_TRUE(after != std::string::npos);
    TEST_ASSERT_TRUE(after > last_b);
    TEST_ASSERT_TRUE(all.ends_with(" - pl B after\n"));
    TEST_ASSERT_EQUAL_size_t(0, b->overwrites());
    // B's new line is in its first sector, after its earlier lines.
    TEST_ASSERT_TRUE(contains(text_of(b->bytes().first(sector)), "pl B after\n"));
}

namespace {

// A store that, once armed, copies the RTC ring at its first write. The copy is what a reset
// during that write would leave.
class reset_store final : public pl::sector_store {
public:
    reset_store(
        std::shared_ptr<pl::ram_sector_store> ram,
        std::shared_ptr<std::optional<internal::ring_type>> saved
    )
        : sector_store(ram->size(), ram->sector_size(), ram->write_size())
        , _ram(std::move(ram))
        , _saved(std::move(saved)) {}

private:
    idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
        return _ram->try_read(offset, dst);
    }
    idfxx::result<void> do_read_raw(size_t offset, idfxx::byte_span dst) const override {
        return _ram->try_read_raw(offset, dst);
    }
    idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
        if (_saved && !*_saved) {
            *_saved = internal::rtc_ring();
        }
        return _ram->try_write(offset, src);
    }
    idfxx::result<void> do_erase_sector(size_t index) override {
        return _ram->try_erase_sector(index);
    }

    std::shared_ptr<pl::ram_sector_store> _ram;
    std::shared_ptr<std::optional<internal::ring_type>> _saved;
};

size_t occurrences(std::string_view hay, std::string_view needle) {
    size_t n = 0;
    for (size_t at = 0; (at = hay.find(needle, at)) != std::string_view::npos; ++at) {
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE(
    "persistent_log doesn't write a flush again after a reset during it",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    auto ram = make_ram(16, 512);
    auto saved = std::make_shared<std::optional<internal::ring_type>>();

    // Flush a line, put the ring back as a reset during the write would leave it, and start again.
    TEST_ASSERT_TRUE(
        pl::try_start(store_config(std::make_unique<reset_store>(ram, saved))).has_value()
    );
    pl::append("pl in flight");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    TEST_ASSERT_TRUE(saved->has_value());
    TEST_ASSERT_TRUE(contains({(*saved)->data, sizeof (*saved)->data}, "pl in flight\n"));
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    internal::rtc_ring() = **saved;
    TEST_ASSERT_TRUE(
        pl::try_start(store_config(std::make_unique<reset_store>(ram, nullptr))).has_value()
    );
    std::string all = read_all();
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    TEST_ASSERT_EQUAL(1, occurrences(all, "pl in flight\n"));

    // The same holds after an overrun. The line that reports the loss isn't written again either.
    saved->reset();
    TEST_ASSERT_TRUE(
        pl::try_start(store_config(std::make_unique<reset_store>(ram, saved))).has_value()
    );
    for (size_t i = 0; i * 32 < 2 * sizeof internal::rtc_ring().data; ++i) {
        pl::append("pl overrun 0123456789abcdefghij");
    }
    pl::append("pl last");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    TEST_ASSERT_TRUE(saved->has_value());
    TEST_ASSERT_TRUE(contains({(*saved)->data, sizeof (*saved)->data}, "pl last\n"));
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    internal::rtc_ring() = **saved;
    TEST_ASSERT_TRUE(
        pl::try_start(store_config(std::make_unique<reset_store>(ram, nullptr))).has_value()
    );
    all = read_all();
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    TEST_ASSERT_EQUAL(1, occurrences(all, "W persistent_log: dropped "));
    TEST_ASSERT_EQUAL(1, occurrences(all, "pl last\n"));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
namespace {

// A wall clock that can be moved but not copied. Copying it throws, as a failed allocation would.
struct uncopyable_clock {
    uncopyable_clock() = default;
    uncopyable_clock(uncopyable_clock&&) = default;
    uncopyable_clock(const uncopyable_clock&) { throw std::bad_alloc(); }

    std::optional<std::chrono::system_clock::time_point> operator()() const { return {}; }
};

// Starts on a store with `tweak` applied to its configuration, which makes the start throw
// std::bad_alloc. Then checks that the log is stopped and its store destroyed, and that nothing
// left from the failed start gets in the way of the next start.
void check_start_throws(void (*tweak)(pl::config&)) {
    idfxx::test::run_after_each_test(stop_log);
    auto ram = make_ram(16, 1024);
    auto destroyed = std::make_shared<bool>(false);
    auto c = store_config(ram, destroyed);
    tweak(c);
    bool threw = false;
    try {
        pl::start(std::move(c));
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    TEST_ASSERT_TRUE(threw);
    TEST_ASSERT_TRUE(*destroyed);
    TEST_ASSERT_TRUE(fails_with(pl::try_stop(), idfxx::errc::invalid_state));
    TEST_ASSERT_TRUE(fails_with(pl::try_read_text(), idfxx::errc::invalid_state));

    TEST_ASSERT_TRUE(pl::try_start(store_config(ram)).has_value());
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

} // namespace

TEST_CASE(
    "persistent_log a start that can't make its writer leaves the log stopped",
    "[persistent_log]"
) {
    check_start_throws([](pl::config& c) {
        c.faults = pl::fault_config{.slot_size = 1024, .slot_count = 2};
        // More stack than the heap holds, but no more than FreeRTOS accepts.
        c.writer = pl::writer_config{.stack_size = 64 * 1024 * 1024};
    });
}

TEST_CASE(
    "persistent_log a start that throws opening the log leaves it stopped",
    "[persistent_log]"
) {
    // The wall clock is copied when the log is opened.
    check_start_throws([](pl::config& c) { c.wall_clock = uncopyable_clock{}; });
}
#endif

TEST_CASE(
    "persistent_log clear drops the waiting lines even when clearing flash fails",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    auto failing = std::make_shared<bool>(false);
    auto store = std::make_unique<test_store>(make_ram(16, 1024), nullptr, failing);
    TEST_ASSERT_TRUE(pl::try_start(store_config(std::move(store))).has_value());
    pl::append("pl clear flushed");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    pl::append("pl clear waiting");

    *failing = true;
    TEST_ASSERT_TRUE(fails_with(pl::try_clear(), idfxx::errc::fail));
    *failing = false;
    std::array<char, 256> newest;
    TEST_ASSERT_TRUE(pl::copy_newest_in_memory(newest).empty());
    // Flash keeps what it had, and the line that was waiting is gone.
    const std::string all = read_all();
    TEST_ASSERT_TRUE(contains(all, "pl clear flushed\n"));
    TEST_ASSERT_FALSE(contains(all, "pl clear waiting"));
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

using pl::testing::last_of;
using pl::testing::read_newest;
using pl::testing::stored_entries;

TEST_CASE(
    "persistent_log read_newest finds the newest entries once the log has wrapped",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    // With small sectors, the flushes split their records across sectors, and the log wraps.
    for (size_t sector_size : {size_t{128}, size_t{512}}) {
        auto ram = make_ram(4, sector_size);
        TEST_ASSERT_TRUE(pl::try_start(store_config(ram)).has_value());
        TEST_ASSERT_TRUE(pl::try_clear().has_value());
        write_lines("pl wrap", 80);
        auto r = pl::try_read();
        TEST_ASSERT_TRUE(r.has_value());
        const auto all = stored_entries(*r);
        // The oldest entries were overwritten. The rest are whole and in order.
        TEST_ASSERT_GREATER_THAN_size_t(1, all.size());
        TEST_ASSERT_LESS_THAN_size_t(80, all.size());
        for (size_t i = 0; i < all.size(); ++i) {
            std::array<char, 16> want;
            std::snprintf(want.data(), want.size(), " pl wrap %03u", unsigned(80 - all.size() + i));
            TEST_ASSERT_TRUE(all[i].ends_with(want.data()));
        }
        for (size_t count : {size_t{0}, size_t{1}, size_t{2}, size_t{5}, all.size() - 1, all.size(),
                             all.size() + 1, size_t{1000}}) {
            TEST_ASSERT_TRUE(read_newest(count) == last_of(all, count));
        }
        TEST_ASSERT_TRUE(pl::try_stop().has_value());
    }
}

// =============================================================================
// Entry readers on a store that fails or changes while they read
// =============================================================================

namespace {

// An entry's stamp and text, copied out of the reader's buffer.
struct copied {
    bool stamped = false;
    std::string text;
};

// Reads every entry that `r` returns, until it returns false or an error. The error, if any, is
// put in `error`.
std::vector<copied>
copy_entries(pl::entry_reader& r, std::optional<std::error_code>* error = nullptr) {
    std::vector<copied> out;
    pl::entry e;
    while (true) {
        auto more = r.try_next(e);
        if (!more) {
            TEST_ASSERT_NOT_NULL(error);
            *error = more.error();
            return out;
        }
        if (!*more) {
            return out;
        }
        out.push_back({e.stamped(), std::string(e.text())});
    }
}

// The texts "<prefix> 000 <pad>", "<prefix> 001 <pad>" and so on, as append_padded() appends them.
std::vector<std::string> padded_texts(const char* prefix, int count) {
    std::vector<std::string> out;
    for (int i = 0; i < count; ++i) {
        std::array<char, 64> line;
        const int n = std::snprintf(line.data(), line.size(), "%s %03d ", prefix, i);
        out.push_back(std::string(line.data(), static_cast<size_t>(n)) + std::string(24, 'x'));
    }
    return out;
}

// Writes the texts of padded_texts(), and flushes after every ten.
void append_padded(const char* prefix, int count) {
    const auto texts = padded_texts(prefix, count);
    for (size_t i = 0; i < texts.size(); ++i) {
        pl::append(texts[i]);
        if (i % 10 == 9) {
            TEST_ASSERT_TRUE(pl::try_flush().has_value());
        }
    }
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
}

// Checks entries that a reader returned across text that it skipped. Each entry is a whole entry
// of `expected`, in order, or an entry cut short where text was skipped. An entry cut short is the
// start of the next expected entry, or isn't stamped when it was cut inside its stamp. No entry
// holds text joined from two entries. Returns the number of places where entries were skipped.
size_t check_in_order(const std::vector<copied>& got, const std::vector<std::string>& expected) {
    size_t at = 0;
    size_t gaps = 0;
    for (const auto& g : got) {
        size_t j = at;
        while (j < expected.size() && expected[j] != g.text) {
            ++j;
        }
        if (j == expected.size()) {
            TEST_ASSERT_TRUE(at < expected.size());
            TEST_ASSERT_TRUE(!g.stamped || expected[at].starts_with(g.text));
            ++gaps;
            ++at;
            continue;
        }
        TEST_ASSERT_TRUE(g.stamped);
        if (j > at) {
            ++gaps;
        }
        at = j + 1;
    }
    return gaps;
}

// The offset of `needle` in the store's bytes, from `from` on.
size_t find_in(pl::ram_sector_store& ram, std::string_view needle, size_t from = 0) {
    const std::string_view bytes(reinterpret_cast<const char*>(ram.bytes().data()), ram.size());
    const size_t at = bytes.find(needle, from);
    TEST_ASSERT_TRUE(at != std::string_view::npos);
    return at;
}

} // namespace

TEST_CASE("persistent_log readers keep failing once a read fails", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    const auto expected = padded_texts("pl err", 60);
    size_t failed = 0;
    for (int k = 0; k < 80; ++k) {
        auto ram = make_ram(8, 1024);
        auto reads = std::make_shared<int>(-1);
        TEST_ASSERT_TRUE(
            pl::try_start(store_config(std::make_unique<test_store>(ram, nullptr, nullptr, reads)))
                .has_value()
        );
        TEST_ASSERT_TRUE(pl::try_clear().has_value());
        append_padded("pl err", 60);
        const std::string all = read_all();

        // The `k`th read from now fails, partway through a refill or a chunk. Every entry before
        // the error is whole, and every later call returns the same error, although the store
        // reads again.
        auto r = pl::try_read();
        TEST_ASSERT_TRUE(r.has_value());
        *reads = k;
        std::optional<std::error_code> error;
        const auto got = copy_entries(*r, &error);
        TEST_ASSERT_LESS_OR_EQUAL_size_t(expected.size(), got.size());
        for (size_t i = 0; i < got.size(); ++i) {
            TEST_ASSERT_TRUE(got[i].stamped && got[i].text == expected[i]);
        }
        if (error) {
            ++failed;
            TEST_ASSERT_TRUE(*error == idfxx::errc::fail);
            pl::entry e;
            for (int again = 0; again < 3; ++again) {
                auto more = r->try_next(e);
                TEST_ASSERT_FALSE(more.has_value());
                TEST_ASSERT_TRUE(more.error() == *error);
            }
        } else {
            TEST_ASSERT_EQUAL_size_t(expected.size(), got.size());
        }

        // The same for a text reader: what it returned before the error is the start of the log.
        *reads = -1;
        auto t = pl::try_read_text();
        TEST_ASSERT_TRUE(t.has_value());
        *reads = k;
        std::string text;
        std::array<char, 100> chunk;
        while (true) {
            auto n = t->try_next(chunk);
            if (!n) {
                for (int again = 0; again < 3; ++again) {
                    auto more = t->try_next(chunk);
                    TEST_ASSERT_FALSE(more.has_value());
                    TEST_ASSERT_TRUE(more.error() == n.error());
                }
                break;
            }
            if (n->empty()) {
                break;
            }
            text.append(*n);
        }
        TEST_ASSERT_TRUE(std::string_view(all).starts_with(text));
        TEST_ASSERT_TRUE(pl::try_stop().has_value());
    }
    TEST_ASSERT_GREATER_THAN_size_t(20, failed);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("persistent_log entry ranges throw a read's error and keep it", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    const auto expected = padded_texts("pl rng", 60);
    size_t in_begin = 0;
    size_t in_increment = 0;
    for (int k = 0; k < 80; ++k) {
        auto ram = make_ram(8, 1024);
        auto reads = std::make_shared<int>(-1);
        TEST_ASSERT_TRUE(
            pl::try_start(store_config(std::make_unique<test_store>(ram, nullptr, nullptr, reads)))
                .has_value()
        );
        TEST_ASSERT_TRUE(pl::try_clear().has_value());
        append_padded("pl rng", 60);

        // The `k`th read from now fails. The loop throws the error from begin() or from an
        // increment. Every entry before the error is whole.
        auto r = pl::read();
        *reads = k;
        std::vector<copied> got;
        std::optional<std::error_code> error;
        bool from_begin = false;
        try {
            from_begin = true;
            for (const auto& e : r) {
                from_begin = false;
                got.push_back({e.stamped(), std::string(e.text())});
            }
        } catch (const std::system_error& ex) {
            error = ex.code();
        }
        TEST_ASSERT_LESS_OR_EQUAL_size_t(expected.size(), got.size());
        for (size_t i = 0; i < got.size(); ++i) {
            TEST_ASSERT_TRUE(got[i].stamped && got[i].text == expected[i]);
        }
        if (!error) {
            TEST_ASSERT_EQUAL_size_t(expected.size(), got.size());
            TEST_ASSERT_TRUE(pl::try_stop().has_value());
            continue;
        }
        TEST_ASSERT_TRUE(*error == idfxx::errc::fail);
        ++(from_begin ? in_begin : in_increment);
        TEST_ASSERT_EQUAL(from_begin, got.empty());
        // The reader stays failed: begin(), next() and try_next() return the same error, although
        // the store reads again.
        for (int again = 0; again < 2; ++again) {
            std::optional<std::error_code> thrown;
            try {
                (void)r.begin();
            } catch (const std::system_error& ex) {
                thrown = ex.code();
            }
            TEST_ASSERT_TRUE(thrown == error);
            thrown.reset();
            pl::entry e;
            try {
                (void)r.next(e);
            } catch (const std::system_error& ex) {
                thrown = ex.code();
            }
            TEST_ASSERT_TRUE(thrown == error);
            auto more = r.try_next(e);
            TEST_ASSERT_TRUE(!more.has_value() && more.error() == *error);
        }
        TEST_ASSERT_TRUE(pl::try_stop().has_value());
    }
    TEST_ASSERT_GREATER_THAN_size_t(0, in_begin);
    TEST_ASSERT_GREATER_THAN_size_t(0, in_increment);
}

TEST_CASE("persistent_log fault ranges throw a read's error and keep it", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    auto throws = [](auto&& call) -> std::optional<std::error_code> {
        try {
            call();
        } catch (const std::system_error& ex) {
            return ex.code();
        }
        return std::nullopt;
    };
    auto ram = make_ram(8, 1024);
    auto reads = std::make_shared<int>(-1);
    auto c = store_config(std::make_unique<test_store>(ram, nullptr, nullptr, reads));
    c.faults = pl::fault_config{.slot_size = 1024, .slot_count = 2};
    TEST_ASSERT_TRUE(pl::try_start(std::move(c)).has_value());
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    append_padded("pl rctx", 12);
    TEST_ASSERT_TRUE(pl::try_record_fault("pl_r1").has_value());
    TEST_ASSERT_TRUE(pl::try_record_fault("pl_r2").has_value());

    // An error in reading a context throws from the context's begin(), and lasts until the
    // reader moves on to the next fault. The increment then reads the next fault.
    {
        auto faults = pl::read_faults();
        auto it = faults.begin();
        TEST_ASSERT_FALSE(it == faults.end());
        TEST_ASSERT_TRUE((*it).kind() == "pl_r1");
        *reads = 0;
        const auto error = throws([&] { (void)faults.context().begin(); });
        TEST_ASSERT_TRUE(error == std::error_code(idfxx::errc::fail));
        TEST_ASSERT_TRUE(throws([&] { (void)faults.context().begin(); }) == error);
        ++it;
        TEST_ASSERT_FALSE(it == faults.end());
        TEST_ASSERT_TRUE((*it).kind() == "pl_r2");
        auto e = faults.context().begin();
        TEST_ASSERT_FALSE(e == std::default_sentinel);
        TEST_ASSERT_TRUE((*e).stamped());
    }

    // An error in reading a fault's header throws from begin() or from an increment, and lasts
    // for every later call.
    for (int at : {0, 1}) {
        auto faults = pl::read_faults();
        std::optional<std::error_code> error;
        if (at == 0) {
            *reads = 0;
            error = throws([&] { (void)faults.begin(); });
        } else {
            auto it = faults.begin();
            TEST_ASSERT_FALSE(it == faults.end());
            *reads = 0;
            error = throws([&] { ++it; });
        }
        TEST_ASSERT_TRUE(error == std::error_code(idfxx::errc::fail));
        TEST_ASSERT_TRUE(throws([&] { (void)faults.begin(); }) == error);
        TEST_ASSERT_TRUE(throws([&] { (void)faults.context().begin(); }) == error);
        pl::fault f;
        TEST_ASSERT_TRUE(throws([&] { (void)faults.next(f); }) == error);
    }
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}
#endif

TEST_CASE(
    "persistent_log fault reader keeps failing once a read fails, until the next fault",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    size_t failed = 0;
    for (int k = 0; k < 40; ++k) {
        auto ram = make_ram(8, 1024);
        auto reads = std::make_shared<int>(-1);
        auto c = store_config(std::make_unique<test_store>(ram, nullptr, nullptr, reads));
        c.faults = pl::fault_config{.slot_size = 1024, .slot_count = 2};
        TEST_ASSERT_TRUE(pl::try_start(std::move(c)).has_value());
        TEST_ASSERT_TRUE(pl::try_clear().has_value());
        TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
        append_padded("pl ctx", 12);
        TEST_ASSERT_TRUE(pl::try_record_fault("pl_k").has_value());
        TEST_ASSERT_TRUE(pl::try_record_fault("pl_k2").has_value());

        // The entries of the first fault's context, read without a failure.
        std::vector<copied> want;
        {
            auto r = pl::try_read_faults();
            TEST_ASSERT_TRUE(r.has_value());
            pl::fault f;
            TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
            pl::entry e;
            while (r->try_next_context(e).value_or(false)) {
                want.push_back({e.stamped(), std::string(e.text())});
            }
        }

        auto r = pl::try_read_faults();
        TEST_ASSERT_TRUE(r.has_value());
        pl::fault f;
        TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
        *reads = k;
        pl::entry e;
        std::vector<copied> got;
        std::optional<std::error_code> error;
        while (true) {
            auto more = r->try_next_context(e);
            if (!more) {
                error = more.error();
                break;
            }
            if (!*more) {
                break;
            }
            got.push_back({e.stamped(), std::string(e.text())});
        }
        TEST_ASSERT_LESS_OR_EQUAL_size_t(want.size(), got.size());
        for (size_t i = 0; i < got.size(); ++i) {
            TEST_ASSERT_TRUE(got[i].stamped == want[i].stamped && got[i].text == want[i].text);
        }
        if (!error) {
            TEST_ASSERT_EQUAL_size_t(want.size(), got.size());
            *reads = -1;
            TEST_ASSERT_TRUE(pl::try_stop().has_value());
            continue;
        }
        ++failed;
        // Both forms of the context read return the same error.
        std::array<char, 16> chunk;
        for (int again = 0; again < 2; ++again) {
            auto more = r->try_next_context(e);
            TEST_ASSERT_TRUE(!more.has_value() && more.error() == *error);
            auto n = r->try_next_context_text(chunk);
            TEST_ASSERT_TRUE(!n.has_value() && n.error() == *error);
        }
        // The next fault's header reads, and so does its context.
        TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
        TEST_ASSERT_TRUE(f.kind() == "pl_k2");
        TEST_ASSERT_TRUE(r->try_next_context(e).value_or(false));
        TEST_ASSERT_TRUE(pl::try_stop().has_value());
    }
    TEST_ASSERT_GREATER_THAN_size_t(5, failed);

    // An error in reading a fault's header lasts for every later call.
    auto ram = make_ram(8, 1024);
    auto reads = std::make_shared<int>(-1);
    auto c = store_config(std::make_unique<test_store>(ram, nullptr, nullptr, reads));
    c.faults = pl::fault_config{.slot_size = 1024, .slot_count = 2};
    TEST_ASSERT_TRUE(pl::try_start(std::move(c)).has_value());
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    TEST_ASSERT_TRUE(pl::try_record_fault("pl_k").has_value());
    auto r = pl::try_read_faults();
    TEST_ASSERT_TRUE(r.has_value());
    *reads = 0;
    pl::fault f;
    auto first = r->try_next(f);
    TEST_ASSERT_FALSE(first.has_value());
    pl::entry e;
    std::array<char, 16> chunk;
    for (int again = 0; again < 2; ++again) {
        auto next = r->try_next(f);
        TEST_ASSERT_TRUE(!next.has_value() && next.error() == first.error());
        auto more = r->try_next_context(e);
        TEST_ASSERT_TRUE(!more.has_value() && more.error() == first.error());
        auto n = r->try_next_context_text(chunk);
        TEST_ASSERT_TRUE(!n.has_value() && n.error() == first.error());
    }
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

TEST_CASE(
    "persistent_log ends a line that a failed flush left open, and goes on",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    auto ram = make_ram(8, 256);
    auto failing = std::make_shared<bool>(false);
    TEST_ASSERT_TRUE(
        pl::try_start(store_config(std::make_unique<test_store>(ram, nullptr, failing)))
            .has_value()
    );
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    // The entry is longer than a sector holds, so the flush writes its first part to the head
    // sector, and then fails to start the next sector. The log goes on without a restart.
    const std::string longest(CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE, 'y');
    pl::append(longest);
    *failing = true;
    TEST_ASSERT_FALSE(pl::try_flush().has_value());
    *failing = false;
    pl::append("pl after");
    TEST_ASSERT_TRUE(pl::try_flush().has_value());

    // The text shows the entry cut short, then a newline, then the next entry on its own line.
    const std::string all = read_all();
    TEST_ASSERT_TRUE(all.ends_with(" pl after\n"));
    const size_t after = all.rfind('\n', all.size() - 2);
    TEST_ASSERT_TRUE(after != std::string::npos && all[after - 1] == 'y');
    // The entries show the same.
    auto r = pl::try_read();
    TEST_ASSERT_TRUE(r.has_value());
    const auto got = copy_entries(*r);
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(got[0].stamped && got[1].stamped);
    TEST_ASSERT_TRUE(got[0].text.size() > 0 && got[0].text.size() < longest.size());
    TEST_ASSERT_TRUE(longest.starts_with(got[0].text));
    TEST_ASSERT_TRUE(got[1].text == "pl after");
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

TEST_CASE(
    "persistent_log entry reader cuts an entry and resumes across an overwritten sector",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    auto ram = make_ram(8, 1024);
    TEST_ASSERT_TRUE(pl::try_start(store_config(ram)).has_value());
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    const auto expected = padded_texts("pl rc", 100);
    append_padded("pl rc", 100);
    auto r = pl::try_read();
    TEST_ASSERT_TRUE(r.has_value());
    // The first call fills the reader's buffer. The entries written next recycle the oldest
    // sectors, including the one that the reader has read up to, but not the head sector that
    // the reader ends in.
    pl::entry e;
    TEST_ASSERT_TRUE(r->try_next(e).value_or(false));
    std::vector<copied> got{{e.stamped(), std::string(e.text())}};
    append_padded("pl new", 100);
    for (const auto& c : copy_entries(*r)) {
        got.push_back(c);
    }
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(1, check_in_order(got, expected));
    TEST_ASSERT_TRUE(got.back().text == expected.back());
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

TEST_CASE(
    "persistent_log entry reader skips a bad record and resumes at the next entry",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    for (bool partly_read : {false, true}) {
        auto ram = make_ram(8, 1024);
        TEST_ASSERT_TRUE(pl::try_start(store_config(ram)).has_value());
        TEST_ASSERT_TRUE(pl::try_clear().has_value());
        const auto expected = padded_texts("pl bd", 100);
        append_padded("pl bd", 100);
        const std::string all = read_all();
        auto r = pl::try_read();
        TEST_ASSERT_TRUE(r.has_value());
        pl::entry e;
        TEST_ASSERT_TRUE(r->try_next(e).value_or(false));
        std::vector<copied> got{{e.stamped(), std::string(e.text())}};
        // Damage a byte of an entry that the reader hasn't read yet: the entry that the reader's
        // buffer ends in, whose record the reader has partly read, or an entry beyond it. Each
        // is sectors before the head sector, because a bad record ends its sector's records, and
        // the reader resumes in the next sector.
        size_t damaged = 62;
        if (partly_read) {
            damaged = 0;
            // The first entry whose text ends after the bytes that the buffer holds.
            const auto end_of = [&](size_t i) {
                return all.find(expected[i]) + expected[i].size();
            };
            while (end_of(damaged) <= pl::entry_buffer_size) {
                ++damaged;
            }
        }
        const size_t at = find_in(*ram, expected[damaged]);
        ram->bytes()[at + expected[damaged].size() - 1] ^= std::byte{0x01};
        for (const auto& c : copy_entries(*r)) {
            got.push_back(c);
        }
        TEST_ASSERT_GREATER_OR_EQUAL_size_t(1, check_in_order(got, expected));
        TEST_ASSERT_TRUE(std::none_of(got.begin(), got.end(), [&](const copied& c) {
            return c.text == expected[damaged];
        }));
        TEST_ASSERT_TRUE(got.back().text == expected.back());
        TEST_ASSERT_TRUE(pl::try_stop().has_value());
    }
}

TEST_CASE("persistent_log an entry stays valid when its reader is moved", "[persistent_log]") {
    idfxx::test::run_after_each_test(stop_log);
    auto ram = make_ram(8, 1024);
    TEST_ASSERT_TRUE(pl::try_start(store_config(ram)).has_value());
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    pl::append("pl move one");
    pl::append("pl move two");
    auto r = pl::try_read();
    TEST_ASSERT_TRUE(r.has_value());
    pl::entry e;
    TEST_ASSERT_TRUE(r->try_next(e).value_or(false));
    TEST_ASSERT_TRUE(e.text() == "pl move one");
    // Moving the reader by construction, and then by assignment, keeps its buffer, so the entry
    // still refers to the same text.
    pl::entry_reader moved = std::move(*r);
    TEST_ASSERT_TRUE(e.text() == "pl move one");
    auto other = pl::try_read();
    TEST_ASSERT_TRUE(other.has_value());
    *other = std::move(moved);
    TEST_ASSERT_TRUE(e.text() == "pl move one");
    pl::entry next;
    TEST_ASSERT_TRUE(other->try_next(next).value_or(false));
    TEST_ASSERT_TRUE(next.text() == "pl move two");
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}

TEST_CASE(
    "persistent_log reads a damaged fault context as entries, as it reads its text",
    "[persistent_log]"
) {
    idfxx::test::run_after_each_test(stop_log);
    auto ram = make_ram(8, 1024);
    auto c = store_config(ram);
    c.faults = pl::fault_config{.slot_size = 1024, .slot_count = 1};
    TEST_ASSERT_TRUE(pl::try_start(std::move(c)).has_value());
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    append_padded("pl dmg", 12);
    TEST_ASSERT_TRUE(pl::try_record_fault("pl_dmg").has_value());
    // Overwrite part of the context in the fault's slot with bytes of every kind: NUL, newlines,
    // tabs, bytes above 0x7F and a line that starts like a stamp.
    using namespace std::string_view_literals;
    constexpr std::string_view noise = "\0\n\n\t\x80\xFF\n12.3 +\x01\n\t\t\r\n\0\0"sv;
    const size_t slot = ram->size() - 1024;
    const size_t at = find_in(*ram, "pl dmg 010", slot);
    std::copy(
        reinterpret_cast<const std::byte*>(noise.data()),
        reinterpret_cast<const std::byte*>(noise.data()) + noise.size(),
        ram->bytes().begin() + static_cast<ptrdiff_t>(at)
    );

    std::string raw;
    {
        auto r = pl::try_read_faults();
        TEST_ASSERT_TRUE(r.has_value());
        pl::fault f;
        TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
        TEST_ASSERT_TRUE(f.damaged());
        std::array<char, 64> chunk;
        while (true) {
            const std::string_view n = r->try_next_context_text(chunk).value_or(std::string_view());
            if (n.empty()) {
                break;
            }
            raw.append(n);
        }
    }
    std::string joined;
    {
        auto r = pl::try_read_faults();
        TEST_ASSERT_TRUE(r.has_value());
        pl::fault f;
        TEST_ASSERT_TRUE(r->try_next(f).value_or(false));
        pl::entry e;
        while (r->try_next_context(e).value_or(false)) {
            joined += pl::detail::entry_access::stored(e);
            joined += '\n';
        }
    }
    // The text without its empty lines, ending with a newline, is the entries joined.
    std::string want;
    bool at_line = true;
    for (char ch : raw) {
        if (at_line && ch == '\n') {
            continue;
        }
        want += ch;
        at_line = ch == '\n';
    }
    if (!want.empty() && want.back() != '\n') {
        want += '\n';
    }
    TEST_ASSERT_TRUE(raw.find(noise.substr(4, 2)) != std::string::npos);
    TEST_ASSERT_TRUE(joined == want);
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
}
