# idfxx_log

Type-safe logging with compile-time format validation for ESP32.

## Features

- **Type-safe format strings** with compile-time validation via std::format
- **Free functions** for ad-hoc logging with explicit tags
- **Logger class** for repeated logging with a fixed tag
- **Zero-cost macros** that eliminate filtered messages including argument evaluation
- **Runtime level control** per-tag, matching ESP-IDF's level filtering
- **Full ESP-IDF integration** preserving timestamps, colors, and level prefixes
- **Sinks** that receive log lines in addition to the console, for example to store the log in
  flash or send it over the network. Each sink chooses which lines it wants, from the lines logged
  through idfxx::log or from every ESP-IDF log line

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_log:
    version: "^1.1.0"
```

Or add `idfxx_log` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Free Functions

```cpp
#include <idfxx/log>

void initialize(int port) {
    idfxx::log::info("app", "Initializing on port {}", port);
}

void check_sensor(float temperature) {
    if (temperature > 85.0f) {
        idfxx::log::warn("sensor", "Temperature {:.1f}C exceeds threshold", temperature);
    }
}
```

### Logger Class

For repeated logging with a fixed tag, create a logger to avoid repeating the tag:

```cpp
#include <idfxx/log>

static constexpr idfxx::log::logger log{"my_component"};

void initialize(int port) {
    log.info("Initializing on port {}", port);
}

void process(std::string_view name, int value) {
    log.debug("Processing {} = {}", name, value);
    if (value < 0) {
        log.error("Invalid value {} for {}", value, name);
    }
}
```

### Convenience Macros

With the macros, the compiler always eliminates a call that is filtered out at compile time:

```cpp
#include <idfxx/log>

void hot_path() {
    IDFXX_LOGD("perf", "Processing batch of {} items", count);
    // When LOG_LOCAL_LEVEL < ESP_LOG_DEBUG, the entire call
    // (including argument evaluation) is eliminated.
}
```

### Runtime Level Control

```cpp
#include <idfxx/log>

void configure_logging() {
    // Set level for a specific tag
    idfxx::log::set_level("my_tag", idfxx::log::level::warn);

    // Set the default level for all tags
    idfxx::log::set_default_level(idfxx::log::level::info);
}
```

### Sinks

A sink receives the lines that it wants. Its choice is independent of the console's log levels, so
it can receive a line whose severity is below the level set with `set_level()`. Derive a class
from `idfxx::log::sink` and add an instance of it:

```cpp
#include <idfxx/log>

#include <atomic>

class warning_counter final : public idfxx::log::sink {
public:
    unsigned count() const { return _count; }

private:
    bool do_wants(idfxx::log::level lvl, std::string_view) const noexcept override {
        return lvl <= idfxx::log::level::warn;
    }
    void do_write(idfxx::log::level, std::string_view, std::string_view) noexcept override {
        ++_count;
    }
    std::atomic<unsigned> _count{0};
};

static warning_counter warnings;

void start_counting() {
    // The sink receives the lines logged through idfxx::log
    idfxx::log::add_sink(warnings);
}

