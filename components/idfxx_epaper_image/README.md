# idfxx_epaper_image

Decoded images on e-paper panels and surfaces, in a panel's measured colors, placed, sized and
oriented on the screen.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__epaper__image.html)**

## Features

- `write_image()` takes the image from a decoder (such as `idfxx::jpeg::decoder`) and shows it on an
  `idfxx::epaper::panel`: turned, cropped, resampled and clipped as an `idfxx::image::placement`
  describes, filling the screen by default
- Converts the picture to the panel's inks using a `calibration` of what those inks really look
  like, with error diffusion, in six colors, four grays or black and white
- Built-in calibrations for the EL133UF1 (reTerminal E1004) and the GDEY075T7 (XIAO EE05), and a
  choice of rendering intents for each
- Streams to the panel a band of rows at a time, so no whole frame is held in memory and an image
  far larger than the screen can be shown
- Pixels outside the target are left as they are
- An optional overlay callback draws over each band just before it is written, such as a caption
  drawn with `idfxx::gfx::canvas`
- The same call renders into any surface of `bool`, `gray4` or `color6` pixels: a framebuffer, or an
  `idfxx::gfx::rotated` or `idfxx::gfx::canvas` view of one
- `write_options::memory_limit` bounds the working memory, so a placement that needs too much fails
  with `image::errc::too_large` instead of exhausting the heap
