# idfxx_gfx

Drawing primitives for pixel surfaces: rectangles, lines, and bitmap-font text.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__gfx.html)**

## Features

- Works on any pixel surface via the `pixel_surface` concept: anything with `set_pixel(x, y, pixel)`
  and reported dimensions, including `idfxx::lcd::mono_framebuffer` (bool pixels) and
  `idfxx::lcd::rgb565_framebuffer` (RGB565 pixels)
- `canvas`: a lightweight view bundling a surface with the drawing primitives, so drawing reads as
  members on one object
- Band rendering: `render_banded` renders a full frame authored in screen coordinates through a
  small framebuffer one band at a time, with correct clipping on all four sides; translated canvases
  underneath it support custom render loops, and `window()` gives a clipped sub-region canvas with
  widget-local coordinates
- `rotated`: a quarter-turn view for a surface mounted sideways (a portrait-native ePaper panel used
  landscape) — draw in viewer coordinates, the framebuffer keeps its native layout and is written to
  the display as usual
- Filled and outlined rectangles, horizontal/vertical/arbitrary lines
- Text in fixed-cell bitmap fonts (`mono_font`) with integer scaling (`scale = 2` turns an 8x16 font
  into 16x32 glyphs)
- Text in proportional fonts (`proportional_font`) rasterized at their real size from
  TrueType/OpenType faces: UTF-8 input, per-glyph advances, a shared baseline, and clean 1-bit edges
  at any size
- Text boxes (`draw_text_box`): text wrapped to a width — at spaces, after hyphens and at soft
  hyphens, honoring newlines — with each line aligned left, centered or right (`text_align`), in
  either kind of font; returns the height drawn, for placing what follows
- Ink-only rendering: only the requested pixels are written, so drawing composes over existing
  content; on monochrome surfaces `ink = false` draws inverse text over filled regions
- Header-only, zero per-pixel dispatch overhead — the ink value is typed to the surface's pixel type
  at compile time
- Signed positions: shapes and text may start or extend off any edge of the surface (scrolling
  text, a sprite sliding in from the left) and draw only their visible part — no pre-clipping
- All drawing clips at surface edges and never fails

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- Components: `idfxx_font` (font packages such as `idfxx_font_spleen` and `idfxx_font_inter` provide
  the fonts themselves)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_gfx: "^3.0.0"
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

display.write(fb);
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
panel.write(band, 0, 0);            // top band

canvas.clear();
canvas.fill_rect(8, 8, 60, 24, rgb565(255, 0, 0));
canvas.draw_rect(6, 6, 64, 28, rgb565(255, 255, 255));
panel.write(band, 0, 40);           // next band down
```

### Proportional text

For large or polished text, use a proportional font drawn at its native size. The `idfxx_font_inter`
package bundles the Inter typeface as `idfxx::font::inter_16`, `inter_24`, `inter_32` and
`inter_48`; to use other faces or sizes, generate fonts from an outline face with `idfxx_font`'s
`idfxx_font_generate()` / `ttf_to_c.py`. Glyphs are drawn at their
native size, so a 48 px heading has smooth outlines instead of magnified pixels. The `y` coordinate
is the line's top; the baseline sits `font.ascent` below it:

```cpp
#include <idfxx/font/inter>
#include <idfxx/gfx>

idfxx::gfx::canvas canvas(fb);
const std::string_view title = "Crème brûlée";
const size_t w = idfxx::font::text_width(idfxx::font::inter_48, title);
canvas.draw_text(idfxx::font::inter_48, (canvas.width() - w) / 2, 40, title, true);
canvas.draw_text(idfxx::font::inter_48, 16, 40 + idfxx::font::inter_48.line_height, "second line", true);
```

Text is UTF-8; code points the font lacks render as its replacement glyph (`?` or U+FFFD by
default). `draw_text` sets one line and does not interpret newlines.

### Text boxes

`draw_text_box` lays out a block of text in a box of a given width: it breaks the text into lines
that fit (with `idfxx::font::break_lines`), aligns each one, and draws them one line height apart.
Lines wrap at spaces and after hyphens, `'\n'` starts a new line, a word too long for a line is
split, and a soft hyphen (`"\u00AD"`) marks where a long word may be hyphenated — a `'-'` is drawn
when it is. It returns the height drawn, so the next item can follow it:

```cpp
using idfxx::gfx::text_align;

