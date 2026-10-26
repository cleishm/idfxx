# Photo on LCD

Decodes a JPEG photo and shows it on an ILI9341 color LCD, turned upright by its EXIF orientation and filling
the screen in landscape.

## What it demonstrates

- Reading a JPEG embedded in the app with an `idfxx::memory_source`, and getting its size and EXIF orientation
  from `idfxx::jpeg::decoder::info()`
- Combining the photo's EXIF orientation with the panel's rotation into one transform with
  `idfxx::image::compose()`
- Decoding at the smallest scale that covers the screen with `scale_to_cover()`, swapping the target when the
  transform turns the photo a quarter turn (`idfxx::image::swaps_axes()`)
- Decoding the whole photo into an `idfxx::image::buffer` in PSRAM (`idfxx::spiram_allocator`), then turning it
  with `idfxx::image::oriented()`
- Centring the photo on the screen with `subview()`: cropping whatever overhangs, and leaving black borders where
  the photo is smaller
- Converting `idfxx::image::rgb888` pixels to `idfxx::lcd::rgb565` and drawing them in 40-row bands with
  `draw_bitmap()`
- Setting up the SPI bus, panel I/O and `idfxx::lcd::ili9341` panel as in the `idfxx_lcd_ili9341` `color_bars`
  example

The `photo_on_lcd_streaming` example does the same without PSRAM, decoding one row at a time.

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- An ESP32-S2 or ESP32-S3 development board with PSRAM (see the note below for other targets). Boards with
  octal PSRAM, such as the ESP32-S3-DevKitC-1 N8R8 and the XIAO ESP32S3, need the setting described under
  Configuration.
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

The photo is shown in landscape. Starting from the display's native portrait orientation, with pixel row 0
along the top edge, turn it a quarter turn counter-clockwise, so that row 0 runs down the left edge. To turn the
display clockwise instead, set `PANEL_ROTATION` in `main/main.cpp` to `idfxx::image::orientation::rotate_270`;
`idfxx::image::orientation::identity` shows the photo in portrait, cropped to fill the screen.

> [!NOTE]
> The ESP32-C3, ESP32-C6 and ESP32-H2 don't support PSRAM, so they can't run this example; use
> `photo_on_lcd_streaming` instead. On the ESP32, change the pins first: GPIO 9–11 are connected to the module's
> SPI flash, and GPIO 12 and 15 are strapping pins.

## Configuration

`sdkconfig.defaults` enables PSRAM in quad mode. Modules with octal PSRAM, such as ESP32-S3 N8R8 and N16R8
modules and the XIAO ESP32S3, need octal mode instead, or the app stops at boot with a PSRAM error. After
`idf.py set-target`, run `idf.py menuconfig` and select Component config → ESP PSRAM → Support for external,
SPI-connected RAM → SPI RAM config → Mode (QUAD/OCT) of SPI RAM chip in use → Octal Mode PSRAM
(`CONFIG_SPIRAM_MODE_OCT`). Alternatively, add `CONFIG_SPIRAM_MODE_OCT=y` to `sdkconfig.defaults` before running
`idf.py set-target`.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_jpeg:photo_on_lcd"
cd photo_on_lcd
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The display shows a landscape scene, with the word "idfxx", the sun, a house and a tree, the right way up when the
display is turned as described above. The photo in `main/photo.jpg` is stored turned a quarter turn
counter-clockwise, with an EXIF orientation that says to turn it clockwise, so it would appear sideways if the
orientation were ignored. To show your own photo, replace `main/photo.jpg`.
