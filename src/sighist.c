/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sighist.h"

#define RAW_S 300		/* every sample of the last 5 minutes */
#define RAW_MAX 128
#define M1_MAX 60		/* one-minute averages: the last hour */
#define M5_MAX 288		/* five-minute averages: the last day */
#define EVENTS_MAX 64
#define CARRIERS_MAX 8		/* serving cells kept per sample: the primary, the secondaries, the NR leg */

static const char *const FIELDS[] = { "lte_rsrp", "lte_rsrq", "lte_sinr", "nr_rsrp", "nr_rsrq", "nr_sinr", "quality", NULL };

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

static bool num(json_object *o, const char *key, double *v)
{
	json_object *x = get(o, key);

	if (!x || !(json_object_is_type(x, json_type_double) || json_object_is_type(x, json_type_int)))
		return false;
	*v = json_object_get_double(x);
	return true;
}

static json_object *array(json_object *hist, const char *key)
{
	json_object *a = get(hist, key);

	if (!a || !json_object_is_type(a, json_type_array)) {
		a = json_object_new_array();
		json_object_object_add(hist, key, a);
	}
	return a;
}

/* drop the oldest entries beyond max */
static void trim(json_object *a, size_t max)
{
	size_t n = json_object_array_length(a);

	if (n > max)
		json_object_array_del_idx(a, 0, n - max);
}

static void event(json_object *hist, long long t, const char *kind, const char *detail)
{
	json_object *e = json_object_new_object(), *a = array(hist, "events");

	json_object_object_add(e, "t", json_object_new_int64(t));
	json_object_object_add(e, "kind", json_object_new_string(kind));
	json_object_object_add(e, "detail", json_object_new_string(detail));
	json_object_array_add(a, e);
	trim(a, EVENTS_MAX);
}

/* "lte, 5gnr" from ModemManager's access technologies; a list longer than out is cut, never written past
 * (audit 2026-10-23, MEM-01: snprintf's return is the length wanted, so n could pass size and out + n point outside) */
static void tech_of(json_object *mm, char *out, size_t size)
{
	json_object *at = get(mm, "access_tech");
	size_t i, n = 0;
	int w;

	out[0] = '\0';
	for (i = 0; at && json_object_is_type(at, json_type_array) && i < json_object_array_length(at); i++) {
		const char *v = json_object_get_string(json_object_array_get_idx(at, i));

		w = snprintf(out + n, size - n, "%s%s", n ? ", " : "", v ? v : "");
		if (w < 0 || (size_t)w >= size - n)
			return;		/* cut: out holds what fit, terminated */
		n += (size_t)w;
	}
}

static const char *text(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v ? json_object_get_string(v) : "";
}

/*
 * the serving cell when the cells were read: its label "lte b3 pci 123" and its
 * identity (type, PLMN, TAC, cell id, ARFCN, PCI: a PCI repeats every few
 * cells, audit F06); no identifier of the subscriber
 */
static void cell_of(json_object *mm, char *out, size_t size, char *id, size_t id_size, char *ci, size_t ci_size)
{
	json_object *cells = get(mm, "cells");
	size_t i;

	out[0] = id[0] = ci[0] = '\0';
	for (i = 0; cells && json_object_is_type(cells, json_type_array) && i < json_object_array_length(cells); i++) {
		json_object *c = json_object_array_get_idx(cells, i), *serving = get(c, "serving"), *pci = get(c, "pci"), *band = get(c, "band");

		if (!serving || !json_object_get_boolean(serving))
			continue;
		snprintf(out, size, "%s%s%s pci %s", str(c, "type") ? str(c, "type") : "cell",
			 band ? " b" : "", band ? json_object_get_string(band) : "", pci ? json_object_get_string(pci) : "?");
		snprintf(id, id_size, "%s|%s|%s|%s|%s%s%s%s|%s", text(c, "type"), text(c, "plmn"), text(c, "tac"), text(c, "ci"),
			 text(c, "earfcn"), text(c, "nrarfcn"), text(c, "uarfcn"), text(c, "arfcn"), text(c, "pci"));
		snprintf(ci, ci_size, "%s", text(c, "ci"));
		return;
	}
}

/*
 * The samples and the averages themselves are kept outside json-c, in plain arrays hung on the history object as
 * its userdata (the owner, 2026-10-29: 3.9 MB → 6.5 MB in two days with 5 modems; as json objects a modem's day of
 * history took about 1 MB, here about 70 KB). A carrier's and a technology's name is kept once per history.
 */
#define NF 7		/* FIELDS */
#define KEYS_MAX 32	/* carrier names a history remembers ("lte-3", "5gnr-78", "lte-3#2") */
#define TECHS_MAX 16	/* technology lists ("lte, 5gnr") */
#define NONE 255

