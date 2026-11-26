// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Tests for idfxx_persistent_log's service: the reset that start finds, and the core dump that
// tells more about the reset. The tests supply both through the service's reset source. They run
// on the test app's "log" partition, with capture off.

#include "sdkconfig.h"

#include "../src/reset.hpp"
#include "../src/rtc.hpp"
#include "../src/sector_store.hpp"
#include "idfxx/persistent_log"
#include "test_cleanup.hpp"
#include "unity.h"

#include <idfxx/partition>

#include <array>
#include <cstdio>
#include <cstring>
#include <esp_app_desc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pl = idfxx::persistent_log;
namespace internal = idfxx::persistent_log::internal;
using idfxx::reset_reason;

namespace {

// =============================================================================
// A reset and a core dump of the tests' own
// =============================================================================

struct fake_dump {
    reset_reason reason = reset_reason::power_on;
    esp_err_t check = ESP_ERR_NOT_FOUND;
    uint32_t crc = 0;
    bool crc_fails = false;
    bool has_summary = false;
    internal::dump_summary summary;
    std::string panic_reason;
    // How often the image's CRC was asked for.
    int crc_reads = 0;
};

fake_dump s_fake;

reset_reason fake_reason() {
    return s_fake.reason;
}

esp_err_t fake_check() {
    return s_fake.check;
}

idfxx::result<uint32_t> fake_crc() {
    ++s_fake.crc_reads;
    if (s_fake.crc_fails) {
        return idfxx::error(idfxx::errc::fail);
    }
    return s_fake.crc;
}

bool fake_summary(internal::dump_summary& out) {
    if (!s_fake.has_summary) {
        return false;
    }
    out = s_fake.summary;
    return true;
}

size_t fake_panic_reason(std::span<char> out) {
    const size_t n = std::min(s_fake.panic_reason.size(), out.size() - 1);
    std::memcpy(out.data(), s_fake.panic_reason.data(), n);
    out[n] = '\0';
    return n;
}

// Restores ESP-IDF's reset source. The reset stays marked as taken for the rest of the boot, so
// that later starts add nothing for it.
void restore_source() {
    (void)pl::try_stop();
    internal::reset_hooks() = internal::esp_reset_source();
}

void use_fake() {
    idfxx::test::run_after_each_test(restore_source);
    s_fake = {};
    internal::reset_hooks() = {
        .reason = fake_reason,
        .image_check = fake_check,
        .image_crc = fake_crc,
        .summary = fake_summary,
        .panic_reason = fake_panic_reason,
    };
}

// Returns the running app's ELF SHA-256 in hex, as a dump of the app holds it.
std::array<char, 65> app_sha() {
    std::array<char, 65> sha{};
    esp_app_get_elf_sha256(sha.data(), sha.size());
    return sha;
}

// Sets up a complete dump of this build on Xtensa, for a crash in task "pl_task".
void xtensa_dump(uint32_t crc) {
    s_fake.check = ESP_OK;
    s_fake.crc = crc;
    s_fake.has_summary = true;
    auto& s = s_fake.summary;
    s = {};
    s.kind = internal::dump_summary::arch::xtensa;
    std::memcpy(s.task.data(), "pl_task", 8);
    s.pc = 0x42001234;
    for (uint32_t i = 0; i < s.backtrace.size(); ++i) {
        s.backtrace[i] = 0x42000000 + i;
    }
    s.depth = 3;
    s.exc_cause = 28;
    s.exc_vaddr = 0x1234;
    s.elf_sha256 = app_sha();
}

// A store that passes each call on to a RAM store that the test keeps, so that the RAM store
// outlives a stop.
class shared_store final : public pl::sector_store {
public:
    explicit shared_store(std::shared_ptr<pl::ram_sector_store> ram)
        : sector_store(ram->size(), ram->sector_size(), ram->write_size())
        , _ram(std::move(ram)) {}

private:
    idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
        return _ram->try_read(offset, dst);
    }
    idfxx::result<void> do_read_raw(size_t offset, idfxx::byte_span dst) const override {
        return _ram->try_read_raw(offset, dst);
    }
    idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
        return _ram->try_write(offset, src);
    }
    idfxx::result<void> do_erase_sector(size_t index) override {
        return _ram->try_erase_sector(index);
    }

    std::shared_ptr<pl::ram_sector_store> _ram;
};

