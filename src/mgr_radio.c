/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "at.h"
#include "daemon.h"
#include "radio.h"

/*
 * The radio settings manager (spec 25.9.12 stage 2, daemon.h): a change, the
 * wait for the connection, the rollback; journalled so a restart of the
 * engine keeps the way back (audit F03).
 */

#define RADIO_WAIT_S 120		/* the connection must come back within this, or the change goes back */
#define RADIO_WAIT_RESTART_S 240	/* ... after a module restart (a cell lock on the T99W175) */
#ifndef RADIO_WAIT_MIN_S
#define RADIO_WAIT_MIN_S 30	/* (tests: shorter) */
#endif
#ifndef RADIO_SETTLE_MS
#define RADIO_SETTLE_MS 20000	/* the modem re-registers: no verdict before this (tests: shorter) */
#endif
#define RADIO_STEP_MS 30000	/* one mmcli or modem_ctrl.sh write */
/* a change that is over (kept, rolled back) offers «Вернуть как было» this long, and not after a restart of the engine
 * (the owner, 2026-10-26: «Изменение оставлено» stayed on a T77 after a reboot) */
#define RADIO_DONE_S 3600

static json_object *radio_txn;		/* device_id -> the last change of its radio settings */

/*
 * The cell-lock guard (the owner, 2026-10-28): a modem locked to a
 * cell that goes away (a sector switched off, the operator's works) sits without a network until someone notices. A
 * lock MedveModem set and that was kept is armed here with the way back read before it; when the modem has had no
 * registration for core.radio_lock_guard minutes, the old settings are written back (not «everything auto»). A few
 * tries 10 minutes apart, then it gives up; the guard is gone once the lock is undone, by it or by anyone.
 */
#ifndef RADIO_GUARD_UNIT_MS
#define RADIO_GUARD_UNIT_MS 60000	/* a guard minute (tests: shorter) */
#endif
#define RADIO_GUARD_DEFAULT "10"
#define RADIO_GUARD_TRIES 3
#define RADIO_GUARD_AGAIN_MIN 10	/* between tries */
static json_object *radio_guard;	/* device_id -> {via, dialect, back, lock, prev_lock, tries}, journalled */
static json_object *guard_clock;	/* device_id -> {since, next, read, armed} monotonic ms, RAM only */

static json_object *radio_txn_of(const char *id)
{
	return radio_txn ? json_object_object_get(radio_txn, id) : NULL;
}

/*
 * The changes are journalled in the state directory before the first write
 * and at every step (audit F03): an engine that restarts in the middle still
 * knows the way back. Only the steps are kept: the owner, its ModemManager
 * index, the tty or the QModem section are resolved again when they run.
 */
static void radio_txn_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	snprintf(path, sizeof(path), "%s/radio-txn.json", state_dir);
	if (!radio_txn || !json_object_object_length(radio_txn)) {
		unlink(path);
		return;
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(state_dir, 0755);
	if (json_object_to_file_ext(tmp, radio_txn, JSON_C_TO_STRING_PLAIN) || rename(tmp, path))
		ULOG_ERR("cannot write %s: %s\n", path, strerror(errno));
}

static bool radio_done(json_object *t)
{
	const char *s = jstr(t, "state");

	return s && (!strcmp(s, "kept") || !strcmp(s, "rolled_back"));
}

/* the changes that are over: all of them at start, else those older than RADIO_DONE_S */
static void radio_forget_done(bool all)
{
	json_object *gone = json_object_new_array();
	long long now = (long long)time(NULL);
	size_t i;

	json_object_object_foreach(radio_txn, id, t)
		if (radio_done(t) && (all || now - json_object_get_int64(json_object_object_get(t, "at")) >= RADIO_DONE_S))
			json_object_array_add(gone, json_object_new_string(id));
	for (i = 0; i < json_object_array_length(gone); i++)
		json_object_object_del(radio_txn, json_object_get_string(json_object_array_get_idx(gone, i)));
	if (json_object_array_length(gone))
		radio_txn_save();
	json_object_put(gone);
}

static void radio_guard_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	snprintf(path, sizeof(path), "%s/radio-guard.json", state_dir);
	if (!radio_guard || !json_object_object_length(radio_guard)) {
		unlink(path);
		return;
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(state_dir, 0755);
	if (json_object_to_file_ext(tmp, radio_guard, JSON_C_TO_STRING_PLAIN) || rename(tmp, path))
		ULOG_ERR("cannot write %s: %s\n", path, strerror(errno));
}

/* minutes without a registration before the lock goes back; 0: off */
static int radio_guard_minutes(void)
{
	int m = atoi(core_opt("radio_lock_guard", RADIO_GUARD_DEFAULT));

	return m < 0 ? 0 : m > 120 ? 120 : m;
}

static json_object *radio_guard_clock(const char *id)
{
	json_object *c;

	if (!guard_clock)
		guard_clock = json_object_new_object();
	if (!(c = json_object_object_get(guard_clock, id))) {
		c = json_object_new_object();
		json_object_object_add(guard_clock, id, c);
	}
	return c;
}

static long long clock_get(json_object *c, const char *k)
{
	return json_object_get_int64(json_object_object_get(c, k));
}

static void clock_set(json_object *c, const char *k, long long v)
{
	json_object_object_add(c, k, json_object_new_int64(v));
}

/* the guard's kind: a cell lock's (absent: written before r160) or a band restriction's (the owner, 2026-11-06,
 * band failover: a band lock that loses the network goes back like a cell lock) */
static bool guard_bands(json_object *g)
{
	return jstr(g, "kind") && !strcmp(jstr(g, "kind"), "bands");
}

