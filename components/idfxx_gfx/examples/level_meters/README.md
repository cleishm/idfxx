# Level Meters

Animates six bar-graph level meters on an ILI9341 LCD, redrawing each frame in small bands with
`idfxx::gfx::render_banded`.

## What it demonstrates

- Turning on the backlight with an `idfxx::gpio` output
- Setting up an SPI bus with `idfxx::spi::master_bus` and an `idfxx::lcd::ili9341` panel through
  `idfxx::panel_io`
- Animating the full 240x320 screen through a 240x40 `idfxx::lcd::rgb565_framebuffer` by calling
  `idfxx::gfx::render_banded()` once per frame, so the framebuffer takes 19 KB instead of 150 KB
- Drawing labels with `draw_text()` and the `idfxx::font::spleen_8x16` font, including text scaled to twice its
  size
- Drawing each meter with `draw_rect()`, `fill_rect()` and `draw_vline()`, in a color chosen from its level
- Pacing the animation with `idfxx::delay()`

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
idf.py create-project-from-example "cleishm/idfxx_gfx:level_meters"
cd level_meters
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The display shows a dark blue "level meters" header above six horizontal bars labelled 60Hz, 250Hz, 1kHz, 4kHz,
8kHz and 16kHz. The bars rise and fall at random. Each bar is green below 60%, amber from 60% to 84%, and red at
85% and above.
