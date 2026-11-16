/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cellev.h"
#include "mmgr.h"

static const char *s_of(json_object *o, const char *k)
{
	json_object *v = NULL;

	return json_object_object_get_ex(o, k, &v) && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static void put(json_object *o, const char *k, const char *v)
{
	if (v && *v)
		json_object_object_add(o, k, json_object_new_string(v));
}

/* «B3», «b3», «LTE BAND 3», «3» → «B3»; «n78» stays; anything else is not a band */
static int band_name(const char *in, const char *prefix, char *out, size_t size)
{
	const char *p = in;
	long n;
	char *end;

	if (!in || !*in)
		return 0;
	while (*p && (*p < '0' || *p > '9'))
		p++;
	n = strtol(p, &end, 10);
	if (p == end || n <= 0 || n > 512 || *end)
		return 0;
	snprintf(out, size, "%s%ld", (in[0] == 'n' || in[0] == 'N') ? "n" : prefix, n);
	return 1;
}

json_object *mm_cellev_digest_reading(json_object *m)
{
	json_object *d = json_object_new_object(), *ca, *v;
	char band[16], nr[16], buf[96] = "", one[16];
	size_t i;

	if (!m)
		return d;
	if (band_name(s_of(m, "band"), "B", band, sizeof(band))) {
		if (band_name(s_of(json_object_object_get(m, "nr"), "band"), "n", nr, sizeof(nr)))
			snprintf(buf, sizeof(buf), "%s+%s", band, nr);
		else
			snprintf(buf, sizeof(buf), "%s", band);
		put(d, "band", buf);
	}
	if ((v = json_object_object_get(m, "cell_id")) && json_object_is_type(v, json_type_int) && json_object_get_int64(v) > 0) {
		snprintf(buf, sizeof(buf), "%llX", (unsigned long long)json_object_get_int64(v));
		put(d, "cell", buf);
	} else if ((v = json_object_object_get(m, "pci")) && json_object_is_type(v, json_type_int)) {
		snprintf(buf, sizeof(buf), "PCI %d", json_object_get_int(v));
		put(d, "cell", buf);
	}
	/* the carriers: a list of band names (web readings), or the FM350's cells (AT+GTCAINFO?) */
	ca = json_object_object_get(m, "ca");
	if (json_object_is_type(ca, json_type_object))
		ca = json_object_object_get(ca, "cells");
	if (json_object_is_type(ca, json_type_array) && json_object_array_length(ca) && band_name(s_of(m, "band"), "B", band, sizeof(band))) {
		snprintf(buf, sizeof(buf), "%s", band);
		for (i = 0; i < json_object_array_length(ca) && strlen(buf) < sizeof(buf) - 12; i++) {
			json_object *c = json_object_array_get_idx(ca, i);
			const char *b = json_object_is_type(c, json_type_string) ? json_object_get_string(c) : NULL;
			char tmp[16];

			if (!b && json_object_is_type(c, json_type_object)) {
				if (s_of(c, "role") && !strcmp(s_of(c, "role"), "primary"))
					continue;
				if ((v = json_object_object_get(c, "band")) && json_object_is_type(v, json_type_int)) {
					snprintf(tmp, sizeof(tmp), "%d", json_object_get_int(v));
					b = tmp;
				}
			}
			if (b && band_name(b, "B", one, sizeof(one))) {
				strcat(buf, "+");
				strcat(buf, one);
			}
		}
		put(d, "ca", strchr(buf, '+') ? buf : "-");
	}
	return d;
}

json_object *mm_cellev_digest_mm(json_object *mm)
{
	json_object *d = json_object_new_object(), *cells = json_object_object_get(mm, "cells"), *ca, *v;
	char buf[96] = "", band[16] = "";
	size_t i;

	if (!mm)
		return d;
	for (i = 0; json_object_is_type(cells, json_type_array) && i < json_object_array_length(cells); i++) {
		json_object *c = json_object_array_get_idx(cells, i);

		if (!json_object_get_boolean(json_object_object_get(c, "serving")))
			continue;
		if (s_of(c, "type") && !strcmp(s_of(c, "type"), "lte") && (v = json_object_object_get(c, "earfcn")) &&
		    json_object_is_type(v, json_type_int) && mm_mmgr_lte_band(json_object_get_int64(v)) && !*band)
			snprintf(band, sizeof(band), "B%d", mm_mmgr_lte_band(json_object_get_int64(v)));
		if (s_of(c, "ci") && !*buf)
			snprintf(buf, sizeof(buf), "%s", s_of(c, "ci"));
		else if (s_of(c, "pci") && !*buf)
			snprintf(buf, sizeof(buf), "PCI %s", s_of(c, "pci"));
	}
	if (!*buf && s_of(json_object_object_get(mm, "location"), "cid"))
		snprintf(buf, sizeof(buf), "%s", s_of(json_object_object_get(mm, "location"), "cid"));
	put(d, "cell", buf);
	/* the carriers from the QMI read: the primary's band first, then the active secondaries' */
	ca = json_object_object_get(json_object_object_get(mm, "ca"), "cells");
	if (json_object_is_type(ca, json_type_array) && json_object_array_length(ca)) {
		char list[96] = "";
		int n = 0;

		for (i = 0; i < json_object_array_length(ca); i++) {
			json_object *c = json_object_array_get_idx(ca, i);
			const char *role = s_of(c, "role"), *st = s_of(c, "state");
			char one[16];

			if (!(v = json_object_object_get(c, "band")) || !json_object_is_type(v, json_type_int) ||
			    (st && strcmp(st, "activated") && role && strcmp(role, "primary")))
				continue;
			snprintf(one, sizeof(one), "B%d", json_object_get_int(v));
			if (role && !strcmp(role, "primary")) {
				if (!*band)
					snprintf(band, sizeof(band), "%s", one);
				if (*list) {
					char tmp[128];

					snprintf(tmp, sizeof(tmp), "%s+%s", one, list);
					snprintf(list, sizeof(list), "%.95s", tmp);
				} else {
					snprintf(list, sizeof(list), "%s", one);
				}
			} else if (strlen(list) < sizeof(list) - 12) {
				if (*list)
					strcat(list, "+");
				strcat(list, one);
			}
			n++;
		}
		if (n)
			put(d, "ca", n > 1 ? list : "-");	/* the carriers were read: one is a value too */
	}
	put(d, "band", band);
	return d;
}

int mm_cellev_observe(json_object *state, json_object *digest, long long now_ms, long long hold_ms, json_object *out)
{
	static const char *const parts[][2] = { { "band", "band-change" }, { "cell", "cell-change" }, { "ca", "ca-change" } };
	int added = 0;
	size_t k;

	if (!state || !digest)
		return 0;
	for (k = 0; k < sizeof(parts) / sizeof(parts[0]); k++) {
		const char *part = parts[k][0], *now = s_of(digest, part);
		char kc[32], kt[32];
		const char *cand, *taken;

		snprintf(kc, sizeof(kc), "%s_cand", part);
		snprintf(kt, sizeof(kt), "%s_at", part);
		if (!now)
			continue;
		cand = s_of(state, kc);
		taken = s_of(state, part);
		if (!cand || strcmp(cand, now)) {
			json_object_object_add(state, kc, json_object_new_string(now));
			continue;
		}
		/* seen twice in a row */
		if (taken && !strcmp(taken, now))
			continue;
		/* the hold counts from the last event of the kind, not from the first value */
		if (taken && (!json_object_object_get(state, kt) ||
			      now_ms - json_object_get_int64(json_object_object_get(state, kt)) >= hold_ms)) {
			char detail[200];
			json_object *ev = json_object_new_array();

			snprintf(detail, sizeof(detail), "%s → %s", !strcmp(taken, "-") ? "one carrier" : taken,
				 !strcmp(now, "-") ? "one carrier" : now);
			json_object_array_add(ev, json_object_new_string(parts[k][1]));
			json_object_array_add(ev, json_object_new_string(detail));
			json_object_array_add(out, ev);
			json_object_object_add(state, kt, json_object_new_int64(now_ms));
			added++;
		} else if (taken) {
			continue;	/* too soon after the last one: kept as it was, tried again by the next readings */
		}
		json_object_object_add(state, part, json_object_new_string(now));
	}
	return added;
}
