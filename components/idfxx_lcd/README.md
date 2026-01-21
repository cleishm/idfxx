# idfxx_lcd

LCD panel I/O interface for SPI-based displays.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__lcd.html)**

## Features

- Panel I/O lifecycle management
- SPI-based communication interface
- Integration with `idfxx_spi` bus
- Foundation for LCD panel and touch controller drivers

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_lcd:
    version: "^0.9.0"
```

Or add `idfxx_lcd` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/spi/master>
#include <idfxx/lcd/panel_io>
#include <idfxx/log>

using namespace frequency_literals;

try {
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

    // panel_io can now be used to create LCD panels or touch controllers

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

// panel_io can now be used to create LCD panels or touch controllers
```

### Using a Named Config Variable

When storing the config in a variable before passing it, use `std::move()` since the config
struct is move-only:

```cpp
idfxx::lcd::panel_io::spi_config io_config{
    .cs_gpio = idfxx::gpio_5,
    .dc_gpio = idfxx::gpio_2,
    .spi_mode = 0,
    .pclk_freq = 40_MHz,
    .trans_queue_depth = 10,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
};

// Must use std::move() when passing a named config variable
auto panel_io = std::make_shared<idfxx::lcd::panel_io>(spi_bus, std::move(io_config));
```

### With Display Panel

The `panel_io` is typically passed to LCD panel drivers.

```cpp
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/ili9341>

// Create SPI bus and panel I/O (as shown above)
auto spi_bus = /* ... */;
auto panel_io = std::make_shared<idfxx::lcd::panel_io>(
    spi_bus, idfxx::lcd::panel_io::spi_config{ /* ... */ });

// Create ILI9341 display using the panel I/O
idfxx::lcd::panel::config panel_config{
    .reset_gpio = idfxx::gpio_4,
    .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,
    .bits_per_pixel = 16,
};

auto display = std::make_unique<idfxx::lcd::ili9341>(panel_io, std::move(panel_config));
```

### With Touch Controller

The same `panel_io` can be shared with touch controllers.

```cpp
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/touch/stmpe610>

// Use the same panel I/O for touch controller
idfxx::lcd::touch::config touch_config{
    .x_max = 240,
    .y_max = 320,
};

auto touch = std::make_unique<idfxx::lcd::touch::stmpe610>(panel_io, touch_config);
```

## API Overview

### `panel_io`

**Creation:**
- `make(spi_bus, config)` - Create panel I/O (result-based)
- `panel_io(spi_bus, config)` - Constructor (exception-based, if enabled)

**Properties:**
- `idf_handle()` - Get ESP-IDF panel I/O handle

**Lifetime:**
- Destructor automatically calls `esp_lcd_panel_io_del()`
- Non-copyable and non-movable
- Keeps SPI bus alive via `std::shared_ptr`

## Configuration

### `panel_io::spi_config`

- `cs_gpio` - Chip select GPIO (`idfxx::gpio`)
- `dc_gpio` - Data/Command GPIO (`idfxx::gpio`)
- `spi_mode` - SPI mode (0-3)
- `pclk_freq` - Pixel clock frequency (`freq::hertz`)
- `trans_queue_depth` - Transaction queue size
- `on_color_transfer_done` - Optional callback invoked when color data transfer completes
- `lcd_cmd_bits` - Command bits (typically 8)
- `lcd_param_bits` - Parameter bits (typically 8)
- `cs_enable_pretrans` - SPI bit-cycles CS activates before transmission (0-16)
- `cs_enable_posttrans` - SPI bit-cycles CS stays active after transmission (0-16)
- `flags` - Extra flags to fine-tune the SPI device:
  - `dc_high_on_cmd` - DC level = 1 indicates command transfer
  - `dc_low_on_data` - DC level = 0 indicates color data transfer
  - `dc_low_on_param` - DC level = 0 indicates parameter transfer
  - `octal_mode` - Transmit with 8 data lines (Intel 8080 timing)
  - `quad_mode` - Transmit with 4 data lines
  - `sio_mode` - Read and write through single data line (MOSI)
  - `lsb_first` - Transmit LSB bit first
  - `cs_high_active` - CS line is high active

### Typical Values

For ILI9341 displays:
```cpp
using namespace frequency_literals;

idfxx::lcd::panel_io::spi_config io_config{
    .cs_gpio = idfxx::gpio_5,
    .dc_gpio = idfxx::gpio_2,
    .spi_mode = 0,
    .pclk_freq = 40_MHz,
    .trans_queue_depth = 10,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
};
```

## Important Notes

- The SPI bus must be created before the panel I/O
- Panel I/O keeps the SPI bus alive via `std::shared_ptr`
- Multiple panel I/O instances can share the same SPI bus (with different CS pins)
- The `dc_gpio` (Data/Command) is required for most LCD controllers
- Clock frequency (`pclk_freq`) affects display performance
- Transaction queue depth affects how many operations can be queued
- This component provides the I/O layer; use `idfxx_lcd_panel` or `idfxx_lcd_touch` for actual device control

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