pl::config reset_config() {
    pl::config c;
    c.boot_count = 3;
    c.wall_clock = [] { return std::optional<std::chrono::system_clock::time_point>{}; };
    c.writer = std::nullopt;
#ifdef CONFIG_IDFXX_STD_FORMAT
    c.capture = std::nullopt;
#endif
    return c;
}

// Starts once with a reset that adds nothing, then empties the log and the faults. That includes
// the suppressed faults and both notes of which dump was taken: the one in RTC memory, and the one
// in the fault slots, which a clear of the faults keeps. So the partition is erased.
void start_clean() {
    (void)pl::try_stop();
    use_fake();
    s_fake.reason = reset_reason::deep_sleep;
    internal::rearm_reset();
    TEST_ASSERT_TRUE(pl::try_start(reset_config()).has_value());
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    auto part = idfxx::partition::try_find("log");
    TEST_ASSERT_TRUE(part.has_value());
    TEST_ASSERT_TRUE(part->try_erase_range(0, part->size()).has_value());
    internal::rtc().faults.has_core_dump_crc = 0;
    internal::rtc().faults.core_dump_crc = 0;
    internal::seal(internal::rtc().faults);
}

// Starts as if after a reset of `reason`, with the dump that s_fake holds.
void start_after(reset_reason reason, pl::config c = reset_config()) {
    (void)pl::try_stop();
    s_fake.reason = reason;
    internal::rearm_reset();
    TEST_ASSERT_TRUE_MESSAGE(pl::try_start(std::move(c)).has_value(), "try_start failed");
}

// Loses what RTC memory holds for the faults, as a power cycle does: the note of which dump was
// taken, and the suppressed kinds. Then starts as if after the power-on reset.
void power_cycle(pl::config c = reset_config()) {
    (void)pl::try_stop();
    internal::rtc().faults = {};
    internal::seal(internal::rtc().faults);
    start_after(reset_reason::power_on, std::move(c));
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

size_t occurrences(std::string_view hay, std::string_view needle) {
    size_t n = 0;
    for (size_t at = 0; (at = hay.find(needle, at)) != std::string_view::npos; ++at) {
        ++n;
    }
    return n;
}

std::vector<pl::fault> faults() {
    std::vector<pl::fault> out;
    auto r = pl::try_read_faults();
    TEST_ASSERT_TRUE(r.has_value());
    pl::fault f;
    while (r->try_next(f).value_or(false)) {
        out.push_back(f);
    }
    return out;
}

// Returns the only fault recorded, after checking that it is of `kind`.
pl::fault only_fault(std::string_view kind) {
    auto got = faults();
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    TEST_ASSERT_TRUE(got[0].kind() == kind);
    return got[0];
}

} // namespace

// =============================================================================
// Kinds of reset
// =============================================================================

TEST_CASE("persistent_log start records a fault reset as a fault of its kind", "[persistent_log]") {
    start_clean();
    start_after(reset_reason::task_watchdog);
    const pl::fault f = only_fault("task_watchdog");
    // Without a dump, nothing more is known. The task is left empty too, because the task that
    // started the log isn't the one that faulted.
    TEST_ASSERT_TRUE(f.detail().empty());
    TEST_ASSERT_TRUE(f.task().empty());
    TEST_ASSERT_EQUAL_HEX32(0, f.exception_pc());
    TEST_ASSERT_EQUAL_size_t(0, f.frames().size());
    // Start fills in what it knows now: this boot and wake, and the time since boot when start
    // found the reset.
    TEST_ASSERT_EQUAL_UINT32(3, f.boot());
    TEST_ASSERT_EQUAL_UINT32(internal::rtc().wake.wake, f.wake());
    TEST_ASSERT_FALSE(f.wall_time().has_value());
    const std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " E persistent_log: fault kind=task_watchdog\n"));
    TEST_ASSERT_EQUAL_size_t(0, occurrences(all, "reset reason="));
    TEST_ASSERT_EQUAL_INT(0, s_fake.crc_reads);
}

