# Contributing to idfxx

## Building

idfxx is an ESP-IDF project and needs ESP-IDF 5.5 or later. Common tasks are captured in the
`Justfile` at the repo root (`just --list` shows them all). The recipes set up the ESP-IDF
environment themselves when `idf.py` is not already on `PATH`.

```bash
just build [group...] [target]         # default config
just build-noexc [group...] [target]   # exceptions disabled (exercises the try_*-only API surface)
just build-noipv6 [group...] [target]  # IPv6 disabled
```

What these build is the test suite, which is split into groups with one app each (see
[Test Groups](#test-groups)). They build every group unless given group names, for esp32s3 unless
given another target. Each config and group builds in its own directory (`build-<group>/`,
`build-noexc-<group>/`, `build-noipv6-<group>/`) from its own sdkconfig defaults. The defaults and
partition tables live in `config/`; the generated `sdkconfig` lives in the build directory, so
`idf.py fullclean` discards `menuconfig` changes.

The recipes compile through [ccache](https://ccache.dev) when it is installed (e.g.
`brew install ccache`), which makes fresh build directories and config switches much faster. Set
`IDF_CCACHE_ENABLE=0` to opt out.

## Testing

Tests use the ESP-IDF Unity framework and live alongside each component in `components/*/tests/`.
They fall into two groups:

- **Software tests** cover error handling, type safety, and logic without hardware dependencies.
- **Hardware tests**, tagged `[hw]`, exercise real peripherals (GPIO pins, NVS flash, SPI buses).

### Test Groups

All the tests together don't fit in one app, so the suite is split into groups, each built as its
own Unity app:

- `system`: the core library, FreeRTOS primitives, events, timers, GPIO and peripherals, sleep,
  storage (NVS, partitions, OTA, SD cards, FAT), buses and the sensor, RTC and radio drivers
- `network`: networking, Wi-Fi, HTTP clients and servers, and cryptography
- `display`: panel I/O, LCD and e-paper panels and drivers, touch, graphics, fonts and images

`tests/groups.cmake` lists each group's components. Every component with tests must be in exactly
one group, and configuring fails otherwise, so a new component needs adding there. Pick the group
its tests are closest to. If a group's app outgrows the `ota_0` partition, rebalance or split the
groups rather than growing the partition.

Because each app holds only its group's tests, a test can't rely on another component's tests having
run first. Set up what a test needs, such as NVS or the default event loop, in the test itself.

`-D IDFXX_TEST_GROUP=<group>` selects a group when invoking `idf.py` directly (in a build directory
of its own). A plain `idf.py reconfigure` with no selection, which is enough for the `format`,
`format-check` and `docs` targets, configures an app holding every group's tests, which need not fit
the `ota_0` partition.

### Running Tests in QEMU

Software tests run in the QEMU ESP32-S3 emulator, without physical hardware:

```bash
just qemu-test [group...] [timeout]
```

For each group (every group unless given names), this builds an isolated QEMU image in
`build-qemu-<group>/` from `config/sdkconfig.qemu`, runs the Unity suite, and reports the pass/fail
summary, logging the output to `qemu_output_<group>.log`. The timeout applies to each app and
defaults to 300 seconds. It fails if any group has failures or doesn't finish. Tests tagged `[hw]`
are excluded.

While working on a component, run just its tests instead (the `idfxx_` prefix is optional):

```bash
just test dht gpio
```

This builds only the named components' tests, and the components they depend on, in `build-test/`.
The image is a fraction of the full suite's size, so rebuilds after an edit take seconds. When a
component's tests use other idfxx components it doesn't depend on itself, list them in
`components/<name>/tests/requires.cmake`:

```cmake
set(test_requires idfxx_task)
```

### Running Tests on Hardware

The full suite, including hardware tests, runs on an ESP32-S3 development board, one group's app at
a time:

```bash
just flash system [port]
just flash network [port]
just flash display [port]
```

Each builds the group's app in `build-<group>/` if needed, flashes it and opens the monitor. Every
app prints its Unity summary at the end, and the hardware apps reprint it every five seconds, since
the light-sleep tests (in `system`) can drop a USB-Serial-JTAG console.

## Continuous Integration

GitHub Actions (`.github/workflows/build.yml`) runs on every push and pull request:

- A clang-format check
- Builds for esp32, esp32s3, esp32c6, and esp32p4, each with exceptions enabled, exceptions
  disabled, and IPv6 disabled
- A compatibility build against ESP-IDF 5.5, and a canary build against the latest ESP-IDF 6.1
  release branch (allowed to fail)
- A build of every component example
- The software test suite in QEMU

Each build job builds every test group's app, one after another.

## Code Style

All components share one clang-format configuration. Format code with:

```bash
just format          # or: cmake --build build --target format
```

This covers component headers, sources, and examples. Tests under `components/*/tests/` are neither
formatted nor checked, so match the surrounding style by hand.
