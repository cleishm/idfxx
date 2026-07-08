# Common tasks for idfxx. Requires ESP-IDF 5.5+ and `just`.
#
# Recipes source the ESP-IDF environment automatically when idf.py is not
# already on PATH. Override IDF_EXPORT (or set IDF_PATH) to select an
# installation; defaults to ~/.espressif/v6.0/esp-idf/export.sh.

set shell := ["bash", "-euo", "pipefail", "-c"]

idf_export := env("IDF_EXPORT", env("IDF_PATH", home_directory() / ".espressif/v6.0/esp-idf") / "export.sh")
env_setup := 'command -v idf.py >/dev/null 2>&1 || source "' + idf_export + '" >/dev/null;'
port := env("ESPPORT", "/dev/ttyUSB0")

# List available recipes
default:
    @just --list

# Build with the default config (exceptions on)
build:
    {{env_setup}} idf.py build

# Flash and monitor (port defaults to $ESPPORT or /dev/ttyUSB0)
flash port=port:
    {{env_setup}} idf.py -p {{port}} flash monitor

# Monitor only
monitor port=port:
    {{env_setup}} idf.py -p {{port}} monitor

# Ensure build/ is configured, for cmake-driven targets
[private]
configure:
    {{env_setup}} [ -f build/CMakeCache.txt ] || idf.py reconfigure

# Apply clang-format to all sources
format: configure
    {{env_setup}} cmake --build build --target format

# Check formatting without modifying files
format-check: configure
    {{env_setup}} cmake --build build --target format-check

# Generate Doxygen documentation
docs: configure
    {{env_setup}} cmake --build build --target docs

# Isolated no-exceptions build — the only config that exercises the try_*-only API surface
build-noexc target="esp32s3":
    #!/usr/bin/env bash
    set -euo pipefail
    {{env_setup}}
    cd "{{justfile_directory()}}"
    if [ ! -f build-noexc/sdkconfig ]; then
        idf.py -B build-noexc -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.no-exceptions" \
            -D SDKCONFIG="{{justfile_directory()}}/build-noexc/sdkconfig" set-target {{target}}
    fi
    idf.py -B build-noexc build

# Isolated no-ipv6 build
build-noipv6 target="esp32s3":
    #!/usr/bin/env bash
    set -euo pipefail
    {{env_setup}}
    cd "{{justfile_directory()}}"
    if [ ! -f build-noipv6/sdkconfig ]; then
        idf.py -B build-noipv6 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.no-ipv6" \
            -D SDKCONFIG="{{justfile_directory()}}/build-noipv6/sdkconfig" set-target {{target}}
    fi
    idf.py -B build-noipv6 build

# Build the QEMU test image (isolated build dir + merged 4MB flash image)
qemu-build:
    #!/usr/bin/env bash
    set -euo pipefail
    {{env_setup}}
    cd "{{justfile_directory()}}"
    mkdir -p build-qemu
    if [ ! -f build-qemu/sdkconfig ] || [ sdkconfig.qemu -nt build-qemu/sdkconfig ]; then
        cp sdkconfig.qemu build-qemu/sdkconfig
    fi
    idf.py -B build-qemu -D SDKCONFIG="{{justfile_directory()}}/build-qemu/sdkconfig" -D IDF_TARGET=esp32s3 build
    cd build-qemu
    esptool.py --chip esp32s3 merge_bin --fill-flash-size 4MB -o qemu_flash.bin @flash_args

# Run the Unity test suite in QEMU (builds first; log in qemu_output.log)
qemu-test timeout="300": qemu-build
    #!/usr/bin/env bash
    set -euo pipefail
    {{env_setup}}
    cd "{{justfile_directory()}}"
    if ! command -v qemu-system-xtensa >/dev/null 2>&1; then
        qemu_bin=$(ls -d "$HOME"/.espressif/tools/qemu-xtensa/*/qemu/bin 2>/dev/null | tail -1)
        [ -n "$qemu_bin" ] && export PATH="$PATH:$qemu_bin"
    fi
    pipe=$(mktemp -u)
    mkfifo "$pipe"
    qemu-system-xtensa -machine esp32s3 -nographic \
        -drive file=build-qemu/qemu_flash.bin,if=mtd,format=raw > "$pipe" 2>&1 &
    qpid=$!
    ( sleep {{timeout}}; kill "$qpid" 2>/dev/null ) & wpid=$!
    : > qemu_output.log
    while IFS= read -r line; do
        printf '%s\n' "$line" >> qemu_output.log
        printf '%s\n' "$line"
        case "$line" in *"### TESTS COMPLETE ###"*) break ;; esac
    done < "$pipe"
    kill "$qpid" 2>/dev/null || true
    pkill -P "$wpid" 2>/dev/null || true
    kill "$wpid" 2>/dev/null || true
    wait "$qpid" 2>/dev/null || true
    rm -f "$pipe"
    summary=$(grep -Eo '[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored' qemu_output.log | tail -1 || true)
    if [ -z "$summary" ]; then
        echo "ERROR: no Unity test summary found (timed out or crashed) — see qemu_output.log"
        exit 1
    fi
    echo "Test summary: $summary"
    failures=$(echo "$summary" | awk '{print $3}')
    [ "$failures" -eq 0 ] && echo "All tests passed" || { echo "$failures test(s) failed"; exit 1; }

# Remove all build directories
clean:
    rm -rf build build-noexc build-noipv6 build-qemu qemu_output.log
