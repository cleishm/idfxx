# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
repository.

## Project Overview

idfxx is a modern C++23 component library for ESP-IDF development. It provides type-safe interfaces
with dual error handling (both exception-based and result-based APIs).

## Build Commands

This is an ESP-IDF project, requring ESP-IDF 5.5+.

Common tasks are captured in the `Justfile` at the repo root (`just --list` to see all recipes). The
recipes source the ESP-IDF environment automatically when `idf.py` is not already on PATH, so they
work from a plain shell.

### Config Matrix Builds

The sdkconfig fragments and partition tables live in `config/`, and each build dir holds its own
generated `sdkconfig` (`build/sdkconfig` for the default build), so `idf.py fullclean` discards
`menuconfig` changes; carry lasting ones into a `config/` fragment.

CI builds a matrix of configs. The no-exceptions config is the only one that exercises the
`try_*`-only API surface. Each uses an isolated build dir with its own `SDKCONFIG` (a plain
`-D SDKCONFIG_DEFAULTS` on the existing `build/` does not switch configs):

```bash
just build-noexc [target]                   # config/sdkconfig.defaults;config/sdkconfig.no-exceptions
just build-noipv6 [target]                  # config/sdkconfig.defaults;config/sdkconfig.no-ipv6
```

### QEMU Testing

For non-hardware tests, components can be tested in QEMU. `just qemu-test` replicates the CI
qemu-test job locally (isolated `build-qemu/` from `config/sdkconfig.qemu`, merged 4MB flash image,
runs the Unity suite watching for the `### TESTS COMPLETE ###` sentinel, then parses the summary):

```bash
# Build the QEMU flash image
just qemu-build

# Build (if needed) and run the Unity suite in QEMU
just qemu-test [timeout]

# Build and run only some components' tests (isolated build-test/; seconds per rebuild)
just test dht gpio
```

A component whose tests use idfxx components outside its own dependencies lists them in
`components/<name>/tests/requires.cmake` (`set(test_requires idfxx_task)`); otherwise
`just test <name>` fails to build while the full suite still passes.

CI runs a matrix of builds: with-exceptions, no-exceptions, no-ipv6, and qemu-test configurations.

## Architecture

### Component Structure

Supported targets: esp32, esp32s2, esp32s3, esp32c3, esp32c6, esp32h2

### Core Patterns

**Dual Error Handling API**: All components provide two API styles:
- `try_*` methods return `idfxx::result<T>` (always available)
- Methods without `try_` prefix throw `std::system_error` (requires
  `CONFIG_COMPILER_CXX_EXCEPTIONS`)

**Result Type**: `idfxx::result<T>` is an alias for `std::expected<T, std::error_code>`. Use
`idfxx::error(idfxx::errc::...)` to return errors.

**Factory Pattern**: Components use `make()` static methods returning `result<T>` for result-based
construction.

**Type-safe Flags**: `idfxx::flags<E>` template provides bitfield operations on scoped enums with
type safety.

**Memory Allocators**: `dram_allocator<T>` for Data RAM allocation.

**Error Helpers**:
- `error(errc)` - Creates `std::unexpected` from an error code enum for returning from result
  functions
- `error(esp_err_t)` - Creates `std::unexpected` from an ESP-IDF error code
- `error(std::error_code)` - Creates `std::unexpected` from a `std::error_code` (for propagating
  errors between result types)
- `wrap(esp_err_t)` - Converts ESP-IDF error codes to `result<void>`
- `unwrap(result<T>)` - Extracts value or throws `std::system_error` on error. Public: lets users
  give their own result-returning code a throwing variant, or call result-only APIs from
  exception-style code (see documentation note below)

**Guarded Operations**: Internal pattern that validates object state before making ESP-IDF calls.

**Config Structs**: Classes accept a nested `struct config` with sensible defaults for
configuration:
```cpp
struct config {
    gpio reset_gpio = gpio::nc();  // optional pin
    bool mirror_x = false;
};
```

**RAII Resource Management**: Classes managing ESP-IDF resources are non-copyable and move-only,
with explicit cleanup in destructors that check for moved-from state. Parent-child relationships
(e.g., bus-device) use references in the public API; the caller ensures parents outlive children.

**Lockable Interface**: Bus classes (I2C, SPI) implement C++ Lockable (`lock()`, `try_lock()`,
`unlock()`) for use with `std::lock_guard`.

**Conditional Compilation**: Throwing methods are guarded by
`#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS`. SOC-specific features use `#if SOC_*` guards.

**Byte Buffers** (`<idfxx/byte_view>`):
- Bytes going into an API are an `idfxx::byte_view` parameter; a buffer the API fills is an
  `idfxx::byte_span`. Both accept `std::byte` and `uint8_t` ranges and `{ptr, len}`, so
  don't add pointer-length or second span-type overloads.
- Extra overloads only where they're the common case: `std::string_view` (and `std::span<char>`
  for reads) where callers pass text, `std::initializer_list<uint8_t>` where they write literal
  bytes (bus and register writes), and scalar single-byte forms. Never store an
  `initializer_list`, and don't give a view a braced-list constructor: a stored view would
  dangle.
- Read into the caller's buffer. Allocate and return a container only when the caller can't know
  the size up front; for small fixed reads add `template<size_t N> requires(N > 0)` forms returning
  `std::array`.
