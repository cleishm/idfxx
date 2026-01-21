# idfxx_lcd_touch_stmpe610

STMPE610 resistive touch controller driver.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__lcd__touch.html)**

## Features

- STMPE610 resistive touch controller driver
- Optional interrupt and reset pin support
- Touch orientation controls (swap_xy, mirror_x, mirror_y)
- Coordinate post-processing callback support

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_lcd_touch_stmpe610:
    version: "^0.9.0"
```

Or add `idfxx_lcd_touch_stmpe610` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/spi/master>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/stmpe610>
#include <idfxx/log>

try {
    using namespace frequency_literals;

    auto spi_bus = std::make_shared<idfxx::spi::master_bus>(
        idfxx::spi::host_device::spi2,
        idfxx::spi::dma_chan::ch_auto,
        idfxx::spi::bus_config {
            .mosi_io_num = idfxx::gpio_23,
            .miso_io_num = idfxx::gpio_19,
            .sclk_io_num = idfxx::gpio_18,
            .max_transfer_sz = 4096,
        }
    );

    auto touch_panel_io = std::make_shared<idfxx::lcd::panel_io>(
        spi_bus,
        idfxx::lcd::panel_io::spi_config {
            .cs_gpio = idfxx::gpio_15,  // Touch CS pin
            .dc_gpio = idfxx::gpio_2,
            .spi_mode = 0,
            .pclk_freq = 1_MHz,
            .trans_queue_depth = 10,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        }
    );

    auto touch = std::make_unique<idfxx::lcd::stmpe610>(
        touch_panel_io,
        idfxx::lcd::touch::config {
            .x_max = 240,
            .y_max = 320,
        }
    );

    idfxx::log::info("TOUCH", "Touch controller initialized successfully");

} catch (const std::system_error& e) {
    idfxx::log::error("TOUCH", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
// Create SPI bus
auto spi_bus_result = idfxx::spi::master_bus::make(
    idfxx::spi::host_device::spi2,
    idfxx::spi::dma_chan::ch_auto,
    { /* ... */ }
);
if (!spi_bus_result) {
    idfxx::log::error("TOUCH", "Failed to create SPI bus: {}", spi_bus_result.error().message());
    return;
}
auto spi_bus = std::move(*spi_bus_result);

// Create panel I/O for touch controller
auto touch_panel_io_result = idfxx::lcd::panel_io::make(
    spi_bus,
    { /* ... */ }
);
if (!touch_panel_io_result) {
    idfxx::log::error("TOUCH", "Failed to create panel I/O: {}", touch_panel_io_result.error().message());
    return;
}
auto touch_panel_io = std::move(*touch_panel_io_result);

// Create STMPE610 touch controller
auto touch_result = idfxx::lcd::stmpe610::make(
    touch_panel_io,
    { /* ... */ }
);
if (!touch_result) {
    idfxx::log::error("TOUCH", "Failed to create touch controller: {}", touch_result.error().message());
    return;
}
auto touch = std::move(*touch_result);

idfxx::log::info("TOUCH", "Touch controller initialized successfully");
```

### Using a Named Config Variable

When storing the config in a variable before passing it, use `std::move()` since the config
struct is move-only:

```cpp
idfxx::lcd::touch::config touch_config{
    .x_max = 240,
    .y_max = 320,
};

// Must use std::move() when passing a named config variable
auto touch = std::make_unique<idfxx::lcd::stmpe610>(touch_panel_io, std::move(touch_config));
```

### Sharing SPI Bus with Display

STMPE610 can share the same SPI bus as the display (using different CS pins). This example uses the exception-based API:

```cpp
using namespace frequency_literals;

// Create shared SPI bus
auto spi_bus = std::make_shared<idfxx::spi::master_bus>(
    idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_config
);

// Create panel I/O for display
idfxx::lcd::panel_io::spi_config display_io_config{
    .cs_gpio = idfxx::gpio_5,   // Display CS
    .dc_gpio = idfxx::gpio_2,
    .pclk_freq = 40_MHz,
    // ... other config
};
auto display_panel_io = std::make_shared<idfxx::lcd::panel_io>(
    spi_bus, std::move(display_io_config)
);

// Create panel I/O for touch
idfxx::lcd::panel_io::spi_config touch_io_config{
    .cs_gpio = idfxx::gpio_15,  // Touch CS (different)
    .dc_gpio = idfxx::gpio_2,
    .pclk_freq = 1_MHz,
    // ... other config
};
auto touch_panel_io = std::make_shared<idfxx::lcd::panel_io>(
    spi_bus, std::move(touch_io_config)
);

// Create display and touch
auto display = std::make_unique<idfxx::lcd::ili9341>(
    display_panel_io,
    idfxx::lcd::panel::config { /* ... */ }
);
auto touch = std::make_unique<idfxx::lcd::stmpe610>(
    touch_panel_io,
    idfxx::lcd::touch::config { /* ... */ }
);
```

