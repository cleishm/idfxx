# Deep Sleep Partial

Keeps a 2.13" SSD1680 ePaper panel on partial refreshes across ESP32 deep sleep, on the Seeed Studio XIAO ePaper
Display Board EE05.

## What it demonstrates

- Switching on the display's power supply with an `idfxx::gpio` output
- Setting up an SPI bus with `idfxx::spi::master_bus`, and panel I/O from
  `idfxx::epaper::ssd1680::spi_io_config()`
- Creating an `idfxx::epaper::ssd1680` driver with reset and BUSY pins
- Drawing into an `idfxx::epaper::mono_framebuffer` through an `idfxx::gfx::canvas`, and showing it with `write()`
  and `refresh()`
- Saving the frame on the glass in RTC memory (`RTC_DATA_ATTR`) before deep sleep, and handing it back to the driver
  with `restore_baseline()` after the wake, so the next update can still be a partial refresh
- A full refresh every 10 wakes to clear ghosting
- Putting the panel to sleep with `sleep()`, then deep-sleeping on a timer with `idfxx::sleep::deep_sleep()`

A deep sleep restarts the ESP32, and the driver starts with no record of what the panel shows. Without
`restore_baseline()`, the first partial refresh after every wake would be promoted to a full one.

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

The panel's frame is about 4 KB, small enough for RTC memory. Frames for larger panels (a 7.5" 800x480 panel needs
48 KB) don't fit there, and need saving to flash or an SD card instead; see the comment at the top of
`main/main.cpp`.

> [!NOTE]
> GPIO 43 and 44 are the ESP32-S3's default UART0 TX and RX pins, which the example takes over for the display.
> Log output still reaches the monitor through the USB Serial/JTAG port, which is enabled as a secondary console
> by default.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_epaper_ssd1680:deep_sleep_partial"
cd deep_sleep_partial
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The board wakes every 30 seconds. Each wake updates a wake counter and a progress bar towards the next full
refresh, then goes back to deep sleep. The USB Serial/JTAG port disconnects while the board sleeps, so the monitor
loses the connection and picks it up again on the next wake. A power-on or reset starts the counter from zero with
a full refresh.
