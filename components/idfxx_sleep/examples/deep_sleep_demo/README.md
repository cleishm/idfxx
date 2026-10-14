# Deep Sleep Demo

Puts the chip into deep sleep and wakes it with a button or a 30-second timer, reporting the wake-up
cause on each boot.

## What it demonstrates

- Keeping a boot counter in RTC memory (`RTC_DATA_ATTR`), which survives deep sleep
- Finding out why the chip started with `idfxx::sleep::wakeup_cause()`
- Finding out which pin woke the chip with `idfxx::sleep::ext1_wakeup_status()`
- Entering deep sleep with `idfxx::sleep::deep_sleep()`, armed with two wake-up sources: a pin and a
  timer (`idfxx::sleep::timer_wake`)
- Choosing the pin wake-up source each chip supports: `idfxx::sleep::ext1_wake` on chips with EXT1
  wake-up, and `idfxx::sleep::deep_sleep_gpio_wake` on the ESP32-C3
- Selecting the EXT1 low-level trigger by target: `ext1_mode::all_low` on the ESP32, `ext1_mode::any_low`
  on the others

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board
- A push button on GPIO 0 that pulls the pin low when pressed

On ESP32, ESP32-S2 and ESP32-S3 development boards, the BOOT button is on GPIO 0 and has an external
pull-up, so no extra parts are needed. The example doesn't enable a pull-up for deep sleep, so on
other chips you need to wire a button yourself:

- **ESP32-C3:** connect a push button between GPIO 0 and GND. The pin is pulled up while the chip
  sleeps. (The BOOT button is on GPIO 9, which can't wake the ESP32-C3 from deep sleep.)
- **ESP32-C6:** connect a push button between GPIO 0 and GND, and a ~10 kΩ pull-up resistor from
  GPIO 0 to 3.3 V. (The BOOT button is on GPIO 9, which can't wake the ESP32-C6 from deep sleep.)
- **ESP32-H2:** GPIO 0 can't wake the ESP32-H2 from deep sleep. Change `WAKE_PIN` in `main/main.cpp`
  to one of GPIO 7–14, and connect a push button from that pin to GND with a ~10 kΩ pull-up resistor
  to 3.3 V.

To use a different pin, change `WAKE_PIN` in `main/main.cpp`. To change the timer, change
`SLEEP_INTERVAL`.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_sleep:deep_sleep_demo"
cd deep_sleep_demo
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the
monitor.

After a cold boot, the chip reports the boot and goes to deep sleep. Press the button to wake it, or
wait 30 seconds for the timer. Waking from deep sleep restarts the application, so each wake prints
the boot count and what woke the chip, then sleeps again. If your board connects to the computer
through the chip's own USB port rather than a USB-to-UART bridge, the serial connection drops while
the chip sleeps.
