// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "sdkconfig.h"

#ifdef CONFIG_IDFXX_STD_FORMAT
#include "capture.hpp"

#include <idfxx/log>
#endif
#include "entry_buffer.hpp"
#include "fault_store.hpp"
#include "flash_log.hpp"
#include "idfxx/persistent_log.hpp"
#include "line_ring.hpp"
#include "log_line.hpp"
#include "reset.hpp"
#include "rtc.hpp"
#include "service.hpp"
#include "store_range.hpp"

#include <idfxx/partition>
#include <idfxx/task>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <esp_app_desc.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>

// Start reads a core dump only if it is in flash and in ELF form. ELF is the only form from ESP-IDF
// 6.0 on, where the option that selects it no longer exists.
#include <esp_idf_version.h>
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) &&                                                \
    (defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF) || ESP_IDF_VERSION_MAJOR >= 6)
#define PL_CORE_DUMP 1
#include <esp_core_dump.h>
#include <esp_partition.h>
#else
#define PL_CORE_DUMP 0
#endif

namespace idfxx::persistent_log {

namespace {

using namespace std::chrono;

constexpr const char* TAG = "persistent_log";

constexpr size_t ring_size = CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE;
constexpr size_t max_line = CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE;

// The ring holds the longest entry (see max_entry_size). The assertion's message below and the
// option's help both state max_prefix_size + 1 as 73.
static_assert(max_prefix_size + 1 == 73);
static_assert(
    ring_size >= max_entry_size,
    "CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE must be at least 73 + "
    "CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE + CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE / 2, to hold a "
    "whole entry"
);

using internal::ring_type;

// The number of characters that snprintf put in a buffer of @p room bytes, given its result @p k.
constexpr size_t printed(int k, size_t room) noexcept {
    return k > 0 ? std::min(static_cast<size_t>(k), room - 1) : 0;
}

// =============================================================================
// RTC state
// =============================================================================

static_assert(std::is_trivially_default_constructible_v<ring_type>);
static_assert(fault_config{}.slot_size == default_fault_slot_size);

RTC_NOINIT_ATTR ring_type s_ring;
RTC_NOINIT_ATTR internal::rtc_state s_rtc;

// Guards the ring against concurrent access from any task. It is held only to copy bytes into or
// out of the ring.
portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;

// Zeroes and seals a record that isn't valid. Returns whether the record was valid.
template<typename Record>
bool keep_or_reset(Record& r) noexcept {
    if (internal::valid(r)) {
        return true;
    }
    std::memset(&r, 0, sizeof r);
    internal::seal(r);
    return false;
}

// =============================================================================
// Reentrancy
// =============================================================================

// Set while a task is inside the log, which is while it is in capture, in append(), or holding the
// flash lock. The lines that the task logs while the flag is set are not captured, so they can't
// feed back into the log. These include the log's diagnostics, the errors from a failing flash
// write, and the lines from a wall clock that logs.
thread_local bool t_busy = false;

bool scheduler_running() noexcept {
    return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}

class busy_guard {
public:
    busy_guard() noexcept
        : _active(scheduler_running()) {
        if (_active) {
            _was = t_busy;
            t_busy = true;
        }
    }
    ~busy_guard() {
        if (_active) {
            t_busy = _was;
        }
    }
    busy_guard(const busy_guard&) = delete;
    busy_guard& operator=(const busy_guard&) = delete;

private:
    bool _active;
    bool _was = false;
};

// =============================================================================
// Running state
// =============================================================================

// Serializes start and stop. Each holds it from beginning to end.
std::mutex s_lifecycle;
// Protected by s_lifecycle.
bool s_started = false;
bool s_wake_counted = false;
std::optional<idfxx::task> s_writer;

// s_running is true while the log takes lines. append() counts itself in s_users while it uses the
// settings below. stop() changes those settings only after s_running is false and s_users is 0, and
// after it has removed the capture sink, which also uses them. A flush stamps lines with s_boot,
// s_wake and s_wall_clock while it holds s_flash. So start sets those three before it publishes the
// log, and stop clears s_wall_clock only after it has closed the log.
std::atomic<bool> s_running{false};
std::atomic<uint32_t> s_users{0};

uint32_t s_boot = 0;
uint32_t s_wake = 0;
std::function<std::optional<system_clock::time_point>()> s_wall_clock;
#ifdef CONFIG_IDFXX_STD_FORMAT
internal::level_filter s_filter;
#endif
size_t s_threshold = std::numeric_limits<size_t>::max();
std::atomic<TaskHandle_t> s_writer_handle{nullptr};
// The number of keep() calls that have loaded s_writer_handle and have not yet notified the writer
// through it.
std::atomic<uint32_t> s_notifying{0};

class user {
public:
    user() noexcept {
        s_users.fetch_add(1);
        _in = s_running.load();
        if (!_in) {
            s_users.fetch_sub(1);
        }
    }
    ~user() {
        if (_in) {
            s_users.fetch_sub(1);
        }
    }
    user(const user&) = delete;
    user& operator=(const user&) = delete;

