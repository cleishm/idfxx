# idfxx_font

Bitmap font models (fixed-cell and proportional) and text metrics.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__font.html)**

## Features

- Compact fixed-cell font representation (`mono_font`): contiguous character range, one cell size,
  row-major MSB-first glyph data
- Proportional font representation (`proportional_font`): per-glyph advance and offsets, bitmaps
  trimmed to the ink, any set of Unicode code points, a replacement glyph for the rest, and line
  metrics (`ascent`, `descent`, `line_height`)
- Text measurement (`text_width`, per-glyph `advance`), fully `constexpr`; proportional fonts take
  UTF-8 text (decoded with `idfxx::utf8_view`)
- Generators: `bdf_to_c.py` turns a monospace BDF font into a `mono_font`; `ttf_to_c.py` rasterizes
  a TrueType/OpenType font into a 1-bit `proportional_font` at the pixel size you draw it, also
  available as the `idfxx_font_generate()` CMake function
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
it in automatically — depend on it directly only when defining fonts or measuring text yourself.

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

Text drawing lives in `idfxx_gfx` (`idfxx::gfx::draw_text`).

## API Overview

| Item | Description |
| ---- | ----------- |
| `mono_font` | Fixed-cell font: `width`, `height`, `first_char`, `last_char`, `bitmap`. |
| `mono_font::contains(c)` | Whether the font has a glyph for `c`. |
| `mono_font::glyph(c)` | Pointer to the glyph's bitmap rows. |
| `mono_font::advance(c)` | Cursor advance for `c` (one cell width). |
| `text_width(font, text, scale)` | Rendered width in pixels (constexpr). |
| `proportional_font` | Proportional font: `ascent`, `descent`, `line_height`, `ranges`, `glyphs`, `replacement`, `bitmap`. |
| `proportional_font::find(cp)` / `contains(cp)` | The font's own glyph for a code point (or null). |
| `proportional_font::resolve(cp)` | The glyph drawn for a code point: its own, else the replacement (or null). |
| `proportional_font::advance(cp)` | Pen advance for a code point (0 when skipped). |
| `glyph_metrics` | Per-glyph `advance`, `x_offset`/`y_offset` from the pen and baseline, bitmap `width`/`height`. |
| `text_width(font, text)` | Width of UTF-8 text in a proportional font (constexpr). |

## Adding fonts

### Proportional fonts from TrueType/OpenType

Glyphs are rasterized once, at build time, at the pixel size they will be drawn — a 48 px heading
gets smooth 1-bit outlines rather than a magnified small font. Pick any `.ttf`/`.otf` whose license
allows embedding (e.g. an SIL Open Font License face), put it in your project, and generate the
sizes you need from your component's `CMakeLists.txt`:

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

Options: `CHARSET ascii|latin1|cp1252` (default `cp1252`: Latin-1 plus curly quotes, dashes,
ellipsis, euro…), `CHARS` / `RANGES` for more code points (`RANGES "0x100-0x17F"` adds Latin
Extended-A), `NAMESPACE` (default `fonts`; also the generated header's directory, so `a::b` gives
`"a/b/inter.hpp"`), `REPLACEMENT <char>|U+XXXX|none` (default U+FFFD if the face has it, else `?`),
`RENDER mono|threshold` with `THRESHOLD`, and `FACE_INDEX` for `.ttc` collections. Each font is its
own translation unit, so unused sizes are dropped at link time.

The generator needs [freetype-py](https://pypi.org/project/freetype-py/) in the Python that runs
it — by default the ESP-IDF Python (`python -m pip install freetype-py` in the IDF environment), or
any interpreter given with `-DIDFXX_FONT_PYTHON=/path/to/python`. Configuration fails with
instructions when it is missing.

To commit generated sources instead (no build-time dependency), run the script directly:

```sh
scripts/ttf_to_c.py Inter-Regular.ttf inter_48 --size 48 -o inter_48.cpp --preview inter_48.png
```

Without `--header`, the generated file declares the font itself; declare
`extern const idfxx::font::proportional_font inter_48;` (in namespace `fonts`) wherever you use it.
`--preview` renders sample text from the generated tables to a PNG, laid out as `draw_text` lays it
out, for judging a face and size before flashing.

`mono` rendering (the default) uses FreeType's hinting and dropout control for monochrome output,
keeping stems even and thin strokes intact. `threshold` rasterizes with anti-aliasing and inks
pixels at or above `THRESHOLD` coverage; lowering it emboldens a face, but thin strokes can break.

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
  baseline; accents on capitals may rise above it.
- Proportional glyph bitmaps use the same row layout as `mono_font` (`ceil(width / 8)` bytes per
  row, MSB = leftmost pixel), trimmed to each glyph's ink and placed by `x_offset` / `y_offset`
  relative to the pen position and baseline.
- There is no kerning: text is set by per-glyph advances only.

## License

Apache-2.0
