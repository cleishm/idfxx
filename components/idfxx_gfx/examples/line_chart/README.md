# Line Chart

Plots a scrolling line chart of a simulated temperature on an SSD1306 OLED, showing how to animate with
`idfxx::gfx::canvas`.

## What it demonstrates

- Setting up an I2C bus with `idfxx::i2c::master_bus` and an `idfxx::lcd::ssd1306` display through
  `idfxx::panel_io`
- Drawing into an `idfxx::lcd::mono_framebuffer` through an `idfxx::gfx::canvas`
- Animating by clearing the canvas with `clear()` and redrawing the whole frame on each tick
- Drawing a text readout with `draw_text()` and the `idfxx::font::spleen_5x8` font
- Drawing the chart frame with `draw_rect()`, a dotted midline with `draw_hline()`, and the data as a line with
  `draw_line()`
- Sending each frame to the display with `write()`, about 10 times a second

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- An ESP32-S2 or ESP32-S3 development board (see the note below for other targets)
- A 128x64 SSD1306 OLED module with an I2C interface, at address 0x3C

Wire the display to the board as follows, plus power and ground:

| Display pin | ESP32 GPIO |
| ----------- | ---------- |
| SDA         | GPIO 8     |
| SCL         | GPIO 9     |

To use different pins, change `PIN_SDA` and `PIN_SCL` at the top of `main/main.cpp`.

> [!NOTE]
> The default pins suit the ESP32-S2 and ESP32-S3. On other targets, change them first:
> - ESP32: GPIO 8 and 9 are connected to the module's SPI flash.
> - ESP32-C3, ESP32-C6 and ESP32-H2: GPIO 8 and 9 are strapping pins.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_gfx:line_chart"
cd line_chart
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The display shows the latest reading (for example "temp 23.7C") above a framed chart. The simulated temperature
wanders between 18 °C and 30 °C, and its trace scrolls from right to left.