static const char *guard_event(json_object *g)
{
	return guard_bands(g) ? "band-guard" : "radio-guard";
}

static void radio_guard_drop(const char *id, const char *why)
{
	if (!radio_guard || !json_object_object_get(radio_guard, id))
		return;
	ULOG_INFO("radio %s: the cell-lock guard is off (%s)\n", id, why);
	json_object_object_del(radio_guard, id);
	if (guard_clock)
		json_object_object_del(guard_clock, id);
	radio_guard_save();
}

/*
 * a change is over and stays (kept, or applied with no connection to watch): a lock arms the guard with the way back
 * read before it; an unlock disarms it; a change of the modes or bands alone keeps the guard with a way back that
 * undoes the lock only (computed when that change started, "guard_back"), so it never brings old bands back
 */
static void radio_settled(const char *id, json_object *t)
{
	json_object *want = json_object_object_get(t, "want"), *cl = NULL, *g;

	if (!json_object_object_get_ex(want, "cell_lock", &cl)) {
		json_object *bands = json_object_object_get(want, "bands");

		g = radio_guard ? json_object_object_get(radio_guard, id) : NULL;
		/* a band restriction arms a band guard with the settings read before it; «any» bands disarm it; a later
		 * restriction keeps the first way back (to the bands before any restriction) */
		if (!g && json_object_is_type(bands, json_type_array) && json_object_array_length(bands) &&
		    json_object_object_get(t, "back")) {
			if (!radio_guard)
				radio_guard = json_object_new_object();
			g = json_object_new_object();
			json_object_object_add(g, "kind", json_object_new_string("bands"));
			json_object_object_add(g, "via", json_object_get(json_object_object_get(t, "via")));
			json_object_object_add(g, "dialect", json_object_get(json_object_object_get(t, "dialect")));
			json_object_object_add(g, "back", json_object_get(json_object_object_get(t, "back")));
			/* the bands before any restriction: a later change of the modes rebuilds the way back from them */
			json_object_object_add(g, "prev_bands", json_object_get(json_object_object_get(json_object_object_get(t, "previous"), "bands")));
			json_object_object_add(g, "tries", json_object_new_int(0));
			json_object_object_add(radio_guard, id, g);
			clock_set(radio_guard_clock(id), "armed", now_mono_ms());
			clock_set(radio_guard_clock(id), "since", 0);
			radio_guard_save();
			ULOG_INFO("radio %s: the band-lock guard is armed\n", id);
			return;
		}
		if (g && guard_bands(g)) {
			if (json_object_is_type(bands, json_type_string) && !strcmp(json_object_get_string(bands), "any"))
				radio_guard_drop(id, "the bands are any again");
			else if (json_object_object_get(t, "guard_back")) {
				/* the audit of r162 (item 4): a change of the modes after the restriction stays; the way back is
				 * the bands before it with the modes as they are now */
				json_object_object_add(g, "back", json_object_get(json_object_object_get(t, "guard_back")));
				radio_guard_save();
				ULOG_INFO("radio %s: the band guard's way back follows the later change\n", id);
			}
			return;
		}
		if (!g)
			return;
		if (!json_object_object_get(t, "guard_back")) {
			radio_guard_drop(id, "the lock's way back could not be planned after another change");
			return;
		}
		json_object_object_add(g, "back", json_object_get(json_object_object_get(t, "guard_back")));
		radio_guard_save();
		return;
	}
	if (!json_object_is_type(cl, json_type_object)) {
		radio_guard_drop(id, "the cell lock is off");
		return;
	}
	if (!radio_guard)
		radio_guard = json_object_new_object();
	g = json_object_new_object();
	json_object_object_add(g, "via", json_object_get(json_object_object_get(t, "via")));
	json_object_object_add(g, "dialect", json_object_get(json_object_object_get(t, "dialect")));
	json_object_object_add(g, "back", json_object_get(json_object_object_get(t, "back")));
	json_object_object_add(g, "lock", json_object_get(cl));
	json_object_object_add(g, "prev_lock", json_object_get(json_object_object_get(json_object_object_get(t, "previous"), "cell_lock")));
	json_object_object_add(g, "tries", json_object_new_int(0));
	json_object_object_add(radio_guard, id, g);
	clock_set(radio_guard_clock(id), "armed", now_mono_ms());
	clock_set(radio_guard_clock(id), "since", 0);
	radio_guard_save();
	ULOG_INFO("radio %s: the cell-lock guard is armed\n", id);
}

/*
 * is the modem on a network: 1 yes, 0 no, -1 not known. A working connection is a yes; ModemManager's registration
 * when it holds the modem (a disabled, locked or failed modem is not the lock's doing: not known); else the engine's
 * AT reading, asked for once a guard minute while the connection is down
 */
