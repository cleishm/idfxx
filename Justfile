# Common tasks for idfxx. Requires ESP-IDF 5.5+ and `just`.
#
# Recipes activate ESP-IDF automatically when idf.py is not already on PATH,
# searching $IDF_EXPORT, $IDF_PATH, the ESP-IDF Installation Manager (eim), and
# then the usual checkout locations. See scripts/idf-env.sh for the full order
# and for how to point the recipes at a specific installation.

set shell := ["bash", "-euo", "pipefail", "-c"]

env_setup := 'source "' + justfile_directory() / "scripts/idf-env.sh" + '";'
port := env("ESPPORT", "/dev/ttyUSB0")

# List available recipes
default:
    @just --list

# The test suite is split into groups, one Unity app each, listed in tests/groups.cmake. Recipes
# that build test apps take group names (default: every group) and, where noted, a target.
groups := `sed -n 's/^set(IDFXX_TEST_GROUPS \(.*\))$/\1/p' tests/groups.cmake`

# Build test apps with the default config into build-<group>/ (`just build [group...] [target]`)
build *args:
    @just build-config build "config/sdkconfig.defaults" {{args}}

# Build, flash and monitor a group's test app (port defaults to $ESPPORT or /dev/ttyUSB0)
flash group port=port:
    @just build {{group}}
    {{env_setup}} idf.py -B build-{{group}} -p {{port}} flash monitor

# Monitor a group's test app (port defaults to $ESPPORT or /dev/ttyUSB0)
monitor group port=port:
    {{env_setup}} idf.py -B build-{{group}} -p {{port}} monitor

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

# No-exceptions builds into build-noexc-<group>/: the only config exercising the try_*-only API
build-noexc *args:
    @just build-config build-noexc \
        "config/sdkconfig.defaults;config/sdkconfig.no-exceptions" {{args}}

# No-IPv6 builds into build-noipv6-<group>/ (`just build-noipv6 [group...] [target]`)
build-noipv6 *args:
    @just build-config build-noipv6 "config/sdkconfig.defaults;config/sdkconfig.no-ipv6" {{args}}

# Build test apps from sdkconfig defaults into <prefix>-<group>/. A build dir is configured for the
# target named in args (esp32s3 when none is) the first time, and again when a different one is.
[private]
build-config prefix defaults *args:
    #!/usr/bin/env bash
    set -euo pipefail
    {{env_setup}}
    cd "{{justfile_directory()}}"
    selected= target=
    for a in {{args}}; do
        case " {{groups}} " in *" $a "*) selected="$selected $a" ;; *) target=$a ;; esac
    done
    selected=${selected:-"{{groups}}"}
    [ -n "$selected" ] || { echo "ERROR: no test groups found in tests/groups.cmake"; exit 1; }
    for g in $selected; do
        dir={{prefix}}-$g
        configured=$(sed -n 's/^CONFIG_IDF_TARGET="\(.*\)"$/\1/p' "$dir/sdkconfig" 2>/dev/null || true)
        if [ -z "$configured" ] || { [ -n "$target" ] && [ "$target" != "$configured" ]; }; then
            idf.py -B "$dir" -D IDFXX_TEST_GROUP="$g" -D SDKCONFIG_DEFAULTS="{{defaults}}" \
                -D SDKCONFIG="$PWD/$dir/sdkconfig" set-target "${target:-esp32s3}"
        fi
        idf.py -B "$dir" build
    done

# Build QEMU test images into build-qemu-<group>/ (`just qemu-build [group...]`)
qemu-build *groups_:
    #!/usr/bin/env bash
    set -euo pipefail
    {{env_setup}}
    cd "{{justfile_directory()}}"
    selected="{{groups_}}"
    selected=${selected:-"{{groups}}"}
    [ -n "$selected" ] || { echo "ERROR: no test groups found in tests/groups.cmake"; exit 1; }
    for g in $selected; do
        case " {{groups}} " in *" $g "*) ;; *) echo "ERROR: no test group named $g"; exit 1 ;; esac
    done
    for g in $selected; do
        dir=build-qemu-$g
        mkdir -p "$dir"
        if [ ! -f "$dir/sdkconfig" ] || [ config/sdkconfig.qemu -nt "$dir/sdkconfig" ]; then
            cp config/sdkconfig.qemu "$dir/sdkconfig"
        fi
        args=(-B "$dir" -D SDKCONFIG="$PWD/$dir/sdkconfig" -D IDF_TARGET=esp32s3)
        idf.py "${args[@]}" -D IDFXX_TEST_GROUP="$g" build
        idf.py "${args[@]}" merge-bin --format raw --fill-flash-size 4MB \
            -o "$PWD/$dir/qemu_flash.bin"
    done

