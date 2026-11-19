#!/usr/bin/env python3
"""Compile and exercise the production EP06 QTEMP parser against firmware-like replies."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/radio.c").read_text(encoding="utf-8")
start = source.index("json_object *mm_quectel_temp_from_at(")
end = source.index("/* ---- the modem's heat", start)
parser = source[start:end]
stub = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct json_object { int value; } json_object;
static json_object *json_object_new_object(void) { return calloc(1, sizeof(json_object)); }
static json_object *json_object_new_int(int n) {
    json_object *o = calloc(1, sizeof(json_object));
    if (o) o->value = n;
    return o;
}
static void json_object_object_add(json_object *o, const char *key, json_object *value) {
    if (strcmp(key, "modem_c") || !o || !value) abort();
    o->value = value->value;
    free(value);
}
'''
test = r'''
int main(void) {
    const struct { const char *raw; int expected; } cases[] = {
        { "AT+QTEMP\r\n+QTEMP: 49,47,47\r\nOK\r\n", 49 },
        { "+QTEMP: 44,49,48,47", 49 },
        { "+QTEMP: -5,-7,-3,255", -3 },
        { "+QTEMP: 37,255,255", 37 },
        { "+QTEMP: 255,255,255", -1000 },
        { "+QTEMP: -32768,-140", -1000 },
        { "AT+QTEMP\r\nERROR", -1000 },
        { "+QTEMP: garbage", -1000 },
        { "+QTEMP: 40\r\n+QTEMP: 41,42", 42 },
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        json_object *o = mm_quectel_temp_from_at(cases[i].raw);
        int got = o ? o->value : -1000;
        free(o);
        if (got != cases[i].expected) {
            fprintf(stderr, "QTEMP case %zu: got %d instead of %d\n", i, got, cases[i].expected);
            return 1;
        }
    }
    puts("PASS: production EP06 QTEMP parser (nine fixtures)");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="ep06-qtemp-") as tmp:
    src = Path(tmp) / "parser.c"
    binary = Path(tmp) / "parser"
    src.write_text(stub + parser + test, encoding="utf-8")
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2",
                    str(src), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
