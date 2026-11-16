#ifndef MEDVEMODEM_WATCH_H
#define MEDVEMODEM_WATCH_H

#include <json-c/json.h>

/*
 * Health watch for automatic recovery (spec 22.1.4, 22.1.5, 23). Pure.
 *
 * From a served device record it derives findings with a recovery reason and
 * the ladder to climb, keeps per-device episode state (since when a finding
 * holds, which levels were tried) and says when to look again. The daemon
 * gates each candidate level (recovery.c, automatic) and executes; this never
 * decides that a level is allowed, only which ones are worth trying in order.
 *
 * Never a trigger: a missing netifd protocol handler (session.proto_missing),
 * a SIM lock or missing SIM, a structural configuration
 * finding (incomplete, unbound, wrong-mode, locked, firmware, service-mode) —
 * those are reported, at most L2 (spec 23). Reachability is a trigger only in
 * one narrow form (recovery study R4, R5): a connected session whose own
 * check confirms no answer (not "limited", which is an operator whitelist)
 * while some other uplink answers or it is the only one — one session
 * reconnect, then "carrier_restricted" (the operator cuts the traffic, no
 * reset helps). On 2G (gsm/gprs/edge only) the same is a radio failure and
 * the ladder goes on to the radio and the module.
 */
struct mm_watch_cfg {
	int session_ms;		/* an engine session failed or down for this long */
	int registration_ms;	/* MM enabled/searching without registering */
	int failed_ms;		/* MM state failed (not a SIM reason) */
	int conflict_ms;	/* two owners of one modem */
	int grace_ms;		/* after an attempt finished, before the next level */
	int internet_ms;	/* a connected session whose check confirms no answer */
	int search_ms;		/* MM searching for a network: heavy steps wait this long (R7) */
};
#define MM_WATCH_DEFAULTS { 60000, 300000, 60000, 60000, 60000, 120000, 720000 }

/*
 * One step for one device. state is the device's watch object (created by the
 * caller as {}, kept between steps). Returns a new object:
 *   {"finding": null | {"reason", "evidence", "since_ms", "ladder": [levels], "blocked": "why"?},
 *    "candidates": [levels not tried yet in this episode, lowest first] (only when due),
 *    "next_ms": monotonic time to look again, or -1}
 * now_ms is monotonic; busy says a recovery of this device is running.
 */
json_object *mm_watch_step(json_object *srv, json_object *state, long long now_ms, int busy,
			   const struct mm_watch_cfg *cfg);

/* The daemon tried a level for the current episode (started it) and when it finished. */
void mm_watch_tried(json_object *state, int level, long long now_ms);
void mm_watch_finished(json_object *state, long long now_ms);
/* A level the gate refused for this episode: not tried again, why kept for the view. */
void mm_watch_skip(json_object *state, int level, const char *why);

/* {"state": "ok" | "watching" | "recovering" | "stalled", "finding"?, "tried"?, "stalled"?} New object. */
json_object *mm_watch_view(json_object *state, long long now_ms);

/* a reach error that starts so is the modem's own web saying no SIM or no network (mgr_web.c web_offline) */
#define MM_WEB_OFFLINE_PREFIX "the modem's web says: "

#endif
