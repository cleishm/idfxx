# idfxx

Modern C++23 components for ESP-IDF development.

## Documentation

Full API documentation is available at: **https://cleishm.github.io/idfxx/**

## Components

| Component | Description | Documentation |
|-----------|-------------|---------------|
| **Core Infrastructure** | | |
| [idfxx_core](https://github.com/cleishm/idfxx/tree/main/components/idfxx_core) | Core utilities: error handling, memory allocators, chrono, scheduling | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__core.html) |
| [idfxx_hw_support](https://github.com/cleishm/idfxx/tree/main/components/idfxx_hw_support) | Hardware support: interrupt allocation and management | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__hw__support.html) |
| [idfxx_log](https://github.com/cleishm/idfxx/tree/main/components/idfxx_log) | Type-safe logging with std::format | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__log.html) |
| **System Services** | | |
| [idfxx_event](https://github.com/cleishm/idfxx/tree/main/components/idfxx_event) | Type-safe event loop for asynchronous events | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__event.html) |
| [idfxx_event_group](https://github.com/cleishm/idfxx/tree/main/components/idfxx_event_group) | Type-safe FreeRTOS event group for inter-task synchronization | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__event__group.html) |
| [idfxx_queue](https://github.com/cleishm/idfxx/tree/main/components/idfxx_queue) | Type-safe FreeRTOS queue for inter-task communication | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__queue.html) |
| [idfxx_task](https://github.com/cleishm/idfxx/tree/main/components/idfxx_task) | FreeRTOS task management with join, cooperative stop, and fire-and-forget support | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__task.html) |
| [idfxx_timer](https://github.com/cleishm/idfxx/tree/main/components/idfxx_timer) | High-resolution timer (esp_timer) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__timer.html) |
| **Peripheral Drivers** | | |
| [idfxx_gpio](https://github.com/cleishm/idfxx/tree/main/components/idfxx_gpio) | GPIO pin management with ISR support | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__gpio.html) |
| [idfxx_i2c](https://github.com/cleishm/idfxx/tree/main/components/idfxx_i2c) | I2C master bus and device driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__i2c.html) |
| [idfxx_spi](https://github.com/cleishm/idfxx/tree/main/components/idfxx_spi) | SPI master bus driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__spi.html) |
| [idfxx_nvs](https://github.com/cleishm/idfxx/tree/main/components/idfxx_nvs) | Non-Volatile Storage (NVS) wrapper | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__nvs.html) |
| **Display Drivers** | | |
| [idfxx_lcd](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd) | LCD panel I/O interface for SPI-based displays | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd.html) |
| [idfxx_lcd_ili9341](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd_ili9341) | ILI9341 LCD controller driver (240x320) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd.html) |
| [idfxx_lcd_touch](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd_touch) | LCD touch controller interface | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd__touch.html) |
| [idfxx_lcd_touch_stmpe610](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd_touch_stmpe610) | STMPE610 resistive touch controller driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd__touch.html) |
| **Sensor Drivers** | | |
| [idfxx_ds18x20](https://github.com/cleishm/idfxx/tree/main/components/idfxx_ds18x20) | DS18x20 1-Wire temperature sensor driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__ds18x20.html) |

## Features

- **Modern C++23** - Uses `std::expected`, concepts, and other C++23 features
- **Dual error handling** - Both exception-throwing and result-returning APIs
- **Type-safe** - Compile-time validation where possible
- **Zero-overhead abstractions** - Minimal runtime cost over raw ESP-IDF APIs
- **ESP Component Registry** - Install via `idf_component.yml`

## Configuration

### `std::format` Support (`CONFIG_IDFXX_STD_FORMAT`)

Several idfxx types provide `std::formatter` specializations (e.g. `gpio`, `i2c::port`,
`spi::host_device`, `core_id`, `flags<E>`). These are controlled by the `CONFIG_IDFXX_STD_FORMAT`
Kconfig option (under **IDFXX** menu), which is enabled by default.

Disabling this option prevents component headers from pulling in `<format>`, which can
significantly reduce image size. Note that `idfxx_log` requires `CONFIG_IDFXX_STD_FORMAT` and
will produce a build error if included without it.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler (GCC 13+ or Clang 16+)

## Installation

Add components to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_core:
    version: "^0.9.0"
  cleishm/idfxx_gpio:
    version: "^0.9.0"
  # Add other components as needed
```

## Versioning

Repository releases use [calendar versioning](https://calver.org/) (e.g., `v2026.02.10`). Individual
components follow [semantic versioning](https://semver.org/) independently — a component's version is
only bumped when that component changes. Use caret constraints (e.g., `^0.9.0`) in your
`idf_component.yml` to receive compatible updates.

## Basic Examples

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/gpio>
#include <idfxx/lcd/ili9341>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/stmpe610>
#include <idfxx/spi/master>

#include <idfxx/log>

extern "C" void app_main() {
    using namespace idfxx;
    using namespace frequency_literals;

    try {
        // Initialize SPI bus for LCD and touch controller
        auto spi_bus = std::make_shared<spi::master_bus>(
            spi::host_device::spi2,
            spi::bus_config{
                .mosi_io_num = gpio_23,
                .miso_io_num = gpio_19,
                .sclk_io_num = gpio_18,
            },
            spi::dma_chan::ch_auto
        );

        // Create panel I/O for LCD
        auto panel_io = std::make_shared<lcd::panel_io>(
            spi_bus,
            lcd::panel_io::spi_config{
                .cs_gpio = gpio_14,
                .dc_gpio = gpio_27,
                .spi_mode = 0,
                .pclk_freq = 10_MHz,
                .trans_queue_depth = 10,
                .lcd_cmd_bits = 8,
                .lcd_param_bits = 8,
            }
        );

        // Create ILI9341 LCD panel (240x320)
        auto display = std::make_unique<lcd::ili9341>(
            panel_io,
            lcd::panel::dev_config{
                .reset_gpio = gpio_33,
                .rgb_element_order = lcd::rgb_element_order::bgr,
                .bits_per_pixel = 16,
            }
        );

        // Turn on the display
        display->display_on(true);

        // Configure backlight GPIO
        auto backlight = gpio_32;
        backlight.set_direction(gpio::mode::output);
        backlight.set_level(true); // Turn on backlight

        idfxx::log::info("app", "Display initialized successfully");

    } catch (const std::system_error& e) {
        idfxx::log::error("app", "Display initialization failed: {}", e.what());
    }
}
```

### Result-Based API

Using `try_*` methods with `std::expected` for explicit error handling:

```cpp
#include <idfxx/gpio>
#include <idfxx/lcd/ili9341>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/stmpe610>
#include <idfxx/spi/master>

#include <idfxx/log>

extern "C" void app_main() {
    using namespace idfxx;
    using namespace frequency_literals;

    // Initialize SPI bus for LCD and touch controller
    auto spi_bus_res = spi::master_bus::make(
        spi::host_device::spi2,
        spi::bus_config{
            .mosi_io_num = gpio_23,
            .miso_io_num = gpio_19,
            .sclk_io_num = gpio_18,
        },
        spi::dma_chan::ch_auto
    );
    if (!spi_bus_res) {
        idfxx::log::error("app", "Failed to initialize SPI bus: {}", spi_bus_res.error().message());
        return;
    }
    auto spi_bus = std::move(*spi_bus_res);

    // Create panel I/O for LCD
    auto panel_io_res = lcd::panel_io::make(
        spi_bus,
        lcd::panel_io::spi_config{
            .cs_gpio = gpio_14,
            .dc_gpio = gpio_27,
            .spi_mode = 0,
            .pclk_freq = 10_MHz,
            .trans_queue_depth = 10,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        }
    );
    if (!panel_io_res) {
        idfxx::log::error("app", "Failed to create panel I/O: {}", panel_io_res.error().message());
        return;
    }
    auto panel_io = std::move(*panel_io_res);

    // Create ILI9341 LCD panel (240x320)
    auto display_res = lcd::ili9341::make(
        panel_io,
        lcd::panel::dev_config{
            .reset_gpio = gpio_33,
            .rgb_element_order = lcd::rgb_element_order::bgr,
            .bits_per_pixel = 16,
        }
    );
    if (!display_res) {
        idfxx::log::error("app", "Failed to initialize display: {}", display_res.error().message());
        return;
    }
    auto display = std::move(*display_res);

    // Turn on the display
    if (auto res = display->try_display_on(true); !res) {
        idfxx::log::error("app", "Failed to turn on display: {}", res.error().message());
        return;
    }

    // Configure backlight GPIO
    auto backlight = gpio_32;
    backlight.try_set_direction(gpio::mode::output);
    backlight.try_set_level(true); // Turn on backlight

    idfxx::log::info("app", "Display initialized successfully");
}
```

## Testing

idfxx includes comprehensive tests using the Unity test framework. Tests are categorized into two types:

- **Software tests** - Test error handling, type safety, and logic without hardware dependencies
- **Hardware tests** - Test actual hardware operations (GPIO pins, NVS flash, SPI peripherals)

### Running Tests in QEMU

Software tests can run in the QEMU ESP32-S3 emulator without physical hardware:

```bash
# Set up ESP-IDF environment
source $IDF_PATH/export.sh

# Configure for QEMU
idf.py set-target esp32s3
cp sdkconfig.qemu sdkconfig
idf.py reconfigure

# Build the project
idf.py build

# Create flash image
esptool.py --chip esp32s3 merge_bin \
  -o flash_image.bin \
  --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/idfxx.bin

# Run tests in QEMU
qemu-system-xtensa \
  -machine esp32s3 \
  -nographic \
  -drive file=flash_image.bin,if=mtd,format=raw \
  -serial mon:stdio
```

QEMU tests automatically exclude hardware-dependent tests tagged with `[hw]`.

### Running Tests on Hardware

All tests, including hardware-dependent tests, can run on physical ESP32-S3 hardware:

```bash
# Build and flash to device
idf.py set-target esp32s3
idf.py build flash monitor
```

Hardware tests require:
- ESP32-S3 development board
- Available GPIO pins for GPIO tests
- Flash storage for NVS tests
- SPI peripheral availability for SPI tests

### Test Organization

Tests are organized by component in `components/*/tests/` directories.

### Continuous Integration

GitHub Actions automatically runs:
1. Build verification for ESP32-S3 target
2. QEMU test execution for software tests

See `.github/workflows/build.yml` for CI configuration.

## Code Style

All components use the same clang-format configuration. Format code with:

```bash
cmake --build build --target format
```

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
