// SPDX-License-Identifier: GPL-3.0-only
/*
 * How long each modem was without the internet today (the owner, 2026-10-27):
 * every uplink pass tells what the modem's link was (the reach check's verdict, or its interface down without a
 * session), downtime.c keeps the day's book. RAM, mirrored in <run>/downtime.json (an engine restart keeps the day, a
 * reboot starts it again); a device the registry forgot loses its book.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "daemon.h"
#include "downtime.h"

static json_object *books;	/* device_id -> downtime.c's book */
static long long saved_ms = -1;

static void save(void)
{
	char path[512], tmp[520];

	if (!books)
		return;
	snprintf(path, sizeof(path), "%s/downtime.json", run_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(run_dir, 0755);
	if (!json_object_to_file_ext(tmp, books, JSON_C_TO_STRING_PLAIN))
		rename(tmp, path);
}

void downtime_mgr_start(void)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/downtime.json", run_dir);
	books = json_object_from_file(path);
	if (!json_object_is_type(books, json_type_object)) {
		json_object_put(books);
		books = json_object_new_object();
	}
	/* the engine was away: the time since the last pass before it is not counted */
	json_object_object_foreach(books, id, b) {
		(void)id;
		json_object_object_del(b, "last_ms");
	}
}

void downtime_mgr_stop(void)
{
	save();
	json_object_put(books);
	books = NULL;
}

void downtime_note(const char *id, const char *state, const char *day)
{
	json_object *b;
	long long now = now_mono_ms();

	if (!books || !id)
		return;
	if (!(b = json_object_object_get(books, id))) {
		if (!state)
			return;		/* nothing known, nothing to keep */
		b = json_object_new_object();
		json_object_object_add(books, id, b);
	}
	mm_downtime_add(b, state, day, now);
	if (saved_ms < 0 || now - saved_ms >= 60000) {
		saved_ms = now;
		save();
	}
}

void downtime_retain(json_object *devices)
{
	json_object *gone = json_object_new_array();

	if (books) {
		json_object_object_foreach(books, id, b) {
			(void)b;
			if (!devices || !json_object_object_get(devices, id))
				json_object_array_add(gone, json_object_new_string(id));
		}
		for (size_t i = 0; i < json_object_array_length(gone); i++)
			json_object_object_del(books, json_object_get_string(json_object_array_get_idx(gone, i)));
	}
	json_object_put(gone);
}

json_object *downtime_of(const char *id)
{
	json_object *b = books && id ? json_object_object_get(books, id) : NULL;

	return b ? mm_downtime_view(b, now_mono_ms()) : NULL;
}