static int radio_guard_registered(const char *id, json_object *rec, json_object *clock, long long now)
{
	json_object *u = uplinks ? json_object_object_get(uplinks, id) : NULL, *r = json_object_object_get(u, "reach");
	json_object *mm = mm_devices ? json_object_object_get(mm_devices, id) : NULL, *m, *reg;
	const char *lv = jstr(r, "level"), *st, *why = NULL, *node;
	int seen = 0;

	if (lv && (!strcmp(lv, "ok") || !strcmp(lv, "limited")))
		return 1;
	if (mm) {
		const char *rg = jstr(mm, "registration");

		st = jstr(mm, "state");
		if ((rg && (!strcmp(rg, "home") || !strcmp(rg, "roaming"))) ||
		    (st && (!strcmp(st, "registered") || !strcmp(st, "connecting") || !strcmp(st, "connected"))))
			return 1;
		return st && (!strcmp(st, "searching") || !strcmp(st, "enabled")) ? 0 : -1;
	}
	if ((m = metrics_cached(id, 2 * RADIO_GUARD_UNIT_MS)) && (reg = json_object_object_get(m, "registration")) &&
	    json_object_is_type(reg, json_type_object)) {
		json_object_object_foreach(reg, k, v) {
			const char *x = json_object_get_string(v);

			(void)k;
			if (x && (!strcmp(x, "home") || !strcmp(x, "roaming")))
				return 1;
			seen = 1;
		}
		if (seen)
			return 0;
	}
	/* no fresh reading: one in the background over the engine's AT port, if it has one and is free */
	if (now >= clock_get(clock, "read") && !at_running(id, AT_METRICS) && !at_running(id, AT_RADIO)) {
		json_object *srv = served(rec), *cmds;
		size_t i;

		clock_set(clock, "read", now + RADIO_GUARD_UNIT_MS);
		if ((node = engine_at_port(srv, &why))) {
			cmds = json_object_new_array();
			for (i = 0; mm_at_metrics_commands[i]; i++)
				json_object_array_add(cmds, json_object_new_string(mm_at_metrics_commands[i]));
			at_begin(NULL, NULL, rec, AT_METRICS, node, cmds, NULL, MM_AT_RADIO_MS, NULL);
		}
		json_object_put(srv);
	}
	return -1;
}

static int radio_run(struct ubus_context *ctx, struct ubus_request_data *req, json_object *rec, json_object *srv,
		     const char *via, const char *dialect, json_object *steps, const char *op);
static void radio_txn_set(json_object *t, const char *state, const char *why);

/* the uplink tick: an armed lock whose modem has had no network long enough goes back */
static void radio_guard_tick(void)
{
	int minutes = radio_guard_minutes();
	long long now = now_mono_ms();
	json_object *gone;
	size_t i;

	if (!radio_guard || !json_object_object_length(radio_guard))
		return;
	gone = json_object_new_array();
	json_object_object_foreach(radio_guard, id, g) {
		json_object *rec = mm_registry_find(&registry, id), *clock = radio_guard_clock(id), *t = radio_txn_of(id);
		json_object *rc = radio_cache ? json_object_object_get(radio_cache, id) : NULL, *cl = NULL, *srv;
		const char *st = t ? jstr(t, "state") : NULL;
		int reg, tries;
		char why[128];

		/* a band restriction undone by someone else: a reading after the arming says any bands */
		if (guard_bands(g) && rc && clock_get(rc, "at") > clock_get(clock, "armed") && rec &&
		    json_object_get_int64(json_object_object_get(rc, "generation")) == json_object_get_int64(json_object_object_get(rec, "generation")) &&
		    json_object_get_boolean(json_object_object_get(json_object_object_get(rc, "radio"), "bands_any"))) {
			json_object_array_add(gone, json_object_new_string(id));
			continue;
		}
		/* undone by someone else (the modem's page, QModem, a reset): a reading after the arming says no lock */
		if (!guard_bands(g) && rc && clock_get(rc, "at") > clock_get(clock, "armed") && rec &&
		    json_object_get_int64(json_object_object_get(rc, "generation")) == json_object_get_int64(json_object_object_get(rec, "generation")) &&
		    json_object_object_get(json_object_object_get(rc, "radio"), "cell_lock_rats") &&
		    json_object_object_get_ex(json_object_object_get(rc, "radio"), "cell_lock", &cl) && !json_object_is_type(cl, json_type_object)) {
			json_object_array_add(gone, json_object_new_string(id));
			continue;
		}
		if (!minutes || !rec || !json_object_get_boolean(json_object_object_get(rec, "present")) || frozen(id) ||
		    at_running(id, AT_RADIO) || (st && (!strcmp(st, "applying") || !strcmp(st, "waiting") ||
							 !strcmp(st, "rolling_back") || !strcmp(st, "failed")))) {
			clock_set(clock, "since", 0);
			continue;
		}
		if ((reg = radio_guard_registered(id, rec, clock, now)) == 1)
			clock_set(clock, "since", 0);
		if (reg)
			continue;
		if (!clock_get(clock, "since"))
			clock_set(clock, "since", now);
		if (now - clock_get(clock, "since") < (long long)minutes * RADIO_GUARD_UNIT_MS || now < clock_get(clock, "next"))
			continue;
		tries = json_object_get_int(json_object_object_get(g, "tries")) + 1;
		json_object_object_add(g, "tries", json_object_new_int(tries));
		radio_guard_save();
		clock_set(clock, "next", now + (long long)RADIO_GUARD_AGAIN_MIN * RADIO_GUARD_UNIT_MS);
		snprintf(why, sizeof(why), "no network for %d min", minutes);
		if (tries == 1)
			mm_history_add(&history, id, (long long)time(NULL), now, guard_event(g), why);
		history_dirty = true;
		ULOG_WARN("radio %s: %s with the %s lock: the settings before it go back (try %d)\n", id, why,
			  guard_bands(g) ? "band" : "cell", tries);
		srv = served(rec);
		if (radio_run(NULL, NULL, rec, srv, jstr(g, "via"), jstr(g, "dialect"), json_object_object_get(g, "back"), "guard") !=
		    UBUS_STATUS_OK || !at_running(id, AT_RADIO)) {
			if (tries >= RADIO_GUARD_TRIES) {
				mm_history_add(&history, id, (long long)time(NULL), now, guard_event(g), "could not start");
				json_object_array_add(gone, json_object_new_string(id));
			}
		}
		json_object_put(srv);
	}
	for (i = 0; i < json_object_array_length(gone); i++)
		radio_guard_drop(json_object_get_string(json_object_array_get_idx(gone, i)), "the lock is undone");
	json_object_put(gone);
}

