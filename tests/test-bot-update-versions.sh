#!/bin/sh
# Regression contract for Telegram update notices and self-update release metadata.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
view="$root/packages/medvemodem-bot/files/usr/share/medvemodem-bot/updview.uc"
update="$root/files/usr/libexec/medvemodem/self-update"
notes="$root/docs/RELEASE-HIGHLIGHTS-ALPHA1-R6.md"
grep -Fq '0-9A-Za-z._+~-' "$view" || { echo 'Bot installed version regex must allow underscores' >&2; exit 1; }
grep -Fq "const CHECK_EVERY = 12 * 3600" "$root/packages/medvemodem-bot/files/usr/share/medvemodem-bot/update.uc"
grep -Fq '## 0.16.0-alpha1-r6' "$notes"
grep -Fq 'HIGHLIGHTS.txt' "$root/.github/workflows/build.yml"
grep -Fq 'older "$eng" "$latest"' "$update"
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
older 0.16.0-alpha1-r4 0.16.0-alpha1-r6
if older 0.16.0_alpha1-r6 0.16.0-alpha1-r6; then echo 'Equal release must not be newer' >&2; exit 1; fi
if older 0.16.0_alpha1-r7 0.16.0-alpha1-r6; then echo 'Downgrade must not be offered' >&2; exit 1; fi
echo 'PASS: bot version display and r6 update comparison contracts'
