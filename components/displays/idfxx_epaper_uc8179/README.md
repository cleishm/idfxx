# idfxx_epaper_uc8179

UC8179 ePaper panel driver (up to 800x600, e.g. 7.5" 800x480).

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__epaper.html)**

## Features

- Implements the full `idfxx::epaper::panel` interface. Application code written against the
  abstract panel works unchanged.
- Full, fast (register-written shortened waveform), and flicker-free partial refreshes.
- 4-level grayscale (`color_mode::gray4`) using the glass's factory grayscale waveform, with a
  configurable frame rate and source voltage.
- Selectable border: driven white or black on full/fast refreshes, or held so it never flickers.
- Deep sleep and wake (hardware reset + re-initialization).
- `restore_baseline()` support, to keep partial refreshes across a wake or an ESP32 deep sleep (in
  `color_mode::mono`).
- No shadow frame: the UC8179 copies the new-image plane to the old-image plane when a refresh
  completes, so partial refreshes diff against what is on the glass without host-side bookkeeping.
- Configurable dimensions covering all UC8179 glasses (up to 800 sources x 600 gates). The defaults
  match the 7.5" 800x480 GDEY075T7 glass.
- `spi_io_config()` helper pre-fills the controller's SPI framing for `idfxx::panel_io`.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- `idfxx_epaper` (abstract interface and framebuffers)
- `idfxx_panel_io` (panel I/O transport)
- `idfxx_gpio` (BUSY and reset lines)
- `idfxx_core` (error handling)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_epaper_uc8179:
    version: "^3.0.0"
```

Or add `idfxx_epaper_uc8179` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

Wiring for the Seeed Studio XIAO ePaper Display Board EE05 (ESP32-S3 Plus) with the 7.5" panel:

```cpp
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/uc8179>
#include <idfxx/spi/master>

idfxx::spi::bus_config bus_cfg{};
bus_cfg.mosi = idfxx::gpio_9;
bus_cfg.sclk = idfxx::gpio_7;
idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

idfxx::panel_io io(bus, idfxx::epaper::uc8179::spi_io_config(idfxx::gpio_44, idfxx::gpio_10));
idfxx::epaper::uc8179 display(io, {
    .reset_gpio = idfxx::gpio_38,
    .busy_gpio = idfxx::gpio_4,
});

idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
fb.set_pixel(10, 20, true); // black ink
display.write(fb);          // upload to controller RAM (48 KB, streamed by DMA)
display.refresh();          // make it visible (blocks until done; start_refresh() returns a future)
display.sleep();            // deep sleep between updates
```

The same flow with the result-based API:

```cpp
auto display = idfxx::epaper::uc8179::make(io, {.reset_gpio = idfxx::gpio_38, .busy_gpio = idfxx::gpio_4});
if (!display) {
    return idfxx::error(display.error());
}
auto fb = idfxx::epaper::mono_framebuffer::make(display->width(), display->height());
if (!fb) {
    return idfxx::error(fb.error());
}
fb->set_pixel(10, 20, true);
if (auto r = display->try_write(*fb); !r) {
    return r;
}
if (auto r = display->try_refresh(); !r) {
    return r;
}
return display->try_sleep();
```

### Partial and fast refreshes

```cpp
display.refresh(idfxx::epaper::refresh_mode::fast);    // shortened full waveform
display.write(fb);
display.refresh(idfxx::epaper::refresh_mode::partial); // differential, no flash
```

### Grayscale

```cpp
display.set_color_mode(idfxx::epaper::color_mode::gray4);
idfxx::epaper::gray4_framebuffer gray_fb(display.width(), display.height());
gray_fb.fill(idfxx::epaper::gray4::light);
display.write(gray_fb);
display.refresh(); // gray4 supports full refresh only
display.set_color_mode(idfxx::epaper::color_mode::mono); // back to monochrome
```

The grayscale operating point is set in the config (see Important Notes):

```cpp
idfxx::epaper::uc8179 display(io, {
    .busy_gpio = idfxx::gpio_4,
    .gray4_frame_rate = quanta::frequency::hertz{50},
    .gray4_source_voltage = quanta::electro::millivolts{10'000},
});
```

### Border

```cpp
idfxx::epaper::uc8179 display(io, {.busy_gpio = idfxx::gpio_4, .border = idfxx::epaper::border::hold});
display.set_border(idfxx::epaper::border::black); // driven black from the next full/fast refresh
```

See `examples/ee05_demo` for a complete application exercising all refresh modes on the EE05 board.

## API Overview

The panel API (`write`, `write_rows`, `clear`, `restore_baseline`, `refresh`, `start_refresh`,
`set_color_mode`, `set_border`, `sleep`, `wake`, and their `try_*` forms) is inherited from
`idfxx::epaper::panel`. See the `idfxx_epaper` documentation.

Driver-specific surface:

- `uc8179(panel_io&, config)` / `uc8179::make(panel_io&, config)` — construction. Resets and
  initializes the controller and clears both image planes to white.
- `config` fields:
  - `width`/`height`: default 800x480. The width must be a multiple of 8.
  - `reset_gpio`: required for `wake`, strongly recommended in general.
  - `busy_gpio`: required.
  - `busy_timeout`: default 30 s.
  - `border`: default `border::white`. All three values are supported. Can be changed later with
    `set_border`.
  - `gray4_frame_rate` / `gray4_source_voltage`: the grayscale operating point, default 20 Hz at
    +/-15 V.
- `spi_io_config(cs, dc, pclk = 10 MHz)` — pre-filled `panel_io::spi_config` (mode 0, 8-bit
  command/parameter framing).

## Error Handling

Uses `idfxx::result<T>` and `idfxx::errc` from `idfxx_core`:

- `errc::invalid_arg` — unconnected `busy_gpio`, dimensions outside the controller's 800x600 range,
  a width that is not a multiple of 8, an unsupported `gray4_frame_rate` or `gray4_source_voltage`,
  or invalid write placement.
- `errc::invalid_state` — writing/refreshing while asleep, a framebuffer that does not match the
  color mode, or `wake` without a `reset_gpio`.
- `errc::timeout` — the BUSY line did not release within `busy_timeout`.
- `errc::not_supported` — `fast`/`partial` refresh in gray4 mode.

Using a moved-from driver object is undefined behavior.

## Important Notes

- **BUSY is required.** The UC8179 holds BUSY low (active low, unlike the SSD1680) for several
  seconds during full refreshes of the 7.5" glass. `refresh` blocks on it (with a tick-sleep poll,
  not a busy-spin). `start_refresh` returns while the glass is still updating. The driver then waits
  for BUSY before the next controller command.
- **Memory**: a full-frame `mono_framebuffer` at 800x480 is 48 KB and a `gray4_framebuffer` is 96
  KB, all DRAM. Grayscale writes additionally stage each plane through a transient buffer of up to
  48 KB (the controller's gray bit sense is inverted from the canonical encoding).
  `gfx::render_banded` with a band-sized framebuffer keeps mono drawing memory small.
- **Waveform switching resets the controller.** The UC8179 selects its waveform at initialization,
  so changing refresh mode between refreshes hardware-resets and re-initializes the controller
  (the image RAM is kept). Wire `reset_gpio`. Without it, mode switches leave stale register state
  behind. Repeated refreshes in the same mode do not reset the controller.
- **Fast refresh uses register-written waveform tables** (from Seeed's driver for this glass).
  Quality varies by panel batch and temperature. Validate it on hardware, and use `full` if you are
  not sure.
- **Gray4 levels are tunable, and shift when you tune them.** The grayscale waveform tables are the
  factory grayscale waveform of the GDEY075T7 glass (on other UC8179 glasses, validate on hardware).
  `config::gray4_frame_rate` and `config::gray4_source_voltage` select the operating point:
  - 20 Hz at +/-15 V (the default): the deepest black and the widest gray spacing, independent of
    the image content. A refresh takes ~9 s.
  - 50 Hz at +/-10 V: keeps the vendor's 3.6 s refresh, and the levels do not depend on the
    content. But the black is lighter, and both grays are in the dark end of the range.
  - 50 Hz at +/-15 V (the vendor's own operating point): a deep black at 3.6 s. But the gray levels
    drop by up to ~15 % on content with dense horizontal detail (the drive supplies sag under
    source-switching load).

  The gray levels move with either setting, so recalibrate any gray mapping after changing them.
- **Black pixels from a fast refresh can turn gray at a later partial refresh.** The shortened
  waveform under-drives the ink, and leaves black pixels in a weakly held state. They look black
  on their own, but a later partial refresh can settle them to a visibly gray level. This is a
  one-time step, not progressive fading. Start a long partial-refresh sequence with a `full`
  refresh, which drives pixels to their stable extremes.
- **Ghosting**: issue a `full` refresh every ~10 partial refreshes to clean accumulated ghosting.
- **A white border flashes with each full refresh.** The controller has no fixed-level border
  drive. It drives the border with the black-to-white pixel waveform, so with `border::white` the
  border flashes along with the image. `border::hold` leaves the border undriven, so it keeps its
  last color. Once the border is white, switching to `hold` stops it flashing. Whether a floating
  border stays clean across many full refreshes depends on the glass. If it turns gray, drive it
  white again occasionally. In `color_mode::gray4`, the border cannot be driven white, so a white
  setting holds the border (black is still driven). Partial refreshes and deep sleep always float
  the border, whatever the setting.
- **Gray4 amplifies ghosting from long-displayed content.** The gray midtones are open-loop drives,
  so they show the residual polarization that the ink builds up while content stays in one state.
  Content that was black on the glass for a long time can show as a faint ghost through the gray
  levels, even after the panel has been cleared to a uniform white. The simplest mitigation is to
  order the refreshes. Refresh grayscale content while the glass is fresh, before any one image has
  been held for long. `examples/ee05_demo` runs its grayscale stage early for this reason. Repeated
  scrub cycles do not help once the polarization exists: a drive applied identically to every pixel
  cannot remove a per-pixel differential. When long-held content is unavoidable, inverse
  compensation works:
  1. Display the *inverse* of the long-held content, so that every pixel makes a full opposite
     transition.
  2. Hold it. The compensation grows with hold time. A hold that approaches the original display
     time almost removes the ghost.
  3. Call `clear()` and refresh.

  In mono mode, `clear()` blanks only the new-image plane, so the final refresh applies the deep
  black-to-white erase waveform to previously-black pixels, instead of the gentle white-to-white
  one.
- The destructor powers off an awake controller and puts it into deep sleep. Call `wake()` after
  re-constructing a driver on a previously slept panel.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