/* the guard's write finished: the lock is gone, or a later try (a few, then it gives up) */
static bool radio_guard_done(struct at_job *j, bool ok, const char *error)
{
	json_object *g = radio_guard ? json_object_object_get(radio_guard, j->device_id) : NULL, *t = radio_txn_of(j->device_id);
	const char *st = t ? jstr(t, "state") : NULL;

	if (ok) {
		bool bands = guard_bands(g);

		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), guard_event(g), "done");
		history_dirty = true;
		ULOG_WARN("radio %s: the %s lock is taken back by its guard\n", j->device_id, bands ? "band" : "cell");
		/* the lock's own change shows as taken back: «Вернуть как было» has nothing left to do */
		if (st && (!strcmp(st, "kept") || !strcmp(st, "applied"))) {
			radio_txn_set(t, "rolled_back", bands ? "the band lock guard took it back: no network" :
				      "the cell lock guard took it back: no network");
			broadcast("medvemodem.radio", t);
		}
		radio_guard_drop(j->device_id, "taken back");
		return true;
	}
	ULOG_WARN("radio %s: the %s guard could not write the old settings: %s\n", j->device_id, guard_bands(g) ? "band-lock" : "cell-lock",
		  error ? error : "the modem refused them");
	if (g && json_object_get_int(json_object_object_get(g, "tries")) >= RADIO_GUARD_TRIES) {
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), guard_event(g), "failed");
		history_dirty = true;
		radio_guard_drop(j->device_id, "it gave up");
	}
	return false;
}

/*
 * at start: a change caught writing or rolling back is written back (the next
 * uplink ticks, once its modem is present); a waiting one waits again from
 * now, its connection not checked yet
 */
static void radio_guard_load(void);

void radio_mgr_start(void)
{
	char path[PATH_MAX];
	json_object *o;

	radio_guard_load();

	snprintf(path, sizeof(path), "%s/radio-txn.json", state_dir);
	o = json_object_from_file(path);
	if (!o || !json_object_is_type(o, json_type_object)) {
		json_object_put(o);
		return;
	}
	json_object_object_foreach(o, id, t) {
		char state[24];

		snprintf(state, sizeof(state), "%s", jstr(t, "state") ? jstr(t, "state") : "");	/* replaced below */
		if (!*state || !json_object_object_get(t, "back")) {
			continue;
		} else if (!strcmp(state, "applying") || !strcmp(state, "rolling_back")) {
			json_object_object_add(t, "state", json_object_new_string("failed"));
			json_object_object_add(t, "why", json_object_new_string("the engine restarted during the change; the settings read before it are written back"));
			mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "radio-rollback", "after a restart");
			history_dirty = true;
		} else if (!strcmp(state, "waiting")) {
			json_object_object_add(t, "applied_mono", json_object_new_int64(now_mono_ms()));
		}
		ULOG_WARN("radio %s: change journalled as %s before the restart\n", id, state);
	}
	json_object_put(radio_txn);
	radio_txn = o;
	radio_forget_done(true);
}

static void radio_guard_load(void)
{
	char path[PATH_MAX];
	json_object *o;

	snprintf(path, sizeof(path), "%s/radio-guard.json", state_dir);
	o = json_object_from_file(path);
	if (!o || !json_object_is_type(o, json_type_object)) {
		json_object_put(o);
		return;
	}
	json_object_put(radio_guard);
	radio_guard = o;
	json_object_object_foreach(o, id, g) {
		(void)g;
		clock_set(radio_guard_clock(id), "armed", now_mono_ms());
		ULOG_INFO("radio %s: the cell-lock guard is armed (kept across the restart)\n", id);
	}
}

static void radio_txn_set(json_object *t, const char *state, const char *why)
{
	json_object_object_add(t, "state", json_object_new_string(state));
	json_object_object_add(t, "at", json_object_new_int64((long long)time(NULL)));
	if (why)
		json_object_object_add(t, "why", json_object_new_string(why));
	else
		json_object_object_del(t, "why");
	radio_txn_save();
}

/* a short line of what was set, for the journal: modes and bands, nothing else */
static void radio_summary(json_object *want, char *out, size_t size)
{
	json_object *m = json_object_object_get(want, "modes"), *b = json_object_object_get(want, "bands"), *a;
	size_t used = 0, i;

	out[0] = '\0';
	if (m && (a = json_object_object_get(m, "allowed")))
		for (i = 0; i < json_object_array_length(a) && used + 8 < size; i++)
			used += (size_t)snprintf(out + used, size - used, "%s%s", i ? "+" : "modes ", json_object_get_string(json_object_array_get_idx(a, i)));
	if (m && jstr(m, "preferred") && used + 24 < size)
		used += (size_t)snprintf(out + used, size - used, " pref %s", jstr(m, "preferred"));
	if (json_object_object_get_ex(want, "cell_lock", &a) && used + 48 < size) {
		if (json_object_is_type(a, json_type_object))
			used += (size_t)snprintf(out + used, size - used, "%scell lock %s %lld pci %s", used ? "; " : "",
						 jstr(a, "rat") ? jstr(a, "rat") : "lte",
						 (long long)json_object_get_int64(json_object_object_get(a, "arfcn")),
						 json_object_object_get(a, "pci") ? json_object_get_string(json_object_object_get(a, "pci")) : "any");
		else
			used += (size_t)snprintf(out + used, size - used, "%scell lock off", used ? "; " : "");
	}
	if (b && json_object_is_type(b, json_type_string) && used + 16 < size)
		used += (size_t)snprintf(out + used, size - used, "%sbands any", used ? "; " : "");
	else if (b && json_object_is_type(b, json_type_array))
		for (i = 0; i < json_object_array_length(b) && used + 16 < size; i++)
			used += (size_t)snprintf(out + used, size - used, "%s%s", i ? "," : used ? "; bands " : "bands ",
						 json_object_get_string(json_object_array_get_idx(b, i)));
}

