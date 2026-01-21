# idfxx_i2c

Type-safe I2C master bus driver for ESP32.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__i2c.html)**

## Features

- I2C bus and device lifecycle management
- Thread-safe bus access with `Lockable` interface
- Device scanning and probing
- Register-based read/write operations (8-bit and 16-bit addressing)
- Raw transmit/receive operations
- `std::chrono` timeout support

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- `idfxx_core` component (error handling)
- `idfxx_gpio` component (pin configuration)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_i2c:
    version: "^0.9.0"
```

Or add `idfxx_i2c` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/i2c/master>
#include <idfxx/log>

using namespace frequency_literals;

try {
    // Create I2C bus
    auto bus = std::make_shared<idfxx::i2c::master_bus>(
        idfxx::i2c::port::i2c0,  // I2C port
        idfxx::gpio_21,          // SDA pin
        idfxx::gpio_22,          // SCL pin
        100_kHz                  // Frequency
    );

    // Create device on the bus
    auto device = std::make_unique<idfxx::i2c::master_device>(
        bus,     // Shared pointer to bus
        0x3C     // 7-bit device address
    );

    // Read from register (throws on error)
    auto data = device->read_register(0x00, 1);
    idfxx::log::info("I2C", "Register value: 0x{:02X}", data[0]);

} catch (const std::system_error& e) {
    idfxx::log::error("I2C", "I2C error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/i2c/master>
#include <idfxx/log>

using namespace frequency_literals;

// Create I2C bus
auto bus_result = idfxx::i2c::master_bus::make(
    idfxx::i2c::port::i2c0,  // I2C port
    idfxx::gpio_21,          // SDA pin
    idfxx::gpio_22,          // SCL pin
    100_kHz                  // Frequency
);

if (!bus_result) {
    idfxx::log::error("I2C", "Failed to create bus: {}", bus_result.error().message());
    return;
}
auto bus = std::move(*bus_result);

// Create device on the bus
auto dev_result = idfxx::i2c::master_device::make(
    bus,     // Shared pointer to bus
    0x3C     // 7-bit device address
);

if (!dev_result) {
    idfxx::log::error("I2C", "Failed to create device: {}", dev_result.error().message());
    return;
}
auto device = std::move(*dev_result);

// Read from register
auto data_result = device->try_read_register(0x00, 1);
if (data_result) {
    idfxx::log::info("I2C", "Register value: 0x{:02X}", (*data_result)[0]);
} else {
    idfxx::log::error("I2C", "Read failed: {}", data_result.error().message());
}
```

### Scanning for Devices

```cpp
using namespace frequency_literals;

auto bus = std::make_shared<idfxx::i2c::master_bus>(
    idfxx::i2c::port::i2c0, idfxx::gpio_21, idfxx::gpio_22, 100_kHz
);

// Scan for devices on the bus
std::vector<uint8_t> devices = bus->scan_devices();

idfxx::log::info("I2C", "Found {} device(s):", devices.size());
for (uint8_t addr : devices) {
    idfxx::log::info("I2C", "  - 0x{:02X}", addr);
}
```

### Register Operations

```cpp
auto device = std::make_unique<idfxx::i2c::master_device>(bus, 0x3C);

// Write to 16-bit register
std::vector<uint8_t> data{0x01, 0x02, 0x03};
device->write_register(0x1000, data);

// Read from 16-bit register
auto read_data = device->read_register(0x1000, 3);
for (uint8_t byte : read_data) {
    idfxx::log::info("I2C", "0x{:02X}", byte);
}

// Write to 8-bit register (using regHigh, regLow)
device->write_register(0x10, 0x00, data);

// Read from 8-bit register
auto read_8bit = device->read_register(0x10, 0x00, 3);
```

### Raw Transmit/Receive

```cpp
auto device = std::make_unique<idfxx::i2c::master_device>(bus, 0x3C);

// Transmit raw bytes
std::vector<uint8_t> tx_data{0x00, 0x01, 0x02};
device->transmit(tx_data);

// Receive raw bytes
auto rx_data = device->receive(4);
idfxx::log::info("I2C", "Received {} bytes", rx_data.size());
```

### Thread-Safe Access

The `master_bus` class implements the Lockable concept:

```cpp
using namespace frequency_literals;

auto bus = std::make_shared<idfxx::i2c::master_bus>(
    idfxx::i2c::port::i2c0, idfxx::gpio_21, idfxx::gpio_22, 100_kHz
);

// Automatic locking with RAII
{
    std::lock_guard<idfxx::i2c::master_bus> lock(*bus);
    // Exclusive bus access here
    // Multiple operations without interruption
}

// Or use unique_lock for more control
std::unique_lock<idfxx::i2c::master_bus> lock(*bus, std::defer_lock);
if (lock.try_lock()) {
    // Got the lock
}
```

### Custom Timeouts

```cpp
using namespace std::chrono_literals;

auto device = std::make_unique<idfxx::i2c::master_device>(bus, 0x3C);

// Read with custom timeout
auto data = device->read_register(0x00, 1, 500ms);

// Default timeout is 50ms (idfxx::i2c::DEFAULT_TIMEOUT)
```

## API Overview

### master_bus

**Creation:**
- `make(port, sda, scl, frequency)` - Create bus (result-based)

**Operations:**
- `scan_devices([timeout])` - Scan for devices
- `try_probe(address, [timeout])` - Probe specific address
- `lock()`, `try_lock()`, `unlock()` - Thread synchronization

**Properties:**
- `port()` - I2C port (`idfxx::i2c::port`)
- `frequency()` - Bus frequency in Hz
- `handle()` - ESP-IDF bus handle

### master_device

**Creation:**
- `make(bus, address)` - Create device (result-based)

**Raw I/O:**
- `try_transmit(data, [timeout])` - Send data
- `try_receive(size, [timeout])` - Receive data

**Register I/O (16-bit address):**
- `try_write_register(reg, data, [timeout])` - Write to register
- `try_read_register(reg, size, [timeout])` - Read from register
- `try_write_registers(regs, data, [timeout])` - Write to multiple registers

**Register I/O (8-bit high/low):**
- `try_write_register(regHigh, regLow, data, [timeout])`
- `try_read_register(regHigh, regLow, size, [timeout])`

**Properties:**
- `bus()` - Parent bus
- `address()` - 7-bit device address
- `handle()` - ESP-IDF device handle

## Integration

See the [Installation](#installation) section above for details on adding `idfxx_i2c` to your project via the ESP-IDF Component Manager or CMakeLists.txt.

## Important Notes

- Addresses are 7-bit (not including R/W bit)
- Register addresses for `write_register(uint16_t, ...)` are sent MSB first
- Bus must be kept alive while devices exist (use `std::shared_ptr`)
- Default timeout is 50ms (`idfxx::i2c::DEFAULT_TIMEOUT`)
- Bus scanning probes addresses 0x08-0x77
- Multiple devices can share the same bus
- Bus access is thread-safe via internal mutex

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
