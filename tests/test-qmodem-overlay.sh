#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Non-building smoke test for the pinned upstream QModem source overlay.
# Usage: sh tests/test-qmodem-overlay.sh /path/to/FUjr-QModem-checkout
set -eu

[ "$#" -eq 1 ] || { echo "usage: $0 QMODEM_SOURCE_CHECKOUT" >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir=$1
pinned=$(sed -n 's/^QMODEM_COMMIT:=//p' "$root/Makefile" | head -1)
actual=$(git -C "$source_dir" rev-parse HEAD)
[ "$actual" = "$pinned" ] || {
    echo "wrong QModem source revision: $actual (expected $pinned)" >&2
    exit 1
}
target=application/qmodem/files/etc/init.d/qmodem_network
[ -f "$source_dir/$target" ] || { echo "missing QModem init source" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/application/qmodem/files/etc/init.d"
cp "$source_dir/$target" "$tmp/$target"
(
    cd "$tmp"
    for item in "$root"/patches/qmodem/*.patch; do
        [ -f "$item" ] || continue
        git apply --check "$item"
        git apply "$item"
    done
    sh -n "$target"
    grep -q 'timeout -s TERM -k 2 8 flock -x 9' "$target"
    grep -q 'stop incomplete: dial.lock busy' "$target"
    grep -q '_qm_reap_disabled_orphans' "$target"
    grep -q 'hang "$1" || return' "$target"
)
echo "PASS: QModem overlay applies in sequence, shell parses and guards are present"
