# idfxx_persistent_log

A log that keeps ESP-IDF's warnings and errors, and the app's own lines, across resets and power
cuts. It also stores the faults that the app records, separately from the log, each with the lines
that led up to it.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__persistent__log.html)**

The log stores every line that it receives. When the log is full, new lines replace the oldest
ones. Faults are stored separately from the log. `record_fault()` writes a fault to a slot of its
own, together with a copy of the log's newest lines. `start()` does the same for a reset caused by
a fault, such as a panic or a watchdog reset. The lines that led up to a fault therefore survive
after new lines have replaced them in the log. There are only a few slots (4 by default). Once
every slot is in use, a new fault replaces the oldest fault; new log lines never replace a fault.
A fault that keeps happening can be recorded with the `suppress_repeats` tag, so that its repeats
don't fill the slots.

## Features

- **Captures log lines** at or above a capture level that is separate from the console's levels
  (warnings and errors by default), optionally with a different level for each tag. Capture is an
  `idfxx::log` sink. With `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES` enabled (the default),
  the sink is added with `all_lines`, and capture stores ESP-IDF's log lines as well as the lines
  logged through `idfxx::log`. With the option disabled, capture stores only the lines logged
  through `idfxx::log`, and nothing is wrapped. The app adds its own lines with `append()`, either
  as text or formatted with `std::format`
- **Survives resets:** lines go first to a ring buffer in RTC memory (the RTC ring), which survives
  deep sleep and any reset other than a power cut. The lines then go to flash, either from a writer
  task or only when the app flushes
- **Stamped entries:** each entry records the boot count and wake count when it was logged, the
  time since that boot or wake, and the wall-clock time if the clock is set. The later lines of a
  multi-line message stay in the same entry
- **Power-cut safe flash format:** a power cut loses at most the record being written, and a read
  never returns a partly written line
- **Faults stored separately:** `record_fault()` stores a fault (kind, detail, firmware version and
  build, task, time) in a slot of its own, with the newest log lines before it. The lines that led
  up to the fault are therefore not lost when the log wraps
- **Repeats suppressed:** `record_fault(kind, detail, suppress_repeats)` records only the first
  occurrence of a fault that keeps happening. Later faults of the same kind are logged but not
  recorded, until `lift_suppression()` lifts the suppression
- **Reset faults at start:** at the next start, a reset caused by a fault (such as a panic, a
  watchdog or a brownout) is recorded as a fault. When a core dump is in flash, the fault also
  includes the task, program counter and backtrace
- **Entry reads:** the app reads the log, its newest entries, or a fault's context, one entry at a
  time, with a range-based `for` loop or with `next()`. Each entry gives its boot count, wake
  count, uptime, wall-clock time and text. `entries(text)` and `next_entry()` split text that the
  app holds, such as a copy of the newest lines in RTC memory, into entries in the same way
- **Text reads:** the app can also read the log, and each fault's context, as text, in chunks of a
  size it chooses. A reader of either kind stays valid while the log is written to
- **`std::format` support** for a fault (one line of escaped text) and for an entry (the entry as
  it is stored)
