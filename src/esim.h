#ifndef MEDVEMODEM_ESIM_H
#define MEDVEMODEM_ESIM_H

#include <json-c/json.h>

/*
 * eSIM capability adapter, core side (docs/ESIM-INTEGRATION.md). Pure.
 *
 * The engine, not the eSIM backend, decides which control resource of which
 * modem an lpac operation may open: the context is built from the registry
 * record of the current attachment generation. The backend never discovers
 * devices and never takes a device path from a client.
 *
 * srv:      the served record (owner, port_roles, modemmanager view);
 * lpac:     the backend's probe ({"installed", "build", "drivers"?}) or NULL;
 * settings: the operator's `config esim` section for this device or NULL
 *           (option backend auto|qmi|uqmi|mbim|at, option apdu_slot N).
 *
 * Returns {"device_id", "generation", "vendor"?, "model"?, "owner",
 *          "resources": {"qmi", "mbim", "at"}, "at_ports": [...], "candidates": [...],
 *          "allowed_apdu": [...], "selected"?, "mbim_proxy", "apdu_slot",
 *          "error"?, "reason"?}. error is one of the normalized codes below.
 * at_ports: the engine-owned AT ports an eUICC port probe may try, the engine's
 * AT port first (none for Sierra). AT goes through the engine's bridge, so it
 * needs lpac's stdio driver when the driver list is known.
 * New object.
 */
json_object *mm_esim_target(json_object *srv, json_object *lpac, json_object *settings);

/*
 * Normalized result of one backend action from its raw output (lpac's JSON
 * lines; the last one is the answer) and exit status.
 * {"ok": true, "data": ...} or {"ok": false, "error": code, "detail"?}.
 * Codes: lpac_missing, euicc_absent, driver_unavailable, transport_unavailable,
 * owner_busy, operation_busy, timeout, stale_generation, lpac_error. Exit 124
 * and 125 are the AT bridge's (lpac 2.3.0's stdio driver; tty not openable).
 * New object.
 */
json_object *mm_esim_result(const char *output, int exit_status, int timed_out);

/* Read-only actions of this stage: version, probe, chip, profiles. */
int mm_esim_action_known(const char *action);

#endif
