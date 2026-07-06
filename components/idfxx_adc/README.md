# idfxx_adc

One-shot and continuous ADC reads with calibrated voltages for ESP32, with type-safe C++23 interfaces.

[API documentation](https://cleishm.github.io/idfxx/group__idfxx__adc.html)

## Features

- One-shot reads (`adc::input`): on-demand conversions — the battery-voltage /
  sensor-divider case
- Continuous sampling (`adc::sampler`): fixed-rate background (DMA) sampling of
  one or more pins, delivered as parsed per-pin samples with overrun accounting
- Single-call setup: ADC unit and channel resolved from the GPIO pin
- Typed voltage readings (`electro::millivolts`) using the chip's factory
  calibration when available
- Attenuation selection for the full input-range spectrum
- Dual error handling: exception-based and `result`-based (`try_*`) APIs

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- Components: `idfxx_core`, `idfxx_gpio`, `electro`, `frequency` (plus
  ESP-IDF `esp_adc`)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_adc: "^1.0.0"
```

## Usage

### Exception-based

```cpp
#include <idfxx/adc>

// One-shot: read a battery divider on demand.
idfxx::adc::input battery({.pin = idfxx::gpio_3});
electro::millivolts v = battery.read_voltage();
```

```cpp
using namespace frequency_literals;

// Continuous: sample a signal at 20 kHz in the background.
idfxx::adc::sampler mic({.pins = {idfxx::gpio_3}, .sample_rate = 20_kHz});
mic.start();

std::array<idfxx::adc::sampler::sample, 256> buf;
while (true) {
    size_t n = mic.read(buf);
    for (size_t i = 0; i < n; ++i) {
        electro::millivolts v = mic.to_voltage(buf[i]);
        // ...
    }
}
```

For a single pin, read straight into calibrated voltages:

```cpp
std::array<electro::millivolts, 256> volts;
size_t n = mic.read(volts);  // reads and converts in one call
```

### Result-based

```cpp
auto battery = idfxx::adc::input::make({.pin = idfxx::gpio_3});
if (!battery) { /* handle error */ }

auto v = battery->try_read_voltage();
if (!v) { /* v.error() */ }
```

```cpp
auto mic = idfxx::adc::sampler::make({.pins = {idfxx::gpio_3}, .sample_rate = freq::hertz{20'000}});
if (!mic) { /* handle error */ }

if (auto r = mic->try_start(); !r) { /* r.error() */ }

std::array<idfxx::adc::sampler::sample, 256> buf;
auto n = mic->try_read(buf, std::chrono::milliseconds{100});
if (!n) { /* n.error() */ }
```

## API Overview

### `idfxx::adc::input`

| Method | Description |
| ------ | ----------- |
| `input(config)` / `make(config)` | Claim the pin's ADC unit and configure the channel. |
| `read_voltage()` / `try_read_voltage()` | Calibrated voltage at the pin (`electro::millivolts`). |
| `read_raw()` / `try_read_raw()` | Raw conversion value. |
| `calibrated()` | Whether factory calibration is active. |

`config`: `pin` (required, ADC-capable), `attenuation` (default `db_12`,
full range).

### `idfxx::adc::sampler`

| Method | Description |
| ------ | ----------- |
| `sampler(config)` / `make(config)` | Claim ADC1 and configure round-robin conversion of the pins. |
| `start()` / `try_start()` | Begin conversion; resets the overrun counter. |
| `stop()` / `try_stop()` | Stop conversion (idempotent). |
| `read(out[, timeout])` / `try_read(out[, timeout])` | Block for at least one parsed sample (`out` a span of `sample`); returns the count written. |
| `read(mv[, timeout])` / `try_read(mv[, timeout])` | Single-pin convenience: read and convert to voltages in one call (`out` a span of `electro::millivolts`). |
| `to_voltage(sample)` / `try_to_voltage(sample)` | Calibrated voltage for one sample (`electro::millivolts`). |
| `to_voltage(in, out)` / `try_to_voltage(in, out)` | Convert a batch of samples to voltages, preserving pin association. |
| `pins()` / `sample_rate()` / `running()` | Configuration and state accessors. |
| `calibrated()` | Whether factory calibration is active for every pin. |
| `overruns()` | Dropped-data events since the last start. |

`config`: `pins` (required, ADC1-capable, no duplicates), `attenuation`
(default `db_12`, uniform across pins), `sample_rate` (default 20 kHz, total
across pins), `frame_samples` (default 256, read granularity),
`buffer_samples` (default 1024, internal pool capacity).

## Error Handling

- `errc::invalid_arg` — pin not connected or not ADC-capable; for the
  sampler also: empty/duplicate or non-ADC1 pins, bad frame/buffer sizes,
  non-positive sample rate, empty read destination, a batch `try_to_voltage`
  output span smaller than its input, or `try_to_voltage` with a pin the
  sampler was not configured with.
- `errc::invalid_state` — sampler operation in the wrong state (e.g.
  `try_read` before `try_start`, double start).
- `errc::timeout` — no sample arrived within the sampler read timeout.
- `errc::not_supported` — voltage conversion with no usable factory
  calibration (fall back to raw values; check `calibrated()`).

## Important Notes

- Each `input` claims its pin's entire ADC unit; two inputs on the same
  unit cannot currently coexist. This component intentionally covers only
  the single-input case.
- **The sampler is ADC1 only.** The digital (continuous) controller serves
  only ADC1 on the supported targets; pins that map to ADC2 are rejected.
- **One-shot and continuous reads exclude each other on ADC1.** While a
  sampler is running, `input` reads on ADC1 fail with a timeout — the
  driver serializes access to the unit. Stop the sampler first.
- **Sampler attenuation is uniform.** The hardware applies one attenuation
  per ADC unit in continuous mode, so all pins share `config.attenuation`.
- **Sample-rate range is chip-specific.** ESP32: 20 kHz – 2 MHz; most other
  targets (S3/C3/C6/H2): 611 Hz – 83.3 kHz. Rates outside the range fail at
  construction. `sample_rate` is the *total* conversion rate — each pin
  samples at `sample_rate / pins.size()`.
- **Overruns drop data, not errors.** If reads don't keep up and the
  sampler's internal pool overflows, the driver discards samples;
  `try_read` still returns valid data and `overruns()` counts the drop
  events. Size `buffer_samples` and read cadence so the pool never fills.
- **The sampler expects a single reader and is not ISR-safe.** Call
  `read`/`try_read` from one task only, and never from an interrupt
  handler.
- ADC2 is shared with Wi-Fi on most chips — prefer ADC1 pins when Wi-Fi is
  active.
- For battery sensing above the input range, use a resistor divider and
  scale the reading in the application.

## License

Apache-2.0
