#ifndef MEDVEMODEM_RECOVERY_H
#define MEDVEMODEM_RECOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/*
 * Recovery controller policy (spec 22.1). Pure: the daemon executes, this
 * decides what may run, and keeps the per-device history that loop protection
 * needs. Clocks are passed in: "m" monotonic ms for windows, wall seconds for
 * display.
 *
 * Levels are the canonical ladder (22.1.2). L0-L2 stay inside the engine and
 * the current owner; L3 and up disturb the modem and need a profile that
 * declares the capability (22.1.3) with a recovery status: "field-reported"
 * allows an operator request and, for radio, modem reset and USB reset, an
 * automatic one too (2026-10-27); a driver rebind and the port's power run by
 * themselves only from "bench-validated".
 */
enum mm_recovery_level {
	MM_REC_OBSERVE,		/* L0 re-resolve, refresh, settle */
	MM_REC_SESSION,		/* L1 reconnect through the current owner */
	MM_REC_CONTROL,		/* L2 leases, inhibition, control mapping */
	MM_REC_RADIO,		/* L3 radio off/on */
	MM_REC_SOFT_RESET,	/* L4 modem reset through its owner */
	MM_REC_REBIND,		/* L5 interface driver unbind/bind */
	MM_REC_USB_RESET,	/* L6 USB deauthorize/authorize */
	MM_REC_POWER,		/* L7 hardware power cycle */
	MM_REC_LEVELS
};
#define MM_REC_DISRUPTIVE MM_REC_RADIO

struct mm_recovery_cfg {
	int cap_5m;		/* disruptive attempts per 5 minutes */
	int cap_1h;		/* disruptive attempts per hour */
	int session_cap_5m;	/* L1 attempts per 5 minutes */
	int base_cooldown_s;	/* after a failed disruptive attempt, doubled per failure */
	int max_cooldown_s;
	int manual_after;	/* consecutive failures that stop automatic recovery */
};
#define MM_RECOVERY_DEFAULTS { 3, 6, 6, 60, 1800, 3 }

/* "observe", "session", "control", "radio", "soft-reset", "rebind", "usb-reset", "power" */
const char *mm_recovery_level_name(int level);
/* A name above or its number 0-7; -1 when neither. */
int mm_recovery_level_parse(const char *s);
/* The reasons of 22.1.4. */
bool mm_recovery_reason_valid(const char *reason);

/*
 * Whether the profile decision (the device's "profile" object, with its
 * "recovery" block) and the control owner permit a level; NULL when they do,
 * else why not.
 */
const char *mm_recovery_support(json_object *profile, const char *owner, int level, bool automatic);

/* [{level, name, supported, why?}] for all levels. New array. */
json_object *mm_recovery_levels(json_object *profile, const char *owner, bool automatic);

/*
 * The full gate: support, then loop protection from the device's state
 * (caps, cooldown, manual-required) and, for automatic disruptive levels, an
 * unstable USB link (22.1.10). NULL when the attempt may start, else the
 * reason written into buf.
 */
const char *mm_recovery_gate(json_object *state, json_object *profile, const char *owner, json_object *stability,
			     int level, bool automatic, long long now_ms, const struct mm_recovery_cfg *cfg,
			     char *buf, size_t size);

/* Record a finished attempt; result: "succeeded" or "failed". state is the device's object. */
void mm_recovery_record(json_object *state, int level, const char *reason, const char *result, const char *detail,
			long long now_ms, long long wall, const struct mm_recovery_cfg *cfg);

/* The normalized view of 22.1.13. active_level < 0 when nothing runs. New object. */
json_object *mm_recovery_view(json_object *state, json_object *profile, const char *owner, int active_level,
			      const char *active_reason, long long now_ms, long long wall_now,
			      const struct mm_recovery_cfg *cfg);

#endif
