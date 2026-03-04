# idfxx_partition

Type-safe flash partition discovery, reading, writing, and memory mapping.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__partition.html)**

## Features

- Partition discovery by type, subtype, and label
- Read and write with automatic encryption/decryption support
- Raw read and write bypassing encryption
- Erase operations with alignment validation
- SHA-256 hash computation and identity comparison
- Memory-mapped partition access with automatic cleanup
- Copyable partition handles valid for the application lifetime

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_partition:
    version: "^0.9.0"
```

Or add `idfxx_partition` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/partition>

try {
    // Find a partition by label
    auto part = idfxx::partition::find("storage");

    // Read data
    std::array<uint8_t, 256> buf{};
    part.read(0, buf.data(), buf.size());

    // Erase before writing (must be aligned to erase_size)
    part.erase_range(0, part.erase_size());

    // Write data
    std::array<uint8_t, 16> data{1, 2, 3, 4};
    part.write(0, data.data(), data.size());

    // Compute SHA-256 hash
    auto hash = part.sha256();

    // Memory-map a region
    auto mapped = part.mmap(0, part.size());
    auto bytes = mapped.as_span<const uint8_t>();

} catch (const std::system_error& e) {
    // Handle error
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/partition>

// Find a partition
auto result = idfxx::partition::try_find("storage");
if (!result) {
    // Handle error
    return;
}
auto part = *result;

// Read data
std::array<uint8_t, 256> buf{};
if (auto r = part.try_read(0, buf.data(), buf.size()); !r) {
    // Handle error
    return;
}

// Find all data partitions
auto parts = idfxx::partition::find_all(idfxx::partition::type::data);
for (const auto& p : parts) {
    // Process each partition
}
```

### OTA Partitions

```cpp
#include <idfxx/partition>

// Find a specific OTA slot
auto ota0 = idfxx::partition::find(idfxx::partition::type::app, idfxx::app_ota(0));

// Find all OTA app partitions
for (unsigned i = 0; i < 16; ++i) {
    auto result = idfxx::partition::try_find(idfxx::partition::type::app, idfxx::app_ota(i));
    if (!result) break;
    // Use partition...
}
```

## API Overview

The component provides two API styles:
- **Exception-based API** (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`) - Methods throw `std::system_error` on error
- **Result-based API** (always available) - Methods prefixed with `try_*` return `result<T>`

### Discovery

**Exception-based:**
- `partition::find(type, subtype, label)` → `partition`
- `partition::find(label)` → `partition`

**Result-based:**
- `partition::try_find(type, subtype, label)` → `result<partition>`
- `partition::try_find(label)` → `result<partition>`

**Always available:**
- `partition::find_all(type, subtype, label)` → `std::vector<partition>`

### Accessors

- `type()` → `partition::type`
- `subtype()` → `partition::subtype`
- `address()` → `uint32_t`
- `size()` → `uint32_t`
- `erase_size()` → `uint32_t`
- `label()` → `std::string_view`
- `encrypted()` → `bool`
- `readonly()` → `bool`
- `idf_handle()` → `const esp_partition_t*`

### Operations

**Exception-based:**
- `read(offset, dst, size)` / `read(offset, span)` - Read with decryption
- `write(offset, src, size)` / `write(offset, span)` - Write with encryption
- `read_raw(offset, dst, size)` / `read_raw(offset, span)` - Raw read
- `write_raw(offset, src, size)` / `write_raw(offset, span)` - Raw write
- `erase_range(offset, size)` - Erase region
- `sha256()` → `std::array<uint8_t, 32>` - Compute hash
- `mmap(offset, size, memory)` → `mmap_handle` - Memory map

**Result-based:**
- `try_read`, `try_write`, `try_read_raw`, `try_write_raw` → `result<void>`
- `try_erase_range` → `result<void>`
- `try_sha256` → `result<std::array<uint8_t, 32>>`
- `try_mmap` → `result<mmap_handle>`

**Always available:**
- `check_identity(other)` → `bool` - Compare partition contents

### Memory Mapping (`mmap_handle`)

- `data()` → `const void*` - Access mapped memory
- `size()` → `size_t` - Mapped region size
- `as_span<T>()` → `std::span<T>` - Typed span over mapped memory
- `release()` → `esp_partition_mmap_handle_t` - Release ownership without unmapping

## Error Handling

Uses standard `idfxx::errc` error codes:

- `not_found` - No partition matches the search criteria
- `invalid_state` - Operation on a null partition
- `invalid_arg` - Invalid offset, size, or alignment
- `invalid_size` - Size exceeds partition bounds
- `not_allowed` - Write to a read-only partition

## Important Notes

- **Dual API Pattern**: Component provides both result-based (`try_*`) and exception-based APIs
  - Exception-based methods require `CONFIG_COMPILER_CXX_EXCEPTIONS` to be enabled
  - Use result-based API when exceptions are disabled or for explicit error handling
  - Use exception-based API for cleaner code when exceptions are available
- Partition handles are copyable and remain valid for the entire application lifetime
- `erase_range()` offset and size must be aligned to `erase_size()`
- `mmap()` returns a handle that automatically unmaps the region on destruction
- `find_all()` always succeeds (returns empty vector if no matches)

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
