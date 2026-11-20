#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cfgplan.h"

static json_object *get(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_is_type(o, json_type_object) && json_object_object_get_ex(o, key, &v) ? v : NULL;
}

static const char *str(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static json_object *copy(json_object *o)
{
	json_object *c = NULL;

	if (!o || json_object_deep_copy(o, &c, NULL))
		return NULL;
	return c;
}

/* structural equality, key order ignored; NULL equals NULL only */
static bool same(json_object *a, json_object *b)
{
	size_t i;

	if (!a || !b)
		return a == b;
	if (json_object_get_type(a) != json_object_get_type(b))
		return false;
	switch (json_object_get_type(a)) {
	case json_type_object:
		if (json_object_object_length(a) != json_object_object_length(b))
			return false;
		{
			json_object_object_foreach(a, key, val) {
				json_object *w;
				if (!json_object_object_get_ex(b, key, &w) || !same(val, w))
					return false;
			}
		}
		return true;
	case json_type_array:
		if (json_object_array_length(a) != json_object_array_length(b))
			return false;
		for (i = 0; i < json_object_array_length(a); i++)
			if (!same(json_object_array_get_idx(a, i), json_object_array_get_idx(b, i)))
				return false;
		return true;
	case json_type_string:
		return !strcmp(json_object_get_string(a), json_object_get_string(b));
	default:
		return json_object_equal(a, b);
	}
}

static json_object *section_of(json_object *current, const char *package, const char *section)
{
	return section ? get(get(current, package), section) : NULL;
}

const char *mm_cfg_find_named(json_object *current, const char *package, const char *type, const char *name)
{
	json_object *p = get(current, package);

	if (!p || !type || !name)
		return NULL;
	{
		json_object_object_foreach(p, sec, val) {
			const char *t = str(val, ".type"), *n = str(val, "name");
			if (t && n && !strcmp(t, type) && !strcmp(n, name))
				return sec;
		}
	}
	return NULL;
}

/* list value as words: UCI lists, or an option holding space-separated words */
static bool list_has(json_object *v, const char *value)
{
	size_t i, n = strlen(value);
	const char *s;

	if (!v)
		return false;
	if (json_object_is_type(v, json_type_array)) {
		for (i = 0; i < json_object_array_length(v); i++)
			if (!strcmp(json_object_get_string(json_object_array_get_idx(v, i)), value))
				return true;
		return false;
	}
	for (s = json_object_get_string(v); s && *s; ) {
		size_t w;
		s += strspn(s, " \t");
		w = strcspn(s, " \t");
		if (w == n && !strncmp(s, value, n))
			return true;
		s += w;
	}
	return false;
}

/* the option as a list of words (new array) */
static json_object *as_list(json_object *v)
{
	json_object *out = json_object_new_array();
	const char *s;
	size_t i;

	if (v && json_object_is_type(v, json_type_array)) {
		for (i = 0; i < json_object_array_length(v); i++)
			json_object_array_add(out, json_object_get(json_object_array_get_idx(v, i)));
		return out;
	}
	for (s = v ? json_object_get_string(v) : NULL; s && *s; ) {
		size_t w;
		s += strspn(s, " \t");
		w = strcspn(s, " \t");
		if (w)
			json_object_array_add(out, json_object_new_string_len(s, (int)w));
		s += w;
	}
	return out;
}

static json_object *new_op(const char *op, const char *package, const char *section)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "op", json_object_new_string(op));
	json_object_object_add(o, "package", json_object_new_string(package));
	json_object_object_add(o, "section", section ? json_object_new_string(section) : NULL);
	return o;
}

static json_object *field_op(const char *op, const char *package, const char *section, const char *option,
			     json_object *value, json_object *expect)
{
	json_object *o = new_op(op, package, section);

	json_object_object_add(o, "option", json_object_new_string(option));
	if (value)
		json_object_object_add(o, "value", copy(value));
	json_object_object_add(o, "expect", copy(expect));
	return o;
}

static json_object *list_op(const char *op, json_object *item, const char *section)
{
	json_object *o = new_op(op, str(item, "package"), section), *m = json_object_new_object();

	json_object_object_add(o, "option", json_object_new_string(str(item, "option")));
	json_object_object_add(o, "value", json_object_new_string(str(item, "value")));
	json_object_object_add(m, "type", json_object_new_string(str(item, "type")));
	json_object_object_add(m, "name", json_object_new_string(str(item, "name")));
	json_object_object_add(o, "match", m);
	return o;
}

