# idfxx_i2c

Type-safe I2C master bus driver for ESP32.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__i2c.html)**

## Features

- I2C bus and device lifecycle management
- Thread-safe bus access with a `TimedLockable` interface; transfers wait at most their timeout for
  the bus
- Device scanning and probing
- Register-based read/write operations (one-byte or two-byte register addresses), including single
  bytes and multi-byte integer values in a chosen byte order
- Raw transmit/receive, multi-buffer transmit, and custom operation sequences
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
    version: "^3.0.0"
```

Or add `idfxx_i2c` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/i2c/master>
#include <idfxx/log>

using namespace quanta::frequency_literals;

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

using namespace quanta::frequency_literals;

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

using namespace quanta::frequency_literals;

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
    .scl_speed = 100_kHz,  // Slower than the bus frequency (omit to use the bus frequency)
});

// 10-bit addressed device
idfxx::i2c::master_device device_10bit(bus, 0x1FF, {
    .addr_10bit = true,
});
```

### Scanning for Devices

```cpp
using namespace quanta::frequency_literals;

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

Register addresses are one byte by default, which suits most devices:

```cpp
idfxx::i2c::master_device sensor(bus, 0x68);

// Write to a register
std::vector<uint8_t> data{0x01, 0x02, 0x03};
sensor.write_register(0x10, data);

// Read from a register
auto read_data = sensor.read_register(0x10, 3);
for (uint8_t byte : read_data) {
    idfxx::log::info("I2C", "0x{:02X}", byte);
}
```

Devices with 16-bit register addresses, such as larger EEPROMs, opt in with `register_address`:

```cpp
idfxx::i2c::master_device eeprom(bus, 0x50, {
    .register_address = idfxx::i2c::register_address_size::two_bytes,
});

// Register addresses are sent most significant byte first
eeprom.write_register(0x1000, data);
auto eeprom_data = eeprom.read_register(0x1000, 3);
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

The `master_bus` class implements the TimedLockable concept. Transfers take the same lock, so
holding it groups several transfers without interruption:

