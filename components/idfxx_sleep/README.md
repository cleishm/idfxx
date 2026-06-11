# idfxx_sleep

Light and deep sleep with type-safe wake-up source configuration for ESP32.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__sleep.html)**

## Features

- **Light sleep** that suspends the CPU and resumes execution in place
- **Deep sleep** that powers down the chip and restarts the application on wake
- **Declarative wake-up sources** passed directly to the sleep call
- **Timer wake-up** with std::chrono durations
- **GPIO wake-up** from light sleep on any digital pin
- **EXT0/EXT1 wake-up** on RTC-capable pins, working in deep sleep
- **Deep-sleep GPIO wake-up** on chips without EXT0/EXT1 (e.g. ESP32-C3)
- **Wake-cause reporting** to detect why (and whether) the chip woke from sleep

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core, idfxx_gpio

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_sleep:
    version: "^1.0.0"
```

Or add `idfxx_sleep` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/sleep>
#include <idfxx/gpio>
#include <idfxx/log>

using namespace std::chrono_literals;

try {
    // Wake on a button (active low) or after 30 seconds, whichever first.
    auto cause = idfxx::sleep::light_sleep(
        idfxx::sleep::timer_wake{30s},
        idfxx::sleep::gpio_wake{idfxx::gpio_0, idfxx::gpio::level::low});

    if (cause == idfxx::sleep::wakeup_source::gpio) {
        idfxx::log::info("sleep", "button pressed");
    }

    // Low-power counterpart to std::this_thread::sleep_for:
    idfxx::sleep::light_sleep_for(5s);
} catch (const std::system_error& e) {
    idfxx::log::error("sleep", "Error: {}", e.what());
}
```

### Result-based API

Always available:

```cpp
#include <idfxx/sleep>
#include <idfxx/log>

using namespace std::chrono_literals;

auto cause = idfxx::sleep::try_light_sleep(idfxx::sleep::timer_wake{30s});
if (!cause) {
    idfxx::log::error("sleep", "Sleep rejected: {}", cause.error().message());
    return;
}
```

### Deep Sleep

Deep sleep powers down the chip; waking restarts the application from the
beginning. Use `wakeup_cause()` to detect the wake after the restart:

```cpp
#include <idfxx/sleep>

using namespace std::chrono_literals;

extern "C" void app_main() {
    if (auto cause = idfxx::sleep::wakeup_cause()) {
        // woke from sleep; *cause identifies the source
    } else {
        // cold boot
    }

    idfxx::sleep::deep_sleep(
        // On the original ESP32 the low trigger is ext1_mode::all_low (see Important Notes).
        idfxx::sleep::ext1_wake{{idfxx::gpio_0}, idfxx::sleep::ext1_mode::any_low},
        idfxx::sleep::timer_wake{60s}); // does not return
}
```

### Persistent wake-up sources

Sources passed to a sleep call are disarmed when it returns. To keep sources
armed across many sleeps, arm them once with `enable_wakeup`:

```cpp
idfxx::sleep::enable_wakeup(idfxx::sleep::gpio_wake{button, idfxx::gpio::level::low});
while (running) {
    auto cause = idfxx::sleep::light_sleep(); // wakes on the armed button
    // ...
}
idfxx::sleep::disable_all_wakeup_sources();
```

See [examples/light_sleep_demo](examples/light_sleep_demo) and
[examples/deep_sleep_demo](examples/deep_sleep_demo) for complete programs.

## API Overview

### Wake-up Source Specifications

| Type | Description |
|------|-------------|
| `timer_wake{duration}` | Wake after a fixed sleep duration |
| `gpio_wake{pin, level}` | Wake from light sleep on a digital pin level |
| `ext0_wake{pin, level}` | Wake on a single RTC-capable pin (EXT0; deep-sleep capable) |
| `ext1_wake{{pins...}, mode}` | Wake on multiple RTC-capable pins (EXT1; deep-sleep capable) |
| `deep_sleep_gpio_wake{{pins...}, mode}` | Deep-sleep pin wake on chips without EXT0/EXT1 |

