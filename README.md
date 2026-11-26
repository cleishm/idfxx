# idfxx

Modern C++23 components for ESP-IDF development.

API documentation: **https://cleishm.github.io/idfxx/**

## Features

- **Modern C++23** - Uses `std::expected`, concepts, and other C++23 features
- **Dual error handling** - Both exception-throwing and result-returning APIs
- **Type-safe** - Compile-time validation where possible
- **Zero-overhead abstractions** - Minimal runtime cost over raw ESP-IDF APIs
- **ESP Component Registry** - Install via `idf_component.yml`

## Components

idfxx is a family of independently installable components covering:

- **[Core infrastructure](components/core/README.md):** error handling, memory allocators, chrono,
  logging, and chip information
- **[System services](components/system/README.md):** tasks, timers, queues, event loops and event
  groups, sleep, and an interactive console
- **[Networking](components/network/README.md):** TCP/UDP sockets and DNS, network interfaces, and
  Wi-Fi
- **[Radio](components/radio/README.md):** LoRa transceivers
- **[Peripherals](components/peripherals/README.md):** GPIO, I2C, SPI, 1-Wire, PWM, ADC, buttons,
  and rotary encoders
- **[Storage](components/storage/README.md):** NVS, flash partitions, OTA updates, SD cards, and FAT
  filesystems
- **[Displays](components/displays/README.md):** LCD and ePaper panels, and touch controllers
- **[Graphics](components/graphics/README.md):** drawing primitives, bitmap fonts, and JPEG images
- **[Sensors](components/sensors/README.md):** temperature and humidity sensors
- **[Real-time clocks](components/rtc/README.md):** external RTC chips, with alarms and timers
- **[Application protocols](components/protocols/README.md):** HTTP client, and HTTP and HTTPS
  servers

The [component catalog](components/README.md) lists every component by area, and each area's README
gives a description of each component with links to its source and API documentation.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler (GCC 13+ or Clang 16+)

## Installation

Add components to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_core:
    version: "^1.0.0"
  cleishm/idfxx_gpio:
    version: "^1.0.0"
  # Add other components as needed
```

## Example

Blink an LED from a periodic timer. If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled, failures throw
`std::system_error`:

```cpp
#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/timer>

#include <chrono>

using namespace std::chrono_literals;
using idfxx::gpio;

static constexpr idfxx::log::logger logger{"app"};

extern "C" void app_main() {
    try {
        auto led = idfxx::gpio_2;
        led.set_direction(gpio::mode::output);

        auto blink = idfxx::timer::start_periodic({.name = "blink"}, 500ms, [led]() mutable { led.toggle_level(); });

        logger.info("Blinking {}", led);
        while (true) {
            idfxx::delay(1s);
        }
    } catch (const std::system_error& e) {
        logger.error("Setup failed: {}", e.what());
    }
}
```

The same with the result-based API, where each `try_*` call returns an `idfxx::result<T>`:

```cpp
extern "C" void app_main() {
    auto led = idfxx::gpio_2;
    if (auto r = led.try_set_direction(gpio::mode::output); !r) {
        logger.error("Failed to configure LED: {}", r.error().message());
        return;
    }

    auto blink = idfxx::timer::try_start_periodic({.name = "blink"}, 500ms, [led]() mutable { led.toggle_level(); });
    if (!blink) {
        logger.error("Failed to start timer: {}", blink.error().message());
        return;
    }

    logger.info("Blinking {}", led);
    while (true) {
        idfxx::delay(1s);
    }
}
```

Each component's README has a fuller usage guide, and most components ship runnable examples under
`examples/`.

## Error Handling

All idfxx components provide two API styles:

- **Exception-based** — Methods throw `std::system_error` on failure (requires
  `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- **Result-based** — `try_*` methods return `idfxx::result<T>`
  (`std::expected<T, std::error_code>`), always available

### Out-of-Memory

Running out of heap memory is treated as **fatal**, in the same way as a failed C++ `operator new`.
When any allocation fails (whether from C++, ESP-IDF, or FreeRTOS), idfxx will:

- **Throw `std::bad_alloc`** if exceptions are enabled (`CONFIG_COMPILER_CXX_EXCEPTIONS`)
- **Call `abort()`** if exceptions are disabled

This applies uniformly to both API styles. Heap exhaustion is never returned as a recoverable error
code in `idfxx::result<T>`. In general, a program can't recover from it, because the standard
library already throws or aborts when an allocation fails. Code that uses idfxx does not need to
check for out-of-memory errors. Either the call succeeds, or the program terminates (or an exception
propagates).

This covers heap exhaustion only. Some ESP-IDF calls also return `ESP_ERR_NO_MEM` when a fixed-size
resource is used up, such as a full table or no free handle. That is a limit that the caller can
handle, and idfxx reports it as an ordinary error.

If you need to handle potential out-of-memory conditions gracefully (e.g. pre-checking available
heap before a large allocation), use the ESP-IDF and FreeRTOS APIs directly.

## Configuration

### `std::format` Support (`CONFIG_IDFXX_STD_FORMAT`)

Several idfxx types provide `std::formatter` specializations (e.g. `gpio`, `i2c::port`,
`spi::host_device`, `core_id`, `flags<E>`). These are controlled by the `CONFIG_IDFXX_STD_FORMAT`
Kconfig option (under **IDFXX** menu), which is enabled by default.

Disabling this option prevents component headers from including `<format>`, which can significantly
reduce image size. Note that `idfxx_log` requires `CONFIG_IDFXX_STD_FORMAT` and will produce a build
error if included without it.

## Versioning

Repository releases use [calendar versioning](https://calver.org/) (e.g., `v2026.02.10`). Individual
components follow [semantic versioning](https://semver.org/) independently. A component's version is
only bumped when that component changes. Use caret constraints (e.g., `^1.0.0`) in your
`idf_component.yml` to receive compatible updates.

## Contributing

See [CONTRIBUTING.md](https://github.com/cleishm/idfxx/blob/main/CONTRIBUTING.md) for building,
testing, and code style.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
