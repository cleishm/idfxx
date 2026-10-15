# idfxx_max31865

MAX31865 RTD-to-digital converter driver for platinum RTDs such as the PT100 and PT1000.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__max31865.html)**

## Features

- One-shot conversions (`measure()`) with automatic bias settling, timed by the DRDY pin or by the
  maximum conversion time
- Continuous conversion at the notch filter rate
- Exact Callendar–Van Dusen conversion between codes, resistance and temperature, including below
  0 °C, with configurable coefficients (IEC 60751 by default)
- Type-safe values using `quanta::electro::milliohms` and `quanta::thermo::millicelsius`
- Fault thresholds set as codes, resistances or temperatures
- Fault detection cycles, automatic or manually timed to suit the input filter
- `verify()` and `restore()` to catch and repair a chip that has reset or taken a corrupted write
- Required configuration fields enforced at compile time
- Dual API: exception-based and result-based error handling

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core, idfxx_gpio, idfxx_spi, and cleishm/quanta (installed automatically)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_max31865:
    version: "^1.0.0"
```

Or add `idfxx_max31865` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/log>
#include <idfxx/max31865>
#include <idfxx/spi/master>

using namespace quanta::electro_literals;
using namespace quanta::thermo_literals;
using namespace std::chrono_literals;

try {
    idfxx::spi::bus_config bus_cfg{};
    bus_cfg.mosi = idfxx::gpio_11;
    bus_cfg.miso = idfxx::gpio_13;
    bus_cfg.sclk = idfxx::gpio_12;
    idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

    idfxx::max31865::device rtd(bus, {
        .cs = idfxx::gpio_10,
        .r0 = 100_Ohm,   // PT100
        .rref = 430_Ohm, // the reference resistor on your board
        .wiring = idfxx::max31865::wiring::three_wire,
        .notch = idfxx::max31865::notch_filter::hz_50,
        .filter_time_constant = 100us,
    });

    // One-shot conversion
    auto s = rtd.measure();
    idfxx::log::info("RTD", "{:b.2f} ({:b.3f})", rtd.to_temperature(s.code), rtd.to_resistance(s.code));

    // Continuous conversion with fault thresholds
    rtd.set_fault_thresholds(-20_c, 150_c);
    rtd.start_continuous();
    // ...later, once the first conversion has completed (about 66 ms at 50 Hz):
    quanta::thermo::millicelsius t = rtd.read_temperature();

} catch (const std::system_error& e) {
    idfxx::log::error("RTD", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/log>
#include <idfxx/max31865>

auto rtd = idfxx::max31865::device::make(bus, {
    .cs = idfxx::gpio_10,
    .r0 = 100_Ohm,
    .rref = 430_Ohm,
    .wiring = idfxx::max31865::wiring::three_wire,
    .notch = idfxx::max31865::notch_filter::hz_50,
    .filter_time_constant = 100us,
});
if (!rtd) {
    idfxx::log::error("RTD", "Failed to create device: {}", rtd.error().message());
    return;
}

auto s = rtd->try_measure();
if (!s) {
    idfxx::log::error("RTD", "Conversion failed: {}", s.error().message());
    return;
}
if (s->fault) {
    auto faults = rtd->try_read_faults();
    if (faults) {
        idfxx::log::warn("RTD", "Faults: {}", *faults);
    }
    (void)rtd->try_clear_faults();
    return;
}
idfxx::log::info("RTD", "{:b.2f}", rtd->to_temperature(s->code));
```

### Watching for a Device Reset

```cpp
// Every few seconds:
if (!rtd.verify()) {
    rtd.restore();
}
```

## API Overview

### Types

- `wiring` - RTD wiring (`two_wire`, `three_wire`, `four_wire`)
- `notch_filter` - Mains-rejection filter (`hz_50`, `hz_60`)
- `cvd_coefficients` - Callendar–Van Dusen coefficients (IEC 60751 defaults)
- `fault` - Fault Status bits, combined as `idfxx::flags<fault>`
- `sample` - A raw conversion result: 15-bit `code` and the `fault` bit
- `device::config` - Device configuration; `r0`, `rref`, `wiring`, `notch` and
  `filter_time_constant` are required

### `device`

**Creation:**
- `device(bus, config)` - Constructor (exception-based, if enabled)
- `make(bus, config)` - Create device (result-based)

