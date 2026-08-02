# idfxx_epaper

Abstract ePaper panel interface, framebuffers, and shared types.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__epaper.html)**

## Features

- Abstract `idfxx::epaper::panel` base class — drivers for any ePaper
  controller (SSD1680, UC8179, ...) implement this single interface so
  application code can target any of them.
- ePaper-native vocabulary: `write` uploads pixels to the controller's RAM
  (invisible), `refresh` drives the physical ink update and blocks until the
  panel's BUSY line releases.
- Three refresh styles: `full` (highest quality, clears ghosting), `fast`
  (shortened full-screen waveform), and `partial` (flicker-free differential
  update of changed pixels only).
- 4-level grayscale via `set_color_mode(color_mode::gray4)` on drivers that
  support it.
- Deep-sleep power management (`sleep` / `wake`) with state tracking — writes
  and refreshes while asleep report `errc::invalid_state` instead of hanging
  on a dead controller.
- Automatic partial-refresh baseline tracking: the first refresh after
  construction, `wake`, or a color-mode change is silently promoted to a
  full refresh.
- `mono_framebuffer` (1 bpp) and `gray4_framebuffer` (2 bpp, two 1-bpp
  planes) value types in the row-major layouts ePaper controllers consume,
  uploadable at a pixel offset for banded rendering.
- Both framebuffers satisfy the `idfxx::gfx::pixel_surface` concept, so the
  `idfxx_gfx` drawing primitives (rectangles, lines, text) work on them
  directly.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- `idfxx_core` (error handling)
- `idfxx_gpio` (BUSY and reset lines)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_epaper:
    version: "^1.0.0"
```

Or add `idfxx_epaper` to the `REQUIRES` list in your component's
`CMakeLists.txt`.

This component is rarely used on its own — depend on a concrete driver such
as `idfxx_epaper_ssd1680` or `idfxx_epaper_uc8179`, which pulls in
`idfxx_epaper` automatically.

## Usage

`idfxx_epaper` defines no concrete driver; it's an interface. Application
code that talks to *any* ePaper display receives an
`idfxx::epaper::panel&`:

```cpp
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/panel>

void show_message(idfxx::epaper::panel& display) {
    idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
    fb.set_pixel(10, 20, true); // true = black ink; the buffer starts white

    fb.flush(display);   // upload to controller RAM — nothing visible yet
    display.refresh();   // drive the ink update; blocks until BUSY releases
    display.sleep();     // deep sleep between updates
}
```

Construct a concrete driver (e.g. `idfxx::epaper::ssd1680`) and pass it
where a `panel&` is expected.

The same flow with the result-based API:

```cpp
idfxx::result<void> show_message(idfxx::epaper::panel& display) {
    auto fb = idfxx::epaper::mono_framebuffer::make(display.width(), display.height());
    if (!fb) {
        return idfxx::error(fb.error());
    }
    fb->set_pixel(10, 20, true);

    if (auto r = fb->try_flush(display); !r) {
        return r;
    }
    if (auto r = display.try_refresh(); !r) {
        return r;
    }
    return display.try_sleep();
}
```

### Partial refresh

A partial refresh flips only the pixels that changed since the previous
refresh — no full-screen flash:

```cpp
fb.flush(display);
display.refresh(idfxx::epaper::refresh_mode::partial);
```

Partial updates accumulate ghosting; issue a `full` refresh periodically
(every ~10 partials is a good rule of thumb) to clean the panel. The first
refresh after construction, `wake`, or a color-mode change has no baseline
image to diff against and is silently promoted to `full`.

### Grayscale

```cpp
display.set_color_mode(idfxx::epaper::color_mode::gray4);

