# Signal Statistics

Samples one analog pin at 20 kHz in the background and logs the minimum, maximum and mean once per
second.

## What it demonstrates

- Creating a continuous sampler (`idfxx::adc::sampler`) for one pin at a set sample rate (`20_kHz`)
- Checking the sample rate with `sample_rate()` and whether factory calibration is active with
  `calibrated()`
- Starting conversion with `start()`
- Reading batches of samples into a buffer with `read()`, which waits until samples are available
- Converting the raw minimum, maximum and mean to voltages with `to_voltage()` when calibration is
  active
- Counting dropped data with `overruns()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board
- A signal to measure, between 0 V and about 3.1 V

The example samples an ADC1 pin that depends on the target:

| Target                       | Pin     |
|------------------------------|---------|
| ESP32                        | GPIO 34 |
| ESP32-S2, ESP32-S3, ESP32-H2 | GPIO 1  |
| ESP32-C3                     | GPIO 3  |
| ESP32-C6                     | GPIO 2  |

Any source works, for example a potentiometer with its ends on 3.3 V and GND and its wiper on that pin.
Connect the source's ground to the board's GND. The sampler only works with ADC1 pins. To use a
different pin, change `signal_pin` in `main/main.cpp`.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_adc:signal_stats"
cd signal_stats
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
