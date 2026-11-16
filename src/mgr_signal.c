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

#include "cellev.h"
#include "daemon.h"
#include "sighist.h"

/* The signal history manager (spec 25.9.13, daemon.h). */

#define SIGHIST_EVERY_MS 60000
#define WEBHIST_EVERY_MS 30000		/* a router-mode modem's web, on its own timer (the owner, 2026-10-22: «раз в 30 сек») */
static json_object *sig_hist;		/* device_id -> mm_sighist state, RAM only */
static struct uloop_timeout sighist_timer, webhist_timer;

/* a background read for the history once a minute: ModemManager's properties (no cells, mm_client_at), and a modem QModem
 * or the engine's AT reads by one levels reading (signal_history_read);
 * UCI core.signal_history '0' turns it off (the history then fills only while a page looks) */
static bool sighist_enabled(void)
{
	json_object *c = core_uci();

	return !(c && jstr(c, "signal_history") && !strcmp(jstr(c, "signal_history"), "0"));
}

bool signal_history_on(void)
{
	return sighist_enabled();
}

/* switched off (core.signal_history '0'), the timer stops; uplink_tick starts it again when switched on */
static void sighist_tick(struct uloop_timeout *t)
{
	(void)t;
	if (!sighist_enabled())
		return;
	if (!strcmp(mm_backend, "available") && mm_devices && json_object_object_length(mm_devices) && !mm_running)
		mm_schedule_look();
	signal_history_read();	/* the modems no ModemManager reads: QModem's or the engine's AT (the FM350) */
	sighist_timer.cb = sighist_tick;
	uloop_timeout_set(&sighist_timer, uplink_override_ms ? uplink_override_ms : SIGHIST_EVERY_MS);
}

/*
 * a router-mode modem (HiLink, ZTE web) has no ModemManager to read: its own web every WEBHIST_EVERY_MS
 * (web_background). Its own timer: on the minute's tick it was read once a minute, not every 30 s as promised
 * (audit 2026-10-23, R86-02)
 */
static void webhist_tick(struct uloop_timeout *t)
{
	(void)t;
	if (!sighist_enabled())
		return;
	if (registry.devices) {
		json_object_object_foreach(registry.devices, id, rec) {
			json_object *srv;
			const char *ctl;

			if (!json_object_get_boolean(json_object_object_get(rec, "present")) || frozen(id))
				continue;
			srv = served(rec);
			ctl = jstr(json_object_object_get(srv, "owner"), "control");
			if (ctl && !strcmp(ctl, "hilink"))
				web_background(rec, srv);
			json_object_put(srv);
		}
	}
	webhist_timer.cb = webhist_tick;
	uloop_timeout_set(&webhist_timer, uplink_override_ms ? uplink_override_ms : WEBHIST_EVERY_MS);
}

