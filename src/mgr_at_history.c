/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "daemon.h"

/*
 * The AT console's history (field 2026-10-13): per device the commands of the
 * last day, newest last, at most AT_HIST_MAX, with the modem's answer, the port
 * and the time. RAM only: answers may hold the IMEI, IMSI or ICCID, which are
 * served to the operator but never kept on flash or logged (decision
 * 2026-10-09). A reboot or an engine restart clears it.
 */
#define AT_HIST_MAX 100
#define AT_HIST_S 86400
#define AT_HIST_LINES 40

static json_object *at_hist;		/* device_id -> [{t, cmd, ok?, final?, lines?, error?, refused?, port?, elapsed_ms?}] */

static void trim(json_object *a, long long now)
{
	while (json_object_array_length(a) &&
	       (json_object_array_length(a) > AT_HIST_MAX ||
		json_object_get_int64(json_object_object_get(json_object_array_get_idx(a, 0), "t")) < now - AT_HIST_S))
		json_object_array_del_idx(a, 0, 1);
}

/* audit M06: the AT console's record of ids the registry no longer has goes with them */
void at_hist_retain(json_object *devices)
{
	json_object *gone = json_object_new_array();
	size_t i;

	if (at_hist) {
		json_object_object_foreach(at_hist, id, a) {
			(void)a;
			if (!json_object_object_get(devices, id))
				json_object_array_add(gone, json_object_new_string(id));
		}
		for (i = 0; i < json_object_array_length(gone); i++)
			json_object_object_del(at_hist, json_object_get_string(json_object_array_get_idx(gone, i)));
	}
	json_object_put(gone);
}

void at_hist_add(const char *id, const char *cmd, json_object *reply)
{
	static const char *const keep[] = { "ok", "final", "error", "refused", "port", "elapsed_ms", NULL };
	json_object *a, *e, *lines;
	long long now = (long long)time(NULL);
	int k;

	if (!id || !cmd)
		return;
	if (!at_hist)
		at_hist = json_object_new_object();
	if (!(a = json_object_object_get(at_hist, id))) {
		a = json_object_new_array();
		json_object_object_add(at_hist, id, a);
	}
	e = json_object_new_object();
	json_object_object_add(e, "t", json_object_new_int64(now));
	json_object_object_add(e, "cmd", json_object_new_string(cmd));
	for (k = 0; keep[k]; k++)
		if (json_object_object_get(reply, keep[k]))
			json_object_object_add(e, keep[k], json_object_get(json_object_object_get(reply, keep[k])));
	if ((lines = json_object_object_get(reply, "lines")) && json_object_is_type(lines, json_type_array)) {
		json_object *l = json_object_new_array();
		size_t i;

		for (i = 0; i < json_object_array_length(lines) && i < AT_HIST_LINES; i++)
			json_object_array_add(l, json_object_get(json_object_array_get_idx(lines, i)));
		json_object_object_add(e, "lines", l);
	}
	json_object_array_add(a, e);
	trim(a, now);
}

enum { AH_ID, AH_CLEAR, __AH_MAX };
const struct blobmsg_policy at_history_policy[2] = {
	[AH_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[AH_CLEAR] = { .name = "clear", .type = BLOBMSG_TYPE_BOOL },
};

/* at_history {id, clear?}: the day's commands of the modem, newest last; clear empties it first */
int m_at_history(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__AH_MAX];
	json_object *rec, *idv = NULL, *o, *a;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(at_history_policy, __AH_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[AH_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = mm_registry_find(&registry, blobmsg_get_string(tb[AH_ID]))) || !json_object_object_get_ex(rec, "device_id", &idv))
		return UBUS_STATUS_NOT_FOUND;
	if (tb[AH_CLEAR] && blobmsg_get_bool(tb[AH_CLEAR]) && at_hist)
		json_object_object_del(at_hist, json_object_get_string(idv));
	a = at_hist ? json_object_object_get(at_hist, json_object_get_string(idv)) : NULL;
	if (a)
		trim(a, (long long)time(NULL));
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_get(idv));
	json_object_object_add(o, "commands", a ? json_object_get(a) : json_object_new_array());
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

void at_hist_stop(void)
{
	json_object_put(at_hist);
	at_hist = NULL;
}