    explicit operator bool() const noexcept { return _in; }

private:
    bool _in;
};

// =============================================================================
// Flash
// =============================================================================

// Guards the stores, the log and the fault store in flash, the hint in RTC memory, and the flush
// buffer. It is held for a complete flush, clear or open, and for each piece of a read.
std::mutex s_flash;
// The log's store, either passed in the config or made for the log's partition, and the ranges of
// that store that the log and the fault slots use.
std::unique_ptr<sector_store> s_store;
std::optional<store_range> s_log_region;
std::optional<store_range> s_fault_region;
std::optional<flash_log> s_log;
std::optional<fault_store> s_faults;
// Incremented at each stop, so that readers created before the stop can't read any more.
uint32_t s_generation = 0;
// Where a flush copies the ring. It is in internal RAM, because flash writes run with the cache
// off.
char s_flush_buffer[ring_size];
// Where a flush formats the line that counts the bytes lost before the flush. It is in internal RAM
// for the same reason. The line is never formatted in s_flush_buffer, which has room only for the
// ring.
char s_loss_line[max_prefix_size + 64];

// Holds s_flash, and marks the task as busy so that the lines it logs while it holds the lock
// aren't captured.
class flash_lock {
public:
    flash_lock()
        : _lock(s_flash) {}

private:
    busy_guard _quiet;
    std::lock_guard<std::mutex> _lock;
};

// Call it with s_flash held.
void save_hint() noexcept {
    const auto h = s_log ? s_log->head() : std::nullopt;
    s_rtc.hint.has_hint = h ? 1 : 0;
    s_rtc.hint.sector = h ? h->sector : 0;
    s_rtc.hint.offset = h ? h->offset : 0;
    s_rtc.hint.seq = h ? h->seq : 0;
    internal::seal(s_rtc.hint);
}

// Call it with s_flash held.
std::optional<position> kept_hint(uint32_t sector_size, uint32_t size) noexcept {
    const auto& h = s_rtc.hint;
    if (!internal::valid(h) || h.has_hint != 1 || h.sector_size != sector_size || h.size != size) {
        return std::nullopt;
    }
    return position{.sector = h.sector, .offset = h.offset, .seq = h.seq};
}

size_t ring_unflushed() noexcept {
    portENTER_CRITICAL_SAFE(&s_ring_lock);
    const size_t n = s_ring.unflushed();
    portEXIT_CRITICAL_SAFE(&s_ring_lock);
    return n;
}

size_t stamp(std::span<char> out);

// Appends text to the log in flash, and logs an error if the append fails. Call it with s_flash
// held.
idfxx::result<void> append_to_log(std::string_view text) {
    auto written = s_log->try_append(text);
    if (!written) {
        ESP_LOGE(
            TAG,
            "flash write of %u bytes failed: %s",
            static_cast<unsigned>(text.size()),
            written.error().message().c_str()
        );
    }
    return written;
}

// Appends a stamped line that counts the bytes lost before they were flushed. Call it with s_flash
// held.
idfxx::result<void> append_loss(uint64_t lost) {
    size_t n = stamp(s_loss_line);
    const int m = std::snprintf(
        s_loss_line + n,
        sizeof s_loss_line - n,
        "W %s: dropped %lu bytes\n",
        TAG,
        static_cast<unsigned long>(std::min<uint64_t>(lost, std::numeric_limits<uint32_t>::max()))
    );
    n += printed(m, sizeof s_loss_line - n);
    return append_to_log({s_loss_line, n});
}

// Copies the ring's pending bytes @p p to the start of s_flush_buffer, and returns the number of
// bytes copied. Call it with s_flash and s_ring_lock held.
size_t copy_pending(const pending_bytes& p) noexcept {
    const size_t first = std::min(p.first.size(), ring_size);
    const size_t size = first + std::min(p.second.size(), ring_size - first);
    std::memcpy(s_flush_buffer, p.first.data(), first);
    std::memcpy(s_flush_buffer + first, p.second.data(), size - first);
    return size;
}

// Moves what the ring holds to flash. Call it with s_flash held.
idfxx::result<void> flush_locked() {
    if (!s_log) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    portENTER_CRITICAL_SAFE(&s_ring_lock);
    if (!s_ring.valid()) {
        // The ring is valid from start on, unless something else changes its memory. In that case
        // its contents can't be trusted, and they are counted as lost.
        const size_t lost = std::min(s_ring.unflushed(), ring_size);
        s_ring.reset();
        s_ring.dropped = static_cast<uint32_t>(lost);
    }
    const auto p = s_ring.pending();
    const size_t size = copy_pending(p);
    // The bytes are marked flushed before they are written. So a reset or a failed write in the
    // middle of the flush loses the rest of the bytes, rather than writing again the part already
    // in flash.
    s_ring.mark_flushed(p);
    portEXIT_CRITICAL_SAFE(&s_ring_lock);
    if (size == 0 && p.lost == 0) {
        return {};
    }
    idfxx::result<void> written;
    size_t from = 0;
    if (p.lost > 0) {
        // The oldest bytes were lost before they were flushed, so the first line may be missing its
        // start. That line is left out, up to its newline, and its bytes are counted with the lost
        // bytes in a separate line. The lines after it that start with a tab continue its entry, so
        // they are left out and counted with it.
        from = detail::entry_start([](size_t i) { return s_flush_buffer[i]; }, 0, size, false);
        written = append_loss(uint64_t{p.lost} + from);
    }
    if (from < size) {
        auto appended = append_to_log({s_flush_buffer + from, size - from});
        if (written) {
            written = appended;
        }
    }
    save_hint();
    return written;
}

// =============================================================================
// Lines
// =============================================================================

std::optional<system_clock::time_point> wall_now() {
    if (s_wall_clock) {
        return s_wall_clock();
    }
    const auto now = system_clock::now();
    if (now < sys_days{year{2020} / 1 / 1}) {
        return std::nullopt;
    }
    return now;
}

// The time at which a line is kept. It holds the time since the boot or the deep-sleep wake, and
// the wall-clock time if it is known.
struct moment {
    milliseconds uptime;
    std::optional<system_clock::time_point> wall;
};

moment now() {
    return {
        .uptime = duration_cast<milliseconds>(microseconds{esp_timer_get_time()}),
        .wall = wall_now(),
    };
}

size_t stamp(std::span<char> out, const moment& m) {
    size_t n = format_prefix(out, s_boot, s_wake, m.uptime, m.wall);
    if (n == 0) {
        // The wall-clock time is outside the years 0 to 9999.
        n = format_prefix(out, s_boot, s_wake, m.uptime, std::nullopt);
    }
    return n;
}

size_t stamp(std::span<char> out) {
    return stamp(out, now());
}

// Adds the text of @p pieces, joined in order, to the ring as one entry stamped at @p m. The
// complete entry is added under one hold of the ring's lock, so that no other entry and no flush
// can come between its lines. The entry is the stamp and the first line, then each later non-empty
// line after a tab, then a newline. If enough bytes are waiting, it wakes the writer.
void keep(const moment& m, std::initializer_list<std::string_view> pieces) noexcept {
    std::array<char, max_prefix_size> st;
    const size_t sn = stamp(st, m);
    portENTER_CRITICAL_SAFE(&s_ring_lock);
    s_ring.append({st.data(), sn});
    // True after a newline, until the next line's first text. A tab goes before that text, and a
    // line without any text is skipped. A line may continue from one piece into the next.
    bool after_newline = false;
    for (std::string_view rest : pieces) {
        while (!rest.empty()) {
            const size_t end = rest.find('\n');
            if (const std::string_view text = rest.substr(0, end); !text.empty()) {
                if (after_newline) {
                    s_ring.append("\n\t");
                    after_newline = false;
                }
                s_ring.append(text);
            }
            if (end == std::string_view::npos) {
                break;
            }
            after_newline = true;
            rest.remove_prefix(end + 1);
        }
    }
    s_ring.append("\n");
    const size_t waiting = s_ring.unflushed();
    portEXIT_CRITICAL_SAFE(&s_ring_lock);
    if (waiting >= s_threshold) {
        // This call counts itself in s_notifying from before it loads the handle until it has
        // notified the writer. Stop clears the handle and then waits for the count to be 0, so it
        // never destroys a writer that is about to be notified.
        s_notifying.fetch_add(1);
        if (TaskHandle_t writer = s_writer_handle.load(); writer != nullptr) {
            xTaskNotifyGive(writer);
        }
        s_notifying.fetch_sub(1);
    }
}

// Adds @p text to the ring as one entry stamped at @p m.
void keep(const moment& m, const char* text, size_t size) noexcept {
    keep(m, {std::string_view{text, size}});
}

// Adds @p text to the ring as one entry stamped now.
void keep(const char* text, size_t size) noexcept {
    keep(now(), text, size);
}

#ifdef CONFIG_IDFXX_STD_FORMAT

// =============================================================================
// Capture
// =============================================================================

// Receives log lines from idfxx::log, and keeps the lines that s_filter passes as entries
// "<letter> <tag>: <message>", or "<letter> <message>" for a line without a tag. With
// CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES, start adds it with all_lines, so that it receives
// every ESP-IDF log line. Otherwise, it receives only the lines logged through idfxx::log. Start
// adds it after everything that keep() reads is set. Stop removes it first, and waits for the calls
// into it to end before it changes any of those settings.
class capture_sink final : public idfxx::log::sink {
    bool do_wants(idfxx::log::level lvl, std::string_view tag) const noexcept override {
        return !t_busy && s_filter.keeps(lvl, tag);
    }