```cpp
using namespace quanta::frequency_literals;

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
if (lock.try_lock_for(std::chrono::milliseconds(100))) {
    // Got the lock within 100 ms
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

The timeout bounds both the wait for the bus lock and the transfer itself. A transfer that doesn't
finish in time fails with `idfxx::errc::invalid_response`, so raise the timeout for long transfers.

### Upgrading from 2.x

- Register addresses are one byte by default. In 2.x the register methods always sent two register
  address bytes; devices that relied on that must set
  `register_address = idfxx::i2c::register_address_size::two_bytes`. The `(high, low)` register
  overloads are replaced by a single `uint16_t` register.
- `read_register()` sends the register address and reads back in one transaction with a repeated
  START. In 2.x it wrote the address, sent a STOP, waited 20 ms, then read. Devices that need a STOP
  or a conversion delay between the two should use `transmit()` then `receive()`.
- `write_registers()` no longer waits 4 ms between registers. Devices with a write cycle, such as
  EEPROMs, need the caller to wait or poll between writes.
- Failed transfers are no longer reported as `idfxx::errc::invalid_arg`; see [Error
  Handling](#error-handling).
- Construction errors from the driver now pass through instead of being reported as
  `idfxx::errc::invalid_arg`. For example, a port that's already in use fails with
  `idfxx::errc::invalid_state`.
- Asynchronous transfers were removed: `master_bus::config::trans_queue_depth`, `wait_all_done()`
  and `register_event_callbacks()` no longer exist, and every transfer finishes before the call
  returns. The ESP-IDF asynchronous mode can't report bus errors or reliably signal when the
  caller's buffers are free.

## API Overview

### master_bus

**Creation:**
- `master_bus(port, config)` - Constructor (exception-based, if enabled)
- `master_bus(port, sda, scl, frequency)` - Convenience constructor with defaults
- `make(port, config)` - Create bus (result-based)
- `make(port, sda, scl, frequency)` - Convenience factory with defaults

**Operations:**
- `scan_devices([timeout])` - Scan for devices
- `probe(address, [timeout])` / `try_probe(address, [timeout])` - Probe specific address
- `lock()`, `try_lock()`, `try_lock_for(timeout)`, `try_lock_until(deadline)`, `unlock()` - Thread
  synchronization

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
- `transmit(data, [timeout])` / `try_transmit(...)` - Send data, from one buffer or several in one
  transaction
- `receive(size, [timeout])` / `try_receive(...)` - Receive data, into a new vector or a caller's
  buffer
- `execute_operations(ops, [timeout])` / `try_execute_operations(...)` - Run a custom
  START/WRITE/READ/STOP sequence
- `probe([timeout])` / `try_probe([timeout])` - Probe the device

**Register I/O** (register address size set by `config.register_address`):
- `write_register(reg, data, [timeout])` / `try_write_register(...)` - Write to register
- `write_register(reg, value, [timeout])` / `try_write_register(...)` - Write a single `uint8_t`
- `write_register<T>(reg, value, order, [timeout])` / `try_write_register<T>(...)` - Write an
  unsigned integer in `std::endian` byte `order`
- `read_register(reg, size, [timeout])` / `try_read_register(...)` - Read from register
- `read_register(reg, [timeout])` / `try_read_register(...)` - Read a single `uint8_t`
- `read_register<T>(reg, order, [timeout])` / `try_read_register<T>(...)` - Read an unsigned integer
  in `std::endian` byte `order`
- `write_registers(regs, data, [timeout])` / `try_write_registers(...)` - Write to multiple
  registers

**Addressing:**
- `change_address(new_address, [timeout])` / `try_change_address(...)` - Change the device address

**Properties:**
- `bus()` - Parent bus
- `address()` - Device address
- `register_address()` - Register address size
- `scl_speed()` - Configured SCL speed (`config.scl_speed`, or the bus frequency when unset)
- `handle()` - ESP-IDF device handle

## Error Handling

Errors are reported as `std::error_code` values from `idfxx::errc` (thrown as `std::system_error` by
the exception-based methods).

**Transfers** (`transmit`, `receive`, register methods, `execute_operations`, `change_address`):
- `idfxx::errc::invalid_response` - The device didn't acknowledge (NACK), the transfer timed out on
  the bus (including clock stretching beyond `scl_wait_us`), or it didn't finish within the timeout.
  The driver can't tell these apart, and they are reported the same way on every supported ESP-IDF
  version, so raise the timeout for long transfers.
- `idfxx::errc::timeout` - The bus could not be acquired within the timeout
- `idfxx::errc::invalid_arg` - An argument was rejected before anything was sent: a register above
  0xFF with one-byte register addresses, a null buffer with a nonzero size, a zero-byte register
  read, or an address that doesn't fit the device's 7-bit or 10-bit addressing in `change_address`
- Other driver errors are passed through

**Probing** (`probe`, `scan_devices`):
- `idfxx::errc::not_found` - No device acknowledged
- `idfxx::errc::timeout` - The bus could not be acquired in time, or the probe timed out on the bus
  (for example, SCL held low); the driver reports both the same way

**Construction** (`master_bus`, `master_device`):
- `idfxx::errc::invalid_arg` - An unconnected SDA/SCL pin, a zero bus frequency, a non-positive
  `scl_speed`, a zero `scl_wait_us`, or a configuration the driver rejects (pins, interrupt level)
- `idfxx::errc::invalid_state` - The port is already in use
- `idfxx::errc::not_supported` - A setting the target or port doesn't support, such as `allow_pd` or
  an LP I2C clock source
- `idfxx::errc::not_found` - No free interrupt
- Other driver errors are passed through; running out of memory throws `std::bad_alloc` (or aborts
  when exceptions are disabled)

## Integration

See the [Installation](#installation) section above for details on adding `idfxx_i2c` to your
project via the ESP-IDF Component Manager or CMakeLists.txt.

## Important Notes

- Addresses are 7-bit by default (not including R/W bit); 10-bit addressing is available via
  `config.addr_10bit` on targets with `SOC_I2C_SUPPORT_10BIT_ADDR` (e.g., esp32, esp32s3,
  esp32c3/c6, esp32h2, esp32p4). The field is not present on targets without this capability (e.g.,
  esp32s2).
- Register addresses are one byte by default; set `config.register_address` to
  `register_address_size::two_bytes` for devices with 16-bit register addresses, which are then sent
  MSB first. With one-byte register addresses, a register above 0xFF fails with
  `idfxx::errc::invalid_arg` before anything is sent
- Multi-byte integer register values always take a byte order. `write_register(reg, value)` writes
  one byte from a `uint8_t` or an `int` such as a plain literal; an unsigned value of any wider type
  doesn't compile, rather than writing only its low byte. That includes wide unsigned masks such as
  `0x80u` or ESP-IDF's `BIT(n)`, so declare single-byte register bits as `uint8_t` constants
  (`constexpr uint8_t ctrl_enable = 0x80;`)
- The bus must remain alive while devices using it exist; the caller is responsible for ensuring
  this
- Default timeout is 50ms (`idfxx::i2c::DEFAULT_TIMEOUT`)
- Bus scanning probes addresses 0x08-0x77
- Multiple devices can share the same bus
- Every transfer finishes before the call that starts it returns
- Bus access is thread-safe via an internal mutex; each transfer waits at most its timeout for it
- With ESP-IDF 6.1 or later and `CONFIG_I2C_ISR_IRAM_SAFE` enabled, buffers that receive data
  (`receive`, `read_register`, and read operations in `execute_operations`) must reside in internal
  RAM, not PSRAM; otherwise the call fails with `idfxx::errc::invalid_arg`

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