# Logs go to qemu_output_<group>.log; the timeout is per app, in seconds (default 300).
# Build and run the Unity suite in QEMU per group (`just qemu-test [group...] [timeout]`)
qemu-test *args:
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{justfile_directory()}}"
    selected= timeout=300
    for a in {{args}}; do
        case "$a" in *[!0-9]*) selected="$selected $a" ;; *) timeout=$a ;; esac
    done
    selected=${selected:-"{{groups}}"}
    just qemu-build $selected
    failed=
    for g in $selected; do
        echo "=== Test group: $g"
        just qemu-run "build-qemu-$g" "$timeout" "qemu_output_$g.log" || failed="$failed $g"
    done
    if [ -n "$failed" ]; then
        echo "Failed groups:$failed"
        exit 1
    fi
    echo "All groups passed:" $selected

# Run only some components' tests in QEMU (`just test dht gpio`; isolated build-test/)
test +components:
    #!/usr/bin/env bash
    set -euo pipefail
    {{env_setup}}
    cd "{{justfile_directory()}}"
    tests=
    for c in {{components}}; do
        case "$c" in idfxx_*) ;; *) c="idfxx_$c" ;; esac
        [ -d "components/$c" ] || { echo "ERROR: no component named $c"; exit 1; }
        tests="${tests:+$tests;}$c"
    done
    mkdir -p build-test
    # config/sdkconfig.qemu supplies the values for options that are new to the
    # filtered set; regenerate the config when it changes.
    if [ -f build-test/sdkconfig ] && [ config/sdkconfig.qemu -nt build-test/sdkconfig ]; then
        rm build-test/sdkconfig
    fi
    args=(-B build-test -D SDKCONFIG="$PWD/build-test/sdkconfig" -D SDKCONFIG_DEFAULTS=config/sdkconfig.qemu)
    idf.py "${args[@]}" -D IDF_TARGET=esp32s3 -D IDFXX_TESTS="$tests" build
    idf.py "${args[@]}" merge-bin --format raw --fill-flash-size 4MB -o "$PWD/build-test/qemu_flash.bin"
    just qemu-run build-test 120

# Run a merged QEMU flash image from a build dir and check the Unity summary
[private]
qemu-run dir timeout log="qemu_output.log":
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
        -drive file={{dir}}/qemu_flash.bin,if=mtd,format=raw > "$pipe" 2>&1 &
    qpid=$!
    ( sleep {{timeout}}; kill "$qpid" 2>/dev/null ) & wpid=$!
    : > "{{log}}"
    while IFS= read -r line; do
        printf '%s\n' "$line" >> "{{log}}"
        printf '%s\n' "$line"
        case "$line" in *"### TESTS COMPLETE ###"*) break ;; esac
    done < "$pipe"
    kill "$qpid" 2>/dev/null || true
    pkill -P "$wpid" 2>/dev/null || true
    kill "$wpid" 2>/dev/null || true
    wait "$qpid" 2>/dev/null || true
    rm -f "$pipe"
    summary=$(grep -Eo '[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored' "{{log}}" | tail -1 || true)
    if [ -z "$summary" ]; then
        echo "ERROR: no Unity test summary found (timed out or crashed) — see {{log}}"
        exit 1
    fi
    echo "Test summary: $summary"
    failures=$(echo "$summary" | awk '{print $3}')
    [ "$failures" -eq 0 ] && echo "All tests passed" || { echo "$failures test(s) failed"; exit 1; }

# Regenerate idfxx_font_inter's sources from its TTF (needs freetype-py in $IDFXX_FONT_PYTHON)
gen-font-inter:
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{justfile_directory()}}/components/idfxx_font_inter"
    for size in 16 24 32 48; do
        "${IDFXX_FONT_PYTHON:-python3}" ../idfxx_font/scripts/ttf_to_c.py \
            fonts/Inter-Regular.ttf "inter_$size" --size "$size" \
            --header idfxx/font/inter.hpp --namespace idfxx::font -o "src/inter_$size.cpp"
    done

# Remove all build directories
clean:
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{justfile_directory()}}"
    for g in {{groups}}; do
        rm -rf "build-$g" "build-noexc-$g" "build-noipv6-$g" "build-qemu-$g"
    done
    rm -rf build build-noexc build-noipv6 build-qemu build-test qemu_output*.log
