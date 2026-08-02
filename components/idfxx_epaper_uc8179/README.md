# idfxx_epaper_uc8179

UC8179 ePaper panel driver (up to 800x600, e.g. 7.5" 800x480).

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__epaper.html)**

## Features

- Implements the full `idfxx::epaper::panel` interface — application code
  written against the abstract panel works unchanged.
- Full, fast (register-written shortened waveform), and flicker-free
  partial refreshes.
- 4-level grayscale (`color_mode::gray4`) using the glass's factory (OTP)
  grayscale waveform.
- Deep sleep and wake (hardware reset + re-initialization).
- No shadow frame: the UC8179 copies the new-image RAM to the
  previous-image RAM when a refresh completes, so partial refreshes diff
  against what is on the glass without host-side bookkeeping.
- Configurable dimensions covering all UC8179 glasses (up to 800 sources x
  600 gates); defaults match the 7.5" 800x480 GDEY075T7 glass.
- `spi_io_config()` helper pre-fills the controller's SPI framing for
  `idfxx::panel_io`.

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
    version: "^1.0.0"
```

Or add `idfxx_epaper_uc8179` to the `REQUIRES` list in your component's
`CMakeLists.txt`.

## Usage

Wiring for the Seeed Studio XIAO ePaper Display Board EE05 (ESP32-S3 Plus)
with the 7.5" panel:

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
fb.flush(display);          // upload to controller RAM (48 KB, streamed by DMA)
display.refresh();          // make it visible (blocks until done)
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
if (auto r = fb->try_flush(*display); !r) {
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
fb.flush(display);
display.refresh(idfxx::epaper::refresh_mode::partial); // differential, no flash
```

### Grayscale

```cpp
display.set_color_mode(idfxx::epaper::color_mode::gray4);
idfxx::epaper::gray4_framebuffer gray_fb(display.width(), display.height());
gray_fb.fill(idfxx::epaper::gray4::light);
gray_fb.flush(display);
display.refresh(); // gray4 supports full refresh only
display.set_color_mode(idfxx::epaper::color_mode::mono); // back to monochrome
```

See `examples/ee05_demo` for a complete application exercising all refresh
modes on the EE05 board.

## API Overview

The panel API (`write`, `write_rows`, `clear`, `refresh`, `wait`,
`wait_for`, `set_color_mode`, `sleep`, `wake`, and their `try_*` forms) is
inherited from `idfxx::epaper::panel` — see the `idfxx_epaper` documentation.

Driver-specific surface:

- `uc8179(panel_io&, config)` / `uc8179::make(panel_io&, config)` —
  construction; resets and initializes the controller and clears both
  image planes to white.
- `config` — `width`/`height` (default 800x480; width must be a multiple
  of 8), `reset_gpio` (required for `wake`, strongly recommended in
  general), `busy_gpio` (required), `busy_timeout` (default 30 s).
- `spi_io_config(cs, dc, pclk = 10 MHz)` — pre-filled
  `panel_io::spi_config` (mode 0, 8-bit command/parameter framing).

## Error Handling

Uses `idfxx::result<T>` and `idfxx::errc` from `idfxx_core`:

- `errc::invalid_arg` — unconnected `busy_gpio`, dimensions outside the
  controller's 800x600 range, a width not a multiple of 8, or invalid
  write placement.
- `errc::invalid_state` — writing/refreshing while asleep, a framebuffer
  not matching the color mode, or `wake` without a `reset_gpio`.
- `errc::timeout` — the BUSY line did not release within `busy_timeout`.
- `errc::not_supported` — `fast`/`partial` refresh in gray4 mode.

Using a moved-from driver object is undefined behavior.

## Important Notes

- **BUSY is required.** The UC8179 holds BUSY low (active low, unlike the
  SSD1680) for several seconds during full refreshes of the 7.5" glass;
  the driver blocks on it (with a tick-sleep poll, not a busy-spin).
- **Memory**: a full-frame `mono_framebuffer` at 800x480 is 48 KB and a
  `gray4_framebuffer` is 96 KB, all DRAM. Grayscale writes additionally
  stage each plane through a transient buffer of up to 48 KB (the
  controller's gray bit sense is inverted from the canonical encoding).
  `gfx::render_banded` with a band-sized framebuffer keeps mono drawing
  memory small.
- **Waveform switching resets the controller.** The UC8179 selects its
  waveform at initialization, so changing refresh mode between refreshes
  hardware-resets and re-initializes the controller (image RAM survives).
  Wire `reset_gpio` — without it, mode switches leave stale register state
  behind. Repeated refreshes in the same mode skip this entirely.
- **Fast refresh uses register-written waveform tables** (from Seeed's
  driver for this glass). Quality varies by panel batch and temperature —
  validate on hardware, and prefer `full` when in doubt.
- **Gray4 uses the glass's factory (OTP) 4-gray waveform**, selected via a
  forced temperature (per the vendor sequence). Not every UC8179 glass
  carries a 4-gray OTP bank — validate on hardware.
- **Fast refresh trades contrast for durability of the image.** The
  shortened waveform under-drives the ink, leaving black pixels in a
  weakly held state: they look black on their own, but a subsequent
  partial refresh can settle them to a visibly gray level (a one-time
  step, not progressive fading). Start a long partial-refresh sequence
  from a `full` refresh, which drives pixels to their stable extremes.
- **Ghosting**: issue a `full` refresh every ~10 partial refreshes to clean
  accumulated ghosting.
- **Gray4 amplifies ghosting from long-displayed content.** The gray
  midtones are open-loop drives, so they expose residual polarization the
  ink accumulates while content sits in one state — content that was black
  on the glass for a long time can ghost faintly through the gray levels
  even after the panel has been cleared to a uniform white. The simplest
  mitigation is sequencing: refresh grayscale content while the glass is
  fresh, before any one image has been held for long (`examples/ee05_demo`
  runs its grayscale stage early for exactly this reason). Repeated scrub
  cycles do not help once the polarization exists: a drive applied
  identically to every pixel cannot remove a per-pixel differential. When
  long-held content is unavoidable, inverse compensation works: display
  the *inverse* of the long-held content (every pixel makes a full
  opposite transition), hold it — the compensation grows with hold time,
  and a hold approaching the original display time all but eliminates the
  ghost — then `clear()` and refresh. In mono mode `clear()` blanks only
  the new-image plane, so that final refresh applies the deep
  black-to-white erase waveform to previously-black pixels rather than
  the gentle white-to-white one.
- The destructor powers off an awake controller and puts it into deep
  sleep; call `wake()` after re-constructing a driver on a previously
  slept panel.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
