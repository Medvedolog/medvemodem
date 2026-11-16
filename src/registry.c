#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "registry.h"

/* Absent records kept for history; the oldest go first. */
#define MM_REGISTRY_MAX_ABSENT 32

static const char *str_of(json_object *o, const char *key)
{
	json_object *v = NULL;
	return json_object_object_get_ex(o, key, &v) ? json_object_get_string(v) : NULL;
}

static long long int_of(json_object *o, const char *key)
{
	json_object *v = NULL;
	return json_object_object_get_ex(o, key, &v) ? json_object_get_int64(v) : 0;
}

static bool bool_of(json_object *o, const char *key)
{
	json_object *v = NULL;
	return json_object_object_get_ex(o, key, &v) && json_object_get_boolean(v);
}

static bool same_json(json_object *a, json_object *b, const char *key)
{
	json_object *va = NULL, *vb = NULL;
	const char *sa, *sb;

	json_object_object_get_ex(a, key, &va);
	json_object_object_get_ex(b, key, &vb);
	sa = va ? json_object_to_json_string_ext(va, JSON_C_TO_STRING_PLAIN) : "";
	sb = vb ? json_object_to_json_string_ext(vb, JSON_C_TO_STRING_PLAIN) : "";
	return !strcmp(sa, sb);
}

static json_object *add_event(json_object *events, const char *what, const char *id, long long gen)
{
	json_object *e = json_object_new_object();

	json_object_object_add(e, "event", json_object_new_string(what));
	json_object_object_add(e, "device_id", json_object_new_string(id));
	json_object_object_add(e, "generation", json_object_new_int64(gen));
	json_object_array_add(events, e);
	return e;
}

/* "a,b": the fields that differ, for the event's detail. */
static void diff_fields(json_object *old, json_object *seen, const char *const *keys, char *out, size_t size)
{
	size_t used = 0;

	out[0] = '\0';
	for (; *keys; keys++) {
		if (same_json(old, seen, *keys))
			continue;
		used += (size_t)snprintf(out + used, used < size ? size - used : 0, "%s%s", used ? "," : "", *keys);
		if (used >= size) {
			out[size - 1] = '\0';
			return;
		}
	}
}

/* Copy discovery fields onto a record, then the registry's own fields. */
static json_object *make_record(json_object *seen, long long gen, long long first, long long change)
{
	json_object *rec = json_object_new_object();

	json_object_object_foreach(seen, key, val)
		json_object_object_add(rec, key, json_object_get(val));
	json_object_object_add(rec, "present", json_object_new_boolean(1));
	json_object_object_add(rec, "generation", json_object_new_int64(gen));
	json_object_object_add(rec, "first_seen", json_object_new_int64(first));
	json_object_object_add(rec, "last_change", json_object_new_int64(change));
	return rec;
}

static void prune_absent(struct mm_registry *r)
{
	for (;;) {
		const char *oldest = NULL;
		long long oldest_t = 0;
		int absent = 0;

		json_object_object_foreach(r->devices, id, rec) {
			if (bool_of(rec, "present"))
				continue;
			absent++;
			if (!oldest || int_of(rec, "last_change") < oldest_t) {
				oldest = id;
				oldest_t = int_of(rec, "last_change");
			}
		}
		if (absent <= MM_REGISTRY_MAX_ABSENT || !oldest)
			return;
		json_object_object_del(r->devices, oldest);
	}
}

void mm_registry_init(struct mm_registry *r)
{
	r->devices = json_object_new_object();
	r->passes = 0;
}

void mm_registry_free(struct mm_registry *r)
{
	json_object_put(r->devices);
	r->devices = NULL;
}

