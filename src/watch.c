#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "watch.h"

/*
 * Device state: {"reason", "since", "tried": [levels], "running", "last_end", "stalled"?}
 * for the current episode; an episode ends when its finding clears or another
 * reason takes over.
 */

static json_object *get(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_object_get_ex(o, key, &v) ? v : NULL;
}

static const char *str(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static bool is(const char *a, const char *b)
{
	return a && b && !strcmp(a, b);
}

static long long num(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v ? json_object_get_int64(v) : 0;
}

struct finding {
	const char *reason, *evidence, *blocked;
	int ladder[4], n;
	int threshold;
};

static bool registered(json_object *mm)
{
	const char *r = str(mm, "registration"), *s = str(mm, "state");

	return is(r, "home") || is(r, "roaming") || is(s, "registered") || is(s, "connecting") || is(s, "connected");
}

/* only 2G radio access: data there is all but dead, it is a radio failure, not a carrier limit */
static bool only_2g(json_object *mm)
{
	json_object *t = get(mm, "access_tech");
	size_t i, n = t && json_object_is_type(t, json_type_array) ? json_object_array_length(t) : 0;

	for (i = 0; i < n; i++) {
		const char *v = json_object_get_string(json_object_array_get_idx(t, i));
		if (!is(v, "gsm") && !is(v, "gprs") && !is(v, "edge") && !is(v, "gsm-compact"))
			return false;
	}
	return n > 0;
}

/*
 * A connected session with an address whose own check confirms no answer
 * (R4): not "limited" (a whitelist), not unconfirmed (hysteresis), and not
 * while every probed uplink fails at once (R5: the targets or the upstream are
 * dead, not this modem).
 */
static bool no_internet(json_object *srv, json_object *mm, json_object *sess, const struct mm_watch_cfg *cfg,
			struct finding *f, char *buf, size_t size)
{
	json_object *up = get(srv, "uplink"), *reach = get(up, "reach");
	const char *role = str(up, "role");

	if (!is(str(sess, "state"), "connected") || !reach || get(reach, "pending") ||
	    json_object_get_boolean(get(reach, "ok")) || json_object_get_boolean(get(reach, "limited")) ||
	    (!is(role, "no_internet") && !is(role, "backup_not_ready")))
		return false;
	if (json_object_get_boolean(get(srv, "uplinks_all_failing")))
		return false;
	f->evidence = buf;
	/* the stick's own web says no SIM or no network: a reset brings neither (the owner's SIM-less E3372-325 and
	 * M150-4, 2026-11-04: «Восстановление … проверка интернета не прошла» over and over) */
	if (str(reach, "error") && !strncmp(str(reach, "error"), MM_WEB_OFFLINE_PREFIX, strlen(MM_WEB_OFFLINE_PREFIX))) {
		snprintf(buf, size, "%s", str(reach, "error"));
		f->reason = "sim";
		f->blocked = "the modem's own web reports no SIM or no network, not reset";
		return true;
	}
	f->threshold = cfg->internet_ms;
	f->ladder[f->n++] = 1;
	if (only_2g(mm)) {
		snprintf(buf, size, "no answer through %s on 2G", str(reach, "device") ? str(reach, "device") : "the modem");
		f->reason = "no_data_2g";
		f->ladder[f->n++] = 3;
		f->ladder[f->n++] = 4;
		return true;
	}
	snprintf(buf, size, "no answer through %s (%s)", str(reach, "device") ? str(reach, "device") : "the modem",
		 str(reach, "error") ? str(reach, "error") : "no answer");
	f->reason = "no_internet";
	/* the driver says its transmit queue hangs (NETDEV WATCHDOG): a reconnect leaves the data path as dead, so the
	 * USB device is reset first, the reconnect after it (bench 2026-10-20: an FM350 over RNDIS, reconnected every
	 * 2-3 minutes for hours; Mag's DW5821e over MBIM 2026-11-03: the reconnect lost 2.5 min, the reset cured it) */
	if (get(srv, "tx_stall")) {
		int i;

		snprintf(buf, size, "no answer through %s; the driver's transmit queue hangs (NETDEV WATCHDOG, %d in 10 min)",
			 str(reach, "device") ? str(reach, "device") : "the modem", (int)num(get(srv, "tx_stall"), "events"));
		for (i = f->n; i > 0; i--)
			f->ladder[i] = f->ladder[i - 1];
		f->ladder[0] = 6;
		f->n++;
	}
	return true;
}

/* the first finding that holds, most fundamental first */
static bool find(json_object *srv, const struct mm_watch_cfg *cfg, struct finding *f, char *buf, size_t size)
{
	static const char *const structural[] = { "incomplete", "unbound", "wrong-mode", "locked", "firmware",
						  "service-mode", "modeswitch-rule", NULL };
	json_object *mm = get(srv, "modemmanager"), *sess = get(srv, "session"), *owner = get(srv, "owner");
	const char *conf = str(get(srv, "configuration"), "state"), *mstate = str(mm, "state");
	int i;

	memset(f, 0, sizeof(*f));
	if (!json_object_get_boolean(get(srv, "present")))
		return false;
	/* the USB port does not feed the modem: every reset only makes it worse (R11) */
	if (get(srv, "usb_power")) {
		snprintf(buf, size, "the kernel reports USB power trouble: %s", str(get(srv, "usb_power"), "last") ?
			 str(get(srv, "usb_power"), "last") : "");
		f->reason = "usb_power";
		f->evidence = buf;
		f->blocked = "USB power trouble is reported, not reset";
		return true;
	}
	for (i = 0; conf && structural[i]; i++)
		if (is(conf, structural[i])) {
			snprintf(buf, size, "configuration is %s", conf);
			f->reason = "configuration";
			f->evidence = buf;
			f->blocked = "a configuration finding is reported, not reset (spec 23)";
			return true;
		}
	if (str(owner, "conflict")) {
		f->reason = "owner_conflict";
		f->evidence = str(owner, "conflict");
		f->ladder[f->n++] = 2;
		f->threshold = cfg->conflict_ms;
		return true;
	}
	if (is(mstate, "failed")) {
		const char *why = str(mm, "failed_reason");

		snprintf(buf, size, "ModemManager state failed%s%s", why ? ": " : "", why ? why : "");
		f->evidence = buf;
		if (why && strstr(why, "sim")) {
			f->reason = "sim";
			f->blocked = "a SIM problem is reported, not reset";
			return true;
		}
		f->reason = "modem_firmware_hang";
		f->ladder[f->n++] = 2;
		f->ladder[f->n++] = 4;
		f->ladder[f->n++] = 6;
		f->threshold = cfg->failed_ms;
		return true;
	}
	/* ModemManager runs and should hold the modem but has no object for it (gave up probing a booting modem) */
	if (!mm && json_object_get_boolean(get(srv, "mm_expected"))) {
		f->reason = "mm_not_holding";
		f->evidence = "ModemManager runs but holds no modem object for it";
		f->ladder[f->n++] = 2;
		f->ladder[f->n++] = 6;
		f->threshold = cfg->failed_ms * 3 / 2;
		return true;
	}
	/* netifd has no handler for the interface's protocol (its package was removed):
	 * no reset brings the session back, the operator installs the package */
	if (sess && json_object_get_boolean(get(sess, "managed")) && str(sess, "proto_missing")) {
		snprintf(buf, size, "netifd has no handler for proto %s (its package is not installed)", str(sess, "proto_missing"));
		f->reason = "proto_missing";
		f->evidence = buf;
		f->blocked = "the protocol package is missing: installing it is the fix, not a reset";
		return true;
	}
	/* a session the engine runs (its configuration owns the interface), not one taken down on purpose */
	if (sess && json_object_get_boolean(get(sess, "managed")) && str(sess, "interface") &&
	    (is(str(sess, "state"), "failed") || is(str(sess, "state"), "down") || is(str(sess, "state"), "disconnected")) &&
	    !(get(get(sess, "netifd"), "autostart") && !json_object_get_boolean(get(get(sess, "netifd"), "autostart"))) &&
	    ((!mm && !json_object_get_boolean(get(srv, "mm_expected"))) || registered(mm) || is(mstate, "disabled"))) {	/* proto modemmanager enables it again */
		/* ModemManager still says connected with the session down: a bearer it failed to disconnect, and every new
		 * connect hits it (a field report). One session restart, then the modem's reset - no radio cycle */
		if (is(mstate, "connected")) {
			snprintf(buf, size, "ModemManager holds a connected bearer while the session on %s is %s", str(sess, "interface"),
				 str(sess, "state"));
			f->reason = "mm_bearer_stuck";
			f->evidence = buf;
			f->ladder[f->n++] = 1;
			f->ladder[f->n++] = 4;
			f->ladder[f->n++] = 6;
			f->threshold = cfg->session_ms;
			return true;
		}
		snprintf(buf, size, "session on %s is %s", str(sess, "interface"), str(sess, "state"));
		f->reason = "session_stuck";
		f->evidence = buf;
		f->ladder[f->n++] = 1;
		f->ladder[f->n++] = 3;
		f->ladder[f->n++] = 4;
		f->ladder[f->n++] = 6;
		f->threshold = cfg->session_ms;
		return true;
	}
	/* the network says no: one re-registration, then a hint, never a module reset (R36) */
	if (is(str(mm, "registration"), "denied") && !is(str(mm, "power_state"), "low")) {
		snprintf(buf, size, "the network denied registration (state %s)", mstate ? mstate : "unknown");
		f->reason = "registration_denied";
		f->evidence = buf;
		f->ladder[f->n++] = 3;
		f->threshold = cfg->registration_ms;
		return true;
	}
	if ((is(mstate, "enabled") || is(mstate, "searching")) && !is(str(mm, "power_state"), "low") && !registered(mm)) {
		snprintf(buf, size, "ModemManager %s, not registered", mstate);
		f->reason = "registration_stuck";
		f->evidence = buf;
		f->ladder[f->n++] = 3;
		f->ladder[f->n++] = 4;
		/* a scan on a weak signal outlasts a short threshold; a reset restarts it (R7) */
		f->threshold = is(mstate, "searching") && cfg->search_ms > cfg->registration_ms ? cfg->search_ms :
			       cfg->registration_ms;
		return true;
	}
	return no_internet(srv, mm, sess, cfg, f, buf, size);
}

#define MM_WATCH_MEMO_MS 900000LL	/* how long the levels tried for «no internet» outlive a clear */

static bool tried(json_object *state, int level)
{
	json_object *t = get(state, "tried");
	size_t i;

	for (i = 0; t && i < json_object_array_length(t); i++)
		if (json_object_get_int(json_object_array_get_idx(t, i)) == level)
			return true;
	return false;
}

static void reset(json_object *state)
{
	static const char *const keys[] = { "reason", "since", "tried", "running", "last_end", "stalled", "evidence",
					    "blocked", "last_start", NULL };
	int i;

	for (i = 0; keys[i]; i++)
		json_object_object_del(state, keys[i]);
}

json_object *mm_watch_step(json_object *srv, json_object *state, long long now_ms, int busy,
			   const struct mm_watch_cfg *cfg)
{
	json_object *o = json_object_new_object(), *fo, *ladder, *cand;
	struct finding f;
	char buf[192];
	long long since, due, next = -1;
	int i;

	if (!find(srv, cfg, &f, buf, sizeof(buf))) {
		if (str(state, "reason"))
			json_object_object_add(state, "cleared", json_object_new_string(str(state, "reason")));
		/* «no internet» clears whenever a reconnect makes the check start over, not because traffic came back
		 * (bench 2026-10-20: a UFI with no service and an FM350 with a hung data path got a session reconnect every
		 * 2-3 min for an hour, each «succeeded»): the levels tried are remembered a while and the ladder climbs */
		if (is(str(state, "reason"), "no_internet") && get(state, "tried")) {
			json_object_object_add(state, "memo_tried", json_object_get(get(state, "tried")));
			json_object_object_add(state, "memo_at", json_object_new_int64(now_ms));
		}
		reset(state);
		json_object_object_add(o, "finding", NULL);
		json_object_object_add(o, "next_ms", json_object_new_int64(-1));
		return o;
	}
	if (!is(str(state, "reason"), f.reason)) {
		reset(state);
		json_object_object_del(state, "cleared");
		json_object_object_add(state, "reason", json_object_new_string(f.reason));
		json_object_object_add(state, "since", json_object_new_int64(now_ms));
		if (is(f.reason, "no_internet") && get(state, "memo_tried") && now_ms - num(state, "memo_at") < MM_WATCH_MEMO_MS) {
			json_object *t = json_object_new_array(), *m = get(state, "memo_tried");
			size_t k;

			for (k = 0; k < json_object_array_length(m); k++)
				json_object_array_add(t, json_object_get(json_object_array_get_idx(m, k)));
			json_object_object_add(state, "tried", t);
		} else {
			json_object_object_add(state, "tried", json_object_new_array());
		}
		json_object_object_del(state, "memo_tried");
		json_object_object_del(state, "memo_at");
	}
	json_object_object_add(state, "evidence", json_object_new_string(f.evidence ? f.evidence : ""));
	since = num(state, "since");

	fo = json_object_new_object();
	ladder = json_object_new_array();
	json_object_object_add(fo, "reason", json_object_new_string(f.reason));
	json_object_object_add(fo, "evidence", json_object_new_string(f.evidence ? f.evidence : ""));
	json_object_object_add(fo, "since_ms", json_object_new_int64(since));
	for (i = 0; i < f.n; i++)
		json_object_array_add(ladder, json_object_new_int(f.ladder[i]));
	json_object_object_add(fo, "ladder", ladder);
	if (f.blocked)
		json_object_object_add(fo, "blocked", json_object_new_string(f.blocked));
	json_object_object_add(o, "finding", fo);

	cand = json_object_new_array();
	if (!f.blocked && !busy && !json_object_get_boolean(get(state, "running"))) {
		due = since + f.threshold;
		if (get(state, "last_end") && num(state, "last_end") + cfg->grace_ms > due)
			due = num(state, "last_end") + cfg->grace_ms;
		if (now_ms >= due) {
			for (i = 0; i < f.n; i++)
				if (!tried(state, f.ladder[i]))
					json_object_array_add(cand, json_object_new_int(f.ladder[i]));
			if (!json_object_array_length(cand))
				json_object_object_add(state, "stalled", json_object_new_string(
					is(f.reason, "no_internet") && strstr(f.evidence ? f.evidence : "", "NETDEV WATCHDOG") ?
					"the data path hangs in the USB network driver after a reconnect and a USB reset: check USB autosuspend, the cable and the hub's power (an FM350 over USB has RNDIS compositions only)" :
					is(f.reason, "no_internet") ?
					"carrier restriction: the session has an address but no traffic after a reconnect" :
					is(f.reason, "registration_denied") ?
					"registration denied: the SIM is barred or not allowed here, or the firmware blocks this country's networks" :
					"every level of the ladder was tried; manual recovery required"));
		} else {
			next = due;
		}
	}
	json_object_object_add(o, "candidates", cand);
	json_object_object_add(o, "next_ms", json_object_new_int64(next));
	return o;
}

void mm_watch_tried(json_object *state, int level, long long now_ms)
{
	json_object *t = get(state, "tried");

	if (!t) {
		t = json_object_new_array();
		json_object_object_add(state, "tried", t);
	}
	json_object_array_add(t, json_object_new_int(level));
	json_object_object_add(state, "running", json_object_new_boolean(1));
	json_object_object_add(state, "last_start", json_object_new_int64(now_ms));
}

void mm_watch_finished(json_object *state, long long now_ms)
{
	json_object_object_add(state, "running", json_object_new_boolean(0));
	json_object_object_add(state, "last_end", json_object_new_int64(now_ms));
}

void mm_watch_skip(json_object *state, int level, const char *why)
{
	json_object *t = get(state, "tried");
	char line[256];

	if (!t) {
		t = json_object_new_array();
		json_object_object_add(state, "tried", t);
	}
	json_object_array_add(t, json_object_new_int(level));
	snprintf(line, sizeof(line), "L%d: %s", level, why ? why : "refused");
	json_object_object_add(state, "blocked", json_object_new_string(line));
}

json_object *mm_watch_view(json_object *state, long long now_ms)
{
	json_object *o = json_object_new_object();
	const char *st = !str(state, "reason") ? "ok" : json_object_get_boolean(get(state, "running")) ? "recovering" :
			 str(state, "stalled") ? "stalled" : "watching";

	json_object_object_add(o, "state", json_object_new_string(st));
	if (str(state, "reason")) {
		json_object_object_add(o, "reason", json_object_new_string(str(state, "reason")));
		json_object_object_add(o, "evidence", json_object_new_string(str(state, "evidence") ? str(state, "evidence") : ""));
		json_object_object_add(o, "for_s", json_object_new_int64((now_ms - num(state, "since")) / 1000));
		json_object_object_add(o, "tried", json_object_get(get(state, "tried")));
	}
	if (str(state, "stalled"))
		json_object_object_add(o, "stalled", json_object_new_string(str(state, "stalled")));
	if (str(state, "blocked"))
		json_object_object_add(o, "blocked", json_object_new_string(str(state, "blocked")));
	return o;
}
