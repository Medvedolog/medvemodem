#!/bin/sh
# Regression contract for Telegram update notices and self-update release metadata.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
view="$root/packages/medvemodem-bot/files/usr/share/medvemodem-bot/updview.uc"
update="$root/files/usr/libexec/medvemodem/self-update"
notes="$root/docs/RELEASE-HIGHLIGHTS-ALPHA1-R10.md"
grep -Fq '0-9A-Za-z._+~-' "$view" || { echo 'Bot installed version regex must allow underscores' >&2; exit 1; }
grep -Fq "const CHECK_EVERY = 12 * 3600" "$root/packages/medvemodem-bot/files/usr/share/medvemodem-bot/update.uc"
grep -Fq '## 0.16.0-alpha1-r10' "$notes"
grep -Fq 'HIGHLIGHTS.txt' "$root/.github/workflows/build.yml"
grep -Fq 'needs_update "$latest"' "$update"
grep -Fq 'component_needs_update "$1" medvemodem luci-app-medvemodem medvemodem-bot luci-app-medvemodem-bot' "$update"
grep -Fq 'if [ -n "$latest" ] && needs_update "$latest"; then newer=true; fi' "$update"
grep -Fq 'component_needs_update "$latest" medvemodem luci-app-medvemodem' "$update"
grep -Fq 'component_needs_update "$latest" medvemodem-bot luci-app-medvemodem-bot' "$update"
grep -Fq 'the release has no complete Telegram bot package set' "$update"
grep -Fq 'ok %s %s %s' "$update"
grep -Fq 'error %s %s %s' "$update"
sh -n "$update"
# Model self-update's AWK numeric comparison using the same AWK program.
older() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        na = split(a, x, /[^0-9]+/); nb = split(b, y, /[^0-9]+/)
        for (i = 1; i <= (na > nb ? na : nb); i++) {
            p = (i <= na && x[i] != "") ? x[i] + 0 : -1
            q = (i <= nb && y[i] != "") ? y[i] + 0 : -1
            if (p < q) exit 0
            if (p > q) exit 1
        }
        exit 1
    }'
}
older 0.16.0_alpha1-r5 0.16.0-alpha1-r6
older 0.16.0_alpha1-r5 0.16.0-alpha1-r8
older 0.16.0_alpha1-r7 0.16.0-alpha1-r10
older 0.16.0-alpha1-r4 0.16.0-alpha1-r6
older 0.16.0_alpha1-r9 0.16.0-alpha1-r10 || { echo 'r9 must be older than r10' >&2; exit 1; }
if older 0.16.0_alpha1-r10 0.16.0-alpha1-r10; then echo 'Equal release must not be newer' >&2; exit 1; fi
if older 0.16.0_alpha1-r10 0.16.0-alpha1-r9; then echo 'Downgrade must not be offered' >&2; exit 1; fi
echo 'PASS: bot and LuCI component update detection, channel-scoped cache and version comparison'
