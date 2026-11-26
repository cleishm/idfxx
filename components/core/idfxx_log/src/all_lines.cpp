// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Captures every ESP-IDF log line for the sinks added with all_lines. esp_log and esp_log_va are
// wrapped at link time (-Wl,--wrap). Each call is passed on to ESP-IDF's log, and then captured for
// the sinks. The lines that idfxx::log passes to ESP-IDF's log are skipped here, because log()
// passes them to the sinks itself.

#include "line_text.hpp"
#include "sinks.hpp"

#include <idfxx/log>

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_attr.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <limits>
#include <string_view>

namespace idfxx::log::detail {

namespace {

constexpr size_t max_line = CONFIG_IDFXX_LOG_SINK_MAX_LINE;
constexpr size_t join_slots = CONFIG_IDFXX_LOG_JOIN_SLOTS;
// The longest tag kept with a line logged in several calls.
constexpr size_t max_tag = max_line / 2;

static_assert(max_line <= std::numeric_limits<uint16_t>::max());

// =============================================================================
// Tasks
// =============================================================================

// Both are kept to this file because, on RISC-V, a thread_local that another file refers to is
// accessed through the global offset table, which ESP-IDF's link discards.
//
// The calling task's id, which is 0 until its first use. Ids aren't reused, so a task that is
// created later at a deleted task's handle doesn't continue the deleted task's line.
constinit thread_local uint32_t t_task_id = 0;
// Whether the calling task may hold a join slot. It is set when the task stores its line. It is
// cleared when the task frees or takes its slot, or finds that its slot is gone. It is still set
// after the task's slot is evicted or freed by release_all_join_slots(), which means that the task
// searches for its slot one more time. It is never clear while the task holds a slot.
constinit thread_local bool t_may_hold_slot = false;

constinit std::atomic<uint32_t> s_last_task_id{0};

uint32_t current_task() noexcept {
    if (t_task_id == 0) {
        uint32_t id = 0;
        while (id == 0) {
            id = s_last_task_id.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        t_task_id = id;
    }
    return t_task_id;
}

// =============================================================================
// Join slots
// =============================================================================

// What is kept of an unfinished line.
enum class line_state : uint8_t {
    // Its text, joined across its calls.
    joining,
    // Its text, shortened to max_line. Later calls are only checked for whether they end the line.
    full,
    // Nothing, because no sink wanted the line at its start. Later calls are only checked for
    // whether they end the line.
    dropping,
};

// An unfinished line, except for its text.
struct line_header {
    // The level of the line's first call. Sinks receive the line at this level.
    level lvl;
    line_state state;
    // `tag_size` is the size of the first call's tag, which is stored at the start of the text.
    // `size` is the size of the text, which holds the tag followed by the line's message.
    uint16_t tag_size;
    uint16_t size;
};

// A task's unfinished line. The line is joined across the calls that it is logged in until its
// newline arrives, or it is dropped until then. All of a slot is protected by s_join_lock. The
// owner copies the line out of the slot and back in, so that a slot that is evicted between the
// two copies is never written by the task that it was taken from.
struct join_slot {
    // The owner's id, or 0 when the slot is free.
    uint32_t owner;
    // When the owner last used the slot, as a value of s_join_clock. When every slot is in use, the
    // least recently used slot is evicted, so that the slot of a deleted task is eventually
    // reclaimed.
    uint32_t used;
    line_header head;
    char text[max_line];
};

portMUX_TYPE s_join_lock = portMUX_INITIALIZER_UNLOCKED;
join_slot s_slots[join_slots];
uint32_t s_join_clock = 0; // under s_join_lock

// What a task had stored of its line, as copied out of its slot.
struct partial {
    // Whether the task held a slot, which means that its last call didn't end its line.
    bool held = false;
    line_header head{};
};

// Returns the slot that @p me holds, or null. Call it with s_join_lock held.
join_slot* owned_by(uint32_t me) noexcept {
    for (auto& s : s_slots) {
        if (s.owner == me) {
            return &s;
        }
    }
    return nullptr;
}

// Copies out what the calling task has stored of its line. The line's text is copied into @p out
// (at least max_line bytes), unless the line is full or being dropped. In those states, only the
// line's end is still to come.
partial load_slot(char* out) noexcept {
    partial p;
    if (!t_may_hold_slot) {
        return p;
    }
    const uint32_t me = current_task();
    portENTER_CRITICAL(&s_join_lock);
    if (join_slot* s = owned_by(me)) {
        s->used = ++s_join_clock;
        p = {.held = true, .head = s->head};
        if (s->head.state == line_state::joining) {
            std::memcpy(out, s->text, s->head.size);
        }
    }
    portEXIT_CRITICAL(&s_join_lock);
    t_may_hold_slot = p.held;
    return p;
}

// Copies out what the calling task has stored of its line, and frees its slot. The line's text is
// copied into @p out (at least max_line bytes).
partial take_slot(char* out) noexcept {
    partial p;
    if (!t_may_hold_slot) {
        return p;
    }
    const uint32_t me = current_task();
    portENTER_CRITICAL(&s_join_lock);
    if (join_slot* s = owned_by(me)) {
        p = {.held = true, .head = s->head};
        std::memcpy(out, s->text, s->head.size);
        s->owner = 0;
    }
    portEXIT_CRITICAL(&s_join_lock);
    t_may_hold_slot = false;
    return p;
}

// Stores the calling task's line so far, which is the @p head.size bytes of @p text. The line is
// stored in the slot that the task holds. If the task holds no slot, the line is stored in a free
// slot. If every slot is in use, the line is stored in the least recently used slot, and that
// slot's unfinished line is discarded.
void store_slot(const line_header& head, const char* text) noexcept {
    const uint32_t me = current_task();
    portENTER_CRITICAL(&s_join_lock);
    join_slot* found = t_may_hold_slot ? owned_by(me) : nullptr;
    if (found == nullptr) {
        for (auto& s : s_slots) {
            if (s.owner == 0) {
                found = &s;
                break;
            }
            if (found == nullptr || static_cast<int32_t>(s.used - found->used) < 0) {
                found = &s;
            }
        }
        found->owner = me;
    }
    found->used = ++s_join_clock;
    found->head = head;
    std::memcpy(found->text, text, head.size);
    portEXIT_CRITICAL(&s_join_lock);
    t_may_hold_slot = true;
}

// Frees the slot that the calling task holds, if it still holds one.
void release_slot() noexcept {
    if (!t_may_hold_slot) {
        return;
    }
    const uint32_t me = current_task();
    portENTER_CRITICAL(&s_join_lock);
    if (join_slot* s = owned_by(me)) {
        s->owner = 0;
    }
    portEXIT_CRITICAL(&s_join_lock);
    t_may_hold_slot = false;
}

// =============================================================================
// Capture
// =============================================================================

// The tag as it appears in a caller's prefix. A missing tag is printed as "(null)".
std::string_view printed_tag(const char* tag) noexcept {
    return tag != nullptr ? std::string_view{tag} : std::string_view{"(null)"};
}

// Whether a call ends its line, determined without formatting the call. It returns true if the
// call's format ends in a newline, or if the format is "%s" and its string argument ends in a
// newline. A call for which this returns false may still end its line.
bool format_ends_line(std::string_view format, va_list* args) noexcept {
    if (format.ends_with('\n')) {
        return true;
    }
    if (format != "%s") {
        return false;
    }
    va_list peek;
    va_copy(peek, *args);
    const char* s = va_arg(peek, const char*);
    va_end(peek);
    return s != nullptr && std::string_view{s}.ends_with('\n');
}

// A call formatted into a line's text.
struct appended {
    // The size of the text, including the call.
    size_t size;
    // The call was shortened to fit max_line.
    bool full;
    // The call ends its line.
    bool ends;
};

// Formats a call into @p out (at least max_line + 1 bytes) at offset @p n, and shortens the text
// to max_line at a UTF-8 character boundary. Whether the call ends its line is determined from its
// last byte. If the call was shortened, it is determined from @p format_ends instead, which is the
// call's format_ends_line() result.
appended
append_call(char* out, size_t n, const char* fmt, va_list* args, bool format_ends) noexcept {
    const size_t room = max_line - n;
    const int r = std::vsnprintf(out + n, room + 1, fmt, *args);
    const size_t got = r > 0 ? static_cast<size_t>(r) : 0;
    if (got > room) {
        return {
            .size = utf8_prefix({out, max_line}, max_line).size(),
            .full = true,
            .ends = format_ends,
        };
    }
    return {.size = n + got, .full = false, .ends = got > 0 && out[n + got - 1] == '\n'};
}

// Whether a call ends its line, for a line that keeps none of the call's text (because the line is
// being dropped, or was already shortened). The call is formatted into @p scratch (at least
// max_line + 1 bytes) and then discarded, but only when its format doesn't show whether it ends
// the line (@p format_ends is false).
bool call_ends_line(char* scratch, const char* fmt, va_list* args, bool format_ends) noexcept {
    return format_ends || append_call(scratch, 0, fmt, args, false).ends;
}

// Whether a sink added with all_lines wants a line. It is always inlined, because as a separate
// function it would only add a stack frame.
[[gnu::always_inline]] inline bool wanted(level lvl, std::string_view tag) noexcept {
    return wanted_by(offered_to::all_lines_sinks, lvl, tag);
}

// Passes a line to each sink added with all_lines that wants it.
void deliver_line(level lvl, std::string_view tag, std::string_view message) noexcept {
    deliver(offered_to::all_lines_sinks, lvl, tag, message);
}

// Passes a line that was logged in several calls to the sinks. The joined line is the @p n bytes
// of @p line: the first call's tag, then the message. The sinks receive the message without the
// newline and color reset that end it.
void deliver_joined(const line_header& head, const char* line, size_t n) noexcept {
    deliver_line(
        head.lvl, {line, head.tag_size}, without_line_end({line + head.tag_size, n - head.tag_size})
    );
}

// Captures one call to esp_log or esp_log_va, after the call has been passed on. @p whole is true
// for a call to esp_log. It is never inlined into the wrapper, so that its line buffer is on the
// stack only while it runs.
[[gnu::noinline]] void capture(
    esp_log_config_t config,
    const char* tag,
    const char* fmt,
    va_list* args,
    bool whole
) noexcept {
    if (config.opts.binary_mode) {
        return;
    }
    // This doesn't enter the sinks for idfxx::log's own lines or for the lines that a sink logs.
    const sink_entry entry = enter_sinks(unconstrained);
    if (!entry) {
        return;
    }
    const auto lvl = static_cast<level>(config.opts.log_level);
    const std::string_view tag_text = tag_view(tag);

    // Holds the line so far, followed by this call.
    char line[max_line + 1];

    if (whole || config.opts.require_formatting) {
        // A line logged in one call: a call to esp_log (ESP_LOGx, ESP_LOG_BUFFER_*), or log v2's
        // own line. It replaces any line that the task left unfinished.
        release_slot();
        if (!wanted(lvl, tag_text)) {
            return;
        }
        // Whether the call ends its line doesn't matter, because the call is a complete line.
        const size_t n = append_call(line, 0, fmt, args, true).size;
        std::string_view message{line, n};
        if (!config.opts.require_formatting) {
            // The caller formatted the complete line, including its prefix. Everything up to the
            // first "<tag>:" is the prefix, and it is removed, as is the newline that ends the
            // line. Log v2 adds the prefix and newline itself, so its format is the message's
            // format, and the sinks receive the message as formatted.
            message = without_line_end(message.substr(message_start(message, printed_tag(tag))));
        }
        deliver_line(lvl, tag_text, message);
        return;
    }

    // A call to esp_log_write. It may start a line, continue the line that the task left
    // unfinished, or end that line.
    const bool format_ends = format_ends_line(fmt, args);
    if (const partial p = load_slot(line); p.held) {
        // This call continues the task's line. The line keeps its first call's level and tag,
        // which are stored in the slot. This call's level and tag are not the line's.
        if (p.head.state != line_state::joining) {
            // None of this call's text is kept. The call is formatted over the line in the buffer.
            // If the call ends the line, the line is copied out of the slot again, and the sinks
            // receive it unless it is being dropped.
            if (call_ends_line(line, fmt, args, format_ends)) {
                if (const partial kept = take_slot(line);
                    kept.held && kept.head.state == line_state::full) {
                    deliver_joined(kept.head, line, kept.head.size);
                }
            }
            return;
        }
        const appended a = append_call(line, p.head.size, fmt, args, format_ends);
        if (a.ends) {
            release_slot();
            deliver_joined(p.head, line, a.size);
            return;
        }
        store_slot(
            {
                .lvl = p.head.lvl,
                .state = a.full ? line_state::full : line_state::joining,
                .tag_size = p.head.tag_size,
                .size = static_cast<uint16_t>(a.size),
            },
            line
        );
        return;
    }

    // This call starts a line, so the line's level and tag are this call's.
    if (!wanted(lvl, tag_text)) {
        // No sink wants the line, so it is dropped, along with the later calls until its end.
        if (!call_ends_line(line, fmt, args, format_ends)) {
            store_slot({.lvl = lvl, .state = line_state::dropping, .tag_size = 0, .size = 0}, line);
        }
        return;
    }
    // Everything up to the first "<tag>:" is the caller's prefix, and it is removed. The line is
    // shortened to max_line as formatted, including the prefix, at a UTF-8 character boundary.
    const appended a = append_call(line, 0, fmt, args, format_ends);
    const size_t from = message_start({line, a.size}, printed_tag(tag));
    if (a.ends) {
        deliver_line(lvl, tag_text, without_line_end({line + from, a.size - from}));
        return;
    }
    // The line continues in later calls, whose tag pointers may differ from this one. So a copy of
    // the tag, shortened to max_tag, is stored before the message, and the tag and the message
    // together fit in max_line.
    const size_t tag_size = utf8_prefix(tag_text, max_tag).size();
    size_t n = a.size - from;
    bool full = a.full;
    if (tag_size + n > max_line) {
        n = utf8_prefix({line + from, n}, max_line - tag_size).size();
        full = true;
    }
    std::memmove(line + tag_size, line + from, n);
    std::memcpy(line, tag_text.data(), tag_size);
    store_slot(
        {
            .lvl = lvl,
            .state = full ? line_state::full : line_state::joining,
            .tag_size = static_cast<uint16_t>(tag_size),
            .size = static_cast<uint16_t>(tag_size + n),
        },
        line
    );
}

} // namespace

void release_all_join_slots() noexcept {
    portENTER_CRITICAL(&s_join_lock);
    for (auto& s : s_slots) {
        s.owner = 0;
    }
    portEXIT_CRITICAL(&s_join_lock);
}

} // namespace idfxx::log::detail

// The log component's own check for a constrained context (an ISR, the flash cache disabled, or the
// scheduler not running). ESP-IDF's log applies this check to each line. It is in a private header.
#include <esp_private/log_util.h>

// The wrappers are in IRAM with esp_log, because they are called wherever esp_log is called,
// including from ISRs and while the flash cache is disabled.
#if CONFIG_LOG_IN_IRAM
#define IDFXX_LOG_WRAP_ATTR IRAM_ATTR
#else
#define IDFXX_LOG_WRAP_ATTR
#endif

extern "C" {

void __real_esp_log_va(esp_log_config_t config, const char* tag, const char* format, va_list args);
void __wrap_esp_log_va(esp_log_config_t config, const char* tag, const char* format, va_list args);
void __wrap_esp_log(esp_log_config_t config, const char* tag, const char* format, ...);

} // extern "C"

namespace {

// The two halves of a wrapper. They are always inlined, so that their code is in each wrapper, in
// IRAM with it, and has no stack frame of its own. Each wrapper copies the call's arguments itself
// between the two halves, because a function that ends a va_list can't be inlined.

// When no sink is added with all_lines (or the call has no format), passes the call on to
// esp_log_va and returns true. This load is the only work done before the call is passed on,
// because the wrapper runs from the first line logged at boot, before constructors run, in ISRs,
// and while the flash cache is disabled.
[[gnu::always_inline]] inline bool
passed_on_alone(esp_log_config_t config, const char* tag, const char* format, va_list args) {
    if (idfxx::log::detail::added(idfxx::log::detail::kind::all_lines)
                .load(std::memory_order_relaxed) != 0 &&
        format != nullptr) {
        return false;
    }
    __real_esp_log_va(config, tag, format, args);
    return true;
}

// Passes a call on to esp_log_va, then captures it from @p mine, which is a copy of the call's
// arguments made before the call was passed on.
[[gnu::always_inline]] inline void pass_on_and_capture(
    esp_log_config_t config,
    const char* tag,
    const char* format,
    va_list args,
    va_list* mine,
    bool whole
) {
    __real_esp_log_va(config, tag, format, args);
    // This runs only after esp_log_va has returned, so that capture's stack frame and the
    // console's are never on the stack at the same time. It also runs only where ESP-IDF's log
    // would format the line itself.
    if (!config.opts.constrained_env && !esp_log_util_is_constrained()) {
        idfxx::log::detail::capture(config, tag, format, mine, whole);
    }
}

} // namespace

extern "C" {

// Every esp_log_va call from outside ESP-IDF's log (esp_log_write, esp_log_writev) comes here.
IDFXX_LOG_WRAP_ATTR void
__wrap_esp_log_va(esp_log_config_t config, const char* tag, const char* format, va_list args) {
    if (passed_on_alone(config, tag, format, args)) {
        return;
    }
    va_list mine;
    va_copy(mine, args);
    pass_on_and_capture(config, tag, format, args, &mine, false);
    va_end(mine);
}

// Every esp_log call (ESP_LOGx, ESP_LOG_BUFFER_*) comes here. esp_log is esp_log_va with its
// arguments collected into a list, and each call is a complete line.
IDFXX_LOG_WRAP_ATTR void
__wrap_esp_log(esp_log_config_t config, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (!passed_on_alone(config, tag, format, args)) {
        va_list mine;
        va_copy(mine, args);
        pass_on_and_capture(config, tag, format, args, &mine, true);
        va_end(mine);
    }
    va_end(args);
}

} // extern "C"
