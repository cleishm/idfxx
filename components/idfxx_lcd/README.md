# idfxx_lcd

LCD panel I/O interface for SPI- and I2C-based displays.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__lcd.html)**

## Features

- Panel I/O lifecycle management
- SPI- and I2C-based communication interfaces
- Integration with `idfxx_spi` and `idfxx_i2c` buses
- Panel base class with drawing, orientation, and inversion controls
- `mono_framebuffer` helper for monochrome (1-bpp, page-packed) displays
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
    version: "^2.1.0"
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
    idfxx::spi::master_bus spi_bus(
        idfxx::spi::host_device::spi2,
        idfxx::spi::dma_chan::ch_auto,
        idfxx::spi::bus_config {
            .mosi = idfxx::gpio_23,
            .miso = idfxx::gpio_19,
            .sclk = idfxx::gpio_18,
            .max_transfer_sz = 4096,
        }
    );

    idfxx::lcd::panel_io panel_io(
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

### I2C Displays

Monochrome OLEDs and other I2C-connected panels use the same `panel_io` class with an
`i2c_config` and an `idfxx::i2c::master_bus`:

```cpp
#include <idfxx/i2c/master>
#include <idfxx/lcd/panel_io>

using namespace frequency_literals;

idfxx::i2c::master_bus i2c_bus(idfxx::i2c::port::i2c0, {
    .sda = idfxx::gpio_21,
    .scl = idfxx::gpio_22,
    .frequency = 400_kHz,
});

idfxx::lcd::panel_io panel_io(
    i2c_bus,
    idfxx::lcd::panel_io::i2c_config {
        .device_address = 0x3C,     // typical SSD1306 address
        .scl_speed = 400_kHz,
        .control_phase_bytes = 1,   // controller-specific; these values suit an SSD1306
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    }
);
```

Panel driver components typically provide a pre-filled configuration for their
controller (e.g. `idfxx_lcd_ssd1306`'s `ssd1306::i2c_io_config()`), so the raw
struct is only needed for panels without one.

### Monochrome Framebuffer

For monochrome (1 bit per pixel) displays such as SSD1306 OLEDs, `mono_framebuffer`
provides an in-memory drawing surface in the page-packed layout those controllers
expect:

```cpp
#include <idfxx/lcd/mono_framebuffer>

idfxx::lcd::mono_framebuffer fb(display.width(), display.height());

fb.set_pixel(10, 20, true);
fb.flush(display);              // push the full frame to the panel

fb.set_pixel(10, 20, false);
fb.flush_rows(display, 16, 24); // partial update: only rows 16-23

fb.set_pixel(10, 20, true);
fb.flush_region(display, 10, 16, 11, 24); // partial update: one column of one page
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
idfxx::lcd::panel_io panel_io(spi_bus, std::move(io_config));
```

### With Display Panel

The `panel_io` is typically passed to LCD panel drivers.

```cpp
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/ili9341>

// Create SPI bus and panel I/O (as shown above)
auto spi_bus = /* ... */;
idfxx::lcd::panel_io panel_io(
    spi_bus, idfxx::lcd::panel_io::spi_config{ /* ... */ });

// Create ILI9341 display using the panel I/O
idfxx::lcd::panel::config panel_config{
    .reset_gpio = idfxx::gpio_4,
    .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,
    .bits_per_pixel = 16,
};

idfxx::lcd::ili9341 display(panel_io, std::move(panel_config));
```

### With Touch Controller

The same SPI bus can be shared with touch controllers (using different CS pins and separate `panel_io` instances).

```cpp
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/stmpe610>

// Create a separate panel I/O for the touch controller
idfxx::lcd::panel_io touch_panel_io(
    spi_bus, idfxx::lcd::panel_io::spi_config{ /* ... */ });

idfxx::lcd::touch::config touch_config{
    .x_max = 240,
    .y_max = 320,
};

idfxx::lcd::stmpe610 touch(touch_panel_io, std::move(touch_config));
```

## API Overview

### `panel_io`

**Creation:**
- `make(spi_bus, config)` / `make(i2c_bus, config)` - Create panel I/O (result-based)
- `panel_io(spi_bus, config)` / `panel_io(i2c_bus, config)` - Constructors (exception-based, if enabled)

**Properties:**
- `idf_handle()` - Get ESP-IDF panel I/O handle

**Lifetime:**
- Destructor automatically calls `esp_lcd_panel_io_del()`
- Non-copyable and move-only
- Takes the bus by reference; the caller must ensure the bus outlives the panel I/O

### `panel` (abstract base class)

Concrete drivers (e.g. `idfxx_lcd_ili9341`, `idfxx_lcd_ssd1306`) inherit from `panel`:

- `draw_bitmap(x_start, y_start, x_end, y_end, data)` / `try_draw_bitmap(...)` - Draw pixel
  data to an end-exclusive region (buffer layout is panel-specific)
- `invert_color(invert)` / `try_invert_color(invert)` - Invert display colors
- `swap_xy(swap)` / `try_swap_xy(swap)` - Swap X and Y axes
- `mirror(mirror_x, mirror_y)` / `try_mirror(...)` - Mirror the display
- `display_on(on)` / `try_display_on(on)` - Turn the display on or off
- `idf_handle()` - Get ESP-IDF panel handle

### `mono_framebuffer`

In-memory framebuffer for monochrome (1-bpp) displays, stored page-packed (each byte is
8 vertically adjacent pixels; the byte for pixel (x, y) is `(y / 8) * width + x`, bit `y % 8`):

- `mono_framebuffer(width, height)` / `make(width, height)` - Create (height must be a multiple of 8)
- `set_pixel(x, y, on)` / `get_pixel(x, y)` - Pixel access (out-of-range coordinates are ignored)
- `fill(on)` / `clear()` - Fill or clear the whole framebuffer
- `data()` - Raw page-packed bytes
- `flush(panel)` / `try_flush(panel)` - Draw the full frame to a panel
- `flush_rows(panel, y_start, y_end)` / `try_flush_rows(...)` - Draw a horizontal band,
  expanded outward to page boundaries
- `flush_region(panel, x_start, y_start, x_end, y_end)` / `try_flush_region(...)` - Draw a
  rectangular region: full-width regions transfer in a single draw, narrower ones one
  draw per page

Panels report their native dimensions via `panel::width()` / `panel::height()`, so a
matching framebuffer is simply `mono_framebuffer fb(display.width(), display.height())`.

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

### `panel_io::i2c_config`

- `device_address` - 7-bit I2C device address (e.g. 0x3C for a typical SSD1306)
- `scl_speed` - I2C SCL frequency (`freq::hertz`)
- `on_color_transfer_done` - Optional callback invoked when color data transfer completes
- `control_phase_bytes` - Bytes used to encode control information (e.g. D/C selection)
- `dc_bit_offset` - Offset of the D/C selection bit in the control phase
- `lcd_cmd_bits` - Command bits (typically 8)
- `lcd_param_bits` - Parameter bits (typically 8)
- `flags` - Extra flags to fine-tune the I2C device:
  - `dc_low_on_data` - DC bit = 0 indicates data transfer (vice versa otherwise)
  - `disable_control_phase` - Don't use the control phase

The control-phase framing is a property of the panel controller; panel driver
components typically provide a pre-filled configuration (e.g. `ssd1306::i2c_io_config()`)
or document the values they require.

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

- The bus (SPI or I2C) must be created before the panel I/O
- The caller must ensure the bus outlives the panel I/O
- Multiple panel I/O instances can share the same SPI bus (with different CS pins) or
  I2C bus (with different device addresses)
- For SPI, the `dc_gpio` (Data/Command) is required for most LCD controllers; for I2C,
  the D/C selection is encoded in the control phase (`control_phase_bytes`/`dc_bit_offset`)
- Clock frequency (`pclk_freq`/`scl_speed`) affects display performance
- Transaction queue depth affects how many operations can be queued
- This component provides the I/O layer and panel base class; use a concrete driver
  (e.g. `idfxx_lcd_ili9341`, `idfxx_lcd_ssd1306`) or `idfxx_lcd_touch` for device control

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
