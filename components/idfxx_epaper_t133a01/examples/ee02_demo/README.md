# EE02 Demo

Shows a six-color screen and then monochrome text pages on a 13.3" T133A01 Spectra 6 ePaper panel driven by the
Seeed Studio XIAO ePaper DIY Kit EE02.

## What it demonstrates

- Switching on the display's power supply with an `idfxx::gpio` output
- Setting up an SPI bus with `idfxx::spi::master_bus`, with a larger `max_transfer_sz` for the panel's large uploads
- Creating two panel I/Os from `idfxx::epaper::t133a01::spi_io_config()`, one for each of the panel's two
  controllers, with separate chip selects and a shared D/C pin
- Creating an `idfxx::epaper::t133a01` driver from both panel I/Os, with reset and BUSY pins
- Drawing six color bars in landscape into a full-frame `idfxx::epaper::color6_framebuffer` through an
  `idfxx::gfx::rotated` view and an `idfxx::gfx::canvas`, then showing it with `write()` and `refresh()`
- Switching to monochrome with `set_color_mode()`, and drawing full-screen text pages with
  `idfxx::gfx::render_banded()` through a 1200x100 `idfxx::epaper::mono_framebuffer`, so no frame-sized buffer is
  needed
- Putting the panel into deep sleep with `sleep()`, after which the image stays on screen without power

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- A Seeed Studio XIAO ePaper DIY Kit EE02 (XIAO ESP32-S3 Plus)
- A 13.3" 1200x1600 E Ink Spectra 6 (T133A01) ePaper panel connected to the board

The example is for the ESP32-S3 only (`sdkconfig.defaults` sets the target to `esp32s3`). It also needs octal
PSRAM, which `sdkconfig.defaults` enables along with 8 MB of flash: the driver keeps a 960 KB copy of the frame in
PSRAM, and the six-color framebuffer is another 960 KB.

The example uses the EE02's display wiring. The panel's two controllers share the SPI bus and D/C pin, and each
has its own chip select: CS for the left half of the panel, CS1 for the right half.

| Display signal             | ESP32-S3 GPIO |
| -------------------------- | ------------- |
| SCLK                       | GPIO 7        |
| MOSI                       | GPIO 9        |
| CS (left half)             | GPIO 44       |
| CS1 (right half)           | GPIO 41       |
| DC                         | GPIO 10       |
| RESET                      | GPIO 38       |
| BUSY                       | GPIO 4        |
| Power enable (active high) | GPIO 43       |

To use a different board, change the `PIN_*` constants at the top of `main/main.cpp`.

> [!NOTE]
> GPIO 43 and 44 are the ESP32-S3's default UART0 TX and RX pins, which the example takes over for the display.
> `sdkconfig.defaults` moves the console to the USB Serial/JTAG port instead, so log output reaches the monitor
> there.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_epaper_t133a01:ee02_demo"
cd ee02_demo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The panel shows three screens, 10 seconds apart. Each refresh takes 30-40 seconds (longer when the panel is cold),
and the panel flashes through several colors while it updates.

1. Six vertical color bars (black, white, yellow, red, blue and green), each labelled with its color name, above
   "idfxx epaper" and, in red, "T133A01 1200x1600 Spectra 6", drawn in landscape
2. A black-and-white page in portrait, with "idfxx epaper" on a black banner, a large "hello" in a box,
   "monochrome, banded", and twelve numbered lines
3. A closing "demo complete" screen, which stays on the panel after it goes to sleep
