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
- Sequential reading of a whole partition or a byte range as an `idfxx::byte_source`
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
    version: "^2.0.0"
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

    // Read data (fills the whole buffer)
    std::array<std::byte, 256> buf{};
    part.read(0, buf);

    // Erase before writing (must be aligned to erase_size)
    part.erase_range(0, part.erase_size());

    // Write data
    std::array<uint8_t, 16> data{1, 2, 3, 4};
    part.write(0, data);

    // Compute SHA-256 hash
    auto hash = part.sha256();

    // Memory-map a region
    auto mapped = part.mmap(0, part.size());
    // Or, on ESP-IDF 6.1+, block flash writes instead of disabling the cache:
    // auto mapped = part.mmap(0, part.size(), {.blocks_write = true});
    auto bytes = mapped.bytes();

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
std::array<std::byte, 256> buf{};
if (auto r = part.try_read(0, buf); !r) {
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

### Sequential Reading

`partition_source` reads a partition — or a byte range of it — from start to end, and works with any
code written against `idfxx::byte_source`:

```cpp
#include <idfxx/partition>

auto part = idfxx::partition::find("storage");

// Read bytes 0x100..0x1100 in chunks
idfxx::partition_source src{part, 0x100, 0x1000};
std::array<std::byte, 256> buf{};
while (size_t n = src.read(buf)) {
    // process idfxx::byte_view{buf}.first(n)...
}

// Result-based: make() rejects a range that extends past the end of the partition
auto whole = idfxx::partition_source::make(part, 0, part.size());
if (!whole) {
    // errc::invalid_arg
}
```

## API Overview

The component provides two API styles:
- **Exception-based API** (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`) - Methods throw
  `std::system_error` on error
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
- `read(offset, byte_span)` - Read with decryption, filling the whole buffer
- `write(offset, byte_view)` - Write with encryption
- `read_raw(offset, byte_span)` - Raw read, filling the whole buffer
- `write_raw(offset, byte_view)` - Raw write
- `erase_range(offset, size)` - Erase region
- `sha256()` → `std::array<std::byte, 32>` - Compute hash
- `mmap(offset, size, config)` → `mmap_handle` - Memory map (`mmap_config`: `memory`, and
  `blocks_write` on ESP-IDF 6.1+)

**Result-based:**
- `try_read`, `try_write`, `try_read_raw`, `try_write_raw` → `result<void>`
- `try_erase_range` → `result<void>`
- `try_sha256` → `result<std::array<std::byte, 32>>`
- `try_mmap` → `result<mmap_handle>`

**Always available:**
- `check_identity(other)` → `bool` - Compare partition contents

### Memory Mapping (`mmap_handle`)

- `data()` → `const void*` - Access mapped memory
- `size()` → `size_t` - Mapped region size
- `bytes()` → `idfxx::byte_view` - Mapped region as read-only bytes
- `as_span<T>()` → `std::span<T>` - Typed span over mapped memory
- `release()` → `esp_partition_mmap_handle_t` - Release ownership without unmapping

### Sequential Reading (`partition_source`)

Models `idfxx::byte_source`.

- `partition_source(part)` - Read the whole partition
- `partition_source(part, offset, length)` - Read a byte range (exception-based; throws on an
  out-of-range window)
- `partition_source::make(part, offset, length)` → `result<partition_source>` - Read a byte range
- `remaining()` → `size_t` - Bytes left in the range
- `read(byte_span)` → `size_t` / `try_read(byte_span)` → `result<size_t>` - Read
  the next bytes, with decryption; 0 at the end of the range

## Error Handling

Uses standard `idfxx::errc` error codes:

- `not_found` - No partition matches the search criteria
- `invalid_state` - Operation on a null partition
- `invalid_arg` - Invalid offset, size, or alignment; a `partition_source` range that extends past
  the end of the partition
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
