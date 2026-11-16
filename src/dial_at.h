#ifndef MEDVEMODEM_DIAL_AT_H
#define MEDVEMODEM_DIAL_AT_H

#include <stddef.h>
#include <json-c/json.h>

/*
 * The 3GPP dialogue of a native AT dial (dial.h, "flow": "3gpp") on the engine's AT port, run in a worker under
 * the port's session lease. job: mm_dial_job(). Returns {"ipv4", "gw"?, "dns": [..], "plmn"?, "pdp", "reused",
 * "steps": ["cops", "reg 1", "reuse", ...]} or NULL / an object with err set (the step that failed named).
 * Bounded by the job's budget_ms; waits between steps are short sleeps, never a loop without a limit.
 */
json_object *mm_dial_at_run(const char *path, json_object *job, int settle_ms, char *err, size_t err_size);

#endif
