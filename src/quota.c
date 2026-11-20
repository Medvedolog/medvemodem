/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quota.h"

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

bool mm_quota_period(const char *today, int reset_day, char *start, char *next)
{
	int y, m, d, sy, sm, ny, nm;

	if (!today || sscanf(today, "%4d-%2d-%2d", &y, &m, &d) != 3 || y < 1970 || y > 9998 || m < 1 || m > 12 || d < 1 || d > 31)
		return false;
	if (reset_day < 1 || reset_day > 28)
		reset_day = 1;
	sy = y;
	sm = m;
	if (d < reset_day && --sm < 1) {
		sm = 12;
		sy--;
	}
	ny = sy;
	nm = sm + 1;
	if (nm > 12) {
		nm = 1;
		ny++;
	}
	{
		char a[40], b[40];

		snprintf(a, sizeof(a), "%04d-%02d-%02d", sy, sm, reset_day);
		snprintf(b, sizeof(b), "%04d-%02d-%02d", ny, nm, reset_day);
		snprintf(start, 11, "%.10s", a);
		snprintf(next, 11, "%.10s", b);
	}
	return true;
}

json_object *mm_quota_add(json_object *usage, const char *iccid, const char *start, long long bytes)
{
	json_object *e = get(usage, iccid);

	/* a clock set back (no NTP yet at boot): the books stay in their period, the bytes count there (audit F04) */
	if (e && str(e, "period") && strcmp(str(e, "period"), start) > 0)
		start = str(e, "period");
	if (!e || !str(e, "period") || strcmp(str(e, "period"), start)) {
		e = json_object_new_object();
		json_object_object_add(e, "period", json_object_new_string(start));
		json_object_object_add(e, "bytes", json_object_new_int64(0));
		json_object_object_add(usage, iccid, e);
	}
	if (bytes > 0)
		json_object_object_add(e, "bytes", json_object_new_int64(json_object_get_int64(get(e, "bytes")) + bytes));
	return e;
}

json_object *mm_quota_view(json_object *entry, const char *key, long long limit_bytes, int warn_percent,
			   const char *start, const char *next, const char *action)
{
	json_object *o = json_object_new_object();
	long long used = entry && str(entry, "period") && !strcmp(str(entry, "period"), start) ?
			 json_object_get_int64(get(entry, "bytes")) : 0;
	const char *state = "none";

	if (warn_percent < 1 || warn_percent > 100)
		warn_percent = 80;
	json_object_object_add(o, "key", json_object_new_string(key));
	json_object_object_add(o, "used_bytes", json_object_new_int64(used));
	if (limit_bytes > 0) {
		int percent = (int)(used * 100 / limit_bytes);

		json_object_object_add(o, "limit_bytes", json_object_new_int64(limit_bytes));
		json_object_object_add(o, "percent", json_object_new_int(percent));
		state = used >= limit_bytes ? "exceeded" : percent >= warn_percent ? "warn" : "ok";
	}
	json_object_object_add(o, "warn_percent", json_object_new_int(warn_percent));
	json_object_object_add(o, "state", json_object_new_string(state));
	json_object_object_add(o, "period_start", json_object_new_string(start));
	json_object_object_add(o, "resets_on", json_object_new_string(next));
	json_object_object_add(o, "action", json_object_new_string(action && !strcmp(action, "backup") ? "backup" : "warn"));
	return o;
}

const char *mm_quota_news(json_object *entry, json_object *view)
{
	const char *state = str(view, "state");

	if (!entry || !state)
		return NULL;
	if (!strcmp(state, "exceeded") && !get(entry, "exceeded")) {
		json_object_object_add(entry, "exceeded", json_object_new_boolean(1));
		json_object_object_add(entry, "warned", json_object_new_boolean(1));
		return "quota-exceeded";
	}
	if (!strcmp(state, "warn") && !get(entry, "warned")) {
		json_object_object_add(entry, "warned", json_object_new_boolean(1));
		return "quota-warn";
	}
	return NULL;
}

void mm_quota_key(const char *iccid, char *out)
{
	unsigned long long h = 0xcbf29ce484222325ULL;	/* FNV-1a 64 */

	for (; iccid && *iccid; iccid++) {
		h ^= (unsigned char)*iccid;
		h *= 0x100000001b3ULL;
	}
	snprintf(out, 21, "sim-%016llx", h);
}

long long mm_quota_bytes(const char *gb)
{
	char *end;
	double v;

	if (!gb || !*gb)
		return 0;
	v = strtod(gb, &end);
	if (end == gb || *end || v <= 0 || v > 1e6)
		return 0;
	return (long long)(v * 1e9 + 0.5);
}
