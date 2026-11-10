# Photo on EE05

Shows a JPEG photo in four grays on the 7.5" screen of the Seeed Studio XIAO ePaper Display Board
EE05, filling the screen, with the whole photo in a rectangle and a caption over that.

## What it demonstrates

- Switching on the display's power supply, setting up an SPI bus and panel I/O, and creating an
  `idfxx::epaper::uc8179` driver as in the `idfxx_epaper_uc8179` `ee05_demo` example
- Starting the gray levels from a known state: a full black refresh, then `clear()` and a white one,
  then `set_color_mode()` to select four grays
- Decoding a JPEG embedded in the app, with an `idfxx::memory_source` and `idfxx::jpeg::decoder`
- Converting it for the panel and uploading it with `idfxx::epaper::write_image()` and
  `idfxx::epaper::calibration::gdey075t7_gray4()`, filling the screen
- Fitting the whole photo in a rectangle over the first one, with `placement::target` and
  `image::layout::fit`, in which only the rectangle's pixels change
- Drawing a caption over each band with the overlay callback and `idfxx::gfx::canvas`, naming the
  band's type with `write_image<gray4_framebuffer>()`
- Showing both with one `refresh()`, then putting the panel into deep sleep with `sleep()`, after
  which the image stays on screen without power

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- A Seeed Studio XIAO ePaper Display Board EE05 (ESP32-S3 Plus)
- A 7.5" 800x480 UC8179 ePaper panel with GDEY075T7 glass connected to the board

The example is for the ESP32-S3 only (`sdkconfig.defaults` sets the target to `esp32s3`). It runs in
internal RAM, as the photo in `main/photo.jpg` is converted a few rows at a time.

The example uses the EE05's display wiring. To use a different board, change the `PIN_*` constants
at the top of `main/display_setup.hpp`.

| Display signal             | ESP32-S3 GPIO |
| -------------------------- | ------------- |
| SCLK                       | GPIO 7        |
| MOSI                       | GPIO 9        |
| CS                         | GPIO 44       |
| DC                         | GPIO 10       |
| RESET                      | GPIO 38       |
| BUSY                       | GPIO 4        |
| Power enable (active high) | GPIO 43       |

> [!NOTE]
> GPIO 43 and 44 are the ESP32-S3's default UART0 TX and RX pins, which the example takes over for
> the display. `sdkconfig.defaults` puts the console on the USB Serial/JTAG port, so log output
> reaches the monitor there.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_epaper_image:photo_ee05"
cd photo_ee05
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The panel goes black and then white, and then nothing happens while the photo is converted, twice.
The panel then refreshes, showing the photo filling the screen, and on its right the whole photo in
a rectangle, with the word "idfxx" across the rectangle's bottom edge.

To show your own photo, replace `main/photo.jpg`. A photo stored on its side, with an EXIF
orientation saying to turn it, still appears upright, but needs memory for a whole frame while it is
converted: about 1.15 MB, which means PSRAM on this board.

The photo is `kodim23` from the [Kodak Lossless True Color Image
Suite](https://r0k.us/graphics/kodak/), photographed by Steve Kelly. That page states its
understanding that Kodak released the suite for unrestricted usage.