idfxx::epaper::gray4_framebuffer fb(display.width(), display.height());
fb.set_pixel(10, 20, idfxx::epaper::gray4::dark);
fb.flush(display);
display.refresh(); // gray4 supports full refresh only
```

### Drawing with idfxx_gfx

Both framebuffers are `idfxx::gfx::pixel_surface`s, so the gfx canvas and
text primitives draw on them directly, and `canvas.try_flush(display)`
completes the draw-then-upload cycle:

```cpp
idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
idfxx::gfx::canvas canvas(fb);
canvas.draw_text(idfxx::font::spleen_8x16, 4, 4, "23.7°C", true, 2);
canvas.flush(display);
display.refresh();
```

## API Overview

### `idfxx::epaper::panel` (abstract)

**Accessors:** `width`, `height`, `color_mode`, `asleep`.

**RAM upload:** `write(fb, x = 0, y = 0)` (overloaded for
`mono_framebuffer` and `gray4_framebuffer`) uploads at a pixel offset;
`write_rows(fb, row_start, row_end, x = 0, y = 0)` uploads a horizontal
band. Destination columns must be multiples of 8 (controller RAM is
byte-packed along the row). `clear()` fills the RAM with white without
needing a framebuffer, and promotes the next partial refresh to full.

**Refresh:** `refresh(mode = refresh_mode::full)` blocks until the update
completes; `wait()` / `wait_for(timeout)` explicitly wait on the BUSY line.

**Color mode:** `set_color_mode(color_mode)` switches between `mono` and
`gray4` operation.

**Power:** `sleep` enters deep sleep (a no-op when already asleep); `wake`
hardware-resets and re-initializes the controller.

Every method has a result-returning `try_*` form; the exception-throwing
forms above are inline wrappers guarded by `CONFIG_COMPILER_CXX_EXCEPTIONS`.

### Framebuffers

- `mono_framebuffer` — 1 bpp, row-major, MSB-first, byte-padded rows;
  `pixel_type` is `bool` (`true` = black ink). Starts all-white.
- `gray4_framebuffer` — 2 bpp as two 1-bpp planes (plane 0 = bit 0 of the
  level, plane 1 = bit 1); `pixel_type` is `epaper::gray4` (`white`,
  `light`, `dark`, `black`). Starts all-white.

Both provide `set_pixel` / `get_pixel` / `fill` / `clear`, raw data access
(`data()` / `row(y)` on mono, `plane(i)` / `plane_row(i, y)` on gray4), and
the flush family (`flush(panel, x = 0, y = 0)`, `flush_rows(panel, y_start,
y_end)`, plus `try_*` forms).

### Types

- `refresh_mode` — `full`, `fast`, `partial`.
- `color_mode` — `mono`, `gray4`.
- `gray4` — `white` (0) through `black` (3); a value-initialized `gray4{}` is
  white so `gfx::canvas::clear()` blanks to paper.

## Error Handling

The interface uses `idfxx::result<T>` and `idfxx::errc` from `idfxx_core`:

- `errc::invalid_arg` — unaligned destination column, out-of-bounds
  placement, invalid row range, or invalid framebuffer dimensions.
- `errc::invalid_state` — writing or refreshing while asleep, writing a
  framebuffer that doesn't match the panel's color mode, or waking a driver
  with no reset line configured.
- `errc::not_supported` — `fast`/`partial` refresh in gray4 mode, or a
  capability the concrete driver lacks.
- `errc::timeout` — the panel's BUSY line did not release in time.

Using a moved-from driver object is undefined behavior.

## Important Notes

- **Write, then refresh.** `write` only stages pixels in the controller's
  RAM; nothing changes on the glass until `refresh`. This is inherent to
  ePaper, not a quirk of the API.
- **Refresh blocks.** A full refresh takes on the order of seconds
  (waveform-dependent); the calling task sleeps in a poll loop on the BUSY
  line rather than busy-spinning.
- **Partial refreshes are full-scan differentials**: `write` places data
  anywhere in RAM, and `refresh(partial)` updates whatever changed across
  the whole panel without flashing. There is no region-limited refresh in
  this version of the interface.
- **Sleep between updates.** Leaving an ePaper controller active degrades
  the glass over time and wastes power; call `sleep()` whenever the display
  will sit idle, and `wake()` before the next update.
- Drivers keep an internal shadow copy of the last-written frame to
  maintain the controller's previous-image plane for partial refreshes —
  budget roughly one frame of heap per panel (see each driver's README for
  exact numbers).

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
