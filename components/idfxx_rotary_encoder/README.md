# idfxx_rotary_encoder

Incremental rotary encoder driver with optional button support for ESP32.

## Features

- **Rotary encoder position tracking** with event-driven callbacks
- **Optional push-button** with press, release, long-press, and click detection
- **Acceleration support** for faster value changes when turning quickly
- **Configurable debounce** for button inputs
- **Type-safe configuration** with std::chrono durations and idfxx::gpio pins

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- `esp-idf-lib/encoder` v3.0.0 (automatically resolved via component manager)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_rotary_encoder:
    version: "^0.9.0"
```

Or add `idfxx_rotary_encoder` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/rotary_encoder>
#include <idfxx/log>

try {
    idfxx::rotary_encoder encoder({
        .pin_a = idfxx::gpio_4,
        .pin_b = idfxx::gpio_5,
        .pin_btn = idfxx::gpio_6,
        .callback = [](const idfxx::rotary_encoder::event& ev) {
            if (ev.type == idfxx::rotary_encoder::event_type::changed) {
                idfxx::log::info("encoder", "Rotated: {}", ev.diff);
            } else if (ev.type == idfxx::rotary_encoder::event_type::btn_clicked) {
                idfxx::log::info("encoder", "Button clicked!");
            }
        },
    });

    // Enable acceleration for faster value changes
    encoder.enable_acceleration(10);

} catch (const std::system_error& e) {
    idfxx::log::error("encoder", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
#include <idfxx/rotary_encoder>
#include <idfxx/log>

auto encoder = idfxx::rotary_encoder::make({
    .pin_a = idfxx::gpio_4,
    .pin_b = idfxx::gpio_5,
    .pin_btn = idfxx::gpio_6,
    .callback = [](const idfxx::rotary_encoder::event& ev) {
        if (ev.type == idfxx::rotary_encoder::event_type::changed) {
            idfxx::log::info("encoder", "Rotated: {}", ev.diff);
        } else if (ev.type == idfxx::rotary_encoder::event_type::btn_clicked) {
            idfxx::log::info("encoder", "Button clicked!");
        }
    },
});

if (!encoder) {
    idfxx::log::error("encoder", "Failed to create encoder: {}", encoder.error().message());
    return;
}

// Enable acceleration
encoder->enable_acceleration(10);
```

## API Overview

### Factory Methods

- `rotary_encoder::make(config)` - Create encoder from config (includes callback)

### Constructors (exception-based)

- `rotary_encoder(config)` - Create encoder from config (includes callback)

### Acceleration Control

- `enable_acceleration(coeff)` - Enable acceleration
- `disable_acceleration()` - Disable acceleration

### Event Types

- `event_type::changed` - Encoder shaft rotated (check `event::diff` for delta)
- `event_type::btn_pressed` - Button pressed
- `event_type::btn_released` - Button released
- `event_type::btn_long_pressed` - Button held beyond long-press threshold
- `event_type::btn_clicked` - Short press completed (pressed then released)

### Configuration

Key configuration options:
- `pin_a`, `pin_b` - Encoder pins (required)
- `pin_btn` - Optional button pin (default: not connected)
- `btn_active_high` - Button active level (default: active low)
- `encoder_pins_pull_mode` - Pull mode for encoder pins A and B (default: pullup, nullopt to leave unchanged)
- `btn_pin_pull_mode` - Pull mode for button pin (default: pullup, nullopt to leave unchanged)
- `btn_dead_time` - Button debounce time (default: 10ms)
- `btn_long_press_time` - Long press threshold (default: 500ms)
- `acceleration_threshold` - Acceleration trigger interval (default: 200ms)
- `acceleration_cap` - Maximum acceleration limit (default: 4ms)
- `polling_interval` - GPIO polling interval (default: 1ms)

## Error Handling

The `make()` factory returns `idfxx::result<rotary_encoder>`. The exception-based constructor throws `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

Error codes from `idfxx::errc`:

- `invalid_arg` - Required pins (pin_a, pin_b) are not connected

## Important Notes

- **Non-copyable/move-only**: Encoder is non-copyable and move-only.
- **Automatic cleanup**: The destructor automatically stops tracking and releases resources.
- **Callback context**: Callbacks are invoked from a timer task context. They must not block or call back into the encoder API.
- **Pin requirements**: `pin_a` and `pin_b` must be connected GPIO pins. `pin_btn` is optional and defaults to not connected.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