- **Any storage:** a flash partition (`log` by default), part of a partition, or a `sector_store`
  that the app implements, such as one for a flash chip on a separate bus. Encrypted partitions
  also work

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- A partition for the log, `log` by default (see [Important Notes](#important-notes))
- Components: `idfxx_core`, `idfxx_partition`, `idfxx_task` and `idfxx_log`
- `CONFIG_IDFXX_STD_FORMAT` (enabled by default) for two features:
  - Capture, which is an `idfxx_log` sink. With `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES`
    enabled (the default), the sink is added with `all_lines`, and the option turns on
    `idfxx_log`'s `CONFIG_IDFXX_LOG_ALL_LINES`.
  - The `std::formatter`s for `fault` and `entry`, and the `append()` that formats its text.

  Without this option, capture is off. The log then holds only the lines passed to `append()`, the
  formatting `append()` is not defined, and the app reads a fault's and an entry's fields through
  their accessors.

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_persistent_log:
    version: "^0.1.0"
```

Or add `idfxx_persistent_log` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

```cpp
#include <idfxx/persistent_log>

#include <array>
#include <esp_log.h>
#include <format>

extern "C" void app_main() {
    // Start early, with a boot count that the app keeps (for example, in NVS).
    idfxx::persistent_log::start({.boot_count = boot_count});

    ESP_LOGW("app", "battery low");                             // captured by default
    idfxx::persistent_log::append("wake reason=timer");         // the app's own line
    idfxx::persistent_log::append("battery={} mV", millivolts); // formatted, as std::format does

    // Record a fault. It is stored separately, with the log lines before it.
    idfxx::persistent_log::record_fault("sd_mount", "card not found");

    // A fault that keeps happening is recorded once. Its repeats are logged but not recorded,
    // until the suppression is lifted.
    if (sensor_answers()) {
        idfxx::persistent_log::lift_suppression("sensor");
    } else {
        idfxx::persistent_log::record_fault(
            "sensor", "no reply", idfxx::persistent_log::suppress_repeats
        );
    }

    // Send the log's newest entries, then each fault with the entries of its context.
    for (const auto& e : idfxx::persistent_log::read_newest(20)) {
        send(std::format("{}\n", e));
    }
    auto faults = idfxx::persistent_log::read_faults();
    for (const auto& f : faults) {
        send(std::format("{}\n", f));
        for (const auto& e : faults.context()) {
            send(std::format("{}\n", e));
        }
    }

    // Send the whole log as text, in chunks.
    auto text = idfxx::persistent_log::read_text();
    std::array<char, 256> chunk;
    for (auto part = text.next(chunk); !part.empty(); part = text.next(chunk)) {
        send(part);
    }

    // Before deep sleep, write the lines waiting in RTC memory to flash.
    idfxx::persistent_log::flush();
}
```

A fault formatted with `std::format`:

```text
fault seq=12 kind=panic boot=4 wake=0 utc=2026-11-20T10:31:02Z uptime=1234 version="1.2.0" detail="abort() was called at PC 0x42001234 on core 0" elf=3a5f09c2e1b04d77 task="main" pc=0x42001234 frames=0x42001234,0x42005678 context=5120
```

Entries in the log, as `std::format` writes them:

```text
12.0 +1234 2026-11-20T10:31:02Z W app: battery low
12.0 +1240 2026-11-20T10:31:02Z wake reason=timer
12.1 +95 - E app: config rejected
	ssid=home
```

The last entry's `boot()` is 12, its `wake()` is 1, its `uptime()` is 95 ms, its `wall_time()`
is `std::nullopt`, and its `text()` is `"E app: config rejected\n\tssid=home"`.

### Result-based API

```cpp
#include <idfxx/persistent_log>

idfxx::result<void> start_log(uint32_t boot_count) {
    auto started = idfxx::persistent_log::try_start({
        .boot_count = boot_count,
        // No writer task: lines reach flash only when the app flushes. This suits a device that
        // wakes only briefly.
        .writer = std::nullopt,
    });
    if (!started) {
        return idfxx::error(started.error());
    }
    auto recorded = idfxx::persistent_log::try_record_fault("sd_mount", "card not found");
    if (!recorded) {
        return idfxx::error(recorded.error());
    }
    // Write to flash only when this many bytes are waiting. Fewer bytes stay in RTC memory for a
    // later wake.
    constexpr size_t flush_threshold = 512;
    return idfxx::persistent_log::try_flush_if_pending(flush_threshold);
}

// Sends the text of the entries that this boot logged, oldest first.
idfxx::result<void> send_this_boot(uint32_t boot_count) {
    auto reader = idfxx::persistent_log::try_read();
    if (!reader) {
        return idfxx::error(reader.error());
    }
    idfxx::persistent_log::entry e;
    while (true) {
        auto more = reader->try_next(e);
        if (!more) {
            return idfxx::error(more.error());
        }
        if (!*more) {
            return {};
        }
        if (e.stamped() && e.boot() == boot_count) {
            send(e.text());
        }
    }
}
```

## API Overview

| Item | Description |
| ---- | ----------- |
| `start(config)` / `try_start` | Opens the log, and starts capture and the writer task. The first start of a boot also logs the reset that caused the boot, or records it as a fault. A wake from deep sleep adds nothing. |
| `stop()` / `try_stop` | Stops accepting lines, stops the writer task, flushes, and closes the log. |
| `append(text)` | Adds an entry from the app to the RTC ring. It never waits for flash. |
| `append(fmt, args...)` | Formats `args` with `fmt`, as `std::format` does, then adds the result as `append(text)` does. A call with only text and no arguments uses `append(text)`, so braces in it are stored as they are. |
| `flush()`, `flush_if_pending(threshold)` | Writes the lines waiting in RTC memory to flash. `flush_if_pending` does so only when at least `threshold` bytes are waiting. |
| `read()` → `entry_reader` | Flushes, then returns the entries of the log in flash, oldest first, one at a time: `for (const auto& e : read())`, or `next(e)`. |
| `read_newest(count)` → `entry_reader` | Flushes, then returns the newest `count` entries in flash, oldest of them first. |
| `read_text()` → `text_reader::next(out)` | Flushes, then streams the log in flash as text, oldest first. `next(out)` returns the text that it put in `out`, as a view of `out`. |
| `entry` | One entry: `stamped()`, `boot()`, `wake()`, `uptime()`, `wall_time()` (to the second, as `std::chrono::sys_seconds`), `text()`. |
| `entries(text)` | A view of text that the app holds as a range of entries, such as the text from `copy_newest()` or `copy_newest_in_memory()`. |
| `next_entry(text, e)` | Takes the first entry from text that the app holds, as `entries(text)` splits it. |
| `std::format("{}", e)` | An `entry` as it is stored, without its final newline. |
| `copy_newest(out)` | Flushes, then copies the newest complete entries in flash into `out`. Returns the copied text, as a view of `out`. |
| `copy_newest_in_memory(out)` | Copies the newest complete entries in RTC memory into `out`, even before `start()`. Returns the copied text, as a view of `out`. |
| `clear()` | Empties the log, in flash and in RTC memory. |
| `record_fault(kind, detail)` | Logs the fault, then stores it with the newest lines before it, unless its kind is suppressed. Returns true if it recorded the fault. |
| `record_fault(kind, detail, suppress_repeats)`, `record_fault(kind, suppress_repeats)` | Records the fault as above, then suppresses its kind. Later faults of that kind are only logged. |
| `lift_suppression(kind)` | Lifts the suppression of `kind`, so that its next fault is recorded again. |
| `read_faults()` → `fault_reader` | Steps through the stored faults, oldest first: each fault's header, then its context, either one entry at a time or as text. `for (const auto& f : faults)` or `next(f)` reads the faults. `for (const auto& e : faults.context())` or `next_context(e)` reads a context's entries, and `next_context_text(out)` reads it as text. |
| `clear_faults()`, `fault_count()` | `clear_faults()` forgets the stored faults and the suppressed kinds. `fault_count()` counts the stored faults. |
| `fault` | A fault read from storage: `sequence()`, `kind()`, `detail()`, `boot()`, `wake()`, `wall_time()`, `uptime()`, `version()`, `elf_sha256()`, `task()`, `exception_pc()`, `frames()`, `damaged()`, `context_size()`. |
| `std::format("{}", f)` | A `fault` as one line, at most `max_fault_line_size` characters. |
| `config` | Storage, boot count, wall clock, writer, capture and fault slots. |
| `writer_config` | The writer task's interval, wake threshold, stack and priority. |
| `capture_config` | The capture level, and levels per tag. |
| `fault_config` | The size and number of fault slots, which use the end of the storage. |
| `sector_store` | The interface for storage that the app implements (`do_read`, `do_read_raw`, `do_write`, `do_erase_sector`). |
| `partition_sector_store(part)`, `(part, offset, length)` / `make` | A store for a whole partition, or for part of one. |

Every call that can fail has a `try_` form returning `idfxx::result`. `append()`,
`copy_newest_in_memory()`, `entries()`, `next_entry()` and `lift_suppression()` don't fail. An
exception that a formatter throws passes out of the formatting `append()`. The readers are ranges
only when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled, because their iterators throw a read's
error. Without exceptions, read with `try_next()`, `try_next_context()` and `try_next_context_text()`.

## Error Handling

| Error | Returned by | When |
| ----- | ----------- | ---- |
| `idfxx::errc::invalid_state` | `start` | The log is already started, or capture is on and `CONFIG_IDFXX_LOG_MAX_SINKS` sinks are already added to `idfxx::log`. |
| `idfxx::errc::invalid_state` | `stop`, `flush`, `flush_if_pending`, `read`, `read_newest`, `read_text`, `copy_newest`, `clear`, the fault calls | The log isn't started. |
| `idfxx::errc::invalid_state` | `entry_reader::next`, `text_reader::next`, `fault_reader::next`, `next_context`, `next_context_text`, and the readers' iterators | The log was stopped after the reader was created. |
| `idfxx::errc::not_found` | `start` | No partition has the label in `config::storage`. |
| `idfxx::errc::invalid_arg` | `start` | The store is null, the store's geometry is not one the log supports, the writer's interval or threshold is out of range, the writer's stack is smaller than `configMINIMAL_STACK_SIZE`, the writer's priority is not less than `configMAX_PRIORITIES`, the fault slot count is 0, the fault slot size is 512 bytes or less or is not a multiple of the storage's sector size, or the fault slots do not fit in the storage with at least two sectors left for the log. |
| `idfxx::errc::invalid_arg` | `record_fault` | The kind is empty, longer than 23 characters, not printable ASCII, contains a space, or is `cleared`. |
| `idfxx::errc::invalid_arg` | `partition_sector_store` | The partition or range is empty or not aligned to the erase size. |
| `idfxx::errc::invalid_arg` | `sector_store::read`, `write`, `erase_sector` | The range lies outside the region, or a write isn't aligned to `write_size()`. |
| `idfxx::errc::not_supported` | `record_fault`, `read_faults`, `clear_faults`, `fault_count` | The log was started without fault slots (`config::faults = std::nullopt`). |
| Flash errors | Any call that reads, writes or erases flash | The error that the partition (or the app's store) returned. `stop` stops the log even when its final flush fails. `read`, `read_newest` and `read_text` return their reader, and `copy_newest` copies, even when their flush fails. |

A reader that has returned an error returns the same error from every later call, because the
failed read may have lost text that the reader had already read. A reader's iterator throws the
error, from `begin()` or from an increment, and the reader then throws it again from every later
call. For `fault_reader`, an error in reading a fault's context lasts until the reader moves on to
the next fault, and an error in reading a fault's header lasts for every later call.

A detail longer than 159 bytes is shortened, not rejected. Damaged data in flash is not an error.
Unused sectors, bad records and empty slots are skipped. A fault whose context does not read back
as it was written is listed with `damaged()` set.

## Important Notes

- **Add the log's partition.** The log is stored in a raw data partition, named `log` unless
  `config::storage` names another. The default fault slots (4 × 8 KiB) use the end of the
  partition, and the log needs at least two more sectors, so make the partition 64 KiB or larger:

  ```csv
  # Name,   Type, SubType,   Offset, Size, Flags
  log,      data, undefined, ,       128K,
  ```

- **Don't log secrets.** The log stores text exactly as it receives it. Don't log passwords, keys
  or other secrets, either with `append()` or in a log line while capture is on.
- **Capture is an `idfxx::log` sink.** While the log is running, its sink counts toward
  `CONFIG_IDFXX_LOG_MAX_SINKS`. Which lines the sink receives depends on
  `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES`:
  - With the option enabled (the default), the sink is added with `all_lines`, and it receives
    ESP-IDF's lines as well as the lines logged through `idfxx::log`. The option therefore turns on
    `CONFIG_IDFXX_LOG_ALL_LINES`, which wraps `esp_log` and `esp_log_va` at link time. Nothing else
    in the app may wrap them (see `idfxx_log`'s notes).
  - With the option disabled, the sink receives only the lines logged with `idfxx::log::log()`, a
    `logger` and the `IDFXX_LOGx` macros, and it receives each of these lines complete. It does not
    receive buffer dumps, or the lines that ESP-IDF, libraries and other idfxx components log
    through ESP-IDF's log. Nothing is wrapped.

  Without `CONFIG_IDFXX_STD_FORMAT`, capture is off, and only `append()` adds lines.
- **Capture has its own levels.** A line that `esp_log_level_set()` hides from the console is
  still stored if its level passes `capture_config`. A line above `CONFIG_LOG_MAXIMUM_LEVEL` is
  never logged at all. With `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES` enabled, capture never
  receives the lines that `CONFIG_IDFXX_LOG_ALL_LINES` does not pass to any sink:
  - lines logged from an interrupt, while the flash cache is disabled, or before the scheduler
    starts
  - `ESP_EARLY_LOGx` and `ESP_DRAM_LOGx` lines
  - lines in ESP-IDF's binary log mode

  With the option disabled, capture never receives the lines logged through `idfxx::log` from an
  interrupt, while the flash cache is disabled, or before the scheduler starts. In both cases,
  capture never stores the lines that a task logs while it is inside the log itself, for example
  while it writes to flash.
- **Capture uses stack.** While capture is on, each task that logs uses stack for capture:
  - With `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES` enabled, the task uses the stack that
    `idfxx::log` needs for a sink added with `all_lines` (see `CONFIG_IDFXX_LOG_SINK_MAX_LINE`),
    plus up to about 768 bytes more.
  - With the option disabled, the task uses up to about 768 bytes more than it uses to log the
    line through `idfxx::log` without capture.

  Both figures were measured on esp32s3 in QEMU. A task with a small stack, such as ESP-IDF's
  system event task, may need a larger one.
- **The formatting `append()` uses stack.** It formats its text into a buffer of
  `CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE` bytes (256 by default) on the calling task's stack. This
  is in addition to the stack that `append(text)` and `std::format` use.
- **A reset during a flush loses the lines being flushed.** Lines still in RTC memory at a reset
  are written at the next flush. The lines that a flush was writing when the reset happened are
  lost, rather than written twice. A power cut loses everything in RTC memory. After a firmware
  update that changes `CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE`, `start()` discards the lines in
  RTC memory.
- **Recording a fault uses stack.** `record_fault` uses up to about 4.7 KiB of the calling task's
  stack in ESP-IDF's default debug build (`-Og`), or 3.1 KiB when built for size (`-Os`), measured
  on esp32s3. ESP-IDF's main task has 3584 bytes by default (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`).
  That is too little at `-Og` and leaves little to spare at `-Os`, so give a task that records
  faults a larger stack. `start()` uses up to about 3 KiB (`-Og`) or 2.3 KiB (`-Os`), which fits
  in the default. When `start()` finds a reset fault after the scheduler has started, it records
  the fault on a separate short-lived task with a 7 KiB stack, and waits for that task. Recording
  the fault therefore adds nothing to the caller's stack use. If memory for that task can't be
  allocated, `start()` throws `std::bad_alloc`, or aborts when exceptions are disabled. Before the
  scheduler starts, `start()` records the fault on the caller's stack.
- **Recording a fault blocks.** `record_fault` erases and writes a slot while it holds the log's
  lock. This takes about 100 ms with the default 8 KiB slot. Flushes, reads and clears wait for
  it, but lines logged during that time do not. The fault's context is the newest complete entries
  that fit in the slot, from flash and then from RTC memory. The context ends with the entry that
  `record_fault` logs (`E persistent_log: fault kind=<kind> <detail>`). If storing the fault
  fails, `record_fault` then logs `W persistent_log: fault not recorded kind=<kind>`.
- **Suppression is per kind.** Once a fault is recorded with `suppress_repeats`, its kind is
  suppressed. Until `lift_suppression(kind)` is called, every `record_fault()` call for that kind
  only logs `W persistent_log: fault repeat kind=<kind> <detail>`. This applies with or without
  the tag, and whatever the detail. The detail has no effect on suppression. To record failures of
  different modes separately, give each mode its own kind (`sd_mount`, `sd_read`).
- **`lift_suppression` may wait.** When the kind isn't suppressed, it returns immediately, without
  taking the log's lock. When the kind is suppressed, it takes the log's lock to forget the kind.
  It may therefore wait for a flush or a fault record in progress, for up to about one slot erase.
- **Suppressed kinds are kept in RTC memory.** RTC memory holds up to
  `CONFIG_IDFXX_PERSISTENT_LOG_SUPPRESSED_FAULTS` suppressed kinds (8 by default). When all of
  them are in use, the kind recorded longest ago is forgotten, and its next fault is recorded
  again. Deep sleep and resets keep the suppressed kinds. A power cut, a start that finds them
  invalid, and `clear_faults()` forget them.
- **Reset faults are stamped when `start()` finds them.** The first `start()` of a boot records a
  reset caused by a fault (`panic`, `interrupt_watchdog`, `task_watchdog`, `watchdog`, `brownout`,
  `power_glitch`, `cpu_lockup`) as a fault of that kind, with its repeats suppressed. The fault has
  the boot, wake and time of that start. The lines in its context keep their own stamps. A reset
  of the same kind again is a repeat, whatever its detail (for example, a second panic, at the
  same place or another). A repeat only logs `W persistent_log: fault repeat kind=<kind> <detail>`,
  until a reset that isn't caused by a fault. A deep-sleep wake adds nothing. Any other reset logs
  `I persistent_log: reset reason=<name>` and lifts the suppression of the reset faults' kinds.
- **Core dumps add detail.** With `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` (ELF format) and a
  `coredump` partition, the fault recorded for a panic or watchdog reset gets the task, program
  counter and backtrace from the core dump. On RISC-V, the backtrace holds only the PC and return
  address. The dump is used only if it is complete, comes from the running build, and has not been
  used before. The log remembers the last dump it used in the fault store in flash, so this holds
  across a power cycle and a clear of the faults. The fault's detail is the panic reason (an
  abort's or the task watchdog's). Without a panic reason, the detail is the exception's cause and
  address, and for a panic also its PC (`exccause=28 excvaddr=0x00000000 pc=0x42001234`, or on
  RISC-V `mcause=… mtval=… pc=…`). The dump is never erased. Both the fault store in flash and RTC
  memory record that the dump has been used. A dump whose fault was suppressed as a repeat is
  recorded only in RTC memory. So after RTC memory is lost (a power cycle), a later panic or
  watchdog reset that wrote no new dump can take its details from that dump. The same can happen
  after RTC memory is lost if the fault store holds no entry. That is the case when the fault store
  was never written, when it was erased, or when it has a single slot and power was cut while that
  slot was rewritten.
