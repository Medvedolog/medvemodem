#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Compile/test the *actual* operator parser extracted from medvemodemd.c,
# without cross-building any router package or opening a serial port.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v cc >/dev/null 2>&1 || { echo "missing host C compiler" >&2; exit 2; }
pkg-config --exists json-c || { echo "missing json-c host headers" >&2; exit 2; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat > "$tmp/test.c" <<'EOF'
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
EOF
awk '/^static void fibocom_operator_from_at\(/ {copy=1} copy {print} copy && /^}$/ {exit}' \
    "$root/src/medvemodemd.c" >> "$tmp/test.c"
grep -q '^static void fibocom_operator_from_at' "$tmp/test.c"
cat >> "$tmp/test.c" <<'EOF'
int main(void)
{
    json_object *m = json_object_new_object();
    const char *name;
    fibocom_operator_from_at(m, "AT+GTCURCAR?\r\n+GTCURCAR: 117,\"China Telecom\"\r\nOK\r\n");
    name = json_object_get_string(json_object_object_get(m, "operator"));
    if (!name || strcmp(name, "China Telecom")) return 1;
    fibocom_operator_from_at(m, "+GTCURCAR: 117,\"bad\nname\"");
    name = json_object_get_string(json_object_object_get(m, "operator"));
    if (strcmp(name, "China Telecom")) return 2;
    fibocom_operator_from_at(m, "ERROR\r\n");
    name = json_object_get_string(json_object_object_get(m, "operator"));
    if (strcmp(name, "China Telecom")) return 3;
    json_object_put(m);
    puts("PASS: QModem owner-side GTCURCAR carrier parsing");
    return 0;
}
EOF
cc -std=c11 -Wall -Wextra -Werror $(pkg-config --cflags json-c) \
    "$tmp/test.c" -o "$tmp/test" $(pkg-config --libs json-c)
"$tmp/test"
