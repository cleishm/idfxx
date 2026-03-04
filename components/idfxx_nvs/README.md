# idfxx_nvs

Type-safe persistent key-value storage in flash memory.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__nvs.html)**

## Features

- NVS namespace lifecycle management
- Type-safe storage for integers (8-64 bits), strings, and binary blobs
- Explicit commit model for atomic updates
- Read-only mode support
- Partition-based APIs for multi-partition NVS configurations
- NVS encryption key generation and reading
- Domain-specific error codes

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_nvs:
    version: "^0.9.0"
```

Or add `idfxx_nvs` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/nvs>
#include <idfxx/log>

// Initialize NVS flash (call once at startup)
idfxx::nvs::flash::init();

try {
    idfxx::nvs nvs("settings", false);

    // Write operations (no need to check for errors)
    nvs.set_value<uint32_t>("counter", 42);
    nvs.set_string("name", "MyDevice");

    std::vector<uint8_t> blob{0x01, 0x02, 0x03};
    nvs.set_blob("data", blob);

    // Commit changes
    nvs.commit();

    // Read operations (throws if key doesn't exist)
    uint32_t counter = nvs.get_value<uint32_t>("counter");
    std::string name = nvs.get_string("name");
    std::vector<uint8_t> data = nvs.get_blob("data");

    // Erase operations
    nvs.erase("old_key");
    nvs.commit();

} catch (const std::system_error& e) {
    idfxx::log::error("NVS", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/nvs>
#include <idfxx/log>

// Initialize NVS flash (call once at startup)
// If initialization fails due to no_free_pages or new_version_found, the flash is erased
// and the initialization re-attempted.
auto init_result = idfxx::nvs::flash::try_init(); // idfxx::result<void>
if (!init_result) {
    if (init_result.error() == idfxx::nvs::errc::no_free_pages ||
        init_result.error() == idfxx::nvs::errc::new_version_found) {
        idfxx::nvs::flash::erase();
        init_result = idfxx::nvs::flash::try_init();
    }
}
if (!init_result) {
    idfxx::log::error("NVS", "Failed to initialize NVS: {}", init_result.error().message());
    return;
}

// Open a namespace
auto nvs_result = idfxx::nvs::make("settings", false); // idfxx::result<idfxx::nvs>
if (!nvs_result) {
    idfxx::log::error("NVS", "Failed to open namespace: {}", nvs_result.error().message());
    return;
}
auto& nvs = *nvs_result;

// Write a value - returns idfxx::result<void>
if (auto result = nvs.try_set_value<uint32_t>("counter", 42); !result) {
    idfxx::log::error("NVS", "Failed to set value: {}", result.error().message());
    return;
}

// Commit changes to flash - returns idfxx::result<void>
if (auto result = nvs.try_commit(); !result) {
    idfxx::log::error("NVS", "Failed to commit: {}", result.error().message());
    return;
}

// Read a value
auto read_result = nvs.try_get_value<uint32_t>("counter"); // idfxx::result<uint32_t>
if (!read_result) {
    idfxx::log::error("NVS", "Failed to read value: {}", read_result.error().message());
    return;
}
idfxx::log::info("NVS", "Counter value: {}", *read_result);
```

### Storing Different Data Types

```cpp
idfxx::nvs nvs("storage", false);

// Integers (8, 16, 32, 64-bit, signed or unsigned)
nvs.set_value<uint8_t>("byte", 255);
nvs.set_value<int32_t>("temperature", -15);
nvs.set_value<uint64_t>("timestamp", 1234567890ULL);

// Strings
nvs.set_string("name", "MyDevice");

// Binary data
std::vector<uint8_t> blob{0x01, 0x02, 0x03, 0x04};
nvs.set_blob("data", blob);

// Must commit to persist changes
nvs.commit();
```

### Read-Only Mode

```cpp
// Open in read-only mode (second parameter = true)
idfxx::nvs nvs("settings", true);

// Read operations work
auto value = nvs.get_string("name");

// Write operations will throw std::system_error with read_only error
// (or use try_set_string() to return result<void> instead of throwing)
nvs.set_string("name", "NewName");  // throws!
```

### Partition-Based Usage

```cpp
#include <idfxx/nvs>
#include <idfxx/partition>

// Find and initialize a specific NVS partition
auto part = idfxx::partition::find(
    idfxx::partition::type::data,
    idfxx::partition::subtype::data_nvs);
idfxx::nvs::flash::init(part);

// Open a namespace on the specific partition
idfxx::nvs nvs(part, "settings");
nvs.set_string("key", "value");
nvs.commit();
```

