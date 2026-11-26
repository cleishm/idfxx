# idfxx_gfx

Drawing primitives for pixel surfaces: shapes, lines, 1-bit bitmaps, and bitmap-font text.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__gfx.html)**

## Features

- Works on any pixel surface via the `pixel_surface` concept: anything with `set_pixel(x, y, pixel)`
  and reported dimensions, including `idfxx::lcd::mono_framebuffer` (bool pixels) and
  `idfxx::lcd::rgb565_framebuffer` (RGB565 pixels)
- `canvas`: a lightweight view that combines a surface with the drawing primitives, so that the
  drawing calls are members of one object
- Band rendering: `render_banded` draws a full frame through a small framebuffer, one band at a
  time. The frame is drawn in screen coordinates, with correct clipping on all four sides.
  `render_banded` uses translated canvases, which also support custom render loops. `window()` gives
  a clipped sub-region canvas with widget-local coordinates.
- `rotated`: a quarter-turn view for a surface that is mounted sideways, for example a
  portrait-native ePaper panel used in landscape orientation. You draw in viewer coordinates. The
  framebuffer keeps its native layout and is written to the display as usual.
- Filled and outlined rectangles, horizontal/vertical/arbitrary lines
- Filled and outlined rounded rectangles, ellipses, circles and triangles, with exact integer pixel
  rules. A pixel is filled when its center is inside the shape, and a triangle's fill also covers
  its outline. Outlines are the fill's one-pixel boundary. Ellipses work the same way for even and
  odd sizes.
- 1-bit bitmaps (`mono_bitmap`) with MSB-first or LSB-first rows. LSB-first matches XBM. A bitmap is
  drawn transparently, so that images drawn at one origin in different inks appear as layers, or
  opaquely over a paper value.
- Text in fixed-cell bitmap fonts (`mono_font`) with integer scaling (`scale = 2` turns an 8x16 font
  into 16x32 glyphs)
- Text in proportional fonts (`proportional_font`) rasterized at their real size from
  TrueType/OpenType faces: UTF-8 input, per-glyph advances, a shared baseline, and clean 1-bit edges
  at any size
- Text boxes (`draw_text_box`): text wrapped to a width, with each line aligned left, centered or
  right (`text_align`), in either kind of font. Lines wrap at spaces, after hyphens and at soft
  hyphens, and break at newlines. The function returns the height drawn, so that the caller can
  place the content that follows.
- Ink-only rendering: only the requested pixels are written, so new drawing goes on top of the
  existing content. On monochrome surfaces, `ink = false` draws inverse text over filled regions.
- Header-only, with no per-pixel dispatch overhead. The ink value has the surface's pixel type,
  which is fixed at compile time.
- Signed positions: shapes and text can start or extend past any edge of the surface, for example
  scrolling text or a sprite that slides in from the left. Only their visible part is drawn, and the
  caller does not need to clip them first.
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
package bundles the Inter typeface in Regular (`idfxx::font::inter_16`, `inter_24`, `inter_32`
and `inter_48`) and Medium (`inter_medium_16` to `inter_medium_48`). To use other faces or sizes,
generate fonts from an outline face with `idfxx_font`'s `idfxx_font_generate()` / `ttf_to_c.py`.
Glyphs are drawn at their native size, so a 48 px heading has smooth outlines instead of magnified
pixels. The `y` coordinate is the line's top. The baseline is `font.ascent` rows below it:

```cpp
#include <idfxx/font/inter>
#include <idfxx/gfx>

idfxx::gfx::canvas canvas(fb);
const std::string_view title = "Crème brûlée";
const size_t w = idfxx::font::text_width(idfxx::font::inter_48, title);
canvas.draw_text(idfxx::font::inter_48, (canvas.width() - w) / 2, 40, title, true);
canvas.draw_text(idfxx::font::inter_48, 16, 40 + idfxx::font::inter_48.line_height, "second line", true);
```

Text is UTF-8. Code points that the font does not have are drawn as its replacement glyph (`?` or
U+FFFD by default). `draw_text` draws one line and does not interpret newlines.

### Text boxes

`draw_text_box` lays out a block of text in a box of a given width. It breaks the text into lines
that fit (with `idfxx::font::break_lines`), aligns each line, and draws the lines one line height
apart. These rules apply:

- Lines wrap at spaces and after hyphens.
- `'\n'` starts a new line.
- A word that is too long for a line is split.
- A soft hyphen (`"\u00AD"`) marks where a long word can be hyphenated. A `'-'` is drawn when the
  word is hyphenated there.

It returns the height drawn, so that the next item can be placed below it:

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
The box limits only the width. Every line is drawn, so check the returned height (or measure first
with `break_lines`) when space is tight.

### Band rendering

A full frame at 16 bpp is large (a 240x320 frame is ~150 KB), so color frames are usually rendered
through a band-sized framebuffer. `render_banded` takes a callback that draws the whole frame in
screen coordinates. It runs the callback once for each band and writes each slice to its screen
position with the destination's `try_write(band, x, y)`. Content that crosses a band boundary is
clipped on all four sides, and exactly its visible part is drawn. Content outside the band is
clipped before drawing, not discarded pixel by pixel:

```cpp
idfxx::lcd::rgb565_framebuffer band(240, 40);
idfxx::gfx::render_banded(band, panel, 320, [&](auto& canvas) {
    // The whole 240x320 frame, in screen coordinates. Invoked once per
    // band, so draw the complete frame as a pure function of your state.
    canvas.draw_text(idfxx::font::spleen_8x16, 8, 8, "title", white, 2);
    canvas.draw_line(0, 0, 239, 319, green); // crosses every band
});
```

Each pass uses a translated canvas. `canvas(band, 0, y, width, height)` declares that the band's
top-left corner is at (0, y) in a `width` x `height` frame, so the canvas reports the frame's size
in every band. You can also use that constructor directly for custom render loops, such as
scrolling viewports and partial updates. The inverse mapping is `canvas.window(x, y, w, h)`. It
gives a sub-region canvas with its own local coordinates and clipping, for example for
widget-local drawing.

### Rotated surfaces

To draw on a panel that is mounted a quarter turn from the drawing layout, use a `rotated` view.
This is typically a portrait-native ePaper panel used in landscape orientation. The view swaps the
reported dimensions and remaps `set_pixel`, so that drawing uses viewer coordinates. The
framebuffer keeps its native layout and is written to the panel as usual:

```cpp
idfxx::epaper::mono_framebuffer fb(display.width(), display.height()); // 122x250 portrait
idfxx::gfx::rotated view(fb, idfxx::gfx::rotation::cw90);              // 250x122 landscape
idfxx::gfx::canvas canvas(view);
canvas.draw_text(idfxx::font::spleen_8x16, 8, 8, "landscape");
display.write(fb);                                                     // native layout
```

