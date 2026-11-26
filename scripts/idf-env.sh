# Locate and activate an ESP-IDF installation.
#
# Source this file (it cannot be executed) to make `idf.py` available in the
# current shell. The Justfile recipes do this for you; run it by hand with:
#
#     . scripts/idf-env.sh
#
# It is a no-op when idf.py is already on PATH. Otherwise it searches, in
# order:
#
#   1. $IDF_EXPORT - an export.sh or eim activation script named explicitly
#   2. $IDF_PATH   - an ESP-IDF checkout named in the environment
#   3. ESP-IDF Installation Manager (eim) installs under $IDF_TOOLS_PATH and
#      ~/.espressif/tools, highest version first
#   4. Common checkout locations: ~/esp/esp-idf, ~/esp/*/esp-idf, ~/esp-idf,
#      ~/.espressif/*/esp-idf, /opt/esp/idf
#
# idfxx requires ESP-IDF 5.5 or later. The version is not checked here. An
# older installation fails during the build instead.

_idfxx_ready() {
    command -v idf.py >/dev/null 2>&1
}

# eim ships an activation script for each install, and an export.sh that does
# not work on its own. A plain checkout has only export.sh. The two kinds are
# activated differently. Tell them apart by the environment block that eim
# embeds in its activation scripts.
_idfxx_is_eim() {
    grep -q '^IDF_PYTHON_ENV_PATH:' "$1" 2>/dev/null
}

# Source an export.sh. These scripts do not work under `set -e`/`set -u`, so
# both options are turned off while the script runs and restored afterward.
_idfxx_use_export() {
    [ -r "$1" ] || return 1
    _idfxx_opts=$-
    set +eu
    # shellcheck disable=SC1090
    . "$1" >/dev/null 2>&1
    case $_idfxx_opts in *e*) set -e ;; esac
    case $_idfxx_opts in *u*) set -u ;; esac
    _idfxx_ready
}

# Apply an eim activation script. Sourcing one aborts the shell unless $0 names
# a shell. Inside just's recipe scripts, $0 does not name a shell. So run the
# script to print its environment (-e), and apply that environment instead.
_idfxx_use_eim() {
    [ -r "$1" ] || return 1
    _idfxx_env=$(sh "$1" -e 2>/dev/null) || return 1
    _idfxx_tools_path=
    while IFS= read -r _idfxx_kv; do
        case $_idfxx_kv in
            # the toolchain directories, applied below
            PATH=*) _idfxx_tools_path=${_idfxx_kv#PATH=} ;;
            # a copy of the PATH of the shell that ran the install. This script
            # does not restore it.
            SYSTEM_PATH=*) ;;
            *=*) export "$_idfxx_kv" ;;
        esac
    done <<EOF
$_idfxx_env
EOF
    [ -n "${IDF_PATH:-}" ] && [ -n "${IDF_PYTHON_ENV_PATH:-}" ] || return 1
    # eim makes idf.py available through a shell function. Put it on PATH
    # instead, so that child processes can also run it. Put it ahead of the
    # /usr/bin that eim's PATH starts with, so that the `#!/usr/bin/env python`
    # shebang finds the ESP-IDF virtualenv.
    PATH="$IDF_PYTHON_ENV_PATH/bin:$IDF_PATH/tools:${_idfxx_tools_path#/usr/bin:}:$PATH"
    export PATH
    _idfxx_ready
}

# Activate whichever kind of script this is.
_idfxx_use_script() {
    if _idfxx_is_eim "$1"; then
        _idfxx_use_eim "$1"
    else
        _idfxx_use_export "$1"
    fi
}

# Print the eim activation scripts, highest version first.
_idfxx_eim_scripts() {
    for _idfxx_file in "${IDF_TOOLS_PATH:-$HOME/.espressif/tools}"/activate_idf_*.sh \
        "$HOME"/.espressif/tools/activate_idf_*.sh; do
        [ -r "$_idfxx_file" ] && printf '%s\n' "$_idfxx_file"
    done | sort $_idfxx_sort_flags
}