/*
 * Carry out steps (mm_radio_plan) through the modem's owner. op "set" or
 * "rollback"; ctx NULL when the engine starts it (the automatic rollback).
 * The reads after a write refresh the reading (QModem, AT).
 */
static int radio_run(struct ubus_context *ctx, struct ubus_request_data *req, json_object *rec, json_object *srv,
		     const char *via, const char *dialect, json_object *steps, const char *op)
{
	json_object *cmds = json_object_new_array(), *x = json_object_new_object();
	const char *node = NULL, *why = NULL, *const *reads;
	size_t i, k;
	int timeout = RADIO_STEP_MS;

	json_object_object_add(x, "op", json_object_new_string(op));
	json_object_object_add(x, "steps", json_object_new_int((int)json_object_array_length(steps)));
	for (i = 0; i < json_object_array_length(steps); i++) {
		json_object *st = json_object_array_get_idx(steps, i);
		char line[1024];
		size_t used = 0;

		for (k = 0; k < json_object_array_length(st) && used + 2 < sizeof(line); k++)
			used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%s", k ? "\x1f" : "",
						 json_object_get_string(json_object_array_get_idx(st, k)));
		json_object_array_add(cmds, json_object_new_string(line));
	}
	if (!strcmp(via, "modemmanager")) {
		json_object *mm = json_object_object_get(srv, "modemmanager");
		char index[24];

		json_object *nw = json_object_object_get(json_object_object_get(srv, "radio_settings"), "narrowed");

		snprintf(index, sizeof(index), "%lld", (long long)json_object_get_int64(json_object_object_get(mm, "index")));
		json_object_object_add(x, "mm_index", json_object_new_string(index));
		/* a ModemManager narrowed to one mode: the control port a «qmi» step writes the stored preference through */
		if (nw) {
			json_object_object_add(x, "qmi_wdm", json_object_get(json_object_object_get(nw, "wdm")));
			json_object_object_add(x, "qmi_mbim", json_object_get(json_object_object_get(nw, "mbim")));
		}
	} else if (!strcmp(via, "qmodem")) {
		const char *sec = qmodem_section_of(rec);

		if (!sec || !qmodem_installed())
			why = "QModem drives this modem but has no section for it";
		else if (qmodem_foreign_port(rec, sec))
			why = "QModem's section names an AT port that is not this modem's; nothing is sent";
		else {
			json_object_object_add(x, "qmodem_section", json_object_new_string(sec));
			json_object_array_add(cmds, json_object_new_string("get_network_prefer"));
			json_object_array_add(cmds, json_object_new_string("get_lockband"));
		}
	} else {
		node = engine_at_port(srv, &why);
		json_object_object_add(x, "dialect", json_object_new_string(dialect ? dialect : ""));
		timeout = MM_AT_RADIO_MS;
		for (reads = mm_radio_commands(dialect); reads && *reads; reads++)
			json_object_array_add(cmds, json_object_new_string(*reads));
	}
	if (why) {
		json_object_put(cmds);
		json_object_put(x);
		return ctx ? at_refuse(ctx, req, jstr(rec, "device_id"), why, NULL) : UBUS_STATUS_UNKNOWN_ERROR;
	}
	return at_begin(ctx, req, rec, AT_RADIO, node, cmds, NULL, timeout, x);
}

/* a module restart as the last step: its answer may never come, the port goes with the modem */
static bool radio_restarts(json_object *steps)
{
	size_t n = json_object_array_length(steps);
	json_object *last = n ? json_object_array_get_idx(steps, n - 1) : NULL;

	return last && !strcmp(json_object_get_string(json_object_array_get_idx(last, 0)), "AT+CFUN=1,1");
}

/* after the worker: did every write take? (a restart that ends the steps counts once the others did) */
static bool radio_steps_ok(struct at_job *j, json_object *responses, const char *error)
{
	int n = json_object_get_int(json_object_object_get(j->results, "steps")), i;

	for (i = 0; i < n; i++) {
		const char *cmd = json_object_get_string(json_object_array_get_idx(j->cmds, i));
		char key[16];

		if (i == n - 1 && j->node[0] && !strcmp(cmd, "AT+CFUN=1,1") && n > 1 &&
		    (error || !jstr(responses, cmd) || !*jstr(responses, cmd)))
			return true;
		if (error)
			return false;

		snprintf(key, sizeof(key), "step%d", i);
		if (jstr(j->results, "mm_index")) {
			if (!jstr(responses, key) || strcmp(jstr(responses, key), "ok"))
				return false;
		} else if (!j->node[0]) {
			if (!json_object_object_get(responses, key))
				return false;
		} else {
			json_object *pr = mm_at_parse(cmd, jstr(responses, cmd) ? jstr(responses, cmd) : "");
			bool ok = json_object_get_boolean(json_object_object_get(pr, "ok"));

			json_object_put(pr);
			if (!ok)
				return false;
		}
	}
	return n > 0;
}

