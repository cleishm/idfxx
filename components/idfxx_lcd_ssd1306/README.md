# idfxx_lcd_ssd1306

SSD1306 monochrome OLED panel driver (128x64 / 128x32).

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__lcd.html)**

## Features

- SSD1306 display controller driver (128x64 and 128x32 displays)
- I2C connectivity via the shared `panel_io` class (SPI-capable modules work with an SPI `panel_io`)
- Display orientation, mirroring, and color-inversion controls
- Pairs with `idfxx::lcd::mono_framebuffer` for pixel-level drawing

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- `idfxx_lcd` component (provides `panel` base class, `panel_io`, and `mono_framebuffer`)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_lcd_ssd1306:
    version: "^1.0.0"
```

Or add `idfxx_lcd_ssd1306` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/i2c/master>
#include <idfxx/lcd/mono_framebuffer>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/ssd1306>
#include <idfxx/log>

try {
    using namespace frequency_literals;

    // Create I2C bus
    idfxx::i2c::master_bus i2c_bus(idfxx::i2c::port::i2c0, {
        .sda = idfxx::gpio_21,
        .scl = idfxx::gpio_22,
        .frequency = 400_kHz,
    });

    // Create panel I/O (i2c_io_config() pre-fills the SSD1306 I2C framing)
    idfxx::lcd::panel_io panel_io(i2c_bus, idfxx::lcd::ssd1306::i2c_io_config());

    // Create SSD1306 panel
    idfxx::lcd::ssd1306 display(
        panel_io,
        idfxx::lcd::ssd1306::config {
            .height = 64,  // or 32 for 128x32 modules
        }
    );

    // The display starts off; turn it on before drawing
    display.display_on(true);

    // Draw via a monochrome framebuffer
    idfxx::lcd::mono_framebuffer fb(display.width(), display.height());
    fb.set_pixel(10, 20, true);
    fb.flush(display);

} catch (const std::system_error& e) {
    idfxx::log::error("OLED", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
// Create I2C bus
auto i2c_bus_result = idfxx::i2c::master_bus::make(idfxx::i2c::port::i2c0, { /* ... */ });
if (!i2c_bus_result) {
    idfxx::log::error("OLED", "Failed to create I2C bus: {}", i2c_bus_result.error().message());
    return;
}
auto i2c_bus = std::move(*i2c_bus_result);

// Create panel I/O
auto panel_io_result = idfxx::lcd::panel_io::make(i2c_bus, idfxx::lcd::ssd1306::i2c_io_config());
if (!panel_io_result) {
    idfxx::log::error("OLED", "Failed to create panel I/O: {}", panel_io_result.error().message());
    return;
}
auto panel_io = std::move(*panel_io_result);

// Create SSD1306 panel
auto display_result = idfxx::lcd::ssd1306::make(panel_io, {.height = 64});
if (!display_result) {
    idfxx::log::error("OLED", "Failed to create panel: {}", display_result.error().message());
    return;
}
auto display = std::move(*display_result);

// Turn on the display
if (auto res = display.try_display_on(true); !res) {
    idfxx::log::error("OLED", "Failed to enable display: {}", res.error().message());
}
```

### Display Control

```cpp
// Invert colors (lit pixels become dark and vice versa)
display.invert_color(true);

// Mirror the display (affects subsequent draws only)
display.mirror(true, false);

// Swap X and Y axes (affects subsequent draws only)
display.swap_xy(true);

// Turn display on/off
display.display_on(true);
```

### Partial Updates

`mono_framebuffer::flush_rows` redraws only a horizontal band, and
`mono_framebuffer::flush_region` narrows the transfer further to a column
range, which keeps I2C traffic low for small changes:

```cpp
fb.set_pixel(64, 20, true);
fb.flush_rows(display, 16, 24);           // only page 2 (rows 16-23) is transferred
fb.flush_region(display, 64, 20, 65, 21); // only one byte of page 2 is transferred
```

