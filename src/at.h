#ifndef MEDVEMODEM_AT_H
#define MEDVEMODEM_AT_H

#include <stdbool.h>
#include <json-c/json.h>

/*
 * AT command policy and response parsing (spec 11.5, 14.1.4). Pure.
 */

/*
 * Hazard class of a command line, worst of its ';'-chained parts; queries
 * ("?", "=?", Quectel AT+Qxxx="name") are never hazards. Classes as in
 * tools/evidence_hazards.py: identity-write, persistent-write,
 * privileged-unlock, bootloader, power-off, composition, reset, none.
 */
const char *mm_at_hazard(const char *cmd);

/*
 * May an operator send this through the AT console? NULL when yes, else the
 * reason (static text). Only hazard class "none", one printable line starting
 * with AT, no dial/online/prompting commands. Expert mode does not lift this.
 */
const char *mm_at_console_refusal(const char *cmd);

/*
 * The same for one modem: what changes its composition only on some families (the T77W676/T77W968 ^SETMODE=,
 * Huawei's network mode on a stick) is refused there too. vidpid "vvvv:pppp" or NULL.
 */
const char *mm_at_device_refusal(const char *cmd, const char *vidpid);

/* The read-only queries a metrics poll sends (NULL-terminated). */
extern const char *const mm_at_metrics_commands[];

/* True when cmd is one of them: a poll never writes settings (H14). */
bool mm_at_is_metrics_command(const char *cmd);

/* Interfaces that must never receive AT (H12), "vid:pid" + interface number. */
bool mm_at_no_at_port(const char *vidpid, const char *ifnum);

/* Raw response holds a final result code (OK, ERROR, +CME/+CMS ERROR, ...). */
bool mm_at_complete(const char *raw);

/*
 * Length of a leading answer that belongs to another command: the echo of a
 * different command and everything up to its final result code (a killed
 * worker's late answer). 0 when the response starts with ours or no echo.
 */
size_t mm_at_stray_prefix(const char *cmd, const char *raw);

/* {"final": text | null, "ok": bool, "lines": [...]} without echo and final. New object. */
json_object *mm_at_parse(const char *cmd, const char *raw);

/* Normalized radio metrics from {"<command>": raw response}. New object. */
json_object *mm_at_metrics(json_object *raw);
/* The same from QModem's modem_ctrl.sh cell_info ({"cell_info": {"modem_info": [...]}}). New object. */
json_object *mm_qmodem_metrics(json_object *responses);
/* The carriers in use from a Fibocom MediaTek modem's AT+GTCAINFO? answer (FM350): {"cells": [{role, type, band,
 * pci, earfcn, bandwidth_mhz}]}, the primary first; NULL when it names none. New object. */
json_object *mm_fibocom_ca(const char *raw);

/* Probe verdict from the raw answers to "AT" and "AT+CGMI": "at", "not-at" or "silent". */
const char *mm_at_probe_verdict(const char *at_raw, const char *cgmi_raw);

#endif
