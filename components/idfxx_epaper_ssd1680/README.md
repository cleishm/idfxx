# idfxx_epaper_ssd1680

SSD1680 ePaper panel driver (up to 176x296, e.g. 2.13" 122x250).

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__epaper.html)**

## Features

- Implements the full `idfxx::epaper::panel` interface — application code
  written against the abstract panel works unchanged.
- Full, fast (temperature-forced shortened waveform), and flicker-free
  partial refreshes.
- 4-level grayscale (`color_mode::gray4`) using an explicitly loaded
  grayscale waveform (the Good Display reference recipe for this glass).
- Deep sleep and wake (hardware reset + re-initialization).
- Automatic previous-image plane maintenance: after every refresh the driver
  re-sends the displayed frame to the controller's old-image RAM, so partial
  refreshes diff against exactly what is on the glass.
- Configurable dimensions covering all SSD1680 glasses (up to 176 sources x
  296 gates); defaults match the Seeed Studio 2.13" 122x250 panel.
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
  cleishm/idfxx_epaper_ssd1680:
    version: "^1.0.0"
```

Or add `idfxx_epaper_ssd1680` to the `REQUIRES` list in your component's
`CMakeLists.txt`.

## Usage

Wiring for the Seeed Studio XIAO ePaper Display Board EE05 (ESP32-S3 Plus)
with the 2.13" panel:

```cpp
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/ssd1680>
#include <idfxx/spi/master>

idfxx::spi::bus_config bus_cfg{};
bus_cfg.mosi = idfxx::gpio_9;
bus_cfg.sclk = idfxx::gpio_7;
idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

idfxx::panel_io io(bus, idfxx::epaper::ssd1680::spi_io_config(idfxx::gpio_44, idfxx::gpio_10));
idfxx::epaper::ssd1680 display(io, {
    .reset_gpio = idfxx::gpio_38,
    .busy_gpio = idfxx::gpio_4,
});

idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
fb.set_pixel(10, 20, true); // black ink
fb.flush(display);          // upload to controller RAM
display.refresh();          // make it visible (blocks until done)
display.sleep();            // deep sleep between updates
```

The same flow with the result-based API:

```cpp
auto display = idfxx::epaper::ssd1680::make(io, {.reset_gpio = idfxx::gpio_38, .busy_gpio = idfxx::gpio_4});
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

- `ssd1680(panel_io&, config)` / `ssd1680::make(panel_io&, config)` —
  construction; resets and initializes the controller.
- `config` — `width`/`height` (default 122x250), `reset_gpio`
  (required for `wake`), `busy_gpio` (required), `busy_timeout` (default
  15 s), `mirror_x`/`mirror_y`.
- `spi_io_config(cs, dc, pclk = 10 MHz)` — pre-filled
  `panel_io::spi_config` (mode 0, 8-bit command/parameter framing).

## Error Handling

Uses `idfxx::result<T>` and `idfxx::errc` from `idfxx_core`:

- `errc::invalid_arg` — unconnected `busy_gpio`, dimensions outside the
  controller's 176x296 range, or invalid write placement.
- `errc::invalid_state` — writing/refreshing while asleep, a framebuffer
  not matching the color mode, or `wake` without a `reset_gpio`.
- `errc::timeout` — the BUSY line did not release within `busy_timeout`.
- `errc::not_supported` — `fast`/`partial` refresh in gray4 mode.

Using a moved-from driver object is undefined behavior.

## Important Notes

- **BUSY is required.** The SSD1680 holds BUSY high for hundreds of
  milliseconds to seconds during refreshes; the driver blocks on it (with
  a tick-sleep poll, not a busy-spin).
- **Shadow frame memory**: the driver keeps `(width + 7) / 8 * height`
  bytes of DRAM (~4 KB at 122x250) mirroring the last-written frame, used
  to refresh the controller's previous-image plane after each update.
- **Fast refresh is a waveform trick** (temperature-register forcing
  selecting a shortened OTP waveform, following Seeed's driver for this
  glass); **gray4 loads an explicit grayscale waveform** (the Good Display
  reference recipe). Quality varies by panel batch and temperature —
  validate on hardware, and prefer `full` refreshes when in doubt.
- **Fast refresh trades contrast for durability of the image.** The
  shortened waveform under-drives the ink, leaving black pixels in a
  weakly held state: they look black on their own, but the first
  subsequent partial refresh settles them to a visibly gray level (a
  one-time step, not progressive fading). Start a long partial-refresh
  sequence from a `full` refresh, which drives pixels to their stable
  extremes.
- **Ghosting**: issue a `full` refresh every ~10 partial refreshes to clean
  accumulated ghosting.
- **`mirror_x`/`mirror_y`** use the controller's address-decrement modes;
  on glasses whose width is not a multiple of 8 (e.g. 122), `mirror_x`
  shifts the image by the row-padding bits — verify on hardware.
- The destructor puts an awake controller into deep sleep; call `wake()`
  after re-constructing a driver on a previously slept panel.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
