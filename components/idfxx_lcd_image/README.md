# idfxx_lcd_image

Decoded images on RGB565 LCD panels and surfaces, placed, sized and oriented on the screen.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__lcd__image.html)**

## Features

- `write_image()` takes the image from a decoder (such as `idfxx::jpeg::decoder`) and shows it on an
  `idfxx::lcd::panel`: turned, cropped, resampled and clipped as an `idfxx::image::placement`
  describes, filling the screen by default
- Streams to the panel a band of rows at a time, so no whole frame is held in memory and an image
  far larger than the screen can be shown
- Pixels outside the target are left as they are
- An optional overlay callback draws over each band just before it is written, such as a caption
  drawn with `idfxx::gfx::canvas`
- The same call renders into any RGB565 surface: an `idfxx::lcd::rgb565_framebuffer`, or an
  `idfxx::gfx::rotated` or `idfxx::gfx::canvas` view of one
- `write_options::memory_limit` bounds the memory for rendering and the band together, so a
  placement that needs too much fails with `image::errc::too_large` instead of exhausting the heap
- Every call comes as a throwing form and a `try_` form returning `idfxx::result<void>`

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core, idfxx_image, idfxx_lcd

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_lcd_image: "^1.0.0"
```

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/byte_source>
#include <idfxx/jpeg>
#include <idfxx/lcd/image_writer>

void show(idfxx::lcd::panel& display, std::span<const uint8_t> jpeg_bytes) {
    try {
        idfxx::memory_source src{jpeg_bytes};
        idfxx::jpeg::decoder dec(src);

        // Fill the screen, upright according to the photo's EXIF orientation.
        idfxx::lcd::write_image(display, dec);
    } catch (const std::system_error& e) {
        ESP_LOGE(TAG, "Showing the photo failed: %s", e.what());
    }
}
```

Placing the photo in a rectangle, with a caption drawn over each band:

```cpp
idfxx::lcd::write_image(
    display,
    dec,
    {
        .target = idfxx::image::rect{.x = 10, .y = 40, .width = 220, .height = 240},
        .layout = idfxx::image::layout::fit,
        .background = {0, 0, 0},
    },
    {.band_rows = 16},
    [&](idfxx::lcd::rgb565_framebuffer& band, size_t x, size_t y) {
        // a canvas the size of the screen: draw in screen coordinates
        idfxx::gfx::canvas canvas(band, x, y, display.display_width(), display.display_height());
        canvas.draw_text(idfxx::font::spleen_8x16, 14, 262, "caption", {255, 255, 255});
    }
);
```

Rendering into a framebuffer instead of a panel:

```cpp
idfxx::lcd::rgb565_framebuffer fb(240, 320);
idfxx::lcd::write_image(fb, dec);
display.write(fb);
```

### Result-based API

Always available:

```cpp
idfxx::result<void> show(idfxx::lcd::panel& display, idfxx::byte_source auto& src) {
    auto dec = idfxx::jpeg::decoder::make(src);
    if (!dec) {
        return idfxx::error(dec.error());
    }
    return idfxx::lcd::try_write_image(display, *dec);
}
```

## API Overview

| Item | Description |
| ---- | ----------- |
| `write_image(panel, decoder[, placement[, options]])` / `try_write_image(...)` | Shows the decoder's image on a panel, a band of rows at a time. The placement defaults to filling the screen. |
| `write_image(panel, decoder[, placement[, options]], overlay)` / `try_write_image(...)` | As above, calling `overlay(rgb565_framebuffer& band, size_t x, size_t y)` with each band and the screen position of its top-left pixel just before the band is written. |
| `write_image(surface, decoder[, placement[, options]])` / `try_write_image(...)` | Renders into an RGB565 surface (`pixel_type` of `rgb565`, `noexcept` `set_pixel`, `width()`, `height()`). |
| `write_options::band_rows` | Rows buffered per write to a panel (default 16). Zero is `invalid_arg`. |
| `write_options::memory_limit` | Optional cap, in bytes, on the memory for rendering and the band together. |

The decoder is anything that models `idfxx::image::decoder`. The placement (`target`, `layout`,
`orientation`, `crop`, `background`, `filter`) is described by `idfxx::image::placement`.

## Error Handling

| Error | Meaning |
| ----- | ------- |
| `idfxx::errc::invalid_arg` | `band_rows` is zero. |
| `idfxx::image::errc::too_large` | `memory_limit` is too small for the band and the rendering. Reported before anything is allocated. |
| `idfxx::errc::invalid_size` | The image is too large to render. |
| Decoder, placement or panel error | Returned unchanged: the decoder's error (such as `jpeg::errc::truncated`), the placement's (such as `invalid_arg` for a crop outside the image), or the panel's. |

Running out of memory is not an error code: as everywhere in idfxx, `std::bad_alloc` is thrown when
exceptions are enabled, and the program aborts otherwise. Set `memory_limit` to the memory that can
be spared.

## Important Notes

- **The screen is the panel's addressed area.** The default placement and the clipping use
  `panel::display_width()` and `display_height()`, which exchange the panel's native width and
  height after `swap_xy(true)`. Placement coordinates are those of the screen as it is addressed, so
  call `swap_xy` before `write_image`.
- **A turned picture** is set with `placement::orientation`, applied on top of the photo's own EXIF
  orientation. To show a photo upright on a display mounted a quarter turn from the panel's native
  layout, use `orientation::rotate_90` or `rotate_270` rather than `swap_xy`, or use `swap_xy` and
  write in the swapped coordinates.
- **Memory for turned pictures.** Only a picture displayed upright or flipped horizontally streams.
  Any other orientation (a quarter turn, a half turn or a vertical flip) is held as one frame of the
  output size, in RGB888 (3 bytes per pixel), until the last source row has been read.
  `memory_limit` counts it.
- **The band** is the output width times `band_rows`, at 2 bytes per pixel, in addition to the
  renderer's memory; `memory_limit` counts it at the full display width. Larger bands make fewer,
  larger transfers.
- **SPI panels.** The band is an ordinary `rgb565_framebuffer`, so its memory is not necessarily
  DMA-capable. The SPI driver copies such a buffer through an internal DMA-capable one, so this
  works, at the cost of that copy and its memory. The SPI bus's `max_transfer_sz` must be at least
  the band's size: `display_width * band_rows * 2` bytes.
- **The overlay runs once per band, not once per image,** and may be called with a band that only
  partly belongs to the picture. It must draw the same thing each time: draw in screen coordinates
  (`gfx::canvas canvas(band, x, y, screen_width, screen_height)`) and let the canvas clip. The last
  band's unused rows are not written to the panel.
- **No dithering.** Pixels are converted to RGB565 by truncation. Monochrome LCDs are not supported:
  `write_image` does not compile for `mono_framebuffer`.
- **A decoder decodes once.** Create a new decoder (over a new source) to show the image again.

## License

Apache-2.0