struct cval {
	unsigned char key;
	unsigned short n;
	double sum;
};

struct pt {
	long long t;
	double v[NF];			/* a sample's value, or a bucket's sum */
	unsigned short n[NF];		/* 1 for a sample's value; a bucket's count */
	unsigned char tech;		/* index in techs, NONE */
	unsigned char nc;
	struct cval *c;			/* nc carriers, NULL when none */
};

struct series {
	struct pt *p;
	size_t len, cap;
};

struct store {
	struct series raw, m1, m5;
	char keys[KEYS_MAX][16];
	int nkeys;
	char techs[TECHS_MAX][64];
	int ntechs;
};

static void series_drop(struct series *s, size_t k)
{
	size_t i;

	if (k > s->len)
		k = s->len;
	for (i = 0; i < k; i++)
		free(s->p[i].c);
	memmove(s->p, s->p + k, (s->len - k) * sizeof(*s->p));
	s->len -= k;
}

static void store_free(json_object *jso, void *data)
{
	struct store *st = data;

	(void)jso;
	if (!st)
		return;
	series_drop(&st->raw, st->raw.len);
	series_drop(&st->m1, st->m1.len);
	series_drop(&st->m5, st->m5.len);
	free(st->raw.p);
	free(st->m1.p);
	free(st->m5.p);
	free(st);
}

static struct store *store_of(json_object *hist, bool make)
{
	struct store *st = hist ? json_object_get_userdata(hist) : NULL;

	if (!st && make && hist && (st = calloc(1, sizeof(*st))))
		json_object_set_userdata(hist, st, store_free);
	return st;
}

static int intern(char (*tab)[16], int *n, int max, const char *name, size_t width)
{
	int i;

	for (i = 0; i < *n; i++)
		if (!strncmp(tab[i], name, width - 1))
			return i;
	if (*n >= max)
		return NONE;
	snprintf(tab[*n], width, "%s", name);
	return (*n)++;
}

static int tech_index(struct store *st, const char *tech)
{
	int i;

	for (i = 0; i < st->ntechs; i++)
		if (!strcmp(st->techs[i], tech))
			return i;
	if (st->ntechs >= TECHS_MAX)
		return NONE;
	snprintf(st->techs[st->ntechs], sizeof(st->techs[0]), "%s", tech);
	return st->ntechs++;
}

static struct pt *series_push(struct series *s, size_t max)
{
	if (s->len == s->cap) {
		size_t cap = s->cap ? s->cap * 2 : 16;
		struct pt *p;

		if (cap > max + 1)
			cap = max + 1;
		if (cap <= s->len || !(p = realloc(s->p, cap * sizeof(*p))))
			return NULL;
		s->p = p;
		s->cap = cap;
	}
	memset(&s->p[s->len], 0, sizeof(s->p[0]));
	return &s->p[s->len++];
}

/*
 * The serving cells' RSRP of a fresh cell read, per carrier: "lte-3": -91.0, "lte-7": -97.0, "5gnr-78": -101.0
 * (spec 25.9.13, carriers). Only when the cells were read at this sample's time: a read without them keeps
 * the last ones (medvemodemd.c), and an old value must not repeat as if measured again.
 */
static void carriers_of(struct store *st, json_object *mm, long long t, struct pt *s)
{
	json_object *cells = get(mm, "cells");
	struct cval c[CARRIERS_MAX];
	double at;
	size_t i;
	int nc = 0;

	if (!num(mm, "radio_at", &at) || (long long)at != t)
		return;
	for (i = 0; cells && json_object_is_type(cells, json_type_array) && i < json_object_array_length(cells); i++) {
		json_object *cell = json_object_array_get_idx(cells, i), *serving = get(cell, "serving"), *band = get(cell, "band");
		const char *type = str(cell, "type");
		char key[40];
		double v;
		int dup, k, j;
		bool taken;

		if (!serving || !json_object_get_boolean(serving) || !type || !num(cell, "rsrp", &v) || v < -160 || v > -20)
			continue;
		if (band)
			snprintf(key, sizeof(key), "%.12s-%d", type, json_object_get_int(band));
		else
			snprintf(key, sizeof(key), "%.12s", type);
		/* two carriers in one band (intra-band aggregation): the second one apart */
		for (dup = 2; dup < 9; dup++) {
			k = intern(st->keys, &st->nkeys, KEYS_MAX, key, sizeof(st->keys[0]));
			for (taken = false, j = 0; j < nc; j++)
				taken = taken || (k != NONE && c[j].key == k);
			if (!taken)
				break;
			size_t n = strcspn(key, "#");
			snprintf(key + n, sizeof(key) - n, "#%d", dup);
		}
		k = intern(st->keys, &st->nkeys, KEYS_MAX, key, sizeof(st->keys[0]));
		if (k == NONE || nc >= CARRIERS_MAX)
			continue;
		for (taken = false, j = 0; j < nc; j++)
			taken = taken || c[j].key == k;
		if (taken)
			continue;
		c[nc].key = (unsigned char)k;
		c[nc].n = 1;
		c[nc].sum = v;
		nc++;
	}
	if (nc && (s->c = malloc((size_t)nc * sizeof(*s->c)))) {
		memcpy(s->c, c, (size_t)nc * sizeof(*c));
		s->nc = (unsigned char)nc;
	}
}

