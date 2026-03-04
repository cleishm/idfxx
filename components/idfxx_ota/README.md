# idfxx_ota

OTA firmware update session management with rollback support.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__ota.html)**

## Features

- RAII OTA update sessions with automatic abort on destruction
- Boot partition control and round-robin partition selection
- Image state management and rollback support
- Application description access (version, project name, build info)
- Resumable OTA updates for interrupted transfers
- Final partition redirection for multi-stage updates
- Domain-specific error codes

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_ota:
    version: "^0.9.0"
```

Or add `idfxx_ota` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic OTA Update

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/ota>

try {
    // Find the next OTA partition to update
    auto part = idfxx::ota::next_update_partition();

    // Begin the update (erases the partition)
    idfxx::ota::update upd(part);

    // Write firmware data in chunks
    while (auto chunk = receive_next_chunk()) {
        upd.write(chunk.data(), chunk.size());
    }

    // Validate and finalize the update
    upd.end();

    // Set the new partition as the boot partition
    idfxx::ota::set_boot_partition(part);

    // Reboot to the new firmware
    esp_restart();

} catch (const std::system_error& e) {
    // Handle error
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/ota>

// Find the next OTA partition
auto part_result = idfxx::ota::try_next_update_partition();
if (!part_result) {
    return;
}
auto& part = *part_result;

// Begin the update
auto upd_result = idfxx::ota::update::make(part);
if (!upd_result) {
    return;
}
auto& upd = *upd_result;

// Write firmware data
while (auto chunk = receive_next_chunk()) {
    if (auto r = upd.try_write(chunk.data(), chunk.size()); !r) {
        return; // Destructor will abort the update
    }
}

// Validate and finalize
if (auto r = upd.try_end(); !r) {
    return;
}

// Set boot partition and reboot
if (auto r = idfxx::ota::try_set_boot_partition(part); !r) {
    return;
}
esp_restart();
```

### Rollback Support

```cpp
#include <idfxx/ota>

// On first boot of new firmware, mark as valid
idfxx::ota::mark_valid();

// Or if the new firmware is broken, roll back
if (idfxx::ota::rollback_possible()) {
    idfxx::ota::mark_invalid_and_rollback(); // Reboots on success
}
```

### Reading App Description

```cpp
#include <idfxx/ota>

auto part = idfxx::ota::running_partition();
auto desc = idfxx::ota::partition_description(part);

auto version = desc.version();       // e.g., "1.2.3"
auto project = desc.project_name();  // e.g., "my-firmware"
auto idf = desc.idf_ver();           // e.g., "v5.5"
```

## API Overview

The component provides two API styles:
- **Exception-based API** (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`) - Methods throw `std::system_error` on error
- **Result-based API** (always available) - Methods prefixed with `try_*` return `result<T>`

### OTA Update Session

**Exception-based:**
- `update(partition, image_size)` - Constructor begins an update
- `update::resume(partition, offset, erase_size)` - Resume interrupted update

**Result-based:**
- `update::make(partition, image_size)` → `result<update>`
- `update::try_resume(partition, offset, erase_size)` → `result<update>`

**Session methods (both APIs):**
- `write(data, size)` / `try_write(data, size)` - Sequential write
- `write(span)` / `try_write(span)` - Sequential write (span overload)
- `write_with_offset(offset, data, size)` / `try_write_with_offset(...)` - Random-access write
- `end()` / `try_end()` - Validate and finalize
- `try_abort()` - Cancel without validation
- `set_final_partition(part, copy)` / `try_set_final_partition(...)` - Redirect final partition

### Boot Partition Management

- `set_boot_partition(part)` / `try_set_boot_partition(part)` - Set next boot partition
- `boot_partition()` / `try_boot_partition()` → `partition` - Get configured boot partition
- `running_partition()` / `try_running_partition()` → `partition` - Get running app partition
- `next_update_partition()` / `try_next_update_partition()` → `partition` - Get next OTA slot

### App Description

- `partition_description(part)` / `try_partition_description(part)` → `app_description`

### State Management

- `mark_valid()` / `try_mark_valid()` - Confirm current app is working
- `mark_invalid_and_rollback()` / `try_mark_invalid_and_rollback()` - Trigger rollback with reboot
- `partition_state(part)` / `try_partition_state(part)` → `image_state`
- `rollback_possible()` → `bool` - Check if rollback is available

### Partition Maintenance

- `erase_last_boot_app_partition()` / `try_erase_last_boot_app_partition()` - Erase previous boot partition
- `invalidate_inactive_ota_data_slot()` / `try_invalidate_inactive_ota_data_slot()` - Invalidate OTA data
- `last_invalid_partition()` / `try_last_invalid_partition()` → `partition`
- `app_partition_count()` → `size_t`

## Error Handling

The `idfxx::ota::errc` enum provides OTA-specific error codes:

- `partition_conflict` - Cannot update the currently running partition
- `select_info_invalid` - OTA data partition contains invalid content
- `validate_failed` - OTA app image is invalid
- `small_sec_ver` - Firmware secure version too low
- `rollback_failed` - No valid firmware for rollback
- `rollback_invalid_state` - Running app pending verification

## Important Notes

- **Dual API Pattern**: Component provides both result-based (`try_*`) and exception-based APIs
  - Exception-based methods require `CONFIG_COMPILER_CXX_EXCEPTIONS` to be enabled
  - Use result-based API when exceptions are disabled or for explicit error handling
  - Use exception-based API for cleaner code when exceptions are available
- The `update` destructor automatically aborts an unfinished session
- Call `mark_valid()` early in a new firmware's lifecycle to prevent automatic rollback
- `mark_invalid_and_rollback()` reboots the device on success and does not return
- Use `sequential_erase` as the image_size when the total size is unknown and writes are sequential
- `next_update_partition()` uses round-robin selection starting from the running partition

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