- **Readers stay valid.** An `entry_reader`, `text_reader` or `fault_reader` can be kept while the
  log is written, flushed or cleared, and any task can call it. A reader ends where the log (or the
  list of faults) ended when the reader was created. A `fault_reader` leaves out a fault that is
  overwritten before the reader reaches it. Once every slot is in use, recording a fault or calling
  `clear_faults()` overwrites the oldest fault. A `text_reader`, and `next_context_text(out)`,
  never return text that was overwritten after the reader checked it. An `entry_reader`, and
  `next_context(e)`, read more text into their buffer than they return. So they can return entries
  whose text in flash was overwritten after they read it. When they reach text that was
  overwritten before they read it, the entry that they were in is cut short there, and they resume
  at the next entry. Stopping the log ends the reader.
- **`read_newest` holds the log's lock.** It flushes, then reads the newest sectors three times,
  while it holds the lock. Until it returns, other flushes, fault recording and other readers wait,
  and captured lines wait in the RTC ring. A `count` larger than the log makes it read the whole
  log three times.
  When the log holds an entry longer than the longest entry that this build stores (text written by
  a build with a larger `CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE`, or damaged text), it can return
  fewer or more than `count` entries.
- **Entry readers allocate a buffer.** An `entry_reader` allocates a buffer when it is created. A
  `fault_reader` allocates one the first time that it reads an entry of a context. The buffer's
  size is `CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE` plus the size of the longest entry, which is
  2505 bytes with the default settings. An entry refers to text in its reader's buffer, so it is
  valid only until the next call to the reader, or the next increment of the reader's iterator.
  Copy the text, or format the entry, to keep it.
