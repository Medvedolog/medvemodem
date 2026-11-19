#!/bin/sh
# Regression: LuCI package install, release self-update and SSH installer
# must serialize apk/opkg writes. No router packages or services are changed.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pkg="$root/files/usr/libexec/medvemodem/pkg-install"
upd="$root/files/usr/libexec/medvemodem/self-update"
ssh="$root/medvemodem-install.sh"
for file in "$pkg" "$upd" "$ssh"; do sh -n "$file"; done

for file in "$pkg" "$upd" "$ssh"; do
    grep -Fq 'MEDVEMODEM_PM_LOCK:-/tmp/medvemodem/package-operation.lock' "$file"
    grep -Fq 'kill -0 "$owner"' "$file"
    grep -Fq 'echo $$ > "$PM_LOCK/owner"' "$file" || {
        # SSH uses a generic lock function instead.
        [ "$file" = "$ssh" ] && grep -Fq 'echo $$ > "$lock_dir/owner"' "$file"
    }
done
grep -Fq 'pm_lock || fail' "$pkg"
[ "$(grep -Fc 'pm_lock || fail' "$upd")" -eq 2 ]
grep -Fq "trap 'pm_unlock' EXIT" "$pkg"
[ "$(grep -Fc "trap 'pm_unlock' EXIT" "$upd")" -eq 2 ]
grep -Fq 'take_package_lock' "$ssh"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
# Isolate and execute the real lock functions from both scripts. Running
# them from their full entrypoints would require actual router services.
sed -n '/^PM_LOCK=/,/^start() {/p' "$pkg" | sed '$d' > "$tmp/pkg-functions"
sed -n '/^PM_LOCK=/,/^put_pid() {/p' "$upd" | sed '$d' > "$tmp/upd-functions"

for impl in pkg upd; do
    MEDVEMODEM_PM_LOCK="$tmp/shared"
    export MEDVEMODEM_PM_LOCK
    sh -c '. "$1"; pm_lock; sleep 2; pm_unlock' sh "$tmp/$impl-functions" &
    holder=$!
    sleep 1
    other=upd
    [ "$impl" = upd ] && other=pkg
    if sh -c '. "$1"; pm_lock' sh "$tmp/$other-functions"; then
        echo "FAIL: $other took the lock while $impl held it" >&2
        kill "$holder" 2>/dev/null || :
        wait "$holder" 2>/dev/null || :
        exit 1
    fi
    wait "$holder"
    sh -c '. "$1"; pm_lock; pm_unlock' sh "$tmp/$other-functions"
done

# A new directory without PID must not be treated as stale.
mkdir "$tmp/shared"
if sh -c '. "$1"; pm_lock' sh "$tmp/pkg-functions"; then
    echo 'FAIL: acquired a fresh ownerless lock' >&2
    exit 1
fi
echo 'PASS: package lock syntax, cross-entrypoint exclusion and ownerless race protection'