    void do_write(
        idfxx::log::level lvl,
        std::string_view tag,
        std::string_view message
    ) noexcept override {
        if (t_busy) {
            return;
        }
        busy_guard quiet;
        static constexpr char letters[] = "?EWIDV";
        const auto i = static_cast<size_t>(lvl);
        const std::array<char, 2> letter = {i < sizeof letters - 1 ? letters[i] : '?', ' '};
        // The head is the letter and the tag, with the tag shortened to half the longest entry. The
        // message is shortened to the room that the head leaves in the longest entry. Each is
        // shortened at the start of a UTF-8 character. The message is shortened only when it is
        // longer than that room, so that otherwise it is kept unchanged. The pieces are passed to
        // the ring as they are, and are never copied into one line here.
        const std::string_view kept_tag = detail::utf8_prefix(tag, max_line / 2);
        const std::string_view colon = tag.empty() ? std::string_view{} : std::string_view{": "};
        const size_t room = max_line - letter.size() - kept_tag.size() - colon.size();
        const std::string_view kept =
            message.size() > room ? detail::utf8_prefix(message, room) : message;
        keep(now(), {std::string_view{letter.data(), letter.size()}, kept_tag, colon, kept});
    }
};

constinit capture_sink s_capture;

#endif // CONFIG_IDFXX_STD_FORMAT

// =============================================================================
// Start
// =============================================================================

// Counts the wake, once per boot. It reads the reset reason from the reset source, as take_reset()
// does, so that the two agree. Call it with s_lifecycle and s_flash held.
void count_wake() noexcept {
    if (s_wake_counted) {
        return;
    }
    internal::check_rtc(internal::reset_hooks().reason() == idfxx::reset_reason::deep_sleep);
    s_wake_counted = true;
}

// It is never inlined, so that its locals are not in start_with's frame.
[[gnu::noinline]] idfxx::result<void> validate(const config& cfg) {
    if (cfg.writer) {
        const writer_config& w = *cfg.writer;
        // The tick conversion truncates to whole ticks and multiplies in 32 bits, so the interval
        // must be at least a tick and short enough for that multiply not to wrap.
        constexpr int64_t max_interval_ms =
            std::numeric_limits<uint32_t>::max() / configTICK_RATE_HZ;
        if (w.interval < milliseconds{portTICK_PERIOD_MS} ||
            w.interval > milliseconds{max_interval_ms}) {
            return idfxx::error(idfxx::errc::invalid_arg);
        }
        if (w.threshold == 0 || w.threshold >= ring_size) {
            return idfxx::error(idfxx::errc::invalid_arg);
        }
        // These are checked here, because FreeRTOS asserts on a priority that is out of range or a
        // stack that is too small to run.
        if (w.priority.value() >= static_cast<unsigned>(configMAX_PRIORITIES) ||
            w.stack_size < static_cast<size_t>(configMINIMAL_STACK_SIZE)) {
            return idfxx::error(idfxx::errc::invalid_arg);
        }
    }
    if (cfg.faults && (cfg.faults->slot_count == 0 || cfg.faults->slot_size == 0)) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    return {};
}

// Returns the store that @p cfg names. That is either the complete partition with the configured
// label, or the store that @p cfg passes in. It is never inlined, so that its locals are not in
// open_flash's frame.
[[gnu::noinline]] idfxx::result<std::unique_ptr<sector_store>> take_store(config& cfg) {
    if (auto* given = std::get_if<std::unique_ptr<sector_store>>(&cfg.storage)) {
        if (!*given) {
            return idfxx::error(idfxx::errc::invalid_arg);
        }
        return std::move(*given);
    }
    auto part = idfxx::partition::try_find(std::get<std::string>(cfg.storage));
    if (!part) {
        return idfxx::error(part.error());
    }
    auto whole = partition_sector_store::make(*part);
    if (!whole) {
        return idfxx::error(whole.error());
    }
    return std::make_unique<partition_sector_store>(std::move(*whole));
}

// Opens the fault store in the @p fault_sectors sectors of s_store that follow the first
// @p log_sectors sectors. Call it with s_flash held. It is never inlined, so that the fault store
// and the result that holds it are not in open_flash's frame while open_flash ends the last line in
// flash.
[[gnu::noinline]] idfxx::result<void>
open_faults(size_t slot_size, size_t log_sectors, size_t fault_sectors) {
    s_fault_region.emplace(*s_store, log_sectors, fault_sectors);
    auto faults = fault_store::try_open(*s_fault_region, slot_size);
    if (!faults) {
        return idfxx::error(faults.error());
    }
    s_faults.emplace(std::move(*faults));
    return {};
}

// Logs that ending the last line in flash failed. It is never inlined, so that the error's message
// is not in open_flash's frame.
[[gnu::noinline]] void warn_end_line_failed(const std::error_code& ec) {
    ESP_LOGW(TAG, "ending the last line in flash failed: %s", ec.message().c_str());
}

// Logs the warnings for log sectors and fault slots of a newer layout. @p label is the partition's
// label, or null when the config passes in a store. Call it with s_flash held. It is never inlined,
// so that its locals are not in open_flash's frame.
[[gnu::noinline]] void warn_newer_format(const std::string* label) {
    // Logs the warning for one kind of record. @p what is "log sectors" or "fault slots".
    const auto warn = [label](const char* what) {
        if (label == nullptr) {
            ESP_LOGW(
                TAG, "the log's store holds %s of a newer layout; they are taken as unused", what
            );
        } else {
            ESP_LOGW(
                TAG,
                "partition '%s' holds %s of a newer layout; they are taken as unused",
                label->c_str(),
                what
            );
        }
    };
    if (s_log->found_newer_format()) {
        warn("log sectors");
    }
    if (s_faults && s_faults->found_newer_format()) {
        warn("fault slots");
    }
}

// Opens the stores, the log and the fault store, and takes the store from @p cfg. Call it with
// s_lifecycle held and start_undo armed. If this function fails, start_undo closes what it left
// open.
[[gnu::noinline]] idfxx::result<void> open_flash(config& cfg) {
    // Used in the warnings below. It is null when cfg passes in a store.
    const std::string* label = std::get_if<std::string>(&cfg.storage);
    auto taken = take_store(cfg);
    if (!taken) {
        return idfxx::error(taken.error());
    }
    std::unique_ptr<sector_store> store = std::move(*taken);
    // The store's implementation supplies its geometry, so the geometry is checked before the code
    // divides by it.
    const size_t sector_size = store->sector_size();
    if (sector_size == 0 || store->size() % sector_size != 0) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    const size_t total = store->size();
    const size_t sectors = store->sector_count();
    size_t fault_sectors = 0;
    if (cfg.faults) {
        if (cfg.faults->slot_size > total / cfg.faults->slot_count ||
            cfg.faults->slot_size % sector_size != 0) {
            return idfxx::error(idfxx::errc::invalid_arg);
        }
        fault_sectors = cfg.faults->slot_size / sector_size * cfg.faults->slot_count;
        if (fault_sectors >= sectors) {
            return idfxx::error(idfxx::errc::invalid_arg);
        }
    }
    const size_t log_sectors = sectors - fault_sectors;
    const size_t log_bytes = log_sectors * sector_size;

    flash_lock lock;
    count_wake();
    // This runs before the log is published, so that a flush that waits for the lock never reads a
    // ring left by a power-on or by a build with another ring size. A line that a reset left
    // unfinished is ended, so that the next line isn't joined to it.
    portENTER_CRITICAL_SAFE(&s_ring_lock);
    if (!s_ring.valid()) {
        s_ring.reset();
    }
    s_ring.end_line();
    portEXIT_CRITICAL_SAFE(&s_ring_lock);
    s_store = std::move(store);
    s_log_region.emplace(*s_store, 0, log_sectors);
    // A hint taken on a range of another geometry is never used. A hint taken on a range of the
    // same geometry is checked against what the range holds.
    auto opened = flash_log::try_open(
        *s_log_region,
        kept_hint(static_cast<uint32_t>(sector_size), static_cast<uint32_t>(log_bytes))
    );
    if (!opened) {
        return idfxx::error(opened.error());
    }
    if (cfg.faults) {
        if (auto faults = open_faults(cfg.faults->slot_size, log_sectors, fault_sectors); !faults) {
            return faults;
        }
    }
    // A line in flash that a power cut left unfinished is ended, so that the first line flushed
    // isn't joined to it. If ending the line fails, start still keeps the log.
    if (auto ended = opened->try_end_line(); !ended) {
        warn_end_line_failed(ended.error());
    }
    // These are set before the log is published, because a flush that has waited for the lock
    // stamps lines with them.
    s_boot = cfg.boot_count;
    s_wake = s_rtc.wake.wake;
    s_wall_clock = cfg.wall_clock;
    s_log.emplace(std::move(*opened));
    s_rtc.hint.sector_size = static_cast<uint32_t>(sector_size);
    s_rtc.hint.size = static_cast<uint32_t>(log_bytes);
    save_hint();

    warn_newer_format(label);
    return {};
}

// Call it with s_flash held.
void close_flash() {
    s_faults.reset();
    s_log.reset();
    s_fault_region.reset();
    s_log_region.reset();
    s_store.reset();
    ++s_generation;
}

// Stops taking lines, and waits for the lines being taken to finish. Then it stops the writer task,
// flushes the waiting lines if @p flush is true, and closes the log. Last, it restores the settings
// that start set. Returns the flush's result. Call it with s_lifecycle held.
idfxx::result<void> shut_down(bool flush) {
#ifdef CONFIG_IDFXX_STD_FORMAT
    // This comes first, because it returns only once no call into the sink is in progress. So no
    // call is left to use what is torn down below, and no line is captured after the final flush.
    // Nothing that the sink calls takes s_lifecycle. For a start that failed before adding the
    // sink, this does nothing.
    idfxx::log::remove_sink(s_capture);
#endif
    s_running = false;
    while (s_users.load() != 0) {
        vTaskDelay(1);
    }
    // A keep() that loaded the handle before it was cleared stays counted in s_notifying until it
    // has notified the writer. A keep() that counts itself after the count is seen to be 0 loads
    // the cleared handle. So once the count is 0, nothing will notify the writer.
    s_writer_handle = nullptr;
    while (s_notifying.load() != 0) {
        vTaskDelay(1);
    }
    s_writer.reset();

    idfxx::result<void> flushed;
    {
        flash_lock lock;
        if (flush) {
            flushed = flush_locked();
        }
        close_flash();
    }

    // This is cleared only after the log is closed, because a flush stamps lines with the wall
    // clock.
    s_wall_clock = {};
#ifdef CONFIG_IDFXX_STD_FORMAT
    s_filter = {};
#endif
    s_threshold = std::numeric_limits<size_t>::max();
    s_started = false;
    return flushed;
}

// Shuts down a start without a flush, unless start disarms it once nothing more in the start can
// fail. It does this in a destructor, so that it runs both when start returns an error and when an
// exception is thrown.
class start_undo {
public:
    ~start_undo() {
        if (_armed) {
            (void)shut_down(false);
        }
    }

