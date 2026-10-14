# Task Lifecycle

Creates a worker task and controls it through its whole life, from start to join.

## What it demonstrates

- Reading the current task's name with `idfxx::task::current_name()`
- Creating an `idfxx::task` with a name, stack size and priority
- Inside the task, reading its name and priority, waiting for notifications with `wait()`, and checking `stop_requested()`
- Waking the task with `notify()`
- Changing its priority with `set_priority()`
- Pausing and continuing it with `suspend()` and `resume()`
- Reading its stack high-water mark with `stack_high_water_mark()`
- Stopping it cleanly with `request_stop()` and `join()` with a timeout, then checking `is_completed()`
- Starting a fire-and-forget task with `idfxx::task::spawn()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_task:task_lifecycle"
cd task_lifecycle
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