TEST_CASE("persistent_log start names each kind of fault reset", "[persistent_log]") {
    start_clean();
    for (auto [reason, name] : std::array<std::pair<reset_reason, std::string_view>, 7>{{
             {reset_reason::panic, "panic"},
             {reset_reason::interrupt_watchdog, "interrupt_watchdog"},
             {reset_reason::task_watchdog, "task_watchdog"},
             {reset_reason::watchdog, "watchdog"},
             {reset_reason::brownout, "brownout"},
             {reset_reason::power_glitch, "power_glitch"},
             {reset_reason::cpu_lockup, "cpu_lockup"},
         }}) {
        start_after(reason);
        TEST_ASSERT_TRUE(faults().back().kind() == name);
    }
    // The slots hold the newest faults.
    TEST_ASSERT_EQUAL_size_t(pl::fault_config{}.slot_count, faults().size());
}

TEST_CASE("persistent_log start adds nothing after deep sleep", "[persistent_log]") {
    start_clean();
    start_after(reset_reason::deep_sleep);
    TEST_ASSERT_EQUAL_size_t(0, faults().size());
    TEST_ASSERT_TRUE(read_all().empty());
}

TEST_CASE(
    "persistent_log start counts the wakes that the reset source reports",
    "[persistent_log]"
) {
    start_clean();
    // After a reset of another kind, the wake count is 0.
    start_after(reset_reason::software);
    TEST_ASSERT_EQUAL_UINT32(0, internal::rtc().wake.wake);
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    pl::append("pl after the reset");
    TEST_ASSERT_TRUE(read_all().starts_with("3.0 "));
    // Each wake from deep sleep adds one to the count. The entries are stamped with the count, and
    // start adds no entry for the reset.
    TEST_ASSERT_TRUE(pl::try_clear().has_value());
    start_after(reset_reason::deep_sleep);
    start_after(reset_reason::deep_sleep);
    TEST_ASSERT_EQUAL_UINT32(2, internal::rtc().wake.wake);
    pl::append("pl after two wakes");
    const std::string all = read_all();
    TEST_ASSERT_TRUE(all.starts_with("3.2 "));
    TEST_ASSERT_TRUE(all.ends_with(" pl after two wakes\n"));
    TEST_ASSERT_EQUAL_size_t(0, occurrences(all, "reset reason="));
    TEST_ASSERT_EQUAL_size_t(0, faults().size());
    // Leave the wake count as a boot other than a wake leaves it.
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    internal::check_rtc(false);
}

TEST_CASE("persistent_log start logs the name of any other reset", "[persistent_log]") {
    start_clean();
    start_after(reset_reason::software);
    TEST_ASSERT_EQUAL_size_t(
        1, occurrences(read_all(), " I persistent_log: reset reason=software\n")
    );
    start_after(reset_reason::power_on);
    start_after(reset_reason::unknown);
    start_after(static_cast<reset_reason>(42));
    const std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " I persistent_log: reset reason=power_on\n"));
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " I persistent_log: reset reason=unknown\n"));
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " I persistent_log: reset reason=unknown_42\n"));
    TEST_ASSERT_EQUAL_size_t(0, faults().size());
}

TEST_CASE("persistent_log start takes the reset once a boot", "[persistent_log]") {
    start_clean();
    start_after(reset_reason::software);
    // When the log is stopped and started again without a reset, start logs nothing more.
    TEST_ASSERT_TRUE(pl::try_stop().has_value());
    TEST_ASSERT_TRUE(pl::try_start(reset_config()).has_value());
    TEST_ASSERT_EQUAL_size_t(1, occurrences(read_all(), "reset reason=software"));
}

