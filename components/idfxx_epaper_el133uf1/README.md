# idfxx_epaper_el133uf1

ePaper panel driver for the E Ink EL133UF1 13.3-inch 1200x1600 Spectra 6 glass,
as used in modules such as Seeed Studio's T133A01 (in the reTerminal E1004).

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
- Drives the glass's two cascaded controller ICs (one per 600-column half)
  on one SPI bus, handling both chip selects and the shared D/C line itself:
  commands for both controllers go out as one transfer with both selected.
- Driver-owned PSRAM shadow frame: writes at any row (and any even column
  in six-color, any multiple of 8 in mono) update the shadow, and `refresh`
  uploads the whole frame — so a small band framebuffer and
  `gfx::render_banded` work without a frame-sized application buffer.
- Deep sleep and wake (hardware reset + re-initialization; the shadow
  frame survives).

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- A target with PSRAM (`CONFIG_SPIRAM=y`); the shadow frame alone is 960 KB,
  and including `<idfxx/epaper/el133uf1>` without it is a compile error
- `idfxx_epaper` (abstract interface and framebuffers)
- `idfxx_spi` (SPI master bus and device)
- `idfxx_gpio` (chip-select, D/C, BUSY and reset lines)
- `idfxx_core` (error handling, PSRAM and DMA allocators)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_epaper_el133uf1:
    version: "^1.0.0"
```

Or add `idfxx_epaper_el133uf1` to the `REQUIRES` list in your component's
`CMakeLists.txt`.

## Usage

Wiring for the Seeed Studio reTerminal E1004:

```cpp
#include <idfxx/epaper/color6_framebuffer>
#include <idfxx/epaper/el133uf1>
#include <idfxx/spi/master>

idfxx::spi::bus_config bus_cfg{};
bus_cfg.mosi = idfxx::gpio_9;
bus_cfg.sclk = idfxx::gpio_7;
idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

// Two controllers on one bus: CS selects the master (left half), CS1 the
// slave (right half); they share the D/C line.
idfxx::epaper::el133uf1 display(bus, {
    .master_cs_gpio = idfxx::gpio_10,
    .slave_cs_gpio = idfxx::gpio_2,
    .dc_gpio = idfxx::gpio_11,
    .reset_gpio = idfxx::gpio_38,
    .busy_gpio = idfxx::gpio_13,
});

idfxx::epaper::color6_framebuffer fb(display.width(), display.height()); // 960 KB, lands in PSRAM
fb.set_pixel(10, 20, idfxx::epaper::color6::red);
display.write(fb);          // copy into the driver's shadow frame
display.refresh();          // upload + six-color update: 25-30 s (start_refresh() returns a future)
display.sleep();            // deep sleep between updates
```

The same flow with the result-based API:

```cpp
auto display = idfxx::epaper::el133uf1::make(bus, {
    .master_cs_gpio = idfxx::gpio_10,
    .slave_cs_gpio = idfxx::gpio_2,
    .dc_gpio = idfxx::gpio_11,
    .reset_gpio = idfxx::gpio_38,
    .busy_gpio = idfxx::gpio_13,
});
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

See `examples/e1004_demo` (reTerminal E1004) for a complete application.

## API Overview

The panel API (`write`, `write_rows`, `clear`, `refresh`, `start_refresh`,
`set_color_mode`, `sleep`, `wake`, and their `try_*` forms) is inherited
from `idfxx::epaper::panel` — see the `idfxx_epaper` documentation.

Driver-specific surface:

- `el133uf1(spi::master_bus& bus, config)` / `el133uf1::make(bus, config)`
  — construction; adds the panel's device to the bus, resets and
  initializes both controllers, allocates the white shadow frame, and
  leaves the panel in `color_mode::color6`. The bus must outlive the
  driver.
- `native_width` / `native_height` — 1200 / 1600.
- `config` — `master_cs_gpio` (required; the controller driving columns
  0-599), `slave_cs_gpio` (required; columns 600-1199), `dc_gpio`
  (required, shared), `reset_gpio` (required for `wake`, strongly
  recommended), `busy_gpio` (required, active low), `pclk` (SPI clock,
  default and maximum 10 MHz), `busy_timeout` (default 60 s).

## Error Handling

Uses `idfxx::result<T>` and `idfxx::errc` from `idfxx_core`:

- `errc::invalid_arg` — an unconnected required GPIO (`master_cs_gpio`,
  `slave_cs_gpio`, `dc_gpio`, `busy_gpio`), the same GPIO for both chip
  selects, or invalid write placement (an odd column for
  `color6_framebuffer`, a column not a multiple of 8 for
  `mono_framebuffer`, or out of bounds).
- `errc::invalid_state` — writing/refreshing while asleep, a framebuffer
  not matching the color mode, or `wake` without a `reset_gpio`.
- `errc::timeout` — the BUSY line did not release within `busy_timeout`.
- `errc::not_supported` — `fast` or `partial` refresh, or
  `color_mode::gray4`.

Running out of memory for the shadow frame (or the driver's 4 KB internal
DMA buffer) is not reported as an error:
allocation failure throws `std::bad_alloc` (or aborts without exceptions),
per the idfxx policy — PSRAM is a hard requirement.

Using a moved-from driver object is undefined behavior.

## Important Notes

- **Full refresh only, and it is slow.** The Spectra 6 waveform takes
  25-30 seconds (longer when cold) and flashes through several color
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
- **The driver drives both chip selects and D/C.** Pass the GPIOs in
  `config`, not to the bus: the driver adds its own device (with no
  hardware chip select) to the bus and selects one or both controllers
  around each transfer, so commands addressed to both reach them as a
  single transfer, as the vendor drivers send them.
- **Other devices on the bus wait during transfers.** The driver locks the
  bus for each command and for each half-frame upload (480 KB, roughly
  0.4 s at 10 MHz), from chip-select assertion to release. It releases the
  bus before waiting on BUSY, so other devices run freely during the
  25-30 second refresh.
- **The bus's default `max_transfer_sz` works.** Uploads go out in chunks
  of at most 4 KB (capped at the bus's maximum transaction length), each
  copied from the PSRAM shadow through a 4 KB internal DMA-capable buffer
  that the driver allocates once at construction.
- **Portrait-native.** `width()` is 1200 and `height()` 1600. Use
  `idfxx::gfx::rotated` for a landscape mounting (the E1004 mounts the
  glass landscape).
- **`color6{}` is black.** The six-color codes are the panel's own and code
  0 is black, so a *windowed* `gfx::canvas::clear()` inks its region black
  rather than blanking it; `color6_framebuffer::clear()` and whole-surface
  canvas clears blank to white. See the `idfxx_epaper` README.
- **Mono mode is a rendering convenience**, not a different waveform:
  1-bpp framebuffers are expanded into the same six-color shadow (ink →
  black, paper → white) and refreshed with the same 25-30 s update.
- **Sleep between updates.** Leaving the controllers powered degrades the
  glass; the destructor powers off an awake controller pair and puts them
  into deep sleep. After `wake()` the shadow frame is intact, so a partial
  redraw plus `refresh()` shows the full composed frame.
- The initialization sequence follows Seeed Studio's drivers for the
  T133A01 module; several registers are undocumented and carried over verbatim.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
