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

Wire the battery's positive terminal through the first resistor to GPIO 3, and GPIO 3 through the
second resistor to GND. Connect the battery's negative terminal to GND. The example uses the default
12 dB attenuation, which measures up to roughly 3.1 V at the pin, so with equal resistors the battery
must stay below about 6.2 V. To use a different pin, change `battery_pin` in `main/main.cpp`; if your
resistors aren't equal, change `.divider` to match.

> [!NOTE]
> On the original ESP32, GPIO 3 is the console UART's RX pin and can't be used as an analog input.
> Choose an ADC pin such as GPIO 34 instead. GPIO 3 is a strapping pin on the ESP32-S3 and ESP32-H2.

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
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
