# idfxx_lcd_ili9341

ILI9341 LCD panel driver (240x320).

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__lcd.html)**

## Features

- ILI9341 display controller driver (240x320 displays)
- Display orientation and mirroring controls
- Integration with `idfxx_lcd` panel I/O

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- `idfxx_lcd` component (provides `panel` base class and `panel_io`)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_lcd_ili9341:
    version: "^0.9.0"
```

Or add `idfxx_lcd_ili9341` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/spi/master>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/ili9341>
#include <idfxx/log>

try {
    using namespace frequency_literals;

    // Create SPI bus
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

    // Create panel I/O
    auto panel_io = std::make_shared<idfxx::lcd::panel_io>(
        spi_bus,
        idfxx::lcd::panel_io::spi_config {
            .cs_gpio = idfxx::gpio_5,
            .dc_gpio = idfxx::gpio_2,
            .spi_mode = 0,
            .pclk_freq = 40_MHz,
            .trans_queue_depth = 10,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        }
    );

    // Create ILI9341 panel
    auto panel = std::make_unique<idfxx::lcd::ili9341>(
        panel_io,
        idfxx::lcd::panel::config {
            .reset_gpio = idfxx::gpio_4,
            .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,
            .bits_per_pixel = 16,
        }
    );

    // Operations throw on error
    panel->swap_xy(true);
    panel->mirror(false, true);
    panel->display_on(true);

} catch (const std::system_error& e) {
    idfxx::log::error("LCD", "Error: {}", e.what());
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
    idfxx::log::error("LCD", "Failed to create SPI bus: {}", spi_bus_result.error().message());
    return;
}
auto spi_bus = std::move(*spi_bus_result);

// Create panel I/O
auto panel_io_result = idfxx::lcd::panel_io::make(
    spi_bus,
    { /* ... */ }
);
if (!panel_io_result) {
    idfxx::log::error("LCD", "Failed to create panel I/O: {}", panel_io_result.error().message());
    return;
}
auto panel_io = std::move(*panel_io_result);

// Create ILI9341 panel
auto panel_result = idfxx::lcd::ili9341::make(
    panel_io,
    { /* ... */ }
);
if (!panel_result) {
    idfxx::log::error("LCD", "Failed to create panel: {}", panel_result.error().message());
    return;
}
auto panel = std::move(*panel_result);

// Initialize display
panel->try_display_on(true);

idfxx::log::info("LCD", "Display initialized successfully");
```

### Using a Named Config Variable

When storing the config in a variable before passing it, use `std::move()` since the config
struct is move-only:

```cpp
idfxx::lcd::panel::config panel_config{
    .reset_gpio = idfxx::gpio_4,
    .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,
    .bits_per_pixel = 16,
};

// Must use std::move() when passing a named config variable
auto panel = std::make_unique<idfxx::lcd::ili9341>(panel_io, std::move(panel_config));
```

### Display Control

```cpp
// Assumes panel_io and panel_config defined as in Basic Example above
auto panel = std::make_unique<idfxx::lcd::ili9341>(panel_io, std::move(panel_config));

// Swap X and Y axes for portrait/landscape
panel->swap_xy(true);

// Mirror display
panel->mirror(true, false);  // Mirror X, don't mirror Y

// Turn display on/off
panel->display_on(true);
```

### Display Orientation Examples

```cpp
// Portrait (default) - 240 wide x 320 tall
panel->swap_xy(false);
panel->mirror(false, false);

// Landscape - 320 wide x 240 tall
panel->swap_xy(true);
panel->mirror(true, false);

// Portrait (flipped)
panel->swap_xy(false);
panel->mirror(true, true);

// Landscape (flipped)
panel->swap_xy(true);
panel->mirror(false, true);
```

### Integration with LVGL

```cpp
#include <idfxx/lcd/ili9341>
#include <lvgl.h>

// Assumes panel_io and panel_config defined as in Basic Example above
auto panel = std::make_unique<idfxx::lcd::ili9341>(panel_io, std::move(panel_config));

// Get ESP-IDF handle for LVGL integration
esp_lcd_panel_handle_t handle = panel->idf_handle();

// Use with LVGL driver
// (See LVGL documentation for complete integration)
```

## API Overview

### `ili9341`

**Creation:**
- `make(panel_io, config)` - Create ILI9341 panel (result-based)
- `ili9341(panel_io, config)` - Constructor (exception-based, if enabled)

**Display Control:** (inherited from `panel` base class in `idfxx_lcd`)
- `try_swap_xy(swap)` - Swap X and Y axes (result-based)
- `try_mirror(mirrorX, mirrorY)` - Mirror display (result-based)
- `try_display_on(on)` - Turn display on/off (result-based)
- `swap_xy(swap)` - Swap X and Y axes (exception-based, if enabled)
- `mirror(mirrorX, mirrorY)` - Mirror display (exception-based, if enabled)
- `display_on(on)` - Turn display on/off (exception-based, if enabled)

**Properties:**
- `idf_handle()` - Get ESP-IDF panel handle

**Lifetime:**
- Non-copyable and non-movable
- Destructor automatically cleans up panel resources

**Specifications:**
- Resolution: 240x320 pixels
- Color depth: 16-bit (RGB565)
- Interface: SPI

## Configuration

### `panel::config`

Configuration structure defined in `idfxx_lcd`:

- `reset_gpio` - Reset pin GPIO (`idfxx::gpio::nc()` if not used)
- `rgb_element_order` - RGB element order (`idfxx::lcd::rgb_element_order::rgb` or `::bgr`)
- `data_endian` - RGB data endian (`idfxx::lcd::rgb_data_endian::big` or `::little`)
- `bits_per_pixel` - Color depth (typically 16 for RGB565)
- `flags.reset_active_high` - Set if panel reset is high level active
- `vendor_config` - Vendor-specific configuration (optional)

### Typical ILI9341 Configuration

```cpp
idfxx::lcd::panel::config panel_config{
    .reset_gpio = idfxx::gpio_4,                              // Or idfxx::gpio::nc()
    .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,  // ILI9341 typically uses BGR
    .bits_per_pixel = 16,                                     // RGB565
};
```

## Important Notes

- **Dual API Pattern**: The component provides both result-based and exception-based APIs
  - `make()` returns `result<std::unique_ptr<ili9341>>` (always available)
  - Constructor throws `std::system_error` (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- The `panel_io` must be created before creating the panel
- The panel keeps `panel_io` alive via `std::shared_ptr`
- Reset pin is optional (use `idfxx::gpio::nc()` if not connected)
- RGB element order depends on specific display module (try both if colors are wrong)
- 16-bit color depth (RGB565) is most common for ILI9341
- Call `display_on(true)` after initialization to turn on the display
- The panel handle can be used with ESP-IDF LCD API or LVGL
- The `ili9341` class inherits from the `panel` base class provided by `idfxx_lcd`

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
