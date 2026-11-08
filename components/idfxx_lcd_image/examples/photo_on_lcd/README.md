# Photo on LCD

Shows a JPEG photo on an ILI9341 color LCD, first filling the screen and then fitted in a rectangle with
a caption drawn over it.

## What it demonstrates

- Decoding a JPEG embedded in the app, with an `idfxx::memory_source` and `idfxx::jpeg::decoder`
- Using the panel in landscape with `swap_xy()`, and filling the screen with `idfxx::lcd::write_image()`
- Fitting the whole photo in a rectangle with `placement::target` and `image::layout::fit`
- Drawing a caption over each band with the overlay callback and `idfxx::gfx::canvas`
- Writing the picture a band of rows at a time, with no frame buffer, and sizing the SPI bus's
  `max_transfer_sz` from `write_options::band_rows`
- Setting up the SPI bus, panel I/O and `idfxx::lcd::ili9341` panel as in the `idfxx_lcd_ili9341`
  `color_bars` example

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- An ESP32-S3 (or other ESP32) development board
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
`PIN_*` constants at the top of `main/display_setup.hpp`. On the ESP32, change the pins first: GPIO 9–11
are connected to the module's SPI flash, and GPIO 12 and 15 are strapping pins.

The photo is shown in landscape, with `swap_xy(true)` in `main/main.cpp`. If it appears mirrored or
upside down on your module, add a call to `mirror()` after that one: `mirror(true, true)` turns the
picture upside down.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_lcd_image:photo_on_lcd"
cd photo_on_lcd
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The display first shows the photo filling the screen, and after 5 seconds the whole photo fitted in a
rectangle, with the word "idfxx" drawn over its bottom edge. To show your own photo, replace
`main/photo.jpg`. A photo stored on its side, with an EXIF orientation saying to turn it, still appears
upright, but needs memory for a whole frame while it is converted.

The photo is `kodim23` from the [Kodak Lossless True Color Image Suite](https://r0k.us/graphics/kodak/),
photographed by Steve Kelly. That page states its understanding that Kodak released the suite for
unrestricted usage.