static void conflict(json_object *out, const char *path, const char *reason, json_object *current, json_object *ours,
		     json_object *wanted)
{
	json_object *c = json_object_new_object();

	json_object_object_add(c, "path", json_object_new_string(path));
	json_object_object_add(c, "reason", json_object_new_string(reason));
	if (current)
		json_object_object_add(c, "current", copy(current));
	if (ours)
		json_object_object_add(c, "ours", copy(ours));
	if (wanted)
		json_object_object_add(c, "wanted", copy(wanted));
	json_object_array_add(out, c);
}

static bool user_field(const char *key)
{
	return key[0] != '.';
}

static void plan_section(const char *key, json_object *current, json_object *led, json_object *des, json_object *ops,
			 json_object *conflicts, json_object *sections)
{
	char package[64], path[256];
	const char *section = strchr(key, '.');
	json_object *cur, *lf = get(led, "fields"), *df = get(des, "fields"), *fields, *mine, *names;
	size_t i;

	if (!section || section == key || (size_t)(section - key) >= sizeof(package))
		return;
	snprintf(package, sizeof(package), "%.*s", (int)(section - key), key);
	section++;
	cur = section_of(current, package, section);

	if (!led || !cur) {
		if (cur) {
			if (des)
				conflict(conflicts, key, "exists-unmanaged", NULL, NULL, NULL);
			return;
		}
		if (!des)
			return;
		{
			json_object *op = new_op("add-section", package, section);
			json_object_object_add(op, "type", json_object_new_string(str(des, "type")));
			json_object_object_add(op, "expect", NULL);
			json_object_array_add(ops, op);
		}
		if (df) {
			json_object_object_foreach(df, f, v)
				json_object_array_add(ops, field_op("set", package, section, f, v, NULL));
		}
		json_object_object_add(sections, key, copy(des));
		return;
	}
	if (!str(cur, ".type") || !str(led, "type") || strcmp(str(cur, ".type"), str(led, "type"))) {
		conflict(conflicts, key, "type-changed", get(cur, ".type"), get(led, "type"), NULL);
		return;
	}

	fields = json_object_new_object();
	mine = json_object_new_array();
	names = json_object_new_array();
	if (lf) {
		json_object_object_foreach(lf, f, v) {
			(void)v;
			json_object_array_add(names, json_object_new_string(f));
		}
	}
	if (df) {
		json_object_object_foreach(df, f, v) {
			(void)v;
			if (!get(lf, f))
				json_object_array_add(names, json_object_new_string(f));
		}
	}
	for (i = 0; i < json_object_array_length(names); i++) {
		const char *f = json_object_get_string(json_object_array_get_idx(names, i));
		json_object *cv = get(cur, f), *lv = get(lf, f), *dv = get(df, f);

		snprintf(path, sizeof(path), "%s.%s", key, f);
		if (!lv) {
			/* a stale value in a field the ledger had left to the user is taken back too (the audit of r162: a path
			 * set by hand, the field then the user's, the next boot's bus numbering left mm0 on a path that names
			 * nothing): the exact stale value only, so another value of the user's stays theirs */
			if (cv && dv && get(get(des, "takes"), f) && same(cv, get(get(des, "takes"), f))) {
				if (!same(cv, dv))
					json_object_array_add(mine, field_op("set", package, section, f, dv, cv));
				json_object_object_add(fields, f, copy(dv));
				continue;
			}
			if (cv) {
				if (dv && !same(cv, dv))
					conflict(conflicts, path, "user-field", cv, NULL, dv);
				continue;
			}
			if (dv) {
				json_object_array_add(mine, field_op("set", package, section, f, dv, NULL));
				json_object_object_add(fields, f, copy(dv));
			}
			continue;
		}
		/* a value the engine found stale names nothing (a sysfs path of the last boot's bus numbering): taken back
		 * whoever wrote it (a tester's OPiZ3, 2026-11-05: a path set by hand made every later repair «user-changed»,
		 * and mm0 stayed down after the next boot renumbered the buses) */
		/* an interface's proto or device gone is no choice of the user's: without it the interface cannot work at all
		 * (netifd: NO_DEVICE), and one who wants no modem link disables it or deletes the section. Written again (the
		 * owner's T99W175, 2026-11-19..20: mm0 kept only its metric, every bring-up skipped the two as «user-removed»
		 * and the interface stayed broken for a day) */
		if (!cv && dv && !strcmp(package, "network") && (!strcmp(f, "proto") || !strcmp(f, "device"))) {
			json_object_array_add(mine, field_op("set", package, section, f, dv, NULL));
			json_object_object_add(fields, f, copy(dv));
			continue;
		}
		if (!same(cv, lv) && !(cv && get(get(des, "takes"), f) && same(cv, get(get(des, "takes"), f)))) {
			conflict(conflicts, path, cv ? "user-changed" : "user-removed", cv, lv, dv);
			continue;
		}
		if (!dv) {
			json_object_array_add(mine, field_op("delete", package, section, f, NULL, cv));
			continue;
		}
		if (!same(cv, dv))
			json_object_array_add(mine, field_op("set", package, section, f, dv, cv));
		json_object_object_add(fields, f, copy(dv));
	}
	json_object_put(names);

	if (!des && json_object_get_boolean(get(led, "adopted"))) {
		/* an interface the operator handed over: released, it stays the user's as it is */
		json_object_put(mine);
		json_object_put(fields);
		return;
	}
	if (!des) {
		int user = 0;

		json_object_object_foreach(cur, f, v)
			if (user_field(f) && !same(v, get(lf, f)))
				user++;
		if (!user) {
			json_object *op = new_op("delete-section", package, section);
			json_object_object_add(op, "expect", copy(cur));
			json_object_array_add(ops, op);
		} else {
			for (i = 0; i < json_object_array_length(mine); i++)
				json_object_array_add(ops, json_object_get(json_object_array_get_idx(mine, i)));
			conflict(conflicts, key, "kept-user-fields", NULL, NULL, NULL);
		}
		json_object_put(mine);
		json_object_put(fields);
		return;
	}
	for (i = 0; i < json_object_array_length(mine); i++)
		json_object_array_add(ops, json_object_get(json_object_array_get_idx(mine, i)));
	json_object_put(mine);
	{
		json_object *s = json_object_new_object();
		json_object_object_add(s, "type", json_object_new_string(str(led, "type")));
		json_object_object_add(s, "fields", fields);
		if (json_object_get_boolean(get(led, "adopted")))
			json_object_object_add(s, "adopted", json_object_new_boolean(1));
		json_object_object_add(sections, key, s);
	}
}

