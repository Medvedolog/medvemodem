// SPDX-License-Identifier: GPL-3.0-only
/*
 * A modem's traffic and peak speed history (spec §42, the owner 2026-11-02): the 5 s rates the uplink pass already
 * computes from the network device's counters go into trafhist.c's history (RAM, like the signal history) and into
 * the highest rates since the router booted. The peaks are mirrored in <run>/traffic-peaks.json: an engine restart
 * keeps them, a reboot (tmpfs) clears them, which is what «since the router's boot» means. Never a modem poll.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "daemon.h"
#include "trafhist.h"

#define PEAKS_SAVE_MS 60000	/* a grown peak is written at most once a minute */

static json_object *hists;	/* device_id -> trafhist.c's history */
static json_object *peaks;	/* device_id -> {rx_bps, tx_bps, rx_at, tx_at} */
static bool peaks_dirty;
static long long peaks_saved_ms = -1;

static void peaks_path(char *path, size_t size)
{
	snprintf(path, size, "%s/traffic-peaks.json", run_dir);
}

static void peaks_save(void)
{
	char path[512], tmp[520];

	if (!peaks || !peaks_dirty)
		return;
	peaks_path(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(run_dir, 0755);
	if (!json_object_to_file_ext(tmp, peaks, JSON_C_TO_STRING_PLAIN) && !rename(tmp, path))
		peaks_dirty = false;
	peaks_saved_ms = now_mono_ms();
}

void traffic_mgr_start(void)
{
	char path[512];

	hists = json_object_new_object();
	peaks_path(path, sizeof(path));
	peaks = json_object_from_file(path);
	if (!json_object_is_type(peaks, json_type_object)) {
		json_object_put(peaks);
		peaks = json_object_new_object();
	}
}

void traffic_mgr_stop(void)
{
	peaks_save();
	json_object_put(hists);
	json_object_put(peaks);
	hists = peaks = NULL;
}

/* audit M06's rule: a device the registry forgot loses its history and peaks */
void traffic_retain(json_object *devices)
{
	json_object *drop = json_object_new_array();
	size_t i;

	if (hists) {
		json_object_object_foreach(hists, id, h) {
			(void)h;
			if (!devices || !json_object_object_get(devices, id))
				json_object_array_add(drop, json_object_new_string(id));
		}
	}
	if (peaks) {
		json_object_object_foreach(peaks, pid, pv) {
			(void)pv;
			if ((!devices || !json_object_object_get(devices, pid)) && !json_object_object_get(hists, pid))
				json_object_array_add(drop, json_object_new_string(pid));
		}
	}
	for (i = 0; i < json_object_array_length(drop); i++) {
		const char *id = json_object_get_string(json_object_array_get_idx(drop, i));

		if (hists)
			json_object_object_del(hists, id);
		if (peaks && json_object_object_get(peaks, id)) {
			json_object_object_del(peaks, id);
			peaks_dirty = true;
		}
	}
	json_object_put(drop);
}

void traffic_sample(const char *id, json_object *traffic, long long wall)
{
	json_object *rxr = json_object_object_get(traffic, "rx_rate"), *txr = json_object_object_get(traffic, "tx_rate"), *h, *p;
	long long rx, tx;

	/* no rate yet (the first sample, a restart of the counters): nothing to keep, a gap */
	if (!hists || !peaks || !id || !rxr || !txr)
		return;
	rx = json_object_get_int64(rxr);
	tx = json_object_get_int64(txr);
	if (signal_history_on()) {
		if (!(h = json_object_object_get(hists, id))) {
			h = json_object_new_object();
			json_object_object_add(hists, id, h);
		}
		mm_trafhist_add(h, rx, tx, wall);
	}
	if (!(p = json_object_object_get(peaks, id))) {
		p = json_object_new_object();
		json_object_object_add(peaks, id, p);
	}
	if (mm_trafpeak_add(p, rx, tx, wall))
		peaks_dirty = true;
	if (peaks_dirty && (peaks_saved_ms < 0 || now_mono_ms() - peaks_saved_ms >= PEAKS_SAVE_MS))
		peaks_save();
}

json_object *traffic_peak_of(const char *id)
{
	json_object *p = peaks && id ? json_object_object_get(peaks, id) : NULL;

	return p && json_object_object_length(p) ? p : NULL;
}

enum { TH_ID, TH_RANGE, __TH_MAX };
const struct blobmsg_policy trafhist_policy[2] = {
	[TH_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[TH_RANGE] = { .name = "range", .type = BLOBMSG_TYPE_STRING },
};

int m_traffic_history(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct blob_attr *tb[__TH_MAX];
	json_object *rec, *idv = NULL, *o, *p;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(trafhist_policy, __TH_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[TH_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = mm_registry_find(&registry, blobmsg_get_string(tb[TH_ID]))) || !json_object_object_get_ex(rec, "device_id", &idv))
		return UBUS_STATUS_NOT_FOUND;
	o = mm_trafhist_view(hists ? json_object_object_get(hists, json_object_get_string(idv)) : NULL,
			     tb[TH_RANGE] ? blobmsg_get_string(tb[TH_RANGE]) : "1h", (long long)time(NULL));
	json_object_object_add(o, "device_id", json_object_get(idv));
	if ((p = traffic_peak_of(json_object_get_string(idv))))
		json_object_object_add(o, "since_boot", json_object_get(p));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}
