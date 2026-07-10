# idfxx_gfx

Drawing primitives for pixel surfaces: rectangles, lines, and bitmap-font text.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__gfx.html)**

## Features

- Works on any pixel surface via the `pixel_surface` concept: anything with
  `set_pixel(x, y, pixel)` and reported dimensions, including
  `idfxx::lcd::mono_framebuffer` (bool pixels) and
  `idfxx::lcd::rgb565_framebuffer` (RGB565 pixels)
- `canvas`: a lightweight view bundling a surface with the drawing
  primitives, so drawing reads as members on one object
- Band rendering: `render_banded` renders a full frame authored in screen
  coordinates through a small framebuffer one band at a time, with correct
  clipping on all four sides; translated canvases underneath it support
  custom render loops, and `window()` gives a clipped sub-region canvas
  with widget-local coordinates
- Filled and outlined rectangles, horizontal/vertical/arbitrary lines
- Bitmap-font text rendering with integer scaling (`scale = 2` turns an 8x16
  font into 16x32 glyphs)
- Ink-only rendering: only the requested pixels are written, so drawing
  composes over existing content; on monochrome surfaces `ink = false`
  draws inverse text over filled regions
- Header-only, zero per-pixel dispatch overhead — the ink value is typed to
  the surface's pixel type at compile time
- All drawing clips at surface edges and never fails

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- Components: `idfxx_font` (font packages such as `idfxx_font_spleen`
  provide the fonts themselves)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_gfx: "^1.0.0"
  cleishm/idfxx_font_spleen: "^1.0.0"   # bundled fonts, if drawing text
```

## Usage

### Monochrome (SSD1306 OLED)

```cpp
#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/lcd/mono_framebuffer>

idfxx::lcd::mono_framebuffer fb(display.width(), display.height());
idfxx::gfx::canvas canvas(fb);

// Filled banner with inverse small text.
canvas.fill_rect(0, 0, canvas.width(), 10, true);
canvas.draw_text(idfxx::font::spleen_5x8, 2, 1, "status", false);

// Big headline (16x32 per glyph).
canvas.draw_text(idfxx::font::spleen_8x16, 4, 16, "23.7", true, 2);

canvas.flush(display);
```

### Color (ILI9341)

```cpp
#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/lcd/rgb565_framebuffer>

using idfxx::lcd::rgb565;

// Band-sized framebuffer: render a 240x320 frame in 240x40 slices.
idfxx::lcd::rgb565_framebuffer band(240, 40);
idfxx::gfx::canvas canvas(band);

canvas.fill({0, 0, 96});
canvas.draw_text(idfxx::font::spleen_8x16, 8, 4, "idfxx gfx", rgb565(255, 255, 255), 2);
canvas.flush(panel, 0, 0);          // top band

canvas.clear();
canvas.fill_rect(8, 8, 60, 24, rgb565(255, 0, 0));
canvas.draw_rect(6, 6, 64, 28, rgb565(255, 255, 255));
canvas.flush(panel, 0, 40);         // next band down
```

### Band rendering

A full frame at 16 bpp is large (a 240x320 frame is ~150 KB), so color
frames are usually rendered through a band-sized framebuffer.
`render_banded` takes a callback that draws the whole frame in screen
coordinates, runs it once per band, and flushes each slice to its screen
position — content straddling a band boundary clips on all four sides and
renders exactly its visible part:

```cpp
idfxx::lcd::rgb565_framebuffer band(240, 40);
idfxx::gfx::render_banded(band, panel, 320, [&](auto& canvas) {
    // The whole 240x320 frame, in screen coordinates. Invoked once per
    // band, so draw the complete frame as a pure function of your state.
    canvas.draw_text(idfxx::font::spleen_8x16, 8, 8, "title", white, 2);
    canvas.draw_line(0, 0, 239, 319, green); // crosses every band
});
```

Under the hood each pass uses a translated canvas — `canvas(band, 0, y)`
declares that the band's top-left corner sits at (0, y) in the frame — and
that constructor is available directly for custom render loops (scrolling
viewports, partial updates). The inverse mapping is
`canvas.window(x, y, w, h)`: a sub-region canvas with its own local
coordinates and clipping, e.g. for widget-local drawing.

### Free functions

Every `canvas` drawing member is also available as a free function taking
the surface as its first argument — useful in generic code that doesn't
want to construct a view:

```cpp
idfxx::gfx::fill_rect(fb, 0, 0, fb.width(), 10, true);
idfxx::gfx::draw_text(fb, idfxx::font::spleen_5x8, 2, 1, "status", false);
```

## API Overview

| Item | Description |
| ---- | ----------- |
| `pixel_surface` | Concept: `pixel_type`, `set_pixel(x, y, pixel)`, `width()`, `height()`. |
| `canvas(surface)` | Drawing view: the operations below as members, plus `fill(ink)` / `clear()` (using the surface's own fill/clear when present) and `flush(...)` / `try_flush(...)` (forwarding to the surface's, when it has them). |
| `canvas(surface, x, y)` | Translated canvas: the surface holds the region of a larger drawing space whose top-left corner is (x, y) — see Band rendering above. |
| `canvas.window(x, y, w, h)` | Sub-region canvas with local coordinates and clipping; `fill`/`clear` affect only the sub-region. |
| `render_banded(band, dest, frame_h, draw)` | Render a frame taller than the band: invokes `draw(canvas)` once per band and flushes each slice (also `try_render_banded`). |
| `fill_rect(s, x, y, w, h, ink)` | Fill a rectangle. |
| `draw_rect(s, x, y, w, h, ink)` | Outline a rectangle (one-pixel border). |
| `draw_hline(s, x, y, len, ink)` / `draw_vline(...)` | Horizontal / vertical line. |
| `draw_line(s, x0, y0, x1, y1, ink)` | Line between two points (endpoints inclusive). |
| `draw_text(s, font, x, y, text, ink, scale)` | Draw text; on bool surfaces `ink` defaults to true. |

Text measurement (`idfxx::font::text_width`) lives in `idfxx_font`.

## Error Handling

Drawing functions are `noexcept` and cannot fail: coordinates outside the
surface are clipped, and a `scale` of 0 is treated as 1. There are no error
codes and no `try_*` variants — nothing needs them. The exceptions are the
operations that transfer to a display: `canvas.flush(...)` /
`canvas.try_flush(...)` forward to the underlying surface and report
whatever it reports (for the idfxx framebuffers, panel I/O errors), and
`render_banded` / `try_render_banded` report flush errors plus
`idfxx::errc::invalid_arg` when the frame height is not a multiple of the
band height.

## Important Notes

- Drawing is ink-only: to replace previous content, clear (or redraw) the
  background first — `canvas.clear()` + redraw is the simplest pattern.
- A `canvas` is a non-owning view: the surface must outlive it. It itself
  satisfies `pixel_surface`, so it can be passed anywhere a surface is
  expected. Its coordinate bounds are captured at construction.
- The `pixel_surface` concept is structural: any user-defined type with a
  matching `set_pixel`/`width`/`height` shape works, no inheritance needed.
- This component is deliberately small: integer coordinates, one-pixel
  strokes, no anti-aliasing, no widgets, no layout. For a full UI toolkit,
  use LVGL with the panel's `idf_handle()`.

## License

Apache-2.0
