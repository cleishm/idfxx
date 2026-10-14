# Result API

Shows how to report and handle idfxx errors without exceptions, using `idfxx::result`.

## What it demonstrates

- Returning a value or an error from a function as an `idfxx::result<T>`, with `idfxx::error()` and `idfxx::errc` codes
- Returning success or an error with `idfxx::result<void>`
- Checking a result, then reading its value or its error message
- Passing an error from one result type to another with `idfxx::error(std::error_code)`
- Stopping the program on an unexpected error with `idfxx::abort_on_error()`, with and without a callback that logs the error first

The example uses the result-based API (`try_*`); `CONFIG_COMPILER_CXX_EXCEPTIONS` is not set in `sdkconfig.defaults`.

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_core:result_api"
cd result_api
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
