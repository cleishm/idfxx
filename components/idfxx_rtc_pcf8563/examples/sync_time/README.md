# PCF8563 Sync Time

Sets the system time from a PCF8563 real-time clock at boot, or seeds the clock with a fixed time if it
has lost track.

## What it demonstrates

- Setting up an I2C bus with `idfxx::i2c::master_bus`
- Creating an `idfxx::rtc::pcf8563` on the bus with the default address (0x51)
- Reading the time with `now()`, which returns `std::nullopt` when the clock's time can't be trusted
- Setting the system time from it with `idfxx::chrono::set_system_time()`
- Seeding a clock that has lost track with `set_time()`
- Reporting failures by catching `std::system_error`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- An ESP32-S2 or ESP32-S3 development board (see the note below for other targets)
- A PCF8563 module (with its 32.768 kHz crystal and a backup battery or supercapacitor), powered from
  3.3 V, at address 0x51

Wire the module to the board as follows, plus power and ground:

| Module pin | ESP32 GPIO |
| ---------- | ---------- |
| SDA        | GPIO 8     |
| SCL        | GPIO 9     |

To use different pins, change `PIN_SDA` and `PIN_SCL` at the top of `main/main.cpp`.

> [!NOTE]
> The default pins suit the ESP32-S2 and ESP32-S3. On other targets, change them first:
> - ESP32: GPIO 8 and 9 are connected to the module's SPI flash.
> - ESP32-C3, ESP32-C6 and ESP32-H2: GPIO 8 and 9 are strapping pins.

Some boards have a PCF8563 built in. On the Seeed reTerminal E1004, for example, it is on I2C0 with
SDA on GPIO 19 and SCL on GPIO 20, so set `PIN_SDA` and `PIN_SCL` to those. Don't use GPIO 19 and 20
on generic ESP32-S3 boards: they are the USB D-/D+ pins.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_rtc_pcf8563:sync_time"
cd sync_time
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
