# idfxx_gpio

Type-safe GPIO pin management for ESP32.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__gpio.html)**

## Features

- **Type-safe GPIO pin management** with compile-time validation
- **Dual error handling** - exception-based and `result<T>` returning methods
- **Enhanced ISR handler management** with support for multiple handlers per pin
- **Constexpr GPIO constants** for all valid pins
- **Zero overhead abstractions** over ESP-IDF GPIO APIs
- **Thread-safe ISR handler** registration and removal

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_gpio:
    version: "^0.9.0"
```

Or add `idfxx_gpio` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
try {
    // Using predefined constants
    auto led = idfxx::gpio_2;

    // Configure as output
    led.set_direction(idfxx::gpio::mode::output);
    led.set_level(true);  // Set high

    // Configure as input with pull-up
    auto button = idfxx::gpio_0;
    button.set_direction(idfxx::gpio::mode::input);
    button.set_pull_mode(idfxx::gpio::pull_mode::pullup);

    // Read input level
    bool pressed = button.get_level();
    idfxx::log::info("GPIO", "Button pressed: {}", pressed);

} catch (const std::system_error& e) {
    idfxx::log::error("GPIO", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/gpio>
#include <idfxx/log>

// Using predefined constants
auto led = idfxx::gpio_2;

// Configure as output - result will be of type idfxx::result<void>
if (auto result = led.try_set_direction(idfxx::gpio::mode::output); !result) {
    idfxx::log::error("GPIO", "Failed to set direction: {}", result.error().message());
    return;
}

// Set high - result will be of type idfxx::result<void>
if (auto result = led.try_set_level(true); !result) {
    idfxx::log::error("GPIO", "Failed to set level: {}", result.error().message());
    return;
}

// Configure as input with pull-up
auto button = idfxx::gpio_0;
if (auto result = button.try_set_direction(idfxx::gpio::mode::input); !result) {
    idfxx::log::error("GPIO", "Failed to set direction: {}", result.error().message());
    return;
}

if (auto result = button.try_set_pull_mode(idfxx::gpio::pull_mode::pullup); !result) {
    idfxx::log::error("GPIO", "Failed to set pull mode: {}", result.error().message());
    return;
}

// Read input level - will return `false` if the button GPIO is invalid.
bool pressed = button.get_level();
idfxx::log::info("GPIO", "Button pressed: {}", pressed);
```

### Interrupt Handling

```cpp
// Install ISR service once at startup
idfxx::gpio::install_isr_service();

// Configure interrupt
button.set_intr_type(idfxx::gpio::intr_type::negedge);

// Add handler (supports multiple handlers per pin)
auto handle = button.isr_handler_add([]() {
    // Handle interrupt (runs in ISR context)
});

// Enable interrupt
button.intr_enable();

// Remove handler when done
button.isr_handler_remove(handle);
```

### Batch Configuration

```cpp
idfxx::gpio::config cfg{
    .mode = idfxx::gpio::mode::input,
    .pull_mode = idfxx::gpio::pull_mode::pullup,
    .intr_type = idfxx::gpio::intr_type::disable
};

idfxx::configure_gpios(cfg, idfxx::gpio_0, idfxx::gpio_1);

// or, using the result-based API
auto result = idfxx::try_configure_gpios(cfg, idfxx::gpio_0, idfxx::gpio_1); // idfxx::result<void>
if (!result) {
    idfxx::log::error("GPIO", "Failed to configure GPIOs: {}", result.error().message());
    return;
}
```

## Key Features

### Multiple ISR Handlers Per Pin

Unlike standard ESP-IDF, idfxx_gpio allows multiple interrupt handlers per pin.

With result-based API:

```cpp
auto result1 = pin.try_isr_handler_add(handler1); // idfxx::result<idfxx::gpio::isr_handle>
auto result2 = pin.try_isr_handler_add(handler2);
if (result1 && result2) {
    // Both handlers registered successfully
}
```

Or, if `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
auto handle1 = pin.isr_handler_add(handler1);
auto handle2 = pin.isr_handler_add(handler2);
// Both handlers will be called on interrupt
```

### Compile-time GPIO Validation

```cpp
// Predefined constants are validated at compile time
auto valid = idfxx::gpio_21;  // OK

// Runtime validation with result type
auto maybe_pin = idfxx::gpio::make(99);  // Returns error for invalid pin (idfxx::result<idfxx::gpio>)
```

See the [full documentation](https://cleishm.github.io/idfxx/group__idfxx__gpio.html) for complete API reference.

## Error Handling

GPIO operations use error codes from `idfxx::errc`:

- `invalid_arg` - Invalid GPIO number or configuration
- `invalid_state` - Operation not valid in current state
- `not_found` - ISR handler not found for removal

All `try_*` methods return `idfxx::result<T>`. Exception-based methods (without `try_` prefix) throw `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

## Important Notes

- **Dual API Pattern**: Methods prefixed with `try_` return `result<T>`; unprefixed methods throw exceptions (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- Call `idfxx::gpio::install_isr_service()` once before using interrupts
- ISR handlers run in interrupt context - keep them short and use IRAM-safe functions
- Multiple handlers per pin are stored in DRAM - ensure sufficient heap space
- GPIO pins used by other peripherals (SPI, I2C, etc.) should not be reconfigured

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
