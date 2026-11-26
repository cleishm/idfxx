# idfxx_epaper

Abstract ePaper panel interface, framebuffers, and shared types.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__epaper.html)**

## Features

- Abstract `idfxx::epaper::panel` base class. Drivers for any ePaper controller or panel (SSD1680,
  UC8179, EL133UF1, ...) implement this single interface, so that application code can target any of
  them.
- ePaper-native vocabulary: `write` uploads pixels to the controller's RAM, where they are not
  visible yet. `refresh` drives the physical ink update and blocks until the panel's BUSY line
  releases. `start_refresh` instead returns an `idfxx::future`, so that the calling task can do
  other work while the glass updates.
- Three refresh styles: `full` (highest quality, clears ghosting), `fast` (shortened full-screen
  waveform), and `partial` (flicker-free differential update of changed pixels only).
- 4-level grayscale via `set_color_mode(color_mode::gray4)` and six-color (E Ink Spectra 6)
  operation via `set_color_mode(color_mode::color6)` on drivers that support them.
  `supports(color_mode)` reports which modes a driver supports.
- Selectable border drive (`set_border`: white, black, or held) on drivers whose glass supports it.
  `supports(border)` reports which border values a driver supports.
- Deep-sleep power management (`sleep` / `wake`) with state tracking. Writes and refreshes while the
  panel is asleep report `errc::invalid_state` instead of waiting for a controller that cannot
  respond.
- Automatic partial-refresh baseline tracking: the first refresh after construction, `wake`, or a
  color-mode change is silently promoted to a full refresh, unless `restore_baseline` re-establishes
  the baseline from a saved frame (e.g. after an ESP32 deep sleep).
- `mono_framebuffer` (1 bpp), `gray4_framebuffer` (2 bpp, two 1-bpp planes), and
  `color6_framebuffer` (4 bpp, two pixels per byte) value types in the row-major layouts that ePaper
  controllers consume. They can be uploaded at a pixel offset for banded rendering.
- All framebuffers satisfy the `idfxx::gfx::pixel_surface` concept, so the `idfxx_gfx` drawing
  primitives (rectangles, lines, text) work on them directly.
- `write_inks` writes a frame of panel inks to a panel or a framebuffer. The frame is converted
  elsewhere and read from any `idfxx::byte_source`, a band of rows at a time. `write_inks` checks
  the frame's length and its six-color values as it reads.

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
    version: "^2.0.0"
```

Or add `idfxx_epaper` to the `REQUIRES` list in your component's `CMakeLists.txt`.

This component is rarely used on its own. Depend on a concrete driver such as
`idfxx_epaper_ssd1680`, `idfxx_epaper_uc8179`, or `idfxx_epaper_el133uf1`, which adds
`idfxx_epaper` to the build automatically.

## Usage

`idfxx_epaper` defines no concrete driver. It is an interface. Application code that talks to *any*
ePaper display receives an `idfxx::epaper::panel&`:

```cpp
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/panel>

