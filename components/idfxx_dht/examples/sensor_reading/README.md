# DHT Sensor Reading

Reads temperature and humidity from a DHT22 sensor every few seconds.

## What it demonstrates

- Creating an `idfxx::dht::sensor` from a config with the data pin and the sensor model
- Printing the sensor's model and pin
- Waiting the sensor's minimum time between reads, from `min_read_interval()`
- Reading temperature and relative humidity with `try_read()`, logging a warning and carrying on if a
  read fails

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`),
with `try_read()` where it handles a failed read and keeps going.

## Hardware required

- Any ESP32-series development board
- A DHT22 (AM2302) sensor, or a DHT11

Connect the sensor's VCC to 3.3 V, GND to GND, and its data pin to GPIO 1. The example enables the
internal pull-up on the data line, which is enough for short wires; for longer wires, add a ~10 kΩ
pull-up resistor from the data line to 3.3 V. To use a different pin, change `sensor_pin` in
`main/main.cpp`. For a DHT11, change `idfxx::dht::model::dht22` to `idfxx::dht::model::dht11`.

> [!NOTE]
> On the original ESP32, GPIO 1 is the console UART's TX pin. Choose another pin, such as GPIO 4.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_dht:sensor_reading"
cd sensor_reading
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the
monitor.
