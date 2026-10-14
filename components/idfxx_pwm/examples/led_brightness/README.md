# LED Brightness

Steps an LED through a series of brightness levels using PWM.

## What it demonstrates

- Starting a 5 kHz PWM output on a pin with `idfxx::pwm::start()`, which picks a free timer and channel
- Setting the brightness as a fraction of full duty with `set_duty()`
- Reading back the raw duty value with `duty_ticks()`
- Stopping the output automatically when the output object goes out of scope

A comment in `main/main.cpp` also shows how to configure a specific timer and channel yourself.

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
idf.py create-project-from-example "cleishm/idfxx_pwm:led_brightness"
cd led_brightness
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. The LED steps through 0%,
10%, 25%, 50%, 75% and 100% three times, then the output stops. Press `Ctrl-]` to exit the monitor.
