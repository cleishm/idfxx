# idfxx_rotary_encoder

Incremental rotary encoder driver for ESP32.

## Features

- **Rotary encoder position tracking** with event-driven callbacks
- **Acceleration support** for faster value changes when turning quickly
- **Type-safe configuration** with std::chrono durations and idfxx::gpio pins

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

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
        .callback = [](int32_t diff) {
            idfxx::log::info("encoder", "Rotated: {}", diff);
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
    .callback = [](int32_t diff) {
        idfxx::log::info("encoder", "Rotated: {}", diff);
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

### Configuration

Key configuration options:
- `pin_a`, `pin_b` - Encoder pins (required)
- `encoder_pins_pull_mode` - Pull mode for encoder pins A and B (default: pullup, nullopt to leave unchanged)
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
- **Pin requirements**: `pin_a` and `pin_b` must be connected GPIO pins.
- **Button support**: Many rotary encoders include a built-in push-button. Use [idfxx_button](../idfxx_button) to handle the button independently.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
