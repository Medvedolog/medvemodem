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

#include "daemon.h"
#include "quota.h"

/*
 * The SIM quota manager (spec 20.9, daemon.h).
 *
 * The bytes of a connected modem's network device count against the SIM in
 * it (ModemManager's ICCID), in UCI config sim / option iccid: quota_gb (decimal
 * GB per period), quota_warn (percent, default 80), quota_day (the period's
 * first day, 1..28, default 1), quota_action (warn | backup: a link over its
 * quota is held behind the others by failover). The books are on flash
 * (state directory) so a reboot does not forgive a month; written at most every
 * 30 minutes and at exit, keyed by a hash of the ICCID (never the identifier
 * itself). A modem without an ICCID (no ModemManager) has none.
 */
#define QUOTA_SAVE_MS 1800000	/* the counters reach the flash every 30 min and at stop (the owner, 2026-10-19: «и флеш тоже») */
static json_object *sim_usage;		/* mm_quota_key(iccid) -> {period, bytes, warned?, exceeded?} */
static json_object *quota_views;	/* device_id -> served quota + {dev, pass}: links counted in the last pass only */
static long long quota_pass;		/* uplink_tick's pass number */
static bool sim_usage_dirty;
static long long sim_usage_saved_ms;

static json_object *sim_section(const char *iccid)
{
	json_object *img = medvemodem_uci();

	if (!img || !iccid || !*iccid)
		return NULL;
	json_object_object_foreach(img, sec, s) {
		(void)sec;
		if (!strcmp(jstr(s, ".type") ? jstr(s, ".type") : "", "sim") && jstr(s, "iccid") && !strcmp(jstr(s, "iccid"), iccid))
			return s;
	}
	return NULL;
}

static void sim_usage_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	if (!sim_usage || !sim_usage_dirty)
		return;
	snprintf(path, sizeof(path), "%s/sim-usage.json", state_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(state_dir, 0755);
	if (!json_object_to_file_ext(tmp, sim_usage, JSON_C_TO_STRING_PLAIN) && !rename(tmp, path))
		sim_usage_dirty = false;
}

void quota_mgr_start(void)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/sim-usage.json", state_dir);
	json_object_put(sim_usage);
	sim_usage = json_object_from_file(path);
	if (!sim_usage || !json_object_is_type(sim_usage, json_type_object)) {
		json_object_put(sim_usage);
		sim_usage = json_object_new_object();
	}
}

/* one sample's bytes of a device: counted against its SIM, judged, said once */
void quota_count(const char *id, json_object *srv, const char *dev, long long bytes, const char *today)
{
	/* ModemManager's card, else the SIM the engine read for this modem (QModem's or the AT port's, r64) */
	const char *iccid = jstr(json_object_object_get(json_object_object_get(srv, "modemmanager"), "sim_card"), "iccid");
	json_object *sec, *entry, *view;
	char start[11], next[11], key[24];
	const char *news;

	if (!quota_views)
		quota_views = json_object_new_object();
	if (!iccid || !*iccid || !strcmp(iccid, "--"))
		iccid = jstr(json_object_object_get(srv, "sim"), "iccid");
	if (!iccid || !*iccid || !strcmp(iccid, "--") || !sim_usage) {
		json_object_object_del(quota_views, id);
		return;
	}
	sec = sim_section(iccid);
	if (!mm_quota_period(today, jstr(sec, "quota_day") ? atoi(jstr(sec, "quota_day")) : 1, start, next))
		return;
	mm_quota_key(iccid, key);
	entry = mm_quota_add(sim_usage, key, start, bytes);
	if (jstr(entry, "period") && strcmp(jstr(entry, "period"), start)) {
		/* the clock went back: judged in the period the books are in */
		char was[11];

		snprintf(was, sizeof(was), "%s", jstr(entry, "period"));
		mm_quota_period(was, jstr(sec, "quota_day") ? atoi(jstr(sec, "quota_day")) : 1, start, next);
		snprintf(start, sizeof(start), "%s", was);
	}
	if (bytes > 0)
		sim_usage_dirty = true;
	view = mm_quota_view(entry, iccid, mm_quota_bytes(jstr(sec, "quota_gb")),
			     jstr(sec, "quota_warn") ? atoi(jstr(sec, "quota_warn")) : 80, start, next, jstr(sec, "quota_action"));
	if ((news = mm_quota_news(entry, view))) {
		char detail[96];

		snprintf(detail, sizeof(detail), "%.1f of %.1f GB", json_object_get_int64(json_object_object_get(view, "used_bytes")) / 1e9,
			 json_object_get_int64(json_object_object_get(view, "limit_bytes")) / 1e9);
		ULOG_WARN("SIM quota of %s: %s (%s)\n", id, news + 6, detail);
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), news, detail);
		history_dirty = true;
		sim_usage_dirty = true;
	}
	json_object_object_add(view, "dev", json_object_new_string(dev));
	json_object_object_add(view, "pass", json_object_new_int64(quota_pass));
	json_object_object_add(quota_views, id, view);
}

/*
 * after a pass of uplink_tick: a device that was not counted in it (unplugged,
 * disconnected, no ICCID, another SIM, no counters) has no quota any more, so a
 * link that now uses its network device is not held for it (audit F02)
 */
static void quota_sweep(void)
{
	json_object *gone = json_object_new_array();
	size_t i;

	if (quota_views) {
		json_object_object_foreach(quota_views, id, v)
			if (json_object_get_int64(json_object_object_get(v, "pass")) != quota_pass)
				json_object_array_add(gone, json_object_new_string(id));
	}
	for (i = 0; i < json_object_array_length(gone); i++)
		json_object_object_del(quota_views, json_object_get_string(json_object_array_get_idx(gone, i)));
	json_object_put(gone);
}

/* the served quota of a device (without the bookkeeping), or NULL */
json_object *quota_of(const char *id)
{
	json_object *v = quota_views ? json_object_object_get(quota_views, id) : NULL, *o;

	if (!v)
		return NULL;
	o = json_object_new_object();
	json_object_object_foreach(v, k, x)
		if (strcmp(k, "dev") && strcmp(k, "pass"))
			json_object_object_add(o, k, json_object_get(x));
	return o;
}

/* failover's opts.quota: the network devices of links over their quota with action backup */
json_object *quota_held_devs(void)
{
	json_object *out = json_object_new_array();

	if (quota_views) {
		json_object_object_foreach(quota_views, id, v) {
			(void)id;
			if (jstr(v, "state") && !strcmp(jstr(v, "state"), "exceeded") && jstr(v, "action") &&
			    !strcmp(jstr(v, "action"), "backup") && jstr(v, "dev"))
				json_object_array_add(out, json_object_new_string(jstr(v, "dev")));
		}
	}
	return out;
}

void quota_pass_begin(void)
{
	quota_pass++;
}

void quota_pass_end(void)
{
	long long now = now_mono_ms();

	quota_sweep();
	if (now - sim_usage_saved_ms >= (uplink_override_ms ? uplink_override_ms : QUOTA_SAVE_MS)) {
		sim_usage_save();
		sim_usage_saved_ms = now;
	}
}

void quota_mgr_stop(void)
{
	sim_usage_save();
	json_object_put(sim_usage);
	json_object_put(quota_views);
	sim_usage = quota_views = NULL;
}
