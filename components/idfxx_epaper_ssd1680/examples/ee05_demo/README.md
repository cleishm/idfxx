# EE05 Demo

Walks through the refresh modes of a 2.13" SSD1680 ePaper panel on the Seeed Studio XIAO ePaper Display Board
EE05.

## What it demonstrates

- Switching on the display's power supply with an `idfxx::gpio` output
- Setting up an SPI bus with `idfxx::spi::master_bus`, and panel I/O from
  `idfxx::epaper::ssd1680::spi_io_config()`
- Creating an `idfxx::epaper::ssd1680` driver with reset and BUSY pins
- Drawing text and shapes into an `idfxx::epaper::mono_framebuffer` through an `idfxx::gfx::canvas`, then showing
  it with `write()` and a full `refresh()`
- Blanking the panel with `clear()`, switching to 4-level grayscale with `set_color_mode()`, and drawing into an
  `idfxx::epaper::gray4_framebuffer`
- A fast refresh with `refresh(refresh_mode::fast)`
- Updating part of the screen with `write_rows()` and partial refreshes, using `start_refresh()` to draw the next
  frame while the panel is still updating, with a full refresh every 10 updates to clear ghosting
- Putting the panel into deep sleep with `sleep()`, after which the image stays on screen without power

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- A Seeed Studio XIAO ePaper Display Board EE05 (ESP32-S3 Plus)
- A 2.13" 122x250 SSD1680 ePaper panel connected to the board

The example is for the ESP32-S3 only (`sdkconfig.defaults` sets the target to `esp32s3`). It uses the EE05's
display wiring:

| Display signal             | ESP32-S3 GPIO |
| -------------------------- | ------------- |
| SCLK                       | GPIO 7        |
| MOSI                       | GPIO 9        |
| CS                         | GPIO 44       |
| DC                         | GPIO 10       |
| RESET                      | GPIO 38       |
| BUSY                       | GPIO 4        |
| Power enable (active high) | GPIO 43       |

To use a different board, change the `PIN_*` constants at the top of `main/main.cpp`.

> [!NOTE]
> GPIO 43 and 44 are the ESP32-S3's default UART0 TX and RX pins, which the example takes over for the display.
> Log output still reaches the monitor through the USB Serial/JTAG port, which is enabled as a secondary console
> by default.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_epaper_ssd1680:ee05_demo"
cd ee05_demo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The panel steps through six screens, about five seconds apart:

1. A title screen with "idfxx epaper" on a black banner, a large "hello" in a box, and "SSD1680 122x250"
2. Four horizontal bands, from white through light and dark gray to black
3. The title screen again, back in black and white
4. The title screen with "fast refresh" added, drawn with a fast refresh
5. A counter from 00 to 20, updated with partial refreshes
6. A closing "demo complete" screen, which stays on the panel after it goes to sleep
