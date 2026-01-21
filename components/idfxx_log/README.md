# idfxx_log

Type-safe logging with compile-time format validation for ESP32.

## Features

- **Type-safe format strings** with compile-time validation via std::format
- **Free functions** for ad-hoc logging with explicit tags
- **Logger class** for repeated logging with a fixed tag
- **Zero-cost macros** that eliminate filtered messages including argument evaluation
- **Runtime level control** per-tag, matching ESP-IDF's level filtering
- **Full ESP-IDF integration** preserving timestamps, colors, and level prefixes

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_log:
    version: "^0.9.0"
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

For guaranteed zero-cost elimination when compile-time filtered:

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

## Important Notes

- **Requires `CONFIG_IDFXX_STD_FORMAT`**: This component depends on `<format>` and requires the `CONFIG_IDFXX_STD_FORMAT` Kconfig option to be enabled (the default). A build error is generated if the option is disabled. If you need logging without `std::format` overhead, use ESP-IDF's `ESP_LOGx` macros directly.
- **std::format allocates on the heap**: Do not use this component in ISR context, DRAM-only contexts, or early boot. Use ESP-IDF's `ESP_DRAM_LOGx` and `ESP_EARLY_LOGx` macros for those cases.
- **Tag lifetime**: The `logger` class stores a pointer to the tag string. Use string literals or static storage duration strings.
- **Three filtering layers**: Macros provide compile-time filtering, template functions check `LOG_LOCAL_LEVEL`, and runtime filtering checks `esp_log_level_get()` before formatting.
- **Format string safety**: Unlike printf-style `ESP_LOGx` macros, format errors are caught at compile time.

## License

Apache-2.0
