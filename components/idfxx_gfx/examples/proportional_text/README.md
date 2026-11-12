# Proportional Text

Draws a small café menu in the Inter typeface on an ILI9341 LCD: centered, left-aligned and
right-aligned UTF-8 text with accents and curly quotes in four sizes, plus a wrapped paragraph.

## What it demonstrates

- Turning on the backlight with an `idfxx::gpio` output
- Setting up an SPI bus with `idfxx::spi::master_bus` and an `idfxx::lcd::ili9341` panel through
  `idfxx::panel_io`
- Drawing a full 240x320 frame through a 240x40 `idfxx::lcd::rgb565_framebuffer` with
  `idfxx::gfx::render_banded()`, so the framebuffer takes 19 KB instead of 150 KB
- Drawing UTF-8 text with `draw_text()` and the proportional fonts `idfxx::font::inter_48`,
  `inter_32`, `inter_24` and `inter_16` from `idfxx_font_inter`, each at its native size
- Laying out text in a box between the margins with `draw_text_box()`: the heading and subtitle
  centered and the footer right-aligned with `idfxx::gfx::text_align`, and a paragraph wrapped to
  the box width, breaking between words and hyphenating a long word at a soft hyphen (`\u00AD`)
- Stacking lines with each font's `line_height` and the height `draw_text_box()` returns

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- An ESP32-S2 or ESP32-S3 development board (see the note below for other targets)
- A 240x320 ILI9341 display module with an SPI interface

Wire the display to the board as follows, plus power and ground:

| Display pin     | ESP32 GPIO |
| --------------- | ---------- |
| SCK             | GPIO 12    |
| SDI (MOSI)      | GPIO 11    |
| CS              | GPIO 10    |
| DC              | GPIO 9     |
| RESET           | GPIO 14    |
| LED (backlight) | GPIO 15    |

The example doesn't read from the display, so MISO isn't needed. To use different pins, change the
`PIN_*` constants at the top of `main/main.cpp`.

> [!NOTE]
> The default pins suit the ESP32-S2 and ESP32-S3. On other targets, change them first:
> - ESP32: GPIO 9–11 are connected to the module's SPI flash, and GPIO 12 and 15 are strapping pins.
> - ESP32-C3: GPIO 12–17 are connected to the SPI flash, and GPIO 9 is a strapping pin.
> - ESP32-C6: GPIO 12 is a USB pin, GPIO 9 and 15 are strapping pins, and GPIO 10 and 11 aren't
>   available on chips with in-package flash.
> - ESP32-H2: GPIO 15 is connected to the SPI flash, and GPIO 9 is a strapping pin.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_gfx:proportional_text"
cd proportional_text
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
