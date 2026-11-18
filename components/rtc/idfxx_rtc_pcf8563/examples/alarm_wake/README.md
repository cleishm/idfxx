# PCF8563 Alarm Wake

Wakes the ESP32 from deep sleep with a PCF8563 real-time clock alarm, through the chip's INT pin.

## What it demonstrates

- Setting up an I2C bus with `idfxx::i2c::master_bus`
- Creating an `idfxx::rtc::pcf8563` on the bus with the default address (0x51)
- Reading the time with `now()`, and seeding a clock that has lost track with `set_time()`
- Telling an alarm wake from other resets with `idfxx::sleep::wakeup_cause()` and `events()`, and
  releasing INT with `clear_events()`
- Setting the next alarm with `set_alarm()`, with its interrupt enabled to drive INT
- Sleeping until INT goes low with `idfxx::sleep::deep_sleep()` and `idfxx::sleep::ext0_wake`
- Reporting failures by catching `std::system_error`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- An ESP32-S2 or ESP32-S3 development board (see the note below for other targets)
- A PCF8563 module (with its 32.768 kHz crystal and a backup battery or supercapacitor), powered
  from 3.3 V, at address 0x51

Wire the module to the board as follows, plus power and ground:

| Module pin | ESP32 GPIO |
| ---------- | ---------- |
| SDA        | GPIO 8     |
| SCL        | GPIO 9     |
| INT        | GPIO 4     |

INT is open-drain and active-low, so it needs a pull-up to 3.3 V. Many modules have one; if yours
doesn't, fit a resistor of about 10 kΩ between INT and 3.3 V.

To use different pins, change `PIN_SDA`, `PIN_SCL` and `PIN_INT` at the top of `main/main.cpp`.
`PIN_INT` must be an RTC-capable GPIO, because EXT0 wake-up uses the RTC IO.

> [!NOTE]
> The example needs EXT0 wake-up, which only the ESP32, ESP32-S2 and ESP32-S3 have; it fails to
> compile on other targets. The default pins suit the ESP32-S2 and ESP32-S3. On the original ESP32,
> change `PIN_SDA` and `PIN_SCL` first, because GPIO 8 and 9 are connected to the module's SPI
> flash.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_rtc_pcf8563:alarm_wake"
cd alarm_wake
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