### Entering Sleep

| Method | Description |
|--------|-------------|
| `light_sleep(sources...)` / `try_light_sleep(sources...)` | Arm the sources, sleep, disarm; returns the wake cause |
| `light_sleep_for(duration)` / `try_light_sleep_for(duration)` | Low-power `sleep_for`; returns the wake cause |
| `deep_sleep(sources...)` / `try_deep_sleep(sources...)` | Arm the sources and power down; returns only on failure |
| `deep_sleep()` / `deep_sleep(duration)` | Power down on previously armed sources; does not return |

### Persistent Configuration

| Method | Description |
|--------|-------------|
| `enable_wakeup(source)` / `try_enable_wakeup(source)` | Arm a source persistently across sleeps |
| `disable_wakeup_source(source)` / `try_disable_wakeup_source(source)` | Disarm one source class |
| `disable_all_wakeup_sources()` / `try_disable_all_wakeup_sources()` | Disarm everything |

### Status

| Method | Description |
|--------|-------------|
| `wakeup_cause()` | `std::optional<wakeup_source>`: the most recent wake cause, or `nullopt` if not a sleep wake |
| `ext1_wakeup_status()` | Mask of pins that triggered the last EXT1 wake |
| `to_string(source)` | Name of a `wakeup_source`, e.g. `"timer"` (also usable via `std::format` when `CONFIG_IDFXX_STD_FORMAT` is enabled) |

## Error Handling

Errors are reported as `idfxx::result<T>` (result-based API) or
`std::system_error` (exception-based API):

- `idfxx::errc::invalid_state` — sleep was rejected, e.g. a wake-up source
  triggered before sleep was entered (`ESP_ERR_SLEEP_REJECT`), or a source
  being disarmed was never armed
- `idfxx::errc::invalid_arg` — negative duration, unconnected pin, a pin
  that is not capable of the requested wake-up, or a timer wake-up too short
  to enter sleep

## Important Notes

- **At least one wake-up source must be armed** before entering light
  sleep, or the sleep request is rejected.
- **Sources passed to a sleep call are disarmed when it returns**; sources
  armed with `enable_wakeup` stay armed until explicitly disarmed.
- **Light-sleep GPIO wake-up is level-triggered.** If the pin is already at
  the wake level when sleep is entered, the chip wakes immediately — and a
  held level re-wakes on every sleep attempt.
- **Digital interrupts are not delivered during light sleep.** GPIO edge
  interrupts that occur while sleeping are lost; re-check interrupt-driven
  state after waking.
- **Wake-up source availability is chip-specific.** EXT0 exists on
  ESP32/S2/S3; EXT1 on ESP32/S2/S3/C6/H2/P4; ESP32-C3 has neither and uses
  `deep_sleep_gpio_wake` instead. The corresponding types are only declared
  on chips that support them (`SOC_PM_SUPPORT_EXT0_WAKEUP`,
  `SOC_PM_SUPPORT_EXT1_WAKEUP`, `SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP`).
- **EXT1's low trigger is named per target.** The original ESP32 defines
  `ext1_mode::all_low` (its hardware wakes only when all selected pins are
  low); every other EXT1 chip defines `ext1_mode::any_low`. Only the matching
  enumerator compiles for a given target, so portable code selects it under
  `#if CONFIG_IDF_TARGET_ESP32` rather than silently getting all-low where
  any-low was intended.
- **Floating EXT1 pins need pull resistors.** This component does not
  configure RTC pulls; use external resistors, or hold a configured pull
  across sleep (see `idfxx::gpio` hold and sleep-pull APIs).
- **Deep sleep does not return.** All RAM state is lost except RTC memory
  (`RTC_DATA_ATTR`); the application restarts in `app_main`.

## License

Apache-2.0
