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
#include "sms.h"

/*
 * Which received SMS the operator has seen (spec 38.2.2, daemon.h): inbox keys
 * per device, hashes only (mm_sms_inbox), on flash.
 */
static json_object *sms_seen;		/* device_id -> [inbox keys seen]; "@at" -> {device_id: when last marked} */

/* a modem gone for good keeps no list on flash (audit 2026-10-21: the file only grew with every modem ever seen): one
 * not looked at for 90 days goes, and no more than 32 are kept, the oldest look first */
#define SMS_SEEN_KEEP_S (90LL * 86400)
#define SMS_SEEN_MAX 32

static void sms_seen_prune(long long now)
{
	json_object *at = json_object_object_get(sms_seen, "@at");

	for (;;) {
		const char *oldest = NULL;
		long long oldest_t = 0;
		int n = 0;

		json_object_object_foreach(sms_seen, id, v) {
			long long t;

			(void)v;
			if (!strcmp(id, "@at"))
				continue;
			n++;
			t = json_object_get_int64(json_object_object_get(at, id));	/* 0: from before the dates */
			if (!oldest || t < oldest_t) {
				oldest = id;
				oldest_t = t;
			}
		}
		if (!oldest || (n <= SMS_SEEN_MAX && (!oldest_t || now - oldest_t < SMS_SEEN_KEEP_S)))
			return;
		json_object_object_del(at, oldest);
		json_object_object_del(sms_seen, oldest);
	}
}

static void sms_seen_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	if (!sms_seen)
		return;
	snprintf(path, sizeof(path), "%s/sms-seen.json", state_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(state_dir, 0755);
	if (!json_object_to_file_ext(tmp, sms_seen, JSON_C_TO_STRING_PLAIN))
		rename(tmp, path);
}

void sms_seen_start(void)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/sms-seen.json", state_dir);
	sms_seen = json_object_from_file(path);
	if (!sms_seen || !json_object_is_type(sms_seen, json_type_object)) {
		json_object_put(sms_seen);
		sms_seen = json_object_new_object();
	}
}

/* the operator saw the list: every message in it is read (and the keys of deleted ones go) */
void sms_seen_mark(const char *id, json_object *msgs)
{
	json_object *keys = mm_sms_inbox(msgs), *was = json_object_object_get(sms_seen, id);

	if (was && !strcmp(json_object_to_json_string(was), json_object_to_json_string(keys))) {
		json_object *at = json_object_object_get(sms_seen, "@at");

		json_object_put(keys);
		/* still looked at: its date moves on, once a day at most on flash */
		if (at && (long long)time(NULL) - json_object_get_int64(json_object_object_get(at, id)) > 86400) {
			json_object_object_add(at, id, json_object_new_int64((long long)time(NULL)));
			sms_seen_save();
		}
		return;
	}
	json_object *at = json_object_object_get(sms_seen, "@at");
	long long now = (long long)time(NULL);

	if (!at) {
		at = json_object_new_object();
		json_object_object_add(sms_seen, "@at", at);
	}
	json_object_object_add(at, id, json_object_new_int64(now));
	json_object_object_add(sms_seen, id, keys);
	sms_seen_prune(now);
	sms_seen_save();
}

bool sms_seen_known(const char *id)
{
	return sms_seen && json_object_object_get(sms_seen, id);
}

/* the ids of the received messages not seen yet (the owner, 2026-11-09: unread ones marked in the list, their count per
 * correspondent); asked before a read marks them seen. NULL until the device's inbox was seen once (everything there was
 * no news then) */
json_object *sms_seen_unread_ids(const char *id, json_object *msgs)
{
	json_object *out, *seen = sms_seen ? json_object_object_get(sms_seen, id) : NULL;
	size_t i, k;

	if (!msgs || !seen || !json_object_is_type(seen, json_type_array))
		return NULL;
	out = json_object_new_array();
	for (i = 0; i < json_object_array_length(msgs); i++) {
		json_object *m = json_object_array_get_idx(msgs, i), *one = json_object_new_array(), *keys;
		bool known = false;

		json_object_array_add(one, json_object_get(m));
		keys = mm_sms_inbox(one);	/* empty for a sent message or a draft */
		for (k = 0; json_object_array_length(keys) && k < json_object_array_length(seen) && !known; k++)
			known = !strcmp(json_object_get_string(json_object_array_get_idx(seen, k)),
					json_object_get_string(json_object_array_get_idx(keys, 0)));
		if (json_object_array_length(keys) && !known && json_object_object_get(m, "id"))
			json_object_array_add(out, json_object_get(json_object_object_get(m, "id")));
		json_object_put(keys);
		json_object_put(one);
	}
	return out;
}

/* the dashboard's envelope for the messages of a device once read: {"unread", "inbox"} */
json_object *sms_seen_badge(const char *id, json_object *msgs)
{
	json_object *o, *keys;

	if (!msgs || !sms_seen)
		return NULL;
	o = json_object_new_object();
	keys = mm_sms_inbox(msgs);
	json_object_object_add(o, "unread", json_object_new_int(mm_sms_unread(msgs, json_object_object_get(sms_seen, id))));
	json_object_object_add(o, "inbox", json_object_new_int((int)json_object_array_length(keys)));
	json_object_put(keys);
	return o;
}

void sms_seen_stop(void)
{
	json_object_put(sms_seen);
	sms_seen = NULL;
}
