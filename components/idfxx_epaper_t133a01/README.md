# idfxx_epaper_t133a01

T133A01 ePaper panel driver (13.3-inch 1200x1600 E Ink Spectra 6, e.g. the
Seeed Studio reTerminal E1004 and XIAO ePaper DIY Kit EE02).

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__epaper.html)**

## Features

- Implements the `idfxx::epaper::panel` interface — application code
  written against the abstract panel works unchanged, provided it uses
  full refreshes (`fast` and `partial` return `errc::not_supported`).
- Six-color operation (`color_mode::color6`, the panel's native 4-bpp
  format: black, white, yellow, red, blue, green) via `color6_framebuffer`.
- Monochrome operation (`color_mode::mono`) renders 1-bpp
  `mono_framebuffer`s as black and white, so existing text and banded
  rendering code works on this panel too.
- Drives the glass's two controller ICs (one per 600-column half) through
  two `idfxx::panel_io` objects with separate chip selects.
- Driver-owned PSRAM shadow frame: writes at any row (and any even column
  in six-color, any multiple of 8 in mono) update the shadow, and `refresh`
  uploads the whole frame — so a small band framebuffer and
  `gfx::render_banded` work without a frame-sized application buffer.
- Deep sleep and wake (hardware reset + re-initialization; the shadow
  frame survives).
- `spi_io_config()` helper pre-fills the controllers' SPI framing for
  `idfxx::panel_io`.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- A target with PSRAM (`CONFIG_SPIRAM=y`); the shadow frame alone is 960 KB,
  and including `<idfxx/epaper/t133a01>` without it is a compile error
- `idfxx_epaper` (abstract interface and framebuffers)
- `idfxx_panel_io` (panel I/O transport)
- `idfxx_gpio` (BUSY and reset lines)
- `idfxx_core` (error handling, PSRAM allocator)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_epaper_t133a01:
    version: "^1.0.0"
```

Or add `idfxx_epaper_t133a01` to the `REQUIRES` list in your component's
`CMakeLists.txt`.

## Usage

Wiring for the Seeed Studio reTerminal E1004:

```cpp
#include <idfxx/epaper/color6_framebuffer>
#include <idfxx/epaper/t133a01>
#include <idfxx/spi/master>

idfxx::spi::bus_config bus_cfg{};
bus_cfg.mosi = idfxx::gpio_9;
bus_cfg.sclk = idfxx::gpio_7;
bus_cfg.max_transfer_sz = 16384; // each half-frame upload is one 480 KB transfer
idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

// Two controllers on one bus: CS selects the master (left half), CS1 the
// slave (right half); they share the D/C line.
idfxx::panel_io master(bus, idfxx::epaper::t133a01::spi_io_config(idfxx::gpio_10, idfxx::gpio_11));
idfxx::panel_io slave(bus, idfxx::epaper::t133a01::spi_io_config(idfxx::gpio_2, idfxx::gpio_11));
idfxx::epaper::t133a01 display(master, slave, {
    .reset_gpio = idfxx::gpio_38,
    .busy_gpio = idfxx::gpio_13,
});

