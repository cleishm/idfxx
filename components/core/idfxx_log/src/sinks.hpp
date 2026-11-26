// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// The sink registry. log.cpp uses it for idfxx::log's own lines. With CONFIG_IDFXX_LOG_ALL_LINES,
// all_lines.cpp also uses it for the wrapper that captures every log line.

#include "sdkconfig.h"

#include <idfxx/log>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace idfxx::log::detail {

// The kinds of sink, by how they were added.
enum class kind : uint8_t {
    // add_sink(s): receives idfxx::log's lines.
    idfxx_only,
    // add_sink(all_lines, s): receives idfxx::log's lines and, through the wrapper, every other
    // line.
    all_lines,
};

// The number of sinks added, for each kind. A count changes only while the registry's mutex is
// held, after a sink is published and after a sink is cleared. So a reader that sees 0 has no sink
// of that kind to pass a line to, and the fast paths only load these counts.
extern constinit std::atomic<unsigned> sinks_added[2];

// The count of sinks of kind @p k. It is always inlined, because the wrapper reads it from IRAM,
// where the wrapper may not call into flash.
[[nodiscard, gnu::always_inline]] inline std::atomic<unsigned>& added(kind k) noexcept {
    return sinks_added[static_cast<size_t>(k)];
}

// Whether a sink of either kind is added.
[[nodiscard, gnu::always_inline]] inline bool any_sink_added() noexcept {
    return added(kind::idfxx_only).load(std::memory_order_relaxed) != 0 ||
        added(kind::all_lines).load(std::memory_order_relaxed) != 0;
}

// The type of `unconstrained`. It selects the enter_sinks() overload for a caller that has already
// checked that the context is not constrained.
struct unconstrained_t {
    explicit unconstrained_t() = default;
};
inline constexpr unconstrained_t unconstrained{};

// Marks the calling task as inside the sinks for the entry's scope. The lines that the task logs
// while it is inside the sinks are not passed to any sink. These are a sink's own diagnostics, or
// the console's call for an idfxx::log line. So a sink's own lines are never passed back to it,
// and no line is passed to a sink twice. Where no sink may be called, enter_sinks() returns an
// entry that tests false and marks nothing.
class [[nodiscard]] sink_entry {
public:
    sink_entry(const sink_entry&) = delete;
    sink_entry& operator=(const sink_entry&) = delete;
    ~sink_entry() {
        if (_entered) {
            leave();
        }
    }

    // Returns an entry that isn't entered, for a caller that has no sink to enter for.
    [[nodiscard]] static sink_entry none() noexcept { return sink_entry{false}; }

    explicit operator bool() const noexcept { return _entered; }

private:
    friend sink_entry enter_sinks() noexcept;
    friend sink_entry enter_sinks(unconstrained_t) noexcept;

    explicit sink_entry(bool entered) noexcept
        : _entered(entered) {}

    // Marks the task as outside the sinks again.
    static void leave() noexcept;

    bool _entered;
};

// Enters the sinks, unless a line logged here can't be passed to them. A line can't be passed to
// the sinks in an interrupt, while the flash cache is disabled, or while the scheduler is not
// running. ESP-IDF's log doesn't format a line itself in those contexts either. A line also can't
// be passed to the sinks while the task is already inside them.
[[nodiscard]] sink_entry enter_sinks() noexcept;

// Enters the sinks, unless the task is already inside them. Call it only where the context isn't
// constrained (see enter_sinks()), because it reads a flag that belongs to the calling task.
[[nodiscard]] sink_entry enter_sinks(unconstrained_t) noexcept;

// The sinks that a line is offered to.
enum class offered_to : bool {
    // Every sink, for idfxx::log's own lines.
    every_sink,
    // The sinks added with all_lines, for the lines that the wrapper captures.
    all_lines_sinks,
};

// Whether any sink that the line is offered to wants it. Call it inside an entry.
[[nodiscard]] bool wanted_by(offered_to to, level lvl, std::string_view tag) noexcept;

// Passes a line to each sink that the line is offered to and that wants it. Call it inside an
// entry.
void deliver(offered_to to, level lvl, std::string_view tag, std::string_view message) noexcept;

#ifdef CONFIG_IDFXX_LOG_ALL_LINES
// Frees every join slot, discarding the unfinished lines in them. It is called with the registry's
// mutex held, when the first sink is added with all_lines. So a line that a task left unfinished
// when the last such sink was removed isn't continued.
void release_all_join_slots() noexcept;
#endif

} // namespace idfxx::log::detail
