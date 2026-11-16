#ifndef MEDVEMODEM_DIAG_H
#define MEDVEMODEM_DIAG_H

#include <json-c/json.h>

/*
 * Diagnostic capture (spec 32). Pure: the CLI gathers the record (from the
 * daemon or a one-shot pass), history, system facts and kernel log text and
 * passes them in.
 */

/* Composition snapshot (spec 32.3): interfaces with drivers, nodes and roles,
 * ModemManager view and the detected profile. New object. */
json_object *mm_diag_composition(json_object *rec);

/*
 * Profile bundle (spec 32.2) before sanitizing: device, composition, the
 * engine's decision and reasoning, history, system, kernel log. engine is
 * {"version", "running"}. New object.
 */
json_object *mm_diag_bundle(json_object *rec, json_object *events, json_object *system, json_object *klog,
			    json_object *engine, const char *label, long long now);

/* Kernel log lines that concern this device (its USB name, interfaces and
 * nodes, never a child device of a hub), the last max of them. New array. */
json_object *mm_diag_klog_filter(const char *text, json_object *rec, int max);

/*
 * Sanitize in place for sharing (spec 32.5): drops identity keys (serial,
 * IMEI, IMSI, ICCID, MSISDN, PIN, passwords), replaces the USB serial wherever
 * it appears with "~" + a 12-hex salted hash, replaces 15-digit (IMEI/IMSI),
 * ICCID-like and phone numbers in any text, and records what it did under
 * "redaction".
 */
void mm_diag_sanitize(json_object *bundle, const char *salt);

/* Structural diff of two bundles' compositions (spec 32.4). New array of
 * {"kind", "interface"?, "before", "after"}. */
json_object *mm_diag_diff(json_object *a, json_object *b);

/* One summary row per record for `diag quick`. New array. */
json_object *mm_diag_quick(json_object *records);

#endif
