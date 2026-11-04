# Inks on EE05

Shows a frame that was converted to the panel's inks elsewhere, as a phone or browser would, on the 7.5"
screen of the Seeed Studio XIAO ePaper Display Board EE05, with no image decoder or converter on the
device.

## What it demonstrates

- Switching on the display's power supply, setting up an SPI bus and panel I/O, and creating an
  `idfxx::epaper::uc8179` driver as in the `idfxx_epaper_uc8179` `ee05_demo` example
- Starting the gray levels from a known state: a full black refresh, then `clear()` and a white one, then
  selecting `color_mode::gray4`
- Reading a frame of four-gray inks from an `idfxx::memory_source` over the frame embedded in the app
- Writing it to the whole panel with `idfxx::epaper::write_inks()`, a band of rows at a time
- Showing it with `refresh()`, then putting the panel into deep sleep with `sleep()`, after which the
  image stays on screen without power

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`). A
real device would read the frame from a network connection instead, which means writing an
`idfxx::byte_source` type for the connection. `write_inks()` takes any `idfxx::byte_source`, so the only
line that changes here is the one that creates the source. `write_inks()` checks the frame's length only;
see
[Writing a frame of inks from a byte source](https://github.com/cleishm/idfxx/tree/main/components/idfxx_epaper#writing-a-frame-of-inks-from-a-byte-source)
for what else can be checked.

## Hardware required

- A Seeed Studio XIAO ePaper Display Board EE05 (ESP32-S3 Plus)
- A 7.5" 800x480 UC8179 ePaper panel with GDEY075T7 glass connected to the board

The example is for the ESP32-S3 only (`sdkconfig.defaults` sets the target to `esp32s3`). It runs in
internal RAM, as the frame is read a few rows at a time.

The example uses the EE05's display wiring. To use a different board, change the `PIN_*` constants at the
top of `main/display_setup.hpp`.

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
> GPIO 43 and 44 are the ESP32-S3's default UART0 TX and RX pins, which the example takes over for the
> display. `sdkconfig.defaults` puts the console on the USB Serial/JTAG port, so log output reaches the
> monitor there.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_epaper:inks_ee05"
cd inks_ee05
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The panel goes black and then white, then refreshes to a photo of two macaws, in four grays.

## The frame

`main/frame.bin` is the whole 800x480 panel in four grays at 2 bits per pixel, 0 for the darkest to 3 for
the lightest, packed as `epi_pack_row` with `EPI_PACK_2BPP` does: each row padded to a whole byte, rows
from top to bottom, no header, so exactly 96,000 bytes. It was made from the 768x512 JPEG `photo.jpg` of
the `idfxx_epaper_image` `photo_ee05` example with the
[`cleishm/epaper-image`](https://github.com/cleishm/epaper-image) library's `convert` tool, using the
library's calibration for the GDEY075T7 in four grays and its default preset, covering the panel and
cropping the photo to fit:

```bash
convert --panel gdey075t7-gray4 --cover photo_ee05/main/photo.jpg main/frame.bin
```

That calibration was measured with the uc8179 driver's default four-gray waveform, at its default 20 Hz
and ±15 V, which this example doesn't change, so the grays come out as the converter intended. A frame
made for different settings, or other glass, needs its own calibration; `write_inks()` can't tell the
difference.

The photo is `kodim23` from the [Kodak Lossless True Color Image Suite](https://r0k.us/graphics/kodak/),
photographed by Steve Kelly. That page states its understanding that Kodak released the suite for
unrestricted usage.
