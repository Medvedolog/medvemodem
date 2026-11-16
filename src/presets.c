#define _GNU_SOURCE
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "at.h"
#include "presets.h"

static const char *str(json_object *o, const char *key)
{
	json_object *v = NULL;

	return o && json_object_object_get_ex(o, key, &v) && json_object_is_type(v, json_type_string) ?
	       json_object_get_string(v) : NULL;
}

static bool rule_fits(json_object *rule, const char *profile, const char *manufacturer, const char *model, const char *vid)
{
	const char *p = str(rule, "profile"), *m = str(rule, "manufacturer"), *d = str(rule, "model"), *v = str(rule, "vid");

	if (!p && !m && !d && !v)
		return false;
	if (p && (!profile || strncmp(profile, p, strlen(p))))
		return false;
	if (m && (!manufacturer || !strcasestr(manufacturer, m)))
		return false;
	if (d && (!model || !strcasestr(model, d)))
		return false;
	if (v && (!vid || strncasecmp(vid, v, strlen(v))))
		return false;
	return true;
}

json_object *mm_at_presets(json_object *catalog, const char *profile, const char *manufacturer, const char *model,
			   const char *vid)
{
	json_object *out = json_object_new_object(), *sets = json_object_new_array(), *all = NULL, *groups = NULL;
	size_t i, k;

	json_object_object_get_ex(catalog, "groups", &groups);
	json_object_object_add(out, "groups", groups ? json_object_get(groups) : json_object_new_array());
	json_object_object_get_ex(catalog, "sets", &all);
	for (i = 0; all && i < json_object_array_length(all); i++) {
		json_object *set = json_object_array_get_idx(all, i), *match = NULL, *presets = NULL, *o, *list;
		bool fits = !json_object_object_get_ex(set, "match", &match);

		for (k = 0; !fits && match && k < json_object_array_length(match); k++)
			fits = rule_fits(json_object_array_get_idx(match, k), profile, manufacturer, model, vid);
		if (!fits)
			continue;
		o = json_object_new_object();
		json_object_object_add(o, "id", json_object_new_string(str(set, "id") ? str(set, "id") : ""));
		json_object_object_add(o, "title", json_object_new_string(str(set, "title") ? str(set, "title") : ""));
		if (str(set, "source"))
			json_object_object_add(o, "source", json_object_new_string(str(set, "source")));
		list = json_object_new_array();
		json_object_object_get_ex(set, "presets", &presets);
		for (k = 0; presets && k < json_object_array_length(presets); k++) {
			json_object *p = json_object_array_get_idx(presets, k), *q;
			const char *cmd = str(p, "command"), *why;

			if (!cmd)
				continue;
			q = json_object_new_object();
			json_object_object_add(q, "group", json_object_new_string(str(p, "group") ? str(p, "group") : "info"));
			json_object_object_add(q, "title", json_object_new_string(str(p, "title") ? str(p, "title") : cmd));
			json_object_object_add(q, "command", json_object_new_string(cmd));
			json_object_object_add(q, "hazard", json_object_new_string(mm_at_hazard(cmd)));
			/* its risk and effects (the owner, 2026-11-14): LuCI asks before a disruptive or a persistent one; a dangerous
			 * one is documentation only, whatever the hazard list would let through. A risk never widens what is sent */
			{
				const char *risk = str(p, "risk");
				json_object *fx = NULL;

				if (!risk || (strcmp(risk, "disruptive") && strcmp(risk, "persistent") && strcmp(risk, "dangerous")))
					risk = "read_only";
				json_object_object_add(q, "risk", json_object_new_string(risk));
				if (json_object_object_get_ex(p, "effects", &fx) && json_object_is_type(fx, json_type_array))
					json_object_object_add(q, "effects", json_object_get(fx));
			}
			/* a template (<n>) is never sent as it stands */
			why = strchr(cmd, '<') ? "a template: the value in <> is the operator's to choose"
					       : str(p, "risk") && !strcmp(str(p, "risk"), "dangerous") ? "documentation only: a dangerous command"
					       : mm_at_console_refusal(cmd);
			json_object_object_add(q, "allowed", json_object_new_boolean(!why));
			if (why)
				json_object_object_add(q, "why", json_object_new_string(why));
			json_object_array_add(list, q);
		}
		json_object_object_add(o, "presets", list);
		json_object_array_add(sets, o);
	}
	json_object_object_add(out, "sets", sets);
	return out;
}

json_object *mm_ussd_codes(json_object *catalog, const char *plmn)
{
	json_object *ops = NULL;
	size_t i, k;

	if (!plmn || !*plmn || !json_object_object_get_ex(catalog, "operators", &ops))
		return NULL;
	for (i = 0; i < json_object_array_length(ops); i++) {
		json_object *op = json_object_array_get_idx(ops, i), *ids = NULL, *o;

		json_object_object_get_ex(op, "plmn", &ids);
		for (k = 0; ids && k < json_object_array_length(ids); k++) {
			if (strcmp(json_object_get_string(json_object_array_get_idx(ids, k)), plmn))
				continue;
			o = json_object_new_object();
			json_object_object_add(o, "name", json_object_new_string(str(op, "name") ? str(op, "name") : plmn));
			json_object_object_add(o, "plmn", json_object_new_string(plmn));
			json_object_object_add(o, "codes", json_object_get(json_object_object_get(op, "codes")));
			return o;
		}
	}
	return NULL;
}