- **A reader is a single-pass range.** A range-based `for` loop over a reader continues from where
  the reader is. A second loop over the same reader doesn't start again at the first entry or
  fault. It continues after the last one that the reader returned.
- **Entries have no level or tag.** A line that `append()` added can't be told apart from a captured
  line, so an `entry` gives only its stamp and its text. A captured line's text starts with its
  level and tag (`W app: `).
- **An entry may not be stamped.** An entry whose text was cut short or damaged may not start with
  a complete stamp. `stamped()` is then false, the stamp's fields are 0 or empty, and `text()` is
  all of the entry's text. A fault's context can end partway through an entry, and text that the
  app passes to `next_entry()` can begin or end partway through one.
- **Read one fault's context in one way.** `context()` and `next_context(e)` read more of the
  context into a buffer than they return. `next_context_text(out)` continues after the text that
  they have read, so it skips the entries that are still in the buffer. Read each fault's context
  either one entry at a time or as text, not both.

## In Flash

[FLASH_FORMAT.md](FLASH_FORMAT.md) describes the layout in full, for decoding a flash image.

Every write covers complete 16-byte blocks at 16-byte offsets, on every partition, so the same
layout works with flash encryption enabled. Fields are little-endian.

Each sector:

| Bytes | Field |
| ----- | ----- |
| 0–3 | Magic `"PLOG"` (ASCII) |
| 4–5 | Layout version, 1 |
| 6–7 | Flags: bit 0 marks the log's start (set on the first sector ever written, and on the sector that each `clear()` starts) |
| 8–11 | Sequence number: one more than the sector started before it |
| 12–15 | CRC32 of bytes 0–11 |
| 16–31 | Retire mark: erased while the sector is in use; written (all `0x00`) just before the sector is erased |
| 32– | Records, each starting on a block: a 2-byte length (1 or more), a CRC32 of the length and text, then the text, padded with `0xFF` to the next block |

