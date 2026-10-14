# NVS Storage

Stores and reads back values in non-volatile storage (NVS) with `idfxx::nvs`.

## What it demonstrates

- Initializing NVS flash with `idfxx::nvs::flash::init()`
- Opening a namespace and checking `is_writeable()`
- Storing and reading integers of different sizes with `set_value()` and `get_value()`
- Storing and reading strings with `set_string()` and `get_string()`
- Storing and reading binary data with `set_blob()` and `get_blob()`
- Saving changes with `commit()`, and removing keys with `erase()` and `erase_all()`
- Catching the error from reading a key that doesn't exist
- Opening a namespace read-only, and catching the error from writing to it
- Deinitializing NVS flash with `idfxx::nvs::flash::deinit()`

The example erases everything it writes, so it leaves the `demo_ns` namespace empty.

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_nvs:nvs_storage"
cd nvs_storage
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
