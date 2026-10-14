# Flags

Shows how to use `idfxx::flags` for type-safe bit flags on a scoped enum.

## What it demonstrates

- Opting a custom `enum class` in to flag operators with `idfxx::enable_flags_operators`
- Combining flags with `operator|`
- Testing flags with `contains()` and `contains_any()`
- Checking for an empty set with `empty()`
- Removing flags with `operator-`
- Converting flags with `idfxx::to_underlying()` and `idfxx::to_string()`
- Formatting flags directly in log messages

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_core:flags"
cd flags
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
