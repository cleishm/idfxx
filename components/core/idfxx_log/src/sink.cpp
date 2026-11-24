// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "line_text.hpp"
#include "sinks.hpp"

#include <idfxx/error>
#include <idfxx/log>
#include <idfxx/sched>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>

// The log component's own check for a constrained context (an ISR, the flash cache disabled, or the
// scheduler not running). ESP-IDF's log applies this check to each line. It is in a private header.
#include <esp_private/log_util.h>

namespace idfxx::log::detail {

constinit std::atomic<unsigned> sinks_added[2]{};

// =============================================================================
// Entering the sinks
// =============================================================================

namespace {

// Whether the calling task is inside the sinks. It is kept to this file because, on RISC-V, a
// thread_local that another file refers to is accessed through the global offset table, which
// ESP-IDF's link discards.
constinit thread_local bool t_busy = false;

} // namespace

void sink_entry::leave() noexcept {
    t_busy = false;
}

sink_entry enter_sinks() noexcept {
    // This is checked first, because before the scheduler runs, there is no task whose flag can be
    // read.
    if (esp_log_util_is_constrained()) {
        return sink_entry{false};
    }
    return enter_sinks(unconstrained);
}

sink_entry enter_sinks(unconstrained_t) noexcept {
    if (t_busy) {
        return sink_entry{false};
    }
    t_busy = true;
    return sink_entry{true};
}

// =============================================================================
// Registry
// =============================================================================

namespace {

// A place for one sink. A caller counts itself in `users` while it loads `sink` and uses the sink
// that it loaded. Removal clears `sink`, then waits for `users` to be 0. So removal never returns
// while a call into the sink that it cleared is in progress, and it never waits for another slot's
// sink. Both sides use sequentially consistent operations, so either the removal sees the caller
// counted, or the caller sees the slot cleared. Before counting itself in, a caller uses a relaxed
// load to skip a slot that appears to hold no sink for it.
struct slot {
    std::atomic<idfxx::log::sink*> sink{nullptr};
    std::atomic<uint32_t> users{0};
    // How the sink was added. It is written before `sink` is published, and read after `sink` is
    // loaded.
    std::atomic<kind> added_as{kind::idfxx_only};
    // Protected by the registry's mutex. `draining` is the sink that was cleared from the slot
    // while calls into it may still be in progress. `removals` counts the removals of that sink
    // that are waiting for those calls. The slot isn't reused until the last removal is done.
    idfxx::log::sink* draining = nullptr;
    unsigned removals = 0;
};

// Serializes adding and removing sinks. It is not held while a removal waits, so that a slow sink
// delays only its own removals. It is created on first use, so that a sink can be added from a
// static constructor.
std::mutex& registry_mutex() noexcept {
    static std::mutex m;
    return m;
}

constinit slot s_slots[CONFIG_IDFXX_LOG_MAX_SINKS];

// Whether a line is offered to the slot's sink, based on how the sink was added. Call it after
// loading `sink`.
bool offered(const slot& sl, offered_to to) noexcept {
    return to == offered_to::every_sink ||
        sl.added_as.load(std::memory_order_relaxed) == kind::all_lines;
}

// Whether, on a relaxed load, the slot appears to hold no sink that the line is offered to. This is
// only a hint. The caller checks again after counting itself in.
bool skips(const slot& sl, offered_to to) noexcept {
    return sl.sink.load(std::memory_order_relaxed) == nullptr || !offered(sl, to);
}

idfxx::result<void> add(idfxx::log::sink& s, kind k) {
    std::lock_guard lock(registry_mutex());
    slot* free = nullptr;
    for (auto& sl : s_slots) {
        idfxx::log::sink* p = sl.sink.load();
        if (p == &s) {
            return idfxx::error(idfxx::errc::invalid_arg);
        }
        if (p == nullptr && sl.draining == nullptr && free == nullptr) {
            free = &sl;
        }
    }
    if (free == nullptr) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
#ifdef CONFIG_IDFXX_LOG_ALL_LINES
    if (k == kind::all_lines && added(kind::all_lines).load() == 0) {
        // A task may have left a line unfinished before the last sink added with all_lines was
        // removed. A call that was already past the wrapper's check may have stored that line.
        // Freeing the join slots ensures that such a line isn't continued.
        release_all_join_slots();
    }
#endif
    free->added_as.store(k, std::memory_order_relaxed);
    free->sink.store(&s);
    added(k).fetch_add(1);
    return {};
}

// Starts removing @p s. If @p s is in a slot, this clears the slot. If the slot is already cleared
// and another removal is still waiting for the calls into @p s, this joins that wait. Returns the
// slot to wait on, or null if @p s isn't added.
slot* start_removal(idfxx::log::sink& s) noexcept {
    std::lock_guard lock(registry_mutex());
    for (auto& sl : s_slots) {
        if (sl.sink.load() != &s) {
            continue;
        }
        sl.sink.store(nullptr);
        sl.draining = &s;
        sl.removals = 1;
        added(sl.added_as.load(std::memory_order_relaxed)).fetch_sub(1);
        return &sl;
    }
    for (auto& sl : s_slots) {
        if (sl.draining == &s) {
            ++sl.removals;
            return &sl;
        }
    }
    return nullptr;
}

// Ends a removal once no call into its sink is in progress. The last removal of the sink frees the
// slot.
void finish_removal(slot& sl) noexcept {
    std::lock_guard lock(registry_mutex());
    if (--sl.removals == 0) {
        sl.draining = nullptr;
    }
}

} // namespace

bool wanted_by(offered_to to, level lvl, std::string_view tag) noexcept {
    for (auto& sl : s_slots) {
        if (skips(sl, to)) {
            continue;
        }
        sl.users.fetch_add(1);
        idfxx::log::sink* p = sl.sink.load();
        const bool wanted = p != nullptr && offered(sl, to) && p->wants(lvl, tag);
        sl.users.fetch_sub(1);
        if (wanted) {
            return true;
        }
    }
    return false;
}

void deliver(offered_to to, level lvl, std::string_view tag, std::string_view message) noexcept {
    for (auto& sl : s_slots) {
        if (skips(sl, to)) {
            continue;
        }
        sl.users.fetch_add(1);
        if (idfxx::log::sink* p = sl.sink.load();
            p != nullptr && offered(sl, to) && p->wants(lvl, tag)) {
            p->write(lvl, tag, message);
        }
        sl.users.fetch_sub(1);
    }
}

bool wanted_by_sink(level lvl, const char* tag) noexcept {
    if (!any_sink_added()) {
        return false;
    }
    const sink_entry entry = enter_sinks();
    return entry && wanted_by(offered_to::every_sink, lvl, tag_view(tag));
}

} // namespace idfxx::log::detail

namespace idfxx::log {

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void add_sink(sink& s) {
    idfxx::unwrap(try_add_sink(s));
}
#endif

idfxx::result<void> try_add_sink(sink& s) {
    return detail::add(s, detail::kind::idfxx_only);
}

#ifdef CONFIG_IDFXX_LOG_ALL_LINES
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void add_sink(all_lines_t, sink& s) {
    idfxx::unwrap(try_add_sink(all_lines, s));
}
#endif

idfxx::result<void> try_add_sink(all_lines_t, sink& s) {
    return detail::add(s, detail::kind::all_lines);
}
#endif

void remove_sink(sink& s) noexcept {
    detail::slot* sl = detail::start_removal(s);
    if (sl == nullptr) {
        return;
    }
    // Callers count themselves in only inside an entry, which is possible only once the scheduler
    // runs. So before the scheduler runs, this loop never waits.
    while (sl->users.load() != 0) {
        idfxx::delay(idfxx::next_tick);
    }
    detail::finish_removal(*sl);
}

} // namespace idfxx::log