static bool same_item(json_object *a, json_object *b)
{
	static const char *const keys[] = { "package", "type", "name", "option", "value" };
	size_t i;

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		const char *x = str(a, keys[i]), *y = str(b, keys[i]);
		if (!x || !y || strcmp(x, y))
			return false;
	}
	return true;
}

static bool has_item(json_object *items, json_object *item)
{
	size_t i;

	for (i = 0; items && i < json_object_array_length(items); i++)
		if (same_item(json_object_array_get_idx(items, i), item))
			return true;
	return false;
}

/* a firewall entry naming an interface the operator handed over: it stays with the interface */
static bool adopted_entry(json_object *sections, json_object *it)
{
	char key[128];

	if (strcmp(str(it, "package") ? str(it, "package") : "", "firewall") || !str(it, "value"))
		return false;
	snprintf(key, sizeof(key), "network.%s", str(it, "value"));
	return json_object_get_boolean(get(get(sections, key), "adopted"));
}

/* the interface is listed by some firewall zone */
static bool in_any_zone(json_object *current, const char *iface)
{
	json_object *fw = get(current, "firewall");

	if (!fw || !iface)
		return false;
	{
		json_object_object_foreach(fw, sec, z) {
			(void)sec;
			if (str(z, ".type") && !strcmp(str(z, ".type"), "zone") && list_has(get(z, "network"), iface))
				return true;
		}
	}
	return false;
}