_idfxx_setup() {
    # zsh treats a pattern that matches nothing as an error. The searches below
    # expect the shell to keep such a pattern unchanged (as bash does) or to
    # remove it (null_glob).
    [ -n "${ZSH_VERSION:-}" ] && setopt local_options null_glob

    _idfxx_ready && return 0

    if [ -n "${IDF_EXPORT:-}" ]; then
        _idfxx_use_script "$IDF_EXPORT" && return 0
        echo "idfxx: IDF_EXPORT=$IDF_EXPORT did not provide idf.py" >&2
        echo "idfxx: (source it directly to see what it reports)" >&2
        return 1
    fi

    # An IDF_PATH in the environment names the wanted installation, but not how
    # to activate it. For an eim install, use the matching activation script.
    # Otherwise, use the checkout's own export.sh.
    if [ -n "${IDF_PATH:-}" ]; then
        _idfxx_script=$(_idfxx_eim_scripts |
            while IFS= read -r _idfxx_s; do
                grep -Fqx "IDF_PATH:${IDF_PATH%/}" "$_idfxx_s" && printf '%s\n' "$_idfxx_s" && break
            done)
        [ -n "$_idfxx_script" ] && _idfxx_use_eim "$_idfxx_script" && return 0
        _idfxx_use_export "$IDF_PATH/export.sh" && return 0
    fi

    _idfxx_script=$(_idfxx_eim_scripts | head -n 1)
    [ -n "$_idfxx_script" ] && _idfxx_use_eim "$_idfxx_script" && return 0

    for _idfxx_dir in "$HOME/esp/esp-idf" "$HOME"/esp/*/esp-idf "$HOME/esp-idf" \
        "$HOME"/.espressif/*/esp-idf /opt/esp/idf; do
        _idfxx_use_export "$_idfxx_dir/export.sh" && return 0
    done

    echo "idfxx: no ESP-IDF installation found (5.5 or later required)." >&2
    echo "idfxx: activate ESP-IDF before running just, or point IDF_EXPORT at" >&2
    echo "idfxx: its export.sh or eim activation script, e.g." >&2
    echo "idfxx:     IDF_EXPORT=~/esp/esp-idf/export.sh just build" >&2
    return 1
}

# Older versions of BSD sort do not have -V. Without it, fall back to a plain
# reverse sort.
if printf '' | sort -V >/dev/null 2>&1; then
    _idfxx_sort_flags=-Vru
else
    _idfxx_sort_flags=-ru
fi

if _idfxx_setup; then
    _idfxx_rc=0
    # A build does not otherwise show which ESP-IDF it used, and the search can
    # pick a different one than expected.
    [ -n "${IDF_PATH:-}" ] && echo "esp-idf: $IDF_PATH" >&2
    # Compile through ccache when it is installed and can write its cache. A
    # sandboxed shell may not be able to write the cache. Skip this when
    # IDF_CCACHE_ENABLE is already set, to either value.
    if [ -z "${IDF_CCACHE_ENABLE+x}" ] && command -v ccache >/dev/null 2>&1; then
        _idfxx_ccache_dir=$(ccache -k cache_dir 2>/dev/null)
        _idfxx_probe="$_idfxx_ccache_dir/.idfxx-probe.$$"
        if [ -n "$_idfxx_ccache_dir" ] && mkdir -p "$_idfxx_ccache_dir" 2>/dev/null &&
            (: > "$_idfxx_probe") 2>/dev/null; then
            rm -f "$_idfxx_probe"
            export IDF_CCACHE_ENABLE=1
        fi
        unset _idfxx_ccache_dir _idfxx_probe
    fi
else
    _idfxx_rc=1
fi

unset -f _idfxx_ready _idfxx_is_eim _idfxx_use_export _idfxx_use_eim \
    _idfxx_use_script _idfxx_eim_scripts _idfxx_setup
unset _idfxx_opts _idfxx_env _idfxx_tools_path _idfxx_kv _idfxx_file \
    _idfxx_dir _idfxx_script _idfxx_s _idfxx_sort_flags

return $_idfxx_rc
