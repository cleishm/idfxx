# Event Loop Demo

Shows how to define your own events and post and handle them on idfxx event loops.

## What it demonstrates

- Defining an event base with `IDFXX_EVENT_DEFINE_BASE`, and typed events with `idfxx::event`, with or without data
- Creating an `idfxx::event_loop` that runs its own task
- Adding a listener for one event, and a listener for every event from a base, with `listener_add()`
- Posting events, with and without data, with `post()`
- Removing listeners with `listener_remove()`
- Removing a listener automatically at the end of a scope with `idfxx::event_loop::unique_listener_handle`
- Creating an `idfxx::user_event_loop` and dispatching its events from your own code with `run()`
- Creating, using and destroying the system event loop with `create_system()`, `system()` and `destroy_system()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_event:event_loop_demo"
cd event_loop_demo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
