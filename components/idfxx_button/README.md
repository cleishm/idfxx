# idfxx_button

GPIO push-button driver with debounce and event detection for ESP32.

## Features

- **Debounced button input** with configurable dead time
- **Click detection** distinguishing short press from long press
- **Long press detection** with configurable threshold
- **Autorepeat mode** for repeated click events while held
- **Polling and interrupt modes** for button state detection
- **Event-driven callbacks** for press, release, click, and long press
- **Type-safe configuration** with std::chrono durations and idfxx::gpio pins

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_button:
    version: "^0.9.0"
```

Or add `idfxx_button` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/button>
#include <idfxx/log>

try {
    idfxx::button btn({
        .pin = idfxx::gpio_4,
        .callback = [](idfxx::button::event_type ev) {
            if (ev == idfxx::button::event_type::clicked) {
                idfxx::log::info("button", "Button clicked!");
            } else if (ev == idfxx::button::event_type::long_press) {
                idfxx::log::info("button", "Long press detected!");
            }
        },
    });

    // Button is now being monitored...

} catch (const std::system_error& e) {
    idfxx::log::error("button", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
#include <idfxx/button>
#include <idfxx/log>

auto btn = idfxx::button::make({
    .pin = idfxx::gpio_4,
    .callback = [](idfxx::button::event_type ev) {
        if (ev == idfxx::button::event_type::clicked) {
            idfxx::log::info("button", "Button clicked!");
        } else if (ev == idfxx::button::event_type::long_press) {
            idfxx::log::info("button", "Long press detected!");
        }
    },
});

if (!btn) {
    idfxx::log::error("button", "Failed to create button: {}", btn.error().message());
    return;
}

// Button is now being monitored...
```

## API Overview

### Factory Methods

- `button::make(config)` - Create button from config (result-based)

### Constructors (exception-based)

- `button(config)` - Create button from config (throws on failure)

### Event Types

- `event_type::pressed` - Button pressed
- `event_type::released` - Button released
- `event_type::clicked` - Short press completed (pressed then released)
- `event_type::long_press` - Button held beyond long-press threshold

### Detection Modes

- `mode::poll` - Periodic timer polling (default)
- `mode::interrupt` - GPIO interrupt with debounce timer

### Configuration

Key configuration options:
- `pin` - GPIO pin (required)
- `mode` - Detection mode (default: poll)
- `pressed_level` - GPIO level when pressed (default: low)
- `enable_pull` - Enable internal pull resistor (default: true)
- `autorepeat` - Enable autorepeat mode (default: false, mutually exclusive with long press)
- `dead_time` - Debounce delay (default: 50ms)
- `long_press_time` - Long press threshold (default: 1000ms)
- `autorepeat_timeout` - Autorepeat start delay (default: 500ms)
- `autorepeat_interval` - Autorepeat repeat interval (default: 250ms)
- `poll_interval` - Polling interval (default: 10ms)
- `callback` - Event callback (required)

## Error Handling

The `make()` factory returns `idfxx::result<button>`. The exception-based constructor throws `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

Error codes from `idfxx::errc`:

- `invalid_arg` - Pin is not connected, or callback is not set

## Important Notes

- **Non-copyable/move-only**: Button is non-copyable and move-only.
- **Automatic cleanup**: The destructor automatically stops monitoring and releases resources.
- **Callback context**: Callbacks are invoked from a timer task context. They must not block or call back into the button API.
- **Autorepeat vs long press**: These modes are mutually exclusive. When autorepeat is enabled, long press detection is disabled.
- **Interrupt mode**: When using `mode::interrupt`, the GPIO ISR service must be installed before creating the button (via `idfxx::gpio::install_isr_service()`).

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
