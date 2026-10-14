# Button Demo

Detects clicks and long presses on a push button with debouncing.

## What it demonstrates

- Creating an `idfxx::button` from a config with the pin, the pressed level, the internal pull-up and
  an event callback
- Handling each `idfxx::button::event_type` (pressed, released, clicked, long press and extended press)
  in the callback
- The default 50 ms debounce time and 1 second long-press threshold
- Counting events in the callback and logging them from the main task, using
  `idfxx::chrono::tick_clock` and `idfxx::delay()` to watch for 30 seconds

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board
- A push button on GPIO 0

On ESP32, ESP32-S2 and ESP32-S3 development boards, the BOOT button is on GPIO 0, so no extra parts
are needed. On ESP32-C3, ESP32-C6 and ESP32-H2 boards the BOOT button is on GPIO 9: either connect a
push button between GPIO 0 and GND, or change `idfxx::gpio_0` in `main/main.cpp` to `idfxx::gpio_9`.
The example enables the internal pull-up and treats a low level as pressed, so no external resistor is
needed.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_button:button_demo"
cd button_demo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. During the 30 seconds after
startup, press and release the button quickly for a click, or hold it for more than a second for a long
press. The example then logs a summary and stops. Press `Ctrl-]` to exit the monitor.
