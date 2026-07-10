# idfxx_font_spleen

The [Spleen](https://github.com/fcambus/spleen) bitmap fonts (5x8 and 8x16) as idfxx font data.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__font__spleen.html)**

## Features

- `spleen_5x8` — compact status text
- `spleen_8x16` — headline text; scale 2 (via `idfxx::gfx::draw_text`) gives
  16x32 digits
- Covers printable ASCII (0x20–0x7E)
- One translation unit per font, so fonts an application never references
  are dropped at link time

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- Components: `idfxx_font`

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_font_spleen: "^1.0.0"
```

## Usage

```cpp
#include <idfxx/font/spleen>
#include <idfxx/gfx>

idfxx::gfx::draw_text(fb, idfxx::font::spleen_5x8, 2, 1, "status", true);
idfxx::gfx::draw_text(fb, idfxx::font::spleen_8x16, 4, 16, "23.7", true, 2);
```

## API Overview

| Item | Description |
| ---- | ----------- |
| `idfxx::font::spleen_5x8` | 5x8 `mono_font`, printable ASCII. |
| `idfxx::font::spleen_8x16` | 8x16 `mono_font`, printable ASCII. |

## Important Notes

- The original BDF sources are included under `fonts/` and can be
  regenerated with `idfxx_font`'s `scripts/bdf_to_c.py`.
- Spleen is Copyright (c) 2018-2026 Frederic Cambus.

## License

BSD-2-Clause (see [LICENSE](LICENSE)).
