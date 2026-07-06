# idfxx_font

Bitmap font rendering for monochrome framebuffers, with the Spleen 5x8 and 8x16 fonts bundled.

[API documentation](https://cleishm.github.io/idfxx/group__idfxx__font.html)

## Features

- Compact fixed-cell font representation (`mono_font`)
- Text drawing into `idfxx::lcd::mono_framebuffer` with integer scaling
- Ink-only rendering: glyph backgrounds stay transparent, and `on = false`
  draws inverse text over filled regions
- Bundled [Spleen](https://github.com/fcambus/spleen) 5x8 and 8x16 fonts
  (BSD-2-Clause, license included); one translation unit per font so unused
  fonts are dropped at link time
- `scale = 2` turns the 8x16 font into 16x32 headline digits
- BDF-to-C converter script included for adding more fonts

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- Components: `idfxx_core`, `idfxx_lcd`

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_font: "^1.0.0"
```

## Usage

```cpp
#include <idfxx/font>

idfxx::lcd::mono_framebuffer fb(display.width(), display.height());

// Big headline (16x32 per glyph).
idfxx::font::draw_text(fb, idfxx::font::spleen_8x16, 4, 16, "23.7", true, 2);

// Right-aligned status line.
const size_t w = idfxx::font::text_width(idfxx::font::spleen_5x8, "-72dBm");
idfxx::font::draw_text(fb, idfxx::font::spleen_5x8, fb.width() - w, 56, "-72dBm");

fb.flush(display);
```

## API Overview

| Item | Description |
| ---- | ----------- |
| `mono_font` | Fixed-cell font: `width`, `height`, `first_char`, `last_char`, `bitmap`. |
| `spleen_5x8`, `spleen_8x16` | Bundled fonts covering printable ASCII (0x20–0x7E). |
| `draw_text(fb, font, x, y, text, on, scale)` | Draw text; clips at framebuffer edges. |
| `text_width(font, text, scale)` | Rendered width in pixels (constexpr). |

## Adding fonts

Convert any monospace BDF font with the included script and add the output
to your build:

```sh
scripts/bdf_to_c.py myfont.bdf my_font > my_font.cpp
```

## Important Notes

- Only glyph ink pixels are written; to replace previous text, clear (or
  redraw) the background first — full-frame `fb.clear()` + redraw is the
  simplest pattern.
- Characters outside a font's range advance the cursor without drawing.
- The bundled Spleen fonts are Copyright (c) 2018-2026 Frederic Cambus,
  BSD-2-Clause (see `fonts/LICENSE`).

## License

Apache-2.0 (component code); bundled fonts BSD-2-Clause.