    void disarm() noexcept { _armed = false; }

private:
    bool _armed = true;
};

// =============================================================================
// Faults
// =============================================================================

// The heads of the log entries that faults add. Each head is followed by the kind and, after a
// space, the kept detail, as in "E persistent_log: fault kind=<kind> <detail>".
constexpr std::string_view fault_head = "E persistent_log: fault kind=";
constexpr std::string_view repeat_head = "W persistent_log: fault repeat kind=";
constexpr std::string_view not_recorded_head = "W persistent_log: fault not recorded kind=";
constexpr std::string_view lifted_head = "I persistent_log: fault suppression lifted kind=";
// The size of the longest such entry's text, shortened to max_line in the same way as the text that
// append() keeps.
constexpr size_t fault_line_size = std::min(repeat_head.size() + 23 + 1 + 159, max_line);

// Adds a fault's entry to the ring, stamped at @p m. The entry is @p head, the kind, and the detail
// if there is one. It is never inlined, so that its text is off the stack before a record that
// follows it.
[[gnu::noinline]] void keep_fault_line(
    const moment& m,
    std::string_view head,
    std::string_view kind,
    std::string_view what
) noexcept {
    std::array<char, fault_line_size> text;
    size_t n = 0;
    // Nothing is added after a piece that was shortened.
    bool cut = false;
    const auto put = [&](std::string_view piece) {
        if (cut) {
            return;
        }
        const std::string_view kept = detail::utf8_prefix(piece, text.size() - n);
        std::memcpy(text.data() + n, kept.data(), kept.size());
        n += kept.size();
        cut = kept.size() < piece.size();
    };
    put(head);
    put(kind);
    if (!what.empty()) {
        put(" ");
        put(what);
    }
    keep(m, text.data(), n);
}

// A fault's context. It is the end of the log in flash, followed by a snapshot of the ring's
// unflushed bytes in s_flush_buffer. It is used with s_flash held, so neither part changes while
// the fault is recorded.
class log_context final : public fault_context {
public:
    log_context(size_t from, size_t end)
        : _at(from)
        , _end(end) {}

    // Creates, in place, the reader for the last @p want bytes of the log in flash.
    [[nodiscard]] idfxx::result<void> read_tail(size_t want) {
        if (auto r = s_log->try_read_tail(want, _tail); !r) {
            return r;
        }
        _tail_left = _tail->size();
        return {};
    }

    [[nodiscard]] size_t size() const override { return _tail_left + (_end - _at); }

    [[nodiscard]] idfxx::result<void> fill(std::span<char> out) override {
        size_t got = 0;
        while (got < out.size() && _tail_left > 0) {
            auto n = _tail->try_next(out.subspan(got, std::min(out.size() - got, _tail_left)));
            if (!n) {
                return idfxx::error(n.error());
            }
            if (*n == 0) {
                // The log changed while the lock was held, which no code does.
                return idfxx::error(idfxx::errc::invalid_state);
            }
            got += *n;
            _tail_left -= *n;
        }
        const size_t n = out.size() - got;
        if (n > _end - _at) {
            return idfxx::error(idfxx::errc::invalid_state);
        }
        std::memcpy(out.data() + got, s_flush_buffer + _at, n);
        _at += n;
        return {};
    }

private:
    std::optional<flash_log::tail_reader> _tail;
    size_t _tail_left = 0;
    size_t _at;
    size_t _end;
};

// Where a fault occurred, when that isn't the calling task with no program counter. It is either
// the place that a core dump records, or empty when nothing is known.
struct fault_place {
    std::string_view task;
    uint32_t exception_pc = 0;
    std::span<const uint32_t> frames;
    // The CRC of the core dump that the place came from, if any. The fault store keeps it as the
    // last dump taken.
    std::optional<uint32_t> dump_crc = std::nullopt;
};

// Records a fault of a kind that recordable_kind() accepts, with @p what as its kept detail. The
// fault is recorded as occurring in the calling task, unless @p place gives where it occurred. Call
// it with s_flash held and with the log and the fault store open.
idfxx::result<void> record_fault_locked(
    std::string_view kind,
    std::string_view what,
    const fault_place* place = nullptr
) {
    // The entry is added first, so that the context ends with it. The fault uses the time of the
    // entry's stamp.
    const moment m = now();
    keep_fault_line(m, fault_head, kind, what);

    // This copies the ring's unflushed bytes into the flush buffer. The ring may change while the
    // slot is erased and written, but the copy stays as it was. The bytes are not marked flushed,
    // so a flush still writes them to the log.
    size_t size = 0;
    uint32_t lost = 0;
    portENTER_CRITICAL_SAFE(&s_ring_lock);
    if (s_ring.valid()) {
        const auto p = s_ring.pending();
        size = copy_pending(p);
        lost = p.lost;
    }
    portEXIT_CRITICAL_SAFE(&s_ring_lock);
    const auto at = [](size_t i) { return s_flush_buffer[i]; };
    // After a loss, the bytes may begin in the middle of an entry, as they may at a flush.
    size_t from = lost > 0 ? detail::entry_start(at, 0, size, false) : 0;

    // The context holds the newest complete entries that fit. When the snapshot fills the room,
    // they come from the snapshot alone. Otherwise, the context also holds the end of the log in
    // flash before the snapshot, which ends where the snapshot begins.
    const size_t room = s_faults->room();
    const bool ring_only = size - from >= room;
    if (ring_only) {
        const size_t cut = size - room;
        from = detail::entry_start(at, cut, size, cut == from || s_flush_buffer[cut - 1] == '\n');
    }
    log_context context(from, size);
    if (!ring_only) {
        if (auto r = context.read_tail(room - (size - from)); !r) {
            return r;
        }
    }

    const esp_app_desc_t* app = esp_app_get_description();
    std::array<std::byte, fault_elf_sha256_size> elf;
    std::memcpy(elf.data(), app->app_elf_sha256, elf.size());
    const fault_input f{
        .kind = kind,
        .detail = what,
        .version = std::string_view(app->version, strnlen(app->version, sizeof app->version)),
        .boot = s_boot,
        .wake = s_wake,
        .wall = m.wall,
        .uptime = m.uptime,
        .elf_sha256 = elf,
        .task = place != nullptr  ? place->task
            : scheduler_running() ? std::string_view(pcTaskGetName(nullptr))
                                  : std::string_view(),
        .exception_pc = place != nullptr ? place->exception_pc : 0,
        .frames = place != nullptr ? place->frames : std::span<const uint32_t>(),
        .dump_crc = place != nullptr ? place->dump_crc : std::nullopt,
    };
    auto recorded = s_faults->try_record(f, context);
    if (!recorded) {
        return idfxx::error(recorded.error());
    }
    return {};
}

// The CRC by which a suppressed kind is remembered. 0 marks an empty slot in fault_memory, so a
// kind whose CRC is 0 is remembered as 1 instead, to tell it apart from an empty slot.
uint32_t kind_crc(std::string_view kind) noexcept {
    const uint32_t crc = esp_rom_crc32_le(
        0, reinterpret_cast<const uint8_t*>(kind.data()), static_cast<uint32_t>(kind.size())
    );
    return crc != 0 ? crc : 1;
}

// Records a fault of a kind that recordable_kind() accepts, with @p what as its kept detail, unless
// its kind is suppressed. Once the fault is recorded, this suppresses its kind if
// @p suppress_repeats is true. If storing the fault fails, this adds an entry that says that the
// fault is not recorded. Call it with s_flash held and with the fault store open.
idfxx::result<bool> record_unless_repeat_locked(
    std::string_view kind,
    std::string_view what,
    bool suppress_repeats,
    const fault_place* place = nullptr
) {
    const uint32_t kc = kind_crc(kind);
    auto& memory = s_rtc.faults.suppressed;
    if (memory.repeats(kc)) {
        keep_fault_line(now(), repeat_head, kind, what);
        return false;
    }
    if (auto recorded = record_fault_locked(kind, what, place); !recorded) {
        // The fault's entry is already in the ring. This entry follows it, so that the log does
        // not read as if the fault was recorded.
        keep_fault_line(now(), not_recorded_head, kind, {});
        return idfxx::error(recorded.error());
    }
    if (suppress_repeats) {
        memory.remember(kc);
        internal::seal(s_rtc.faults);
    }
    return true;
}

// The checks that every fault call makes, in order. Call it with s_flash held.
idfxx::result<void> faults_open() {
    if (!s_log) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    if (!s_faults) {
        return idfxx::error(idfxx::errc::not_supported);
    }
    return {};
}

// The work that every form of try_record_fault does. It makes the checks that every fault call
// makes, then records the fault.
idfxx::result<bool>
record_fault_checked(std::string_view kind, std::string_view detail, bool suppress_repeats) {
    flash_lock lock;
    if (auto open = faults_open(); !open) {
        return idfxx::error(open.error());
    }
    if (!recordable_kind(kind)) {
        return idfxx::error(idfxx::errc::invalid_arg);
    }
    return record_unless_repeat_locked(kind, kept_detail(detail), suppress_repeats);
}

// =============================================================================
// The reset before start
// =============================================================================

// idfxx::reset_reason's enumerator names, by value.
constexpr std::array<std::string_view, 16> reset_names = {
    "unknown",
    "power_on",
    "external",
    "software",
    "panic",
    "interrupt_watchdog",
    "task_watchdog",
    "watchdog",
    "deep_sleep",
    "brownout",
    "sdio",
    "usb",
    "jtag",
    "efuse",
    "power_glitch",
    "cpu_lockup",
};
static_assert(static_cast<int>(idfxx::reset_reason::panic) == 4);
static_assert(static_cast<int>(idfxx::reset_reason::deep_sleep) == 8);
static_assert(static_cast<int>(idfxx::reset_reason::cpu_lockup) == 15);

// The resets that are faults. Each is recorded as a fault whose kind is the reset's name, with its
// repeats suppressed.
constexpr std::array<idfxx::reset_reason, 7> fault_resets = {
    idfxx::reset_reason::panic,
    idfxx::reset_reason::interrupt_watchdog,
    idfxx::reset_reason::task_watchdog,
    idfxx::reset_reason::watchdog,
    idfxx::reset_reason::brownout,
    idfxx::reset_reason::power_glitch,
    idfxx::reset_reason::cpu_lockup,
};

// The log entry that a reset adds when it isn't recorded as a fault:
// "I persistent_log: reset reason=<name>".
constexpr std::string_view reset_head = "I persistent_log: reset reason=";

// A reset reason's name. It is the enumerator's name, or unknown_<n> for any other value.
struct reset_name {
    std::array<char, 24> text{};
    size_t size = 0;