- Every call comes as a throwing form and a `try_` form returning `idfxx::result<void>`

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core, idfxx_image, idfxx_epaper
- [`cleishm/epaper-image`](https://components.espressif.com/components/cleishm/epaper-image), as a
  public dependency: `calibration::make` users get the library's headers through it

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_epaper_image: "^1.0.0"
```

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/byte_source>
#include <idfxx/epaper/image_writer>
#include <idfxx/jpeg>

void show(idfxx::epaper::panel& display, std::span<const uint8_t> jpeg_bytes) {
    try {
        idfxx::memory_source src{jpeg_bytes};
        idfxx::jpeg::decoder dec(src);

        // Fill the screen, upright according to the photo's EXIF orientation.
        display.set_color_mode(idfxx::epaper::color_mode::color6);
        idfxx::epaper::write_image(display, idfxx::epaper::calibration::el133uf1(), dec);
        display.refresh();
    } catch (const std::system_error& e) {
        ESP_LOGE(TAG, "Showing the photo failed: %s", e.what());
    }
}
```

Placing the photo in a rectangle, with a caption drawn over each band:

```cpp
using namespace idfxx::epaper;

write_image<gray4_framebuffer>(
    display,
    calibration::gdey075t7_gray4(),
    dec,
    {
        .target = idfxx::image::rect{.x = 400, .y = 120, .width = 360, .height = 240},
        .layout = idfxx::image::layout::fit,
    },
    {.band_rows = 32},
    [&](gray4_framebuffer& band, size_t x, size_t y) {
        // a canvas the size of the screen: draw in screen coordinates
        idfxx::gfx::canvas canvas(band, x, y, display.width(), display.height());
        canvas.draw_text(idfxx::font::spleen_8x16, 404, 342, "caption", gray4::black);
    }
);
display.refresh();
```

Rendering into a framebuffer instead of a panel:

```cpp
const auto& cal = idfxx::epaper::calibration::gdey075t7_gray4();
idfxx::epaper::gray4_framebuffer fb(800, 480);
idfxx::epaper::write_image(fb, cal, dec);

display.write(fb);
display.refresh();
```

### Result-based API

Always available:

```cpp
idfxx::result<void> show(idfxx::epaper::panel& display, idfxx::byte_source auto& src) {
    auto dec = idfxx::jpeg::decoder::make(src);
    if (!dec) {
        return idfxx::error(dec.error());
    }
    if (auto r = display.try_set_color_mode(idfxx::epaper::color_mode::color6); !r) {
        return r;
    }
    if (auto r = idfxx::epaper::try_write_image(display, idfxx::epaper::calibration::el133uf1(), *dec); !r) {
        return r;
    }
    return display.try_refresh();
}
```

## API Overview

| Item | Description |
| ---- | ----------- |
| `write_image(panel, calibration, decoder[, placement[, options]])` / `try_write_image(...)` | Shows the decoder's image on a panel, a band of rows at a time. The placement defaults to filling the screen. The caller refreshes. |
| `write_image<Framebuffer>(panel, calibration, decoder[, placement[, options]], overlay)` / `try_write_image<Framebuffer>(...)` | As above, calling `overlay(Framebuffer& band, size_t x, size_t y)` with each band and the screen position of its top-left pixel just before the band is written. |
| `write_image(surface, calibration, decoder[, placement[, options]])` / `try_write_image(...)` | Renders into a surface (`pixel_type` of `bool`, `gray4` or `color6`, `noexcept` `set_pixel`, `width()`, `height()`). |
| `write_options::intent` | The rendering intent. By default, the calibration's first preset. |
| `write_options::band_rows` | Rows converted between writes to a panel (default 32). Zero is `invalid_arg`. |
| `write_options::memory_limit` | Optional cap, in bytes, on the memory for rendering, the conversion and the band together. |
| `calibration::el133uf1()`, `gdey075t7_mono()`, `gdey075t7_gray4()` | The built-in calibrations. |
| `calibration::make(panel)` / `calibration(panel)` | A calibration from a panel of the [`cleishm/epaper-image`](https://components.espressif.com/components/cleishm/epaper-image) library, such as a gray or color panel you have measured. |
| `calibration::name()`, `width()`, `height()`, `color_mode()` | What the calibration is for. |
| `calibration::presets()` | The panel's named `rendering_intent`s. The first is its default. |
| `calibration::preset(name)` / `try_preset(name)` | The `rendering_intent` of one of the panel's presets, such as `"Bright"`, ignoring ASCII case. |
| `calibration::inks()` | The panel's inks: the framebuffer's pixel `value`, and its `nominal` and `measured` colors. |
| `rendering_intent` | `tone`, `detail`, `adapt`, `vibrance`, `veil`, `lift` (all required). |

The decoder is anything that models `idfxx::image::decoder`. The placement (`target`, `layout`,
`orientation`, `crop`, `background`, `filter`, `fill_to_alignment`) is described by
`idfxx::image::placement`.

## Error Handling

| Error | Meaning |
| ----- | ------- |
| `idfxx::errc::invalid_arg` | The panel's size isn't the calibration's; a surface's pixel type or an overlay's band doesn't match the calibration's color mode; `band_rows` is zero; the rendering intent is out of range; or the placement isn't aligned. |
| `idfxx::errc::not_found` | `preset` or `try_preset` was given a name the panel has no preset for. |
| `idfxx::image::errc::too_large` | `memory_limit` is too small for the band, the conversion and the rendering. Reported before anything is allocated. |
| `idfxx::errc::invalid_size` | The image is too large to render, or the surface is over 65535 pixels a side. |
| `idfxx::errc::invalid_state` | The panel's color mode isn't the calibration's, or the panel is asleep. Reported before anything is decoded. |
| Decoder, placement or panel error | Returned unchanged: the decoder's error (such as `jpeg::errc::truncated`), the placement's (such as `invalid_arg` for a crop outside the image), or the panel's. |

Running out of memory is not an error code: as everywhere in idfxx, `std::bad_alloc` is thrown when
exceptions are enabled, and the program aborts otherwise. Set `memory_limit` to the memory that can
be spared.

## Important Notes

- **The caller refreshes.** `write_image` only uploads to the panel, in the color mode the panel is
  already in; nothing changes on the glass until `refresh()`. Set the color mode first: a panel in
  another one is `invalid_state`.
- **A calibration holds for the waveform it was measured with, which can't be detected.** A panel
  showing other colors than its calibration says will look wrong, without any error.
  `gdey075t7_gray4` holds only at `uc8179`'s default `gray4_frame_rate` and `gray4_source_voltage`.
- **Alignment.** A panel writes whole bytes, so on a panel the left edge of what is written must be
  a multiple of 2 columns in `color6`, or 8 in `mono` and `gray4`, and so must its right edge unless
  it reaches the right edge of the screen. Otherwise `write_image` returns `invalid_arg`, before
  decoding, unless `placement::fill_to_alignment` is set: the picture then lands exactly where it
  was placed, and the write extends to the alignment with `background`. A framebuffer or other
  surface has no such rule, whether or not it is written to a panel afterward at an aligned
  position.
- **Time.** A full EL133UF1 frame takes 70 to 90 s to convert when built with `-O2` at 240 MHz, as
  measured on the E1004, over twice that with ESP-IDF's default `-Og` at 160 MHz, and about 30 s to
  refresh. During the conversion the task gives way for one tick about every half second, so that
  other tasks and the idle task the watchdog looks after can run.
- **Memory for the conversion.** About 193 KB for a picture 1200 wide, and about 88 KB for one 800
  wide, plus the band: the picture's width times `band_rows` at 2 pixels a byte in `color6`, 8 a
  byte in `mono` and 4 a byte in `gray4`. `memory_limit` counts both at the full screen width.
- **Memory for turned pictures.** Only a picture displayed upright or flipped horizontally streams.
  Any other orientation (a quarter turn, a half turn or a vertical flip), whether from the photo's
  EXIF orientation or from `placement::orientation`, is held as one frame of the output size, in
  RGB888, until the last source row has been read: 5.76 MB for a full EL133UF1 screen and about 1.15
  MB for a full GDEY075T7 one. `memory_limit` counts it.
- **Progressive JPEGs** hold about 3 bytes per source pixel in the decoder at any output scale,
  beyond all of the above: a 12-megapixel one needs about 36 MB. Set the decoder's `memory_limit` to
  have it fail with `image::errc::too_large`.
- **No `swap_xy` on e-paper.** Placement coordinates are always the panel's native ones. To show a
  photo upright on a display mounted a quarter turn from the panel's native layout, use
  `placement::orientation`, or render into a `gfx::rotated` surface and write the framebuffer to the
  panel. Pick the quarter turn that cancels the photo's own orientation if you can: a photo stored
  the way it's displayed needs no frame.
- **The overlay runs once per band, not once per image,** and may be called with a band that only
  partly belongs to the picture. It must draw the same thing each time: draw in screen coordinates
  (`gfx::canvas canvas(band, x, y, screen_width, screen_height)`) and let the canvas clip. The last
  band's unused rows are not written to the panel. The caller names the band's type,
  `write_image<gray4_framebuffer>(...)`, and it must match the calibration's color mode.
- **A decoder decodes once.** Create a new decoder (over a new source) to show the image again.

## License

Apache-2.0