void show_message(idfxx::epaper::panel& display) {
    idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
    fb.set_pixel(10, 20, true); // true = black ink. The buffer starts white.

    display.write(fb);   // upload to controller RAM (nothing is visible yet)
    display.refresh();   // drive the ink update, and block until BUSY releases
    display.sleep();     // deep sleep between updates
}
```

Construct a concrete driver (e.g. `idfxx::epaper::ssd1680`) and pass it where a `panel&` is
expected.

The same flow with the result-based API:

```cpp
idfxx::result<void> show_message(idfxx::epaper::panel& display) {
    auto fb = idfxx::epaper::mono_framebuffer::make(display.width(), display.height());
    if (!fb) {
        return idfxx::error(fb.error());
    }
    fb->set_pixel(10, 20, true);

    if (auto r = display.try_write(*fb); !r) {
        return r;
    }
    if (auto r = display.try_refresh(); !r) {
        return r;
    }
    return display.try_sleep();
}
```

### Partial refresh

A partial refresh flips only the pixels that changed since the previous refresh. There is no
full-screen flash:

```cpp
display.write(fb);
display.refresh(idfxx::epaper::refresh_mode::partial);
```

Partial updates accumulate ghosting. Issue a `full` refresh periodically to clean the panel. A full
refresh after about every 10 partial refreshes is a good guideline. The first refresh after
construction, `wake`, or a color-mode change has no baseline image to diff against. So it is
silently promoted to `full`, unless `restore_baseline` re-establishes a baseline (see Important
Notes).

### Refreshing without blocking

`refresh` blocks for the duration of the update, which takes seconds for a full refresh.
`start_refresh` starts the same update and returns an `idfxx::future<void>` instead. The calling
task can do other work while the glass updates, and then wait, poll, or simply drop the handle:

```cpp
display.write(fb);
auto done = display.start_refresh();
read_sensors();                 // the glass updates during this call
done.wait();                    // or: if (done.done()) { ... }
display.sleep();
```

The panel cannot be commanded while the glass is updating. So the next panel operation
(`write`, `clear`, `refresh`, `set_color_mode`, `sleep`) first completes an outstanding update. A
task that refreshes once a minute and then puts the panel to sleep therefore blocks only in
`sleep()`, for the part of the update that is still running. Dropping the future is safe. To check
later whether the update is done, keep the future.

### Grayscale

```cpp
display.set_color_mode(idfxx::epaper::color_mode::gray4);

idfxx::epaper::gray4_framebuffer fb(display.width(), display.height());
fb.set_pixel(10, 20, idfxx::epaper::gray4::dark);
display.write(fb);
display.refresh(); // gray4 supports full refresh only
```

### Six-color

Spectra 6 panels take a 4-bit color code per pixel, packed two per byte. So a `color6_framebuffer`
upload must start at an even column, rather than at the multiple of 8 that the 1-bpp formats need:

```cpp
display.set_color_mode(idfxx::epaper::color_mode::color6);

idfxx::epaper::color6_framebuffer fb(display.width(), display.height());
fb.set_pixel(10, 20, idfxx::epaper::color6::red);
display.write(fb);
display.refresh(); // color6 has no partial refresh
```

A full 1200x1600 frame is 960 KB, so this framebuffer is usually in PSRAM. With
`CONFIG_SPIRAM_USE_MALLOC=y` (and the default `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`), `std::vector`
places it there automatically. Without PSRAM, draw through a band-sized framebuffer with
`idfxx::gfx::render_banded` instead.

### Drawing with idfxx_gfx

All framebuffers are `idfxx::gfx::pixel_surface`s, so the gfx canvas and text primitives draw on
them directly. Upload the framebuffer that the canvas draws on with `display.write(fb)`:

```cpp
idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
idfxx::gfx::canvas canvas(fb);
canvas.draw_text(idfxx::font::spleen_8x16, 4, 4, "23.7°C", true, 2);
display.write(fb);
display.refresh();
```

### Writing a frame of inks from a byte source

A phone or browser can convert a picture to a panel's inks and push the result to the device. The
device then only has to check it and put it on the panel:

```cpp
#include <idfxx/epaper/ink_writer>

