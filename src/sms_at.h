#ifndef MEDVEMODEM_SMS_AT_H
#define MEDVEMODEM_SMS_AT_H

#include <stddef.h>
#include <json-c/json.h>

/*
 * The AT path of SMS and USSD (spec 38):
 * one exchange on the engine's AT port, run in a worker under the port's lease.
 * job: {"op": "list"} | {"op": "delete", "indices": [..]} |
 * {"op": "send", "pdus": [{"pdu", "len"}]} | {"op": "ussd", "verb":
 * "start"|"respond"|"cancel", "value": ..., "wait_ms": n}.
 * Returns the raw answers (parsed by the engine) or NULL with err.
 */
json_object *mm_sms_at_run(const char *path, json_object *job, int settle_ms, char *err, size_t err_size);

#endif
