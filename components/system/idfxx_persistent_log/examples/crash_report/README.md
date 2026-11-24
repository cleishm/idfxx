# Crash Report

Keeps a support log that survives crashes. Each boot prints the faults recorded before it, each
with the log lines that led up to it. It then logs, reads a sensor that fails for a while, and
crashes.

## What it demonstrates

- Starting the log with `idfxx::persistent_log::start()`, with a boot count kept in NVS (in
  `main/boot_count.hpp`) and capture of log lines at info level for the example's tag
- Reading the stored faults with `read_faults()`, and printing each fault and the entries of its
  context (`context()`) with range-based `for` loops and `std::println`
- Printing the log's newest entries with `read_newest()`, a range-based `for` loop and
  `std::println`. `read_newest()` writes the lines that are still in RTC memory to flash first
- Logging with an `idfxx::log::logger`. Capture stores these lines in the log
- Handling a fault that keeps happening. `record_fault()` with the `suppress_repeats` tag records
  the sensor's first failure and only logs its repeats, even across a reset. `lift_suppression()`
  lifts the suppression once the sensor answers again. The sensor is simulated (in
  `main/sensor.hpp`)
- A crash (`abort()`) after about 10 seconds. The next boot records a `panic` fault, with the lines
  before the crash as its context, and prints it. A reset fault is also recorded with its repeats
  suppressed, so later panics are only logged as repeats
- A core dump in flash (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` in `sdkconfig.defaults` and a
  `coredump` partition). From the dump, the `panic` fault gets the crashing task, its program
  counter and backtrace, and the panic reason as its detail

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`). It gives the main task a 6 KiB stack (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`),
because recording a fault uses about 4.7 KiB of the main task's stack in a debug build. That is
more than the default of 3584 bytes.

The example keeps crashing and booting again, about every 10 seconds, until it is stopped:

- On the first boot, the sensor stops answering, and the example records a `sensor` fault.
- On the second boot, the sensor answers again partway through. Its failures before that are only
  repeats, because the suppression survives the reset. The second boot also records a `panic`
  fault.
- On every boot after that, the sensor answers. Each of these boots finds another panic. A panic
  has the same kind as the one recorded, so it is a repeat, and only logs
  `W persistent_log: fault repeat kind=panic`. This continues until a reset not caused by a fault,
  such as a power cycle, lifts the suppression. See the component README's notes on suppression
  and reset faults.

The log keeps the newest four faults.

## Hardware required

- Any ESP32-series development board

The example's `partitions.csv` adds the 128 KiB `log` partition that holds the log. The fault slots
use its last 32 KiB. It also adds a 64 KiB `coredump` partition for the core dump.

## Configuration

`CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES` is enabled by default, so the log captures the
lines that ESP-IDF and its libraries log, as well as the example's own lines. If you disable the
option in menuconfig, nothing wraps ESP-IDF's log. The report then holds only the lines that the
example logs with its `logger`, and the entries that the log adds itself, such as the fault
repeats and the reset reasons.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_persistent_log:crash_report"
cd crash_report
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