void show_frame(idfxx::epaper::panel& display, idfxx::byte_source auto& connection) {
    display.set_color_mode(idfxx::epaper::color_mode::color6);
    idfxx::epaper::write_inks(display, connection); // the whole panel
    display.refresh();
}
```

The same flow with the result-based API:

```cpp
idfxx::result<void> show_frame(idfxx::epaper::panel& display, idfxx::byte_source auto& connection) {
    if (auto r = display.try_set_color_mode(idfxx::epaper::color_mode::color6); !r) {
        return r;
    }
    if (auto r = idfxx::epaper::try_write_inks(display, connection); !r) {
        return r;
    }
    return display.try_refresh();
}
```

A frame is rows from top to bottom with no header. Each row is padded to a whole byte. The panel's
`color_mode()`, or the framebuffer's type, selects the format. In the table, `w` is the width of the
rectangle being written:

| Mode     | Bits per pixel | Order             | Pixel values                      | Bytes in a row |
| -------- | -------------- | ----------------- | --------------------------------- | -------------- |
| `color6` | 4              | high nibble first | `color6` values: 0, 1, 2, 3, 5, 6 | `(w + 1) / 2`  |
| `gray4`  | 2              | high bits first   | 0 is darkest, 3 is lightest (the reverse of `gray4` values) | `(w + 3) / 4`  |
| `mono`   | 1              | high bit first    | 1 is white, 0 is black            | `(w + 7) / 8`  |

The `cleishm/epaper-image` library's `epi_pack_row` produces these rows.

`ink_options` places a frame on part of the destination. Without a `width` or `height`, the
rectangle extends to the destination's edge:

```cpp
// A 400x300 frame at (80, 40).
idfxx::epaper::write_inks(display, connection, {.x = 80, .y = 40, .width = 400, .height = 300});
```

To verify a frame, wrap the source in a `byte_source` of your own that computes a CRC of the bytes
as they are read. Compare the CRC before refreshing:

```cpp
#include <esp_rom_crc.h>

template<idfxx::byte_source Source>
class crc_source {
public:
    explicit crc_source(Source& source) : _source(source) {}

    idfxx::result<size_t> try_read(std::span<uint8_t> buf) {
        auto n = _source.try_read(buf);
        if (n) {
            _crc = esp_rom_crc32_le(_crc, buf.data(), *n);
        }
        return n;
    }

    uint32_t crc() const noexcept { return _crc; }

private:
    Source& _source;
    uint32_t _crc = 0;
};

