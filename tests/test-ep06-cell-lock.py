#!/usr/bin/env python3
"""Compile the production EP06 QNWLOCK parser and run firmware-form fixtures.

Does not need OpenWrt SDK or libjson-c: the small JSON adapter checks only the
return values and the state required for a reversible lock.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
code = (root / "src/radio.c").read_text(encoding="utf-8")

def section(start, end):
    i = code.index(start)
    return code[i:code.index(end, i)]

line_after = section("static bool line_after(", "/* bits of a hex mask")
reader = section("static bool ep06_lock_read(", "/* ---- Quectel")
cell_lock = section("static json_object *cell_lock(const char *rat, long arfcn, long pci)\n{",
                    "static json_object *fibocom_mtk(")

stub = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
typedef struct json_object {
    long arfcn, pci, count, integer;
    char *string;
} json_object;
static json_object *json_object_new_object(void) {
    return calloc(1, sizeof(json_object));
}
static json_object *json_object_new_int64(long x) {
    json_object *o = json_object_new_object(); o->integer = x; return o;
}
static json_object *json_object_new_int(int x) {
    return json_object_new_int64(x);
}
static json_object *json_object_new_string(const char *x) {
    json_object *o = json_object_new_object(); o->string = strdup(x); return o;
}
static void json_object_object_add(json_object *o, const char *key, json_object *v) {
    if (!o) abort();
    if (!strcmp(key, "arfcn")) o->arfcn = v->integer;
    if (!strcmp(key, "pci")) o->pci = v ? v->integer : -1;
    if (!strcmp(key, "count")) o->count = v->integer;
    if (v) { free(v->string); free(v); }
}
static void json_object_put(json_object *o) {
    if (o) { free(o->string); free(o); }
}
'''

fixtures = r'''
static void trial(const char *reply, const char *form, bool valid, const char *restore,
                  int count, long arfcn, long pci) {
    char save[256] = {0};
    json_object *lock = NULL;
    int n = -1;
    bool got = ep06_lock_read(reply, form, save, sizeof(save), &lock, &n);
    if (got != valid) {
        fprintf(stderr, "validity mismatch for %s: %s\n", form, reply);
        exit(1);
    }
    if (got && (strcmp(save, restore) || n != count)) {
        fprintf(stderr, "snapshot mismatch: got %s, count %d\n", save, n);
        exit(1);
    }
    if (got && lock && (lock->arfcn != arfcn || lock->pci != pci)) {
        fprintf(stderr, "lock mismatch: arfcn %ld PCI %ld\n", lock->arfcn, lock->pci);
        exit(1);
    }
    if (got && ((count > 0) != (lock != NULL))) {
        fputs("lock presence mismatch\n", stderr);
        exit(1);
    }
    if (got && count > 1 && (!lock || lock->count != count)) {
        fputs("multi-cell count mismatch\n", stderr);
        exit(1);
    }
    json_object_put(lock);
}
int main(void) {
    trial("+QNWLOCK: \"common/4g\",0\r\nOK", "common/4g", true, "0", 0, 0, 0);
    trial("+QNWLOCK: \"common/4g\",1,1749,226\r\nOK", "common/4g",
          true, "1,1749,226", 1, 1749, 226);
    trial("+QNWLOCK: \"common/4g\",2,1749,226,1850,127", "common/4g",
          true, "2,1749,226,1850,127", 2, 1749, 226);
    trial("+QNWLOCK: \"common/4g\",2,1749,226", "common/4g",
          false, "", 0, 0, 0);
    trial("+QNWLOCK: \"common/4g\",1,1749,504", "common/4g",
          false, "", 0, 0, 0);
    trial("+QNWLOCK: \"common/lte\",0,0,0,0", "common/lte",
          true, "0", 0, 0, 0);
    trial("+QNWLOCK: \"common/lte\",2,1749,226,0", "common/lte",
          true, "2,1749,226", 1, 1749, 226);
    trial("+QNWLOCK: \"common/lte\",1,1749,0,0", "common/lte",
          true, "1,1749,0", 1, 1749, -1);
    trial("+QNWLOCK: \"common/lte\",2,1749,226,1", "common/lte",
          false, "", 0, 0, 0); /* incomplete locking */
    trial("AT+QNWLOCK=\"common/4g\"\r\nERROR", "common/4g",
          false, "", 0, 0, 0);
    trial("+QNWLOCK: \"common/4g\",1,1749,226;AT+CFUN=0", "common/4g",
          false, "", 0, 0, 0);
    puts("PASS: EP06 QNWLOCK (11 production parser fixtures)");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="ep06-lock-") as tmp:
    src = Path(tmp) / "lock.c"
    exe = Path(tmp) / "lock"
    src.write_text(stub + line_after +
                   'static json_object *cell_lock(const char *, long, long);\n' +
                   reader + cell_lock + fixtures, encoding="utf-8")
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2",
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