### NVS Encryption Key Management

```cpp
#include <idfxx/nvs>
#include <idfxx/partition>

// Find the NVS keys partition
auto key_part = idfxx::partition::find(
    idfxx::partition::type::data,
    idfxx::partition::subtype::data_nvs_keys);

// Generate keys (or read existing ones)
auto cfg = idfxx::nvs::flash::generate_keys(key_part);

// Initialize NVS with encryption
auto nvs_part = idfxx::partition::find("nvs");
idfxx::nvs::flash::init(nvs_part, cfg);
```

## API Overview

The component provides two API styles:
- **Exception-based API** (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`) - Methods throw `std::system_error` on error
- **Result-based API** (always available) - Methods prefixed with `try_*` return `result<T>`

### Opening a Namespace

**Exception-based:**
- `nvs(namespace, read_only)` - Constructor throws on error
- `nvs(partition, namespace, read_only)` - Opens on a specific partition

**Result-based:**
- `nvs::make(namespace, read_only)` → `result<nvs>`
- `nvs::make(partition, namespace, read_only)` → `result<nvs>`

### Querying State

- `is_writeable()` → `bool` - Returns true if the handle allows writes

### Writing Data

**Exception-based:**
- `set_value<T>(key, value)` - throws on error
- `set_string(key, value)` - throws on error
- `set_blob(key, data, length)` - throws on error
- `set_blob(key, data)` - throws on error (vector overload)
- `commit()` - throws on error

**Result-based:**
- `try_set_value<T>(key, value)` → `result<void>`
- `try_set_string(key, value)` → `result<void>`
- `try_set_blob(key, data, length)` → `result<void>`
- `try_set_blob(key, data)` → `result<void>` (vector overload)
- `try_commit()` → `result<void>`

### Reading Data

**Exception-based:**
- `get_value<T>(key)` → `T` (throws on error)
- `get_string(key)` → `std::string` (throws on error)
- `get_blob(key)` → `std::vector<uint8_t>` (throws on error)

**Result-based:**
- `try_get_value<T>(key)` → `result<T>`
- `try_get_string(key)` → `result<std::string>`
- `try_get_blob(key)` → `result<std::vector<uint8_t>>`

### Erasing Data

**Exception-based:**
- `erase(key)` - throws on error
- `erase_all()` - throws on error

**Result-based:**
- `try_erase(key)` → `result<void>`
- `try_erase_all()` → `result<void>`

## Error Handling

The `idfxx::nvs::errc` enum provides NVS-specific error codes:

- `not_found` - Key doesn't exist
- `type_mismatch` - Wrong type for key
- `read_only` - Namespace opened read-only
- `not_enough_space` - Flash full
- `invalid_name` - Invalid namespace name
- `key_too_long` - Key name exceeds limit
- And more...

### Flash Management

**Exception-based:**
- `flash::init()` / `flash::init(label)` / `flash::init(partition)` - Initialize NVS
- `flash::init(cfg)` / `flash::init(partition, cfg)` - Initialize with encryption
- `flash::init(insecure)` / `flash::init(insecure, partition)` - Initialize without encryption
- `flash::deinit()` / `flash::deinit(label)` - Deinitialize NVS
- `flash::erase()` / `flash::erase(label)` / `flash::erase(partition)` - Erase NVS data
- `flash::generate_keys(partition)` → `secure_config` - Generate encryption keys
- `flash::read_security_cfg(partition)` → `secure_config` - Read encryption keys

**Result-based:**
- `flash::try_init(...)` → `result<void>` (same overloads as above)
- `flash::try_deinit(...)` → `result<void>`
- `flash::try_erase(...)` → `result<void>`
- `flash::try_generate_keys(partition)` → `result<secure_config>`
- `flash::try_read_security_cfg(partition)` → `result<secure_config>`

## Important Notes

- **Dual API Pattern**: Component provides both result-based (`try_*`) and exception-based APIs
  - Exception-based methods require `CONFIG_COMPILER_CXX_EXCEPTIONS` to be enabled
  - Use result-based API when exceptions are disabled or for explicit error handling
  - Use exception-based API for cleaner code when exceptions are available
- Changes are **not persisted** until `commit()` / `try_commit()` is called
- Namespace names are limited to 15 characters
- Key names are limited to 15 characters
- Each namespace is independent
- Call `nvs::flash::init()` or `nvs::flash::try_init()` once before using NVS
- Partition-based APIs accept `idfxx::partition` objects from `idfxx_partition`
- Supported integer types: `uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`, `uint64_t`, `int64_t`

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
