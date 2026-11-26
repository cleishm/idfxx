# idfxx_adc

One-shot and continuous ADC reads with calibrated voltages for ESP32, with type-safe C++23
interfaces.

[API documentation](https://cleishm.github.io/idfxx/group__idfxx__adc.html)

## Features

- One-shot reads (`adc::input`): on-demand conversions. Typical uses are a battery voltage or a
  sensor divider.
- Continuous sampling (`adc::sampler`): fixed-rate background (DMA) sampling of one or more pins.
  The results are parsed samples, each tagged with its pin. The sampler counts overruns.
- Single-call setup: ADC unit and channel resolved from the GPIO pin
- Typed voltage readings (`quanta::electro::millivolts`) using the chip's factory calibration when
  available
- Attenuation selection across the full input range
- Dual error handling: exception-based and `result`-based (`try_*`) APIs

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- Components: `idfxx_core`, `idfxx_gpio`, `quanta` (plus ESP-IDF `esp_adc`)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_adc: "^2.1.0"
```

## Usage

### Exception-based

```cpp
#include <idfxx/adc>

// One-shot: read a battery behind a 2:1 divider on demand.
idfxx::adc::input battery({.pin = idfxx::gpio_3, .divider = {2, 1}});
quanta::electro::millivolts v = battery.read_voltage();  // battery-side voltage
```

To get both the pin and source voltage from a single sample, read the pin voltage and convert it:

```cpp
quanta::electro::millivolts at_pin = battery.read_pin_voltage();
quanta::electro::millivolts at_source = battery.divider().source_voltage(at_pin);
```

```cpp
using namespace quanta::frequency_literals;

// Continuous: sample a signal at 20 kHz in the background.
idfxx::adc::sampler mic({.pins = std::vector<idfxx::gpio>{idfxx::gpio_3}, .sample_rate = 20_kHz});
mic.start();

std::array<idfxx::adc::sampler::sample, 256> buf;
while (true) {
    size_t n = mic.read(buf);
    for (size_t i = 0; i < n; ++i) {
        quanta::electro::millivolts v = mic.to_voltage(buf[i]);
        // ...
    }
}
```

For a single pin, read straight into calibrated voltages:

```cpp
std::array<quanta::electro::millivolts, 256> volts;
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
auto mic = idfxx::adc::sampler::make({
    .pins = std::vector<idfxx::gpio>{idfxx::gpio_3},
    .sample_rate = quanta::frequency::hertz{20'000},
});
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
| `read_voltage()` / `try_read_voltage()` | Calibrated voltage, scaled by the configured divider (`quanta::electro::millivolts`). |
| `read_pin_voltage()` / `try_read_pin_voltage()` | Calibrated voltage at the pin, not scaled by the divider. |
| `read_raw()` / `try_read_raw()` | Raw conversion value. |
| `calibrated()` | Whether factory calibration is active. |

`config` fields:

- `pin`: required, ADC-capable.
- `attenuation`: default `db_12` (full range).
- `divider`: external divider ratio, default 1:1. `read_voltage` scales by it to report the source
  voltage. It does not affect `read_pin_voltage` and `read_raw`.

`divider_ratio::source_voltage(pin)` converts a pin voltage to the source voltage.

### `idfxx::adc::sampler`

| Method | Description |
| ------ | ----------- |
| `sampler(config)` / `make(config)` | Claim ADC1 and configure round-robin conversion of the pins. |
| `start()` / `try_start()` | Begin conversion and reset the overrun count. |
| `stop()` / `try_stop()` | Stop conversion (idempotent). |
| `read(out[, timeout])` / `try_read(out[, timeout])` | Block until at least one parsed sample is available (`out` is a span of `sample`). Returns the count written. |
| `read(mv[, timeout])` / `try_read(mv[, timeout])` | Single-pin convenience: read and convert to voltages in one call (`out` is a span of `quanta::electro::millivolts`). |
| `to_voltage(sample)` / `try_to_voltage(sample)` | Calibrated voltage for one sample (`quanta::electro::millivolts`). |
| `to_voltage(raw)` / `try_to_voltage(raw)` | Calibrated voltage for a raw value (single-pin samplers only). |
| `to_voltage(in, out)` / `try_to_voltage(in, out)` | Convert a batch of samples to voltages, preserving pin association. |
| `pins()` / `sample_rate()` / `running()` | Configuration and state accessors. |
| `calibrated()` | Whether factory calibration is active for every pin. |
| `overruns()` | Dropped-data events since the last start. |

`config` fields:

- `pins`: required, ADC1-capable, no duplicates.
- `attenuation`: default `db_12`, uniform across pins.
- `sample_rate`: default 20 kHz, total across pins.
- `frame_samples`: default 256, the read granularity.
- `buffer_samples`: default 1024, the internal pool capacity.

## Error Handling

- `errc::invalid_arg` — pin not connected or not ADC-capable, or a non-positive divider ratio. The
  sampler also returns it for:
  - empty, duplicate or non-ADC1 pins
  - bad frame or buffer sizes
  - a non-positive sample rate
  - an empty read destination
  - a batch `try_to_voltage` output span that is smaller than its input
  - `try_to_voltage` with a pin that the sampler was not configured with
- `errc::invalid_state` — sampler operation in the wrong state (e.g. `try_read` before `try_start`,
  double start).
- `errc::timeout` — no sample arrived within the sampler read timeout.
- `errc::not_supported` — voltage conversion with no usable factory calibration. Check
  `calibrated()`, and use raw values when it is false.

## Important Notes

- Each `input` claims its pin's entire ADC unit. Two inputs on the same unit cannot currently
  coexist. This component intentionally covers only the single-input case.
- **The sampler is ADC1 only.** The digital (continuous) controller serves only ADC1 on the
  supported targets. Pins that map to ADC2 are rejected.
- **One-shot and continuous reads exclude each other on ADC1.** While a sampler is running, `input`
  reads on ADC1 fail with a timeout, because the driver serializes access to the unit. Stop the
  sampler first.
- **Sampler attenuation is uniform.** The hardware applies one attenuation per ADC unit in
  continuous mode, so all pins share `config.attenuation`.
- **Sample-rate range is chip-specific.** ESP32: 20 kHz – 2 MHz. Most other targets (S3/C3/C6/H2):
  611 Hz – 83.3 kHz. Rates outside the range fail at construction. `sample_rate` is the *total*
  conversion rate. Each pin samples at `sample_rate / pins.size()`.
- **Overruns drop data but do not cause errors.** If reads are too slow and the sampler's internal
  pool overflows, the driver discards samples. `try_read` still returns valid data, and
  `overruns()` counts the drop events. Choose `buffer_samples` and the read interval so that the
  pool never fills.
- **The sampler expects a single reader and is not ISR-safe.** Call `read`/`try_read` from one task
  only, and never from an interrupt handler.
- ADC2 is shared with Wi-Fi on most chips. Prefer ADC1 pins when Wi-Fi is active.
- For battery sensing above the input range, use a resistor divider and set `config::divider` so
  that `read_voltage` reports the source voltage.

## License

Apache-2.0
