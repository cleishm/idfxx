# idfxx_lcd

LCD panel interface, color types, and framebuffers for SPI- and I2C-based displays.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__lcd.html)**

## Features

- Panel base class with drawing, orientation, and inversion controls
- `mono_framebuffer` helper for monochrome (1-bpp, page-packed) displays
- `rgb565` color type and `rgb565_framebuffer` helper for 16-bpp color
  displays, with offset flushes for band-at-a-time rendering
- Foundation for LCD panel and touch controller drivers

The I/O layer for SPI- and I2C-connected panels is provided by the
[`idfxx_panel_io`](../idfxx_panel_io) component (`idfxx::panel_io`).
`idfxx::lcd::panel_io` remains available as a deprecated alias.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_lcd:
    version: "^2.2.0"
```

Or add `idfxx_lcd` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### With a Display Panel

Concrete panel drivers (e.g. `idfxx_lcd_ili9341`, `idfxx_lcd_ssd1306`) derive from
`idfxx::lcd::panel` and are created from an `idfxx::panel_io`:

```cpp
#include <idfxx/panel_io>
#include <idfxx/lcd/ili9341>

using namespace frequency_literals;

// Create SPI bus (see idfxx_spi), then the panel I/O
idfxx::panel_io panel_io(
    spi_bus,
    idfxx::panel_io::spi_config {
        .cs_gpio = idfxx::gpio_5,
        .dc_gpio = idfxx::gpio_2,
        .spi_mode = 0,
        .pclk_freq = 40_MHz,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    }
);

// Create ILI9341 display using the panel I/O
idfxx::lcd::panel::config panel_config{
    .reset_gpio = idfxx::gpio_4,
    .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,
    .bits_per_pixel = 16,
};

idfxx::lcd::ili9341 display(panel_io, std::move(panel_config));
```

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

### Color Framebuffer

For RGB565 (16 bits per pixel) displays such as ILI9341 panels,
`rgb565_framebuffer` provides a row-major drawing surface of `rgb565` colors,
stored in panel byte order so flushes pass the buffer straight through. A full
frame at 16 bpp is large (a 240x320 panel needs 150 KB), so the framebuffer can
also be sized as a horizontal band and flushed at a destination offset,
rendering the frame in slices:

```cpp
#include <idfxx/lcd/rgb565_framebuffer>

idfxx::lcd::rgb565_framebuffer band(display.width(), 40);

for (size_t y = 0; y < display.height(); y += band.height()) {
    band.fill({0, 0, 0});
    // ... draw the slice covering rows [y, y + band.height()) ...
    band.flush(display, 0, y);
}
```

## API Overview

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

### `rgb565` / `rgb565_framebuffer`

`rgb565` is a 16-bit color value stored in panel byte order (big-endian data,
the panel default), so arrays of it pass directly to `draw_bitmap`:

- `rgb565(r, g, b)` - Pack 8-bit components into the 5-6-5 layout (constexpr)
- `rgb565::from_value(v)` / `value()` - Packed RGB565 round trip

`rgb565_framebuffer` is the row-major color counterpart of `mono_framebuffer`
(the pixel (x, y) is at index `y * width + x`):

- `rgb565_framebuffer(width, height)` / `make(width, height)` - Create (any non-zero size)
- `set_pixel(x, y, color)` / `get_pixel(x, y)` - Pixel access (out-of-range coordinates are ignored)
- `fill(color)` / `clear()` - Fill with a color, or set all pixels black
- `data()` - Raw pixels, panel byte order
- `flush(panel, x = 0, y = 0)` / `try_flush(...)` - Draw the full framebuffer with its
  top-left corner at (x, y), so a band-sized framebuffer can render a taller frame in slices
- `flush_rows(panel, y_start, y_end)` / `try_flush_rows(...)` - Draw a horizontal band to
  the same rows on the panel
- `flush_region(panel, x_start, y_start, x_end, y_end)` / `try_flush_region(...)` - Draw a
  rectangular region: full-width regions transfer in a single draw, narrower ones one
  draw per row

Panels report their native dimensions via `panel::width()` / `panel::height()`, so a
matching framebuffer is simply `mono_framebuffer fb(display.width(), display.height())`.

## Important Notes

- This component provides the panel base class and framebuffers; use a concrete driver
  (e.g. `idfxx_lcd_ili9341`, `idfxx_lcd_ssd1306`) or `idfxx_lcd_touch` for device control
- The I/O layer (`idfxx::panel_io`) lives in the `idfxx_panel_io` component; the
  `idfxx::lcd::panel_io` alias is deprecated and will be removed in a future major release

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
