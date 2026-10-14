# DS18x20 Temperature Sensor

Finds DS18x20 temperature sensors on a 1-Wire bus and reads their temperatures.

## What it demonstrates

- Finding the sensors on a bus with `idfxx::ds18x20::scan_devices()`
- Printing each sensor's address and family
- Reading one sensor at a time with `measure_and_read()`
- Reading all sensors together with `idfxx::ds18x20::measure_and_read_multi()`
- Reading and changing a DS18B20's resolution with `get_resolution()` and `set_resolution()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board
- One or more DS18x20-family sensors (DS18B20, DS18S20, DS1822 or MAX31850)
- A 4.7 kΩ resistor

Connect each sensor's VDD to 3.3 V, GND to GND, and data pin (DQ) to GPIO 4. 1-Wire needs a pull-up:
fit the 4.7 kΩ resistor between the data line and 3.3 V. Several sensors can share the same three
wires. To use a different pin, change `sensor_pin` in `main/main.cpp`.

> [!NOTE]
> GPIO 4 is a strapping pin on the ESP32-C6.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_ds18x20:temperature_sensor"
cd temperature_sensor
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. The example runs once: it
lists the sensors it finds, reads them, tries 10-bit resolution on each DS18B20 and sets it back to
12-bit. Press `Ctrl-]` to exit the monitor.