`rotation::cw90` means that the panel is mounted turned clockwise (its native top edge at the
viewer's right). `rotation::ccw90` means the opposite. A rotated view is itself a `pixel_surface`,
so it works with canvases, windows and the free functions.

### Shapes and icons

One call fills or outlines a rounded rectangle, ellipse, circle or triangle. `draw_bitmap` draws
1-bit icons (`constexpr` tables of packed rows). `idfxx::font::text_ink_bounds` gives the box of
the pixels that a label's ink covers. With these, you can draw a button with an icon and a label
that is centered by its ink:

```cpp
using namespace idfxx;

static constexpr uint8_t wifi_bits[] = {0x3C, 0x42, 0x81, 0x3C, 0x42, 0x18, 0x00, 0x18};
static constexpr gfx::mono_bitmap wifi_icon{wifi_bits, 8, 8};

canvas.fill_round_rect(10, 10, 120, 36, 8, black);           // the button
canvas.draw_bitmap(20, 24, wifi_icon, white);                // set bits only
if (const auto ink = font::text_ink_bounds(font::inter_16, "Connect")) {
    // Center the label's ink in the part of the button right of the icon.
    const int x = 36 + (88 - static_cast<int>(ink->width)) / 2;
    const int y = 10 + (36 - static_cast<int>(ink->height)) / 2;
    canvas.draw_text(font::inter_16, x - ink->left, y - ink->top, "Connect", white);
}
canvas.draw_circle(150, 28, 10, black);                      // a status light
canvas.fill_triangle(170, 20, 184, 28, 170, 36, black);      // a "play" arrow
```

A circle of radius `r` is the ellipse in the `2r + 1` square box around its center. A rounded
rectangle's corners are quadrants of the ellipse in a `2r` square box. So shapes of the same family
line up pixel for pixel. Triangle vertices name pixels (like `draw_line` endpoints).
`draw_triangle` draws each edge as `draw_line` does, from its upper endpoint (the left one for a
horizontal edge). `fill_triangle` fills every pixel whose center is inside the triangle or on an
edge. It also fills the pixels that `draw_triangle` draws. Neither function depends on the order of
the vertices. So a filled triangle lines up with a `draw_triangle` border, and triangles that share
an edge have no gap between them.

Bitmaps drawn without a paper value write only their set bits, so bitmaps drawn at one origin
appear as layers, for example an outline in black and a level in red. With a paper value
(`draw_bitmap(x, y, icon, ink, paper)`), every pixel that the bitmap covers is written.

### Free functions

Every `canvas` drawing member is also available as a free function that takes the surface as its
first argument. This is useful in generic code that does not construct a view:

```cpp
idfxx::gfx::fill_rect(fb, 0, 0, fb.width(), 10, true);
idfxx::gfx::draw_text(fb, idfxx::font::spleen_5x8, 2, 1, "status", false);
```

## API Overview

Drawing positions (`x`, `y`, `x0`, ...) are `int` and can be negative or beyond the surface. Sizes
(`w`, `h`, `len`, `width`) are `size_t`, as are a surface's own `set_pixel` coordinates.

| Item | Description |
| ---- | ----------- |
| `pixel_surface` | Concept: `pixel_type`, `set_pixel(x, y, pixel)`, `width()`, `height()`. |
| `canvas(surface)` | Drawing view: the operations below as members, plus `fill(ink)` / `clear()` (using the surface's own fill/clear when present). |
| `canvas(surface, x, y, w, h)` | Translated canvas: the surface holds the region of a w x h drawing space whose top-left corner is (x, y). See Band rendering above. |
| `canvas.window(x, y, w, h)` | Sub-region canvas with local coordinates and clipping. It can start off the canvas. `fill`/`clear` affect only the sub-region. |
| `rotated(surface, rotation)` | Quarter-turn view (`rotation::cw90` / `ccw90`): swapped dimensions, `set_pixel` remapped to the surface's native layout. `fill`/`clear` call the surface's own. |
| `to_string(rotation)` / `to_string(text_align)` / `to_string(bit_order)` | Enumerator name; also `std::formatter` specializations (with `CONFIG_IDFXX_STD_FORMAT`). |
| `render_banded(band, dest, frame_h, draw)` | Render a frame taller than the band: invokes `draw(canvas)` once per band and writes each slice with `dest.try_write(band, 0, y)` (also `try_render_banded`). |
| `fill_rect(s, x, y, w, h, ink)` | Fill a rectangle. |
| `draw_rect(s, x, y, w, h, ink)` | Outline a rectangle (one-pixel border). |
| `draw_hline(s, x, y, len, ink)` / `draw_vline(...)` | Horizontal / vertical line. |
| `draw_line(s, x0, y0, x1, y1, ink)` | Line between two points (endpoints inclusive). |
| `fill_round_rect(s, x, y, w, h, r, ink)` / `draw_round_rect(...)` | Fill / outline a rectangle with corners of radius `r` (clamped to half the shorter side; 0 is a plain rectangle). |
| `fill_ellipse(s, x, y, w, h, ink)` / `draw_ellipse(...)` | Fill / outline the ellipse inscribed in a `w` x `h` box. |
| `fill_circle(s, cx, cy, r, ink)` / `draw_circle(...)` | Fill / outline a circle: the ellipse in the `2r + 1` square box centered on pixel (cx, cy). |
| `fill_triangle(s, x0, y0, x1, y1, x2, y2, ink)` | Fill the pixels centered inside a triangle or on its edges, and its `draw_triangle` outline. |
| `draw_triangle(s, x0, y0, x1, y1, x2, y2, ink)` | Outline a triangle with three `draw_line` edges, each from its upper (or, if horizontal, left) endpoint. |
| `mono_bitmap` | A 1-bit image: `data` (a `std::span<const uint8_t>`), `width`, `height`, `order` (`bit_order::msb_first` or `lsb_first`). Each row is `bytes_per_row()` bytes. |
| `draw_bitmap(s, x, y, bitmap, ink)` | Draw a bitmap's set bits in `ink`, leaving the rest untouched. |
| `draw_bitmap(s, x, y, bitmap, ink, paper)` | Draw a bitmap opaquely: set bits in `ink`, clear bits in `paper`. |
| `draw_text(s, font, x, y, text, ink, scale)` | Draw text in a `mono_font`. On bool surfaces, `ink` defaults to true. |
| `draw_text(s, font, x, y, text, ink)` | Draw UTF-8 text in a `proportional_font`, with (x, y) at the line's top-left. On bool surfaces, `ink` defaults to true. |
| `draw_text_box(s, font, x, y, width, text, ink, align)` | Draw UTF-8 text in a `proportional_font`, wrapped to `width` and aligned within it. Returns the height drawn. On bool surfaces, `ink` defaults to true. |
| `draw_text_box(s, font, x, y, width, text, ink, align, scale)` | The same in a `mono_font`. On bool surfaces, `ink` defaults to true (that form has no `scale`). |
| `text_align` | Line alignment in a text box: `left`, `center`, `right`. |

Text measurement (`idfxx::font::text_width`, and `idfxx::font::text_ink_bounds` for the pixels that
the text's ink covers) and line breaking (`idfxx::font::break_lines`) are in `idfxx_font`.

## Error Handling

Drawing functions are `noexcept` and cannot fail. Coordinates outside the surface are clipped, and
a `scale` of 0 is treated as 1. There are no error codes and no `try_*` variants for drawing,
because drawing cannot fail. The exception is band rendering, which writes to a display.
`render_banded` / `try_render_banded` report any error that the destination's `try_write` reports
(for the idfxx panels, panel I/O errors). They also report `idfxx::errc::invalid_arg` when the frame
height is not a multiple of the band height.

## Important Notes

- Drawing is ink-only. To replace previous content, clear (or redraw) the background first. The
  simplest pattern is `canvas.clear()` followed by a redraw. The one exception is `draw_bitmap` with
  a paper value, which writes every pixel that the bitmap covers.
- Shape geometry is exact and integer. An ellipse fills the pixels whose centers are inside the
  ellipse inscribed in its box. So in a very thin box the end rows can be empty, a radius-1 circle
  is a 3 x 3 square, and a radius-1 rounded rectangle has square corners. `fill_triangle` covers the
  line pixels that `draw_triangle` draws, and both are the same for any order of the vertices. A
  degenerate (flat) triangle fills the pixels that `draw_triangle` draws for it. A `draw_line` along
  an edge matches the edge when the line is drawn from the edge's upper endpoint. When the line is
  drawn the other way, it can differ at half-pixel ties.
- `draw_bitmap` never reads past a `mono_bitmap`'s `data`. When `data` holds fewer than `height`
  complete rows, only the rows that it holds are drawn.
- A `canvas` is a non-owning view: the surface must outlive it. It itself satisfies `pixel_surface`,
  so it can be passed anywhere a surface is expected. Its coordinate bounds are captured at
  construction.
- A `rotated` view is likewise non-owning and itself a `pixel_surface`. The framebuffer behind it is
  written to the display in its native layout (`display.write(fb)`), so any write offsets are in the
  framebuffer's native coordinates.
- A canvas only draws. To show the image on screen, write the framebuffer that it draws on to the
  display (`display.write(fb)`, or `panel.write(band, 0, y)` for a band).
- The `pixel_surface` concept is structural. Any user-defined type with matching
  `set_pixel`/`width`/`height` members works, and no inheritance is needed.
- Proportional glyphs can overhang the line, for example a `j` that extends left of `x`, or accents
  on capitals above the line top. Ink outside the surface is clipped. The font's ink extents give
  the space to leave: `ink_ascent - ascent` rows above the line top, `ink_descent - descent` rows
  below the line's bottom edge, `-min_left_bearing` columns left of `x` and `-min_right_bearing`
  columns past the text's width (each only when positive).
- A soft hyphen (U+00AD) in proportional text is never drawn and takes no space. It only marks where
  `draw_text_box` can hyphenate a word.
- This component is deliberately small: integer coordinates, one-pixel strokes, no anti-aliasing, no
  arcs or polygons, no bitmap scaling, no kerning, no widgets, and layout limited to wrapped,
  aligned text boxes. For a full UI toolkit, use
  LVGL with the panel's `idf_handle()`.

## License

Apache-2.0