static void radio_rollback(const char *id, const char *why)
{
	json_object *t = radio_txn_of(id), *rec = mm_registry_find(&registry, id), *srv;

	int tries;
	char reason[160];

	if (!t || !rec || !json_object_get_boolean(json_object_object_get(rec, "present")))
		return;		/* the modem is away: tried again when it is back */
	tries = json_object_get_int(json_object_object_get(t, "tries")) + 1;
	json_object_object_add(t, "tries", json_object_new_int(tries));
	snprintf(reason, sizeof(reason), "%s", why);
	srv = served(rec);
	radio_txn_set(t, "rolling_back", reason);
	if (tries == 1)
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "radio-rollback", reason);
	ULOG_WARN("radio %s: rolling back (%s)\n", id, reason);
	if (radio_run(NULL, NULL, rec, srv, jstr(t, "via"), jstr(t, "dialect"), json_object_object_get(t, "back"), "rollback") !=
	    UBUS_STATUS_OK || !at_running(id, AT_RADIO)) {
		/* the next uplink tick tries again, a few times */
		radio_txn_set(t, tries >= 5 ? "rollback_failed" : "failed", reason);
		if (tries >= 5)
			mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "radio-rollback", "could not start");
	}
	json_object_put(srv);
}

bool radio_txn_done(struct at_job *j, json_object *responses, const char *error, json_object *reply)
{
	json_object *t = radio_txn_of(j->device_id);
	const char *op = jstr(j->results, "op");
	bool ok = radio_steps_ok(j, responses, error);

	if (op && !strcmp(op, "guard"))
		return radio_guard_done(j, ok, ok ? NULL : error);
	if (!t)
		return false;
	if (ok)
		error = NULL;	/* the restart took the port: expected */
	if (!strcmp(op, "set")) {
		if (!ok) {
			json_object_object_add(reply, "error", json_object_new_string(error ? error : "the modem refused the change"));
			/* a part may have taken: the next tick writes back what was read (this job still holds the slot) */
			radio_txn_set(t, "failed", "the change was refused; the settings read before it are written back");
			mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "radio-change", "refused");
			broadcast("medvemodem.radio", t);
			return false;
		}
		json_object_object_add(t, "applied_mono", json_object_new_int64(now_mono_ms()));
		radio_txn_set(t, json_object_get_boolean(json_object_object_get(t, "watch")) ? "waiting" : "applied", NULL);
		if (!strcmp(jstr(t, "state"), "applied"))
			radio_settled(j->device_id, t);
		json_object_object_add(reply, "change", json_object_get(t));
	} else {
		radio_txn_set(t, ok ? "rolled_back" : "rollback_failed", ok ? jstr(t, "why") : error ? error : "the modem refused the old settings");
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "radio-rollback", ok ? "done" : "failed");
		ULOG_WARN("radio %s: rollback %s\n", j->device_id, ok ? "done" : "failed");
	}
	broadcast("medvemodem.radio", t);
	return ok;
}

/* the uplink tick: a waiting change is kept once its modem answers again, rolled back when the time is up */
void radio_mgr_tick(void)
{
	long long now = now_mono_ms();

	radio_guard_tick();
	if (!radio_txn)
		return;
	radio_forget_done(false);
	json_object_object_foreach(radio_txn, id, t) {
		const char *state = jstr(t, "state");
		long long applied = json_object_get_int64(json_object_object_get(t, "applied_mono"));
		json_object *u = uplinks ? json_object_object_get(uplinks, id) : NULL, *r = json_object_object_get(u, "reach");

		if (state && !strcmp(state, "failed") && !at_running(id, AT_RADIO)) {
			radio_rollback(id, jstr(t, "why") ? jstr(t, "why") : "the change failed");
			continue;
		}
		if (!state || strcmp(state, "waiting"))
			continue;
		/* a check made after the modem settled, and it answered */
		if (now - applied >= RADIO_SETTLE_MS && r && jstr(r, "level") && !json_object_object_get(r, "pending") &&
		    (!strcmp(jstr(r, "level"), "ok") || !strcmp(jstr(r, "level"), "limited")) &&
		    json_object_get_int64(json_object_object_get(r, "mono")) > applied + RADIO_SETTLE_MS / 2) {
			radio_txn_set(t, "kept", NULL);
			radio_settled(id, t);
			mm_history_add(&history, id, (long long)time(NULL), now, "radio-kept", "the connection came back");
			broadcast("medvemodem.radio", t);
		} else if (now - applied >= json_object_get_int64(json_object_object_get(t, "wait_s")) * 1000) {
			char why[96];

			snprintf(why, sizeof(why), "the connection did not come back in %lld s",
				 (long long)json_object_get_int64(json_object_object_get(t, "wait_s")));
			radio_rollback(id, why);
		}
	}
}

/* the device record's view of the last change */
json_object *radio_change_view(const char *id)
{
	json_object *t = radio_txn_of(id), *o;
	long long applied;

	if (!t)
		return NULL;
	o = json_object_new_object();
	json_object_object_foreach(t, k, v)
		if (strcmp(k, "back") && strcmp(k, "applied_mono") && strcmp(k, "previous") && strcmp(k, "guard_back"))
			json_object_object_add(o, k, json_object_get(v));
	applied = json_object_get_int64(json_object_object_get(t, "applied_mono"));
	if (applied && jstr(t, "state") && !strcmp(jstr(t, "state"), "waiting")) {
		long long left = json_object_get_int64(json_object_object_get(t, "wait_s")) - (now_mono_ms() - applied) / 1000;
		json_object_object_add(o, "left_s", json_object_new_int64(left > 0 ? left : 0));
	}
	return o;
}