crc_source checked(connection);
idfxx::epaper::write_inks(display, checked);
// The bytes after the frame are still unread, so the CRC can follow it.
if (checked.crc() == read_expected_crc(connection)) {
    display.refresh();
}
```

## API Overview

### `idfxx::epaper::panel` (abstract)

**Accessors:** `width`, `height`, `color_mode`, `asleep`.

**RAM upload:** `write(fb, x = 0, y = 0)` (overloaded for `mono_framebuffer`, `gray4_framebuffer`,
and `color6_framebuffer`) uploads at a pixel offset.
`write_rows(fb, row_start, row_end, x = 0, y = 0)` uploads a horizontal band. Destination columns
must be a multiple of the framebuffer's `x_alignment`. This is 8 for the 1-bpp formats, because
controller RAM is byte-packed along the row, and 2 for `color6_framebuffer`, which has two pixels
per byte. Rows are written in whole bytes too. So a framebuffer whose width is not a multiple of the
`x_alignment` also writes its white row padding over the panel columns up to the next byte
boundary. To leave the neighboring columns untouched, make the width of a partial framebuffer a
multiple of the `x_alignment`. `clear()` fills
the RAM with white without needing a framebuffer, and promotes the next partial refresh to full.

**Partial-refresh baseline:** `restore_baseline(fb)` / `try_restore_baseline(fb)` load a panel-sized
`mono_framebuffer` that holds the image already on the glass. The next partial refresh then diffs
against it instead of being promoted to full. Drivers without support report `errc::not_supported`.

**Refresh:** `refresh(mode = refresh_mode::full)` blocks until the update completes.
`start_refresh(mode = refresh_mode::full)` returns an `idfxx::future<void>` that completes when the
update completes.

**Color mode:** `set_color_mode(color_mode)` switches between `mono`, `gray4`, and `color6`
operation. Every driver supports `mono`. `supports(color_mode)` reports whether a driver supports
the others. `set_color_mode` rejects a mode that the driver doesn't support.

**Border:** `set_border(border)` / `try_set_border(border)` select how the border is driven on full
and fast refreshes (`border()` reports the current setting). `supports(border)` reports which values
the driver accepts.

**Power:** `sleep` enters deep sleep (a no-op when already asleep). `wake` hardware-resets and
re-initializes the controller.

Every method has a result-returning `try_*` form. The exception-throwing forms above are inline
wrappers guarded by `CONFIG_COMPILER_CXX_EXCEPTIONS`.

### Framebuffers

- `mono_framebuffer` — 1 bpp, row-major, MSB-first, byte-padded rows. `pixel_type` is `bool`
  (`true` = black ink). Starts all-white.
- `gray4_framebuffer` — 2 bpp as two 1-bpp planes (plane 0 = bit 0 of the level, plane 1 = bit 1).
  `pixel_type` is `epaper::gray4` (`white`, `light`, `dark`, `black`). Starts all-white.
- `color6_framebuffer` — 4 bpp, row-major, two pixels per byte (high nibble = the even/left pixel),
  byte-padded rows. `pixel_type` is `epaper::color6` (`black`, `white`, `yellow`, `red`, `blue`,
  `green`). Starts all-white.

All provide `set_pixel` / `get_pixel` / `fill` / `clear`, an `x_alignment` constant, and raw data
access: `data()` / `row(y)` on mono and color6, and `plane(i)` / `plane_row(i, y)` on gray4.
`data()` and `plane(i)` are also writable, e.g. to load a saved frame in one copy. Upload them with
the panel's `write` / `write_rows`.

### Writing inks from a byte source

`write_inks(destination, source, options = {})` and
`try_write_inks(destination, source, options = {})` read a frame of inks from any
`idfxx::byte_source` and write it to a `panel`, `mono_framebuffer`, `gray4_framebuffer`, or
`color6_framebuffer`. `ink_options` holds the rectangle to fill (`x`, `y`, and optional `width` and
`height`) and `band_rows`, the number of rows read at a time. More rows mean fewer panel writes and
more memory.

See `examples/inks_ee05` for a four-gray frame shown on the XIAO EE05.

### Types

- `refresh_mode` — `full`, `fast`, `partial`.
- `color_mode` — `mono`, `gray4`, `color6`.
- `border` — `white` (default), `black`, `hold`.
- `gray4` — `white` (0) through `black` (3). A value-initialized `gray4{}` is white, so
  `gfx::canvas::clear()` blanks to paper.
- `color6` — the panel-native Spectra 6 codes: `black` (0), `white` (1), `yellow` (2), `red` (3),
  `blue` (5), `green` (6). Note that a value-initialized `color6{}` is **black** (see Important
  Notes).

## Error Handling

The interface uses `idfxx::result<T>` and `idfxx::errc` from `idfxx_core`:

- `errc::invalid_arg` — unaligned destination column, out-of-bounds placement, invalid row range,
  invalid framebuffer dimensions, or a `restore_baseline` framebuffer that isn't panel-sized.
- `errc::invalid_state` — writing or refreshing while asleep, writing a framebuffer that doesn't
  match the panel's color mode, restoring a baseline while asleep or outside `color_mode::mono`, or
  waking a driver with no reset line configured.
- `errc::not_supported` — `fast`/`partial` refresh in gray4 mode, `partial` in color6, a color mode
  the concrete driver doesn't support (`supports` returns false), or another capability it lacks.
- `errc::timeout` — the panel's BUSY line did not release in time.

`write_inks` checks everything it can before reading any byte from the source:

- `errc::invalid_state` — the panel is asleep.
- `errc::invalid_arg` — `band_rows` is zero, the rectangle's size is zero, or the rectangle goes
  past the destination's edge. A frame has a fixed number of bytes, so it is never clipped. The same
  error is returned for a left edge that isn't a multiple of the mode's `x_alignment`, or for a
  right edge that isn't a multiple of it and isn't the destination's right edge. This also applies
  to a framebuffer, because the input is packed.

And while reading:

- `errc::invalid_size` — the stream ended before the frame did.
- `errc::invalid_arg` — a nibble in a `color6` frame isn't a `color6` value, including a row's
  padding nibble. No part of that band is written.
- Any error from the source or the panel stops the write, and `write_inks` returns it unchanged.

Using a moved-from driver object is undefined behavior.

## Important Notes

- **Write, then refresh.** `write` only stages pixels in the controller's RAM. Nothing changes on
  the glass until `refresh`. This is inherent to ePaper, not a quirk of the API.
- **Refresh blocks. start_refresh doesn't.** A full refresh takes seconds, depending on the
  waveform. `refresh` sleeps the calling task in a poll loop on the BUSY line. It polls once per
  RTOS tick, and does not busy-spin. `start_refresh` returns immediately, and the next panel
  operation waits for the rest of the update.
- **Partial refreshes scan the whole panel.** `write` places data anywhere in RAM, and
  `refresh(partial)` updates all changed pixels across the whole panel without flashing. There is no
  region-limited refresh in this version of the interface.
- **Sleep between updates.** Leaving an ePaper controller active degrades the glass over time and
  wastes power. Call `sleep()` whenever the display will be idle, and `wake()` before the next
  update.
- **Keeping partial refreshes across deep sleep.** An ESP32 deep sleep is a reboot. Both a deep
  sleep and `wake()` lose the partial-refresh baseline, so the next partial refresh would be
  promoted to full. To avoid that, persist the frame on the glass (for example, to flash or an SD
  card) before sleeping. After constructing the driver, pass it back with `restore_baseline`
  (mono only):

  ```cpp
  // Before sleeping:
  save_frame(fb.data());   // application storage
  display.sleep();

  // After the reboot, once the driver is constructed:
  idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
  if (load_frame(fb.data())) {   // false if nothing valid was saved
      display.restore_baseline(fb);
  }
  fb.set_pixel(10, 20, true);    // draw the changes
  display.write(fb);
  display.refresh(idfxx::epaper::refresh_mode::partial);
  ```

  The saved frame must match the glass. A mismatch does no damage, but the differing pixels are not
  driven correctly until the next full refresh. `idfxx_epaper_ssd1680`'s
  `examples/deep_sleep_partial` shows the full cycle, keeping the frame in RTC memory across timer
  wakes.
- **`color6{}` is black, not white.** The six-color codes are the panel's own, and code 0 is black.
  `color6_framebuffer` starts white, and its `clear()` blanks to white. A `gfx::canvas` that covers
  the whole framebuffer calls that `clear()`. But a *windowed* canvas (one placed at an offset, or
  narrower than its surface) cannot call a whole-surface clear. It fills with the value-initialized
  pixel instead, inking the window black. Use `canvas.fill(color6::white)` there.
- **`write_inks` cannot detect all corruption.** A `color6` frame can contain invalid values. But in
  1 bpp and 2 bpp frames every value is a valid ink, so `write_inks` can check only their length,
  and it cannot detect corruption. Verify a frame with a CRC if it matters (see Usage).
- **A frame for other glass can't be detected.** A frame made for a different panel of the same size
  and mode is accepted. The application must check which panel a frame is for, for example when it
  connects.
- **`write_inks` stops at the end of the frame.** Bytes after the frame aren't read or checked, and
  `write_inks` does not wait for the end of the stream. So an open connection doesn't block, and a
  CRC can follow the frame.
- **After a failed `write_inks`, write the frame again before refreshing.** Earlier bands stay in
  the panel's memory. After a panel error, part of the band being written may also stay there. The
  glass doesn't change until a refresh, but any later refresh shows them. The partial-refresh
  baseline isn't affected: it remains what is on the glass, so a partial refresh after the frame
  has been written again is correct. To write all of the frame or none of it, write the frame to a
  framebuffer first, and then write the framebuffer to the panel. This uses a frame's memory.
- **A row's padding is always written as white,** whatever value the frame contains, so a frame
  can't change a framebuffer's padding. A `color6` padding nibble must still be a valid value.
- Drivers keep an internal shadow copy of the last-written frame to maintain the controller's
  previous-image plane for partial refreshes. Allow roughly one frame of heap per panel (see each
  driver's README for exact numbers).

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
