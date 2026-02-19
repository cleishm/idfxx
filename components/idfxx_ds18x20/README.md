# idfxx_ds18x20

DS18x20 1-Wire temperature sensor driver.

## Features

- DS18S20, DS1822, DS18B20, and MAX31850 temperature sensor support
- Typed 1-Wire address with family code extraction and hex formatting
- Bus scanning to discover connected devices
- Type-safe temperature values using `thermo::millicelsius`
- Temperature measurement with configurable resolution (9-12 bit)
- Low-level scratchpad read/write/copy access
- Multi-device batch temperature reading
- Dual API: exception-based and result-based error handling

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_ds18x20:
    version: "^0.9.0"
```

Or add `idfxx_ds18x20` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Single Sensor

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/ds18x20>
#include <idfxx/log>

try {
    // For a single sensor, address::any() (default) skips ROM matching
    idfxx::ds18x20::device sensor(idfxx::gpio_4);

    thermo::millicelsius temp = sensor.measure_and_read();
    idfxx::log::info("TEMP", "Temperature: {}", temp);

} catch (const std::system_error& e) {
    idfxx::log::error("TEMP", "Error: {}", e.what());
}
```

### Multiple Sensors

```cpp
#include <idfxx/ds18x20>
#include <idfxx/log>

try {
    // Scan for all sensors on the bus
    auto devices = idfxx::ds18x20::scan_devices(idfxx::gpio_4);
    idfxx::log::info("TEMP", "Found {} sensors", devices.size());

    for (auto& dev : devices) {
        thermo::millicelsius temp = dev.measure_and_read();
        idfxx::log::info("TEMP", "{}: {}", dev.addr(), temp);
    }

} catch (const std::system_error& e) {
    idfxx::log::error("TEMP", "Error: {}", e.what());
}
```

### Configuring Resolution

```cpp
#include <idfxx/ds18x20>

try {
    idfxx::ds18x20::device sensor(idfxx::gpio_4);

    // Set to 9-bit for faster reads (~94ms instead of ~750ms)
    sensor.set_resolution(idfxx::ds18x20::resolution::bits_9);

    // Persist to EEPROM so it survives power cycles
    sensor.copy_scratchpad();

    thermo::millicelsius temp = sensor.measure_and_read();

} catch (const std::system_error& e) {
    // handle error
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/ds18x20>
#include <idfxx/log>

auto dev_result = idfxx::ds18x20::device::make(idfxx::gpio_4);
if (!dev_result) {
    idfxx::log::error("TEMP", "Failed to create device: {}", dev_result.error().message());
    return;
}
auto sensor = *dev_result;

auto temp_result = sensor.try_measure_and_read();
if (!temp_result) {
    idfxx::log::error("TEMP", "Failed to read temperature: {}", temp_result.error().message());
    return;
}

idfxx::log::info("TEMP", "Temperature: {}", *temp_result);
```

### Batch Temperature Reading

```cpp
#include <idfxx/ds18x20>

try {
    auto devices = idfxx::ds18x20::scan_devices(idfxx::gpio_4);

    // Read all sensors
    auto temps = idfxx::ds18x20::measure_and_read_multi(devices);

    for (size_t i = 0; i < devices.size(); ++i) {
        idfxx::log::info("TEMP", "{}: {}", devices[i].addr(), temps[i]);
    }

} catch (const std::system_error& e) {
    // handle error
}
```

## API Overview

### Types

- `address` - Typed 64-bit 1-Wire ROM address with family code extraction
- `family` - Device family enum (ds18s20, ds1822, ds18b20, max31850)
- `resolution` - ADC resolution enum (bits_9, bits_10, bits_11, bits_12)

### `device`

**Creation:**
- `device(pin, addr)` - Constructor (exception-based, if enabled)
- `make(pin, addr)` - Create device (result-based)

**Temperature:**
- `measure(wait)` / `try_measure(wait)` - Initiate conversion
- `read_temperature()` / `try_read_temperature()` - Read last conversion
- `measure_and_read()` / `try_measure_and_read()` - Combined measure and read

**Resolution:**
- `set_resolution(res)` / `try_set_resolution(res)` - Set ADC resolution
- `get_resolution()` / `try_get_resolution()` - Get current resolution

**Scratchpad:**
- `read_scratchpad()` / `try_read_scratchpad()` - Read 9-byte scratchpad
- `write_scratchpad(data)` / `try_write_scratchpad(data)` - Write 3 bytes (TH, TL, config)
- `copy_scratchpad()` / `try_copy_scratchpad()` - Persist to EEPROM

### Free Functions

- `scan_devices(pin)` / `try_scan_devices(pin)` - Discover devices on bus
- `measure_and_read_multi(devices)` / `try_measure_and_read_multi(devices)` - Batch read

## Error Handling

- **Dual API Pattern**: The component provides both result-based and exception-based APIs
  - `try_*` methods return `idfxx::result<T>` (always available)
  - Methods without `try_` prefix throw `std::system_error` (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)

## Important Notes

- **Wiring**: DS18x20 sensors require a 4.7k pull-up resistor on the data line
- **Conversion time**: Temperature measurement takes up to 750ms at 12-bit resolution
- **Single-device buses**: Use `address::any()` (the default) to skip ROM matching
- **Multi-device buses**: Use `scan_devices()` to discover all connected sensors
- **Resolution**: Only DS18B20 supports configurable resolution (9-12 bit); DS18S20 is fixed at 9-bit
- **EEPROM persistence**: Use `copy_scratchpad()` to save resolution and alarm settings across power cycles
- **Lightweight value type**: `device` is copyable with no resource ownership

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
