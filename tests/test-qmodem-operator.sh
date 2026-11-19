#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Compile and exercise the actual QModem cell_info normalizer; no router or tty.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v cc >/dev/null 2>&1 || { echo "missing host C compiler" >&2; exit 2; }
pkg-config --exists json-c || { echo "missing json-c host headers" >&2; exit 2; }
engine="$root/src/medvemodemd.c"
grep -Fq 'argv[0] = "/bin/ubus"' "$engine"
grep -Fq 'argv[4] = "qmodem"' "$engine"
grep -Fq 'argv[1] = "-t"' "$engine"
grep -Fq 'json_object_new_string("cell_info")' "$engine"
! grep -Fq 'json_object_new_string("sim_info")' "$engine"
! grep -Fq 'FIBOCOM_OPERATOR' "$engine"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat > "$tmp/test.c" <<'EOF'
#include <json-c/json.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
EOF
sed -n '/^static bool qmodem_metric_integer/,/^\/\*$/p' "$root/src/at.c" | sed '$d' >> "$tmp/test.c"
grep -q '^json_object \*mm_qmodem_metrics' "$tmp/test.c"
cat >> "$tmp/test.c" <<'EOF'
static int number(json_object *o, const char *key, long long want)
{
    json_object *v = json_object_object_get(o, key);
    return v && json_object_get_int64(v) == want;
}
static int string_is(json_object *o, const char *key, const char *want)
{
    json_object *v = json_object_object_get(o, key);
    return v && !strcmp(json_object_get_string(v), want);
}
int main(void)
{
    const char *lte =
        "{\"cell_info\":{\"modem_info\":["
        "{\"key\":\"network_mode\",\"value\":\"LTE Mode\"},"
        "{\"key\":\"MCC\",\"value\":\"250\"},"
        "{\"key\":\"MNC\",\"value\":\"01\"},"
        "{\"key\":\"TAC\",\"value\":\"4096\"},"
        "{\"key\":\"Cell ID\",\"value\":\"12345678\"},"
        "{\"key\":\"Physical Cell ID\",\"value\":\"321\"},"
        "{\"key\":\"EARFCN\",\"value\":\"1300\"},"
        "{\"key\":\"Band\",\"value\":\"B3\"},"
        "{\"key\":\"DL Bandwidth\",\"value\":\"20M\"},"
        "{\"key\":\"RSRP\",\"value\":\"-98\"},"
        "{\"key\":\"RSRQ\",\"value\":\"-12.5\"},"
        "{\"key\":\"RSSNR\",\"value\":\"16.5\"},"
        "{\"key\":\"RSSI\",\"value\":\"-67\"}]}}";
    const char *nsa =
        "{\"cell_info\":{\"modem_info\":["
        "{\"key\":\"network_mode\",\"value\":\"EN-DC Mode\"},"
        "{\"key\":\"MCC\",\"value\":\"250\"},"
        "{\"key\":\"MNC\",\"value\":\"01\"},"
        "{\"key\":\"LTE\",\"value\":\"LTE\"},"
        "{\"key\":\"RSRP\",\"value\":\"-101\"},"
        "{\"key\":\"NR5G-NSA\",\"value\":\"NR5G-NSA\"},"
        "{\"key\":\"Band\",\"value\":\"n78\"},"
        "{\"key\":\"ARFCN\",\"value\":\"640000\"},"
        "{\"key\":\"Physical Cell ID\",\"value\":\"45\"},"
        "{\"key\":\"RSRP\",\"value\":\"-87\"},"
        "{\"key\":\"RSRQ\",\"value\":\"-9.5\"},"
        "{\"key\":\"SINR\",\"value\":\"21\"}]}}";
    json_object *raw = json_tokener_parse(lte), *m = mm_qmodem_metrics(raw);
    if (!string_is(m, "access_technology", "lte") || !string_is(m, "operator", "25001") ||
        !number(m, "rsrp_dbm", -98) || !number(m, "rssi_dbm", -67) ||
        !number(m, "pci", 321) || !number(m, "earfcn", 1300) ||
        !number(m, "cell_id", 12345678) || !number(m, "tac", 4096) ||
        !string_is(m, "band", "B3")) return 1;
    if (json_object_get_double(json_object_object_get(m, "rsrq_db")) != -12.5 ||
        json_object_get_double(json_object_object_get(m, "sinr_db")) != 16.5 ||
        json_object_get_double(json_object_object_get(m, "bandwidth_mhz")) != 20.0) return 2;
    json_object_put(raw); json_object_put(m);

    raw = json_tokener_parse(nsa); m = mm_qmodem_metrics(raw);
    json_object *nr = json_object_object_get(m, "nr");
    if (!string_is(m, "access_technology", "nr-nsa") ||
        !number(nr, "rsrp_dbm", -87) || !string_is(nr, "band", "n78") ||
        !number(nr, "earfcn", 640000) || !number(nr, "pci", 45)) return 3;
    if (number(m, "rsrp_dbm", -87)) return 4; /* NR must not overwrite LTE */
    json_object_put(raw); json_object_put(m);
    puts("PASS: QModem LTE/NR cell_info normalizes operator, RAT, signal and serving-cell fields");
    return 0;
}
EOF
cc -std=c11 -Wall -Wextra -Werror $(pkg-config --cflags json-c) \
    "$tmp/test.c" -o "$tmp/test" $(pkg-config --libs json-c)
"$tmp/test"
