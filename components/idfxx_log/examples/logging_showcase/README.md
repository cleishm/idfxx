# Logging Showcase

Shows the logging features of `idfxx::log`, from simple messages to hex dumps.

## What it demonstrates

- Logging at each level with an `idfxx::log::logger`
- Formatting arguments with `std::format`-style placeholders
- Logging with a tag through the free functions (`idfxx::log::error()`, `idfxx::log::info()` and so on)
- Changing the level for one tag with `idfxx::log::set_level()`
- Changing the default level for all tags with `idfxx::log::set_default_level()`
- Converting and formatting `idfxx::log::level` values with `idfxx::to_string()`
- Logging buffers with `buffer_hex()`, `buffer_char()` and `buffer_hex_dump()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

`sdkconfig.defaults` sets the default log level to verbose (`CONFIG_LOG_DEFAULT_LEVEL_VERBOSE=y`), so debug and verbose messages appear.

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_log:logging_showcase"
cd logging_showcase
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
