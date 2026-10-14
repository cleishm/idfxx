# Exception API

Shows how to report and handle idfxx errors with C++ exceptions.

## What it demonstrates

- Throwing a `std::system_error` built from an `idfxx::errc` error code
- Catching `std::system_error` and reading the error code's value and message
- Returning errors from a function as an `idfxx::result` with `idfxx::error()`, and turning a failed result into an exception
- Nesting `try`/`catch` blocks to handle errors from different sources separately

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_core:exception_api"
cd exception_api
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