json_object *mm_registry_apply(struct mm_registry *r, json_object *scan, long long now)
{
	json_object *events = json_object_new_array();
	json_object *seen_ids = json_object_new_object();
	size_t i;

	r->passes++;
	for (i = 0; scan && i < json_object_array_length(scan); i++) {
		json_object *seen = json_object_array_get_idx(scan, i), *old = NULL;
		const char *id = str_of(seen, "device_id");
		long long gen = 1, first = now, change = now;
		const char *what = "added";
		char why[96] = "";

		if (!id)
			continue;
		json_object_object_add(seen_ids, id, NULL);
		if (json_object_object_get_ex(r->devices, id, &old)) {
			static const char *const moved_keys[] = { "attachment_id", "ports", NULL };
			static const char *const changed_keys[] = { "detection_status", "configuration", "profile", NULL };
			bool was_present = bool_of(old, "present");

			gen = int_of(old, "generation");
			first = int_of(old, "first_seen");
			change = int_of(old, "last_change");
			what = NULL;
			diff_fields(old, seen, moved_keys, why, sizeof(why));
			if (!was_present || why[0]) {
				gen++;
				change = now;
				what = "reattached";
				if (!was_present)
					snprintf(why, sizeof(why), "returned");
			} else {
				diff_fields(old, seen, changed_keys, why, sizeof(why));
				if (why[0]) {
					change = now;
					what = "changed";
				}
			}
		}
		json_object_object_add(r->devices, id, make_record(seen, gen, first, change));
		if (what) {
			json_object *e = add_event(events, what, id, gen);
			if (why[0])
				json_object_object_add(e, "cause", json_object_new_string(why));
		}
	}

	json_object_object_foreach(r->devices, id, rec) {
		if (!bool_of(rec, "present") || json_object_object_get_ex(seen_ids, id, NULL))
			continue;
		/* Old paths must never be reused: forget ports, keep identity. */
		json_object_object_add(rec, "present", json_object_new_boolean(0));
		json_object_object_add(rec, "last_change", json_object_new_int64(now));
		json_object_object_add(rec, "ports", json_object_new_object());
		add_event(events, "removed", id, int_of(rec, "generation"));
	}
	json_object_put(seen_ids);
	prune_absent(r);
	return events;
}

json_object *mm_registry_list(const struct mm_registry *r, bool include_absent)
{
	json_object *arr = json_object_new_array();

	json_object_object_foreach(r->devices, id, rec) {
		(void)id;
		if (include_absent || bool_of(rec, "present"))
			json_object_array_add(arr, json_object_get(rec));
	}
	return arr;
}

json_object *mm_registry_find(const struct mm_registry *r, const char *id)
{
	json_object *rec = NULL;

	if (!id)
		return NULL;
	if (json_object_object_get_ex(r->devices, id, &rec))
		return rec;
	json_object_object_foreach(r->devices, key, cand) {
		const char *att = str_of(cand, "attachment_id");
		(void)key;
		if (att && bool_of(cand, "present") && !strcmp(att, id))
			return cand;
	}
	return NULL;
}

json_object *mm_registry_by_attachment(const struct mm_registry *r, const char *attachment_id)
{
	json_object *best = NULL;

	if (!attachment_id)
		return NULL;
	json_object_object_foreach(r->devices, id, rec) {
		const char *att = str_of(rec, "attachment_id");
		(void)id;
		if (!att || strcmp(att, attachment_id))
			continue;
		if (bool_of(rec, "present"))
			return rec;
		if (!best || int_of(rec, "last_change") > int_of(best, "last_change"))
			best = rec;
	}
	return best;
}

static const char *node_name(const char *node)
{
	return strncmp(node, "/dev/", 5) ? node : node + 5;
}

json_object *mm_registry_lookup_node(const struct mm_registry *r, const char *node)
{
	static const char *const kinds[] = { "tty", "control", "net" };

	if (!node)
		return NULL;
	json_object_object_foreach(r->devices, id, rec) {
		json_object *ports = NULL;
		(void)id;
		if (!bool_of(rec, "present") || !json_object_object_get_ex(rec, "ports", &ports))
			continue;
		for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
			json_object *arr = NULL;
			if (!json_object_object_get_ex(ports, kinds[k], &arr))
				continue;
			for (size_t i = 0; i < json_object_array_length(arr); i++) {
				const char *s = json_object_get_string(json_object_array_get_idx(arr, i));
				if (s && !strcmp(node_name(s), node_name(node)))
					return rec;
			}
		}
	}
	return NULL;
}
