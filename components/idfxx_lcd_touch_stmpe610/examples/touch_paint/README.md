# Touch Paint

Paints dots wherever you touch an ILI9341 display, showing how to read an `idfxx::lcd::stmpe610` touch controller
that shares the display's SPI bus.

## What it demonstrates

- Turning on the backlight with an `idfxx::gpio` output
- Setting up one SPI bus with `idfxx::spi::master_bus`, with MISO so the touch controller can be read
- Two `idfxx::panel_io` devices on that bus, each with its own CS pin: the display at 40 MHz and the touch
  controller at 1 MHz
- Creating an `idfxx::lcd::ili9341` panel, and an `idfxx::lcd::stmpe610` touch controller whose coordinates match
  the 240x320 display
- Getting the ESP-IDF handles with `idf_handle()` to draw with `esp_lcd_panel_draw_bitmap()` and to poll for
  touches with `esp_lcd_touch_read_data()` and `esp_lcd_touch_get_data()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- An ESP32-S2 or ESP32-S3 development board (see the note below for other targets)
- A 240x320 ILI9341 display module with an STMPE610 resistive touch controller, both on SPI

The display and the touch controller share SCK, MOSI and MISO, and each has its own CS. Wire them to the board as
follows, plus power and ground:

| Display pin     | ESP32 GPIO |
| --------------- | ---------- |
| SCK             | GPIO 12    |
| MOSI            | GPIO 11    |
| MISO            | GPIO 13    |
| Display CS      | GPIO 10    |
| DC              | GPIO 9     |
| RESET           | GPIO 14    |
| LED (backlight) | GPIO 15    |
| Touch CS        | GPIO 16    |

To use different pins, change the `PIN_*` constants at the top of `main/main.cpp`.

> [!NOTE]
> The default pins suit the ESP32-S2 and ESP32-S3. On other targets, change them first:
> - ESP32: GPIO 9–11 and 16 are connected to the module's SPI flash or PSRAM, and GPIO 12 and 15 are strapping
>   pins.
> - ESP32-C3: GPIO 12–17 are connected to the SPI flash, and GPIO 9 is a strapping pin.
> - ESP32-C6: GPIO 12 and 13 are the USB pins, GPIO 9 and 15 are strapping pins, and GPIO 10 and 11 aren't
>   available on chips with in-package flash.
> - ESP32-H2: GPIO 15 and 16 are connected to the SPI flash, and GPIO 9 is a strapping pin.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_lcd_touch_stmpe610:touch_paint"
cd touch_paint
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The screen clears to black. Touch it, or drag a stylus across it, to paint white dots.
