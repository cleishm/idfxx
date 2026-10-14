# Event Group Sync

Shows how to signal and synchronize tasks with `idfxx::event_group`.

## What it demonstrates

- Defining event bits as a scoped enum with `idfxx::enable_flags_operators`
- Setting, clearing and reading bits with `set()`, `clear()` and `get()`
- Waiting for any of several bits with `wait()` and `idfxx::wait_mode::any`, while another task sets them
- Waiting for all of several bits with `wait()` and `idfxx::wait_mode::all`
- Making two tasks meet at a rendezvous point with `sync()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_event_group:event_group_sync"
cd event_group_sync
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
