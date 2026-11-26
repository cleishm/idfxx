# idfxx_font

Bitmap font models (fixed-cell and proportional) and text metrics.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__font.html)**

## Features

- Compact fixed-cell font representation (`mono_font`): contiguous character range, one cell size,
  row-major MSB-first glyph data
- Proportional font representation (`proportional_font`): per-glyph advance and offsets, bitmaps
  trimmed to the ink, any set of Unicode code points, a replacement glyph for the rest, line
  metrics (`ascent`, `descent`, `line_height`), and ink extents bounding every glyph (`ink_ascent`,
  `ink_descent`, `min_left_bearing`, `min_right_bearing`)
- Text measurement (`text_width`, per-glyph `advance`), fully `constexpr`. Proportional fonts take
  UTF-8 text (decoded with `idfxx::utf8_view`)
- Ink bounds (`text_ink_bounds`): the box of pixels that a line of text actually inks, relative to
  where it is drawn. Use it to place text by its ink (centering a label optically, setting it flush
  against an edge) rather than by its advance and line box. Also `constexpr`
- Line breaking (`break_lines`): a lazy, non-allocating, `constexpr` view that splits text into
  lines that fit a width. It wraps at spaces and after hyphens, honors newlines, hyphenates at soft
  hyphens (U+00AD) in proportional fonts, and splits words too long for a line
- Generators: `bdf_to_c.py` turns a monospace BDF font into a `mono_font`. `ttf_to_c.py` rasterizes
  a TrueType/OpenType font into a 1-bit `proportional_font` at the pixel size that you draw it at.
  It is also available as the `idfxx_font_generate()` CMake function
- Header-only (the TrueType generator needs Python with `freetype-py` at build time)

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- idfxx_core

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_font: "^1.1.0"
```

Font data packages (`cleishm/idfxx_font_spleen` for fixed-cell fonts, `cleishm/idfxx_font_inter`
for proportional ones) and text rendering (`cleishm/idfxx_gfx`) depend on this component and pull
it in automatically. Depend on it directly only when defining fonts or measuring text yourself.

## Usage

```cpp
#include <idfxx/font>
#include <idfxx/font/spleen>   // fonts from idfxx_font_spleen

// Right-alignment math with constexpr metrics.
const size_t w = idfxx::font::text_width(idfxx::font::spleen_5x8, "-72dBm");
const size_t x = display_width - w;

// Fonts are plain structs; glyph data is directly accessible.
const auto& f = idfxx::font::spleen_8x16;
const uint8_t* glyph = f.contains('A') ? f.glyph('A') : nullptr;
```

Proportional fonts measure UTF-8 text the same way:

```cpp
#include <idfxx/font/inter>   // fonts from idfxx_font_inter

const size_t w = idfxx::font::text_width(idfxx::font::inter_48, "Crème brûlée");
const size_t x = (display_width - w) / 2;               // centered
const size_t next_line_y = y + idfxx::font::inter_48.line_height;
```

`text_width` is the distance that the pen travels. To place text by the pixels that it inks instead,
use `text_ink_bounds`. It returns the ink's box relative to the origin that `draw_text` takes (the
pen start and the line top), or `std::nullopt` when nothing inks. Drawing at `(x - left, y - top)`
puts the ink's top-left pixel at `(x, y)`, so centering a label's ink in a box is:

```cpp
using namespace idfxx::font;

const int box_x = 16, box_y = 40, box_w = 96, box_h = 32; // the box to center the label in
if (const auto ink = text_ink_bounds(inter_24, "23°")) {
    const int ink_x = box_x + (box_w - static_cast<int>(ink->width)) / 2;
    const int ink_y = box_y + (box_h - static_cast<int>(ink->height)) / 2;
    canvas.draw_text(inter_24, ink_x - ink->left, ink_y - ink->top, "23°", black);
}
```

Break text into lines that fit a width with `break_lines`. Each line is a slice of the input, with
its measured width. A line broken at a soft hyphen (`"\u00AD"`) needs a `'-'` drawn after it:

```cpp
using namespace idfxx::font;