static void plan_items(json_object *current, json_object *led, json_object *des, json_object *ops,
		       json_object *conflicts, json_object *kept, json_object *sections)
{
	json_object *all = json_object_new_array();
	size_t i;

	for (i = 0; led && i < json_object_array_length(led); i++)
		json_object_array_add(all, json_object_get(json_object_array_get_idx(led, i)));
	for (i = 0; des && i < json_object_array_length(des); i++)
		if (!has_item(led, json_object_array_get_idx(des, i)))
			json_object_array_add(all, json_object_get(json_object_array_get_idx(des, i)));

	for (i = 0; i < json_object_array_length(all); i++) {
		json_object *it = json_object_array_get_idx(all, i);
		const char *pkg = str(it, "package"), *sec = mm_cfg_find_named(current, pkg, str(it, "type"), str(it, "name"));
		bool inl = has_item(led, it), ind = has_item(des, it), present;
		char path[256];

		if (!pkg || !str(it, "option") || !str(it, "value"))
			continue;
		snprintf(path, sizeof(path), "%s.@%s[name=%s].%s:%s", pkg, str(it, "type"), str(it, "name"),
			 str(it, "option"), str(it, "value"));
		if (!sec) {
			if (ind)
				conflict(conflicts, path, "section-missing", NULL, NULL, NULL);
			continue;
		}
		present = list_has(get(section_of(current, pkg, sec), str(it, "option")), str(it, "value"));
		if (ind && !inl) {
			if (!present) {
				json_object_array_add(ops, list_op("add-list", it, sec));
				json_object_array_add(kept, copy(it));
			}
		} else if (ind) {
			if (present)
				json_object_array_add(kept, copy(it));
			/*
			 * our interface in a firewall zone, gone from it and now in no zone at all: deleting the interface in
			 * LuCI takes it out of the zone too, and the engine's new one stayed outside, with no NAT for the LAN
			 * (an FM350 tester, 2026-11-03: «инета на телефоне нет»). Put back; one moved to another zone stays.
			 */
			else if (!strcmp(pkg, "firewall") && str(it, "option") && !strcmp(str(it, "option"), "network") &&
				 !in_any_zone(current, str(it, "value"))) {
				json_object_array_add(ops, list_op("add-list", it, sec));
				json_object_array_add(kept, copy(it));
			} else
				conflict(conflicts, path, "user-removed", NULL, NULL, NULL);
		} else if (present && !adopted_entry(sections, it)) {
			json_object_array_add(ops, list_op("del-list", it, sec));
		}
	}
	json_object_put(all);
}

static json_object *find_option(json_object *list, json_object *o)
{
	size_t i;

	for (i = 0; list && i < json_object_array_length(list); i++) {
		json_object *x = json_object_array_get_idx(list, i);
		if (str(x, "package") && str(o, "package") && !strcmp(str(x, "package"), str(o, "package")) &&
		    str(x, "section") && str(o, "section") && !strcmp(str(x, "section"), str(o, "section")) &&
		    str(x, "option") && str(o, "option") && !strcmp(str(x, "option"), str(o, "option")))
			return x;
	}
	return NULL;
}

/* single fields in sections owned by someone else (three-way, previous value kept) */
static void plan_options(json_object *current, json_object *led, json_object *des, json_object *ops,
			 json_object *conflicts, json_object *kept)
{
	json_object *all = json_object_new_array();
	size_t i;

	for (i = 0; led && i < json_object_array_length(led); i++)
		json_object_array_add(all, json_object_get(json_object_array_get_idx(led, i)));
	for (i = 0; des && i < json_object_array_length(des); i++)
		if (!find_option(led, json_object_array_get_idx(des, i)))
			json_object_array_add(all, json_object_get(json_object_array_get_idx(des, i)));

	for (i = 0; i < json_object_array_length(all); i++) {
		json_object *o = json_object_array_get_idx(all, i), *l = find_option(led, o), *d = find_option(des, o);
		const char *pkg = str(o, "package"), *sec = str(o, "section"), *opt = str(o, "option");
		json_object *s = section_of(current, pkg, sec), *cv;
		char path[256];

		if (!pkg || !sec || !opt)
			continue;
		snprintf(path, sizeof(path), "%s.%s.%s", pkg, sec, opt);
		if (!s) {
			if (d)
				conflict(conflicts, path, "section-missing", NULL, NULL, NULL);
			continue;
		}
		cv = get(s, opt);
		if (l && !same(cv, get(l, "value"))) {
			/* changed since we wrote it: the user's now */
			conflict(conflicts, path, cv ? "user-changed" : "user-removed", cv, get(l, "value"), d ? get(d, "value") : NULL);
			continue;
		}
		if (d) {
			json_object *k = copy(d);
			json_object_object_add(k, "previous", copy(l ? get(l, "previous") : cv));
			if (!same(cv, get(d, "value")))
				json_object_array_add(ops, field_op("set", pkg, sec, opt, get(d, "value"), cv));
			json_object_array_add(kept, k);
		} else {
			/* released: put back what was there before us */
			json_object *prev = get(l, "previous");
			if (prev)
				json_object_array_add(ops, field_op("set", pkg, sec, opt, prev, cv));
			else
				json_object_array_add(ops, field_op("delete", pkg, sec, opt, NULL, cv));
		}
	}
	json_object_put(all);
}

