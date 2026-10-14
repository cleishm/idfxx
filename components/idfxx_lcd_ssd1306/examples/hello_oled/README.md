# Hello OLED

Draws a test pattern on an SSD1306 OLED and animates it with partial updates, showing how to use
`idfxx::lcd::ssd1306` over I2C.

## What it demonstrates

- Setting up an I2C bus at 400 kHz with `idfxx::i2c::master_bus`
- Creating I2C panel I/O with `idfxx::panel_io` and `idfxx::lcd::ssd1306::i2c_io_config()`
- Creating an `idfxx::lcd::ssd1306` panel for a 128x64 module and turning it on with `display_on()`
- Drawing pixels into an `idfxx::lcd::mono_framebuffer` with `set_pixel()`, then sending the whole frame with
  `write()`
- Updating a single column with `write_region()`, and a band of rows with `write_rows()`
- Inverting the display with `invert_color()` and changing the brightness with `set_contrast()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- An ESP32-S2 or ESP32-S3 development board (see the note below for other targets)
- A 128x64 SSD1306 OLED module with an I2C interface, at address 0x3C

Wire the display to the board as follows, plus power and ground:

| Display pin | ESP32 GPIO |
| ----------- | ---------- |
| SDA         | GPIO 8     |
| SCL         | GPIO 9     |

To use different pins, change `PIN_SDA` and `PIN_SCL` at the top of `main/main.cpp`. If your module uses address
0x3D, pass it to `i2c_io_config()`. For a 128x32 module, change `.height = 64` to `.height = 32`.

> [!NOTE]
> The default pins suit the ESP32-S2 and ESP32-S3. On other targets, change them first:
> - ESP32: GPIO 8 and 9 are connected to the module's SPI flash.
> - ESP32-C3, ESP32-C6 and ESP32-H2: GPIO 8 and 9 are strapping pins.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_lcd_ssd1306:hello_oled"
cd hello_oled
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The display shows a border, an X from corner to corner, and a checkerboard band along the bottom. A bar then
sweeps across the middle from left to right. Each time it finishes, the bar is cleared and the display switches
between normal and inverted, with lower contrast while inverted.