idfxx::epaper::color6_framebuffer fb(display.width(), display.height()); // 960 KB, lands in PSRAM
fb.set_pixel(10, 20, idfxx::epaper::color6::red);
display.write(fb);          // copy into the driver's shadow frame
display.refresh();          // upload + six-color update: 30-40 s (start_refresh() returns a future)
display.sleep();            // deep sleep between updates
```

The same flow with the result-based API:

```cpp
auto display = idfxx::epaper::t133a01::make(master, slave, {.reset_gpio = idfxx::gpio_38, .busy_gpio = idfxx::gpio_13});
if (!display) {
    return idfxx::error(display.error());
}
auto fb = idfxx::epaper::color6_framebuffer::make(display->width(), display->height());
if (!fb) {
    return idfxx::error(fb.error());
}
fb->set_pixel(10, 20, idfxx::epaper::color6::red);
if (auto r = display->try_write(*fb); !r) {
    return r;
}
if (auto r = display->try_refresh(); !r) {
    return r;
}
return display->try_sleep();
```

### Landscape layouts

The panel is portrait-native (1200 wide, 1600 tall). For a landscape
mounting, draw through an `idfxx::gfx::rotated` view, which presents the
framebuffer as 1600x1200; the framebuffer keeps its native layout and is
written to the display as usual:

```cpp
idfxx::epaper::color6_framebuffer fb(display.width(), display.height());
idfxx::gfx::rotated view(fb, idfxx::gfx::rotation::cw90);
idfxx::gfx::canvas canvas(view);
canvas.draw_text(idfxx::font::spleen_8x16, 24, 24, "landscape", idfxx::epaper::color6::black, 4);
display.write(fb);
display.refresh();
```

### Monochrome, band by band

```cpp
display.set_color_mode(idfxx::epaper::color_mode::mono);
idfxx::epaper::mono_framebuffer band(display.width(), 100); // 15 KB instead of 240 KB
idfxx::gfx::render_banded(band, display, display.height(), [](auto& canvas) {
    canvas.draw_text(idfxx::font::spleen_8x16, 32, 32, "hello", true, 8);
});
display.refresh();
```

See `examples/e1004_demo` (reTerminal E1004) and `examples/ee02_demo`
(XIAO ePaper DIY Kit EE02) for complete applications.

## API Overview

The panel API (`write`, `write_rows`, `clear`, `refresh`, `start_refresh`,
`set_color_mode`, `sleep`, `wake`, and their `try_*` forms) is inherited
from `idfxx::epaper::panel` — see the `idfxx_epaper` documentation.

Driver-specific surface:

- `t133a01(panel_io& master, panel_io& slave, config)` /
  `t133a01::make(...)` — construction; resets and initializes both
  controllers, allocates the white shadow frame, and leaves the panel in
  `color_mode::color6`. The master I/O is the chip select of the
  controller driving columns 0-599, the slave that of columns 600-1199.
- `native_width` / `native_height` — 1200 / 1600.
- `config` — `reset_gpio` (required for `wake`, strongly recommended),
  `busy_gpio` (required, active low), `busy_timeout` (default 60 s).
- `spi_io_config(cs, dc, pclk = 10 MHz)` — pre-filled
  `panel_io::spi_config` (mode 0, 8-bit command/parameter framing); call
  once per chip select.

## Error Handling

Uses `idfxx::result<T>` and `idfxx::errc` from `idfxx_core`:

- `errc::invalid_arg` — unconnected `busy_gpio`, the same `panel_io` passed
  as both master and slave, or invalid write placement (an odd column for
  `color6_framebuffer`, a column not a multiple of 8 for
  `mono_framebuffer`, or out of bounds).
- `errc::invalid_state` — writing/refreshing while asleep, a framebuffer
  not matching the color mode, or `wake` without a `reset_gpio`.
- `errc::timeout` — the BUSY line did not release within `busy_timeout`.
- `errc::not_supported` — `fast` or `partial` refresh, or
  `color_mode::gray4`.

Running out of memory for the shadow frame is not reported as an error:
allocation failure throws `std::bad_alloc` (or aborts without exceptions),
per the idfxx policy — PSRAM is a hard requirement.

Using a moved-from driver object is undefined behavior.

## Important Notes

- **Full refresh only, and it is slow.** The Spectra 6 waveform takes
  30-40 seconds (longer when cold) and flashes through several color
  passes; there is no fast or partial mode. `refresh` blocks on BUSY for
  that long (tick-sleep polling, not a busy-spin); `start_refresh` returns
  a future, and the next panel operation absorbs whatever remains. The
  default `busy_timeout` is 60 s for this reason.
- **PSRAM is required.** Without `CONFIG_SPIRAM`, including the driver's
  header fails with an `#error` (the component itself still builds, empty,
  so depending on it from a PSRAM-less configuration is harmless). The
  driver keeps a 960 KB shadow of the frame in PSRAM, and a full-frame `color6_framebuffer` is another 960 KB (a
  `mono_framebuffer` is 240 KB). With `CONFIG_SPIRAM_USE_MALLOC=y` and the
  default `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` (16 KB), framebuffers that
  large land in PSRAM automatically; the example `sdkconfig.defaults` files
  show a working configuration for the ESP32-S3 with octal PSRAM.
- **Two `panel_io`s on one bus.** Each controller has its own chip select
  and both share the D/C line, so construct two `panel_io` objects from
  the same `spi_io_config` (different `cs`, same `dc`) on the same bus.
  Commands addressed to both controllers are sent sequentially, master
  then slave.
- **Set `max_transfer_sz`.** Each half-frame upload is a single 480 KB
  `panel_io` transfer that `esp_lcd` splits into chunks of the bus's
  `max_transfer_sz` while holding chip select. The default (4092 bytes)
  works but is slow; 16 KB is a good value.
- **Portrait-native.** `width()` is 1200 and `height()` 1600. Use
  `idfxx::gfx::rotated` for a landscape mounting (the E1004 mounts the
  glass landscape).
- **`color6{}` is black.** The six-color codes are the panel's own and code
  0 is black, so a *windowed* `gfx::canvas::clear()` inks its region black
  rather than blanking it; `color6_framebuffer::clear()` and whole-surface
  canvas clears blank to white. See the `idfxx_epaper` README.
- **Mono mode is a rendering convenience**, not a different waveform:
  1-bpp framebuffers are expanded into the same six-color shadow (ink →
  black, paper → white) and refreshed with the same 30-40 s update.
- **Sleep between updates.** Leaving the controllers powered degrades the
  glass; the destructor powers off an awake controller pair and puts them
  into deep sleep. After `wake()` the shadow frame is intact, so a partial
  redraw plus `refresh()` shows the full composed frame.
- The initialization sequence follows Seeed Studio's drivers for this
  module; several registers are undocumented and carried over verbatim.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
