# idfxx_font_inter

The [Inter](https://rsms.me/inter/) typeface (Regular, 16–48 px) as idfxx proportional font data.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__font__inter.html)**

## Features

- `inter_16`, `inter_24`, `inter_32`, `inter_48` — Inter 4.1 Regular as 1-bit
  `proportional_font`s, each rasterized at its own size so large text stays crisp
- Covers Windows-1252: printable ASCII, Latin-1 accented letters and symbols, and typographic
  punctuation (curly quotes, en and em dashes, ellipsis, bullet, `€`, `™`); other code points are
  drawn as `?`
- UTF-8 text, measured with `idfxx::font::text_width` and drawn with `idfxx::gfx::draw_text`
- One translation unit per font, so sizes an application never references are dropped at link time

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
| `idfxx::font::inter_16` | 16 px | 20 px | 6.7 KB | Body text. |
| `idfxx::font::inter_24` | 24 px | 30 px | 9.8 KB | Subheadings. |
| `idfxx::font::inter_32` | 32 px | 39 px | 14.1 KB | Headings. |
| `idfxx::font::inter_48` | 48 px | 59 px | 25.8 KB | Large headings and readouts. |

Sizes are em sizes; capitals are roughly three quarters of the size tall.

## Important Notes

- Text is drawn with the line's top-left corner at the given position; the baseline sits `ascent`
  rows below it. Step lines by `line_height`.
- Some ink reaches past the line: accents on capitals rise above its top edge at the larger sizes,
  and a few glyphs extend a column or two left of the pen or past their advance. Each font's
  `ink_ascent`, `ink_descent`, `min_left_bearing` and `min_right_bearing` give the room to leave
  around text that must show in full (see `idfxx_font`).
- The fonts are generated from `fonts/Inter-Regular.ttf` with `idfxx_font`'s
  `scripts/ttf_to_c.py`. The TTF is kept in the idfxx repository, not in the published package;
  there, `just gen-font-inter` regenerates them (set `IDFXX_FONT_PYTHON` to a Python with
  `freetype-py`). For other sizes, weights or character sets,
  generate your own with `idfxx_font_generate()` (see `idfxx_font`).
- Inter is Copyright (c) 2016 The Inter Project Authors (https://github.com/rsms/inter).

## License

SIL Open Font License 1.1 (see [LICENSE](LICENSE)).
