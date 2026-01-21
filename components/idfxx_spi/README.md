# idfxx_spi

Type-safe SPI master bus driver for ESP32.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__spi.html)**

## Features

- SPI bus lifecycle management
- Support for DMA transfers

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_spi:
    version: "^0.9.0"
```

Or add `idfxx_spi` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
try {
    auto bus = std::make_unique<idfxx::spi::master_bus>(
        idfxx::spi::host_device::spi2,
        idfxx::spi::dma_chan::ch_auto,
        idfxx::spi::bus_config{ /* ... */ }
    );

    // Use bus
    idfxx::log::info("SPI", "SPI bus initialized");

} catch (const std::system_error& e) {
    idfxx::log::error("SPI", "Failed to create bus: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/spi/master>
#include <idfxx/log>

// Configure SPI bus
idfxx::spi::bus_config config{
    .mosi_io_num = idfxx::gpio_23,
    .miso_io_num = idfxx::gpio_19,
    .sclk_io_num = idfxx::gpio_18,
    .max_transfer_sz = 4096,
};

// Create SPI bus - returns idfxx::result<std::unique_ptr<idfxx::spi::master_bus>>
auto bus_result = idfxx::spi::master_bus::make(
    idfxx::spi::host_device::spi2,  // SPI host
    idfxx::spi::dma_chan::ch_auto,  // DMA channel (or dma_chan::disabled)
    std::move(config)               // Bus configuration. Using `std::move(...)` is not required, as the bus config
                                    // is copyable, but it is included here for consistency with other classes.
);

if (!bus_result) {
    idfxx::log::error("SPI", "Failed to create bus: {}", bus_result.error().message());
    return;
}
auto bus = std::move(*bus_result);

idfxx::log::info("SPI", "SPI bus initialized");
```

### With DMA

```cpp
auto bus = std::make_unique<idfxx::spi::master_bus>(
    idfxx::spi::host_device::spi2,
    idfxx::spi::dma_chan::ch_auto,  // Automatic DMA channel selection
    idfxx::spi::bus_config{
        .mosi_io_num = idfxx::gpio_23,
        .miso_io_num = idfxx::gpio_19,
        .sclk_io_num = idfxx::gpio_18,
        .max_transfer_sz = 32768,  // Larger transfers with DMA
    }
);
```

### Without DMA

```cpp
auto bus = std::make_unique<idfxx::spi::master_bus>(
    idfxx::spi::host_device::spi2,
    idfxx::spi::dma_chan::disabled,  // No DMA
    idfxx::spi::bus_config{ /* ... */ }
);
```

### Integration with `idfxx_lcd`

The SPI bus is commonly used with LCD panels.

```cpp
#include <idfxx/spi/master>
#include <idfxx/lcd/panel_io>

// Create shared SPI bus
auto spi_bus = std::make_shared<idfxx::spi::master_bus>(
    idfxx::spi::host_device::spi2,
    idfxx::spi::dma_chan::ch_auto,
    idfxx::spi::bus_config{ /* ... */ }
);

// Create panel I/O using the bus
auto panel_io = std::make_unique<idfxx::lcd::panel_io>(
    spi_bus,              // Shared pointer to bus
    idfxx::lcd::panel_io::spi_config{ /* ... */ }
);

// Bus is kept alive as long as panel_io exists
```

## API Overview

### `master_bus`

**Creation:**
- `make(host, dma_chan, config)` - Create SPI bus (result-based)
- `master_bus(host, dma_chan, config)` - Constructor (exception-based, if enabled)

**Properties:**
- `host()` - Get SPI host device

**Lifetime:**
- Non-copyable and non-movable
- Destructor automatically cleans up resources

## SPI Hosts

ESP32 provides multiple SPI hosts via `idfxx::spi::host_device`:
- `host_device::spi1` - Typically used for flash
- `host_device::spi2` - General purpose (HSPI on some chips)
- `host_device::spi3` - General purpose (VSPI on some chips)

Check your ESP32 variant's documentation for available hosts.

## DMA Channels

Via `idfxx::spi::dma_chan`:
- `dma_chan::ch_auto` - Automatic DMA channel selection (recommended)
- `dma_chan::ch_1`, `dma_chan::ch_2` - Specific DMA channels (ESP32 only)
- `dma_chan::disabled` - No DMA (limited transfer size)

## Important Notes

- **Dual API Pattern**: The component provides both result-based and exception-based APIs
  - `make()` returns `result<std::unique_ptr<master_bus>>` (always available)
  - Constructor throws `std::system_error` (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- Only one bus can be initialized per SPI host
- The bus must remain alive while devices using it exist
- Use `std::shared_ptr` when passing to other components
- DMA enables larger transfers but requires DMA-capable memory
- Maximum transfer size without DMA is ~64 bytes
- With DMA, transfer size is limited by `max_transfer_sz` in config
- GPIO pins must not be used by other peripherals

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
