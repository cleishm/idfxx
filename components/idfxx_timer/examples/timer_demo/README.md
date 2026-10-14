# Timer Demo

Shows how to run callbacks after a delay or at a fixed interval with `idfxx::timer`.

## What it demonstrates

- Reading the time since boot with `idfxx::timer::clock`
- Starting a one-shot timer with `start_once()` and checking it with `is_active()`
- Starting a periodic timer with `start_periodic()`, reading its `period()`, and stopping it with `stop()`
- Changing a running timer's timeout with `restart()`
- Creating and starting a timer in one call with the static `idfxx::timer::start_once()` and `idfxx::timer::start_periodic()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_timer:timer_demo"
cd timer_demo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
