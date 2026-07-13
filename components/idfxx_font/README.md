# idfxx_font

Fixed-cell bitmap font model and text metrics.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__font.html)**

## Features

- Compact fixed-cell font representation (`mono_font`): contiguous character
  range, one cell size, row-major MSB-first glyph data
- Text measurement (`text_width`, per-glyph `advance`), fully `constexpr`
- BDF-to-C converter script for turning any monospace BDF font into a
  `mono_font` translation unit
- Header-only, no dependencies beyond the C++ standard library

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_font: "^1.0.0"
```

Font data packages (e.g. `cleishm/idfxx_font_spleen`) and text rendering
(`cleishm/idfxx_gfx`) depend on this component and pull it in automatically —
depend on it directly only when defining fonts or measuring text yourself.

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

Text drawing lives in `idfxx_gfx` (`idfxx::gfx::draw_text`).

## API Overview

| Item | Description |
| ---- | ----------- |
| `mono_font` | Fixed-cell font: `width`, `height`, `first_char`, `last_char`, `bitmap`. |
| `mono_font::contains(c)` | Whether the font has a glyph for `c`. |
| `mono_font::glyph(c)` | Pointer to the glyph's bitmap rows. |
| `mono_font::advance(c)` | Cursor advance for `c` (one cell width). |
| `text_width(font, text, scale)` | Rendered width in pixels (constexpr). |

## Adding fonts

Convert any monospace BDF font with the included script and add the output
to your build:

```sh
scripts/bdf_to_c.py myfont.bdf my_font my/header.hpp > my_font.cpp
```

The named header must declare `extern const mono_font my_font;` in namespace
`idfxx::font` so the generated definition gets external linkage (see
`idfxx_font_spleen` for the pattern).

## Important Notes

- Glyph bitmaps store each row in `ceil(width / 8)` bytes, MSB = leftmost
  pixel, rows top to bottom, glyphs consecutive from `first_char`.
- Characters outside a font's range still advance the cursor (rendered
  blank by `idfxx::gfx::draw_text`), so `text_width` counts them too.

## License

Apache-2.0
