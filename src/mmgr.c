#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mmgr.h"
#include "uevent.h"

static json_object *get(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_object_get_ex(o, key, &v) ? v : NULL;
}

/* mmcli prints "--" for "no value" */
static json_object *value(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	const char *s;

	if (!v)
		return NULL;
	if (json_object_is_type(v, json_type_string)) {
		s = json_object_get_string(v);
		if (!*s || !strcmp(s, "--"))
			return NULL;
	}
	return json_object_get(v);
}

static const char *str(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

/* "allowed: 3g, 4g; preferred: 4g" -> {allowed: [3g, 4g], preferred: 4g}; "none" is no value */
static json_object *modes_of(const char *s)
{
	json_object *o, *allowed;
	const char *a, *p;

	if (!s || !(a = strstr(s, "allowed:")))
		return NULL;
	o = json_object_new_object();
	allowed = json_object_new_array();
	a += 8;
	p = strstr(s, "preferred:");
	while (*a && (!p || a < p)) {
		size_t n;

		a += strspn(a, " ,");
		n = strcspn(a, ",;");
		if (p && a + n > p)
			n = (size_t)(p - a);
		while (n && a[n - 1] == ' ')
			n--;
		if (n && !(n == 4 && !strncmp(a, "none", 4)))
			json_object_array_add(allowed, json_object_new_string_len(a, (int)n));
		a += n;
		a += strspn(a, " ,;");
	}
	json_object_object_add(o, "allowed", allowed);
	if (p) {
		size_t n;

		p += 10;
		p += strspn(p, " ");
		n = strcspn(p, " ,;");
		json_object_object_add(o, "preferred", n && strncmp(p, "none", 4) ? json_object_new_string_len(p, (int)n) : NULL);
	}
	return o;
}

static json_object *list_of(json_object *o, const char *key)
{
	json_object *v = get(o, key), *out = json_object_new_array();
	size_t i;

	for (i = 0; v && json_object_is_type(v, json_type_array) && i < json_object_array_length(v); i++) {
		const char *s = json_object_get_string(json_object_array_get_idx(v, i));
		if (s && *s && strcmp(s, "--"))
			json_object_array_add(out, json_object_new_string(s));
	}
	return out;
}

static bool truthy(json_object *o)
{
	const char *s;

	if (!o)
		return false;
	if (json_object_is_type(o, json_type_boolean))
		return json_object_get_boolean(o);
	s = json_object_get_string(o);
	return s && (!strcasecmp(s, "yes") || !strcasecmp(s, "true") || !strcmp(s, "1"));
}

static json_object *number_value(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	const char *s;
	char *end = NULL;
	long long n;

	if (!v)
		return NULL;
	if (json_object_is_type(v, json_type_int))
		return json_object_get(v);
	s = json_object_get_string(v);
	if (!s || !*s || !strcmp(s, "--"))
		return NULL;
	n = strtoll(s, &end, 10);
	return end && !*end ? json_object_new_int64(n) : NULL;
}

static json_object *normalize_ip(json_object *raw)
{
	json_object *o = json_object_new_object();

	if (!raw || !json_object_is_type(raw, json_type_object))
		return o;
	json_object_object_add(o, "method", value(raw, "method"));
	json_object_object_add(o, "address", value(raw, "address"));
	json_object_object_add(o, "prefix", number_value(raw, "prefix"));
	json_object_object_add(o, "gateway", value(raw, "gateway"));
	json_object_object_add(o, "dns", list_of(raw, "dns"));
	json_object_object_add(o, "mtu", number_value(raw, "mtu"));
	return o;
}

static json_object *normalize_bearer(json_object *bearer)
{
	json_object *status = get(bearer, "status"), *stats = get(bearer, "stats");
	json_object *o = json_object_new_object(), *st = json_object_new_object();
	const char *path = str(bearer, "dbus-path");

	json_object_object_add(o, "object", path ? json_object_new_string(path) : NULL);
	json_object_object_add(o, "connected", json_object_new_boolean(truthy(get(status, "connected"))));
	json_object_object_add(o, "suspended", json_object_new_boolean(truthy(get(status, "suspended"))));
	json_object_object_add(o, "interface", value(status, "interface"));
	json_object_object_add(o, "ipv4", normalize_ip(get(bearer, "ipv4-config")));
	json_object_object_add(o, "ipv6", normalize_ip(get(bearer, "ipv6-config")));
	json_object_object_add(st, "start_date", number_value(stats, "start-date"));
	json_object_object_add(st, "duration", number_value(stats, "duration"));
	json_object_object_add(st, "rx_bytes", number_value(stats, "bytes-rx"));
	json_object_object_add(st, "tx_bytes", number_value(stats, "bytes-tx"));
	json_object_object_add(st, "uplink_bps", number_value(stats, "uplink-speed"));
	json_object_object_add(st, "downlink_bps", number_value(stats, "downlink-speed"));
	json_object_object_add(o, "stats", st);
	return o;
}

static bool path_in(json_object *arr, const char *wanted)
{
	size_t i;

	if (!arr || !wanted || !json_object_is_type(arr, json_type_array))
		return false;
	for (i = 0; i < json_object_array_length(arr); i++) {
		const char *s = json_object_get_string(json_object_array_get_idx(arr, i));
		if (s && !strcmp(s, wanted))
			return true;
	}
	return false;
}

/* "-95.00" -> -95.0; "--", empty or junk -> no value */
static bool number(json_object *o, const char *key, double *v)
{
	const char *t = str(o, key);
	char *end;

	if (!t || !*t)
		return false;
	*v = strtod(t, &end);
	return end != t && !*end;
}

/*
 * `mmcli -J -m N --signal-get`: {"modem": {"signal": {"lte": {...}, "5g": {...},
 * "refresh": {"rate": "30"}}}} -> {"rate": 30, "lte": {"rsrp", "rsrq", "rssi", "snr"}, "5g": {...}, ...}
 * with only the numbers MM reported (a modem without extended signal: just the rate).
 */
static json_object *normalize_signal(json_object *sig)
{
	static const char *const techs[] = { "lte", "5g", "umts", "gsm", NULL };
	static const char *const keys[] = { "rsrp", "rsrq", "rssi", "snr", "rscp", "ecio", NULL };
	json_object *o = json_object_new_object();
	double v;
	int t, k;

	/* each level in its physical range: a firmware's -32768 is no reading (bench 2026-10-26, an MBIM modem's 5G) */
	static const double lo[] = { -160, -45, -130, -30, -130, -40 }, hi[] = { -30, 21, 0, 61, 0, 6 };
	for (t = 0; techs[t]; t++) {
		json_object *src = get(sig, techs[t]), *dst = NULL;
		for (k = 0; src && keys[k]; k++) {
			if (!number(src, keys[k], &v) || v <= lo[k] || v >= hi[k])
				continue;
			if (!dst)
				dst = json_object_new_object();
			json_object_object_add(dst, keys[k], json_object_new_double(v));
		}
		/* LTE or 5G without its RSRP is no reading: an LTE-only modem gives «5g: rsrq 0, snr 0» (a field report of
		 * modem-telemetry, 2026-11-01: its 5G showed as present) */
		if (dst && t < 2 && !get(dst, "rsrp")) {
			json_object_put(dst);
			dst = NULL;
		}
		if (dst)
			json_object_object_add(o, techs[t], dst);
	}
	if (number(get(sig, "refresh"), "rate", &v))
		json_object_object_add(o, "rate", json_object_new_int((int)v));
	return o;
}

/* E-UTRA band from the downlink EARFCN (3GPP TS 36.101 table 5.7.3-1) */
int mm_mmgr_lte_band(long earfcn)
{
	static const struct { long lo, hi; int band; } t[] = {
		{ 0, 599, 1 }, { 600, 1199, 2 }, { 1200, 1949, 3 }, { 1950, 2399, 4 }, { 2400, 2649, 5 },
		{ 2750, 3449, 7 }, { 3450, 3799, 8 }, { 3800, 4149, 9 }, { 4150, 4749, 10 }, { 5010, 5179, 12 },
		{ 5180, 5279, 13 }, { 5280, 5379, 14 }, { 5730, 5849, 17 }, { 5850, 5999, 18 }, { 6000, 6149, 19 },
		{ 6150, 6449, 20 }, { 6450, 6599, 21 }, { 8040, 8689, 25 }, { 8690, 9039, 26 }, { 9040, 9209, 27 },
		{ 9210, 9659, 28 }, { 9660, 9769, 29 }, { 9770, 9869, 30 }, { 9870, 9919, 31 }, { 9920, 10359, 32 },
		{ 36000, 36199, 33 }, { 36200, 36349, 34 }, { 37750, 38249, 38 }, { 38250, 38649, 39 },
		{ 38650, 39649, 40 }, { 39650, 41589, 41 }, { 41590, 43589, 42 }, { 43590, 45589, 43 },
		{ 46790, 54539, 46 }, { 55240, 56739, 48 }, { 66436, 67335, 66 }, { 68586, 68935, 71 },
		{ 68936, 68985, 72 }, { 70366, 70545, 87 }, { 70546, 70595, 88 },
	};
	size_t i;

	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (earfcn >= t[i].lo && earfcn <= t[i].hi)
			return t[i].band;
	return 0;
}

/* NR band from the NR-ARFCN (TS 38.104: global raster, then the narrowest common FR1 band) */
int mm_mmgr_nr_band(long arfcn)
{
	static const struct { double lo, hi; int band; } t[] = {	/* narrowest first where bands overlap */
		{ 3300, 3800, 78 }, { 2570, 2620, 38 }, { 2620, 2690, 7 }, { 2496, 2690, 41 }, { 3300, 4200, 77 },
		{ 4400, 5000, 79 }, { 2110, 2170, 1 }, { 1805, 1880, 3 }, { 869, 894, 5 }, { 925, 960, 8 },
		{ 791, 821, 20 }, { 758, 803, 28 }, { 2300, 2400, 40 }, { 1930, 1995, 25 }, { 617, 652, 71 },
	};
	double f;
	size_t i;

	if (arfcn < 0 || arfcn > 2016666)
		return 0;
	f = arfcn < 600000 ? arfcn * 0.005 : 3000 + (arfcn - 600000) * 0.015;
	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (f >= t[i].lo && f <= t[i].hi)
			return t[i].band;
	return 0;
}

/*
 * `mmcli -J -m N --get-cell-info`: {"modem": {"generic": {"cell-info": ["cell type: lte, serving: yes,
 * tac: 1F2E, ci: 0A1B2C3, physical ci: 1CF (hex), earfcn: 1300, rsrp: -91.000000, ..., serving cell type: 1"]}}}
 * (ModemManager 1.24, mm_cell_info_build_string) -> [{type, serving, role, earfcn|nrarfcn, band, pci, ...}]
 */
static json_object *normalize_cells(json_object *list)
{
	static const char *const roles[] = { "serving", "primary", "secondary", "scg-primary", "scg-secondary" };
	json_object *out = json_object_new_array();
	size_t i;

	for (i = 0; list && json_object_is_type(list, json_type_array) && i < json_object_array_length(list) &&
		    json_object_array_length(out) < 32; i++) {
		const char *s = json_object_get_string(json_object_array_get_idx(list, i));
		json_object *c;
		char buf[512], *save = NULL, *part;
		bool serving = false;
		long role = -1, arfcn = -1;
		bool nr = false;

		if (!s || strncmp(s, "cell type: ", 11))
			continue;
		snprintf(buf, sizeof(buf), "%s", s);
		c = json_object_new_object();
		for (part = strtok_r(buf, ",", &save); part; part = strtok_r(NULL, ",", &save)) {
			char *colon = strchr(part, ':'), *key = part, *val;
			char *end = NULL;
			double d;

			while (*key == ' ')
				key++;
			if (!colon)
				continue;
			*colon = '\0';
			val = colon + 1;
			while (*val == ' ')
				val++;
			if (!strcmp(key, "cell type")) {
				json_object_object_add(c, "type", json_object_new_string(val));
				nr = !strcmp(val, "5gnr");
			} else if (!strcmp(key, "serving")) {
				serving = !strcmp(val, "yes");
			} else if (!strcmp(key, "serving cell type")) {
				role = strtol(val, &end, 10);
			} else if (!strcmp(key, "earfcn") || !strcmp(key, "nrarfcn") || !strcmp(key, "uarfcn") || !strcmp(key, "arfcn")) {
				arfcn = strtol(val, &end, 10);
				json_object_object_add(c, key, json_object_new_int64(arfcn));
			} else if (!strcmp(key, "physical ci")) {
				/* ModemManager prints it in hex ("%X", QMI and MBIM alike): «1CF» is PCI 463 */
				long pci = strtol(val, &end, 16);
				char dec[16];

				if (end != val && pci >= 0 && pci <= 1007) {
					snprintf(dec, sizeof(dec), "%ld", pci);
					json_object_object_add(c, "pci", json_object_new_string(dec));
				}
			} else if (!strcmp(key, "tac") || !strcmp(key, "ci") || !strcmp(key, "lac") || !strcmp(key, "operator id")) {
				json_object_object_add(c, !strcmp(key, "operator id") ? "plmn" : key, json_object_new_string(val));
			} else if (!strcmp(key, "rsrp") || !strcmp(key, "rsrq") || !strcmp(key, "sinr") || !strcmp(key, "rscp") ||
				   !strcmp(key, "ecio") || !strcmp(key, "rssi")) {
				d = strtod(val, &end);
				if (end != val)
					json_object_object_add(c, key, json_object_new_double((double)(long)(d * 10) / 10));
			} else if (!strcmp(key, "bandwidth") || !strcmp(key, "timing advance")) {
				long n = strtol(val, &end, 10);
				if (end != val)
					json_object_object_add(c, !strcmp(key, "bandwidth") ? "bandwidth" : "timing_advance", json_object_new_int64(n));
			}
		}
		json_object_object_add(c, "serving", json_object_new_boolean(serving));
		json_object_object_add(c, "role", json_object_new_string(!serving ? "neighbour" :
			role >= 0 && role <= 4 ? roles[role] : "serving"));
		if (arfcn >= 0 && json_object_object_get_ex(c, "earfcn", NULL)) {
			int b = mm_mmgr_lte_band(arfcn);
			if (b)
				json_object_object_add(c, "band", json_object_new_int(b));
		} else if (arfcn >= 0 && nr) {
			int b = mm_mmgr_nr_band(arfcn);
			if (b)
				json_object_object_add(c, "band", json_object_new_int(b));
		}
		json_object_array_add(out, c);
	}
	return out;
}

/* a number inside its sane range, else nothing */
static json_object *ranged(json_object *v, double lo, double hi)
{
	double d;

	if (!v || !(json_object_is_type(v, json_type_double) || json_object_is_type(v, json_type_int)))
		return NULL;
	d = json_object_get_double(v);
	return d >= lo && d <= hi ? json_object_new_double((double)(long)(d * 10) / 10) : NULL;
}

/*
 * The worker's digest of `qmicli -p --nas-get-tx-rx-info=lte|5gnr` (QMI NAS tx-rx info, read through the
 * proxy beside ModemManager): {"txrx": {"ri": "lte", "chains": [{"chain": 0, "tuned": true, "power": -61.5,
 * "rsrp": -93.4, "ecio": -2.5, "phase": 12.0}], "tx": {"traffic": true, "power": 11.5}}} -> {chains, tx}
 */
static json_object *normalize_txrx(json_object *t)
{
	json_object *raw = get(t, "chains"), *out = json_object_new_object(), *chains = json_object_new_array(), *tx = get(t, "tx");
	size_t i;

	for (i = 0; raw && json_object_is_type(raw, json_type_array) && i < json_object_array_length(raw) &&
		    json_object_array_length(chains) < 8; i++) {
		json_object *c = json_object_array_get_idx(raw, i), *n = get(c, "chain"), *o, *v;

		if (!n || !json_object_is_type(n, json_type_int) || json_object_get_int(n) < 0 || json_object_get_int(n) > 7)
			continue;
		o = json_object_new_object();
		json_object_object_add(o, "chain", json_object_new_int(json_object_get_int(n)));
		json_object_object_add(o, "tuned", json_object_new_boolean(truthy(get(c, "tuned"))));
		if ((v = ranged(get(c, "power"), -150, 0)))
			json_object_object_add(o, "power", v);
		if ((v = ranged(get(c, "rsrp"), -160, -20)))
			json_object_object_add(o, "rsrp", v);
		if ((v = ranged(get(c, "ecio"), -40, 5)))
			json_object_object_add(o, "ecio", v);
		if ((v = ranged(get(c, "phase"), -360, 360)))
			json_object_object_add(o, "phase", v);
		json_object_array_add(chains, o);
	}
	json_object_object_add(out, "chains", chains);
	if (tx && json_object_is_type(tx, json_type_object)) {
		json_object *o = json_object_new_object(), *v;

		json_object_object_add(o, "traffic", json_object_new_boolean(truthy(get(tx, "traffic"))));
		if (truthy(get(tx, "traffic")) && (v = ranged(get(tx, "power"), -60, 40)))
			json_object_object_add(o, "power", v);
		json_object_object_add(out, "tx", o);
	}
	return out;
}

/*
 * The worker's digest of `qmicli -p --nas-get-lte-cphy-ca-info`: {"ca": {"cells": [{"role": "primary", "pci": 135,
 * "earfcn": 525, "bandwidth_mhz": 15}, {"role": "secondary", "pci": 389, "earfcn": 1301, "bandwidth_mhz": 20,
 * "state": "deactivated", "index": 1}]}} -> {cells: [... with the band from the EARFCN]}. The modem's own band field
 * is not used: the T77W968 reports it one too high.
 */
static json_object *normalize_ca(json_object *ca)
{
	json_object *raw = get(ca, "cells"), *out = json_object_new_object(), *cells = json_object_new_array();
	static const char *const states[] = { "activated", "deactivated", "deconfigured", NULL };
	size_t i;

	for (i = 0; raw && json_object_is_type(raw, json_type_array) && i < json_object_array_length(raw) &&
		    json_object_array_length(cells) < 8; i++) {
		json_object *c = json_object_array_get_idx(raw, i), *o, *v;
		const char *role = str(c, "role"), *st = str(c, "state");
		long earfcn;
		int k;

		if (!role || (strcmp(role, "primary") && strcmp(role, "secondary")))
			continue;
		o = json_object_new_object();
		json_object_object_add(o, "role", json_object_new_string(role));
		if ((v = get(c, "pci")) && json_object_is_type(v, json_type_int) && json_object_get_int(v) >= 0 && json_object_get_int(v) < 504)
			json_object_object_add(o, "pci", json_object_new_int(json_object_get_int(v)));
		if ((v = get(c, "earfcn")) && json_object_is_type(v, json_type_int) && (earfcn = json_object_get_int64(v)) >= 0 && earfcn < 262144) {
			int b = mm_mmgr_lte_band(earfcn);
			json_object_object_add(o, "earfcn", json_object_new_int64(earfcn));
			if (b)
				json_object_object_add(o, "band", json_object_new_int(b));
		}
		if ((v = get(c, "bandwidth_mhz")) && (json_object_is_type(v, json_type_int) || json_object_is_type(v, json_type_double)) &&
		    json_object_get_double(v) > 0 && json_object_get_double(v) <= 100)
			json_object_object_add(o, "bandwidth_mhz", json_object_new_double(json_object_get_double(v)));
		if ((v = get(c, "index")) && json_object_is_type(v, json_type_int) && json_object_get_int(v) >= 0 && json_object_get_int(v) < 16)
			json_object_object_add(o, "index", json_object_new_int(json_object_get_int(v)));
		for (k = 0; st && states[k]; k++)
			if (!strcmp(st, states[k]))
				json_object_object_add(o, "state", json_object_new_string(states[k]));
		json_object_array_add(cells, o);
	}
	json_object_object_add(out, "cells", cells);
	return out;
}

/* `mmcli -J -m N --location-get`: only the network part (3GPP); GPS and other sources are never read */
static json_object *normalize_location(json_object *loc)
{
	static const char *const keys[] = { "mcc", "mnc", "lac", "tac", "cid", NULL };
	json_object *g = get(loc, "3gpp"), *o = json_object_new_object();
	int k;

	for (k = 0; keys[k]; k++) {
		const char *v = str(g, keys[k]);
		if (v && *v && strcmp(v, "--") && strcmp(v, "0") && strcmp(v, "000000") && strcmp(v, "0000"))
			json_object_object_add(o, keys[k], json_object_new_string(v));
	}
	return o;
}

/* the firmware as one string: a T77W968 prints "T77W968.F1.0.0.5.2.GC.013\n050  1  [May 31 2021 ...]",
 * where "050" is the tail of the version (AT^VERSION? writes it with a dot) */
static json_object *firmware_of(const char *rev)
{
	char first[128], tail[16] = "";
	const char *nl;
	size_t n = 0;

	if (!rev || !*rev || !strcmp(rev, "--"))
		return NULL;
	nl = strchr(rev, '\n');
	snprintf(first, sizeof(first), "%.*s", nl ? (int)(nl - rev) : (int)strlen(rev), rev);
	if (nl) {
		const char *p = nl + 1;
		while (*p == ' ')
			p++;
		while (isdigit((unsigned char)p[n]) && n < sizeof(tail) - 1)
			n++;
		if (n && (p[n] == ' ' || p[n] == '\0' || p[n] == '\n'))
			snprintf(tail, sizeof(tail), ".%.*s", (int)n, p);
	}
	for (n = strlen(first); n && isspace((unsigned char)first[n - 1]); n--)
		first[n - 1] = '\0';
	if (!*first)
		return NULL;
	{
		char out[160];
		snprintf(out, sizeof(out), "%s%s", first, tail);
		return json_object_new_string(out);
	}
}

/*
 * "unlock-retries": ["sim-pin (3)", "sim-puk (10)", ...] -> {"sim-pin": 3, "sim-puk": 10}: the page warns
 * before the last PIN try, which turns the SIM into a PUK lock. Junk and "--" are left out.
 */
static json_object *retries_of(json_object *list)
{
	json_object *o = json_object_new_object();
	size_t i;

	for (i = 0; list && json_object_is_type(list, json_type_array) && i < json_object_array_length(list); i++) {
		const char *s = json_object_get_string(json_object_array_get_idx(list, i)), *p;
		char key[16];
		size_t n;
		char *end = NULL;
		long v;

		if (!s || !(p = strstr(s, " (")))
			continue;
		n = (size_t)(p - s);
		if (!n || n >= sizeof(key))
			continue;
		v = strtol(p + 2, &end, 10);
		if (end == p + 2 || *end != ')' || v < 0 || v > 999)
			continue;
		memcpy(key, s, n);
		key[n] = '\0';
		json_object_object_add(o, key, json_object_new_int((int)v));
	}
	return o;
}

static json_object *normalize(json_object *modem)
{
	json_object *g = get(modem, "generic"), *gpp = get(modem, "3gpp"), *o = json_object_new_object();
	json_object *ports = json_object_new_array(), *types = json_object_new_object(), *raw, *sq;
	const char *path = str(modem, "dbus-path"), *dev = str(g, "device"), *slash;
	char name[64];
	size_t i;

	json_object_object_add(o, "object", path ? json_object_new_string(path) : NULL);
	slash = path ? strrchr(path, '/') : NULL;
	json_object_object_add(o, "index", slash && isdigit((unsigned char)slash[1]) ? json_object_new_int(atoi(slash + 1)) : NULL);
	json_object_object_add(o, "device", value(g, "device"));
	/* ModemManager 1.24 finds a modem by a sysfs path through its Physdev; one it left without («--»: a ZTE MF100 on
	 * a Nokia, 2026-11-04) is found by no path, so OpenWrt's proto modemmanager said «Device not managed by
	 * ModemManager» for good: netifd is pointed at its D-Bus object instead. An older ModemManager has no physdev
	 * at all and finds a modem by its device */
	if (get(g, "physdev") && !value(g, "physdev") && path)
		json_object_object_add(o, "lookup", json_object_new_string(path));
	json_object_object_add(o, "usb_name", dev && mm_uevent_usb_name(dev, name, sizeof(name)) ? json_object_new_string(name) : NULL);

	/* "ttyUSB0 (at)" -> name ttyUSB0, type at */
	raw = get(g, "ports");
	for (i = 0; raw && json_object_is_type(raw, json_type_array) && i < json_object_array_length(raw); i++) {
		const char *p = json_object_get_string(json_object_array_get_idx(raw, i));
		const char *sp = p ? strchr(p, ' ') : NULL;
		char port[64], type[32] = "";

		if (!p || !*p || !strcmp(p, "--"))
			continue;
		snprintf(port, sizeof(port), "%.*s", sp ? (int)(sp - p) : (int)strlen(p), p);
		if (sp && sp[1] == '(')
			snprintf(type, sizeof(type), "%.*s", (int)strcspn(sp + 2, ")"), sp + 2);
		json_object_array_add(ports, json_object_new_string(port));
		if (*type)
			json_object_object_add(types, port, json_object_new_string(type));
	}
	json_object_object_add(o, "ports", ports);
	json_object_object_add(o, "port_types", types);
	json_object_object_add(o, "primary_port", value(g, "primary-port"));
	json_object_object_add(o, "drivers", list_of(g, "drivers"));
	json_object_object_add(o, "plugin", value(g, "plugin"));
	json_object_object_add(o, "state", value(g, "state"));
	json_object_object_add(o, "failed_reason", value(g, "state-failed-reason"));
	json_object_object_add(o, "power_state", value(g, "power-state"));
	json_object_object_add(o, "unlock_required", value(g, "unlock-required"));
	json_object_object_add(o, "unlock_retries", retries_of(get(g, "unlock-retries")));
	json_object_object_add(o, "manufacturer", value(g, "manufacturer"));
	json_object_object_add(o, "model", value(g, "model"));
	json_object_object_add(o, "revision", value(g, "revision"));
	json_object_object_add(o, "firmware", firmware_of(str(g, "revision")));
	{
		/* the IMEI is shown to the operator (decision 2026-10-09); diagnostic bundles still redact it (diag.c) */
		const char *imei = str(gpp, "imei");
		size_t n = imei ? strlen(imei) : 0;
		bool ok = n >= 8 && strcmp(imei, "--");
		json_object_object_add(o, "imei", ok ? json_object_new_string(imei) : NULL);
		json_object_object_add(o, "imei_tail", ok ? json_object_new_string(imei + n - 4) : NULL);
	}
	json_object_object_add(o, "access_tech", list_of(g, "access-technologies"));
	/* "eutran-3", "ngran-78": the bands the modem is allowed to use; the page checks the band it computed against them */
	json_object_object_add(o, "current_bands", list_of(g, "current-bands"));
	/* the radio settings ModemManager reports: what the modem may use now and what it could (spec 25.9.12) */
	json_object_object_add(o, "supported_bands", list_of(g, "supported-bands"));
	json_object_object_add(o, "current_modes", modes_of(str(g, "current-modes")));
	{
		json_object *sm = get(g, "supported-modes"), *list = json_object_new_array();
		size_t k;

		for (k = 0; sm && json_object_is_type(sm, json_type_array) && k < json_object_array_length(sm); k++) {
			json_object *m = modes_of(json_object_get_string(json_object_array_get_idx(sm, k)));
			if (m)
				json_object_array_add(list, m);
		}
		json_object_object_add(o, "supported_modes", list);
	}
	sq = get(get(g, "signal-quality"), "value");
	json_object_object_add(o, "signal_quality", sq && json_object_get_string(sq) && isdigit((unsigned char)*json_object_get_string(sq))
				? json_object_new_int(atoi(json_object_get_string(sq))) : NULL);
	json_object_object_add(o, "operator", value(gpp, "operator-name"));
	json_object_object_add(o, "operator_code", value(gpp, "operator-code"));
	json_object_object_add(o, "registration", value(gpp, "registration-state"));
	json_object_object_add(o, "sim", value(g, "sim"));
	/* SIM slots (ModemManager 1.20+): «--» is an empty one; switched with --set-primary-sim-slot (field practice and the
	 * owner, 2026-10-27: a module with a physical slot and an eUICC) */
	{
		json_object *slots = get(g, "sim-slots"), *list;
		const char *prim = str(g, "primary-sim-slot");

		if (slots && json_object_is_type(slots, json_type_array) && json_object_array_length(slots) > 1) {
			list = json_object_new_array();
			for (size_t k = 0; k < json_object_array_length(slots); k++) {
				const char *p = json_object_get_string(json_object_array_get_idx(slots, k));

				json_object_array_add(list, json_object_new_boolean(p && *p == '/'));
			}
			json_object_object_add(o, "sim_slots", list);
			if (prim && isdigit((unsigned char)*prim))
				json_object_object_add(o, "primary_sim_slot", json_object_new_int(atoi(prim)));
		}
	}
	json_object_object_add(o, "own_numbers", list_of(g, "own-numbers"));
	json_object_object_add(o, "bearer_paths", list_of(g, "bearers"));
	return o;
}

json_object *mm_mmgr_parse(const char *buf, size_t len)
{
	json_object *out = json_object_new_array(), *last = NULL;
	json_tokener *tok = json_tokener_new();
	size_t off = 0;

	while (tok && off < len) {
		json_object *o, *modem, *bearer;
		enum json_tokener_error err;

		while (off < len && isspace((unsigned char)buf[off]))
			off++;
		if (off >= len)
			break;
		json_tokener_reset(tok);
		o = json_tokener_parse_ex(tok, buf + off, (int)(len - off));
		err = json_tokener_get_error(tok);
		if (err != json_tokener_success) {
			/* a torn or foreign chunk: resume after the next newline */
			const char *nl = memchr(buf + off, '\n', len - off);
			json_object_put(o);
			if (!nl)
				break;
			off = (size_t)(nl - buf) + 1;
			continue;
		}
		off += json_tokener_get_parse_end(tok);
		modem = get(o, "modem");
		bearer = get(o, "bearer");
		if (last && get(o, "ca") && json_object_is_type(get(o, "ca"), json_type_object)) {
			/* carrier aggregation of the modem just read */
			json_object_object_add(last, "ca", normalize_ca(get(o, "ca")));
			json_object_put(o);
			continue;
		}
		if (last && (get(o, "txrx") || str(o, "txrx-why"))) {
			/* antennas of the modem just read: one digest per radio, or why there is none */
			json_object *ant = get(last, "antennas");
			const char *ri = str(get(o, "txrx"), "ri"), *why = str(o, "txrx-why");

			if (!ant) {
				ant = json_object_new_object();
				json_object_object_add(last, "antennas", ant);
			}
			if (why && (!strcmp(why, "no-qmicli") || !strcmp(why, "no-control-port") || !strcmp(why, "failed"))) {
				json_object_object_add(ant, "why", json_object_new_string(why));
				json_object_object_add(ant, "why_at", json_object_new_int64((long long)time(NULL)));
			}
			else if (ri && (!strcmp(ri, "lte") || !strcmp(ri, "5gnr"))) {
				/* a reading clears an old reason (bench 2026-10-19: «install qmi-utils» stayed after it was installed) */
				json_object_object_del(ant, "why");
				json_object_object_add(ant, !strcmp(ri, "lte") ? "lte" : "nr", normalize_txrx(get(o, "txrx")));
			}
			json_object_put(o);
			continue;
		}
		if (last && get(o, "qsig")) {
			/* 5G NR levels from QMI (inside MBIM too): they stand in where ModemManager's read gave none */
			json_object *sig = get(last, "signal");

			if (!sig) {
				sig = json_object_new_object();
				json_object_object_add(last, "signal", sig);
			}
			if (!get(get(sig, "5g"), "rsrp")) {
				json_object *w = json_object_new_object(), *n;

				json_object_object_add(w, "5g", json_object_get(get(o, "qsig")));
				n = normalize_signal(w);
				if (get(n, "5g"))
					json_object_object_add(sig, "5g", json_object_get(get(n, "5g")));
				json_object_put(n);
				json_object_put(w);
			}
			json_object_put(o);
			continue;
		}
		if (get(o, "sim") && json_object_is_type(get(o, "sim"), json_type_object)) {
			/* mmcli -J -i N of the modem just read: only its own SIM */
			json_object *sim = get(o, "sim"), *p = get(sim, "properties"), *n;
			const char *path = str(sim, "dbus-path");

			if (last && path && str(last, "sim") && !strcmp(path, str(last, "sim"))) {
				n = json_object_new_object();
				json_object_object_add(n, "operator_code", value(p, "operator-code"));
				json_object_object_add(n, "operator_name", value(p, "operator-name"));
				json_object_object_add(n, "gid1", value(p, "gid1"));
				json_object_object_add(n, "type", value(p, "sim-type"));	/* physical or esim (MM 1.20+) */
				/* identifiers: served to the operator (2026-10-09), never logged or kept on flash */
				json_object_object_add(n, "imsi", value(p, "imsi"));
				{
					/* some modems pad the ICCID with F (BCD filler): shown without it */
					const char *ic = str(p, "iccid");
					size_t l = ic ? strlen(ic) : 0;
					while (l && (ic[l - 1] == 'F' || ic[l - 1] == 'f'))
						l--;
					json_object_object_add(n, "iccid", ic && l && strcmp(ic, "--") ?
							       json_object_new_string_len(ic, (int)l) : NULL);
				}
				json_object_object_add(last, "sim_card", n);
			}
		} else if (modem && json_object_is_type(modem, json_type_object) && get(modem, "location") && !get(modem, "generic")) {
			/* --location-get of the modem just read */
			if (last)
				json_object_object_add(last, "location", normalize_location(get(modem, "location")));
		} else if (modem && json_object_is_type(modem, json_type_object) && get(get(modem, "generic"), "cell-info") &&
			   !get(get(modem, "generic"), "device")) {
			/* --get-cell-info of the modem just read */
			if (last)
				json_object_object_add(last, "cells", normalize_cells(get(get(modem, "generic"), "cell-info")));
		} else if (modem && json_object_is_type(modem, json_type_object) && get(modem, "signal") && !get(modem, "generic")) {
			/* --signal-get of the modem just read */
			if (last)
				json_object_object_add(last, "signal", normalize_signal(get(modem, "signal")));
		} else if (modem && json_object_is_type(modem, json_type_object)) {
			last = normalize(modem);
			json_object_object_add(last, "bearers", json_object_new_array());
			json_object_array_add(out, last);
		} else if (last && bearer && json_object_is_type(bearer, json_type_object)) {
			const char *path = str(bearer, "dbus-path");
			json_object *paths = get(last, "bearer_paths");
			if (path_in(paths, path)) {
				json_object *b = normalize_bearer(bearer);
				json_object_array_add(get(last, "bearers"), json_object_get(b));
				if (json_object_get_boolean(get(b, "connected")) && !get(last, "bearer"))
					json_object_object_add(last, "bearer", json_object_get(b));
				json_object_put(b);
			}
		}
		json_object_put(o);
	}
	json_tokener_free(tok);
	return out;
}

static const char *device_of(json_object *rec)
{
	return str(rec, "device_id");
}

json_object *mm_mmgr_map(json_object *modems, const struct mm_registry *reg)
{
	json_object *res = json_object_new_object(), *devs = json_object_new_object();
	json_object *unmapped = json_object_new_array(), *conflicts = json_object_new_array();
	json_object *claims = json_object_new_object();	/* device_id -> number of modems */
	size_t i, n = modems ? json_object_array_length(modems) : 0;
	const char **target = calloc(n ? n : 1, sizeof(*target));
	const char **mapping = calloc(n ? n : 1, sizeof(*mapping));

	for (i = 0; i < n; i++) {
		json_object *m = json_object_array_get_idx(modems, i), *ports = get(m, "ports"), *rec = NULL;
		const char *by_path = NULL, *by_port = NULL, *usb = str(m, "usb_name");
		bool clash = false, overlap = false;
		size_t k;

		if (usb) {
			char att[72];
			snprintf(att, sizeof(att), "usb:%s", usb);
			rec = mm_registry_by_attachment(reg, att);
			if (rec && json_object_get_boolean(get(rec, "present")))
				by_path = device_of(rec);
		}
		for (k = 0; ports && k < json_object_array_length(ports); k++) {
			json_object *owner = mm_registry_lookup_node(reg, json_object_get_string(json_object_array_get_idx(ports, k)));
			const char *id = owner ? device_of(owner) : NULL;

			if (!id)
				continue;
			if (by_path && strcmp(id, by_path))
				clash = true;		/* our port, another device */
			else if (by_path)
				overlap = true;
			else if (by_port && strcmp(id, by_port))
				clash = true;		/* ports spread over two devices */
			else
				by_port = id;
		}
		if (clash) {
			json_object *c = json_object_get(m);
			json_object_object_add(c, "reason", json_object_new_string("ports belong to another device"));
			json_object_array_add(conflicts, c);
			continue;
		}
		target[i] = by_path ? by_path : by_port;
		mapping[i] = by_path ? (overlap || !json_object_array_length(ports) ? "exact" : "path-only") : "ports-only";
		if (!target[i]) {
			json_object_array_add(unmapped, json_object_get(m));
			continue;
		}
		json_object_object_add(claims, target[i], json_object_new_int(json_object_get_int(get(claims, target[i])) + 1));
	}
	for (i = 0; i < n; i++) {
		json_object *m = json_object_array_get_idx(modems, i), *copy;

		if (!target[i])
			continue;
		copy = json_object_new_object();
		json_object_object_foreach(m, key, val)
			json_object_object_add(copy, key, json_object_get(val));
		if (json_object_get_int(get(claims, target[i])) > 1) {
			/* two ModemManager objects for one physical device */
			json_object_object_add(copy, "reason", json_object_new_string("several ModemManager objects claim this device"));
			json_object_object_add(copy, "device_id", json_object_new_string(target[i]));
			json_object_array_add(conflicts, copy);
			continue;
		}
		json_object_object_add(copy, "mapping", json_object_new_string(mapping[i]));
		json_object_object_add(devs, target[i], copy);
	}
	free(target);
	free(mapping);
	json_object_put(claims);
	json_object_object_add(res, "devices", devs);
	json_object_object_add(res, "unmapped", unmapped);
	json_object_object_add(res, "conflicts", conflicts);
	return res;
}

static void add_change(json_object *events, const char *id, const char *kind, const char *detail)
{
	json_object *e = json_object_new_object();

	json_object_object_add(e, "device_id", json_object_new_string(id));
	json_object_object_add(e, "kind", json_object_new_string(kind));
	if (detail)
		json_object_object_add(e, "detail", json_object_new_string(detail));
	json_object_array_add(events, e);
}

json_object *mm_mmgr_diff(json_object *old_devices, json_object *new_devices)
{
	json_object *events = json_object_new_array();
	char detail[160];

	if (old_devices) {
		json_object_object_foreach(old_devices, id, om) {
			json_object *nm = get(new_devices, id);
			const char *op = str(om, "object"), *np = str(nm, "object");

			if (!nm || !np || !op || strcmp(op, np))
				add_change(events, id, "mm-remove", op);
		}
	}
	if (new_devices) {
		json_object_object_foreach(new_devices, id, nm) {
			json_object *om = get(old_devices, id);
			const char *op = str(om, "object"), *np = str(nm, "object");
			const char *os = str(om, "state"), *ns = str(nm, "state");

			if (!om || !op || !np || strcmp(op, np)) {
				add_change(events, id, "mm-add", np);
			} else if ((os || ns) && (!os || !ns || strcmp(os, ns))) {
				snprintf(detail, sizeof(detail), "%s -> %s", os ? os : "?", ns ? ns : "?");
				add_change(events, id, "mm-state", detail);
			}
		}
	}
	return events;
}

json_object *mm_mmgr_health(json_object *modem)
{
	const char *unlock = str(modem, "unlock_required"), *power = str(modem, "power_state");
	json_object *o, *ev;
	char line[128];
	bool secondary_sim_lock = unlock && (!strcmp(unlock, "sim-pin2") || !strcmp(unlock, "sim-puk2"));

	/*
	 * PIN2/PUK2 protects secondary SIM facilities (FDN etc.) and does not by
	 * itself block registration or a data bearer. ModemManager may report it
	 * even for an already connected modem, so it must not become a session-
	 * blocking configuration finding.
	 */
	if (unlock && strcmp(unlock, "none") && !secondary_sim_lock) {
		snprintf(line, sizeof(line), "SIM or network lock: ModemManager requires %s", unlock);
	} else if (power && (!strcmp(power, "low") || !strcmp(power, "off"))) {
		snprintf(line, sizeof(line), "radio is off: ModemManager power state %s", power);
	} else {
		return NULL;
	}
	o = json_object_new_object();
	ev = json_object_new_array();
	json_object_object_add(o, "state", json_object_new_string("locked"));
	json_object_array_add(ev, json_object_new_string(line));
	json_object_object_add(o, "evidence", ev);
	return o;
}
