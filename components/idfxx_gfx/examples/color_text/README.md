# Color Text

Draws colored text and shapes on an ILI9341 LCD, rendering the frame in small bands with
`idfxx::gfx::render_banded`.

## What it demonstrates

- Turning on the backlight with an `idfxx::gpio` output
- Setting up an SPI bus with `idfxx::spi::master_bus` and an `idfxx::lcd::ili9341` panel through
  `idfxx::panel_io`
- Drawing a full 240x320 frame through a 240x40 `idfxx::lcd::rgb565_framebuffer` with
  `idfxx::gfx::render_banded()`, so the framebuffer takes 19 KB instead of 150 KB
- Drawing shapes that cross band boundaries with `fill_rect()`, `draw_rect()` and `draw_hline()`
- Drawing colored text with `draw_text()` and the `idfxx::font::spleen_8x16` font, including text scaled to twice
  its size
- Measuring text with `idfxx::font::text_width()` to right-align it

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

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

The example doesn't read from the display, so MISO isn't needed. To use different pins, change the `PIN_*`
constants at the top of `main/main.cpp`.

> [!NOTE]
> The default pins suit the ESP32-S2 and ESP32-S3. On other targets, change them first:
> - ESP32: GPIO 9–11 are connected to the module's SPI flash, and GPIO 12 and 15 are strapping pins.
> - ESP32-C3: GPIO 12–17 are connected to the SPI flash, and GPIO 9 is a strapping pin.
> - ESP32-C6: GPIO 12 is a USB pin, GPIO 9 and 15 are strapping pins, and GPIO 10 and 11 aren't available on
>   chips with in-package flash.
> - ESP32-H2: GPIO 15 is connected to the SPI flash, and GPIO 9 is a strapping pin.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_gfx:color_text"
cd color_text
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The display shows a dark blue header with a large "idfxx gfx", then six framed color swatches, each labelled in
its own color (red, orange, yellow, green, azure and violet). At the bottom, under a white line, a green
"-72dBm 45s" status line is right-aligned.