int y = 4;
y += canvas.draw_text_box(idfxx::font::inter_48, 8, y, 224, "Café", amber, text_align::center);
y += canvas.draw_text_box(idfxx::font::inter_16, 8, y, 224,
                          "Baked fresh daily with ingre\u00ADdients from local farms.", white);
canvas.draw_text_box(idfxx::font::inter_16, 8, 298, 224, "Served daily, 8–11 am", amber,
                     text_align::right);
```

Fixed-cell fonts work the same way, with an optional `scale`
(`canvas.draw_text_box(idfxx::font::spleen_8x16, 0, 0, 128, msg, true, text_align::left, 2)`).
The box limits width only: every line is drawn, so check the returned height (or measure first with
`break_lines`) when space is tight.

### Band rendering

A full frame at 16 bpp is large (a 240x320 frame is ~150 KB), so color frames are usually rendered
through a band-sized framebuffer. `render_banded` takes a callback that draws the whole frame in
screen coordinates, runs it once per band, and writes each slice to its screen position with the
destination's `try_write(band, x, y)` — content straddling a band boundary clips on all four sides
and renders exactly its visible part, and content outside the band is clipped before drawing rather
than discarded pixel by pixel:

```cpp
idfxx::lcd::rgb565_framebuffer band(240, 40);
idfxx::gfx::render_banded(band, panel, 320, [&](auto& canvas) {
    // The whole 240x320 frame, in screen coordinates. Invoked once per
    // band, so draw the complete frame as a pure function of your state.
    canvas.draw_text(idfxx::font::spleen_8x16, 8, 8, "title", white, 2);
    canvas.draw_line(0, 0, 239, 319, green); // crosses every band
});
```

Under the hood each pass uses a translated canvas — `canvas(band, 0, y, width, height)` declares
that the band's top-left corner sits at (0, y) in a `width` x `height` frame, so the canvas reports
the frame's size in every band — and that constructor is available directly for custom render loops
(scrolling viewports, partial updates). The inverse mapping is `canvas.window(x, y, w, h)`: a
sub-region canvas with its own local coordinates and clipping, e.g. for widget-local drawing.

### Rotated surfaces

A panel mounted a quarter turn from the drawing layout — typically a portrait-native ePaper panel
used landscape — is drawn through a `rotated` view. The view swaps the reported dimensions and
remaps `set_pixel`, so drawing happens in viewer coordinates while the framebuffer keeps its native
layout and is written to the panel as usual:

```cpp
idfxx::epaper::mono_framebuffer fb(display.width(), display.height()); // 122x250 portrait
idfxx::gfx::rotated view(fb, idfxx::gfx::rotation::cw90);              // 250x122 landscape
idfxx::gfx::canvas canvas(view);
canvas.draw_text(idfxx::font::spleen_8x16, 8, 8, "landscape");
display.write(fb);                                                     // native layout
```

`rotation::cw90` means the panel is mounted turned clockwise (its native top edge at the viewer's
right); `rotation::ccw90` the opposite. A rotated view is itself a `pixel_surface`, so it composes
with everything else here — canvases, windows, and the free functions.

### Free functions

Every `canvas` drawing member is also available as a free function taking the surface as its first
argument — useful in generic code that doesn't want to construct a view:

```cpp
idfxx::gfx::fill_rect(fb, 0, 0, fb.width(), 10, true);
idfxx::gfx::draw_text(fb, idfxx::font::spleen_5x8, 2, 1, "status", false);
```

## API Overview

Drawing positions (`x`, `y`, `x0`, ...) are `int` and may be negative or beyond the surface; sizes
(`w`, `h`, `len`, `width`) are `size_t`, as are a surface's own `set_pixel` coordinates.

| Item | Description |
| ---- | ----------- |
| `pixel_surface` | Concept: `pixel_type`, `set_pixel(x, y, pixel)`, `width()`, `height()`. |
| `canvas(surface)` | Drawing view: the operations below as members, plus `fill(ink)` / `clear()` (using the surface's own fill/clear when present). |
| `canvas(surface, x, y, w, h)` | Translated canvas: the surface holds the region of a w x h drawing space whose top-left corner is (x, y) — see Band rendering above. |
| `canvas.window(x, y, w, h)` | Sub-region canvas with local coordinates and clipping (it may start off the canvas); `fill`/`clear` affect only the sub-region. |
| `rotated(surface, rotation)` | Quarter-turn view (`rotation::cw90` / `ccw90`): swapped dimensions, `set_pixel` remapped to the surface's native layout; `fill`/`clear` forward to the surface's. |
| `to_string(rotation)` / `to_string(text_align)` | Enumerator name; also `std::formatter` specializations (with `CONFIG_IDFXX_STD_FORMAT`). |
| `render_banded(band, dest, frame_h, draw)` | Render a frame taller than the band: invokes `draw(canvas)` once per band and writes each slice with `dest.try_write(band, 0, y)` (also `try_render_banded`). |
| `fill_rect(s, x, y, w, h, ink)` | Fill a rectangle. |
| `draw_rect(s, x, y, w, h, ink)` | Outline a rectangle (one-pixel border). |
| `draw_hline(s, x, y, len, ink)` / `draw_vline(...)` | Horizontal / vertical line. |
| `draw_line(s, x0, y0, x1, y1, ink)` | Line between two points (endpoints inclusive). |
| `draw_text(s, font, x, y, text, ink, scale)` | Draw text in a `mono_font`; on bool surfaces `ink` defaults to true. |
| `draw_text(s, font, x, y, text, ink)` | Draw UTF-8 text in a `proportional_font`, (x, y) the line's top-left; on bool surfaces `ink` defaults to true. |
| `draw_text_box(s, font, x, y, width, text, ink, align)` | Draw UTF-8 text in a `proportional_font`, wrapped to `width` and aligned within it; returns the height drawn. On bool surfaces `ink` defaults to true. |
| `draw_text_box(s, font, x, y, width, text, ink, align, scale)` | The same in a `mono_font` (bool surfaces: `ink` defaults to true, without `scale`). |
| `text_align` | Line alignment in a text box: `left`, `center`, `right`. |

Text measurement (`idfxx::font::text_width`) and line breaking (`idfxx::font::break_lines`) live in
`idfxx_font`.

## Error Handling

Drawing functions are `noexcept` and cannot fail: coordinates outside the surface are clipped, and a
`scale` of 0 is treated as 1. There are no error codes and no `try_*` variants — nothing needs them.
The exception is band rendering, which transfers to a display: `render_banded` / `try_render_banded`
report whatever the destination's `try_write` reports (for the idfxx panels, panel I/O errors) plus
`idfxx::errc::invalid_arg` when the frame height is not a multiple of the band height.

## Important Notes

- Drawing is ink-only: to replace previous content, clear (or redraw) the background first —
  `canvas.clear()` + redraw is the simplest pattern.
- A `canvas` is a non-owning view: the surface must outlive it. It itself satisfies `pixel_surface`,
  so it can be passed anywhere a surface is expected. Its coordinate bounds are captured at
  construction.
- A `rotated` view is likewise non-owning and itself a `pixel_surface`. The framebuffer behind it is
  written to the display in its native layout (`display.write(fb)`), so any write offsets are in the
  framebuffer's native coordinates.
- A canvas only draws; to get the image on screen, write the framebuffer it draws on to the display
  (`display.write(fb)`, or `panel.write(band, 0, y)` for a band).
- The `pixel_surface` concept is structural: any user-defined type with a matching
  `set_pixel`/`width`/`height` shape works, no inheritance needed.
- Proportional glyphs may overhang the line (e.g. `j` reaching left of `x`, accents on capitals
  above the line top), and ink outside the surface is clipped. The font's ink extents give the room
  to leave: `ink_ascent - ascent` rows above the line top, `ink_descent - descent` rows below the
  line's bottom edge, `-min_left_bearing` columns left of `x` and `-min_right_bearing` columns past
  the text's width (each only when positive).
- A soft hyphen (U+00AD) in proportional text is never drawn and takes no space; it only marks where
  `draw_text_box` may hyphenate a word.
- This component is deliberately small: integer coordinates, one-pixel strokes, no anti-aliasing, no
  kerning, no widgets, and layout limited to wrapped, aligned text boxes. For a full UI toolkit, use
  LVGL with the panel's `idf_handle()`.

## License

Apache-2.0