/* ---- simulation and guards ---- */

void mm_cfg_simulate(json_object *current, json_object *op)
{
	const char *kind = str(op, "op"), *pkg = str(op, "package"), *sec = str(op, "section"), *opt = str(op, "option");
	json_object *p = get(current, pkg), *s;

	if (!kind || !pkg)
		return;
	if (!p) {
		p = json_object_new_object();
		json_object_object_add(current, pkg, p);
	}
	s = sec ? get(p, sec) : NULL;
	if (!strcmp(kind, "add-section")) {
		s = json_object_new_object();
		json_object_object_add(s, ".type", json_object_new_string(str(op, "type")));
		json_object_object_add(p, sec, s);
	} else if (!strcmp(kind, "delete-section")) {
		json_object_object_del(p, sec);
	} else if (!s || !opt) {
		return;
	} else if (!strcmp(kind, "set")) {
		json_object_object_add(s, opt, copy(get(op, "value")));
	} else if (!strcmp(kind, "delete")) {
		json_object_object_del(s, opt);
	} else if (!strcmp(kind, "add-list") || !strcmp(kind, "del-list")) {
		json_object *list = as_list(get(s, opt)), *out = json_object_new_array();
		const char *v = str(op, "value");
		size_t i;

		for (i = 0; i < json_object_array_length(list); i++) {
			json_object *x = json_object_array_get_idx(list, i);
			if (strcmp(json_object_get_string(x), v))
				json_object_array_add(out, json_object_get(x));
		}
		if (!strcmp(kind, "add-list"))
			json_object_array_add(out, json_object_new_string(v));
		json_object_put(list);
		if (json_object_array_length(out))
			json_object_object_add(s, opt, out);
		else {
			json_object_put(out);
			json_object_object_del(s, opt);
		}
	}
}

static const char *check_one(json_object *state, json_object *op)
{
	const char *kind = str(op, "op"), *pkg = str(op, "package"), *sec = str(op, "section"), *opt = str(op, "option");
	json_object *s = section_of(state, pkg, sec);

	if (!kind || !pkg)
		return "malformed";
	if (!strcmp(kind, "add-section"))
		return !sec ? "malformed" : s ? "section-exists" : NULL;
	if (!s)
		return "section-missing";
	if (!strcmp(kind, "delete-section"))
		return same(s, get(op, "expect")) ? NULL : "changed-since";
	if (!opt)
		return "malformed";
	if (!strcmp(kind, "set") || !strcmp(kind, "delete"))
		return same(get(s, opt), get(op, "expect")) ? NULL : "changed-since";
	if (!strcmp(kind, "add-list"))
		return list_has(get(s, opt), str(op, "value")) ? "already-present" : NULL;
	if (!strcmp(kind, "del-list"))
		return list_has(get(s, opt), str(op, "value")) ? NULL : "already-absent";
	return "malformed";
}

json_object *mm_cfg_check(json_object *current, json_object *ops)
{
	json_object *state = copy(current), *apply = json_object_new_array(), *skipped = json_object_new_array();
	json_object *out = json_object_new_object();
	size_t i;

	if (!state)
		state = json_object_new_object();
	for (i = 0; ops && i < json_object_array_length(ops); i++) {
		json_object *op = copy(json_object_array_get_idx(ops, i)), *m = get(op, "match");
		const char *why;

		if (m) {
			/* list ops follow the named section, wherever it is now */
			const char *sec = mm_cfg_find_named(state, str(op, "package"), str(m, "type"), str(m, "name"));
			json_object_object_add(op, "section", sec ? json_object_new_string(sec) : NULL);
		}
		why = check_one(state, op);
		if (why) {
			json_object *s = json_object_new_object();
			json_object_object_add(s, "op", op);
			json_object_object_add(s, "reason", json_object_new_string(why));
			json_object_array_add(skipped, s);
			continue;
		}
		mm_cfg_simulate(state, op);
		json_object_array_add(apply, op);
	}
	json_object_put(state);
	json_object_object_add(out, "apply", apply);
	json_object_object_add(out, "skipped", skipped);
	return out;
}

/* ---- undo ---- */

static bool added_here(json_object *ops, const char *pkg, const char *sec)
{
	size_t i;

	for (i = 0; sec && i < json_object_array_length(ops); i++) {
		json_object *op = json_object_array_get_idx(ops, i);
		if (!strcmp(str(op, "op"), "add-section") && !strcmp(str(op, "package"), pkg) &&
		    !strcmp(str(op, "section"), sec))
			return true;
	}
	return false;
}