void stop_counting() {
    idfxx::log::remove_sink(warnings);
}
```

With `CONFIG_IDFXX_LOG_ALL_LINES` enabled, a sink can receive every ESP-IDF log line instead. That
includes lines logged with `ESP_LOGx` and `esp_log_write`, and by the libraries that log through
them. This example uses the result-based API:

```cpp
void start_counting_all() {
    auto added = idfxx::log::try_add_sink(idfxx::log::all_lines, warnings);
    if (!added) {
        idfxx::log::error("app", "No sink added: {}", added.error().message());
    }
}
```

## API Overview

### Level Enum

| Value     | Description                    |
|-----------|--------------------------------|
| `none`    | No log output                  |
| `error`   | Critical errors                |
| `warn`    | Warning conditions             |
| `info`    | Informational messages         |
| `debug`   | Detailed debugging information |
| `verbose` | Highly detailed trace output   |

### Free Functions

| Function             | Description                  |
|----------------------|------------------------------|
| `error()`            | Log at error level           |
| `warn()`             | Log at warning level         |
| `info()`             | Log at info level            |
| `debug()`            | Log at debug level           |
| `verbose()`          | Log at verbose level         |
| `log()`              | Log at a specified level     |
| `set_level()`        | Set level for a tag          |
| `set_default_level()`| Set default level            |
| `add_sink()`         | Add a sink                   |
| `try_add_sink()`     | Add a sink (result-based)    |
| `remove_sink()`      | Remove a sink                |

### Sinks

| Item                                 | Description                                              |
|--------------------------------------|----------------------------------------------------------|
| `sink`                               | Base class: override `do_wants()` and `do_write()`       |
| `sink::wants()`                      | Returns whether the sink wants a line                    |
| `sink::write()`                      | Receives a line: its level, tag and message              |
| `add_sink(s)`                        | Add `s` for the lines logged through idfxx::log          |
| `add_sink(all_lines, s)`             | Add `s` for every ESP-IDF log line (`CONFIG_IDFXX_LOG_ALL_LINES`) |
| `remove_sink(s)`                     | Remove `s`; returns once no call into it is in progress  |

### Logger Methods

| Method        | Description                          |
|---------------|--------------------------------------|
| `error()`     | Log at error level                   |
| `warn()`      | Log at warning level                 |
| `info()`      | Log at info level                    |
| `debug()`     | Log at debug level                   |
| `verbose()`   | Log at verbose level                 |
| `log()`       | Log at a specified level             |
| `set_level()` | Set runtime level for this tag       |
| `tag()`       | Get the tag associated with a logger |

### Macros

| Macro        | Level   |
|--------------|---------|
| `IDFXX_LOGE` | Error   |
| `IDFXX_LOGW` | Warning |
| `IDFXX_LOGI` | Info    |
| `IDFXX_LOGD` | Debug   |
| `IDFXX_LOGV` | Verbose |

## Error Handling

`add_sink()` throws `std::system_error` with one of these error codes, and `try_add_sink()` returns
one:

| Error                 | Cause                                                                              |
|-----------------------|------------------------------------------------------------------------------------|
| `errc::invalid_arg`   | The sink is already added (for idfxx::log lines or for every line)                 |
| `errc::invalid_state` | `CONFIG_IDFXX_LOG_MAX_SINKS` sinks are already added, counting sinks being removed |

The logging functions do not report errors.

## Important Notes

- **Requires `CONFIG_IDFXX_STD_FORMAT`**: This component depends on `<format>` and requires the
  `CONFIG_IDFXX_STD_FORMAT` Kconfig option to be enabled (the default). With the option disabled,
  the component compiles nothing. An app can then still depend on it, directly or through another
  component, as long as no code includes `<idfxx/log>`. Including `<idfxx/log>` stops the build
  with an error that says the option must be enabled. If you need logging without `std::format`
  overhead, use ESP-IDF's `ESP_LOGx` macros directly.
- **std::format allocates on the heap**: Do not use this component in ISR context, DRAM-only
  contexts, or early boot. Use ESP-IDF's `ESP_DRAM_LOGx` and `ESP_EARLY_LOGx` macros for those
  cases.
- **Tag lifetime**: The `logger` class stores a pointer to the tag string. Use string literals or
  static storage duration strings.
- **Three filtering layers**: Macros provide compile-time filtering. Template functions check
  `LOG_LOCAL_LEVEL`. Before formatting, they also check the tag's runtime level with
  `esp_log_level_get()`. A line whose severity is below the tag's runtime level is still formatted
  if a sink wants it.
- **Sink lifetime**: A sink must remain valid until it is removed. `remove_sink()` returns once no
  call into the sink is in progress, so the sink can be destroyed afterward. While a removal waits,
  other sinks can be added and removed, but the sink being removed still counts toward
  `CONFIG_IDFXX_LOG_MAX_SINKS` until the calls into it have finished. A sink must not add or remove
  sinks from inside its own functions. If a task is deleted while it is inside a sink,
  `remove_sink()` for that sink never returns, so do not delete a task that may be logging.
- **When sinks are called**: Sinks are called from the task that logged the line, after the
  console has printed the line. Several tasks can call a sink at the same time. Lines logged from
  an interrupt, while the flash cache is disabled, or before the scheduler starts are not passed
  to any sink. Lines that a task logs while it is inside a sink are not passed to any sink either,
  so a sink can log safely.
- **`CONFIG_IDFXX_LOG_ALL_LINES` wraps ESP-IDF's log**: With this option enabled, idfxx_log wraps
  `esp_log` and `esp_log_va` at link time (`-Wl,--wrap`). Nothing else in the app may wrap them: a
  second definition of `__wrap_esp_log` or `__wrap_esp_log_va` makes the link fail.
- **Lines passed to a sink added with `all_lines`**:
  - Lines logged through idfxx::log are passed complete. Buffer dumps are different: like
    `ESP_LOG_BUFFER_*`, they are logged through ESP-IDF's log, so they are handled, and shortened,
    like any other ESP-IDF log line.
  - Each `esp_log` call (as made by `ESP_LOGx`) is one line. A line logged in several
    `esp_log_write` calls is passed with the level and tag of its first call.
  - An ESP-IDF log line longer than `CONFIG_IDFXX_LOG_SINK_MAX_LINE` bytes is shortened to that
    limit. The limit includes the prefix that the line's caller formatted before the message and,
    for a line logged in several calls, the line's tag.
  - The prefix is located using the line's tag, so a line with an empty tag is passed with its
    prefix still attached.
  - The sink does not receive early, DRAM or binary-mode lines, or lines more verbose than the
    compile-time maximum level. With `CONFIG_LOG_MASTER_LEVEL` under log v1, it also does not
    receive `ESP_LOGx` lines more verbose than the master level, because the macros skip those
    lines before logging them.
- **Stack use with `CONFIG_IDFXX_LOG_ALL_LINES`**: With the option enabled and no sink added with
  `all_lines`, each log call uses about 64 bytes more stack. With such a sink added, each log call
  uses up to about `CONFIG_IDFXX_LOG_SINK_MAX_LINE` plus 64 bytes more, plus the stack that the
  sinks themselves use. The options' help text has the details. Tasks with small stacks that log
  (such as ESP-IDF's `sys_evt`) may need larger stacks.
- **Format string safety**: Unlike printf-style `ESP_LOGx` macros, format errors are caught at
  compile time.

## License

Apache-2.0