    [[nodiscard]] std::string_view view() const noexcept { return {text.data(), size}; }
};

reset_name name_of(idfxx::reset_reason r) noexcept {
    reset_name n;
    const int v = static_cast<int>(r);
    if (v >= 0 && static_cast<size_t>(v) < reset_names.size()) {
        const std::string_view s = reset_names[static_cast<size_t>(v)];
        std::memcpy(n.text.data(), s.data(), s.size());
        n.size = s.size();
    } else {
        const int k = std::snprintf(n.text.data(), n.text.size(), "unknown_%d", v);
        n.size = printed(k, n.text.size());
    }
    return n;
}

bool is_fault_reset(idfxx::reset_reason r) noexcept {
    return std::ranges::find(fault_resets, r) != fault_resets.end();
}

// The resets that a core dump may give more detail about.
bool dumps(idfxx::reset_reason r) noexcept {
    return r == idfxx::reset_reason::panic || r == idfxx::reset_reason::interrupt_watchdog ||
        r == idfxx::reset_reason::task_watchdog;
}

// Where a reset's fault occurred and why, as read from a core dump.
struct dump_fault {
    std::array<char, fault_detail_size> detail{};
    size_t detail_size = 0;
    std::array<char, 16> task{};
    uint32_t pc = 0;
    std::array<uint32_t, max_fault_frames> frames{};
    size_t frame_count = 0;
    // The CRC of the dump's image. The fault store keeps it with the recorded fault, and the caller
    // notes it in RTC memory once the fault is recorded or suppressed as a repeat.
    uint32_t crc = 0;

    [[nodiscard]] std::string_view detail_view() const noexcept {
        return {detail.data(), detail_size};
    }