enum { SH_ID, SH_RANGE, __SH_MAX };
const struct blobmsg_policy sighist_policy[2] = {
	[SH_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[SH_RANGE] = { .name = "range", .type = BLOBMSG_TYPE_STRING },
};

int m_signal_history(struct ubus_context *ctx, struct ubus_object *obj,
			    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__SH_MAX];
	json_object *rec, *idv = NULL, *o, *h;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(sighist_policy, __SH_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[SH_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = mm_registry_find(&registry, blobmsg_get_string(tb[SH_ID]))) || !json_object_object_get_ex(rec, "device_id", &idv))
		return UBUS_STATUS_NOT_FOUND;
	h = sig_hist ? json_object_object_get(sig_hist, json_object_get_string(idv)) : NULL;
	o = mm_sighist_view(h, tb[SH_RANGE] ? blobmsg_get_string(tb[SH_RANGE]) : "1h", (long long)time(NULL));
	json_object_object_add(o, "device_id", json_object_get(idv));
	mm_maybe_refresh();	/* a page looks: the next read may bring the cells too */
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/*
 * Band, cell and carrier-aggregation changes into the modem's history (the owner, 2026-11-06, as network
 * events): a value once two readings agree, at most one event of a kind in CELLEV_HOLD_MS. A modem ModemManager holds
 * is followed by ModemManager's reads alone (its cell id and the engine's AT reading spell the same cell differently)
 */
#define CELLEV_HOLD_MS 120000
static json_object *cell_state;	/* device_id -> mm_cellev state, RAM only */

static void cellev_feed(const char *id, json_object *digest)
{
	json_object *st, *ev = json_object_new_array();
	long long now = now_mono_ms();
	size_t i;

	if (!cell_state)
		cell_state = json_object_new_object();
	if (!(st = json_object_object_get(cell_state, id))) {
		st = json_object_new_object();
		json_object_object_add(cell_state, id, st);
	}
	if (mm_cellev_observe(st, digest, now, uplink_override_ms ? 2 * uplink_override_ms : CELLEV_HOLD_MS, ev)) {
		for (i = 0; i < json_object_array_length(ev); i++) {
			json_object *e = json_object_array_get_idx(ev, i);

			mm_history_add(&history, id, (long long)time(NULL), now, json_object_get_string(json_object_array_get_idx(e, 0)),
				       json_object_get_string(json_object_array_get_idx(e, 1)));
		}
		history_dirty = true;
	}
	json_object_put(ev);
	json_object_put(digest);
}

void signal_sample(const char *id, json_object *mm, long long wall)
{
	json_object *h;

	if (!sig_hist)
		sig_hist = json_object_new_object();
	if (!(h = json_object_object_get(sig_hist, id))) {
		h = json_object_new_object();
		json_object_object_add(sig_hist, id, h);
	}
	mm_sighist_add(h, mm, wall);
	cellev_feed(id, mm_cellev_digest_mm(mm));
}

/*
 * A reading the engine took itself (a router-mode modem's web page, AT, QModem's cell_info) in ModemManager's
 * shape for the history: rsrp_dbm, rsrq_db, sinr_db, access_technology, and "nr" with the same for the 5G cell
 * (the owner, 2026-10-20: the FM350 under QModem had no «История сигнала»).
 */
void signal_sample_reading(const char *id, json_object *m, long long wall)
{
	static const char *const keys[][2] = { { "rsrp_dbm", "rsrp" }, { "rsrq_db", "rsrq" }, { "sinr_db", "snr" } };
	json_object *mm, *sig, *lte, *nr, *src = json_object_object_get(m, "nr"), *v;
	const char *tech = json_object_get_string(json_object_object_get(m, "access_technology"));
	size_t i;

	if (!id || !m)
		return;
	if (!(mm_devices && json_object_object_get(mm_devices, id)))
		cellev_feed(id, mm_cellev_digest_reading(m));
	mm = json_object_new_object();
	sig = json_object_new_object();
	lte = json_object_new_object();
	nr = json_object_new_object();
	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if ((v = json_object_object_get(m, keys[i][0])))
			json_object_object_add(lte, keys[i][1], json_object_get(v));
		if (src && (v = json_object_object_get(src, keys[i][0])))
			json_object_object_add(nr, keys[i][1], json_object_get(v));
	}
	if (!json_object_object_length(lte) && !json_object_object_length(nr)) {
		json_object_put(lte);
		json_object_put(nr);
		json_object_put(sig);
		json_object_put(mm);
		return;
	}
	json_object_object_add(sig, "lte", lte);
	json_object_object_add(sig, "5g", nr);
	json_object_object_add(mm, "signal", sig);
	if (tech && *tech && strcmp(tech, "unknown")) {
		json_object *at = json_object_new_array();

		json_object_array_add(at, json_object_new_string(tech));
		json_object_object_add(mm, "access_tech", at);
	}
	signal_sample(id, mm, wall);
	json_object_put(mm);
}

void signal_mgr_start(void)
{
	sighist_timer.cb = sighist_tick;
	uloop_timeout_set(&sighist_timer, uplink_override_ms ? uplink_override_ms : SIGHIST_EVERY_MS);
	webhist_timer.cb = webhist_tick;
	uloop_timeout_set(&webhist_timer, uplink_override_ms ? uplink_override_ms : WEBHIST_EVERY_MS);
}

void signal_mgr_tick(void)
{
	if (!sighist_timer.pending && sighist_enabled()) {
		sighist_timer.cb = sighist_tick;
		uloop_timeout_set(&sighist_timer, uplink_override_ms ? uplink_override_ms : SIGHIST_EVERY_MS);
	}
	if (!webhist_timer.pending && sighist_enabled()) {
		webhist_timer.cb = webhist_tick;
		uloop_timeout_set(&webhist_timer, uplink_override_ms ? uplink_override_ms : WEBHIST_EVERY_MS);
	}
}

/* audit M06: the histories of ids the registry no longer has go with them (devices: the registry's id -> record) */
void signal_retain(json_object *devices)
{
	json_object *gone = json_object_new_array();
	size_t i;

	if (sig_hist) {
		json_object_object_foreach(sig_hist, id, h) {
			(void)h;
			if (!json_object_object_get(devices, id))
				json_object_array_add(gone, json_object_new_string(id));
		}
		for (i = 0; i < json_object_array_length(gone); i++) {
			json_object_object_del(sig_hist, json_object_get_string(json_object_array_get_idx(gone, i)));
			if (cell_state)
				json_object_object_del(cell_state, json_object_get_string(json_object_array_get_idx(gone, i)));
		}
	}
	json_object_put(gone);
}

void signal_mgr_stop(void)
{
	uloop_timeout_cancel(&sighist_timer);
	uloop_timeout_cancel(&webhist_timer);
	json_object_put(sig_hist);
	sig_hist = NULL;
	json_object_put(cell_state);
	cell_state = NULL;
}
