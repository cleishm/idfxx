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
- A signal to measure on GPIO 3, between 0 V and about 3.1 V

Any source works, for example a potentiometer with its ends on 3.3 V and GND and its wiper on GPIO 3.
Connect the source's ground to the board's GND. The sampler only works with ADC1 pins. To use a
different pin, change `signal_pin` in `main/main.cpp`.

> [!NOTE]
> On the original ESP32, GPIO 3 is the console UART's RX pin and isn't an ADC pin. Choose an ADC1 pin
> (GPIO 32–39) instead. GPIO 3 is a strapping pin on the ESP32-S3 and ESP32-H2.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_adc:signal_stats"
cd signal_stats
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
