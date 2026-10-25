# E1004 Demo

Shows a six-color screen and then monochrome text pages on the 13.3" Spectra 6 ePaper panel of the Seeed Studio
reTerminal E1004 (Seeed's T133A01 module, built on the E Ink EL133UF1 glass).

## What it demonstrates

- Switching on the display's power supply with an `idfxx::gpio` output
- Setting up an SPI bus with `idfxx::spi::master_bus`
- Creating an `idfxx::epaper::el133uf1` driver on the bus, with the chip-select pins of the panel's two controllers,
  their shared D/C pin, and the reset and BUSY pins
- Drawing six color bars in landscape into a full-frame `idfxx::epaper::color6_framebuffer` through an
  `idfxx::gfx::rotated` view and an `idfxx::gfx::canvas`, then showing it with `write()` and `refresh()`
- Switching to monochrome with `set_color_mode()`, and drawing full-screen text pages with
  `idfxx::gfx::render_banded()` through a 1200x100 `idfxx::epaper::mono_framebuffer`, so no frame-sized buffer is
  needed
- Putting the panel into deep sleep with `sleep()`, after which the image stays on screen without power

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- A Seeed Studio reTerminal E1004 (ESP32-S3), with its 13.3" 1200x1600 Spectra 6 T133A01 module (E Ink EL133UF1 glass)

The example is for the ESP32-S3 only (`sdkconfig.defaults` sets the target to `esp32s3`). It also needs octal
PSRAM, which `sdkconfig.defaults` enables along with 8 MB of flash: the driver keeps a 960 KB copy of the frame in
PSRAM, and the six-color framebuffer is another 960 KB.

The example uses the E1004's display wiring. The panel's two controllers share the SPI bus and D/C pin, and each
has its own chip select: CS for the left half of the panel, CS1 for the right half.

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

To use a different board, change the `PIN_*` constants at the top of `main/main.cpp`.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_epaper_el133uf1:e1004_demo"
cd e1004_demo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The panel shows three screens, 10 seconds apart. Each refresh takes 25-30 seconds (longer when the panel is cold),
and the panel flashes through several colors while it updates.

1. Six vertical color bars (black, white, yellow, red, blue and green), each labelled with its color name, above
   "idfxx epaper" and, in red, "EL133UF1 1200x1600 Spectra 6", drawn in landscape
2. A black-and-white page in portrait, with "idfxx epaper" on a black banner, a large "hello" in a box,
   "monochrome, banded", and twelve numbered lines
3. A closing "demo complete" screen, which stays on the panel after it goes to sleep
