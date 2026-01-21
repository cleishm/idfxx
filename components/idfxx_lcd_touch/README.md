# idfxx_lcd_touch

LCD touch controller interface.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__lcd__touch.html)**

## Features

- Abstract base class for touch controller implementations

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_lcd_touch:
    version: "^0.9.0"
```

Or add `idfxx_lcd_touch` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

This component provides the abstract base class. Use concrete implementations like `idfxx_lcd_touch_stmpe610` for actual touch controllers.

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/lcd/touch>
#include <idfxx/lcd/stmpe610>
#include <idfxx/log>

// Create touch controller using a concrete implementation
auto touch = std::make_shared<idfxx::lcd::stmpe610>(
    panel_io,
    idfxx::lcd::touch::config {
        .x_max = 240,
        .y_max = 320,
        .rst_gpio = idfxx::gpio_4,
        .int_gpio = idfxx::gpio::nc(),
        .levels{
            .reset = 0,
            .interrupt = 0,
        },
        .flags{
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    }
);

// Access ESP-IDF handle for lower-level operations
esp_lcd_touch_handle_t handle = touch->idf_handle();
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
// Create touch controller using a concrete implementation
auto touch_result = idfxx::lcd::stmpe610::make(
    panel_io,
    { /* ... */ }
);
if (!touch_result) {
    idfxx::log::error("TOUCH", "Failed to create touch: {}", touch_result.error().message());
    return;
}
auto touch = std::move(*touch_result);

// Access ESP-IDF handle for lower-level operations
esp_lcd_touch_handle_t handle = touch->idf_handle();
```

### Using a Named Config Variable

When storing the config in a variable before passing it, use `std::move()` since the config
struct is move-only:

```cpp
idfxx::lcd::touch::config touch_config{
    .x_max = 240,
    .y_max = 320,
    .rst_gpio = idfxx::gpio_4,
    .int_gpio = idfxx::gpio::nc(),
    .levels{
        .reset = 0,
        .interrupt = 0,
    },
    .flags{
        .swap_xy = false,
        .mirror_x = false,
        .mirror_y = false,
    },
};

// Must use std::move() when passing a named config variable
auto touch = std::make_shared<idfxx::lcd::stmpe610>(panel_io, std::move(touch_config));
```

### Coordinate Transformation

The base class supports automatic coordinate transformation:

```cpp
idfxx::lcd::touch::config touch_config{
    .x_max = 240,
    .y_max = 320,
    .flags{
        .swap_xy = true,    // Swap X and Y coordinates
        .mirror_x = true,   // Mirror X coordinate (x_new = x_max - x)
        .mirror_y = false,  // Don't mirror Y coordinate
    },
};
```

### Custom Coordinate Processing

Apply custom transformations after reading coordinates:

```cpp
idfxx::lcd::touch::config touch_config{
    .x_max = 240,
    .y_max = 320,
    .process_coordinates = [](uint16_t* x, uint16_t* y, uint16_t* strength,
                              uint8_t* point_num, uint8_t max_point_num) {
        // Apply calibration offset
        for (uint8_t i = 0; i < *point_num; i++) {
            x[i] += 10;
            y[i] -= 5;
        }

        // Filter weak touches
        uint8_t filtered = 0;
        for (uint8_t i = 0; i < *point_num; i++) {
            if (strength[i] > 50) {
                x[filtered] = x[i];
                y[filtered] = y[i];
                strength[filtered] = strength[i];
                filtered++;
            }
        }
        *point_num = filtered;
    },
};
```

### Interrupt-Driven Touch

Configure interrupt-driven touch detection:

```cpp
idfxx::lcd::touch::config touch_config{
    .x_max = 240,
    .y_max = 320,
    .int_gpio = idfxx::gpio_36,  // Interrupt pin
    .levels{
        .interrupt = 0,  // Active low interrupt
    },
};

// The touch controller will trigger interrupts on touch events
// (Interrupt handling is implementation-specific)
```

## API Overview

### `touch` (Abstract Base Class)

**Pure Virtual Methods:**
- `idf_handle()` - Get ESP-IDF touch handle

**Configuration:**
- `config` - Touch controller configuration structure

**Lifetime:**
- Pure abstract class, instantiate via concrete implementations
- Implementations typically manage ESP-IDF handle lifecycle

## Configuration

### `touch::config`

**Coordinate Range:**
- `x_max` - Maximum X coordinate (used for mirroring)
- `y_max` - Maximum Y coordinate (used for mirroring)

**GPIO Pins:**
- `rst_gpio` - Reset pin (default: `gpio::nc()`)
- `int_gpio` - Interrupt pin (default: `gpio::nc()`)

**Signal Levels:**
- `levels.reset` - Logic level during reset (0 or 1)
- `levels.interrupt` - Active interrupt level (0 or 1)

**Coordinate Transformation:**
- `flags.swap_xy` - Swap X and Y coordinates after reading
- `flags.mirror_x` - Mirror X coordinate (`x_new = x_max - x`)
- `flags.mirror_y` - Mirror Y coordinate (`y_new = y_max - y`)

**Custom Processing:**
- `process_coordinates` - Optional callback for custom coordinate transformations

### Typical Values

For resistive touch screens:
```cpp
idfxx::lcd::touch::config touch_config{
    .x_max = 240,
    .y_max = 320,
    .rst_gpio = idfxx::gpio::nc(),
    .int_gpio = idfxx::gpio::nc(),
    .levels{ .reset = 0, .interrupt = 0 },
    .flags{ .swap_xy = false, .mirror_x = false, .mirror_y = false },
};
```

For capacitive touch screens:
```cpp
idfxx::lcd::touch::config touch_config{
    .x_max = 320,
    .y_max = 480,
    .rst_gpio = idfxx::gpio_4,
    .int_gpio = idfxx::gpio_36,
    .levels{ .reset = 0, .interrupt = 0 },
    .flags{ .swap_xy = true, .mirror_x = false, .mirror_y = true },
};
```

## Implementing Custom Touch Controllers

Extend the `touch` base class to create custom implementations:

```cpp
class my_touch_controller : public idfxx::lcd::touch {
public:
    static result<std::unique_ptr<my_touch_controller>> make(
        std::shared_ptr<idfxx::lcd::panel_io> panel_io,
        const config& cfg);

    esp_lcd_touch_handle_t idf_handle() const override {
        return handle_;
    }

private:
    esp_lcd_touch_handle_t handle_;
};
```

## Important Notes

- `idfxx::lcd::touch` is an abstract base class; use concrete implementations for actual touch controllers
- Coordinate transformations (swap, mirror) are applied by ESP-IDF automatically
- Custom `process_coordinates` callback runs after automatic transformations
- Reset and interrupt pins are optional (use `gpio::nc()` if not needed)
- Touch coordinates are device-specific; verify orientation matches your display

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
