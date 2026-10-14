# Producer-Consumer

Shows how to pass data between tasks with `idfxx::queue`.

## What it demonstrates

- Creating an `idfxx::queue` and reading its `size()`, `available()` and `empty()` state
- Sending and receiving items with `send()` and `receive()`
- Putting an item at the head of the queue with `send_to_front()`
- Reading the next item without removing it with `peek()`
- Using a one-item queue as a mailbox with `overwrite()`
- Passing a struct from a producer task to a consumer task, with the consumer waiting for a limited time with `try_receive()`
- Checking `full()`, then emptying the queue with `reset()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_queue:producer_consumer"
cd producer_consumer
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
