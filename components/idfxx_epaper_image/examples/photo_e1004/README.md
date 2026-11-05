# Photo on E1004

Shows a JPEG photo in six colors, filling the 13.3" screen of the Seeed Studio reTerminal E1004.

## What it demonstrates

- Switching on the display's power supply, setting up an SPI bus, and creating an
  `idfxx::epaper::el133uf1` driver as in the `idfxx_epaper_el133uf1` `e1004_demo` example
- Selecting six colors with `set_color_mode()`
- Decoding a JPEG embedded in the app, with an `idfxx::memory_source` and `idfxx::jpeg::decoder`
- Converting it for the panel and uploading it with `idfxx::epaper::write_image()` and
  `idfxx::epaper::calibration::el133uf1()`, turned with `placement::orientation`, a band of rows at a
  time
- Showing it with `refresh()`, then putting the panel into deep sleep with `sleep()`, after which the
  image stays on screen without power

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- A Seeed Studio reTerminal E1004 (ESP32-S3), with its 13.3" 1200x1600 Spectra 6 T133A01 module (E Ink
  EL133UF1 glass)

The example is for the ESP32-S3 only (`sdkconfig.defaults` sets the target to `esp32s3`). It also needs
octal PSRAM, which `sdkconfig.defaults` enables along with 8 MB of flash: the driver keeps a 960 KB copy
of the frame in PSRAM, and the conversion takes about 193 KB more. The defaults also set 240 MHz and
`-O2`, which make the conversion over twice as fast.

The example uses the E1004's display wiring. To use a different board, change the `PIN_*` constants at
the top of `main/display_setup.hpp`.

| Display signal             | ESP32-S3 GPIO |
| -------------------------- | ------------- |
| SCLK                       | GPIO 7        |
| MOSI                       | GPIO 9        |
| CS (left half)             | GPIO 10       |
| CS1 (right half)           | GPIO 2        |
| DC                         | GPIO 11       |
| RESET                      | GPIO 38       |
| BUSY                       | GPIO 13       |
| Power enable (active high) | GPIO 12       |

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_epaper_image:photo_e1004"
cd photo_e1004
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The conversion takes over a minute with nothing happening on the panel. The refresh then takes about 30
seconds, and the panel flashes through several colors while it updates. The photo should show upright
with the E1004 in the same landscape orientation as the `e1004_demo` example, and it stays on the panel.

The photo in `main/photo.jpg` is stored on its side, with an EXIF orientation that says to turn it a
quarter turn clockwise. `TURN` in `main/main.cpp` is the opposite quarter turn, so the two cancel and the
photo is written as it is stored, with no frame in memory. If your E1004 stands the other way up, set
`TURN` to `idfxx::image::orientation::rotate_90`. To show your own photo, replace `main/photo.jpg`: one
stored upright also shows in landscape with the current `TURN`. Either change holds a 5.76 MB frame while
the photo is converted, which the board's 8 MB of PSRAM has room for.

The photo is `kodim23` from the [Kodak Lossless True Color Image Suite](https://r0k.us/graphics/kodak/),
photographed by Steve Kelly. That page states its understanding that Kodak released the suite for
unrestricted usage.