Each append starts a new block. A record never spans sectors: text that doesn't fit in the head
sector's remaining blocks is split. These sectors are unused:

- a sector with an erased or wrong header
- a sector with the magic, a good CRC and a higher layout version
- a sector whose retire mark, read as stored (without decryption), is not erased

`start()` logs a warning when it finds a sector of a higher layout version while it opens the log.
After a reset that keeps RTC memory, `start()` usually opens the log from the position stored
there. It then reads only the sectors at the end of the log, so it may not find such a sector.

A record whose first block, read as stored, is erased ends the sector's records. A record whose
length or CRC is wrong also ends them; a power cut during a write leaves such a record.

Sectors are started in order around the region. To reuse a sector, the log retires it (writes its
retire mark), erases it, and then writes its header. A power cut can stop the erase partway,
leaving the sector's header, part of its old text and its written retire mark. Because the retire
mark is written, the sector is unused, and none of its old text comes back into the log.

The head is the sector with the highest sequence number. The log runs back from the head through
each sector whose sequence number is one less than that of the sector after it. It stops at the
newest sector marked as a start (which it includes), or at the oldest sector left in the ring.

Each fault slot, from its first byte:

| Bytes | Field |
| ----- | ----- |
| 0–3 | Magic `"PFLT"` (ASCII) |
| 4–5 | Layout version, 1 |
| 6–7 | Reserved, 0 |
| 8–11 | Sequence number: one more than that of the newest fault when this one was recorded |
| 12–15, 16–19 | Boot count, wake count |
| 20–23 | Reserved, 0 |
| 24–31 | Wall time in Unix seconds, signed; `INT64_MIN` when the clock wasn't set |
| 32–35 | Milliseconds since the boot or the deep-sleep wake |
| 36–39, 40–43 | Context length, context CRC32 |
| 44–67 | Kind, NUL-padded (up to 23 characters) |
| 68–99 | Firmware version, NUL-padded (up to 31 bytes) |
| 100–259 | Detail, NUL-padded (up to 159 bytes) |
| 260–267 | The first 8 bytes of the app ELF file's SHA-256 |
| 268–283 | Task name, NUL-padded (up to 15 bytes) |
| 284–287 | Exception PC |
| 288–291 | Frame count (up to 16) |
| 292–355 | Frames: 16 PCs, innermost first; those past the count are 0 |
| 356–359 | 1 when bytes 360–363 hold a core dump's CRC, otherwise 0 (any value other than 1 reads as 0) |
| 360–363 | CRC32 of the image of the last core dump that the log used, as of this fault; 0 when there is none |
| 364–507 | Reserved, 0 |
| 508–511 | CRC32 of bytes 0–507 |
| 512– | Context: log text, padded with `0xFF` to a block, up to the end of the slot |

Reserved bytes are written as 0 and ignored when read. A slot is empty if its header is erased or
wrong. A slot is also empty if it has the magic, a good CRC and a higher layout version; `start()`
logs a warning for it. A fault whose context fails its CRC is still listed, marked as damaged. The
next fault goes in the slot after the one with the highest sequence number. To record a fault, the
log erases the sectors that the fault needs (the header's sector first), writes the context, and
then writes the header.

## License

Apache-2.0.