- What the API hands back: opaque data (payloads, file and blob contents, keys, digests) is
  `std::byte`, values read as numbers (register reads) are `uint8_t`. Views into the API's own
  storage are `byte_view`; the filled prefix of a caller's buffer is a `byte_span`.
  SHA-256 digests are `std::array<std::byte, 32>`.
- Struct fields holding caller buffers are views declared `= {}`. Generic code reads an
  `idfxx::byte_source` through a `byte_span`.

## Code Requirements

### Line Width

Code, comments and Markdown prose all wrap at 100 columns. clang-format enforces it for C++
(`ColumnLimit: 100`). In Markdown, hard-wrap paragraphs and list items at 100, indenting list
continuations to the item's content; code blocks, tables, headings and unbreakable tokens (URLs,
long inline code) may run over. Trailing comments in a struct or enum share one aligned column; when
any trailing member doc (`///<`) can't fit in that column, document every member of the scope with
leading `///` comments instead.

### Naming Conventions

- **Member variables**: underscore prefix (`_handle`, `_num`)
- **Private methods**: underscore prefix (`_probe()`)
- **Enums**: `enum class` with `snake_case` values (`mode::input`, `pull_mode::pullup`)
- **Namespaces**: nested under `idfxx` (`idfxx::gpio`, `idfxx::i2c`)
- **Constants**: `constexpr` preferred over `#define`

### Documentation

**Focus on API purpose, not implementation**: Documentation should describe what the API provides
and how to use it, not how it's implemented or what it wraps. Avoid phrases like "C++ wrapper for
ESP-IDF X" or "wraps the underlying Y API". Instead, describe functionality from the user's
perspective. Implementation details are appropriate only when exposed through the API (e.g.,
`idf_handle()` methods).

**American English**: All wording uses American spelling: docs, comments, READMEs, examples, log
strings and identifiers (`color`, `gray`, `center`, `behavior`, `initialize`, `canceled`, `labeled`,
`toward`, `afterward`). Third-party symbols keep their own spelling (e.g. the `epaper-image`
library's `epi_colour_panel_init`, `epi_grey_panel_init`).

**Method Ordering**: Within each functional group (construction, listener registration, event
posting, etc.), methods must be ordered consistently:
1. Exception-based methods (guarded by `#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS`) appear first
2. Result-based (`try_*`) methods follow immediately after

This ordering applies to both declarations and implementations, ensuring a predictable API structure
across all components.

**Documentation Completeness**: All public methods require complete Doxygen documentation:
- **Brief description** (`@brief`) explaining the method's purpose
- **Template parameters** (`@tparam`) for all template types with clear descriptions
- **Parameters** (`@param`) for every parameter, explaining purpose and valid values
- **Return documentation** (`@return` or `@retval`) describing success values and error conditions
- **Exception documentation** (`@throws`) for exception-based methods, specifying thrown types
- **Availability notes** (`@note`) indicating when methods are conditionally compiled (e.g.,
  `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- **Usage examples** (`@code`/`@endcode`) for complex or non-obvious APIs

Exception-based methods should not have abbreviated documentation. Inline implementations within
class definitions still require full documentation blocks.

**Documentation Examples**: Use the exception-based API (methods without `try_` prefix) in
documentation examples, as it's cleaner and more readable. Do not use `unwrap()` in examples where a
throwing method exists - call the throwing method instead. `unwrap()` is public (for users' own
result-based code), but it is not how idfxx APIs are meant to be called.

**Doxygen** with consistent tags:
- `@headerfile`, `@file`, `@brief` on all public items
- `@defgroup` / `@ingroup` for hierarchical organization
- `@retval` for documenting error conditions
- `@cond INTERNAL` / `@endcond` for hiding implementation details
- `[[nodiscard]]` attribute on result-returning functions, unless intentionally omitted to allow
  fire-and-forget usage. Omit `[[nodiscard]]` on cleanup/teardown operations that are unlikely to
  fail in normal use (e.g., deregister, deinit, stop) where callers reasonably don't need to check
  the result. Keep `[[nodiscard]]` on creation, initialization, and operations where failure
  indicates a real problem the caller should handle.

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

**Example README**: Every example under `components/*/examples/` needs a `README.md` (the ESP
Component Registry displays it). Follow `components/idfxx_gpio/examples/blink/README.md`:
1. Title and one-line summary
2. What it demonstrates (APIs used, in order; which API style)
3. Hardware required (parts, wiring/pins, where to change them)
4. Configuration (only if the user must set something first)
5. How to use (`idf.py create-project-from-example "cleishm/<component>:<example>"`, then set-target
   and flash)

No supported-targets table or example output (both go stale); note target restrictions in Hardware
required.

### Testing

- Compile-time validation with `static_assert` for type properties and enum values
- Runtime tests use ESP-IDF Unity framework: `TEST_CASE("name", "[tag]")`
- Tests verify enum values match ESP-IDF constants
- **Do not use GPIO 19 or GPIO 20 in tests** — these are the USB D-/D+ pins on ESP32-S3 and
  configuring them for peripheral output (e.g., LEDC) hangs when USB-JTAG/Serial is active
- **Do not use GPIO 26–32 in tests on ESP32-S3 modules with embedded flash/PSRAM** — these pins are
  connected to the SPI flash/PSRAM. Driving them from a peripheral hijacks the flash CS/CLK/data
  lines; the next IROM fetch then reads 0xFF from the disabled flash, poisoning ICache and causing
  IllegalInstruction crashes that look like cache-coherency bugs (see espressif/esp-idf#18518). GPIO
  26 is the most commonly mistaken pin (it's the flash CS line)
