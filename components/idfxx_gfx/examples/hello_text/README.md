# Hello Text

Draws text on an SSD1306 OLED, showing how to use `idfxx::gfx::canvas` with Spleen fonts.

## What it demonstrates

- Setting up an I2C bus with `idfxx::i2c::master_bus` and an `idfxx::lcd::ssd1306` display through
  `idfxx::panel_io`
- Drawing into an `idfxx::lcd::mono_framebuffer` through an `idfxx::gfx::canvas`
- Filling a rectangle with `fill_rect()`, and drawing unlit text on top of it
- Drawing text in the `idfxx::font::spleen_5x8` and `idfxx::font::spleen_8x16` fonts with `draw_text()`,
  including text scaled to twice its size
- Measuring text with `idfxx::font::text_width()` to right-align it
- Sending the finished frame to the display with `write()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- An ESP32-S2 or ESP32-S3 development board (see the note below for other targets)
- A 128x64 SSD1306 OLED module with an I2C interface, at address 0x3C

Wire the display to the board as follows, plus power and ground:

| Display pin | ESP32 GPIO |
| ----------- | ---------- |
| SDA         | GPIO 8     |
| SCL         | GPIO 9     |

To use different pins, change `PIN_SDA` and `PIN_SCL` at the top of `main/main.cpp`.

> [!NOTE]
> The default pins suit the ESP32-S2 and ESP32-S3. On other targets, change them first:
> - ESP32: GPIO 8 and 9 are connected to the module's SPI flash.
> - ESP32-C3, ESP32-C6 and ESP32-H2: GPIO 8 and 9 are strapping pins.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_gfx:hello_text"
cd hello_text
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The display shows a lit banner across the top with "idfxx_gfx demo" in dark text, a large "23.7" in the middle,
and a small "-72dBm 45s" status line in the bottom right corner.