enum { RSET_ID, RSET_WAIT, __RSET_MAX };
const struct blobmsg_policy radio_set_policy[2] = {
	[RSET_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[RSET_WAIT] = { .name = "wait_s", .type = BLOBMSG_TYPE_INT32 },
};

/*
 * radio_set {id, modes?: {allowed, preferred?}, bands?: [..] | "any", wait_s?}:
 * the operator's change. Refused unless the reading is recent, the owner can
 * carry it out and the way back can be written; while the modem had a working
 * connection, it must come back within wait_s or the change goes back.
 */
int m_radio_set(struct ubus_context *ctx, struct ubus_object *obj,
		       struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__RSET_MAX];
	json_object *rec, *srv, *rs, *want = NULL, *plan, *back, *prev, *t, *cached, *u, *r, *guard_back = NULL;
	const char *id, *via, *dialect, *state;
	char why[256], *text;
	int rc, wait = RADIO_WAIT_S;
	bool watch;

	(void)obj; (void)method;
	blobmsg_parse(radio_set_policy, __RSET_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[RSET_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[RSET_ID])))
		return mm_registry_find(&registry, blobmsg_get_string(tb[RSET_ID])) ?
			at_refuse(ctx, req, blobmsg_get_string(tb[RSET_ID]), "device not present", NULL) : UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	if (tb[RSET_WAIT]) {
		wait = (int)blobmsg_get_u32(tb[RSET_WAIT]);
		if (wait < RADIO_WAIT_MIN_S || wait > 600)
			return at_refuse(ctx, req, id, "wait_s: 30 to 600", NULL);
	}
	t = radio_txn_of(id);
	state = t ? jstr(t, "state") : NULL;
	if (state && (!strcmp(state, "applying") || !strcmp(state, "waiting") || !strcmp(state, "rolling_back")))
		return at_refuse(ctx, req, id, "a change of this modem's radio settings is still running", NULL);
	text = blobmsg_format_json(msg, true);
	want = text ? json_tokener_parse(text) : NULL;
	free(text);
	if (want) {
		json_object_object_del(want, "id");
		json_object_object_del(want, "wait_s");
		json_object_object_del(want, "raw");	/* only the engine writes raw values back */
		json_object_object_del(want, "ubus_rpc_session");	/* rpcd's, from LuCI: not a setting (bench 2026-10-26) */
	}
	srv = served(rec);
	rs = json_object_object_get(srv, "radio_settings");
	via = jstr(rs, "via");
	dialect = jstr(rs, "dialect");
	cached = json_object_object_get(radio_cache, id);
	/* a cell lock alone on a T99 that ModemManager holds: ModemManager has none, so it goes over the engine's AT
	 * port as for a T99 without it, from the engine's own reading (modes and bands stay ModemManager's) */
	if (!strcmp(via, "modemmanager") && mm_lock_dialect(dialect) && want && json_object_object_length(want) == 1 &&
	    json_object_object_get_ex(want, "cell_lock", NULL)) {
		const char *no = NULL;

		if (!engine_at_port(srv, &no)) {
			rc = at_refuse(ctx, req, id, no, NULL);
			goto out;
		}
		if (!cached || strcmp(jstr(cached, "via") ? jstr(cached, "via") : "", "at")) {
			rc = at_refuse(ctx, req, id, "read the settings first (ubus call medvemodem radio): the cell lock is read over AT", NULL);
			goto out;
		}
		via = "at";
		rs = json_object_object_get(cached, "radio");
	}
	if (strcmp(via, "modemmanager") &&
	    (!cached || now_mono_ms() - json_object_get_int64(json_object_object_get(cached, "at")) > MM_RADIO_MAX_AGE_MS)) {
		rc = at_refuse(ctx, req, id, "read the settings first (ubus call medvemodem radio): a change starts from a recent reading", NULL);
		goto out;
	}
	if (!(plan = mm_radio_plan(via, dialect, rs, want, why, sizeof(why)))) {
		rc = at_refuse(ctx, req, id, why, NULL);
		goto out;
	}
	/* the way back, from what was read: no change without it */
	prev = json_object_new_object();
	json_object_object_add(prev, "modes", json_object_get(json_object_object_get(rs, "modes")));
	if (json_object_get_boolean(json_object_object_get(rs, "bands_any")) && !strcmp(via, "modemmanager"))
		json_object_object_add(prev, "bands", json_object_new_string("any"));
	else
		json_object_object_add(prev, "bands", json_object_get(json_object_object_get(rs, "bands")));
	if (json_object_object_get(rs, "raw"))
		json_object_object_add(prev, "raw", json_object_get(json_object_object_get(rs, "raw")));
	if (json_object_object_get_ex(want, "cell_lock", NULL)) {
		json_object *cl = json_object_object_get(rs, "cell_lock");

		json_object_object_add(prev, "cell_lock", cl ? json_object_get(cl) : json_object_new_string("none"));
	}
	/* a module restart re-enumerates and dials again: give the connection longer to come back */
	if (!tb[RSET_WAIT] && radio_restarts(json_object_object_get(plan, "steps")))
		wait = RADIO_WAIT_RESTART_S;
	if (!(back = mm_radio_plan(via, dialect, rs, prev, why, sizeof(why)))) {
		char msg2[300];

		snprintf(msg2, sizeof(msg2), "the change could not be undone: %s", why);
		json_object_put(plan);
		json_object_put(prev);
		rc = at_refuse(ctx, req, id, msg2, NULL);
		goto out;
	}
	/* an armed lock stays guarded through a change of the modes or bands: its way back then undoes the lock alone */
	{
		json_object *g = radio_guard ? json_object_object_get(radio_guard, id) : NULL;

		/* a band guard: back to the bands before any restriction, the modes as this change leaves them */
		if (g && guard_bands(g) && json_object_object_get(g, "prev_bands") && !json_object_object_get_ex(want, "cell_lock", NULL)) {
			json_object *only = json_object_new_object(), *gb, *wm = json_object_object_get(want, "modes");
			char no[256];

			json_object_object_add(only, "modes", json_object_get(wm ? wm : json_object_object_get(rs, "modes")));
			/* no "raw": the reading's own words would write the restriction back as it is now */
			json_object_object_add(only, "bands", json_object_get(json_object_object_get(g, "prev_bands")));
			gb = mm_radio_plan(via, dialect, rs, only, no, sizeof(no));
			json_object_put(only);
			if (!gb)
				ULOG_WARN("radio %s: the band guard's way back could not be planned again: %s\n", id, no);
			guard_back = gb ? json_object_get(json_object_object_get(gb, "steps")) : NULL;
			json_object_put(gb);
		}
		if (g && !guard_bands(g) && !json_object_object_get_ex(want, "cell_lock", NULL)) {
			json_object *only = json_object_new_object(), *gb;
			char no[256];

			json_object_object_add(only, "cell_lock", json_object_get(json_object_object_get(g, "prev_lock")));
			if (!json_object_object_get(only, "cell_lock"))
				json_object_object_add(only, "cell_lock", json_object_new_string("none"));
			/* the lock's own reading: a T99's lock goes over AT while ModemManager has its modes */
			gb = mm_radio_plan(jstr(g, "via"), jstr(g, "dialect"),
					   strcmp(jstr(g, "via") ? jstr(g, "via") : "", via) ? json_object_object_get(cached, "radio") : rs,
					   only, no, sizeof(no));
			json_object_put(only);
			guard_back = gb ? json_object_get(json_object_object_get(gb, "steps")) : NULL;
			json_object_put(gb);
		}
	}
	/* a modem that had a working connection must get it back */
	u = uplinks ? json_object_object_get(uplinks, id) : NULL;
	r = json_object_object_get(u, "reach");
	watch = r && jstr(r, "level") && (!strcmp(jstr(r, "level"), "ok") || !strcmp(jstr(r, "level"), "limited"));
	if (!radio_txn)
		radio_txn = json_object_new_object();
	t = json_object_new_object();
	json_object_object_add(t, "via", json_object_new_string(via));
	json_object_object_add(t, "dialect", dialect ? json_object_new_string(dialect) : NULL);
	json_object_object_add(t, "want", json_object_get(want));
	json_object_object_add(t, "previous", prev);
	json_object_object_add(t, "back", json_object_get(json_object_object_get(back, "steps")));
	if (guard_back)
		json_object_object_add(t, "guard_back", guard_back);
	json_object_object_add(t, "watch", json_object_new_boolean(watch));
	json_object_object_add(t, "wait_s", json_object_new_int(wait));
	json_object_object_add(t, "started", json_object_new_int64((long long)time(NULL)));
	json_object_object_add(radio_txn, id, t);
	radio_txn_set(t, "applying", NULL);	/* journalled before the first write */
	json_object_put(back);
	rc = radio_run(ctx, req, rec, srv, via, dialect, json_object_object_get(plan, "steps"), "set");
	/* refused before anything was written (a port held, a recovery running): no change to wait for */
	if (!at_running(id, AT_RADIO)) {
		json_object_object_del(radio_txn, id);
		radio_txn_save();
	} else {
		radio_summary(want, why, sizeof(why));
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "radio-change", why);
		ULOG_WARN("radio %s: change via %s (%s), %s\n", id, via, why, watch ? "the connection is watched" : "no connection to watch");
	}
	json_object_put(plan);