    [[nodiscard]] fault_place place() const noexcept {
        return {
            .task = std::string_view(task.data(), strnlen(task.data(), task.size())),
            .exception_pc = pc,
            .frames = std::span(frames).first(frame_count),
            .dump_crc = crc,
        };
    }
};

// Whether a dump came from this build. It did when the hex SHA-256 of the ELF file is the same in
// both, up to the length of the shorter one. It didn't when either is empty.
bool same_build(const internal::dump_summary& s) noexcept {
    std::array<char, 65> mine{};
    esp_app_get_elf_sha256(mine.data(), mine.size());
    const std::string_view a(mine.data(), strnlen(mine.data(), mine.size()));
    const std::string_view b(
        s.elf_sha256.data(), strnlen(s.elf_sha256.data(), s.elf_sha256.size())
    );
    const size_t n = std::min(a.size(), b.size());
    return n > 0 && a.substr(0, n) == b.substr(0, n);
}

// Returns what the core dump in flash says about the reset @p r. It returns nothing unless @p r is
// a reset that a dump may give more detail about, and the dump is complete, is not the one already
// taken, and is from this build. Two notes tell which dump was taken last, and a dump that either
// note names is not taken again:
// - The fault store's entries in flash. Each entry holds the CRC of the last dump taken, so this
//   note is kept across a power cycle, a clear of the faults and the ring wrapping.
// - RTC memory. The caller notes the dump's CRC there once the fault is recorded or suppressed as
//   a repeat. A suppressed repeat writes nothing to flash, so a dump taken only by a suppressed
//   repeat is noted only in RTC memory, and a later reset after RTC memory is lost may take it
//   again.
// Call it with s_flash held and with the fault store open. It is never inlined, so that its panic
// reason and summary are off the stack before the fault is recorded.
[[gnu::noinline]] std::optional<dump_fault> take_dump(idfxx::reset_reason r) {
    const auto& src = internal::reset_hooks();
    if (!dumps(r) || src.image_check() != ESP_OK) {
        return std::nullopt;
    }
    // The CRC covers the entire image, because two dumps of the same crash have the same summary
    // but not the same stacks.
    const auto crc = src.image_crc();
    if (!crc) {
        ESP_LOGW(TAG, "reading the core dump failed: %s", crc.error().message().c_str());
        return std::nullopt;
    }
    const auto& record = s_rtc.faults;
    if (record.has_core_dump_crc == 1 && record.core_dump_crc == *crc) {
        return std::nullopt;
    }
    if (s_faults->last_dump_crc() == *crc) {
        return std::nullopt;
    }
    internal::dump_summary s;
    if (!src.summary(s) || !same_build(s)) {
        return std::nullopt;
    }
    dump_fault d;
    d.crc = *crc;
    std::array<char, fault_detail_size> reason{};
    const size_t n = src.panic_reason(reason);
    if (n > 0) {
        const std::string_view kept = kept_detail({reason.data(), std::min(n, reason.size() - 1)});
        std::memcpy(d.detail.data(), kept.data(), kept.size());
        d.detail_size = kept.size();
    } else {
        // The detail is the exception's cause and address. For a panic, it also includes the PC,
        // which shows where the panic occurred. A watchdog's PC only shows where the code happened
        // to be when the watchdog fired.
        const bool xtensa = s.kind == internal::dump_summary::arch::xtensa;
        const int k = std::snprintf(
            d.detail.data(),
            d.detail.size(),
            xtensa ? "exccause=%lu excvaddr=0x%08lx" : "mcause=0x%08lx mtval=0x%08lx",
            static_cast<unsigned long>(xtensa ? s.exc_cause : s.mcause),
            static_cast<unsigned long>(xtensa ? s.exc_vaddr : s.mtval)
        );
        size_t size = printed(k, d.detail.size());
        if (r == idfxx::reset_reason::panic) {
            const int p = std::snprintf(
                d.detail.data() + size,
                d.detail.size() - size,
                " pc=0x%08lx",
                static_cast<unsigned long>(s.pc)
            );
            size += printed(p, d.detail.size() - size);
        }
        d.detail_size = size;
    }
    std::memcpy(d.task.data(), s.task.data(), d.task.size());
    d.task.back() = '\0';
    d.pc = s.pc;
    if (s.kind == internal::dump_summary::arch::xtensa) {
        d.frame_count = std::min<size_t>({s.depth, s.backtrace.size(), d.frames.size()});
        std::copy_n(s.backtrace.begin(), d.frame_count, d.frames.begin());
    } else {
        // RISC-V has no backtrace on the device, so the frames are the exception's PC and the
        // return address.
        d.frames[0] = s.pc;
        d.frames[1] = s.ra;
        d.frame_count = 2;
    }
    return d;
}

// Protected by s_lifecycle. The reset is taken once per boot.
bool s_reset_taken = false;

// Records the fault reset @p r as a fault of its kind, with its repeats suppressed and with the
// detail from a core dump. A recorded fault notes the dump as taken in the fault store. Once the
// fault is recorded or suppressed as a repeat, this also notes the dump as taken in RTC memory.
// Call it with s_flash held and with the fault store open.
void record_reset_fault(idfxx::reset_reason r) {
    const reset_name name = name_of(r);
    const auto dump = take_dump(r);
    const fault_place place = dump ? dump->place() : fault_place{};
    const std::string_view what = dump ? dump->detail_view() : std::string_view();
    auto done = record_unless_repeat_locked(name.view(), what, true, &place);
    if (!done) {
        // The dump isn't noted as taken in flash or in RTC memory, so the next boot takes it again.
        ESP_LOGW(
            TAG,
            "recording the %s reset as a fault failed: %s",
            name.text.data(),
            done.error().message().c_str()
        );
        return;
    }
    if (dump) {
        // The fault was recorded, or it repeats a fault that was recorded. Either way, the dump is
        // noted as taken in RTC memory. A repeat writes nothing to flash, so for a repeat this is
        // the only note.
        s_rtc.faults.has_core_dump_crc = 1;
        s_rtc.faults.core_dump_crc = dump->crc;
        internal::seal(s_rtc.faults);
    }
}

// The stack size of the task that records a fault reset. Recording a panic with a core dump into a
// full log took up to 5412 bytes of this stack in a debug build (-Og), and up to 3492 bytes at -Os,
// measured on esp32s3 in QEMU. This size leaves more than 1.5 KiB to spare.
constexpr size_t reset_task_stack = 7168;

// Takes the reset before this boot into account. Start calls it once per boot, guarded by
// s_reset_taken. A fault reset is recorded as a fault of its kind, with its repeats suppressed and
// with the detail from a core dump. A deep-sleep reset adds nothing. Any other reset adds an entry
// with its name, and lifts the suppression of the fault resets' kinds. Call it with s_lifecycle
// held and the log open, but without s_flash, because it takes s_flash itself.
[[gnu::noinline]] void take_reset() {
    const idfxx::reset_reason r = internal::reset_hooks().reason();
    if (r == idfxx::reset_reason::deep_sleep) {
        return;
    }
    if (is_fault_reset(r) && s_faults) {
        if (!scheduler_running()) {
            flash_lock lock;
            record_reset_fault(r);
            return;
        }
        // Recording takes more stack than start's caller may have to spare. So it runs on a
        // separate task at the caller's priority, and start waits for that task. While it waits,
        // start holds only s_lifecycle, which nothing in the task takes. The task takes s_flash
        // itself, as any other caller of the log does, so a flush or a read may take s_flash before
        // the task does. If memory for the task can't be allocated, task::make throws
        // std::bad_alloc, or aborts when exceptions are disabled. The exception passes out of
        // start, and start_undo stops the log.
        auto recorder = idfxx::task::make(
            idfxx::task::config{
                .name = "pl_reset",
                .stack_size = reset_task_stack,
                .priority = static_cast<unsigned>(uxTaskPriorityGet(nullptr)),
            },
            [r](idfxx::task::self&) {
                flash_lock lock;
                record_reset_fault(r);
            }
        );
        if (recorder) {
            (void)recorder->try_join();
            return;
        }
        // task::make returns an error only for a config that it rejects: a priority that is not
        // less than configMAX_PRIORITIES, a stack smaller than configMINIMAL_STACK_SIZE, or a core
        // that doesn't exist. The config above has none of these, so this branch isn't reached.
        // If it were, the fault wouldn't be recorded, as when there are no fault slots, and the
        // reset would only be logged by name below.
        ESP_LOGW(TAG, "reset fault not recorded: %s", recorder.error().message().c_str());
    }
    flash_lock lock;
    const reset_name name = name_of(r);
    keep_fault_line(now(), reset_head, name.view(), {});
    if (is_fault_reset(r)) {
        return;
    }
    bool forgot = false;
    for (const auto k : fault_resets) {
        forgot = s_rtc.faults.suppressed.forget(kind_crc(name_of(k).view())) || forgot;
    }
    if (forgot) {
        internal::seal(s_rtc.faults);
    }
}

// The functions that read ESP-IDF's reset reason and core dump.
idfxx::reset_reason esp_reason() {
    return static_cast<idfxx::reset_reason>(esp_reset_reason());
}

#if PL_CORE_DUMP
esp_err_t esp_image_check() {
    return esp_core_dump_image_check();
}

idfxx::result<uint32_t> esp_image_crc() {
    size_t addr = 0;
    size_t size = 0;
    if (const esp_err_t e = esp_core_dump_image_get(&addr, &size); e != ESP_OK) {
        return idfxx::error(e);
    }
    // Read through the partition, so that an encrypted partition is decrypted.
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr
    );
    if (part == nullptr || addr < part->address || size > part->size - (addr - part->address)) {
        return idfxx::error(idfxx::errc::not_found);
    }
    std::array<uint8_t, 256> piece;
    uint32_t crc = 0;
    for (size_t done = 0; done < size;) {
        const size_t n = std::min(piece.size(), size - done);
        if (const esp_err_t e =
                esp_partition_read(part, addr - part->address + done, piece.data(), n);
            e != ESP_OK) {
            return idfxx::error(e);
        }
        crc = esp_rom_crc32_le(crc, piece.data(), static_cast<uint32_t>(n));
        done += n;
    }
    return crc;
}

bool esp_summary(internal::dump_summary& out) {
    // The summary is large on RISC-V, which keeps a stack dump in it. So it is allocated on the
    // heap, once per boot.
    auto s = std::make_unique<esp_core_dump_summary_t>();
    if (esp_core_dump_get_summary(s.get()) != ESP_OK) {
        return false;
    }
    out = {};
    std::memcpy(out.task.data(), s->exc_task, std::min(out.task.size(), sizeof s->exc_task));
    out.task.back() = '\0';
    out.pc = s->exc_pc;
    std::memcpy(
        out.elf_sha256.data(),
        s->app_elf_sha256,
        std::min(out.elf_sha256.size(), sizeof s->app_elf_sha256)
    );
    out.elf_sha256.back() = '\0';
#if CONFIG_IDF_TARGET_ARCH_XTENSA
    out.kind = internal::dump_summary::arch::xtensa;
    out.depth = std::min<uint32_t>(s->exc_bt_info.depth, out.backtrace.size());
    std::copy_n(s->exc_bt_info.bt, out.depth, out.backtrace.begin());
    out.exc_cause = s->ex_info.exc_cause;
    out.exc_vaddr = s->ex_info.exc_vaddr;
#else
    out.kind = internal::dump_summary::arch::riscv;
    out.ra = s->ex_info.ra;
    out.mcause = s->ex_info.mcause;
    out.mtval = s->ex_info.mtval;
#endif
    return true;
}

size_t esp_panic_reason(std::span<char> out) {
    if (out.empty() || esp_core_dump_get_panic_reason(out.data(), out.size()) != ESP_OK) {
        return 0;
    }
    out.back() = '\0';
    return strnlen(out.data(), out.size());
}
#else
esp_err_t esp_image_check() {
    return ESP_ERR_NOT_SUPPORTED;
}

idfxx::result<uint32_t> esp_image_crc() {
    return idfxx::error(idfxx::errc::not_supported);
}

bool esp_summary(internal::dump_summary&) {
    return false;
}

size_t esp_panic_reason(std::span<char>) {
    return 0;
}
#endif

// Creates the writer task with @p w, and sets the threshold at which a kept line wakes it. Call it
// with s_lifecycle held. It is never inlined, so that the task's config and the task itself are
// not in start_with's frame.
[[gnu::noinline]] idfxx::result<void> start_writer(const writer_config& w) {
    s_threshold = w.threshold;
    const auto interval = w.interval;
    auto writer = idfxx::task::make(
        idfxx::task::config{
            .name = "persistent_log",
            .stack_size = w.stack_size,
            .priority = w.priority,
        },
        [interval](idfxx::task::self& self) {
            while (!self.stop_requested()) {
                (void)self.take_for(interval);
                if (self.stop_requested()) {
                    break;
                }
                flash_lock lock;
                (void)flush_locked();
            }
        }
    );
    if (!writer) {
        return idfxx::error(writer.error());
    }
    s_writer.emplace(std::move(*writer));
    s_writer_handle = s_writer->idf_handle();
    return {};
}

#ifdef CONFIG_IDFXX_STD_FORMAT
// Sets the capture filter from @p c, and adds the capture sink. Call it with s_lifecycle held. It
// is never inlined, so that the filter that it makes is not in start_with's frame.
[[gnu::noinline]] idfxx::result<void> start_capture(const capture_config& c) {
    s_filter = internal::level_filter(c.level, c.tag_levels);
#ifdef CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES
    return idfxx::log::try_add_sink(idfxx::log::all_lines, s_capture);
#else
    return idfxx::log::try_add_sink(s_capture);
#endif
}
#endif

// Starts the log with the store from @p cfg. It is never inlined, so that the stack frames of the
// functions it calls come and go one after another, rather than adding up in one frame.
[[gnu::noinline]] idfxx::result<void> start_with(config&& cfg) {
    std::lock_guard<std::mutex> lifecycle(s_lifecycle);
    if (s_started) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    if (auto valid = validate(cfg); !valid) {
        return valid;
    }
    start_undo undo;
    if (auto opened = open_flash(cfg); !opened) {
        return opened;
    }

    if (cfg.writer) {
        if (auto writer = start_writer(*cfg.writer); !writer) {
            return writer;
        }
    } else {
        s_threshold = std::numeric_limits<size_t>::max();
    }

#ifdef CONFIG_IDFXX_STD_FORMAT
    // This comes last, once everything that the sink reads is set, because the sink receives lines
    // from the moment it is added.
    if (cfg.capture) {
        if (auto added = start_capture(*cfg.capture); !added) {
            return added;
        }
    }
#endif

    s_started = true;
    s_running = true;

    // This runs once the log takes lines, so that the reset's entry is stamped like any other
    // entry. The reset is noted as taken only after take_reset() returns, so that a start that
    // throws leaves the reset for the next start.
    if (!s_reset_taken) {
        take_reset();
        s_reset_taken = true;
    }
    undo.disarm();
    return {};
}

} // namespace

