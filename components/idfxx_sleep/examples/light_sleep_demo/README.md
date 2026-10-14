# Light Sleep Demo

Light-sleeps in a loop, waking on a 10-second timer or a button press, and reports what woke it.

## What it demonstrates

- Setting up the wake pin as an input with the internal pull-up
- Entering light sleep with `idfxx::sleep::light_sleep()`, armed with a timer
  (`idfxx::sleep::timer_wake`) and a low level on a pin (`idfxx::sleep::gpio_wake`)
- Using the wake-up cause that `light_sleep()` returns
- Waiting for the button to be released with `get_level()` and `idfxx::delay()`, because a held button
  would wake the next sleep straight away

Execution carries on after each wake, with all state kept.

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board
- A push button on GPIO 0

On ESP32, ESP32-S2 and ESP32-S3 development boards, the BOOT button is on GPIO 0, so no extra parts
are needed. On ESP32-C3, ESP32-C6 and ESP32-H2 boards the BOOT button is on GPIO 9: either connect a
push button between GPIO 0 and GND, or change `WAKE_PIN` in `main/main.cpp` to `idfxx::gpio_9`. The
example enables the internal pull-up, so no external resistor is needed. To change the timer, change
`SLEEP_INTERVAL`.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_sleep:light_sleep_demo"
cd light_sleep_demo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the
monitor.

Each cycle, the chip light-sleeps until 10 seconds pass or you press the button, then logs which one
woke it and starts the next cycle.
