# idfxx_onewire

1-Wire bus protocol driver.

## Features

- Typed 64-bit device address with family code extraction and hex formatting
- Bus reset, ROM select, and skip ROM commands
- Single-byte and multi-byte read/write operations
- Parasitic power control for battery-less devices
- Device search with optional family code filtering
- Thread-safe bus access (Lockable interface)
- CRC8 and CRC16 utility functions
- Dual API: exception-based and result-based error handling

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_onewire:
    version: "^0.9.0"
```

Or add `idfxx_onewire` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Bus Operations

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/onewire>
#include <idfxx/log>

try {
    idfxx::onewire::bus bus(idfxx::gpio_4);

    if (bus.reset()) {
        bus.skip_rom();
        bus.write(0x44);  // e.g., DS18B20 Convert T command
    }

} catch (const std::system_error& e) {
    idfxx::log::error("OW", "Error: {}", e.what());
}
```

### Device Search

```cpp
#include <idfxx/onewire>
#include <idfxx/log>

try {
    idfxx::onewire::bus bus(idfxx::gpio_4);

    // Find all devices
    auto devices = bus.search();
    idfxx::log::info("OW", "Found {} devices", devices.size());

    for (auto& addr : devices) {
        idfxx::log::info("OW", "  {}", addr);
    }

    // Find only DS18B20 devices (family code 0x28)
    auto sensors = bus.search(0x28);

} catch (const std::system_error& e) {
    idfxx::log::error("OW", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/onewire>
#include <idfxx/log>

auto bus_result = idfxx::onewire::bus::make(idfxx::gpio_4);
if (!bus_result) {
    idfxx::log::error("OW", "Failed to create bus: {}", bus_result.error().message());
    return;
}
auto& bus = **bus_result;

if (bus.reset()) {
    auto result = bus.try_skip_rom();
    if (!result) {
        idfxx::log::error("OW", "Skip ROM failed: {}", result.error().message());
        return;
    }
    auto write_result = bus.try_write(0x44);
    if (!write_result) {
        idfxx::log::error("OW", "Write failed: {}", write_result.error().message());
    }
}
```

### Thread-Safe Access

The bus satisfies the C++ Lockable named requirement:

```cpp
#include <idfxx/onewire>
#include <mutex>

idfxx::onewire::bus bus(idfxx::gpio_4);

{
    std::lock_guard<idfxx::onewire::bus> lock(bus);
    // Exclusive access to the bus
    bus.reset();
    bus.skip_rom();
    bus.write(0x44);
}
```

## API Overview

### Types

- `address` - Typed 64-bit 1-Wire ROM address with family code extraction
- `bus` - 1-Wire bus controller with thread-safe access

### `address`

- `address()` - Default construction (equivalent to `any()`)
- `address(uint64_t)` - Construction from raw 64-bit value
- `any()` - Wildcard address for single-device buses
- `none()` - Invalid sentinel address
- `raw()` - Underlying 64-bit value
- `family()` - Family code (low byte)

### `bus`

**Creation:**
- `bus(pin)` - Constructor (exception-based, if enabled)
- `make(pin)` - Create bus (result-based)

**Bus Control:**
- `reset()` - 1-Wire reset cycle (returns true if device present)

**ROM Commands:**
- `select(addr)` / `try_select(addr)` - Select specific device
- `skip_rom()` / `try_skip_rom()` - Select all devices

**Data Transfer:**
- `write(byte)` / `try_write(byte)` - Write single byte
- `write(data)` / `try_write(data)` - Write multiple bytes
- `read()` / `try_read()` - Read single byte
- `read(buf)` / `try_read(buf)` - Read multiple bytes

**Power:**
- `power()` / `try_power()` - Drive bus high for parasitic power
- `depower()` - Release bus (always succeeds)

**Search:**
- `search(max)` / `try_search(max)` - Find all devices
- `search(family, max)` / `try_search(family, max)` - Find by family code

### Free Functions

- `crc8(data)` - Compute 8-bit CRC
- `crc16(data, iv)` - Compute 16-bit CRC
- `check_crc16(data, crc, iv)` - Verify 16-bit CRC

## Error Handling

- **Dual API Pattern**: The component provides both result-based and exception-based APIs
  - `try_*` methods return `idfxx::result<T>` (always available)
  - Methods without `try_` prefix throw `std::system_error` (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)

## Important Notes

- **Wiring**: 1-Wire devices require a 4.7k pull-up resistor on the data line
- **Reset first**: Always call `reset()` before issuing ROM commands
- **Single-device buses**: Use `address::any()` or `skip_rom()` to skip ROM matching
- **Parasitic power**: Call `power()` after write commands when devices need extra power; `reset()` automatically depowers
- **Thread safety**: All bus operations acquire the internal mutex; use `std::lock_guard` for multi-operation sequences

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
