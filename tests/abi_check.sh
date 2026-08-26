#!/bin/sh
# ABI regression check for libudfread.
#
# Compares the exported (dynamic) symbols of the built shared library
# against the checked-in golden list tests/abi/libudfread-exports.txt.
#
# Usage:
#   tests/abi_check.sh [library_path] [top_srcdir]   - check (default)
#   tests/abi_check.sh --update [library_path] [top_srcdir]
#                                                     - regenerate golden list
#
# The check is best-effort: it skips (exit 0) when the library was not
# built as a shared library or when nm cannot handle the binary format
# (e.g. platform without GNU nm -D support).
#
# Regenerating the golden list after an intentional ABI change:
#   make update-abi
set -u

update=0
if [ "${1:-}" = "--update" ]; then
    update=1
    shift
fi

libpath="${1:-}"
srcdir="${2:-.}"

# If libpath is a directory, search for the library there
if [ -d "$libpath" ]; then
    builddir="$libpath"
    so=""
    for dir in "$builddir/.libs" "$builddir/src/libudfread/.libs" "$builddir/src"; do
        for f in "$dir"/libudfread.so* "$dir"/libudfread.dylib "$dir"/libudfread-*.dll; do
            if [ -f "$f" ]; then
                so="$f"
                break 2
            fi
        done
    done
else
    # libpath is the library file itself
    so="$libpath"
    builddir="$(dirname "$so")"
fi

if [ -z "$so" ] || [ ! -f "$so" ]; then
    echo "abi_check: SKIP (no shared library found)"
    exit 0
fi

if ! command -v nm >/dev/null 2>&1; then
    echo "abi_check: SKIP (nm not found)"
    exit 0
fi

extract() {
    nm -D --defined-only "$so" 2>/dev/null | awk '{print $3}' | grep -v '^__gcov_' | grep -v '^mangle_path$' | LC_ALL=C sort -u
}

actual="$(extract)"
if [ -z "$actual" ]; then
    echo "abi_check: SKIP (nm -D produced no output; unsupported binary format)"
    exit 0
fi

actual_f="$(mktemp)" || exit 1
exp_f="$(mktemp)" || { rm -f "$actual_f"; exit 1; }
trap 'rm -f "$actual_f" "$exp_f"' EXIT

printf '%s\n' "$actual" > "$actual_f"
LC_ALL=C sort -u "$srcdir/tests/abi/libudfread-exports.txt" > "$exp_f"

if [ "$update" = "1" ]; then
    cp "$actual_f" "$srcdir/tests/abi/libudfread-exports.txt"
    echo "abi_check: golden list updated: $srcdir/tests/abi/libudfread-exports.txt"
    exit 0
fi

if cmp -s "$actual_f" "$exp_f"; then
    echo "abi_check: OK ($(wc -l < "$actual_f") exported symbols match golden list)"
    exit 0
fi

echo "abi_check: FAIL - exported symbols differ from $srcdir/tests/abi/libudfread-exports.txt"
if [ -f "$srcdir/tests/abi/libudfread-exports.txt" ]; then
    added="$(LC_ALL=C comm -13 "$exp_f" "$actual_f")"
    removed="$(LC_ALL=C comm -23 "$exp_f" "$actual_f")"
    [ -n "$added" ] && echo "  newly exported (check if intentional):" && printf '    %s\n' $added
    [ -n "$removed" ] && echo "  previously exported, now missing (ABI break):" && printf '    %s\n' $removed
fi
echo "  If the change is intentional, update the golden list with: make update-abi"
exit 1