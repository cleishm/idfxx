# Rotary Encoder Demo

Tracks the position of a rotary encoder, first without acceleration, then with it.

## What it demonstrates

- Creating an `idfxx::rotary_encoder` from a config with the A and B pins and a callback that receives
  each change in position
- The default internal pull-ups on both encoder pins
- Turning on acceleration with `enable_acceleration()`, so fast turns move the position further
- Turning it off again with `disable_acceleration()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board
- A quadrature rotary encoder

Connect the encoder's A output (often labelled CLK) to GPIO 4, its B output (often labelled DT) to
GPIO 5, and its common pin to GND. The example enables the internal pull-ups on both pins; external
pull-up resistors to 3.3 V give more reliable readings. If the encoder turns the wrong way, swap the
two pins. To use different pins, change `pin_a` and `pin_b` in `main/main.cpp`.

> [!NOTE]
> GPIO 5 is a strapping pin on the ESP32, and GPIO 4 and GPIO 5 are strapping pins on the ESP32-C6.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_rotary_encoder:encoder_demo"
cd encoder_demo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Turn the encoder and watch
the position change. The example runs for 15 seconds without acceleration, 15 seconds with it, then
10 seconds without it, and logs the final position. Press `Ctrl-]` to exit the monitor.
