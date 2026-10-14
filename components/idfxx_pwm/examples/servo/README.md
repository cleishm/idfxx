# Servo

Sweeps a hobby servo from 0 to 180 degrees and back, then centers it.

## What it demonstrates

- Starting a 50 Hz PWM output with 14-bit duty resolution using `idfxx::pwm::start()`, which picks a
  free timer and channel
- Setting the pulse width directly as a `std::chrono` duration with `set_pulse_width()`, mapping each
  angle to a 1000–2000 µs pulse

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board
- A standard hobby servo, with its signal wire on GPIO 18

Connect the servo's signal wire to GPIO 18. Power the servo from 5 V rather than from a GPIO or the
board's 3.3 V pin, and connect the servo's ground to the board's GND. To use a different pin, change
`idfxx::gpio_18` in `main/main.cpp`.

The example assumes 1000 µs is 0 degrees and 2000 µs is 180 degrees. If your servo uses a different
range, change `min_pulse` and `max_pulse` in `main/main.cpp`.

> [!NOTE]
> On the ESP32-C3, GPIO 18 is a USB-JTAG pin, and using it disables USB-JTAG. On the ESP32-H2,
> GPIO 18 is connected to the SPI flash. Choose another pin on these chips.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_pwm:servo"
cd servo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. The servo sweeps once in
10-degree steps and stops at 90 degrees. Press `Ctrl-]` to exit the monitor.
