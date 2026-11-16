#ifndef MEDVEMODEM_SMS_H
#define MEDVEMODEM_SMS_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/*
 * SMS and USSD through ModemManager (spec 38): the parts
 * that need no daemon state — parsing what mmcli prints and checking what an
 * operator types before it reaches an mmcli argument.
 */

/* The worker's output: one JSON document per line — `mmcli -J -m N
 * --messaging-list-sms`, then `mmcli -J -s P` per message. Returns
 * {"messages": [{id, number, text, state, type, storage, timestamp, epoch,
 * class, reference, delivery_state}], "listed": n} or {"error": ...}. */
json_object *mm_sms_parse(const char *buf);

/* +7…, 7…, 8…, short numbers: an optional '+', 3–20 digits */
bool mm_sms_number_ok(const char *number);
/* valid UTF-8, no control characters but newline and tab, 1–1600 bytes */
bool mm_sms_text_ok(const char *text);
/* the argument of `--messaging-create-sms`: number='…',text='…' with a quote the
 * text does not contain (ModemManager's key-value parser has no escapes); false
 * when it does not fit. smsc: the SMS centre of the message, NULL or "" for the SIM's */
bool mm_sms_create_arg(const char *number, const char *text, const char *smsc, char *out, size_t size);
/* GSM 7-bit (with the extension table) or UCS-2, and the segment count */
void mm_sms_measure(const char *text, bool *gsm7, int *units, int *segments);

/* USSD codes: only what USSD consists of — digits, '*', '#', '+'; 2–32 characters */
bool mm_ussd_code_ok(const char *code);
/* a reply typed for a USSD menu: printable, 1–64 bytes, no quote or newline */
bool mm_ussd_reply_ok(const char *reply);
/* `mmcli --3gpp-ussd-initiate|respond` output: {"reply": "..."} or {"error": "..."} */
json_object *mm_ussd_parse(const char *out, int exit_code);

/* "2026-10-09T11:39:15+03:00" (also +03, Z, none) to a UTC epoch; -1 when unreadable */
long long mm_sms_epoch(const char *iso);

/*
 * r95: QModem's `modem_ctrl.sh get_sms` answer ({"msg": [{index, sender, timestamp (epoch), content, reference?,
 * total?, part?}], ...} as its tom_modem prints it; text before the JSON is skipped) to our messages: the parts of
 * one message joined in order (id = the first part's index, parts = all of them, have/total when some are missing),
 * received/deliver, path "qmodem". NULL when it is no such answer.
 */
json_object *mm_sms_qmodem_list(const char *raw);

/*
 * Incoming messages the operator has not seen (dashboard badge, spec 38). A
 * received message is known by a hash of its number, timestamp and text, so
 * the seen list keeps no content (the router's archive, mgr_sms.c, is apart). mm_sms_inbox: the keys of the received
 * (deliver) messages of a list (new array). mm_sms_unread: how many of them
 * are not in seen (an array of keys).
 */
json_object *mm_sms_inbox(json_object *msgs);
/* the inbox key of one message (number, time, text hashed): the same message wherever it is kept */
void mm_sms_key(json_object *m, char *out, size_t size);
int mm_sms_unread(json_object *msgs, json_object *seen);

#endif