/* a sample from ModemManager's view; false: nothing in it worth keeping */
static bool sample_of(struct store *st, json_object *mm, long long t, struct pt *s)
{
	json_object *sig = get(mm, "signal"), *lte = get(sig, "lte"), *nr = get(sig, "5g");
	double v;
	bool any = false;
	char tech[64];
	int f = 0;

	memset(s, 0, sizeof(*s));
	s->t = t;
	s->tech = NONE;
	/* a modem's «no value» (-32768, 0x7fff, 255) is no reading: one such sample stretched the chart's axis over
	 * thousands of grid lines and stacked its labels (the owner's T99, 2026-10-21) */
#define PUT(src, key, lo, hi) \
	if (num(src, key, &v) && v >= (lo) && v <= (hi)) { s->v[f] = v; s->n[f] = 1; any = true; } f++;
	PUT(lte, "rsrp", -160, -20)
	PUT(lte, "rsrq", -45, 5)
	PUT(lte, "snr", -30, 60)
	PUT(nr, "rsrp", -160, -20)
	PUT(nr, "rsrq", -45, 5)
	PUT(nr, "snr", -30, 60)
	PUT(mm, "signal_quality", 0, 100)
#undef PUT
	tech_of(mm, tech, sizeof(tech));
	if (*tech)
		s->tech = (unsigned char)tech_index(st, tech);
	carriers_of(st, mm, t, s);
	if (s->nc)
		any = true;
	if (!any) {
		free(s->c);
		s->c = NULL;
		s->nc = 0;
	}
	return any;
}

/* fold a sample into the bucket of its period in a coarse series (sign 1), or take it back out (sign -1) */
static void fold_by(struct series *series, const struct pt *s, long long period, size_t max, int sign)
{
	long long start = s->t - s->t % period;
	struct pt *b = series->len ? &series->p[series->len - 1] : NULL;
	int f, i, j;

	if (!b || b->t != start) {
		if (sign < 0)
			return;		/* its bucket is gone already */
		if (!(b = series_push(series, max)))
			return;
		b->t = start;
		b->tech = NONE;
		if (series->len > max) {
			series_drop(series, series->len - max);
			b = &series->p[series->len - 1];
		}
	}
	for (f = 0; f < NF; f++)
		if (s->n[f]) {
			b->v[f] += sign * s->v[f];
			b->n[f] = (unsigned short)(b->n[f] + sign);
		}
	if (sign > 0 && s->tech != NONE)
		b->tech = s->tech;
	/* the carriers: a sum and a count per carrier; a bucket keeps at most twice the carriers of one sample (a handover in it) */
	for (i = 0; i < s->nc; i++) {
		for (j = 0; j < b->nc && b->c[j].key != s->c[i].key; j++)
			;
		if (j == b->nc) {
			struct cval *c;

			if (sign < 0 || b->nc >= CARRIERS_MAX * 2 || !(c = realloc(b->c, (size_t)(b->nc + 1) * sizeof(*c))))
				continue;
			b->c = c;
			b->c[j].key = s->c[i].key;
			b->c[j].n = 0;
			b->c[j].sum = 0;
			b->nc++;
		}
		b->c[j].sum += sign * s->c[i].sum;
		b->c[j].n = (unsigned short)(b->c[j].n + sign);
	}
}

