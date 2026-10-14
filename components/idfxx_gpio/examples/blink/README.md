# Blink

Blinks an LED, showing how to use `idfxx::gpio` pins for output.

## What it demonstrates

- Pin constants (`idfxx::gpio_2`, `idfxx::gpio_nc`), printing pins, and checking whether a pin is connected
- Setting the pin direction, then setting and reading the output level
- Inverting a level with `operator~`, and toggling the output with `toggle_level()`
- Reading and setting the drive capability
- Configuring several pins at once with `idfxx::configure_gpios()`
- Blinking at a steady rate with `idfxx::delay_until()`, so timing errors don't add up
- Returning the pin to its default state with `reset()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board
- An LED on GPIO 2

Many ESP32 development boards have an LED on GPIO 2. If yours doesn't, connect an LED in series with a
~330 Ω resistor between GPIO 2 and GND. To use a different pin, change `idfxx::gpio_2` in
`main/main.cpp`.

> [!NOTE]
> The ESP32-S3, ESP32-C3 and ESP32-C6 DevKits have an addressable RGB LED, which this example can't
> drive. Connect a plain LED instead.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_gpio:blink"
cd blink
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
