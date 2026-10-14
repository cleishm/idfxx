# Battery Voltage

Reads a battery's voltage through a 2:1 resistor divider every 5 seconds.

## What it demonstrates

- Creating a one-shot analog input (`idfxx::adc::input`) from a pin and an external divider ratio
  (`.divider = {2, 1}`), so readings report the battery voltage rather than the pin voltage
- Checking the input's pin with `pin()` and whether factory calibration is active with `calibrated()`
- Reading a calibrated voltage with `try_read_voltage()`, logging a warning and carrying on if a read
  fails

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`),
with `try_read_voltage()` where it handles a failed read and keeps going.

## Hardware required

- Any ESP32-series development board
- A battery, and two resistors of equal value (for example 100 kΩ each)

The example reads the battery on an ADC1 pin that depends on the target:

| Target                       | Pin     |
|------------------------------|---------|
| ESP32                        | GPIO 34 |
| ESP32-S2, ESP32-S3, ESP32-H2 | GPIO 1  |
| ESP32-C3                     | GPIO 3  |
| ESP32-C6                     | GPIO 2  |

Wire the battery's positive terminal through the first resistor to that pin, and the pin through the
second resistor to GND. Connect the battery's negative terminal to GND. The example uses the default
12 dB attenuation, which measures up to roughly 3.1 V at the pin, so with equal resistors the battery
must stay below about 6.2 V. To use a different pin, change `battery_pin` in `main/main.cpp`; if your
resistors aren't equal, change `.divider` to match.

The first log line says whether calibration is active. Without calibration data, voltage reads fail
and the example logs a warning each time.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_adc:battery_voltage"
cd battery_voltage
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