internal::reset_source internal::esp_reset_source() noexcept {
    return {
        .reason = esp_reason,
        .image_check = esp_image_check,
        .image_crc = esp_image_crc,
        .summary = esp_summary,
        .panic_reason = esp_panic_reason,
    };
}

internal::reset_source& internal::reset_hooks() noexcept {
    static reset_source source = esp_reset_source();
    return source;
}

void internal::rearm_reset() noexcept {
    std::lock_guard<std::mutex> lifecycle(s_lifecycle);
    s_wake_counted = false;
    s_reset_taken = false;
}

internal::ring_type& internal::rtc_ring() noexcept {
    return s_ring;
}

internal::rtc_state& internal::rtc() noexcept {
    return s_rtc;
}

void internal::check_rtc(bool deep_sleep_wake) noexcept {
    (void)keep_or_reset(s_rtc.hint);
    (void)keep_or_reset(s_rtc.faults);
    const bool counted = keep_or_reset(s_rtc.wake);
    s_rtc.wake.wake = counted && deep_sleep_wake ? s_rtc.wake.wake + 1 : 0;
    internal::seal(s_rtc.wake);
}

// =============================================================================
// Public API
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
// It calls start_with directly, rather than through try_start, so that no second copy of the
// config is made in its frame.
void start(config cfg) {
    unwrap(start_with(std::move(cfg)));
}
#endif

idfxx::result<void> try_start(config cfg) {
    return start_with(std::move(cfg));
}

idfxx::result<void> try_stop() {
    std::lock_guard<std::mutex> lifecycle(s_lifecycle);
    if (!s_started) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    return shut_down(true);
}

void append(std::string_view text) noexcept {
    // Leading newlines would make only empty lines, so the entry's first line is the first line
    // that isn't empty.
    text.remove_prefix(std::min(text.find_first_not_of('\n'), text.size()));
    if (text.empty() || xPortInIsrContext()) {
        return;
    }
    busy_guard quiet;
    user u;
    if (!u) {
        return;
    }
    // The text is kept as one entry of at most max_line bytes, like a captured message, and is
    // shortened at the start of a UTF-8 character.
    char buffer[max_line];
    const std::string_view kept = detail::utf8_prefix(text, max_line);
    std::memcpy(buffer, kept.data(), kept.size());
    keep(buffer, kept.size());
}

idfxx::result<void> try_flush() {
    flash_lock lock;
    return flush_locked();
}

idfxx::result<void> try_flush_if_pending(size_t threshold) {
    flash_lock lock;
    if (!s_log) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    if (ring_unflushed() < threshold) {
        return {};
    }
    return flush_locked();
}

namespace {

// Passes on a reader's result, and keeps its error in `failed`. A reader that returned an error
// returns the same error from then on. A read that fails can lose the bytes that the call had
// already read, so the reader can't go on from where it was.
template<typename T>
idfxx::result<T> keep_error(idfxx::result<T> r, std::optional<std::error_code>& failed) {
    if (!r) {
        failed = r.error();
    }
    return r;
}

// Checks that a reader can read. It returns the error that the reader kept in `failed`, and then
// the error that it kept in the member that @p also names, if any. Otherwise it returns
// invalid_state when @p open is false, when @p s is null because the reader was moved from, or when
// the log stopped after the reader was created.
template<typename State>
idfxx::result<void>
check_reader(const State* s, bool open, std::optional<std::error_code> State::* also = nullptr) {
    if (s && s->failed) {
        return idfxx::error(*s->failed);
    }
    if (s && also && s->*also) {
        return idfxx::error(*(s->*also));
    }
    if (!open || !s || s->generation != s_generation) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    return {};
}

} // namespace

struct text_reader::state {
    flash_log::reader reader;
    uint32_t generation;
    // The error that the reader returned, if any.
    std::optional<std::error_code> failed = std::nullopt;
};