TEST_CASE(
    "persistent_log start records a fault reset once until another reset",
    "[persistent_log]"
) {
    start_clean();
    start_after(reset_reason::brownout);
    start_after(reset_reason::brownout);
    start_after(reset_reason::panic);
    start_after(reset_reason::brownout);
    TEST_ASSERT_EQUAL_size_t(2, faults().size());
    std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(
        2, occurrences(all, " W persistent_log: fault repeat kind=brownout\n")
    );
    // A reset that isn't a fault ends the repeats, so the next fault of each kind is recorded
    // again.
    start_after(reset_reason::software);
    start_after(reset_reason::brownout);
    start_after(reset_reason::panic);
    TEST_ASSERT_EQUAL_size_t(4, faults().size());
    // Deep sleep doesn't end the repeats.
    start_after(reset_reason::deep_sleep);
    start_after(reset_reason::panic);
    all = read_all();
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " W persistent_log: fault repeat kind=panic\n"));
    TEST_ASSERT_EQUAL_size_t(2, occurrences(all, " E persistent_log: fault kind=panic\n"));
}

TEST_CASE("persistent_log start without fault slots only logs the reset", "[persistent_log]") {
    start_clean();
    xtensa_dump(7);
    auto c = reset_config();
    c.faults = std::nullopt;
    start_after(reset_reason::panic, std::move(c));
    c = reset_config();
    c.faults = std::nullopt;
    start_after(reset_reason::software, std::move(c));
    const std::string all = read_all();
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " I persistent_log: reset reason=panic\n"));
    TEST_ASSERT_EQUAL_size_t(1, occurrences(all, " I persistent_log: reset reason=software\n"));
    TEST_ASSERT_EQUAL_size_t(0, occurrences(all, "fault kind="));
    TEST_ASSERT_TRUE(pl::try_fault_count().error() == idfxx::errc::not_supported);
    // The dump is left as it was.
    TEST_ASSERT_EQUAL_INT(0, s_fake.crc_reads);
}

TEST_CASE(
    "persistent_log start carries on when recording the reset's fault fails",
    "[persistent_log]"
) {
    use_fake();
    auto store = std::make_unique<pl::ram_sector_store>(8, 1024);
    auto* raw = store.get();
    // Every write and erase fails, starting with the first. The store still opens, because opening
    // only reads, and then the record fails.
    raw->cut_after(0);
    auto c = reset_config();
    c.storage = std::move(store);
    c.faults = pl::fault_config{.slot_size = 1024, .slot_count = 2};
    start_after(reset_reason::panic, std::move(c));
    raw->restore_power();
    TEST_ASSERT_EQUAL_size_t(0, faults().size());
}

// =============================================================================
// The core dump
// =============================================================================

TEST_CASE("persistent_log start takes a dump's task, PC, backtrace and cause", "[persistent_log]") {
    start_clean();
    xtensa_dump(111);
    s_fake.summary.depth = 20; // more frames than are kept
    start_after(reset_reason::panic);
    const pl::fault f = only_fault("panic");
    TEST_ASSERT_TRUE(f.detail() == "exccause=28 excvaddr=0x00001234 pc=0x42001234");
    TEST_ASSERT_TRUE(f.task() == "pl_task");
    TEST_ASSERT_EQUAL_HEX32(0x42001234, f.exception_pc());
    TEST_ASSERT_EQUAL_size_t(16, f.frames().size());
    TEST_ASSERT_EQUAL_HEX32(0x42000000, f.frames()[0]);
    TEST_ASSERT_EQUAL_HEX32(0x4200000f, f.frames()[15]);
    // The dump is noted as taken.
    TEST_ASSERT_TRUE(internal::valid(internal::rtc().faults));
    TEST_ASSERT_EQUAL_UINT32(1, internal::rtc().faults.has_core_dump_crc);
    TEST_ASSERT_EQUAL_UINT32(111, internal::rtc().faults.core_dump_crc);
    TEST_ASSERT_EQUAL_size_t(
        1,
        occurrences(
            read_all(),
            " E persistent_log: fault kind=panic exccause=28 excvaddr=0x00001234 pc=0x42001234\n"
        )
    );
}

