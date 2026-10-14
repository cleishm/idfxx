# Buzzer

Plays a C major scale on a passive buzzer, setting each note's pitch by changing the PWM frequency.

## What it demonstrates

- Note frequencies as typed values (`262_Hz`)
- Configuring a specific PWM timer (`idfxx::pwm::timer_0`) with a frequency and duty resolution
- Starting an output on a pin with a chosen timer and channel and a 50% duty cycle, using
  `idfxx::pwm::start()`
- Changing the pitch with the timer's `set_frequency()`
- Silencing the output by setting the duty cycle to 0 with `set_duty()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board
- A passive buzzer on GPIO 18

Connect the buzzer's positive lead to GPIO 18 and its negative lead to GND. Use a passive buzzer: an
active buzzer has its own oscillator and sounds at one fixed pitch. To use a different pin, change
`idfxx::gpio_18` in `main/main.cpp`.

> [!NOTE]
> On the ESP32-C3, GPIO 18 is a USB-JTAG pin, and using it disables USB-JTAG. On the ESP32-H2,
> GPIO 18 is connected to the SPI flash. Choose another pin on these chips.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_pwm:buzzer"
cd buzzer
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. The scale plays once at
startup. Press `Ctrl-]` to exit the monitor.