const std::string_view text = "Baked fresh daily with ingre\u00ADdients from local farms.";
int y = 0;
for (const text_line& line : break_lines(inter_16, text, 224)) {
    const int x = 8 + (224 - line.width) / 2;             // centered in a 224 px box
    canvas.draw_text(inter_16, x, y, line.text, white);
    if (line.hyphenated) {
        canvas.draw_text(inter_16, x + text_width(inter_16, line.text), y, "-", white);
    }
    y += inter_16.line_height;
}
```

Text drawing is in `idfxx_gfx`: `idfxx::gfx::draw_text` for a single line, and
`idfxx::gfx::draw_text_box`, which runs the loop above (aligned left, centered or right).

## API Overview

| Item | Description |
| ---- | ----------- |
| `mono_font` | Fixed-cell font: `width`, `height`, `first_char`, `last_char`, `bitmap`. |
| `mono_font::contains(c)` | Whether the font has a glyph for `c`. |
| `mono_font::glyph(c)` | Pointer to the glyph's bitmap rows. |
| `mono_font::advance(c)` | Cursor advance for `c` (one cell width). |
| `text_width(font, text, scale)` | Rendered width in pixels (constexpr). |
| `proportional_font` | Proportional font: `ascent`, `descent`, `line_height`, `ink_ascent`, `ink_descent`, `min_left_bearing`, `min_right_bearing`, `ranges`, `glyphs`, `replacement`, `bitmap`. |
| `proportional_font::find(cp)` / `contains(cp)` | The font's own glyph for a code point (or null). |
| `proportional_font::resolve(cp)` | The glyph drawn for a code point: its own, else the replacement (or null). |
| `proportional_font::advance(cp)` | Pen advance for a code point (0 when skipped). |
| `glyph_metrics` | Per-glyph `advance`, `x_offset`/`y_offset` from the pen and baseline, bitmap `width`/`height`. |
| `text_width(font, text)` | Width of UTF-8 text in a proportional font (constexpr). |
| `text_ink_bounds(font, text)` | The `ink_bounds` of the pixels that text inks when drawn, in either kind of font, or `std::nullopt` when nothing inks (constexpr). |
| `text_ink_bounds(font, line)` | The same for a `text_line`, including the `'-'` drawn after a hyphenated proportional line. |
| `ink_bounds` | `left`, `top` (relative to the draw origin: pen start and line top), `width`, `height`. Equality comparable. |
| `break_lines(font, text, max_width)` | Lazy view of the `text_line`s of UTF-8 text broken to fit `max_width` in a proportional font. |
| `break_lines(font, text, max_width, scale)` | The same for a `mono_font` drawn at `scale`. |
| `text_line` | One line: `text` (a slice of the input), `width` in pixels, `hyphenated` (draw a `'-'` after `text`). |
| `line_break_view<Font>` | The forward view that `break_lines` returns. It is non-owning and computes each line as it is reached. |

## Adding fonts

### Proportional fonts from TrueType/OpenType

Glyphs are rasterized once, at build time, at the pixel size that they will be drawn at. A 48 px
heading therefore gets smooth 1-bit outlines rather than a magnified small font. Pick any
`.ttf`/`.otf` whose license allows embedding (e.g. an SIL Open Font License face), put it in your
project, and generate the sizes that you need from your component's `CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.cpp" PRIV_REQUIRES idfxx_gfx)
idfxx_font_generate(
    NAME inter                  # fonts::inter_36, fonts::inter_64
    FONT fonts/Inter-Regular.ttf
    SIZES 36 64                 # em size in pixels; capitals are about 0.7x this tall
)
```

```cpp
#include "fonts/inter.hpp"
canvas.draw_text(fonts::inter_64, x, y, "Bonjour, café", true);
```

Options:

- `CHARSET ascii|latin1|cp1252` (default `cp1252`: Latin-1 plus curly quotes, dashes, ellipsis,
  euro…)
- `CHARS` / `RANGES` for more code points (`RANGES "0x100-0x17F"` adds Latin Extended-A)
- `NAMESPACE` (default `fonts`). It is also the generated header's directory, so `a::b` gives
  `"a/b/inter.hpp"`
- `REPLACEMENT <char>|U+XXXX|none` (default U+FFFD if the face has it, else `?`)
- `RENDER mono|threshold` with `THRESHOLD`
- `HINTING native|auto`
- `FACE_INDEX` for `.ttc` collections

Each font is its own translation unit, so unused sizes are dropped at link time.

The generator needs [freetype-py](https://pypi.org/project/freetype-py/) in the Python that runs it.
By default, this is the ESP-IDF Python (`python -m pip install freetype-py` in the IDF environment).
Another interpreter can be given with `-DIDFXX_FONT_PYTHON=/path/to/python`. Configuration fails
with instructions when it is missing.

To commit generated sources instead (no build-time dependency), run the script directly:

```sh
scripts/ttf_to_c.py Inter-Regular.ttf inter_48 --size 48 -o inter_48.cpp --preview inter_48.png
```

Without `--header`, the generated file declares the font itself. Declare
`extern const idfxx::font::proportional_font inter_48;` (in namespace `fonts`) wherever you use it.
`--preview` renders sample text from the generated tables to a PNG, laid out as `draw_text` lays it
out, for judging a face and size before flashing.

`mono` rendering (the default) uses FreeType's hinting and dropout control for monochrome output,
keeping thin strokes intact. `threshold` rasterizes with anti-aliasing and inks pixels at or above
`THRESHOLD` coverage. Lowering it emboldens a face, but thin strokes can break.

Hinting fits outlines to the pixel grid. `native` (the default) applies the font's own hinting
instructions (only vertically, in `threshold` mode). Some faces' instructions fit only the vertical
axis, and some faces have none. Native hinting then rounds each stem's width on its own, so stems
that are equal in the design can differ by a pixel (an `H` with one leg thinner than the other).
`auto` (`--hinting auto`, or `HINTING auto`) uses FreeType's auto-hinter instead, in either
rendering mode. The auto-hinter fits every stem of a glyph to the same width. Compare the two with
`--preview` when choosing for a face.

### Fixed-cell fonts from BDF

Convert any monospace BDF font with the included script and add the output to your build:

```sh
scripts/bdf_to_c.py myfont.bdf my_font my/header.hpp > my_font.cpp
```

The named header must declare `extern const mono_font my_font;` in namespace `idfxx::font` so the
generated definition gets external linkage (see `idfxx_font_spleen` for the pattern).

## Important Notes

- Glyph bitmaps store each row in `ceil(width / 8)` bytes, MSB = leftmost pixel, rows top to bottom,
  glyphs consecutive from `first_char`.
- Characters outside a `mono_font`'s range still advance the cursor (rendered blank by
  `idfxx::gfx::draw_text`), so `text_width` counts them too.
- In a `proportional_font`, code points without a glyph measure and draw as the replacement glyph,
  or take no space when the font has none. Generated fonts set the line top `ascent` rows above the
  baseline. Accents on capitals may rise above it.
- A `proportional_font`'s ink extents bound where its glyphs' ink can fall: from `ink_ascent` rows
  above the baseline to `ink_descent` rows below it, and from `min_left_bearing` columns right of
  the pen to `min_right_bearing` columns left of the advance (negative values extend past them).
  Reserve `ink_ascent - ascent` rows above a line (when positive) to show every glyph in full, and
  likewise for the other edges. Generated fonts set them exactly. In a font written by hand, they
  must cover every glyph, including the replacement. They may be larger than needed but never too
  small, because renderers may skip text whose ink box does not overlap the area being drawn.
- Proportional glyph bitmaps use the same row layout as `mono_font` (`ceil(width / 8)` bytes per
  row, MSB = leftmost pixel), trimmed to each glyph's ink and placed by `x_offset` / `y_offset`
  relative to the pen position and baseline.
- There is no kerning: text is set by per-glyph advances only.
- `text_ink_bounds` measures the text that it is given, glyph by glyph, as `idfxx::gfx::draw_text`
  draws it. The font-wide ink extents above bound every glyph instead. A proportional glyph counts
  with its whole bitmap box, which generated fonts trim to its ink. Fixed-cell results are unscaled:
  for text drawn with a `scale`, multiply each field by it.
- U+00AD SOFT HYPHEN is invisible and zero-width in proportional text (`text_width`, `draw_text`),
  even when the font has a glyph for it. It only marks where `break_lines` may hyphenate a word.
  Mono fonts treat text as bytes and do not interpret it.
- `break_lines` keeps leading spaces at the start of the text and after a `'\n'` as indentation, and
  drops the spaces at each wrap point and at the end of a line. The view references the font and the
  text, both of which must outlive it.

## License

Apache-2.0
