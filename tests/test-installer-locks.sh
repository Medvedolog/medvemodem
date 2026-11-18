#!/bin/sh
# Exercise actual BusyBox/POSIX lock helpers in a controlled temporary root.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
# Extract precisely the two lock helpers without sourcing the executable
# installer's root/package-manager checks and production traps.
sed -n '/^take_lock() {/,/^take_package_lock() {/p' medvemodem-install.sh | sed '$d' > "$tmp/locks.sh"
sh -n "$tmp/locks.sh"
. "$tmp/locks.sh"
lock="$tmp/package.lock"
take_lock "$lock"
[ "$(cat "$lock/owner")" = "$$" ] || { echo 'FAIL: lock owner is not the current PID' >&2; exit 1; }
# A different process must NOT steal our current lock.
if sh -c '. "$1"; take_lock "$2"' sh "$tmp/locks.sh" "$lock" >/dev/null 2>&1; then
    echo 'FAIL: second process stole a live lock' >&2
    exit 1
fi
release_lock "$lock"
[ ! -e "$lock" ] || { echo 'FAIL: owner did not release its lock' >&2; exit 1; }
# A stale dead process may be replaced. A separate owner cannot release ours.
mkdir "$lock"
echo 99999999 > "$lock/owner"
take_lock "$lock"
[ "$(cat "$lock/owner")" = "$$" ] || { echo 'FAIL: stale lock not recovered' >&2; exit 1; }
sh -c '. "$1"; release_lock "$2"' sh "$tmp/locks.sh" "$lock" >/dev/null 2>&1
[ -d "$lock" ] || { echo 'FAIL: non-owner deleted lock' >&2; exit 1; }
release_lock "$lock"
[ ! -e "$lock" ] || { echo 'FAIL: stale lock not cleaned' >&2; exit 1; }
echo 'PASS: install lock ownership, concurrency, stale recovery, cleanup'
