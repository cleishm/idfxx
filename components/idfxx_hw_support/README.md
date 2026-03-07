# idfxx_hw_support

Hardware support: interrupt allocation, chip info, and MAC addresses.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__hw__support.html)**

## Features

- Type-safe interrupt allocation flags with operator support
- Chip model identification, revision, and feature detection
- MAC address value type with reading and configuration

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_hw_support:
    version: "^0.9.0"
```

Or add `idfxx_hw_support` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Interrupt Allocation Flags

```cpp
#include <idfxx/intr_alloc>
#include <esp_intr_alloc.h>

// Type-safe interrupt flags with bitwise operators
idfxx::flags<idfxx::intr_flag> flags =
    idfxx::intr_flag::level1 | idfxx::intr_flag::iram;

// Use predefined level masks
idfxx::flags<idfxx::intr_flag> low_priority = idfxx::intr_flag_lowmed;  // Levels 1-3
idfxx::flags<idfxx::intr_flag> high_priority = idfxx::intr_flag_high;   // Levels 4-6 + NMI

// Pass to ESP-IDF functions
esp_intr_alloc(ETS_TIMER0_INTR_SOURCE,
               static_cast<int>(to_underlying(flags)),
               handler,
               nullptr,
               &handle);
```

### Combining Flags

```cpp
// Multiple interrupt levels
auto multi_level = idfxx::intr_flag::level1 |
                   idfxx::intr_flag::level2 |
                   idfxx::intr_flag::level3;

// Shared, edge-triggered interrupt
auto shared_edge = idfxx::intr_flag::shared | idfxx::intr_flag::edge;

// IRAM-safe interrupt handler
auto iram_flags = idfxx::intr_flag_lowmed | idfxx::intr_flag::iram;

// Check if specific flag is set
if ((flags & idfxx::intr_flag::iram) != idfxx::intr_flag::none) {
    idfxx::log::info("INTR", "IRAM flag is set");
}
```

### Chip Info

```cpp
#include <idfxx/chip>

auto info = idfxx::chip_info::get();
auto model = info.model();           // e.g. chip_model::esp32s3
auto cores = info.cores();           // e.g. 2
auto rev = info.revision();          // e.g. 100 (v1.0)

if (info.features().contains(idfxx::chip_feature::wifi)) {
    // Chip has WiFi
}
```

### MAC Addresses

```cpp
#include <idfxx/mac>

auto mac = idfxx::base_mac_address();
auto s = idfxx::to_string(mac); // "AA:BB:CC:DD:EE:FF"

// Result-based API
auto r = idfxx::try_read_mac(idfxx::mac_type::wifi_sta);
if (r) {
    auto wifi_mac = *r;
}
```

## Important Notes

- **Priority Levels**: Levels 1-3 can use C/C++ handlers. Levels 4-6 and NMI require assembly handlers and must pass NULL as the handler function.
- **IRAM Flag**: Use `intr_flag::iram` for ISR handlers that need to run when cache is disabled (e.g., during flash operations).
- **Shared Interrupts**: Multiple handlers can share the same interrupt source with `intr_flag::shared`.
- **Type Safety**: All enums are strongly typed and prevent accidental misuse.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
