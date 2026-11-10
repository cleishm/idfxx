# idfxx_rtc

Abstract interface for external real-time-clock chips.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__rtc.html)**

## Features

- Abstract `idfxx::rtc::clock` base class — drivers for battery-backed real-time-clock chips (such as
  the PCF8563) implement this single interface, so application code can target any of them
- Calendar time as `std::chrono::sys_seconds` (UTC)
- Reads the time only while the clock is keeping reliable time — not lost (supply or oscillator
  failure), not stopped, and holding a time the chip can represent — and returns `std::nullopt`
  otherwise, never a time that isn't valid
- Setting the time clears the chip's lost-time indication in the same operation and restarts a
  stopped clock, so the time is valid afterward
- Dual API: exception-based and result-based error handling

`idfxx::rtc` is for external real-time-clock chips only. It is unrelated to
`idfxx::chrono::rtc_clock` in `idfxx_core`, which reads the ESP32's internal RTC timer (time since
power-on, kept through deep sleep).

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core (installed automatically)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_rtc:
    version: "^1.0.0"
```

Or add `idfxx_rtc` to the `REQUIRES` list in your component's `CMakeLists.txt`.

This component is rarely used on its own — depend on a concrete driver such as `idfxx_rtc_pcf8563`,
which pulls in `idfxx_rtc` automatically.

## Usage

`idfxx_rtc` defines no concrete clock; it's an interface. Code that works with any real-time clock
takes an `idfxx::rtc::clock&`. Construct a concrete driver (e.g. `idfxx::rtc::pcf8563`) and pass it
where a `clock&` is expected.

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/chrono>
#include <idfxx/log>
#include <idfxx/rtc/clock>

#include <chrono>

// Restore the system time at boot, or seed the clock if it has lost track
void restore_system_time(idfxx::rtc::clock& rtc) {
    using namespace std::chrono;
    try {
        if (auto t = rtc.now()) {
            idfxx::chrono::set_system_time(*t);
        } else {
            rtc.set_time(sys_days{2026y / January / 1});
        }
    } catch (const std::system_error& e) {
        idfxx::log::error("RTC", "Error: {}", e.what());
    }
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/chrono>
#include <idfxx/log>
#include <idfxx/rtc/clock>

void restore_system_time(idfxx::rtc::clock& rtc) {
    auto now = rtc.try_now();
    if (!now) {
        idfxx::log::error("RTC", "Failed to read the clock: {}", now.error().message());
        return;
    }
    if (!*now) {
        idfxx::log::warn("RTC", "The clock's time is not valid");
        return;
    }
    if (auto r = idfxx::chrono::try_set_system_time(**now); !r) {
        idfxx::log::error("RTC", "Failed to set the system time: {}", r.error().message());
    }
}
```

## API Overview

### `idfxx::rtc::clock` (abstract)

- `now()` / `try_now()` - Read the current time (UTC, to the second), or `std::nullopt` while the
  clock's time is not valid
- `set_time(t)` / `try_set_time(t)` - Set the time, clearing the chip's lost-time indication and
  restarting a stopped clock

Drivers implement the protected `do_now()` and `do_set_time()` hooks.

## Error Handling

- **Dual API Pattern**: The component provides both result-based and exception-based APIs
  - `try_*` methods return `idfxx::result<T>` (always available)
  - Methods without `try_` prefix throw `std::system_error` (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- **Time that is not valid**: not an error. `now()` returns `std::nullopt` while the chip has lost
  track of the time (such as on first boot or after a battery change), the clock is stopped, or it
  holds a time the chip can't represent. Reseed the clock with `set_time()`, which also restarts a
  stopped clock; the time is valid afterward
- **Error codes**:
  - `errc::invalid_arg` - `set_time()` with a time outside the range the chip can hold; nothing is
    written
  - Any other error (for an I2C chip, `errc::invalid_response` or `errc::timeout`) means a transfer
    with the chip failed. Retry

Using a moved-from driver object is undefined behavior.

## Important Notes

- **UTC**: Clocks hold UTC. Convert to local time when displaying it.
- **Chip ranges**: The range of times a clock can hold depends on the chip (the PCF8563 holds
  2000–2099). Each driver documents and checks its own range.
- **System time**: Reading a clock doesn't change the system time. Pass the time `now()` returns to
  `idfxx::chrono::set_system_time()` (from `idfxx_core`) to set it, usually once at boot.
- **Chip features**: Alarms, timers and clock outputs differ between chips, so they live on the
  concrete drivers, not on this interface.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
