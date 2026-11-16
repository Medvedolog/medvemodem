// SPDX-License-Identifier: GPL-3.0-only
#include <stdbool.h>
#include <string.h>

#include "downtime.h"

static const char *const kinds[] = { "ok", "limited", "none", "off" };

static const char *str(json_object *o, const char *k)
{
	json_object *v = o ? json_object_object_get(o, k) : NULL;

	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static long long num(json_object *o, const char *k)
{
	return o ? json_object_get_int64(json_object_object_get(o, k)) : 0;
}

static void add(json_object *o, const char *k, long long v)
{
	json_object_object_add(o, k, json_object_new_int64(num(o, k) + v));
}

static bool bad(const char *s)
{
	return s && (!strcmp(s, "none") || !strcmp(s, "off"));
}

static void new_day(json_object *book, const char *day, long long now_ms)
{
	json_object *y = json_object_new_object();
	size_t i;

	if (str(book, "day")) {
		json_object_object_add(y, "day", json_object_new_string(str(book, "day")));
		for (i = 0; i < sizeof(kinds) / sizeof(*kinds); i++)
			json_object_object_add(y, kinds[i], json_object_new_int64(num(book, kinds[i])));
		json_object_object_add(y, "outages", json_object_new_int64(num(book, "outages")));
		json_object_object_add(y, "longest", json_object_new_int64(num(book, "longest")));
		json_object_object_add(book, "yesterday", y);
	} else {
		json_object_put(y);
	}
	json_object_object_add(book, "day", json_object_new_string(day));
	for (i = 0; i < sizeof(kinds) / sizeof(*kinds); i++)
		json_object_object_add(book, kinds[i], json_object_new_int64(0));
	json_object_object_add(book, "outages", json_object_new_int64(0));
	json_object_object_add(book, "longest", json_object_new_int64(0));
	/*
	 * An outage going on at midnight goes on: today it counts from this pass, as the time up to it went to yesterday
	 * (review 2026-11-15: from the last pass it counted that time twice, and after a gap the whole gap as an outage).
	 */
	if (bad(str(book, "state")) && json_object_object_get(book, "since_ms"))
		json_object_object_add(book, "since_ms", json_object_new_int64(now_ms));
	json_object_object_del(book, "counted");	/* and it is today's outage too once long enough */
}

void mm_downtime_add(json_object *book, const char *state, const char *day, long long now_ms)
{
	const char *was = str(book, "state");
	long long dt = json_object_object_get(book, "last_ms") ? now_ms - num(book, "last_ms") : 0;

	if (!day)
		return;
	if (dt > 0 && dt <= MM_DOWNTIME_GAP_MS && was) {
		add(book, was, dt);
		/* the stretch so far, also when it ends now */
		if (bad(was) && json_object_object_get(book, "since_ms") && now_ms - num(book, "since_ms") > num(book, "longest"))
			json_object_object_add(book, "longest", json_object_new_int64(now_ms - num(book, "since_ms")));
	} else if (dt > MM_DOWNTIME_GAP_MS && bad(was)) {
		/* a gap: its time is not known; an outage still going on after it is the same one, measured afresh */
		json_object_object_add(book, "since_ms", json_object_new_int64(now_ms));
	}
	if (!str(book, "day") || strcmp(str(book, "day"), day))
		new_day(book, day, now_ms);
	/* a stretch counts as an outage once it lasted MM_DOWNTIME_OUTAGE_MS: a session coming up is not one */
	if (bad(was) && json_object_object_get(book, "since_ms") && !json_object_object_get(book, "counted") &&
	    now_ms - num(book, "since_ms") >= MM_DOWNTIME_OUTAGE_MS) {
		add(book, "outages", 1);
		json_object_object_add(book, "counted", json_object_new_boolean(1));
	}
	if (bad(state) && !bad(was)) {
		json_object_object_add(book, "since_ms", json_object_new_int64(now_ms));
		json_object_object_del(book, "counted");
	}
	if (!bad(state)) {
		json_object_object_del(book, "since_ms");
		json_object_object_del(book, "counted");
	}
	if (state)
		json_object_object_add(book, "state", json_object_new_string(state));
	else
		json_object_object_del(book, "state");
	json_object_object_add(book, "last_ms", json_object_new_int64(now_ms));
}

json_object *mm_downtime_view(json_object *book, long long now_ms)
{
	json_object *v, *y;
	size_t i;

	if (!str(book, "day"))
		return NULL;
	v = json_object_new_object();
	json_object_object_add(v, "day", json_object_new_string(str(book, "day")));
	for (i = 0; i < sizeof(kinds) / sizeof(*kinds); i++)
		json_object_object_add(v, kinds[i], json_object_new_int64(num(book, kinds[i]) / 1000));
	json_object_object_add(v, "outages", json_object_new_int64(num(book, "outages")));
	json_object_object_add(v, "longest", json_object_new_int64(num(book, "longest") / 1000));
	if (bad(str(book, "state")) && json_object_object_get(book, "since_ms"))
		json_object_object_add(v, "current", json_object_new_int64((now_ms - num(book, "since_ms")) / 1000));
	if ((y = json_object_object_get(book, "yesterday"))) {
		json_object *yv = json_object_new_object();

		json_object_object_add(yv, "day", json_object_new_string(str(y, "day") ? str(y, "day") : ""));
		for (i = 0; i < sizeof(kinds) / sizeof(*kinds); i++)
			json_object_object_add(yv, kinds[i], json_object_new_int64(num(y, kinds[i]) / 1000));
		json_object_object_add(yv, "outages", json_object_new_int64(num(y, "outages")));
		json_object_object_add(yv, "longest", json_object_new_int64(num(y, "longest") / 1000));
		json_object_object_add(v, "yesterday", yv);
	}
	return v;
}
