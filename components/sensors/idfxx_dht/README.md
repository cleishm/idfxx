# idfxx_dht

DHT11/DHT22 (AM2301/AM2302) temperature and humidity sensor driver for ESP32 with type-safe C++23
interfaces.

[API documentation](https://cleishm.github.io/idfxx/group__idfxx__dht.html)

## Features

- DHT11 and DHT22/AM2301/AM2302 support on a single GPIO data line
- Reply frames are captured with the RMT peripheral. The driver does not bit-bang the line, so it
  does not depend on interrupt latency and needs no critical section during the 5 ms frame
- Checksum verification and strict frame validation
- Temperature as `quanta::thermo::millicelsius`, humidity in percent
- Enforces the sensor's minimum read interval (1 s DHT11 / 2 s DHT22)
- Dual error handling: exception-based and `result`-based (`try_*`) APIs

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- Components: `idfxx_core`, `idfxx_gpio`, `quanta` (plus ESP-IDF `esp_driver_rmt`)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_dht: "^2.0.0"
```

## Usage

### Exception-based

```cpp
#include <idfxx/dht>

idfxx::dht::sensor sensor({
    .pin = idfxx::gpio_1,
    .model = idfxx::dht::model::dht22,
});

auto r = sensor.read();
// r.temperature (quanta::thermo::millicelsius), r.humidity_pct (float %)
```

### Result-based

```cpp
auto sensor = idfxx::dht::sensor::make({.pin = idfxx::gpio_1});
if (!sensor) { /* handle error */ }

auto r = sensor->try_read();
if (!r) { /* r.error(): timeout / invalid_crc / invalid_state / ... */ }
```

## API Overview

### `idfxx::dht::sensor`

| Method                | Description                                        |
| --------------------- | -------------------------------------------------- |
| `sensor(config)` / `make(config)` | Construct on a data-line GPIO.         |
| `read()` / `try_read()` | Perform one measurement (~25 ms blocking).       |
| `min_read_interval()` | Model's minimum interval between reads.            |
| `pin()`, `model()`    | Configuration accessors.                           |

`config` fields:

- `pin` (required)
- `model` (default `dht22`)
- `internal_pullup` (default `true`). Disable it when the board has an external pull-up.

## Error Handling

- `errc::invalid_state` — called again within `min_read_interval()` of the previous read.
- `errc::timeout` — the sensor did not answer the start pulse (check wiring and the pull-up).
- `errc::invalid_response` — malformed reply frame (noise, wrong model, or a marginal line).
- `errc::invalid_crc` — reply failed its checksum.

## Important Notes

- The data line needs a pull-up. The internal pull-up (~45 kΩ) works for short wires. Use an
  external ~10 kΩ resistor for longer runs.
- `read` blocks for the start pulse plus the reply. This is roughly 25 ms for the DHT11 (most of it
  is the ~20 ms start pulse) and under 10 ms for the DHT22. Call it from a task that can wait that
  long.
- The driver claims one RMT receive channel (64 symbols of channel memory) for the lifetime of the
  `sensor` object.
- A reading can be older than the read call. The sensor measures at most once per
  `min_read_interval()`, and the first read after power-up may report the previous conversion.

## License

Apache-2.0
