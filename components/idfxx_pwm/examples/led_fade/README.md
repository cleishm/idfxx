# LED Fade

Fades an LED up and down in a breathing pattern using hardware PWM fades.

## What it demonstrates

- Starting a 5 kHz PWM output on a pin with `idfxx::pwm::start()`, which picks a free timer and channel
- Installing the fade service with `idfxx::pwm::output::install_fade_service()`
- Fading to a duty cycle over a set time with `fade_to()`, waiting for each fade to finish with
  `idfxx::pwm::fade_mode::wait_done`
- Starting a fade that runs in the background while the program carries on
- Reading the current duty cycle with `duty()`
- Uninstalling the fade service with `uninstall_fade_service()`, and stopping the output when the
  output object goes out of scope

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
idf.py create-project-from-example "cleishm/idfxx_pwm:led_fade"
cd led_fade
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. The LED breathes five times
(one second up, one second down), fades to half brightness in the background, then the output stops.
Press `Ctrl-]` to exit the monitor.