## API Overview

### `ssd1306`

**Creation:**
- `make(panel_io, config)` - Create SSD1306 panel (result-based)
- `ssd1306(panel_io, config)` - Constructor (exception-based, if enabled)
- `i2c_io_config(device_address = 0x3C, scl_speed = 400 kHz)` - Static helper returning a
  `panel_io::i2c_config` pre-filled with the I2C framing the SSD1306 requires

**Display Control:** (inherited from `panel` base class in `idfxx_lcd`)
- `try_draw_bitmap(xStart, yStart, xEnd, yEnd, data)` - Draw page-packed pixel data (result-based)
- `try_invert_color(invert)` - Invert display colors (result-based)
- `try_swap_xy(swap)` - Swap X and Y axes (result-based)
- `try_mirror(mirrorX, mirrorY)` - Mirror display (result-based)
- `try_display_on(on)` - Turn display on/off (result-based)
- `draw_bitmap(...)`, `invert_color(...)`, `swap_xy(...)`, `mirror(...)`, `display_on(...)` -
  Exception-based equivalents (if enabled)

**Properties:**
- `width()` - Display width in pixels, from the `panel` base class (always 128)
- `height()` - Display height in pixels, from the `panel` base class (64 or 32, as configured)
- `idf_handle()` - Get ESP-IDF panel handle

**Lifetime:**
- Non-copyable and move-only
- Destructor automatically cleans up panel resources

**Specifications:**
- Resolution: 128x64 or 128x32 pixels
- Color depth: 1-bit monochrome (page-packed)
- Interface: I2C (typical) or SPI

## Configuration

### `ssd1306::config`

- `reset_gpio` - Reset pin GPIO (`idfxx::gpio::nc()` if not used; most I2C modules have no reset pin)
- `height` - Display height in pixels: 64 (default) or 32
- `reset_active_level` - Active level for the panel reset signal (`gpio::level::low` or `gpio::level::high`)

### Panel I/O Configuration

`ssd1306::i2c_io_config()` returns a `panel_io::i2c_config` with the controller's
required framing filled in (a one-byte control phase with the D/C selection in
bit 6, and 8-bit commands and parameters); only the wiring-specific values are
parameters:

```cpp
using namespace frequency_literals;

// Defaults: address 0x3C, 400 kHz
auto io_config = idfxx::lcd::ssd1306::i2c_io_config();

// Module strapped to the alternate address, on a slow bus
auto alt_config = idfxx::lcd::ssd1306::i2c_io_config(0x3D, 100_kHz);
```

## Error Handling

- `make()` returns `idfxx::errc::invalid_arg` if `config.height` is not 64 or 32
- Panel operations return/throw errors propagated from the underlying I/O transfer
- Operations on a default-state (e.g. moved-from) panel return `idfxx::errc::invalid_state`

## Important Notes

- **The display starts off**: call `display_on(true)` after construction, or the panel
  stays dark even though drawing succeeds
- The typical I2C address is **0x3C** (the `i2c_io_config()` default); some modules are
  strapped to 0x3D
- OLED panels **age with use**: pixels dim in proportion to their cumulative on-time, so
  static content shown for long periods leaves ghost images (burn-in). Blank the screen or
  call `display_on(false)` when idle, and avoid keeping fixed content lit for days at a time
- Pixel data is **page-packed 1-bpp**: each byte holds 8 vertically adjacent pixels
  (bit 0 topmost) and pages span the full width — the same layout
  `idfxx::lcd::mono_framebuffer` produces
- Row coordinates passed to `draw_bitmap` must be **page-aligned** (multiples of 8);
  `mono_framebuffer::flush_rows` handles this rounding for you
- `swap_xy` and `mirror` only affect subsequent draws — redraw the frame after changing them
- The `panel_io` must be created before the panel, and must outlive it
- The `ssd1306` class inherits from the `panel` base class provided by `idfxx_lcd`

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