namespace {

// For measuring the stack that start uses after a reset of `s_stack_reason`, on a task of its own.
reset_reason s_stack_reason = reset_reason::deep_sleep;
size_t s_stack_free = 0;
TaskHandle_t s_stack_waiter = nullptr;

void start_on_task(void*) {
    s_fake.reason = s_stack_reason;
    internal::rearm_reset();
    TEST_ASSERT_TRUE(pl::try_start(reset_config()).has_value());
    s_stack_free = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
    xTaskNotifyGive(s_stack_waiter);
    vTaskSuspend(nullptr);
}

size_t start_stack(reset_reason reason) {
    (void)pl::try_stop();
    constexpr uint32_t stack = 8192;
    s_stack_reason = reason;
    s_stack_waiter = xTaskGetCurrentTaskHandle();
    TaskHandle_t task = nullptr;
    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(start_on_task, "pl_start", stack, nullptr, uxTaskPriorityGet(nullptr), &task)
    );
    TEST_ASSERT_EQUAL(1, ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000)));
    vTaskDelete(task);
    return stack - s_stack_free;
}

} // namespace

TEST_CASE(
    "persistent_log start records a reset's fault on a stack of its own",
    "[persistent_log]"
) {
    start_clean();
    const size_t plain = start_stack(reset_reason::deep_sleep);
    xtensa_dump(160);
    const size_t recording = start_stack(reset_reason::panic);
    std::printf(
        "start takes %u bytes of stack, %u when it records a panic\n",
        unsigned(plain),
        unsigned(recording)
    );
    TEST_ASSERT_LESS_OR_EQUAL_size_t(plain + 512, recording);
    // The fault is recorded as usual.
    const pl::fault f = only_fault("panic");
    TEST_ASSERT_TRUE(f.detail() == "exccause=28 excvaddr=0x00001234 pc=0x42001234");
    TEST_ASSERT_TRUE(f.task() == "pl_task");
    TEST_ASSERT_EQUAL_UINT32(160, internal::rtc().faults.core_dump_crc);
}

TEST_CASE(
    "persistent_log start leaves a dump to the next boot when its fault isn't recorded",
    "[persistent_log]"
) {
    start_clean();
    xtensa_dump(130);
    auto ram = std::make_shared<pl::ram_sector_store>(8, 1024);
    const auto config_on = [&ram] {
        auto c = reset_config();
        c.storage = std::make_unique<shared_store>(ram);
        c.faults = pl::fault_config{.slot_size = 1024, .slot_count = 2};
        return c;
    };
    // The record fails, so the dump isn't noted as taken.
    ram->cut_after(0);
    start_after(reset_reason::panic, config_on());
    ram->restore_power();
    TEST_ASSERT_EQUAL_size_t(0, faults().size());
    TEST_ASSERT_TRUE(internal::valid(internal::rtc().faults));
    TEST_ASSERT_EQUAL_UINT32(0, internal::rtc().faults.has_core_dump_crc);
    // The fault's entry is followed by an entry that says that the fault is not recorded.
    const std::string all = read_all();
    const size_t fault = all.find(
        " E persistent_log: fault kind=panic exccause=28 excvaddr=0x00001234 pc=0x42001234\n"
    );
    const size_t lost = all.find(" W persistent_log: fault not recorded kind=panic\n");
    TEST_ASSERT_TRUE(fault != std::string::npos && lost != std::string::npos);
    TEST_ASSERT_TRUE(fault < lost);
    // At the next boot, with the same reset and dump, the fault is recorded with what the dump
    // tells.
    start_after(reset_reason::panic, config_on());
    const pl::fault f = only_fault("panic");
    TEST_ASSERT_TRUE(f.detail() == "exccause=28 excvaddr=0x00001234 pc=0x42001234");
    TEST_ASSERT_TRUE(f.task() == "pl_task");
    TEST_ASSERT_EQUAL_UINT32(1, internal::rtc().faults.has_core_dump_crc);
    TEST_ASSERT_EQUAL_UINT32(130, internal::rtc().faults.core_dump_crc);
    // A repeat records nothing, but start still takes the dump.
    s_fake.crc = 131;
    start_after(reset_reason::panic, config_on());
    TEST_ASSERT_EQUAL_size_t(1, faults().size());
    TEST_ASSERT_EQUAL_UINT32(131, internal::rtc().faults.core_dump_crc);
    // After a power cycle, with the fault slots noting dump 130, the record of a new dump fails.
    // Neither note takes the new dump, so the next boot takes it.
    power_cycle(config_on());
    // With the log's lines in flash, start has only the fault to write.
    TEST_ASSERT_TRUE(pl::try_flush().has_value());
    s_fake.crc = 132;
    ram->cut_after(0);
    start_after(reset_reason::panic, config_on());
    ram->restore_power();
    TEST_ASSERT_EQUAL_size_t(1, faults().size());
    TEST_ASSERT_EQUAL_UINT32(0, internal::rtc().faults.has_core_dump_crc);
    start_after(reset_reason::panic, config_on());
    auto got = faults();
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(got.back().task() == "pl_task");
    TEST_ASSERT_EQUAL_UINT32(132, internal::rtc().faults.core_dump_crc);
}

