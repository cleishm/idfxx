# Photo on LCD (streaming)

Decodes a JPEG photo one row at a time and shows it on an ILI9341 color LCD, turned upright by its EXIF
orientation and filling the screen in landscape, without needing PSRAM.

## What it demonstrates

- Reading a JPEG embedded in the app with an `idfxx::memory_source`, and getting its size and EXIF orientation
  from `idfxx::jpeg::decoder::info()`
- Capping the decoder's working memory with `decoder::config::memory_limit`, so that a photo too large to decode in
  internal RAM, such as a big progressive JPEG, fails with `idfxx::jpeg::errc::too_large`, and reporting that case
- Combining the photo's EXIF orientation with the panel's rotation into one transform with
  `idfxx::image::compose()`
- Decoding at the smallest scale that covers the screen with `scale_to_cover()`, swapping the target when the
  transform turns the photo a quarter turn (`idfxx::image::swaps_axes()`)
- Decoding row by row into a sink lambda with `decode()`, so only one row of pixels is held in memory
- Working out where each decoded row lands on the panel with `idfxx::image::source_point()` and
  `idfxx::image::inverse()`: as a panel row or, when the photo is turned a quarter turn, a panel column, running
  forwards or backwards
- Centring the photo on the screen: skipping rows that are cropped off, drawing only the visible part of the rest,
  and leaving black borders where the photo is smaller
- Converting `idfxx::image::rgb888` pixels to `idfxx::lcd::rgb565` in panel order and drawing each row as a
  1-pixel-high row or 1-pixel-wide column with `try_draw_bitmap()`, whose error stops the decode
- Setting up the SPI bus, panel I/O and `idfxx::lcd::ili9341` panel as in the `idfxx_lcd_ili9341` `color_bars`
  example

The `photo_on_lcd` example does the same more simply, by decoding the whole photo into PSRAM first.

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- An ESP32-S2 or ESP32-S3 development board (see the note below for other targets); PSRAM isn't needed
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
> The default pins suit the ESP32-S2 and ESP32-S3. On other targets, change them first:
> - ESP32: GPIO 9–11 are connected to the module's SPI flash, and GPIO 12 and 15 are strapping pins.
> - ESP32-C3: GPIO 12–17 are connected to the SPI flash, and GPIO 9 is a strapping pin.
> - ESP32-C6: GPIO 12 is a USB pin, GPIO 9 and 15 are strapping pins, and GPIO 10 and 11 aren't available on
>   chips with in-package flash.
> - ESP32-H2: GPIO 15 is connected to the SPI flash, and GPIO 9 is a strapping pin.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_jpeg:photo_on_lcd_streaming"
cd photo_on_lcd_streaming
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The display shows a landscape scene, with the word "idfxx", the sun, a house and a tree, the right way up when the
display is turned as described above. It fills in a line at a time, from the right-hand edge, as the rows are
decoded. The photo in `main/photo.jpg` is stored turned a quarter turn counter-clockwise, with an EXIF orientation
that says to turn it clockwise, so it would appear sideways if the orientation were ignored.

To show your own photo, replace `main/photo.jpg`. A baseline JPEG needs working memory for its tables and a few rows
of pixels, which grows with the width it is decoded at: about 28 KB for the example photo. A progressive JPEG also
needs about 3 bytes of working memory per pixel of the full-size image. A photo that needs more than
`DECODER_MEMORY_LIMIT` (128 KB) is reported on the serial output instead of being shown.
