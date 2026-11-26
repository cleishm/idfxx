# idfxx_epaper_el133uf1

ePaper panel driver for the E Ink EL133UF1 13.3-inch 1200x1600 Spectra 6 glass, as used in modules
such as Seeed Studio's T133A01 (in the reTerminal E1004).

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__epaper.html)**

## Features

- Implements the `idfxx::epaper::panel` interface. Application code written against the abstract
  panel works unchanged if it uses full or fast refreshes. `partial` always returns
  `errc::not_supported`.
- Fast refreshes run the same waveform as a full refresh on a faster frame clock. There are four
  image-quality levels, set in `config` or per refresh. A fast refresh takes about 11-18 s, compared
  with 25-30 s for a full refresh.
- Six-color operation (`color_mode::color6`, the panel's native 4-bpp format: black, white, yellow,
  red, blue, green) via `color6_framebuffer`.
- Monochrome operation (`color_mode::mono`) renders 1-bpp `mono_framebuffer`s as black and white, so
  existing text and banded rendering code works on this panel too.
- Drives the glass's two cascaded controller ICs (one per 600-column half) on one SPI bus. The
  driver handles both chip selects and the shared D/C line itself. A command for both controllers
  is sent as one transfer with both selected.
- Driver-owned PSRAM shadow frame. Writes update the shadow, and `refresh` uploads the whole
  frame, so a small band framebuffer and `gfx::render_banded` work without a frame-sized
  application buffer. A write can start at any row, and at any even column in six-color or any
  multiple of 8 in mono.
- Deep sleep and wake. Wake does a hardware reset and re-initialization, and keeps the shadow frame.
- Optional presence check. When the panel's data output is wired to the bus's MISO line and
  `check_presence` is set, the driver confirms that both controllers answer during construction and
  `wake`. A disconnected panel is then reported as an error instead of appearing idle.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- A target with PSRAM (`CONFIG_SPIRAM=y`). The shadow frame alone is 960 KB. Including
  `<idfxx/epaper/el133uf1>` without PSRAM is a compile error.
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
    version: "^2.1.0"
```

Or add `idfxx_epaper_el133uf1` to the `REQUIRES` list in your component's `CMakeLists.txt`.

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

// Two controllers on one bus. CS selects the master (left half), and CS1
// selects the slave (right half). They share the D/C line.
idfxx::epaper::el133uf1 display(bus, {
    .master_cs_gpio = idfxx::gpio_10,
    .slave_cs_gpio = idfxx::gpio_2,
    .dc_gpio = idfxx::gpio_11,
    .reset_gpio = idfxx::gpio_38,
    .busy_gpio = idfxx::gpio_13,
});

idfxx::epaper::color6_framebuffer fb(display.width(), display.height()); // 960 KB, in PSRAM
fb.set_pixel(10, 20, idfxx::epaper::color6::red);
display.write(fb);          // copy into the driver's shadow frame
display.refresh();          // upload + six-color update: 25-30 s (start_refresh() returns a future)
display.sleep();            // deep sleep between updates
```

Use a fast refresh for black-and-white content that changes often. Run a full refresh occasionally
to restore a clean image:

```cpp
using idfxx::epaper::el133uf1;

display.refresh(idfxx::epaper::refresh_mode::fast); // at config's fast_refresh_quality (high)
display.refresh(el133uf1::fast_quality::medium);    // fast, at medium for this refresh only
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

The panel is portrait-native (1200 wide, 1600 tall). For a landscape mounting, draw through an
`idfxx::gfx::rotated` view, which presents the framebuffer as 1600x1200. The framebuffer keeps its
native layout and is written to the display as usual:

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

The panel API (`write`, `write_rows`, `clear`, `refresh`, `start_refresh`, `set_color_mode`,
`sleep`, `wake`, and their `try_*` forms) is inherited from `idfxx::epaper::panel`. See the
`idfxx_epaper` documentation.

Driver-specific surface:

- `el133uf1(spi::master_bus& bus, config)` / `el133uf1::make(bus, config)` — construction. Adds the
  panel's device to the bus, resets and initializes both controllers, and allocates the white
  shadow frame. The panel is then in `color_mode::color6`. The bus must outlive the driver.
- `native_width` / `native_height` — 1200 / 1600.
- `config` fields:
  - `master_cs_gpio`: required. Selects the controller driving columns 0-599.
  - `slave_cs_gpio`: required. Selects the controller driving columns 600-1199.
  - `dc_gpio`: required, shared by both controllers.
  - `reset_gpio`: required for `wake`, strongly recommended.
  - `busy_gpio`: required, active low.
  - `check_presence`: default `false`. When set, the driver confirms that both controllers answer
    during construction and `wake`. Needs the bus's MISO line wired to the panel.
  - `pclk`: SPI clock, default and maximum 10 MHz.
  - `busy_timeout`: default 60 s.
  - `fast_refresh_quality`: the image quality that `refresh_mode::fast` refreshes run at. Default
    `fast_quality::high`.
- `refresh(fast_quality)` / `start_refresh(fast_quality)` (and `try_*` forms) — a fast refresh at
  the given quality, for that refresh only. The panel's `refresh(refresh_mode)` overloads are still
  available.
- `fast_quality` — `high` (about 18 s, the cleanest), `medium` (about 16 s), `low` (about 13 s),
  `draft` (about 11 s, the fastest). Each lower level is faster but gives lower image quality. White
  turns increasingly cream, and a faint ghost of the previous image may remain.

## Error Handling

Uses `idfxx::result<T>` and `idfxx::errc` from `idfxx_core`:

- `errc::invalid_arg` — an unconnected required GPIO (`master_cs_gpio`, `slave_cs_gpio`, `dc_gpio`,
  `busy_gpio`), the same GPIO for both chip selects, or invalid write placement (an odd column for
  `color6_framebuffer`, a column that is not a multiple of 8 for `mono_framebuffer`, or out of
  bounds).
- `errc::invalid_state` — writing/refreshing while asleep, a framebuffer that does not match the
  color mode, or `wake` without a `reset_gpio`.
- `errc::timeout` — the BUSY line did not release within `busy_timeout`.
- `errc::not_found` — with `check_presence` set, a controller did not answer during construction or
  `wake` (the log names which one).
- `errc::not_supported` — any `partial` refresh (including the first one, before a baseline
  exists), or `color_mode::gray4`.

Running out of memory for the shadow frame (or the driver's 4 KB internal DMA buffer) is not
reported as an error. Following the idfxx policy, allocation failure throws `std::bad_alloc`, or
aborts without exceptions. PSRAM is a hard requirement.

Using a moved-from driver object is undefined behavior.

## Important Notes

- **Refreshes are slow.** The Spectra 6 waveform takes 25-30 seconds (longer when cold) and
  flashes through several color passes. There is no partial mode. A fast refresh runs the same
  waveform on a faster frame clock. It still flashes and still redraws every pixel, and takes about
  11-18 s, depending on `fast_refresh_quality`. `refresh` blocks on BUSY for that long (tick-sleep
  polling, not a busy-spin). `start_refresh` returns a future, and the next panel operation waits
  for any remaining part of the update. The default `busy_timeout` is 60 s for this reason.
- **Fast refreshes reduce image quality.** White turns cream at the faster clocks, and colors other
  than black and white may shift, so use fast refreshes only for black-and-white content. A full
  refresh afterward removes any artifacts that the fast refresh left.
- **PSRAM is required.** Without `CONFIG_SPIRAM`, including the driver's header fails with an
  `#error`. The component itself still builds as an empty component, so a configuration without
  PSRAM can depend on it without harm. The driver keeps a 960 KB shadow of the frame in PSRAM, and a
  full-frame `color6_framebuffer` is another 960 KB (a `mono_framebuffer` is 240 KB). With
  `CONFIG_SPIRAM_USE_MALLOC=y` and the default `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` (16 KB),
  framebuffers that large are allocated in PSRAM automatically. The example `sdkconfig.defaults`
  files show a working configuration for the ESP32-S3 with octal PSRAM.
- **The driver drives both chip selects and D/C.** Pass the GPIOs in `config`, not to the bus. The
  driver adds its own device (with no hardware chip select) to the bus, and selects one or both
  controllers around each transfer, so a command addressed to both controllers is sent as a single
  transfer, in the same way that the vendor drivers send it.
- **Other devices on the bus wait during transfers.** The driver locks the bus for each command and
  for each half-frame upload (480 KB, roughly 0.4 s at 10 MHz), from chip-select assertion to
  release. It releases the bus before waiting on BUSY, so other devices can use the bus during the
  25-30 second refresh.
- **The bus's default `max_transfer_sz` works.** Uploads are sent in chunks of at most 4 KB (capped
  at the bus's maximum transaction length). The driver copies each chunk from the PSRAM shadow
  through a 4 KB internal DMA-capable buffer, which it allocates once at construction.
- **The presence check reads over MISO.** `check_presence` needs the panel's data output on the
  bus's MISO line. Without it, the check reads an idle line and fails. On the reTerminal E1004, the
  panel's MISO (GPIO8) is shared with the microSD card's data line, so keep the card deselected (SD
  CS, GPIO14, held high) while constructing or waking the panel with the check enabled.
- **Portrait-native.** `width()` is 1200 and `height()` 1600. Use `idfxx::gfx::rotated` for a
  landscape mounting (the E1004 mounts the glass landscape).
- **`color6{}` is black.** The six-color codes are the panel's own, and code 0 is black, so a
  *windowed* `gfx::canvas::clear()` fills its region with black instead of blanking it.
  `color6_framebuffer::clear()` and whole-surface canvas clears set the pixels to white. See the
  `idfxx_epaper` README.
- **Mono mode is a rendering convenience**, not a different waveform. 1-bpp framebuffers are
  expanded into the same six-color shadow (ink → black, paper → white) and refreshed with the same
  25-30 s update.
- **Sleep between updates.** Leaving the controllers powered degrades the glass. The destructor
  powers off an awake controller pair and puts them into deep sleep. After `wake()` the shadow frame
  is intact, so a partial redraw plus `refresh()` shows the full composed frame.
- **Without a reset line, the driver cannot wake the panel from deep sleep.** Only a hardware reset
  wakes the controllers, so with no `reset_gpio`, `wake()` fails with `errc::invalid_state`. After
  `sleep()` or the destructor, only cycling the panel's power wakes the controllers. A driver
  created before the power is cycled fails with `errc::not_found` when `check_presence` is set.
  Without the check, it succeeds, but writes and refreshes leave the glass unchanged. Without a
  reset line, construction waits for BUSY to release instead of resetting, so it works with a panel
  that is still finishing an earlier update.
- The initialization sequence follows Seeed Studio's drivers for the T133A01 module. Several
  registers are undocumented and carried over verbatim.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