TEST_CASE(
    "persistent_log start takes a panic at another PC as a repeat of the kind", "[persistent_log]"
) {
    start_clean();
    xtensa_dump(140);
    start_after(reset_reason::panic);
    // The same cause and address at another PC is the same kind, so it is a repeat. Start logs the
    // repeat with its own detail, and takes the dump.
    s_fake.crc = 141;
    s_fake.summary.pc = 0x42009999;
    start_after(reset_reason::panic);
    auto got = faults();
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    TEST_ASSERT_TRUE(got[0].detail() == "exccause=28 excvaddr=0x00001234 pc=0x42001234");
    TEST_ASSERT_EQUAL_UINT32(141, internal::rtc().faults.core_dump_crc);
    TEST_ASSERT_EQUAL_size_t(
        1,
        occurrences(
            read_all(),
            " W persistent_log: fault repeat kind=panic exccause=28 excvaddr=0x00001234 "
            "pc=0x42009999\n"
        )
    );
    // After a reset that isn't a fault, the next panic is recorded, with what its dump tells.
    start_after(reset_reason::software);
    s_fake.crc = 142;
    start_after(reset_reason::panic);
    got = faults();
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(got[1].detail() == "exccause=28 excvaddr=0x00001234 pc=0x42009999");
}

TEST_CASE("persistent_log start leaves a watchdog's PC out of its detail", "[persistent_log]") {
    start_clean();
    xtensa_dump(150);
    start_after(reset_reason::interrupt_watchdog);
    TEST_ASSERT_TRUE(
        only_fault("interrupt_watchdog").detail() == "exccause=28 excvaddr=0x00001234"
    );
    start_clean();
    xtensa_dump(151);
    s_fake.summary.kind = internal::dump_summary::arch::riscv;
    s_fake.summary.mcause = 7;
    s_fake.summary.mtval = 0x20;
    start_after(reset_reason::task_watchdog);
    TEST_ASSERT_TRUE(only_fault("task_watchdog").detail() == "mcause=0x00000007 mtval=0x00000020");
}

TEST_CASE("persistent_log start takes a dump's panic reason as the detail", "[persistent_log]") {
    start_clean();
    xtensa_dump(112);
    s_fake.panic_reason = "abort() was called at PC 0x42001234 on core 0";
    start_after(reset_reason::task_watchdog);
    const pl::fault f = only_fault("task_watchdog");
    TEST_ASSERT_TRUE(f.detail() == "abort() was called at PC 0x42001234 on core 0");
    TEST_ASSERT_EQUAL_size_t(3, f.frames().size());
    // A long panic reason is shortened to fit.
    start_clean();
    xtensa_dump(113);
    s_fake.panic_reason = std::string(300, 'r');
    start_after(reset_reason::interrupt_watchdog);
    TEST_ASSERT_TRUE(only_fault("interrupt_watchdog").detail() == std::string(159, 'r'));
    // The panic reason is shortened before the log sees it. When the cut falls in the middle of a
    // character, that character is dropped.
    start_clean();
    xtensa_dump(115);
    s_fake.panic_reason = std::string(158, 'r') + "\xE2\x82\xAC";
    start_after(reset_reason::interrupt_watchdog);
    TEST_ASSERT_TRUE(only_fault("interrupt_watchdog").detail() == std::string(158, 'r'));
}

