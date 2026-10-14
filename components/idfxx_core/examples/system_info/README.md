# System Info

Prints application, reset, heap and clock information, and generates random numbers.

## What it demonstrates

- Reading application metadata with `idfxx::app` (project name, version, ESP-IDF version, build date and time, ELF SHA-256)
- Reading the last reset reason with `idfxx::last_reset_reason()`
- Reading current and minimum free heap with `idfxx::memory::free_size()` and `minimum_free_size()`
- Generating random numbers with `idfxx::random()`, and using `idfxx::random_device` with a standard distribution
- Reading the FreeRTOS tick count with `idfxx::chrono::tick_clock`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_core:system_info"
cd system_info
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
