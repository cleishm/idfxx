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
    idfxx::i2c::master_bus bus(
        idfxx::i2c::port::i2c0,  // I2C port
        idfxx::gpio_21,          // SDA pin
        idfxx::gpio_22,          // SCL pin
        100_kHz                  // Frequency
    );

    // Create device on the bus
    idfxx::i2c::master_device device(
        bus,     // Reference to bus
        0x3C     // Device address
    );

    // Read from register (throws on error)
    auto data = device.read_register(0x00, 1);
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
    bus,     // Reference to bus
    0x3C     // Device address
);

if (!dev_result) {
    idfxx::log::error("I2C", "Failed to create device: {}", dev_result.error().message());
    return;
}
auto device = std::move(*dev_result);

// Read from register
auto data_result = device.try_read_register(0x00, 1);
if (data_result) {
    idfxx::log::info("I2C", "Register value: 0x{:02X}", (*data_result)[0]);
} else {
    idfxx::log::error("I2C", "Read failed: {}", data_result.error().message());
}
```

### Advanced Configuration

For fine-grained control over bus and device parameters, use the config-based constructors:

```cpp
#include <idfxx/i2c/master>

using namespace frequency_literals;

// Bus with custom settings
idfxx::i2c::master_bus bus(idfxx::i2c::port::i2c0, {
    .sda = idfxx::gpio_21,
    .scl = idfxx::gpio_22,
    .frequency = 400_kHz,
    .glitch_ignore_cnt = 10,
    .enable_internal_pullup = false,
});

// Device with per-device SCL speed
idfxx::i2c::master_device device(bus, 0x3C, {
    .scl_speed = 100_kHz,  // Slower than bus default
});

// 10-bit addressed device
idfxx::i2c::master_device device_10bit(bus, 0x1FF, {
    .addr_10bit = true,
});
```

### Scanning for Devices

```cpp
using namespace frequency_literals;

idfxx::i2c::master_bus bus(
    idfxx::i2c::port::i2c0, idfxx::gpio_21, idfxx::gpio_22, 100_kHz
);

// Scan for devices on the bus
std::vector<uint8_t> devices = bus.scan_devices();

idfxx::log::info("I2C", "Found {} device(s):", devices.size());
for (uint8_t addr : devices) {
    idfxx::log::info("I2C", "  - 0x{:02X}", addr);
}
```

### Register Operations

```cpp
idfxx::i2c::master_device device(bus, 0x3C);

// Write to 16-bit register
std::vector<uint8_t> data{0x01, 0x02, 0x03};
device.write_register(0x1000, data);

// Read from 16-bit register
auto read_data = device.read_register(0x1000, 3);
for (uint8_t byte : read_data) {
    idfxx::log::info("I2C", "0x{:02X}", byte);
}

// Write to 8-bit register (using high, low)
device.write_register(0x10, 0x00, data);

// Read from 8-bit register
auto read_8bit = device.read_register(0x10, 0x00, 3);
```

### Raw Transmit/Receive

```cpp
idfxx::i2c::master_device device(bus, 0x3C);

// Transmit raw bytes
std::vector<uint8_t> tx_data{0x00, 0x01, 0x02};
device.transmit(tx_data);

// Receive raw bytes
auto rx_data = device.receive(4);
idfxx::log::info("I2C", "Received {} bytes", rx_data.size());
```

### Thread-Safe Access

The `master_bus` class implements the Lockable concept:

```cpp
using namespace frequency_literals;

idfxx::i2c::master_bus bus(
    idfxx::i2c::port::i2c0, idfxx::gpio_21, idfxx::gpio_22, 100_kHz
);

// Automatic locking with RAII
{
    std::lock_guard<idfxx::i2c::master_bus> lock(bus);
    // Exclusive bus access here
    // Multiple operations without interruption
}

// Or use unique_lock for more control
std::unique_lock<idfxx::i2c::master_bus> lock(bus, std::defer_lock);
if (lock.try_lock()) {
    // Got the lock
}
```

### Custom Timeouts

```cpp
using namespace std::chrono_literals;

idfxx::i2c::master_device device(bus, 0x3C);

// Read with custom timeout
auto data = device.read_register(0x00, 1, 500ms);

// Default timeout is 50ms (idfxx::i2c::DEFAULT_TIMEOUT)
```

## API Overview

### master_bus

**Creation:**
- `master_bus(port, config)` - Constructor (exception-based, if enabled)
- `master_bus(port, sda, scl, frequency)` - Convenience constructor with defaults
- `make(port, config)` - Create bus (result-based)
- `make(port, sda, scl, frequency)` - Convenience factory with defaults

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
- `master_device(bus, address, config)` - Constructor (exception-based, if enabled)
- `master_device(bus, address)` - Convenience constructor with defaults
- `make(bus, address, config)` - Create device (result-based)
- `make(bus, address)` - Convenience factory with defaults

**Raw I/O:**
- `try_transmit(data, [timeout])` - Send data
- `try_receive(size, [timeout])` - Receive data

**Register I/O (16-bit address):**
- `try_write_register(reg, data, [timeout])` - Write to register
- `try_read_register(reg, size, [timeout])` - Read from register
- `try_write_registers(regs, data, [timeout])` - Write to multiple registers

**Register I/O (8-bit high/low):**
- `try_write_register(high, low, data, [timeout])`
- `try_read_register(high, low, size, [timeout])`

**Properties:**
- `bus()` - Parent bus
- `address()` - Device address
- `handle()` - ESP-IDF device handle

## Integration

See the [Installation](#installation) section above for details on adding `idfxx_i2c` to your project via the ESP-IDF Component Manager or CMakeLists.txt.

## Important Notes

- Addresses are 7-bit by default (not including R/W bit); 10-bit addressing is available via `config.addr_10bit` on targets with `SOC_I2C_SUPPORT_10BIT_ADDR` (e.g., esp32, esp32s3, esp32c3/c6, esp32h2, esp32p4). The field is not present on targets without this capability (e.g., esp32s2).
- Register addresses for `write_register(uint16_t, ...)` are sent MSB first
- The bus must remain alive while devices using it exist; the caller is responsible for ensuring this
- Default timeout is 50ms (`idfxx::i2c::DEFAULT_TIMEOUT`)
- Bus scanning probes addresses 0x08-0x77
- Multiple devices can share the same bus
- Bus access is thread-safe via internal mutex

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