void mm_sighist_add(json_object *hist, json_object *mm, long long t)
{
	struct store *st = store_of(hist, true);
	struct pt s, *p;
	char tech[64], cell[64], cell_id[192], ci[32];
	size_t drop;

	if (!st || !sample_of(st, mm, t, &s))
		return;
	tech_of(mm, tech, sizeof(tech));
	if (*tech) {
		if (str(hist, "tech") && strcmp(str(hist, "tech"), tech)) {
			char d[140];

			snprintf(d, sizeof(d), "%s → %s", str(hist, "tech"), tech);
			event(hist, t, "tech", d);
		}
		json_object_object_add(hist, "tech", json_object_new_string(tech));
	}
	cell_of(mm, cell, sizeof(cell), cell_id, sizeof(cell_id), ci, sizeof(ci));
	if (*cell) {
		const char *was = str(hist, "cell_id") ? str(hist, "cell_id") : str(hist, "cell");

		if (was && strcmp(was, cell_id)) {
			char d[200];

			if (str(hist, "cell") && strcmp(str(hist, "cell"), cell))
				snprintf(d, sizeof(d), "%s → %s", str(hist, "cell"), cell);
			else if (*ci && str(hist, "ci") && *str(hist, "ci"))
				snprintf(d, sizeof(d), "%s: ci %s → %s", cell, str(hist, "ci"), ci);
			else
				snprintf(d, sizeof(d), "%s: another cell", cell);
			event(hist, t, "cell", d);
		}
		json_object_object_add(hist, "cell", json_object_new_string(cell));
		json_object_object_add(hist, "cell_id", json_object_new_string(cell_id));
		json_object_object_add(hist, "ci", json_object_new_string(ci));
	}
	/* the same second twice (a read on request right after a background one): the newer counts, in the averages too (audit F05) */
	if (st->raw.len && st->raw.p[st->raw.len - 1].t == t) {
		struct pt *old = &st->raw.p[st->raw.len - 1];

		fold_by(&st->m1, old, 60, M1_MAX, -1);
		fold_by(&st->m5, old, 300, M5_MAX, -1);
		free(old->c);
		st->raw.len--;
	}
	fold_by(&st->m1, &s, 60, M1_MAX, 1);
	fold_by(&st->m5, &s, 300, M5_MAX, 1);
	if (!(p = series_push(&st->raw, RAW_MAX))) {
		free(s.c);
		return;
	}
	*p = s;		/* the carriers move with it */
	for (drop = 0; drop < st->raw.len && st->raw.p[drop].t < t - RAW_S; drop++)
		;
	series_drop(&st->raw, drop);
	if (st->raw.len > RAW_MAX)
		series_drop(&st->raw, st->raw.len - RAW_MAX);
}

static double tenth(double sum, int n)
{
	double avg = sum / n * 10;

	return (double)(long long)(avg + (avg < 0 ? -0.5 : 0.5)) / 10;
}

json_object *mm_sighist_view(json_object *hist, const char *range, long long now)
{
	json_object *o = json_object_new_object(), *points = json_object_new_array(), *events = json_object_new_array(), *src;
	struct store *st = store_of(hist, false);
	struct series *series = NULL;
	long long span, step;
	size_t i;
	bool raw;

	if (range && !strcmp(range, "5m")) {
		span = RAW_S;
		step = 0;
	} else if (range && !strcmp(range, "24h")) {
		span = 86400;
		step = 300;
	} else {
		range = "1h";
		span = 3600;
		step = 60;
	}
	raw = !step;
	if (st)
		series = raw ? &st->raw : step == 60 ? &st->m1 : &st->m5;
	for (i = 0; series && i < series->len; i++) {
		const struct pt *b = &series->p[i];
		json_object *p;
		int f, j;

		if (b->t < now - span || b->t > now)
			continue;
		p = json_object_new_object();
		json_object_object_add(p, "t", json_object_new_int64(b->t));
		for (f = 0; f < NF; f++)
			if (raw ? b->n[f] > 0 : (short)b->n[f] > 0)
				json_object_object_add(p, FIELDS[f], json_object_new_double(raw ? b->v[f] : tenth(b->v[f], b->n[f])));
		if (b->tech != NONE && b->tech < st->ntechs)
			json_object_object_add(p, "tech", json_object_new_string(st->techs[b->tech]));
		if (b->nc) {
			json_object *pc = json_object_new_object();

			for (j = 0; j < b->nc; j++)
				if ((short)b->c[j].n > 0)
					json_object_object_add(pc, st->keys[b->c[j].key],
							       json_object_new_double(raw ? b->c[j].sum : tenth(b->c[j].sum, b->c[j].n)));
			if (json_object_object_length(pc))
				json_object_object_add(p, "carriers", pc);
			else
				json_object_put(pc);
		}
		json_object_array_add(points, p);
	}
	src = get(hist, "events");
	for (i = 0; src && i < json_object_array_length(src); i++) {
		json_object *e = json_object_array_get_idx(src, i);
		long long t = json_object_get_int64(get(e, "t"));

		if (t >= now - span && t <= now)
			json_object_array_add(events, json_object_get(e));
	}
	json_object_object_add(o, "range", json_object_new_string(range));
	json_object_object_add(o, "step_s", json_object_new_int64(step));
	json_object_object_add(o, "points", points);
	json_object_object_add(o, "events", events);
	return o;
}