### Integration with LVGL

```cpp
#include <idfxx/lcd/stmpe610>
#include <lvgl.h>

// Assumes touch_panel_io and touch_config defined as in Basic Example above
auto touch = std::make_unique<idfxx::lcd::stmpe610>(
    touch_panel_io, std::move(touch_config)
);

// Get ESP-IDF handle for LVGL integration
esp_lcd_touch_handle_t handle = touch->idf_handle();

// Use with LVGL touch driver
// (See LVGL documentation for complete integration)
```

### Using the Abstract Base Class

The `stmpe610` class inherits from `touch` base class (defined in `idfxx_lcd`):

```cpp
void initialize_touch(idfxx::lcd::touch& touch) {
    // Works with any touch implementation
    esp_lcd_touch_handle_t handle = touch.idf_handle();
    // Use with LVGL or other higher-level libraries
}

// Assumes touch_panel_io and touch_config defined as in Basic Example above
auto stmpe610 = std::make_unique<idfxx::lcd::stmpe610>(
    touch_panel_io, std::move(touch_config)
);
initialize_touch(*stmpe610);
```

## API Overview

### `stmpe610`

**Creation:**
- `make(panel_io, config)` - Create STMPE610 controller (result-based)
- `stmpe610(panel_io, config)` - Constructor (exception-based, if enabled)

**Properties:** (inherited from `touch` base class in `idfxx_lcd`)
- `idf_handle()` - Get ESP-IDF touch handle

**Lifetime:**
- Non-copyable and non-movable
- Destructor automatically cleans up touch resources
- Keeps `panel_io` alive via `std::shared_ptr`

**Specifications:**
- Type: Resistive touch
- Interface: SPI
- Typical clock: 1-2 MHz
- Commonly paired with: ILI9341 displays (240x320)

## Configuration

### `touch::config`

Configuration structure defined in `idfxx_lcd`:

- `x_max` - Maximum X coordinate (display width)
- `y_max` - Maximum Y coordinate (display height)
- `rst_gpio` - Reset pin GPIO (`idfxx::gpio::nc()` if not used)
- `int_gpio` - Interrupt pin GPIO (`idfxx::gpio::nc()` if not used)
- `levels.reset` - Active level for reset pin
- `levels.interrupt` - Active level for interrupt pin
- `flags.swap_xy` - Swap X and Y coordinates
- `flags.mirror_x` - Mirror X axis
- `flags.mirror_y` - Mirror Y axis
- `process_coordinates` - Optional callback for coordinate post-processing

### Typical STMPE610 Configuration

```cpp
idfxx::lcd::touch::config touch_config{
    .x_max = 240,
    .y_max = 320,
    // rst_gpio and int_gpio default to gpio::nc()
    .flags{
        .swap_xy = 0,        // Adjust based on orientation
        .mirror_x = 0,       // Adjust based on orientation
        .mirror_y = 0,       // Adjust based on orientation
    },
};
```

## Touch Orientation

The `swap_xy`, `mirror_x`, and `mirror_y` flags should match your display orientation:

**Portrait (240x320):**
```cpp
.flags{
    .swap_xy = 0,
    .mirror_x = 0,
    .mirror_y = 0,
}
```

**Landscape (320x240):**
```cpp
.flags{
    .swap_xy = 1,
    .mirror_x = 1,
    .mirror_y = 0,
}
```

Adjust these values to match your specific display module and mounting orientation.

## Important Notes

- **Dual API Pattern**: The component provides both result-based and exception-based APIs
  - `make()` returns `result<std::unique_ptr<stmpe610>>` (always available)
  - Constructor throws `std::system_error` (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- The `panel_io` must be created before the touch controller
- The touch controller keeps `panel_io` alive via `std::shared_ptr`
- STMPE610 typically uses much slower SPI clock than displays (1-2 MHz vs 10-40 MHz)
- Touch and display can share SPI bus but need different CS pins
- Reset and interrupt pins are optional (default to `idfxx::gpio::nc()`)
- Orientation flags should match your display orientation
- The touch handle can be used with LVGL or other touch input libraries
- The `stmpe610` class inherits from the `touch` base class provided by `idfxx_lcd`
- Polling mode (no interrupt) is simpler but uses more CPU
- Interrupt mode requires wiring and configuration but is more efficient

## Using with LVGL

The touch handle integrates with LVGL's input device system. Refer to LVGL documentation for details on:
- Creating input device descriptors
- Reading touch coordinates
- Handling touch events
- Calibration (if needed)

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