**Conversions:**
- `measure()` / `try_measure()` - Blocking one-shot conversion
- `start_continuous()` / `try_start_continuous()` - Start continuous conversion
- `stop_continuous()` / `try_stop_continuous()` - Stop continuous conversion
- `read()` / `try_read()` - Read the latest result
- `read_temperature()` / `try_read_temperature()` - Read the latest result as a temperature

**Bias:**
- `enable_bias()` / `try_enable_bias()` - Keep the RTD bias on between conversions
- `disable_bias()` / `try_disable_bias()` - Turn the RTD bias off

**Unit conversion:**
- `to_resistance(code)`, `to_temperature(code)` - Convert a code
- `to_code(milliohms)`, `to_code(millicelsius)` - Convert to the nearest code (saturating)

**Faults:**
- `read_faults()` / `try_read_faults()` - Read the latched faults
- `clear_faults()` / `try_clear_faults()` - Clear the latched faults
- `detect_faults()` / `try_detect_faults()` - Clear latched faults, then run a fault detection cycle
- `set_fault_thresholds(low, high)` / `try_set_fault_thresholds(low, high)` - Set thresholds as codes,
  resistances or temperatures
- `read_fault_thresholds()` / `try_read_fault_thresholds()` - Read the threshold codes

**Verification:**
- `verify()` / `try_verify()` - Check the chip's configuration and thresholds
- `restore()` / `try_restore()` - Rewrite the configuration and thresholds

**Accessors:**
- `spi()` - The SPI device the driver owns

## Error Handling

- **Dual API Pattern**: The component provides both result-based and exception-based APIs
  - `try_*` methods return `idfxx::result<T>` (always available)
  - Methods without `try_` prefix throw `std::system_error` (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- **Error codes**:
  - `errc::invalid_arg` - Unconnected chip-select pin, clock speed of 0 or above 5 MHz, non-positive
    `r0`/`rref`, negative `filter_time_constant`, or an out-of-range or inverted threshold
  - `errc::not_found` - No chip responded when the device was created
  - `errc::invalid_state` - `measure()` or `disable_bias()` while continuous conversion is running
  - `errc::invalid_response` - `read_temperature()` found the fault bit set
  - `errc::timeout` - DRDY or a fault detection cycle did not complete in time

## Important Notes

- **RREF**: The reference resistor doesn't have to be 4 × R0. Codes are always converted using the
  configured `rref`, so set it to the value fitted on your board (commonly 430 Ω for a PT100 and
  4.3 kΩ for a PT1000).
- **No linear approximation**: The `(code / 32) − 256` shortcut from the datasheet is not used; the
  Callendar–Van Dusen equation is solved exactly, to within 1 m°C, from −200 °C to 850 °C.
- **Notch filter**: Choose the notch for your local mains frequency (50 Hz or 60 Hz). It is fixed when
  the device is created.
- **Filter time constant**: `filter_time_constant` is the R × C of the filter on the RTDIN inputs. It
  sets how long `measure()` and `start_continuous()` wait after the bias is turned on (10.5 time
  constants plus 1 ms) and how long `detect_faults()` waits before a cycle (5 time constants), and
  above 100 µs it switches `detect_faults()` from the automatic cycle to a manually timed one.
- **Shared buses**: SDO stays high-impedance until the data phase of a read, so the MAX31865 can share
  a bus with other devices. ESP-IDF arbitrates the bus between devices for each transaction.
- **CS setup time**: The driver sets the chip-select setup time (400 ns) itself, from the clock speed.
- **Device resets**: A brown-out or reset of the chip silently restores its power-on defaults, and SPI
  has no CRC to catch a corrupted write. Call `verify()` periodically and `restore()` on a mismatch.
- **Thresholds**: Fault thresholds are not preserved across a device reset; `restore()` rewrites them.
- **Continuous start**: `start_continuous()` blocks until the input filter has settled, but not for
  the first conversion, which takes as long as a one-shot conversion; a `read()` immediately after
  `start_continuous()` may return a stale or zero result.
- **Fault detection**: `detect_faults()` clears faults latched earlier before running its cycle.
- **Thread safety**: A device is not thread-safe. Use it from one task, or synchronize access
  externally.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