TEST_CASE(
    "persistent_log start takes a RISC-V dump's cause and return address",
    "[persistent_log]"
) {
    start_clean();
    xtensa_dump(114);
    auto& s = s_fake.summary;
    s.kind = internal::dump_summary::arch::riscv;
    s.depth = 0;
    s.pc = 0x42005678;
    s.ra = 0x42009abc;
    s.mcause = 5;
    s.mtval = 0x10;
    start_after(reset_reason::panic);
    const pl::fault f = only_fault("panic");
    TEST_ASSERT_TRUE(f.detail() == "mcause=0x00000005 mtval=0x00000010 pc=0x42005678");
    TEST_ASSERT_EQUAL_HEX32(0x42005678, f.exception_pc());
    TEST_ASSERT_EQUAL_size_t(2, f.frames().size());
    TEST_ASSERT_EQUAL_HEX32(0x42005678, f.frames()[0]);
    TEST_ASSERT_EQUAL_HEX32(0x42009abc, f.frames()[1]);
}

TEST_CASE(
    "persistent_log start leaves a dump of another reset, or a broken one",
    "[persistent_log]"
) {
    // The dump can't be for this kind of reset, so the image isn't even read.
    start_clean();
    xtensa_dump(115);
    start_after(reset_reason::watchdog);
    TEST_ASSERT_TRUE(only_fault("watchdog").detail().empty());
    TEST_ASSERT_EQUAL_INT(0, s_fake.crc_reads);
    // When the image isn't complete, or there is none, the image isn't read either.
    start_clean();
    xtensa_dump(116);
    s_fake.check = ESP_ERR_INVALID_SIZE;
    start_after(reset_reason::panic);
    TEST_ASSERT_TRUE(only_fault("panic").task().empty());
    TEST_ASSERT_EQUAL_INT(0, s_fake.crc_reads);
    // An image that can't be read.
    start_clean();
    xtensa_dump(117);
    s_fake.crc_fails = true;
    start_after(reset_reason::panic);
    TEST_ASSERT_TRUE(only_fault("panic").task().empty());
    TEST_ASSERT_EQUAL_UINT32(0, internal::rtc().faults.has_core_dump_crc);
}

TEST_CASE("persistent_log start leaves a dump of another build", "[persistent_log]") {
    start_clean();
    xtensa_dump(118);
    s_fake.summary.elf_sha256[0] = s_fake.summary.elf_sha256[0] == '0' ? '1' : '0';
    start_after(reset_reason::panic);
    // The fault takes nothing from the dump, and nothing from the task that started the log.
    const pl::fault f = only_fault("panic");
    TEST_ASSERT_TRUE(f.detail().empty());
    TEST_ASSERT_TRUE(f.task().empty());
    TEST_ASSERT_EQUAL_HEX32(0, f.exception_pc());
    TEST_ASSERT_EQUAL_size_t(0, f.frames().size());
    TEST_ASSERT_EQUAL_UINT32(0, internal::rtc().faults.has_core_dump_crc);
    // The same holds for a dump that names no build.
    start_clean();
    xtensa_dump(119);
    s_fake.summary.elf_sha256 = {};
    start_after(reset_reason::panic);
    TEST_ASSERT_TRUE(only_fault("panic").detail().empty());
    // The two SHA-256s are compared only as far as the shorter one goes.
    start_clean();
    xtensa_dump(120);
    s_fake.summary.elf_sha256[8] = '\0';
    start_after(reset_reason::panic);
    TEST_ASSERT_FALSE(only_fault("panic").detail().empty());
}

