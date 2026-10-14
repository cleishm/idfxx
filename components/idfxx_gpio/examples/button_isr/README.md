# Button ISR

Counts button presses with a GPIO interrupt, showing how to use `idfxx::gpio` pins for input and
interrupt handling.

## What it demonstrates

- Pin constants (`idfxx::gpio_0`, `idfxx::gpio_9`) chosen by target, and printing pins
- Setting the pin direction to input and enabling the internal pull-up
- Installing the GPIO interrupt service with `gpio::install_isr_service()`
- Triggering on a falling edge with `set_intr_type()`
- Adding an interrupt handler with `isr_handler_add()`, held in a `gpio::unique_isr_handle` that removes
  it automatically
- Enabling and disabling the pin's interrupt with `intr_enable()` and `intr_disable()`
- Watching for presses for 30 seconds with `idfxx::chrono::tick_clock` and `idfxx::delay()`
- Cleaning up in order: removing the handler, uninstalling the service with
  `gpio::uninstall_isr_service()`, and returning the pin to its default state with `reset()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

The example uses the board's BOOT button: GPIO 9 on the ESP32-C3, ESP32-C6 and ESP32-H2, and GPIO 0
on the others, so no extra parts are needed. To use a different button, connect it between a pin and
GND and change `button_pin` in `main/main.cpp`. The example enables the internal pull-up, so no
external resistor is needed.

The example doesn't debounce the button, so a single press can be counted more than once.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_gpio:button_isr"
cd button_isr
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press the button during the
30 seconds after startup; the example logs the running count, then cleans up and stops. Press `Ctrl-]`
to exit the monitor.
