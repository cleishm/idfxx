# idfxx_font_inter

The [Inter](https://rsms.me/inter/) typeface (Regular and Medium, 16–48 px) as idfxx proportional
font data.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__font__inter.html)**

## Features

- `inter_16`, `inter_24`, `inter_32`, `inter_48` — Inter 4.1 Regular as 1-bit
  `proportional_font`s, each rasterized at its own size so large text stays crisp
- `inter_medium_16`, `inter_medium_24`, `inter_medium_32`, `inter_medium_48` — Inter 4.1 Medium at
  the same sizes, a heavier weight for small text
- Covers Windows-1252: printable ASCII, Latin-1 accented letters and symbols, and typographic
  punctuation (curly quotes, en and em dashes, ellipsis, bullet, `€`, `™`). Other code points are
  drawn as `?`
- UTF-8 text, measured with `idfxx::font::text_width` and drawn with `idfxx::gfx::draw_text`
- One translation unit per font, so sizes that an application never references are dropped at link
  time

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- Components: `idfxx_font`

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_font_inter: "^1.0.0"
```

## Usage

```cpp
#include <idfxx/font/inter>
#include <idfxx/gfx>

const auto& title = idfxx::font::inter_48;
const size_t w = idfxx::font::text_width(title, "Crème brûlée");
canvas.draw_text(title, (canvas.width() - w) / 2, 8, "Crème brûlée", white);

const auto& body = idfxx::font::inter_16;
int y = 8 + title.line_height;
canvas.draw_text(body, 8, y, "“naïve” café", white);
y += body.line_height;
canvas.draw_text(body, 8, y, "Today — 18 °C", white);
```

## API Overview

| Item | Size | Line height | Flash | Description |
| ---- | ---- | ----------- | ----- | ----------- |
| `idfxx::font::inter_16` | 16 px | 20 px | 6.6 KB | Regular body text. |
| `idfxx::font::inter_24` | 24 px | 30 px | 9.7 KB | Regular subheadings. |
| `idfxx::font::inter_32` | 32 px | 39 px | 13.9 KB | Regular headings. |
| `idfxx::font::inter_48` | 48 px | 59 px | 25.5 KB | Regular large headings and readouts. |
| `idfxx::font::inter_medium_16` | 16 px | 20 px | 6.6 KB | Medium body text. |
| `idfxx::font::inter_medium_24` | 24 px | 30 px | 9.8 KB | Medium subheadings. |
| `idfxx::font::inter_medium_32` | 32 px | 39 px | 14.3 KB | Medium headings. |
| `idfxx::font::inter_medium_48` | 48 px | 59 px | 26.3 KB | Medium large headings and readouts. |

Sizes are em sizes. Capitals are roughly three quarters of the size tall.

## Important Notes

- At 16 px, Regular's stems are 1 px wide, which can look faint on low-resolution or ePaper
  displays. `inter_medium_16` has 2 px stems.
- Text is drawn with the line's top-left corner at the given position. The baseline is `ascent`
  rows below it. Step lines by `line_height`.
- Some ink extends past the line. At the larger sizes, accents on capitals rise above its top edge.
  A few glyphs extend up to three columns left of the pen or past their advance. Each font's
  `ink_ascent`, `ink_descent`, `min_left_bearing` and `min_right_bearing` give the space to leave
  around text that must be fully visible (see `idfxx_font`).
- The fonts are generated from `fonts/Inter-Regular.ttf` and `fonts/Inter-Medium.ttf` with
  `idfxx_font`'s `scripts/ttf_to_c.py`, using FreeType's auto-hinter (`--hinting auto`) so that
  each glyph's stems are the same width. The TTFs are kept in the idfxx repository, not in the
  published package. In the repository, `just gen-font-inter` regenerates the fonts (set
  `IDFXX_FONT_PYTHON` to a Python with `freetype-py`). For other sizes, weights or character
  sets, generate your own with `idfxx_font_generate()` (see `idfxx_font`).
- Inter is Copyright (c) 2016 The Inter Project Authors (https://github.com/rsms/inter).

## License

SIL Open Font License 1.1 (see [LICENSE](LICENSE)).
