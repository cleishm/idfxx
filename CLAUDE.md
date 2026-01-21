# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

idfxx is a modern C++23 component library for ESP-IDF development. It provides zero-overhead C++ wrappers around ESP-IDF APIs with type-safe interfaces and dual error handling (both exception-based and result-based APIs).

## Build Commands

This is an ESP-IDF project. Ensure ESP-IDF 5.5+ is installed and sourced (`source $IDF_PATH/export.sh`).

```bash
# Full build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Format code
cmake --build build --target format

# Check formatting
cmake --build build --target format-check

# Generate documentation
cmake --build build --target docs
```

### QEMU Testing

For non-hardware tests, components can be tested in QEMU:

```bash
# Build for QEMU
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.qemu" build

# Run tests in QEMU
idf.py qemu monitor
```

CI runs a matrix of builds: with-exceptions, no-exceptions, and qemu-test configurations.

## Architecture

### Component Structure

All components live in `components/` with a consistent layout:
- `include/idfxx/<component>.hpp` - Public headers (may include sub-directories, e.g., `include/idfxx/spi/master.hpp`)
- `src/` - Implementation files
- `tests/` - Unity test files (run on hardware via ESP-IDF test framework)
- `idf_component.yml` - Component metadata and dependencies
- `README.md` - Component documentation following standard structure

Supported targets: esp32, esp32s2, esp32s3, esp32c3, esp32c6, esp32h2

### Core Patterns

**Dual Error Handling API**: All components provide two API styles:
- `try_*` methods return `idfxx::result<T>` (always available)
- Methods without `try_` prefix throw `std::system_error` (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)

**Result Type**: `idfxx::result<T>` is an alias for `std::expected<T, std::error_code>`. Use `std::unexpected(idfxx::errc::...)` to return errors.

**Factory Pattern**: Components use `make()` static methods returning `result<std::unique_ptr<T>>` for result-based construction.

**Type-safe Flags**: `idfxx::flags<E>` template provides bitfield operations on scoped enums with type safety.

**Memory Allocators**: `dram_allocator<T>` for Data RAM allocation.

**Error Helpers**:
- `wrap(esp_err_t)` - Converts ESP-IDF error codes to `std::error_code`
- `unwrap(result<T>)` - Extracts value or throws on error

**Guarded Operations**: Internal pattern that validates object state before making ESP-IDF calls.

**Config Structs**: Classes accept a nested `struct config` with sensible defaults for configuration:
```cpp
struct config {
    gpio reset_gpio = gpio::nc();  // optional pin
    bool mirror_x = false;
};
```

**RAII Resource Management**: Classes managing ESP-IDF resources are non-copyable and non-movable with explicit cleanup in destructors.

**Lockable Interface**: Bus classes (I2C, SPI) implement C++ Lockable (`lock()`, `try_lock()`, `unlock()`) for use with `std::lock_guard`.

**Conditional Compilation**: Throwing methods are guarded by `#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS`. SOC-specific features use `#if SOC_*` guards.

### Components

Components are organized in dependency layers. Check `components/` for the current list. Key components include:

**Base Layer:**
- `idfxx_core` - Base utilities (error handling, memory allocators, chrono, flags)

**Level 1 (depend on core):**
- `idfxx_gpio` - GPIO pin management with ISR support
- `idfxx_hw_support` - Hardware interrupt allocation

**Level 2 (depend on core + gpio):**
- `idfxx_spi` - SPI master bus driver
- `idfxx_i2c` - I2C master bus and device driver
- `idfxx_nvs` - Non-Volatile Storage wrapper

**Level 3 (depend on core + gpio + spi):**
- `idfxx_lcd` - LCD panel I/O interface

**Level 4 (depend on lcd):**
- `idfxx_lcd_ili9341` - ILI9341 LCD controller (240x320)
- `idfxx_lcd_touch` - LCD touch controller interface
- `idfxx_lcd_touch_stmpe610` - STMPE610 resistive touch driver

New components follow these patterns. Run `ls components/` to see all available components.

### ESP-IDF Integration

- Components register via `idf_component_register()` in CMakeLists.txt
- All components require C++23: `target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)`
- Default sdkconfig enables exceptions, RTTI, and C++23

## Code Style

Uses clang-format with LLVM base style:
- 4-space indentation, no tabs
- 120 character line limit
- K&R braces (attached)
- Left-aligned pointers/references (`int* ptr`)
- Include order: idfxx headers, C++ standard library, ESP-IDF headers

### Naming Conventions

- **Member variables**: underscore prefix (`_handle`, `_num`)
- **Private methods**: underscore prefix (`_probe()`)
- **Enums**: `enum class` with `snake_case` values (`mode::input`, `pull_mode::pullup`)
- **Namespaces**: nested under `idfxx` (`idfxx::gpio`, `idfxx::i2c`)
- **Constants**: `constexpr` preferred over `#define`

### Documentation

**Doxygen** with consistent tags:
- `@headerfile`, `@file`, `@brief` on all public items
- `@defgroup` / `@ingroup` for hierarchical organization
- `@retval` for documenting error conditions
- `@cond INTERNAL` / `@endcond` for hiding implementation details
- `[[nodiscard]]` attribute on all result-returning functions

**Component README** structure:
1. Title and one-line description
2. Link to Doxygen API docs
3. Features (bulleted list)
4. Requirements (ESP-IDF version, C++23, dependencies)
5. Installation (ESP-IDF Component Manager)
6. Usage (exception-based example, then result-based example)
7. API Overview (key methods summary)
8. Error Handling (component-specific error codes)
9. Important Notes (gotchas and tips)
10. License

### Testing

- Compile-time validation with `static_assert` for type properties and enum values
- Runtime tests use ESP-IDF Unity framework: `TEST_CASE("name", "[tag]")`
- Tests verify enum values match ESP-IDF constants
