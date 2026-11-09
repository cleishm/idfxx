# Contributing to idfxx

## Building

idfxx is an ESP-IDF project and needs ESP-IDF 5.5 or later. Common tasks are captured in the
`Justfile` at the repo root (`just --list` shows them all). The recipes set up the ESP-IDF
environment themselves when `idf.py` is not already on `PATH`.

```bash
just build                  # default config
just build-noexc [target]   # exceptions disabled (exercises the try_*-only API surface)
just build-noipv6 [target]  # IPv6 disabled
```

Each config builds in its own directory (`build-noexc/`, `build-noipv6/`) from its own sdkconfig
defaults, so they don't disturb the default `build/`.

## Testing

Tests use the ESP-IDF Unity framework and live alongside each component in `components/*/tests/`.
They fall into two groups:

- **Software tests** cover error handling, type safety, and logic without hardware dependencies.
- **Hardware tests**, tagged `[hw]`, exercise real peripherals (GPIO pins, NVS flash, SPI buses).

### Running Tests in QEMU

Software tests run in the QEMU ESP32-S3 emulator, without physical hardware:

```bash
just qemu-test
```

This builds an isolated QEMU image in `build-qemu/` from `sdkconfig.qemu`, runs the Unity suite,
and reports the pass/fail summary. Tests tagged `[hw]` are excluded.

### Running Tests on Hardware

The full suite, including hardware tests, runs on an ESP32-S3 development board:

```bash
idf.py set-target esp32s3
just flash [port]
```

## Continuous Integration

GitHub Actions (`.github/workflows/build.yml`) runs on every push and pull request:

- A clang-format check
- Builds for esp32s3, esp32c6, and esp32p4, each with exceptions enabled, exceptions disabled, and
  IPv6 disabled
- A compatibility build against ESP-IDF 5.5, and a canary build against the latest ESP-IDF 6.1
  release branch (allowed to fail)
- A build of every component example
- The software test suite in QEMU

## Code Style

All components share one clang-format configuration. Format code with:

```bash
just format          # or: cmake --build build --target format
```

This covers component headers, sources, and examples. Tests under `components/*/tests/` are
neither formatted nor checked, so match the surrounding style by hand.
