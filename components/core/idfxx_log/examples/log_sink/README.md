# Log Sink

Stores the most recent warnings and errors in a custom log sink, then prints them. The sink
receives lines logged through `idfxx::log` and lines logged through ESP-IDF's log.

## What it demonstrates

- Writing a sink: a class derived from `idfxx::log::sink`. Its `do_wants()` selects warnings and
  errors. Its `do_write()` stores copies of the last 8 lines. A mutex protects them because several
  tasks can log at the same time
- Adding the sink for every log line with `idfxx::log::add_sink(idfxx::log::all_lines, sink)`
- Lowering one tag's console level with `idfxx::log::set_level()`: the console no longer prints
  that tag's warnings, but the sink still receives them
- Logging with an `idfxx::log::logger`, with the free functions and with a plain `ESP_LOGW`. The
  same sink receives all three
- Removing the sink with `idfxx::log::remove_sink()`

The example prints the stored lines: the warnings and errors from both APIs, including the warning
from the tag whose console level was lowered. It prints none of the info lines.

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board; nothing needs to be connected

## Configuration

A sink can receive every ESP-IDF log line only when `CONFIG_IDFXX_LOG_ALL_LINES` is enabled
(Component config → IDFXX log → Let sinks receive every log line). The example's
`sdkconfig.defaults` already enables it. With this option enabled, idfxx_log wraps `esp_log` and
`esp_log_va` at link time, so nothing else in the app may wrap them. Each task that logs while
such a sink is added also uses more stack. The option's help text gives the amounts.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_log:log_sink"
cd log_sink
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