text_reader::text_reader(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

text_reader::~text_reader() = default;

text_reader::text_reader(text_reader&&) noexcept = default;

text_reader& text_reader::operator=(text_reader&&) noexcept = default;

idfxx::result<text_reader> try_read_text() {
    flash_lock lock;
    if (!s_log) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    // A failed flush logs its error. The reader then reads what is in flash, without the lines
    // that the flush could not write.
    (void)flush_locked();
    auto r = s_log->try_read();
    if (!r) {
        return idfxx::error(r.error());
    }
    return text_reader(
        std::make_unique<text_reader::state>(text_reader::state{std::move(*r), s_generation})
    );
}

idfxx::result<std::string_view> text_reader::try_next(std::span<char> out) {
    flash_lock lock;
    if (auto ok = check_reader(_state.get(), s_log.has_value()); !ok) {
        return idfxx::error(ok.error());
    }
    auto n = keep_error(_state->reader.try_next(out), _state->failed);
    if (!n) {
        return idfxx::error(n.error());
    }
    return std::string_view(out.data(), *n);
}

struct entry_reader::state {
    flash_log::reader reader;
    uint32_t generation;
    entry_buffer buffer;
    // The error that the reader returned, if any.
    std::optional<std::error_code> failed = std::nullopt;
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    // The entry that an iterator over the reader is at.
    entry current = {};
#endif
};

entry_reader::entry_reader(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

entry_reader::~entry_reader() = default;

entry_reader::entry_reader(entry_reader&&) noexcept = default;

entry_reader& entry_reader::operator=(entry_reader&&) noexcept = default;

idfxx::result<entry_reader> try_read() {
    entry_buffer buffer;
    flash_lock lock;
    if (!s_log) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    // A failed flush logs its error. The reader then reads what is in flash, without the lines
    // that the flush could not write.
    (void)flush_locked();
    auto r = s_log->try_read();
    if (!r) {
        return idfxx::error(r.error());
    }
    return entry_reader(
        std::make_unique<entry_reader::state>(
            entry_reader::state{std::move(*r), s_generation, std::move(buffer)}
        )
    );
}

idfxx::result<entry_reader> try_read_newest(size_t count) {
    // The reader's buffer holds the text that is read to find the entries.
    entry_buffer buffer;
    flash_lock lock;
    if (!s_log) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    // A failed flush logs its error. The reader then reads what is in flash, without the lines
    // that the flush could not write.
    (void)flush_locked();
    auto r = s_log->try_read_newest(count, max_entry_size, buffer.scratch());
    if (!r) {
        return idfxx::error(r.error());
    }
    return entry_reader(
        std::make_unique<entry_reader::state>(
            entry_reader::state{std::move(*r), s_generation, std::move(buffer)}
        )
    );
}

idfxx::result<bool> entry_reader::try_next(entry& out) {
    flash_lock lock;
    if (auto ok = check_reader(_state.get(), s_log.has_value()); !ok) {
        return idfxx::error(ok.error());
    }
    return keep_error(
        _state->buffer.next(
            out, [this](std::span<char> room) { return _state->reader.try_next(room); }
        ),
        _state->failed
    );
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
entry_reader::iterator entry_reader::begin() {
    return iterator(_advance() ? this : nullptr);
}

bool entry_reader::_advance() {
    if (!_state) {
        // The reader was moved from.
        throw std::system_error(idfxx::make_error_code(idfxx::errc::invalid_state));
    }
    return next(_state->current);
}

const entry& entry_reader::iterator::operator*() const noexcept {
    return _reader->_state->current;
}

entry_reader::iterator& entry_reader::iterator::operator++() {
    if (!_reader->_advance()) {
        _reader = nullptr;
    }
    return *this;
}
#endif

idfxx::result<size_t> internal::copy_newest_in_flash(std::span<char> out) {
    flash_lock lock;
    if (!s_log) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    return s_log->try_tail(out);
}

idfxx::result<std::string_view> try_copy_newest(std::span<char> out) {
    flash_lock lock;
    if (!s_log) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    // A failed flush logs its error. The copy then holds what is in flash, without the lines that
    // the flush could not write.
    (void)flush_locked();
    auto n = s_log->try_tail(out);
    if (!n) {
        return idfxx::error(n.error());
    }
    return std::string_view(out.data(), *n);
}

std::string_view copy_newest_in_memory(std::span<char> out) noexcept {
    size_t n = 0;
    portENTER_CRITICAL_SAFE(&s_ring_lock);
    if (s_ring.valid()) {
        const auto t = s_ring.tail(out.size());
        std::memcpy(out.data(), t.first.data(), t.first.size());
        std::memcpy(out.data() + t.first.size(), t.second.data(), t.second.size());
        n = t.size();
    }
    portEXIT_CRITICAL_SAFE(&s_ring_lock);
    return {out.data(), n};
}

idfxx::result<void> try_clear() {
    flash_lock lock;
    if (!s_log) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    // The ring's lines are cleared too, whether flushed or not, so that neither the log nor
    // copy_newest_in_memory() returns them. The ring is emptied before the flash is cleared, even
    // though a failed clear then loses its lines. Lines are kept without s_flash, so emptying the
    // ring afterward would drop the lines kept while the flash was erased.
    portENTER_CRITICAL_SAFE(&s_ring_lock);
    s_ring.reset();
    portEXIT_CRITICAL_SAFE(&s_ring_lock);
    auto cleared = s_log->try_clear();
    save_hint();
    return cleared;
}

idfxx::result<bool> try_record_fault(std::string_view kind, std::string_view detail) {
    return record_fault_checked(kind, detail, false);
}

idfxx::result<bool>
try_record_fault(std::string_view kind, std::string_view detail, suppress_repeats_t) {
    return record_fault_checked(kind, detail, true);
}

struct fault_reader::state {
    fault_store::reader reader;
    uint32_t generation;
    // The buffer for reading a context's entries. It is made on the first such read.
    std::optional<entry_buffer> context = std::nullopt;
    // The error that reading a fault's header returned, if any. A fault reader moves on to the
    // next slot before it reads the header, so after an error it can't read that fault again.
    std::optional<std::error_code> failed = std::nullopt;
    // The error that reading the current fault's context returned, if any. Reading the next
    // fault's header forgets it.
    std::optional<std::error_code> context_failed = std::nullopt;
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    // The fault that an iterator over the reader is at, and the entry that an iterator over its
    // context is at.
    fault current = {};
    entry current_context = {};
#endif
};

fault_reader::fault_reader(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

fault_reader::~fault_reader() = default;

fault_reader::fault_reader(fault_reader&&) noexcept = default;

fault_reader& fault_reader::operator=(fault_reader&&) noexcept = default;

idfxx::result<fault_reader> try_read_faults() {
    flash_lock lock;
    if (auto open = faults_open(); !open) {
        return idfxx::error(open.error());
    }
    return fault_reader(
        std::make_unique<fault_reader::state>(
            fault_reader::state{.reader = s_faults->read(), .generation = s_generation}
        )
    );
}

idfxx::result<bool> fault_reader::try_next(fault& out) {
    flash_lock lock;
    if (auto ok = check_reader(_state.get(), s_faults.has_value()); !ok) {
        return idfxx::error(ok.error());
    }
    // The text that the buffer holds, and an error in reading it, belong to the previous fault's
    // context.
    if (_state->context) {
        _state->context->reset();
    }
    _state->context_failed.reset();
    return keep_error(_state->reader.try_next(out), _state->failed);
}

idfxx::result<std::string_view> fault_reader::try_next_context_text(std::span<char> out) {
    flash_lock lock;
    if (auto ok = check_reader(_state.get(), s_faults.has_value(), &state::context_failed); !ok) {
        return idfxx::error(ok.error());
    }
    auto n = keep_error(_state->reader.try_next_context(out), _state->context_failed);
    if (!n) {
        return idfxx::error(n.error());
    }
    return std::string_view(out.data(), *n);
}

idfxx::result<bool> fault_reader::try_next_context(entry& out) {
    // The buffer is made before the lock is taken, and only when there is a context to read. The
    // reader's own state is not shared with other tasks, so it is read here without the lock.
    if (_state && !_state->context && _state->reader.in_context()) {
        _state->context.emplace();
    }
    flash_lock lock;
    if (auto ok = check_reader(_state.get(), s_faults.has_value(), &state::context_failed); !ok) {
        return idfxx::error(ok.error());
    }
    if (!_state->context) {
        // There is no context to read.
        return false;
    }
    return keep_error(
        _state->context->next(
            out, [this](std::span<char> room) { return _state->reader.try_next_context(room); }
        ),
        _state->context_failed
    );
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
fault_reader::iterator fault_reader::begin() {
    return iterator(_advance() ? this : nullptr);
}

fault_reader::context_range::iterator fault_reader::context_range::begin() {
    return iterator(_reader->_advance_context() ? _reader : nullptr);
}

bool fault_reader::_advance() {
    if (!_state) {
        // The reader was moved from.
        throw std::system_error(idfxx::make_error_code(idfxx::errc::invalid_state));
    }
    return next(_state->current);
}

bool fault_reader::_advance_context() {
    if (!_state) {
        // The reader was moved from.
        throw std::system_error(idfxx::make_error_code(idfxx::errc::invalid_state));
    }
    return next_context(_state->current_context);
}

const fault& fault_reader::iterator::operator*() const noexcept {
    return _reader->_state->current;
}

fault_reader::iterator& fault_reader::iterator::operator++() {
    if (!_reader->_advance()) {
        _reader = nullptr;
    }
    return *this;
}

const entry& fault_reader::context_range::iterator::operator*() const noexcept {
    return _reader->_state->current_context;
}

fault_reader::context_range::iterator& fault_reader::context_range::iterator::operator++() {
    if (!_reader->_advance_context()) {
        _reader = nullptr;
    }
    return *this;
}
#endif

void lift_suppression(std::string_view kind) noexcept {
    if (xPortInIsrContext() || !s_running.load()) {
        return;
    }
    // This checks first without the lock, because the check reads only a word per slot, and most
    // kinds are not remembered.
    const uint32_t kc = kind_crc(kind);
    if (!s_rtc.faults.suppressed.holds(kc)) {
        return;
    }
    flash_lock lock;
    if (!s_log || !s_rtc.faults.suppressed.forget(kc)) {
        return;
    }
    internal::seal(s_rtc.faults);
    keep_fault_line(now(), lifted_head, kind, {});
}

idfxx::result<void> try_clear_faults() {
    flash_lock lock;
    if (auto open = faults_open(); !open) {
        return open;
    }
    // The suppressed kinds are forgotten before the slots are cleared. A clear that fails in the
    // middle may have erased the record of a remembered fault. Forgetting first leaves no kind
    // remembered, so each fault is recorded again.
    s_rtc.faults.suppressed.clear();
    internal::seal(s_rtc.faults);
    return s_faults->try_clear();
}

idfxx::result<size_t> try_fault_count() {
    flash_lock lock;
    if (auto open = faults_open(); !open) {
        return idfxx::error(open.error());
    }
    return s_faults->try_count();
}

} // namespace idfxx::persistent_log
