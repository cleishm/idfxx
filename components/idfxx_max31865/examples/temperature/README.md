# MAX31865 Temperature

Reads a PT1000 RTD through a MAX31865 once a second and prints the temperature and resistance.

## What it demonstrates

- Setting up an SPI bus with `idfxx::spi::master_bus`
- Creating an `idfxx::max31865::device` from a config with the RTD, reference resistor, wiring,
  notch filter, input filter time constant and DRDY pin
- Checking the RTD and its wiring with `detect_faults()`, and clearing faults with `clear_faults()`
- Setting fault thresholds in °C with `set_fault_thresholds()`
- Starting continuous conversion with `start_continuous()` and reading results with `read()`
- Converting codes with `to_temperature()` and `to_resistance()`
- Decoding latched faults with `read_faults()`
- Checking the chip's registers every 10 seconds with `verify()`, and rewriting them with `restore()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- An ESP32-S3 development board
- A MAX31865 breakout board (such as the Adafruit MAX31865) set up for a 3-wire PT1000
- A 3-wire PT1000 RTD

Wire the breakout to the board as follows, or change the `PIN_*` constants at the top of
`main/main.cpp`:

| MAX31865 pin | ESP32-S3 GPIO |
| ------------ | ------------- |
| SDI          | GPIO 11       |
| SDO          | GPIO 13       |
| CLK          | GPIO 12       |
| CS           | GPIO 10       |
| DRDY         | GPIO 14       |

Power the breakout from 3.3 V and GND.

> [!NOTE]
> These pins don't suit other chips. On the ESP32, GPIO 6–11 are connected to the SPI flash; on the
> ESP32-C3, GPIO 12–17 are; and on the ESP32-C6, GPIO 12 and 13 are USB-JTAG pins. Choose other pins
> on these chips.

## Configuration

Before building, set these in `main/main.cpp` to match your hardware:

- `.rref`: the reference resistor fitted on your board. The example uses 4300 Ω; read the value
  printed on your board's resistor.
- `.filter_time_constant`: the time constant (R × C) of the filter on the RTDIN inputs. The example
  uses 100 µs.
- `.notch`: `hz_50` for 50 Hz mains, `hz_60` for 60 Hz mains.
- `.r0` and `.wiring`: change these for a PT100 (`100_Ohm`) or a 2- or 4-wire RTD.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_max31865:temperature"
cd temperature
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
