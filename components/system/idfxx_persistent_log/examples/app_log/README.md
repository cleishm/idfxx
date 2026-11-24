# App Log

Keeps the app's own log lines in flash across resets. Each boot prints the lines stored before it,
then logs a few temperature readings.

## What it demonstrates

- Starting the log with `idfxx::persistent_log::start()`, with a boot count kept in NVS (in
  `main/boot_count.hpp`), with capture of log lines at info level, and without fault slots
- Printing the log's newest entries with `read_newest()`, a range-based `for` loop and
  `std::println`. `read_newest()` writes the lines that are still in RTC memory to flash first
- Logging with an `idfxx::log::logger`. Capture stores these lines in the log
- Adding a formatted line with `append()`, which stores the line in the log without printing it on
  the console

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

On the first boot after the flash is erased, the stored log holds only the line with the reason for
that boot's reset. On each later boot, the stored log also holds the lines that the earlier boots
logged.

## Hardware required

- Any ESP32-series development board

The example's `partitions.csv` adds the 128 KiB `log` partition that holds the log. The example
starts the log without fault slots, so the log uses the whole partition.

## Configuration

The example disables `CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES` in `sdkconfig.defaults`. The
log then stores only the lines that the example logs with its `logger`, the line that it adds with
`append()`, and the lines that the log adds itself, such as the reset reasons. Nothing wraps
ESP-IDF's log.

If you enable the option in menuconfig, the log also stores the lines that ESP-IDF and its
libraries log with `ESP_LOGx`. To receive these lines, idfxx_log wraps `esp_log` and `esp_log_va`
at link time, so nothing else in the app may wrap them. Each log call also uses more stack. The
option's help in menuconfig describes the wrap, and it refers to the help of idfxx_log's options
for the stack use.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_persistent_log:app_log"
cd app_log
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.

The writer task writes the lines to flash within 5 seconds. After that, reset the board, or unplug
it and plug it in again. The next boot prints the lines in the stored log.