out:
	json_object_put(want);
	json_object_put(srv);
	return rc;
}

enum { RCONF_ID, RCONF_ACTION, __RCONF_MAX };
const struct blobmsg_policy radio_confirm_policy[2] = {
	[RCONF_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[RCONF_ACTION] = { .name = "action", .type = BLOBMSG_TYPE_STRING },
};

/* radio_confirm {id, action: keep | rollback}: the operator ends the wait, or takes the change back */
int m_radio_confirm(struct ubus_context *ctx, struct ubus_object *obj,
			   struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__RCONF_MAX];
	json_object *rec, *t, *o;
	const char *id, *action, *state;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(radio_confirm_policy, __RCONF_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[RCONF_ID] || !tb[RCONF_ACTION])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[RCONF_ID])))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	action = blobmsg_get_string(tb[RCONF_ACTION]);
	t = radio_txn_of(id);
	state = t ? jstr(t, "state") : NULL;
	if (!state || (strcmp(state, "waiting") && strcmp(state, "applied") && strcmp(state, "kept")))
		return at_refuse(ctx, req, id, "no change to keep or take back", NULL);
	if (!strcmp(action, "keep")) {
		if (strcmp(state, "kept")) {
			radio_txn_set(t, "kept", "kept by the operator");
			radio_settled(id, t);
			mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "radio-kept", "by the operator");
			broadcast("medvemodem.radio", t);
		}
	} else if (!strcmp(action, "rollback")) {
		radio_rollback(id, "taken back by the operator");
	} else {
		return UBUS_STATUS_INVALID_ARGUMENT;
	}
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "change", radio_change_view(id));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

void radio_mgr_stop(void)
{
	json_object_put(radio_txn);
	radio_txn = NULL;
	json_object_put(radio_guard);
	radio_guard = NULL;
	json_object_put(guard_clock);
	guard_clock = NULL;
}