TEST_CASE(
    "persistent_log start uses each dump once, told apart by its whole image",
    "[persistent_log]"
) {
    start_clean();
    xtensa_dump(121);
    start_after(reset_reason::panic);
    TEST_ASSERT_FALSE(only_fault("panic").detail().empty());
    // After another panic with the same image, the dump was already taken, so the fault has no
    // detail.
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    start_after(reset_reason::panic);
    TEST_ASSERT_TRUE(only_fault("panic").detail().empty());
    // The same summary in another image is a new dump.
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    s_fake.crc = 122;
    start_after(reset_reason::panic);
    TEST_ASSERT_TRUE(
        only_fault("panic").detail() == "exccause=28 excvaddr=0x00001234 pc=0x42001234"
    );
    TEST_ASSERT_EQUAL_UINT32(122, internal::rtc().faults.core_dump_crc);
}

namespace {

// Checks that a fault took nothing from a dump.
void assert_no_dump(const pl::fault& f) {
    TEST_ASSERT_TRUE(f.detail().empty());
    TEST_ASSERT_TRUE(f.task().empty());
    TEST_ASSERT_EQUAL_HEX32(0, f.exception_pc());
    TEST_ASSERT_EQUAL_size_t(0, f.frames().size());
}

} // namespace

TEST_CASE(
    "persistent_log start doesn't use a dump again after a power cycle",
    "[persistent_log]"
) {
    start_clean();
    xtensa_dump(170);
    start_after(reset_reason::panic);
    TEST_ASSERT_TRUE(only_fault("panic").task() == "pl_task");
    // RTC memory no longer notes the dump, but the fault slots do. A panic that writes no new dump
    // leaves the same image in flash, and the fault takes nothing from it.
    power_cycle();
    TEST_ASSERT_EQUAL_UINT32(0, internal::rtc().faults.has_core_dump_crc);
    start_after(reset_reason::panic);
    auto got = faults();
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(got.back().kind() == "panic");
    assert_no_dump(got.back());
    TEST_ASSERT_EQUAL_UINT32(0, internal::rtc().faults.has_core_dump_crc);
}

TEST_CASE(
    "persistent_log start doesn't use a dump again after a clear and a power cycle",
    "[persistent_log]"
) {
    start_clean();
    xtensa_dump(171);
    start_after(reset_reason::panic);
    TEST_ASSERT_TRUE(only_fault("panic").task() == "pl_task");
    TEST_ASSERT_TRUE(pl::try_clear_faults().has_value());
    power_cycle();
    start_after(reset_reason::panic);
    assert_no_dump(only_fault("panic"));
}

TEST_CASE(
    "persistent_log start doesn't use a dump again once its fault is overwritten",
    "[persistent_log]"
) {
    start_clean();
    xtensa_dump(172);
    start_after(reset_reason::panic);
    TEST_ASSERT_TRUE(only_fault("panic").task() == "pl_task");
    // Faults that take no dump fill every slot, so the panic that took the dump is gone.
    const size_t slots = pl::fault_config{}.slot_count;
    for (size_t i = 0; i < slots; ++i) {
        TEST_ASSERT_TRUE(pl::try_record_fault("plain").value_or(false));
    }
    auto got = faults();
    TEST_ASSERT_EQUAL_size_t(slots, got.size());
    for (const auto& f : got) {
        TEST_ASSERT_TRUE(f.kind() == "plain");
    }
    power_cycle();
    start_after(reset_reason::panic);
    got = faults();
    TEST_ASSERT_EQUAL_size_t(slots, got.size());
    TEST_ASSERT_TRUE(got.back().kind() == "panic");
    assert_no_dump(got.back());
}

TEST_CASE("persistent_log start uses a new dump after a power cycle", "[persistent_log]") {
    start_clean();
    xtensa_dump(173);
    start_after(reset_reason::panic);
    power_cycle();
    // The same summary in another image is a new dump.
    s_fake.crc = 174;
    start_after(reset_reason::panic);
    auto got = faults();
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(got.back().detail() == "exccause=28 excvaddr=0x00001234 pc=0x42001234");
    TEST_ASSERT_TRUE(got.back().task() == "pl_task");
    TEST_ASSERT_EQUAL_size_t(3, got.back().frames().size());
    TEST_ASSERT_EQUAL_UINT32(1, internal::rtc().faults.has_core_dump_crc);
    TEST_ASSERT_EQUAL_UINT32(174, internal::rtc().faults.core_dump_crc);
}