static json_object *build_undo(json_object *current, json_object *ops)
{
	json_object *after = copy(current), *undo = json_object_new_array();
	size_t i;

	if (!after)
		after = json_object_new_object();
	for (i = 0; i < json_object_array_length(ops); i++)
		mm_cfg_simulate(after, json_object_array_get_idx(ops, i));
	for (i = json_object_array_length(ops); i-- > 0; ) {
		json_object *op = json_object_array_get_idx(ops, i), *u = NULL;
		const char *kind = str(op, "op"), *pkg = str(op, "package"), *sec = str(op, "section"), *opt = str(op, "option");

		if (!strcmp(kind, "add-section")) {
			u = new_op("delete-section", pkg, sec);
			json_object_object_add(u, "expect", copy(section_of(after, pkg, sec)));
		} else if (!strcmp(kind, "set") || !strcmp(kind, "delete")) {
			if (added_here(ops, pkg, sec))
				continue;	/* the section's delete undoes it */
			if (get(op, "expect"))
				u = field_op("set", pkg, sec, opt, get(op, "expect"), get(op, "value"));
			else
				u = field_op("delete", pkg, sec, opt, NULL, get(op, "value"));
		} else if (!strcmp(kind, "delete-section")) {
			json_object *old = get(op, "expect");
			u = new_op("add-section", pkg, sec);
			json_object_object_add(u, "type", json_object_new_string(str(old, ".type")));
			json_object_object_add(u, "expect", NULL);
			json_object_array_add(undo, u);
			{
				json_object_object_foreach(old, f, v)
					if (user_field(f))
						json_object_array_add(undo, field_op("set", pkg, sec, f, v, NULL));
			}
			continue;
		} else if (!strcmp(kind, "add-list") || !strcmp(kind, "del-list")) {
			u = copy(op);
			json_object_object_add(u, "op", json_object_new_string(!strcmp(kind, "add-list") ? "del-list" : "add-list"));
		}
		if (u)
			json_object_array_add(undo, u);
	}
	json_object_put(after);
	return undo;
}

json_object *mm_cfg_plan(json_object *current, json_object *ledger, json_object *desired)
{
	json_object *out = json_object_new_object(), *ops = json_object_new_array(), *conflicts = json_object_new_array();
	json_object *newled = json_object_new_object(), *sections = json_object_new_object(), *items = json_object_new_array();
	json_object *ls = get(ledger, "sections"), *ds = get(desired, "sections"), *packages = json_object_new_array();
	json_object *options = json_object_new_array();
	size_t i, j;

	if (ls) {
		json_object_object_foreach(ls, key, led)
			plan_section(key, current, led, get(ds, key), ops, conflicts, sections);
	}
	if (ds) {
		json_object_object_foreach(ds, key, des)
			if (!get(ls, key))
				plan_section(key, current, NULL, des, ops, conflicts, sections);
	}
	plan_items(current, get(ledger, "list_items"), get(desired, "list_items"), ops, conflicts, items, ls);
	plan_options(current, get(ledger, "options"), get(desired, "options"), ops, conflicts, options);

	if (desired) {
		json_object_object_add(newled, "interface", copy(get(desired, "interface")));
		json_object_object_add(newled, "sections", sections);
		json_object_object_add(newled, "list_items", items);
		json_object_object_add(newled, "options", options);
		/* everything else the generator says (backend, slot...) rides along */
		json_object_object_foreach(desired, k, v)
			if (!get(newled, k))
				json_object_object_add(newled, k, copy(v));
	} else {
		/* released: what we could not remove is the user's now */
		json_object_put(sections);
		json_object_put(items);
		json_object_put(options);
		json_object_put(newled);
		newled = NULL;
	}
	for (i = 0; i < json_object_array_length(ops); i++) {
		const char *p = str(json_object_array_get_idx(ops, i), "package");
		bool seen = false;
		for (j = 0; j < json_object_array_length(packages); j++)
			seen = seen || !strcmp(json_object_get_string(json_object_array_get_idx(packages, j)), p);
		if (!seen)
			json_object_array_add(packages, json_object_new_string(p));
	}
	json_object_object_add(out, "ops", ops);
	json_object_object_add(out, "undo", build_undo(current, ops));
	json_object_object_add(out, "conflicts", conflicts);
	json_object_object_add(out, "ledger", newled);
	json_object_object_add(out, "packages", packages);
	return out;
}
