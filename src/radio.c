// SPDX-License-Identifier: GPL-3.0-only
/*
 * Radio settings from QModem and the AT dialects (spec 25.9.12, stage 1b):
 * network mode and bands in the shape ModemManager's report has, so the
 * page shows one block whoever drives the modem. Formats as the vendors'
 * AT references and QModem's modem_ctrl.sh print them; nothing here
 * changes a setting.
 */
#define _GNU_SOURCE	/* strcasestr on musl */
#include "radio.h"
#include "atport.h"
#include "mmgr.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *const quectel_cmds[] = {
	"AT+QNWPREFCFG=\"mode_pref\"", "AT+QNWPREFCFG=\"lte_band\"", "AT+QNWPREFCFG=\"nsa_nr5g_band\"",
	"AT+QNWPREFCFG=\"nr5g_band\"", "AT+QCFG=\"nwscanmode\"", "AT+QCFG=\"band\"", NULL };
/* the reading, then what the firmware accepts, then the cell lock (an older firmware answers ERROR: no lock) */
static const char *const ep06_cmds[] = {
	"AT+QCFG=\"nwscanmode\"", "AT+QCFG=\"band\"",
	"AT+QNWLOCK=\"common/4g\"", "AT+QNWLOCK=\"common/lte\"", NULL };
static const char *const gtact_cmds[] = { "AT+GTACT?", "AT+GTACT=?", "AT+EMMCHLCK?", NULL };
static const char *const xact_cmds[] = { "AT+XACT?", NULL };
/* ^SYSCFG first: the older sticks (E171, E1550, E173) know only it, and one that does not answer ^SYSCFGEX
 * ends the exchange (field 2026-10-17, an E171: no answer to AT^SYSCFGEX? within 3 s) */
static const char *const huawei_cmds[] = { "AT^SYSCFG?", "AT^SYSCFGEX?", NULL };
static const char *const sdx55_cmds[] = { "AT^SLMODE?", "AT^BAND_PREF?", "AT^LTE_LOCK?", "AT^NR5G_LOCK?", NULL };
static const char *const t77_cmds[] = { "AT^SLMODE?", "AT^SLBAND?", "AT^SLBAND=?", NULL };
/* the families ModemManager drives when it is there (the owner, 2026-11-13: «на роутерах с ограниченной оперативной памятью
 * где ММ может быть лишним»): read only, on the engine's own AT port. SIMCom: the preferred RAT and the band masks;
 * Telit: the 27.007 RAT selection; Sierra: !SELRAT (its name after the code) */
static const char *const simcom_cmds[] = { "AT+CNMP?", "AT+CNBP?", NULL };
static const char *const telit_cmds[] = { "AT+WS46?", NULL };
static const char *const sierra_cmds[] = { "AT!SELRAT?", NULL };

const char *const *mm_radio_commands(const char *dialect)
{
	if (!dialect)
		return NULL;
	if (!strcmp(dialect, "quectel-ep06"))
		return ep06_cmds;
	if (!strcmp(dialect, "quectel"))
		return quectel_cmds;
	if (!strcmp(dialect, "fibocom-mtk"))
		return gtact_cmds;
	if (!strcmp(dialect, "xmm"))
		return xact_cmds;
	if (!strcmp(dialect, "huawei"))
		return huawei_cmds;
	if (!strcmp(dialect, "foxconn-sdx"))
		return sdx55_cmds;
	if (!strcmp(dialect, "foxconn-t77"))
		return t77_cmds;
	if (!strcmp(dialect, "simcom"))
		return simcom_cmds;
	if (!strcmp(dialect, "telit"))
		return telit_cmds;
	if (!strcmp(dialect, "sierra"))
		return sierra_cmds;
	return NULL;
}

/* ---- shared pieces ---- */

static void add_band(json_object *arr, const char *kind, long n)
{
	char name[32];
	size_t i;

	if (n <= 0 || n > 999)
		return;
	snprintf(name, sizeof(name), "%s-%ld", kind, n);
	for (i = 0; i < json_object_array_length(arr); i++)
		if (!strcmp(json_object_get_string(json_object_array_get_idx(arr, i)), name))
			return;
	json_object_array_add(arr, json_object_new_string(name));
}

static void add_mode(json_object *arr, const char *m)
{
	size_t i;

	for (i = 0; i < json_object_array_length(arr); i++)
		if (!strcmp(json_object_get_string(json_object_array_get_idx(arr, i)), m))
			return;
	json_object_array_add(arr, json_object_new_string(m));
}

/* the text after "<prefix>" on the line that carries it, up to the line end, in buf */
static bool line_after(const char *text, const char *prefix, char *buf, size_t size)
{
	const char *p = text ? strstr(text, prefix) : NULL;
	size_t n;

	if (!p)
		return false;
	p += strlen(prefix);
	p += strspn(p, " ");
	n = strcspn(p, "\r\n");
	if (n >= size)
		n = size - 1;
	memcpy(buf, p, n);
	buf[n] = '\0';
	return true;
}

/* bits of a hex mask of any length, bit 0 = band 1 */
static int mask_bands(const char *hex, json_object *arr, const char *kind)
{
	size_t len;
	int bit = 0, set = 0;

	hex += strspn(hex, " \"");
	if (!strncmp(hex, "0x", 2) || !strncmp(hex, "0X", 2))
		hex += 2;
	len = strspn(hex, "0123456789abcdefABCDEF");
	while (len--) {
		int v = isdigit((unsigned char)hex[len]) ? hex[len] - '0' : (tolower((unsigned char)hex[len]) - 'a' + 10), k;

		for (k = 0; k < 4; k++, bit++)
			if (v & (1 << k)) {
				add_band(arr, kind, bit + 1);
				set++;
			}
	}
	return set;
}

static json_object *block(json_object *allowed, const char *preferred, json_object *bands, bool any)
{
	json_object *o = json_object_new_object(), *m = NULL;

	if (allowed && json_object_array_length(allowed)) {
		m = json_object_new_object();
		json_object_object_add(m, "allowed", json_object_get(allowed));
		json_object_object_add(m, "preferred", preferred ? json_object_new_string(preferred) : NULL);
	}
	json_object_object_add(o, "modes", m);
	json_object_object_add(o, "bands", json_object_get(bands));
	json_object_object_add(o, "bands_any", json_object_new_boolean(any));
	return o;
}

/* EP06: read QNWLOCK replies strictly. Restore the complete multi-cell snapshot. */
static json_object *cell_lock(const char *rat, long arfcn, long pci);
static bool ep06_lock_read(const char *raw, const char *form, char *save, size_t cap, json_object **held, int *count)
{
	char prefix[64], line[512], *cur, *end;
	long v[17];
	int n = 0, i;
	bool multi = !strcmp(form, "common/4g");
	*held = NULL;
	*count = 0;
	snprintf(prefix, sizeof(prefix), "+QNWLOCK: \"%s\",", form);
	if (!line_after(raw, prefix, line, sizeof(line)) || !*line)
		return false;
	for (cur = line; cur && *cur; cur = end) {
		char *next = strchr(cur, ','), *stop;
		long value;
		if (n >= 17)
			return false;
		if (next)
			*next = '\0';
		cur += strspn(cur, " \t");
		value = strtol(cur, &stop, 10);
		while (*stop == ' ' || *stop == '\t')
			stop++;
		if (stop == cur || *stop || value < 0 || value > 3279165)
			return false;
		v[n++] = value;
		end = next ? next + 1 : NULL;
		if (next && !*end)
			return false;
	}
	if (!n)
		return false;
	if (multi) {
		if (v[0] > 8 || n != 1 + 2 * v[0])
			return false;
		for (i = 0; i < v[0]; i++)
			if (v[1 + 2 * i] > 262143 || v[2 + 2 * i] > 503)
				return false;
		*count = (int)v[0];
		if (*count)
			*held = cell_lock("lte", v[1], v[2]);
	} else {
		if (v[0] > 2 || (v[0] == 0 && n != 1 && n != 4) ||
		    (v[0] != 0 && n != 3 && n != 4))
			return false;
		if (n >= 3 && (v[1] > 262143 || v[2] > 503))
			return false;
		if (n == 4 && v[3] != 0)
			return false; /* still pending; never write during a transition */
		*count = v[0] ? 1 : 0;
		if (*count)
			*held = cell_lock("lte", v[1], v[0] == 2 ? v[2] : -1);
	}
	save[0] = '\0';
	for (i = 0; i < n; i++) {
		size_t len = strlen(save);
		if (len + 16 >= cap) {
			json_object_put(*held);
			*held = NULL;
			return false;
		}
		snprintf(save + len, cap - len, "%s%ld", i ? "," : "", v[i]);
	}
	if (*held && *count > 1)
		json_object_object_add(*held, "count", json_object_new_int(*count));
	return true;
}

/* ---- Quectel: QNWPREFCFG (5G generation), QCFG nwscanmode / band (LTE generation) ---- */

static json_object *quectel(json_object *r, bool ep06)
{
	json_object *allowed = json_object_new_array(), *bands = json_object_new_array(), *out = NULL, *raw = json_object_new_object();
	char buf[512];
	bool seen = false, any = false;
	static const struct { const char *cmd, *kind; } groups[] = {
		{ "AT+QNWPREFCFG=\"lte_band\"", "eutran" }, { "AT+QNWPREFCFG=\"nsa_nr5g_band\"", "ngran" },
		{ "AT+QNWPREFCFG=\"nr5g_band\"", "ngran" } };
	size_t g;

	if (line_after(json_object_get_string(json_object_object_get(r, "AT+QNWPREFCFG=\"mode_pref\"")),
		       "\"mode_pref\",", buf, sizeof(buf))) {
		char *save = NULL, *tok;

		seen = true;
		json_object_object_add(raw, "mode_pref", json_object_new_string(buf));
		for (tok = strtok_r(buf, ":", &save); tok; tok = strtok_r(NULL, ":", &save)) {
			if (!strcmp(tok, "AUTO"))
				add_mode(allowed, "any");
			else if (!strcmp(tok, "GSM"))
				add_mode(allowed, "2g");
			else if (!strcmp(tok, "WCDMA"))
				add_mode(allowed, "3g");
			else if (!strcmp(tok, "LTE"))
				add_mode(allowed, "4g");
			else if (!strcmp(tok, "NR5G"))
				add_mode(allowed, "5g");
		}
	}
	for (g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
		const char *key = strchr(groups[g].cmd, '"');
		char want[40];

		snprintf(want, sizeof(want), "%s,", key);
		if (line_after(json_object_get_string(json_object_object_get(r, groups[g].cmd)), want, buf, sizeof(buf))) {
			char *save = NULL, *tok, group[24];

			seen = true;
			snprintf(group, sizeof(group), "%.*s", (int)strlen(key) - 2, key + 1);
			json_object_object_add(raw, group, json_object_new_string(buf));
			for (tok = strtok_r(buf, ":", &save); tok; tok = strtok_r(NULL, ":", &save))
				add_band(bands, groups[g].kind, strtol(tok, NULL, 10));
		}
	}
	if (!seen) {
		/* the LTE generation (EP06, EM12, EC25) answers QCFG instead */
		if (line_after(json_object_get_string(json_object_object_get(r, "AT+QCFG=\"nwscanmode\"")),
			       "\"nwscanmode\",", buf, sizeof(buf))) {
			static const char *const scan[] = { "any", "2g", "3g", "4g" };
			long v = strtol(buf, NULL, 10);

			seen = true;
			if (v >= 0 && v <= 3)
				add_mode(allowed, scan[v]);
		}
		if (line_after(json_object_get_string(json_object_object_get(r, "AT+QCFG=\"band\"")), "\"band\",", buf, sizeof(buf))) {
			char *lte = strchr(buf, ',');

			seen = true;
			if (lte)
				any = mask_bands(lte + 1, bands, "eutran") >= 60;
		}
	}
	if (ep06) {
		const char *four = json_object_get_string(json_object_object_get(r, "AT+QNWLOCK=\"common/4g\""));
		const char *lte = json_object_get_string(json_object_object_get(r, "AT+QNWLOCK=\"common/lte\""));
		char save4[256], savelte[256];
		json_object *held4 = NULL, *heldlte = NULL, *held = NULL;
		int n4 = 0, nlte = 0;
		bool ok4 = ep06_lock_read(four, "common/4g", save4, sizeof(save4), &held4, &n4);
		bool oklte = ep06_lock_read(lte, "common/lte", savelte, sizeof(savelte), &heldlte, &nlte);
		const char *form = NULL, *restore = NULL;

		/* Prefer an already active dialect; never overwrite two active forms. */
		if (!(ok4 && oklte && n4 && nlte)) {
			if (ok4 && (!oklte || n4 || !nlte)) {
				form = "common/4g"; restore = save4; held = held4; held4 = NULL;
			} else if (oklte) {
				form = "common/lte"; restore = savelte; held = heldlte; heldlte = NULL;
			}
		}
		json_object_put(held4);
		json_object_put(heldlte);
		if (form) {
			json_object *rats = json_object_new_array();
			json_object_array_add(rats, json_object_new_string("lte"));
			json_object_object_add(raw, "qnwlock_form", json_object_new_string(form));
			json_object_object_add(raw, "qnwlock_restore", json_object_new_string(restore));
			out = json_object_new_object();
			json_object_object_add(out, "cell_lock", held);
			json_object_object_add(out, "cell_lock_rats", rats);
			seen = true;
		}
	}
	if (any)
		while (json_object_array_length(bands))
			json_object_array_del_idx(bands, 0, 1);
	if (seen) {
		json_object *base = block(allowed, NULL, bands, any);
		if (out) {
			json_object_object_foreach(out, k, v)
				json_object_object_add(base, k, json_object_get(v));
			json_object_put(out);
		}
		out = base;
		if (json_object_object_length(raw))
			json_object_object_add(out, "raw", json_object_get(raw));
	}
	json_object_put(raw);
	json_object_put(allowed);
	json_object_put(bands);
	return out;
}

/* ---- "<rat>,<pref1>,<pref2>,<band>,…": Fibocom GTACT (MediaTek) and Intel XACT ---- */

static json_object *act_list(const char *text, const char *prefix, bool xmm)
{
	json_object *allowed, *bands, *out;
	char buf[1024], *save = NULL, *tok, *p;
	const char *preferred = NULL;
	long rat;
	int field = 0, count = 0;

	if (!line_after(text, prefix, buf, sizeof(buf)))
		return NULL;
	allowed = json_object_new_array();
	bands = json_object_new_array();
	rat = strtol(buf, &p, 10);
	if (p == buf)
		rat = -1;
	if (xmm) {
		/* the L850/L860 semantics: 1 = 3G, 2 = 4G, 4 = 3G + 4G */
		if (rat == 1 || rat == 4)
			add_mode(allowed, "3g");
		if (rat == 2 || rat == 4)
			add_mode(allowed, "4g");
	} else {
		/* Fibocom: 1 UMTS, 2 LTE, 4 UMTS+LTE, 10 automatic (the 4G-only modules), 14 NR, 16 UMTS+NR,
		 * 17 LTE+NR, 20 all three */
		if (rat == 10)
			add_mode(allowed, "any");
		if (rat == 1 || rat == 4 || rat == 16 || rat == 20)
			add_mode(allowed, "3g");
		if (rat == 2 || rat == 4 || rat == 17 || rat == 20)
			add_mode(allowed, "4g");
		if (rat == 14 || rat == 16 || rat == 17 || rat == 20)
			add_mode(allowed, "5g");
	}
	/* strtok_r would merge empty fields: walk the commas by hand */
	for (tok = buf; tok; tok = save) {
		long v;

		save = strchr(tok, ',');
		if (save)
			*save++ = '\0';
		field++;
		if (field == 2 && xmm && json_object_array_length(allowed) > 1) {
			/* XACT's first preferred technology: 0 GSM, 1 UMTS, 2 LTE */
			v = strtol(tok, &p, 10);
			if (p != tok)
				preferred = v == 2 ? "4g" : v == 1 ? "3g" : v == 0 ? "2g" : NULL;
		}
		if (field <= 3)
			continue;
		v = strtol(tok, &p, 10);
		if (p == tok)
			continue;
		count++;
		if (!xmm && v >= 5000)
			add_band(bands, "ngran", v - 5000);
		else if (!xmm && v >= 500 && v < 510)
			add_band(bands, "ngran", v - 500);
		else if (v >= 100 && v < 200)
			add_band(bands, "eutran", v - 100);
		else if (v > 0 && v < 100)
			add_band(bands, "utran", v);
	}
	/* no band list: the modem may use every band it has */
	out = block(allowed, preferred, bands, count == 0);
	json_object_put(allowed);
	json_object_put(bands);
	return out;
}

/*
 * ---- Fibocom FM350 / RW350 (MediaTek): GTACT, its test form, the cell lock ----
 * AT+GTACT=? lists what the firmware takes: "(rat),(pref),(pref),(),(UMTS),(LTE),(),(),(NR)", the
 * LTE group as 1xx, NR as 5xx / 5xxx (a RW350-GL-16 lists no NR).
 * AT+EMMCHLCK? "+EMMCHLCK: <on>,<rat>,<gsm1900>,<arfcn>[,<pci>,<mode>]", rat 7 = LTE (live
 * test form on an FM350-GL: (0-1),(0,2,7),(0,1),(0-46589),(0-511)).
 */
static const struct { long code; const char *modes[3]; } gtact_rats[] = {
	{ 1, { "3g" } }, { 2, { "4g" } }, { 4, { "3g", "4g" } }, { 10, { "any" } }, { 14, { "5g" } },
	{ 16, { "3g", "5g" } }, { 17, { "4g", "5g" } }, { 20, { "3g", "4g", "5g" } } };

static void gtact_band(json_object *arr, long v)
{
	if (v >= 5000)
		add_band(arr, "ngran", v - 5000);
	else if (v >= 500 && v < 510)
		add_band(arr, "ngran", v - 500);
	else if (v >= 100 && v < 200)
		add_band(arr, "eutran", v - 100);
	else if (v > 0 && v < 100)
		add_band(arr, "utran", v);
}

/* the cell lock in the block: {rat, arfcn, pci} (pci null when only the frequency is held) */
static json_object *cell_lock(const char *rat, long arfcn, long pci)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "rat", json_object_new_string(rat));
	json_object_object_add(o, "arfcn", json_object_new_int64(arfcn));
	json_object_object_add(o, "pci", pci >= 0 ? json_object_new_int64(pci) : NULL);
	return o;
}

static json_object *fibocom_mtk(json_object *r)
{
	const char *now = json_object_get_string(json_object_object_get(r, "AT+GTACT?"));
	const char *cap = json_object_get_string(json_object_object_get(r, "AT+GTACT=?"));
	const char *lock = json_object_get_string(json_object_object_get(r, "AT+EMMCHLCK?"));
	json_object *out = act_list(now, "+GTACT:", false), *raw, *supported, *combos;
	char buf[1024], rats[128] = "", *p, *q;
	size_t used = 0, k, m;
	int group = 0;

	if (!out)
		return NULL;
	raw = json_object_new_object();
	if (line_after(now, "+GTACT:", buf, sizeof(buf))) {
		/* some firmwares answer "20, 6, 3, ..." and refuse that form written back (12.09.2026) */
		for (p = q = buf; *p; p++)
			if (*p != ' ')
				*q++ = *p;
		*q = '\0';
		json_object_object_add(raw, "gtact", json_object_new_string(buf));
	}
	supported = json_object_new_array();
	combos = json_object_new_array();
	if (line_after(cap, "+GTACT:", buf, sizeof(buf))) {
		for (p = buf; *p; p++) {
			long v;

			if (*p == '(') {
				group++;
				continue;
			}
			if (!isdigit((unsigned char)*p) || (p > buf && (isdigit((unsigned char)p[-1]) || p[-1] == '-')))
				continue;
			v = strtol(p, &q, 10);
			if (*q == '-')
				continue;	/* a range ("0-46589") is no list of values */
			if (group == 1 && used + 8 < sizeof(rats))
				used += (size_t)snprintf(rats + used, sizeof(rats) - used, "%s%ld", used ? "," : "", v);
			else if (group == 5 || group == 6 || group == 9)
				gtact_band(supported, v);
		}
	}
	if (rats[0]) {
		json_object_object_add(raw, "rats", json_object_new_string(rats));
		for (p = rats; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL)
			for (k = 0; k < sizeof(gtact_rats) / sizeof(gtact_rats[0]); k++) {
				json_object *one, *a;

				if (strtol(p, NULL, 10) != gtact_rats[k].code)
					continue;
				one = json_object_new_object();
				a = json_object_new_array();
				for (m = 0; m < 3 && gtact_rats[k].modes[m]; m++)
					json_object_array_add(a, json_object_new_string(gtact_rats[k].modes[m]));
				json_object_object_add(one, "allowed", a);
				json_object_object_add(one, "preferred", NULL);
				json_object_array_add(combos, one);
			}
	}
	if (json_object_array_length(combos))
		json_object_object_add(out, "supported_modes", json_object_get(combos));
	if (json_object_array_length(supported))
		json_object_object_add(out, "supported_bands", json_object_get(supported));
	json_object_put(combos);
	json_object_put(supported);
	if (line_after(lock, "+EMMCHLCK:", buf, sizeof(buf))) {
		long f[6] = { -1, -1, -1, -1, -1, -1 };
		int n = 0;

		for (p = buf; n < 6; n++) {
			f[n] = strtol(p, &q, 10);
			if (q == p)
				f[n] = -1;
			if (!(p = strchr(p, ',')))
				break;
			p++;
		}
		json_object_object_add(raw, "emmchlck", json_object_new_string(buf));
		json_object_object_add(out, "cell_lock", f[0] == 1 && f[3] >= 0 ?
				       cell_lock(f[1] == 7 ? "lte" : f[1] == 2 ? "umts" : f[1] == 0 ? "gsm" : "other", f[3],
						 f[4] > 0 ? f[4] : -1) : NULL);
		json_object_object_add(out, "cell_lock_rats", json_object_new_array());
		json_object_array_add(json_object_object_get(out, "cell_lock_rats"), json_object_new_string("lte"));
	}
	json_object_object_add(out, "raw", raw);
	return out;
}

/*
 * ---- Huawei, 2G/3G sticks: ^SYSCFG: <mode>,<acqorder>,<band>,<roam>,<srvdomain> ----
 * mode 2 automatic, 13 GSM only, 14 WCDMA only; acqorder 0 automatic, 1 GSM first, 2 WCDMA first
 * (Huawei's AT command reference for the E-series sticks). The band mask is 2G/3G's, not shown as bands:
 * 3FFFFFFF is every band.
 */
static json_object *huawei_old(json_object *r)
{
	json_object *allowed, *bands, *out;
	char buf[256], mask[32] = "";
	int mode = -1, order = -1;

	if (!line_after(json_object_get_string(json_object_object_get(r, "AT^SYSCFG?")), "^SYSCFG:", buf, sizeof(buf)) ||
	    sscanf(buf, "%d,%d,%31[0-9A-Fa-f]", &mode, &order, mask) < 2)
		return NULL;
	allowed = json_object_new_array();
	bands = json_object_new_array();
	if (mode == 2 || mode == 13)
		add_mode(allowed, "2g");
	if (mode == 2 || mode == 14)
		add_mode(allowed, "3g");
	out = block(allowed, mode == 2 && order == 1 ? "2g" : mode == 2 && order == 2 ? "3g" : NULL, bands,
		    !strcasecmp(mask, "3FFFFFFF"));
	json_object_put(allowed);
	json_object_put(bands);
	return out;
}

/* ---- Huawei: ^SYSCFGEX: "<acqorder>",<band>,<roam>,<srvdomain>,<lteband>,, ---- */

static json_object *huawei(json_object *r)
{
	json_object *allowed, *bands, *out;
	char buf[512], *q, *f;
	const char *preferred = NULL;
	int field;
	bool any = false;

	if (!line_after(json_object_get_string(json_object_object_get(r, "AT^SYSCFGEX?")), "^SYSCFGEX:", buf, sizeof(buf)))
		return huawei_old(r);
	allowed = json_object_new_array();
	bands = json_object_new_array();
	q = buf + strspn(buf, " \"");
	{
		size_t i, n = strspn(q, "0123456789");
		int pairs = 0;

		for (i = 0; i + 1 < n + 1 && i + 2 <= n; i += 2) {
			const char *m = !strncmp(q + i, "00", 2) ? "any" : !strncmp(q + i, "01", 2) ? "2g" :
					!strncmp(q + i, "02", 2) ? "3g" : !strncmp(q + i, "03", 2) ? "4g" :
					!strncmp(q + i, "08", 2) ? "5g" : NULL;
			if (!m)
				continue;
			add_mode(allowed, m);
			if (!pairs++)
				preferred = m;
		}
		if (pairs < 2 || (preferred && !strcmp(preferred, "any")))
			preferred = NULL;
	}
	/* the fifth field is the LTE band mask */
	for (field = 1, f = buf; f && field < 5; field++) {
		f = strchr(f, ',');
		if (f)
			f++;
	}
	if (f && *f && *f != ',')
		any = mask_bands(f, bands, "eutran") >= 60;
	if (any)
		while (json_object_array_length(bands))
			json_object_array_del_idx(bands, 0, 1);
	out = block(allowed, preferred, bands, any);
	json_object_put(allowed);
	json_object_put(bands);
	return out;
}

/* ---- Foxconn: T99W175 (SDX55, AT^SLMODE bit mask + AT^BAND_PREF) and T77W968 (X20, AT^SLMODE codes + AT^SLBAND) ---- */

/*
 * The number after "^SLMODE:1," (T77 prints "^SLMODE:1,30-LTE Only"); -1 when
 * absent. The two families share the command but not the values: on the T99
 * the second number is a mask (1 WCDMA, 2 LTE, 4 NR5G, 0 automatic; QModem's
 * field reports), on the T77 a code
 * (4 automatic, 14 WCDMA, 30 LTE, 35 WCDMA+LTE):
 * "4" is automatic on one and NR5G only on the other.
 */
static long slmode(const char *text)
{
	char buf[128], *p, *end;
	long v;

	if (!line_after(text, "SLMODE:", buf, sizeof(buf)) || !(p = strchr(buf, ',')))
		return -1;
	p++;
	p += strspn(p, " ");
	v = strtol(p, &end, 10);
	return end == p ? -1 : v;
}

/* the numbers after the colon (or after skip) of one line, as "1,3,7" in out; false when not a band list */
static bool num_list(const char *from, char *out, size_t size)
{
	size_t used = 0;
	const char *p = from;

	out[0] = '\0';
	while (*p && *p != '\r' && *p != '\n') {
		char *end;
		long v = strtol(p, &end, 10);

		if (end == p) {
			if (*p != ',' && *p != ' ' && *p != '(' && *p != ')')
				return false;
			p++;
			continue;
		}
		if (v > 0 && v < 1000 && used + 8 < size)
			used += (size_t)snprintf(out + used, size - used, "%s%ld", used ? "," : "", v);
		p = end;
	}
	return true;
}

/* a "1,3,7" list added as bands of one kind */
static void list_bands(const char *list, json_object *arr, const char *kind)
{
	const char *p = list;
	char *end;

	while (p && *p) {
		add_band(arr, kind, strtol(p, &end, 10));
		p = *end == ',' ? end + 1 : NULL;
	}
}

/*
 * The enabled (or disabled) list of one technology in AT^BAND_PREF? / AT^SLBAND?.
 * Forms seen live (answers from 4pda):
 *   " LTE,Enable Bands :1,2,3,"   (leading space, trailing comma; also "Disable Bands:")
 *   "NR5G_NSA,Enable Bands :..."  (QModem's reading of the same firmware)
 *   "^SLBAND: LTE,Enabled,1,3,7,20"  /  "^SLBAND: LTE,1,3,7,8,20,38"  (T77 firmwares)
 * Every line of the tech is merged (NR5G, NR5G_NSA and NR5G_SA alike).
 */
static bool tech_list(const char *text, const char *tech, bool disabled, char *out, size_t size)
{
	const char *line = text;
	size_t used = 0, tl = strlen(tech);
	bool seen = false;

	out[0] = '\0';
	while (line && *line) {
		const char *p = line + strspn(line, " \t"), *eol = line + strcspn(line, "\r\n");
		char list[512];

		if (!strncmp(p, "^SLBAND:", 8))
			p += 8 + strspn(p + 8, " ");
		if (!strncmp(p, tech, tl) && (p[tl] == ',' || p[tl] == '_')) {
			const char *q = p + tl;
			bool dis = false, ok = true;

			q += strcspn(q, ",\r\n");	/* past "_NSA" / "_SA" */
			if (*q == ',')
				q++;
			if (!strncmp(q, "Enable", 6) || !strncmp(q, "Disable", 7)) {
				dis = *q == 'D';
				q += strcspn(q, ":,\r\n");
				if (*q == ':' || *q == ',')
					q++;
			}
			if (dis == disabled && q < eol && (ok = num_list(q, list, sizeof(list))) && list[0]) {
				if (used + strlen(list) + 2 < size)
					used += (size_t)snprintf(out + used, size - used, "%s%s", used ? "," : "", list);
			}
			seen = seen || ok;
		}
		line = *eol ? eol + 1 : NULL;
	}
	return seen;
}

/* a "1,3,7" list without repeats, numbers ascending */
static void tidy_list(char *list, size_t size)
{
	long nums[256], n;
	size_t i, k, c = 0, used = 0;
	const char *p = list;
	char *end;

	while (p && *p && c < 256) {
		n = strtol(p, &end, 10);
		if (end == p)
			break;
		for (k = 0; k < c && nums[k] != n; k++)
			;
		if (k == c)
			nums[c++] = n;
		p = *end == ',' ? end + 1 : NULL;
	}
	for (i = 1; i < c; i++)
		for (k = i; k > 0 && nums[k - 1] > nums[k]; k--) {
			n = nums[k];
			nums[k] = nums[k - 1];
			nums[k - 1] = n;
		}
	list[0] = '\0';
	for (i = 0; i < c && used + 8 < size; i++)
		used += (size_t)snprintf(list + used, size - used, "%s%ld", i ? "," : "", nums[i]);
}

static json_object *foxconn(json_object *r, bool t77)
{
	static const struct { const char *tech, *kind; } techs[] = { { "WCDMA", "utran" }, { "LTE", "eutran" }, { "NR5G", "ngran" } };
	json_object *allowed = json_object_new_array(), *bands = json_object_new_array(), *supported = json_object_new_array();
	json_object *raw = json_object_new_object(), *out = NULL, *lock_rats = NULL, *lock_held = NULL;
	const char *bt = json_object_get_string(json_object_object_get(r, t77 ? "AT^SLBAND?" : "AT^BAND_PREF?"));
	long mode = slmode(json_object_get_string(json_object_object_get(r, "AT^SLMODE?")));
	bool seen = false;
	char on[512], off[512], buf[24];
	size_t t;

	if (mode >= 0) {
		seen = true;
		snprintf(buf, sizeof(buf), "%ld", mode);
		json_object_object_add(raw, "slmode", json_object_new_string(buf));
		if (t77) {
			if (mode == 4)
				add_mode(allowed, "any");
			if (mode == 14 || mode == 35)
				add_mode(allowed, "3g");
			if (mode == 30 || mode == 35)
				add_mode(allowed, "4g");
		} else if (mode == 0) {
			add_mode(allowed, "any");
		} else if (mode < 8) {
			if (mode & 1)
				add_mode(allowed, "3g");
			if (mode & 2)
				add_mode(allowed, "4g");
			if (mode & 4)
				add_mode(allowed, "5g");
		}
	}
	for (t = 0; t < sizeof(techs) / sizeof(techs[0]); t++) {
		if (t77 && t == 2)
			break;	/* Snapdragon X20: LTE Cat.16, no NR */
		if (!tech_list(bt, techs[t].tech, false, on, sizeof(on)))
			continue;
		seen = true;
		tidy_list(on, sizeof(on));
		json_object_object_add(raw, techs[t].tech, json_object_new_string(on));
		list_bands(on, bands, techs[t].kind);
		list_bands(on, supported, techs[t].kind);
		if (tech_list(bt, techs[t].tech, true, off, sizeof(off)))
			list_bands(off, supported, techs[t].kind);
	}
	if (t77) {
		/* "^SLBAND: LTE,(1,2,3,...,66)": what this firmware has */
		const char *sup = json_object_get_string(json_object_object_get(r, "AT^SLBAND=?"));

		if (tech_list(sup, "LTE", false, on, sizeof(on)) && on[0])
			list_bands(on, supported, "eutran");
	}
	if (!t77) {
		/*
		 * "^LTE_LOCK: (<pci>,<earfcn>)" (PCI first: the T99W175 manual and 4pda #123, #6253); "LTE_LOCK:Have not set cell lock before" when there is none; the NR5G
		 * one reads the same way. ERROR: this firmware has no such lock.
		 */
		static const struct { const char *cmd, *tag, *rat, *key; } locks[] = {
			{ "AT^LTE_LOCK?", "LTE_LOCK:", "lte", "lte_lock" }, { "AT^NR5G_LOCK?", "NR5G_LOCK:", "nr", "nr_lock" } };
		json_object *rats = json_object_new_array(), *held = NULL;

		for (t = 0; t < sizeof(locks) / sizeof(locks[0]); t++) {
			const char *text = json_object_get_string(json_object_object_get(r, locks[t].cmd));
			long pci = -1, arfcn = -1;
			char *p, *q;

			if (!line_after(text, locks[t].tag, on, sizeof(on)))
				continue;
			p = on + strspn(on, " (");
			pci = strtol(p, &q, 10);
			if (q == p || *q != ',')
				pci = -1;
			else {
				p = q + 1 + strspn(q + 1, " ");
				arfcn = strtol(p, &q, 10);
				if (q == p)
					arfcn = pci = -1;
			}
			json_object_array_add(rats, json_object_new_string(locks[t].rat));
			snprintf(off, sizeof(off), pci >= 0 ? "%ld,%ld" : "", pci, arfcn);
			json_object_object_add(raw, locks[t].key, json_object_new_string(off));
			if (pci >= 0 && !held)
				held = cell_lock(locks[t].rat, arfcn, pci);
		}
		if (json_object_array_length(rats)) {
			lock_rats = rats;
			lock_held = held;
		} else {
			json_object_put(rats);
		}
	}
	if (seen) {
		/* what SLMODE can say, in ModemManager's shape: the page offers these only */
		static const char *const t99_sets[][3] = { { "3g" }, { "4g" }, { "3g", "4g" }, { "5g" }, { "3g", "5g" },
							     { "4g", "5g" }, { "3g", "4g", "5g" } };
		json_object *combos = json_object_new_array();
		size_t c, m;

		for (c = 0; c < (t77 ? 3 : sizeof(t99_sets) / sizeof(t99_sets[0])); c++) {
			json_object *one = json_object_new_object(), *a = json_object_new_array();

			for (m = 0; m < 3 && t99_sets[c][m]; m++)
				json_object_array_add(a, json_object_new_string(t99_sets[c][m]));
			json_object_object_add(one, "allowed", a);
			json_object_object_add(one, "preferred", NULL);
			json_object_array_add(combos, one);
		}
		out = block(allowed, NULL, bands, false);
		json_object_object_add(out, "supported_modes", combos);
		if (json_object_array_length(supported))
			json_object_object_add(out, "supported_bands", json_object_get(supported));
		json_object_object_add(out, "raw", json_object_get(raw));
		if (lock_rats) {
			json_object_object_add(out, "cell_lock", lock_held);
			json_object_object_add(out, "cell_lock_rats", lock_rats);
		}
	} else {
		json_object_put(lock_rats);
		json_object_put(lock_held);
	}
	json_object_put(raw);
	json_object_put(allowed);
	json_object_put(bands);
	json_object_put(supported);
	return out;
}

/*
 * ---- SIMCom: +CNMP: <mode> and +CNBP: <2G/3G mask>,<LTE mask>[,...] ----
 * CNMP (SIM7100/SIM7600/SIM8200 AT manuals): 2 automatic, 13 GSM, 14 WCDMA, 38 LTE, 19 GSM+WCDMA, 39 GSM+WCDMA+LTE,
 * 51 GSM+LTE, 54 WCDMA+LTE, 71 NR5G, 109 LTE+NR5G; the others (CDMA, TD-SCDMA) are no mode of ours. The second CNBP
 * mask is LTE's, bit 0 band 1. The bands are what the module allows, not a lock: every band it has reads as all set.
 */
static json_object *simcom(json_object *r)
{
	static const struct { int code; const char *modes[3]; } cnmp[] = {
		{ 2, { "any" } }, { 13, { "2g" } }, { 14, { "3g" } }, { 38, { "4g" } }, { 19, { "2g", "3g" } },
		{ 39, { "2g", "3g", "4g" } }, { 51, { "2g", "4g" } }, { 54, { "3g", "4g" } }, { 71, { "5g" } }, { 109, { "4g", "5g" } } };
	json_object *allowed, *bands, *out;
	char buf[256], *f;
	int code = -1;
	size_t i, k;

	if (!line_after(json_object_get_string(json_object_object_get(r, "AT+CNMP?")), "+CNMP:", buf, sizeof(buf)) ||
	    sscanf(buf, "%d", &code) != 1)
		return NULL;
	allowed = json_object_new_array();
	bands = json_object_new_array();
	for (i = 0; i < sizeof(cnmp) / sizeof(cnmp[0]); i++)
		if (cnmp[i].code == code)
			for (k = 0; k < 3 && cnmp[i].modes[k]; k++)
				add_mode(allowed, cnmp[i].modes[k]);
	if (line_after(json_object_get_string(json_object_object_get(r, "AT+CNBP?")), "+CNBP:", buf, sizeof(buf)) &&
	    (f = strchr(buf, ',')))
		mask_bands(f + 1, bands, "eutran");
	out = block(allowed, NULL, bands, false);
	json_object_put(allowed);
	json_object_put(bands);
	return out;
}

/* ---- Telit: +WS46: <n>, 3GPP 27.007's values (12 GERAN, 22 UTRAN, 25 all three, 28 E-UTRAN, 29-31 the pairs, 36-38 with
 * NG-RAN); the bands (#BND, indexes per product) are not read yet ---- */
static json_object *telit(json_object *r)
{
	static const struct { int code; const char *modes[4]; } ws46[] = {
		{ 12, { "2g" } }, { 22, { "3g" } }, { 25, { "2g", "3g", "4g" } }, { 28, { "4g" } }, { 29, { "2g", "3g" } },
		{ 30, { "2g", "4g" } }, { 31, { "3g", "4g" } }, { 36, { "5g" } }, { 37, { "4g", "5g" } }, { 38, { "3g", "4g", "5g" } } };
	json_object *allowed, *out;
	char buf[128];
	int code = -1;
	size_t i, k;

	if (!line_after(json_object_get_string(json_object_object_get(r, "AT+WS46?")), "+WS46:", buf, sizeof(buf)) ||
	    sscanf(buf, "%d", &code) != 1)
		return NULL;
	allowed = json_object_new_array();
	for (i = 0; i < sizeof(ws46) / sizeof(ws46[0]); i++)
		if (ws46[i].code == code)
			for (k = 0; k < 4 && ws46[i].modes[k]; k++)
				add_mode(allowed, ws46[i].modes[k]);
	out = block(allowed, NULL, NULL, false);
	json_object_put(allowed);
	return out;
}

/* ---- Sierra: !SELRAT: <nn>, <name> («00, Automatic», «06, LTE Only», «0C, UMTS and LTE only»): the name says which
 * RATs, the code differs between generations; the bands (!BAND profiles) are not read yet ---- */
static json_object *sierra(json_object *r)
{
	json_object *allowed, *out;
	char buf[128], *name;

	if (!line_after(json_object_get_string(json_object_object_get(r, "AT!SELRAT?")), "!SELRAT:", buf, sizeof(buf)) ||
	    !(name = strchr(buf, ',')))
		return NULL;
	name++;
	allowed = json_object_new_array();
	if (strcasestr(name, "automatic"))
		add_mode(allowed, "any");
	else {
		if (strcasestr(name, "GSM"))
			add_mode(allowed, "2g");
		if (strcasestr(name, "UMTS"))
			add_mode(allowed, "3g");
		if (strcasestr(name, "LTE"))
			add_mode(allowed, "4g");
		if (strcasestr(name, "NR5G") || strcasestr(name, "5G"))
			add_mode(allowed, "5g");
	}
	out = block(allowed, NULL, NULL, false);
	json_object_put(allowed);
	return out;
}

json_object *mm_radio_from_at(const char *dialect, json_object *responses)
{
	if (!dialect || !responses)
		return NULL;
	if (!strcmp(dialect, "quectel") || !strcmp(dialect, "quectel-ep06"))
		return quectel(responses, !strcmp(dialect, "quectel-ep06"));
	if (!strcmp(dialect, "fibocom-mtk"))
		return fibocom_mtk(responses);
	if (!strcmp(dialect, "xmm"))
		return act_list(json_object_get_string(json_object_object_get(responses, "AT+XACT?")), "+XACT:", true);
	if (!strcmp(dialect, "huawei"))
		return huawei(responses);
	if (!strcmp(dialect, "foxconn-sdx") || !strcmp(dialect, "foxconn-t77"))
		return foxconn(responses, !strcmp(dialect, "foxconn-t77"));
	if (!strcmp(dialect, "simcom"))
		return simcom(responses);
	if (!strcmp(dialect, "telit"))
		return telit(responses);
	if (!strcmp(dialect, "sierra"))
		return sierra(responses);
	return NULL;
}

/* ---- QModem: {"network_prefer": {"3G": "1", …}} and {"lockband": {"LTE": {available_band, lock_band}, …}} ---- */

json_object *mm_radio_from_qmodem(json_object *prefer, json_object *lockband)
{
	json_object *allowed = json_object_new_array(), *bands = json_object_new_array(), *supported = json_object_new_array();
	json_object *np = NULL, *lb = NULL, *out = NULL, *prefer_raw = json_object_new_array(), *lock_raw = json_object_new_object();
	bool seen = false;

	if (prefer && json_object_object_get_ex(prefer, "network_prefer", &np) && json_object_is_type(np, json_type_object)) {
		static const struct { const char *key, *mode; } techs[] = { { "2G", "2g" }, { "3G", "3g" }, { "4G", "4g" }, { "5G", "5g" } };
		size_t t;

		seen = true;
		for (t = 0; t < sizeof(techs) / sizeof(techs[0]); t++) {
			const char *v = json_object_get_string(json_object_object_get(np, techs[t].key));
			if (v && !strcmp(v, "1")) {
				add_mode(allowed, techs[t].mode);
				json_object_array_add(prefer_raw, json_object_new_string(techs[t].key));
			}
		}
	}
	if (lockband && json_object_object_get_ex(lockband, "lockband", &lb) && json_object_is_type(lb, json_type_object)) {
		json_object_object_foreach(lb, group, val) {
			const char *kind = !strcmp(group, "LTE") ? "eutran" : !strcmp(group, "UMTS") ? "utran" :
					   !strcmp(group, "NR") || !strcmp(group, "NR_NSA") ? "ngran" : NULL;
			json_object *lock = json_object_object_get(val, "lock_band"), *avail = json_object_object_get(val, "available_band");
			size_t i;

			char list[512] = "";
			size_t used = 0;

			if (!kind)
				continue;
			seen = true;
			for (i = 0; json_object_is_type(lock, json_type_array) && i < json_object_array_length(lock); i++) {
				long n = strtol(json_object_get_string(json_object_array_get_idx(lock, i)), NULL, 10);

				add_band(bands, kind, n);
				if (n > 0 && used + 8 < sizeof(list))
					used += (size_t)snprintf(list + used, sizeof(list) - used, "%s%ld", used ? "," : "", n);
			}
			json_object_object_add(lock_raw, group, json_object_new_string(list));
			for (i = 0; json_object_is_type(avail, json_type_array) && i < json_object_array_length(avail); i++)
				add_band(supported, kind, strtol(json_object_get_string(
					json_object_object_get(json_object_array_get_idx(avail, i), "band_id")), NULL, 10));
		}
	}
	if (seen) {
		json_object *raw = json_object_new_object();

		out = block(allowed, NULL, bands, false);
		if (json_object_array_length(supported))
			json_object_object_add(out, "supported_bands", json_object_get(supported));
		/* as QModem said it, per band class: a rollback gives it back the same */
		if (np)
			json_object_object_add(raw, "network_prefer", json_object_get(prefer_raw));
		if (lb)
			json_object_object_add(raw, "lockband", json_object_get(lock_raw));
		json_object_object_add(out, "raw", raw);
	}
	json_object_put(prefer_raw);
	json_object_put(lock_raw);
	json_object_put(allowed);
	json_object_put(bands);
	json_object_put(supported);
	return out;
}

/* ---- stage 2: writing (spec 25.9.12) ---- */

static const char *const mode_order[] = { "2g", "3g", "4g", "5g" };

static bool listed(json_object *arr, const char *s)
{
	size_t i;

	for (i = 0; arr && json_object_is_type(arr, json_type_array) && i < json_object_array_length(arr); i++)
		if (!strcmp(json_object_get_string(json_object_array_get_idx(arr, i)), s))
			return true;
	return false;
}

static bool same_set(json_object *a, json_object *b)
{
	size_t i;

	if (json_object_array_length(a) != json_object_array_length(b))
		return false;
	for (i = 0; i < json_object_array_length(a); i++)
		if (!listed(b, json_object_get_string(json_object_array_get_idx(a, i))))
			return false;
	return true;
}

/* "eutran-3" -> kind "eutran", number 3; false when it is not a band name */
static bool band_parts(const char *name, char *kind, size_t size, long *n)
{
	const char *dash = name ? strrchr(name, '-') : NULL;
	char *end;

	if (!dash || (size_t)(dash - name) >= size)
		return false;
	snprintf(kind, size, "%.*s", (int)(dash - name), name);
	if (strcmp(kind, "eutran") && strcmp(kind, "ngran") && strcmp(kind, "utran"))
		return false;
	*n = strtol(dash + 1, &end, 10);
	return *end == '\0' && *n > 0 && *n < 1000;
}

/* the numbers of one kind, joined by sep ("" when none) */
static void band_list(json_object *bands, const char *want, char sep, char *out, size_t size)
{
	size_t i, used = 0;
	char kind[16];
	long n;

	out[0] = '\0';
	for (i = 0; i < json_object_array_length(bands); i++)
		if (band_parts(json_object_get_string(json_object_array_get_idx(bands, i)), kind, sizeof(kind), &n) &&
		    !strcmp(kind, want) && used + 8 < size)
			used += (size_t)snprintf(out + used, size - used, "%s%ld", used ? (char[]){ sep, 0 } : "", n);
}

static bool has_kind(json_object *bands, const char *want)
{
	char buf[64];

	band_list(bands, want, ',', buf, sizeof(buf));
	return buf[0] != '\0';
}

/* a number in a "1,2,4,10" list */
static bool code_listed(const char *list, long code)
{
	const char *p = list;
	char *end;

	while (p && *p) {
		if (strtol(p, &end, 10) == code && end != p)
			return true;
		p = strchr(p, ',');
		if (p)
			p++;
	}
	return false;
}

static json_object *step(const char *a, const char *b)
{
	json_object *s = json_object_new_array();

	json_object_array_add(s, json_object_new_string(a));
	if (b)
		json_object_array_add(s, json_object_new_string(b));
	return s;
}

static json_object *fail(char *why, size_t size, const char *text, json_object *steps)
{
	snprintf(why, size, "%s", text);
	json_object_put(steps);
	return NULL;
}

json_object *mm_modes_narrowed(json_object *supported_modes, json_object *supported_bands, json_object *port_types)
{
	static const char *const band_tech[][2] = { { "geran-", "2g" }, { "utran-", "3g" }, { "eutran-", "4g" }, { "ngran-", "5g" } };
	json_object *one, *allowed, *o, *possible;
	bool have[4] = { false, false, false, false }, mbim = false;
	const char *wdm = NULL;
	size_t i, k, n = 0;

	if (!json_object_is_type(supported_modes, json_type_array) || json_object_array_length(supported_modes) != 1)
		return NULL;
	one = json_object_array_get_idx(supported_modes, 0);
	allowed = json_object_object_get(one, "allowed");
	if (!json_object_is_type(allowed, json_type_array) || json_object_array_length(allowed) != 1)
		return NULL;
	for (i = 0; json_object_is_type(supported_bands, json_type_array) && i < json_object_array_length(supported_bands); i++) {
		const char *b = json_object_get_string(json_object_array_get_idx(supported_bands, i));

		for (k = 0; b && k < 4; k++)
			if (!strncmp(b, band_tech[k][0], strlen(band_tech[k][0])))
				have[k] = true;
	}
	for (k = 0; k < 4; k++)
		n += have[k];
	if (n < 2)
		return NULL;	/* a single-technology modem: nothing narrowed */
	if (json_object_is_type(port_types, json_type_object)) {
		json_object_object_foreach(port_types, port, type) {
			const char *t = json_object_get_string(type);

			if (!wdm && !strncmp(port, "cdc-wdm", 7) && strspn(port + 7, "0123456789") == strlen(port + 7) && port[7] &&
			    t && (!strcmp(t, "qmi") || !strcmp(t, "mbim"))) {
				wdm = port;
				mbim = !strcmp(t, "mbim");
			}
		}
	}
	if (!wdm)
		return NULL;	/* no QMI to write the preference through */
	o = json_object_new_object();
	possible = json_object_new_array();
	for (k = 0; k < 4; k++)
		if (have[k])
			json_object_array_add(possible, json_object_new_string(band_tech[k][1]));
	json_object_object_add(o, "offered", json_object_get(allowed));
	json_object_object_add(o, "possible", possible);
	json_object_object_add(o, "wdm", json_object_new_string(wdm));
	json_object_object_add(o, "mbim", json_object_new_boolean(mbim));
	return o;
}

json_object *mm_radio_plan(const char *via, const char *dialect, json_object *current, json_object *want, char *why, size_t size)
{
	json_object *steps = json_object_new_array(), *out, *modes = NULL, *bands = NULL, *allowed = NULL, *raw = NULL, *lock = NULL;
	json_object *supported = json_object_object_get(current, "supported_bands");
	const char *preferred = NULL, *lk_rat = NULL;
	bool bands_any = false, rollback, lock_asked = false;
	long lk_arfcn = -1, lk_pci = -1;
	char buf[512];
	size_t i, k;

	why[0] = '\0';
	if (!via || !want || !json_object_is_type(want, json_type_object))
		return fail(why, size, "nothing to set", steps);
	/* a block read earlier with its raw values: the way back */
	rollback = json_object_object_get_ex(want, "raw", &raw) && json_object_is_type(raw, json_type_object);
	if (json_object_object_get_ex(want, "modes", &modes) && modes) {
		json_object *p = json_object_object_get(modes, "preferred");

		allowed = json_object_object_get(modes, "allowed");
		if (!json_object_is_type(allowed, json_type_array) || !json_object_array_length(allowed))
			return fail(why, size, "modes.allowed: at least one technology", steps);
		for (i = 0; i < json_object_array_length(allowed); i++) {
			const char *m = json_object_get_string(json_object_array_get_idx(allowed, i));
			bool known = !strcmp(m, "any") && json_object_array_length(allowed) == 1;

			for (k = 0; k < 4; k++)
				known = known || !strcmp(m, mode_order[k]);
			if (!known)
				return fail(why, size, "modes.allowed: 2g, 3g, 4g, 5g, or any alone", steps);
		}
		if (p && json_object_is_type(p, json_type_string) && strcmp(json_object_get_string(p), "none")) {
			preferred = json_object_get_string(p);
			if (!listed(allowed, preferred) || json_object_array_length(allowed) < 2)
				return fail(why, size, "modes.preferred: one of two or more allowed technologies", steps);
		}
	}
	if (json_object_object_get_ex(want, "bands", &bands) && bands) {
		if (json_object_is_type(bands, json_type_string) && !strcmp(json_object_get_string(bands), "any")) {
			bands_any = true;
		} else if (json_object_is_type(bands, json_type_array) && json_object_array_length(bands)) {
			for (i = 0; i < json_object_array_length(bands); i++) {
				const char *b = json_object_get_string(json_object_array_get_idx(bands, i));
				char kind[16];
				long n;

				if (!band_parts(b, kind, sizeof(kind), &n))
					return fail(why, size, "bands: names like eutran-3, ngran-78, utran-1, or \"any\"", steps);
				if (!rollback && json_object_is_type(supported, json_type_array) && json_object_array_length(supported) &&
				    !listed(supported, b))
					return fail(why, size, "bands: a band the modem does not have", steps);
			}
		} else if (!rollback) {
			return fail(why, size, "bands: at least one band, or \"any\"", steps);
		} else {
			bands = NULL;	/* read with no band list: nothing to give back */
		}
	}
	if (json_object_object_get_ex(want, "cell_lock", &lock)) {
		json_object *rats = json_object_object_get(current, "cell_lock_rats");

		lock_asked = true;
		if (!json_object_is_type(rats, json_type_array) || !json_object_array_length(rats))
			return fail(why, size, "this modem's cell lock could not be read, so it is not changed", steps);
		if (!lock || (json_object_is_type(lock, json_type_string) && !strcmp(json_object_get_string(lock), "none"))) {
			lock = NULL;	/* off */
		} else if (json_object_is_type(lock, json_type_object)) {
			json_object *a = json_object_object_get(lock, "arfcn"), *c = json_object_object_get(lock, "pci");

			lk_rat = json_object_get_string(json_object_object_get(lock, "rat"));
			if (!lk_rat)
				lk_rat = "lte";
			if (!listed(rats, lk_rat))
				return fail(why, size, "cell_lock.rat: a technology this modem cannot lock", steps);
			if (!json_object_is_type(a, json_type_int) || (lk_arfcn = json_object_get_int64(a)) < 0 ||
			    lk_arfcn > (!strcmp(lk_rat, "nr") ? 3279165 : 262143))
				return fail(why, size, "cell_lock.arfcn: the channel number (EARFCN, NR-ARFCN)", steps);
			if (c && (!json_object_is_type(c, json_type_int) || (lk_pci = json_object_get_int64(c)) < 0 ||
				  lk_pci > (!strcmp(lk_rat, "nr") ? 1007 : 503)))
				return fail(why, size, "cell_lock.pci: the cell's physical id (LTE 0-503, NR 0-1007)", steps);
		} else {
			return fail(why, size, "cell_lock: {rat, arfcn, pci} or \"none\"", steps);
		}
		if (strcmp(via, "at"))
			return fail(why, size, "the cell lock is set over the engine's own AT port, not through ModemManager or QModem", steps);
	}
	if (!allowed && !bands && !rollback && !lock_asked)
		return fail(why, size, "nothing to set", steps);

	if (!strcmp(via, "modemmanager")) {
		if (allowed) {
			json_object *sm = json_object_object_get(current, "supported_modes");
			bool ok = !json_object_is_type(sm, json_type_array) || !json_object_array_length(sm);
			char a[64] = "", pref[48];
			size_t used = 0;

			if (listed(allowed, "any"))
				return fail(why, size, "ModemManager: name the technologies (any is not one of its modes)", steps);
			/* narrowed: what MM offers stays MM's, the rest goes to the modem's own preference below */
			if (json_object_object_get(current, "narrowed"))
				ok = !preferred && same_set(json_object_object_get(json_object_object_get(current, "narrowed"), "offered"), allowed);
			for (i = 0; !ok && !json_object_object_get(current, "narrowed") && i < json_object_array_length(sm); i++) {
				json_object *c = json_object_array_get_idx(sm, i);
				const char *cp = json_object_get_string(json_object_object_get(c, "preferred"));

				if (cp && !strcmp(cp, "none"))
					cp = NULL;
				ok = same_set(json_object_object_get(c, "allowed"), allowed) &&
				     ((!cp && !preferred) || (cp && preferred && !strcmp(cp, preferred)));
			}
			if (!ok) {
				json_object *nw = json_object_object_get(current, "narrowed"), *pos = json_object_object_get(nw, "possible");
				static const char *const qmi_name[] = { "gsm", "umts", "lte", "5gnr" };
				bool fits = nw && json_object_is_type(pos, json_type_array) && !preferred;

				for (i = 0; fits && i < json_object_array_length(allowed); i++)
					fits = listed(pos, json_object_get_string(json_object_array_get_idx(allowed, i)));
				if (!fits)
					return fail(why, size, nw ? "ModemManager offers one mode only (the modem stored it); this combination is not one the modem's bands allow" :
							 "ModemManager does not offer this combination of technologies for the modem", steps);
				/* the modem's own stored preference, LTE first, then 5G, 3G, 2G (the order is the search order) */
				{
					static const int order[] = { 2, 3, 1, 0 };

					snprintf(buf, sizeof(buf), "--nas-set-system-selection-preference=");
					for (k = 0; k < 4; k++)
						if (listed(allowed, mode_order[order[k]]))
							snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%s%s",
								 buf[strlen(buf) - 1] == '=' ? "" : "|", qmi_name[order[k]]);
				}
				json_object_array_add(steps, step("qmi", buf));
				goto bands_step;
			}
			for (k = 0; k < 4; k++)
				if (listed(allowed, mode_order[k]))
					used += (size_t)snprintf(a + used, sizeof(a) - used, "%s%s", used ? "|" : "", mode_order[k]);
			snprintf(buf, sizeof(buf), "--set-allowed-modes=%s", a);
			snprintf(pref, sizeof(pref), "--set-preferred-mode=%s", preferred ? preferred : "none");
			json_object_array_add(steps, step(buf, pref));
		}
bands_step:
		if (bands_any) {
			json_object_array_add(steps, step("--set-current-bands=any", NULL));
		} else if (bands) {
			size_t used = (size_t)snprintf(buf, sizeof(buf), "--set-current-bands=");

			for (i = 0; i < json_object_array_length(bands) && used + 16 < sizeof(buf); i++)
				used += (size_t)snprintf(buf + used, sizeof(buf) - used, "%s%s", i ? "|" : "",
							 json_object_get_string(json_object_array_get_idx(bands, i)));
			json_object_array_add(steps, step(buf, NULL));
		}
	} else if (!strcmp(via, "qmodem")) {
		json_object *rp = rollback ? json_object_object_get(raw, "network_prefer") : NULL;
		json_object *rl = rollback ? json_object_object_get(raw, "lockband") : NULL;

		if (preferred)
			return fail(why, size, "QModem sets the allowed technologies only, not a preferred one", steps);
		if (rp && json_object_is_type(rp, json_type_array) && json_object_array_length(rp)) {
			json_object_array_add(steps, step("set_network_prefer", json_object_to_json_string_ext(rp, JSON_C_TO_STRING_PLAIN)));
		} else if (allowed) {
			json_object *np = json_object_new_array();
			static const char *const up[] = { "2G", "3G", "4G", "5G" };

			for (k = 0; k < 4; k++)
				if (listed(allowed, "any") ? k > 0 : listed(allowed, mode_order[k]))
					json_object_array_add(np, json_object_new_string(up[k]));
			json_object_array_add(steps, step("set_network_prefer", json_object_to_json_string_ext(np, JSON_C_TO_STRING_PLAIN)));
			json_object_put(np);
		}
		if (rl && json_object_is_type(rl, json_type_object)) {
			json_object_object_foreach(rl, cls, list) {
				json_object *arg;

				if (!json_object_get_string(list) || !*json_object_get_string(list))
					continue;
				arg = json_object_new_object();
				json_object_object_add(arg, "band_class", json_object_new_string(cls));
				json_object_object_add(arg, "lock_band", json_object_get(list));
				json_object_array_add(steps, step("set_lockband", json_object_to_json_string_ext(arg, JSON_C_TO_STRING_PLAIN)));
				json_object_put(arg);
			}
		} else if (bands || bands_any) {
			static const struct { const char *cls, *kind; } classes[] = {
				{ "UMTS", "utran" }, { "LTE", "eutran" }, { "NR", "ngran" }, { "NR_NSA", "ngran" } };
			json_object *src = bands_any ? supported : bands;
			json_object *have = json_object_object_get(json_object_object_get(current, "raw"), "lockband");

			if (bands_any && (!json_object_is_type(supported, json_type_array) || !json_object_array_length(supported)))
				return fail(why, size, "QModem did not say which bands the modem has: name them", steps);
			for (k = 0; k < sizeof(classes) / sizeof(classes[0]); k++) {
				json_object *arg;

				/* a class QModem reported for this modem; never one left empty */
				if (have && !json_object_object_get(have, classes[k].cls))
					continue;
				band_list(src, classes[k].kind, ',', buf, sizeof(buf));
				if (!buf[0]) {
					if (supported && has_kind(supported, classes[k].kind))
						return fail(why, size, "keep at least one band of each kind the modem has (LTE, NR, UMTS)", steps);
					continue;
				}
				arg = json_object_new_object();
				json_object_object_add(arg, "band_class", json_object_new_string(classes[k].cls));
				json_object_object_add(arg, "lock_band", json_object_new_string(buf));
				json_object_array_add(steps, step("set_lockband", json_object_to_json_string_ext(arg, JSON_C_TO_STRING_PLAIN)));
				json_object_put(arg);
			}
		}
	} else if (!strcmp(via, "at") && dialect && !strcmp(dialect, "quectel-ep06")) {
		json_object *snap = rollback ? raw : json_object_object_get(current, "raw");
		const char *form = json_object_get_string(json_object_object_get(snap, "qnwlock_form"));
		const char *restore = json_object_get_string(json_object_object_get(snap, "qnwlock_restore"));
		if (!lock_asked)
			return fail(why, size, "EP06 QCFG mode/band edits are read-only", steps);
		if (!rollback && (allowed || bands))
			return fail(why, size, "EP06 cell lock cannot be combined with mode/band edits", steps);
		if (!form || (strcmp(form, "common/4g") && strcmp(form, "common/lte")) || !restore)
			return fail(why, size, "EP06 QNWLOCK is unreadable: no safe rollback", steps);
		if (rollback)
			snprintf(buf, sizeof(buf), "AT+QNWLOCK=\"%s\",%s", form, restore);
		else if (!lock)
			snprintf(buf, sizeof(buf), "AT+QNWLOCK=\"%s\",0", form);
		else {
			if (lk_pci < 0)
				return fail(why, size, "EP06 needs both EARFCN and PCI", steps);
			snprintf(buf, sizeof(buf), "AT+QNWLOCK=\"%s\",%d,%ld,%ld",
				 form, !strcmp(form, "common/4g") ? 1 : 2, lk_arfcn, lk_pci);
		}
		json_object_array_add(steps, step(buf, NULL));
	} else if (!strcmp(via, "at") && dialect && !strcmp(dialect, "quectel")) {
		static const char *const groups[] = { "mode_pref", "lte_band", "nsa_nr5g_band", "nr5g_band" };
		json_object *have = rollback ? raw : json_object_object_get(current, "raw");

		/* the QNWPREFCFG generation only: the LTE one (QCFG) is read, not written yet */
		if (!have || !json_object_object_get(have, "mode_pref"))
			return fail(why, size, "this Quectel generation (QCFG) is read only for now", steps);
		if (preferred)
			return fail(why, size, "Quectel's mode_pref has no preferred technology", steps);
		if (rollback) {
			for (k = 0; k < sizeof(groups) / sizeof(groups[0]); k++) {
				const char *v = json_object_get_string(json_object_object_get(raw, groups[k]));

				if (!v || !*v)
					continue;
				snprintf(buf, sizeof(buf), "AT+QNWPREFCFG=\"%s\",%s", groups[k], v);
				json_object_array_add(steps, step(buf, NULL));
			}
		} else {
			if (allowed) {
				static const char *const q[] = { "GSM", "WCDMA", "LTE", "NR5G" };
				size_t used = 0;
				char m[64] = "";

				for (k = 0; k < 4 && !listed(allowed, "any"); k++)
					if (listed(allowed, mode_order[k]))
						used += (size_t)snprintf(m + used, sizeof(m) - used, "%s%s", used ? ":" : "", q[k]);
				snprintf(buf, sizeof(buf), "AT+QNWPREFCFG=\"mode_pref\",%s", used ? m : "AUTO");
				json_object_array_add(steps, step(buf, NULL));
			}
			if (bands_any)
				return fail(why, size, "Quectel: name the bands (it has no \"all bands\" value to write)", steps);
			if (bands) {
				for (k = 1; k < sizeof(groups) / sizeof(groups[0]); k++) {
					char list[400];

					if (!json_object_object_get(have, groups[k]))
						continue;
					band_list(bands, k == 1 ? "eutran" : "ngran", ':', list, sizeof(list));
					if (!list[0])
						return fail(why, size, "keep at least one band of each kind the modem has (LTE, NR)", steps);
					snprintf(buf, sizeof(buf), "AT+QNWPREFCFG=\"%s\",%s", groups[k], list);
					json_object_array_add(steps, step(buf, NULL));
				}
			}
		}
	} else if (!strcmp(via, "at") && dialect && (!strcmp(dialect, "foxconn-sdx") || !strcmp(dialect, "foxconn-t77"))) {
		/*
		 * T99W175: AT^SLMODE=1,<mask> and AT^BAND_PREF=<tech>,2,<list> per technology;
		 * T77W968: AT^SLMODE=1,<code> and AT^SLBAND=LTE,2,<list> (its 3G set is read only:
		 * no write form in the field corpus). Never the bare AT^BAND_PREF / AT^SLBAND: it
		 * resets every technology's mask to the factory one (an MV31-W field
		 * report 2026-08-17). The list goes unquoted: the quoted form is silently
		 * ignored by the firmware (same report).
		 */
		bool t77 = !strcmp(dialect, "foxconn-t77");
		static const struct { const char *tech, *kind; } techs[] = { { "WCDMA", "utran" }, { "LTE", "eutran" }, { "NR5G", "ngran" } };
		json_object *have = rollback ? raw : json_object_object_get(current, "raw");
		const char *cmd = t77 ? "AT^SLBAND" : "AT^BAND_PREF";

		if (!have || !json_object_object_get(have, "slmode"))
			return fail(why, size, "read the settings first: the modem did not answer AT^SLMODE?", steps);
		if (preferred)
			return fail(why, size, "Foxconn's SLMODE has no preferred technology", steps);
		if (rollback) {
			const char *m = json_object_get_string(json_object_object_get(raw, "slmode"));

			if (m && *m) {
				snprintf(buf, sizeof(buf), "AT^SLMODE=1,%s", m);
				json_object_array_add(steps, step(buf, NULL));
			}
		} else if (allowed) {
			long code = -1;

			if (listed(allowed, "2g"))
				return fail(why, size, "Foxconn: no 2G on this modem", steps);
			if (t77) {
				if (listed(allowed, "5g"))
					return fail(why, size, "T77W968 (Snapdragon X20) has no 5G", steps);
				code = listed(allowed, "any") ? 4 : listed(allowed, "3g") && listed(allowed, "4g") ? 35 :
				       listed(allowed, "3g") ? 14 : 30;
			} else {
				code = listed(allowed, "any") ? 0 : (listed(allowed, "3g") ? 1 : 0) | (listed(allowed, "4g") ? 2 : 0) |
								  (listed(allowed, "5g") ? 4 : 0);
			}
			snprintf(buf, sizeof(buf), "AT^SLMODE=1,%ld", code);
			json_object_array_add(steps, step(buf, NULL));
		}
		for (k = 0; k < sizeof(techs) / sizeof(techs[0]); k++) {
			char list[400];
			const char *was = json_object_get_string(json_object_object_get(have, techs[k].tech));

			if (!was || !*was)
				continue;	/* a technology this modem did not report: never written */
			if (rollback) {
				snprintf(list, sizeof(list), "%s", json_object_get_string(json_object_object_get(raw, techs[k].tech)));
			} else if (bands || bands_any) {
				json_object *src = bands_any ? supported : bands;

				if (bands_any && (!json_object_is_type(supported, json_type_array) || !json_object_array_length(supported)))
					return fail(why, size, "the modem did not say which bands it has: name them", steps);
				band_list(src, techs[k].kind, ',', list, sizeof(list));
				tidy_list(list, sizeof(list));
				if (!list[0])
					return fail(why, size, "keep at least one band of each kind the modem has (3G, LTE, NR)", steps);
				if (!strcmp(list, was))
					continue;	/* unchanged */
			} else {
				continue;
			}
			if (t77 && strcmp(techs[k].tech, "LTE")) {
				if (rollback)
					continue;
				return fail(why, size, "T77W968: only the LTE bands can be changed (its 3G set is read only)", steps);
			}
			snprintf(buf, sizeof(buf), "%s=%s,2,%s", cmd, techs[k].tech, list);
			json_object_array_add(steps, step(buf, NULL));
		}
		if (lock_asked) {
			/*
			 * AT^LTE_LOCK=<pci>,<earfcn> / AT^NR5G_LOCK=<pci>,<arfcn>, the bare command clears it. The
			 * firmware takes it only at the module's restart: before that the reading still says no lock
			 * (live reports 24.08.2026), so the restart is the last
			 * step. The NR5G write form is by symmetry, not seen live: an ERROR stops before the restart.
			 */
			static const struct { const char *rat, *cmd, *key; } locks[] = {
				{ "lte", "AT^LTE_LOCK", "lte_lock" }, { "nr", "AT^NR5G_LOCK", "nr_lock" } };
			json_object *rats = json_object_object_get(current, "cell_lock_rats");

			if (lock && lk_pci < 0)
				return fail(why, size, "T99W175: the lock needs the cell's PCI as well as its channel", steps);
			for (k = 0; k < sizeof(locks) / sizeof(locks[0]); k++) {
				const char *was = rollback ? json_object_get_string(json_object_object_get(raw, locks[k].key)) : NULL;

				if (!listed(rats, locks[k].rat))
					continue;
				if (rollback && was && *was)
					snprintf(buf, sizeof(buf), "%s=%s", locks[k].cmd, was);
				else if (!rollback && lock && !strcmp(lk_rat, locks[k].rat))
					snprintf(buf, sizeof(buf), "%s=%ld,%ld", locks[k].cmd, lk_pci, lk_arfcn);
				else
					snprintf(buf, sizeof(buf), "%s", locks[k].cmd);
				json_object_array_add(steps, step(buf, NULL));
			}
			json_object_array_add(steps, step("AT+CFUN=1,1", NULL));
		}
	} else if (!strcmp(via, "at") && dialect && !strcmp(dialect, "fibocom-mtk")) {
		/*
		 * AT+GTACT=<rat>,<pref1>,<pref2>[,<codes>]: codes UMTS n, LTE 100+n, NR 500+n / 5000+n; "0" for
		 * the factory set, the one way that lets the modem gather its carriers again (a live
		 * FM350). A mode alone goes without codes. A RW350-GL-16 answers
		 * "2,3," and ignores a write of that form: the second preference repeats the first (owner's
		 * finding 18.09.2026, same source). The cell lock (LTE only) needs flight mode, which drops the
		 * carriers to one band: the bands are written again after it.
		 */
		json_object *have = rollback ? raw : json_object_object_get(current, "raw");
		const char *line = json_object_get_string(json_object_object_get(have, "gtact"));
		const char *rats = json_object_get_string(json_object_object_get(have, "rats"));
		char f[3][16] = { "", "", "" }, tail[400] = "", prefix[48] = "", codes[400] = "";
		const char *c;
		long cur_rat, new_rat;
		int n = 0;

		if (!line || !*line)
			return fail(why, size, "read the settings first: the modem did not answer AT+GTACT?", steps);
		if (preferred)
			return fail(why, size, "Fibocom keeps its own order of technologies: no preferred one", steps);
		for (c = line; n < 3; n++) {
			size_t l = strcspn(c, ",");

			snprintf(f[n], sizeof(f[n]), "%.*s", (int)(l < sizeof(f[n]) ? l : sizeof(f[n]) - 1), c);
			c += l;
			if (*c != ',')
				break;
			c++;
		}
		if (n == 3)
			snprintf(tail, sizeof(tail), "%s", c);
		cur_rat = strtol(f[0], NULL, 10);
		new_rat = cur_rat;
		if (allowed && !rollback) {
			static const char *const sets[][3] = { { "3g" }, { "4g" }, { "3g", "4g" }, { "5g" }, { "3g", "5g" },
							       { "4g", "5g" }, { "3g", "4g", "5g" } };
			static const long codes_of[] = { 1, 2, 4, 14, 16, 17, 20 };

			new_rat = -1;
			if (listed(allowed, "2g"))
				return fail(why, size, "Fibocom FM350: no 2G", steps);
			if (listed(allowed, "any")) {
				/* automatic: 20 on a 5G module, 10 on the 4G-only ones */
				new_rat = !rats || code_listed(rats, 20) ? 20 : code_listed(rats, 10) ? 10 : 20;
			} else {
				for (k = 0; k < sizeof(sets) / sizeof(sets[0]) && new_rat < 0; k++) {
					json_object *a = json_object_new_array();
					size_t m;

					for (m = 0; m < 3 && sets[k][m]; m++)
						json_object_array_add(a, json_object_new_string(sets[k][m]));
					if (same_set(a, allowed))
						new_rat = codes_of[k];
					json_object_put(a);
				}
			}
			/* the page sends the technologies ticked: 3G + 4G on a 4G-only module in automatic is that automatic */
			if (new_rat == 4 && cur_rat == 10)
				new_rat = 10;
			if (new_rat < 0 || (rats && *rats && !code_listed(rats, new_rat)))
				return fail(why, size, "the modem does not offer this combination of technologies", steps);
		}
		/* the mode part of a write with codes */
		if (new_rat == cur_rat && f[1][0])
			snprintf(prefix, sizeof(prefix), "%ld,%s,%s", cur_rat, f[1], f[2][0] ? f[2] : f[1]);
		else if (new_rat == 20 || new_rat == 17)
			snprintf(prefix, sizeof(prefix), "%ld,6,3", new_rat);
		else if (new_rat == 16)
			snprintf(prefix, sizeof(prefix), "%ld,6,2", new_rat);
		else if (new_rat == 4)
			snprintf(prefix, sizeof(prefix), "%ld,3,2", new_rat);
		else if (new_rat == 1 || new_rat == 2 || new_rat == 14)
			snprintf(prefix, sizeof(prefix), "%ld,%d,%d", new_rat, new_rat == 1 ? 2 : new_rat == 2 ? 3 : 6,
				 new_rat == 1 ? 2 : new_rat == 2 ? 3 : 6);
		/* the codes: asked, the factory set, or what was there (after flight mode, or on the way back) */
		if (bands_any) {
			snprintf(codes, sizeof(codes), "0");
		} else if (bands && !rollback) {
			static const char *const kinds[] = { "utran", "eutran", "ngran" };
			size_t used = 0;

			for (k = 0; k < 3; k++)
				if (json_object_is_type(supported, json_type_array) && has_kind(supported, kinds[k]) &&
				    !has_kind(bands, kinds[k]))
					return fail(why, size, "keep at least one band of each kind the modem has (3G, LTE, NR)", steps);
			for (i = 0; i < json_object_array_length(bands) && used + 8 < sizeof(codes); i++) {
				char kind[16];
				long b;

				band_parts(json_object_get_string(json_object_array_get_idx(bands, i)), kind, sizeof(kind), &b);
				used += (size_t)snprintf(codes + used, sizeof(codes) - used, "%s%ld", used ? "," : "",
							 !strcmp(kind, "eutran") ? 100 + b : !strcmp(kind, "ngran") ? (b < 10 ? 500 + b : 5000 + b) : b);
			}
		} else if (rollback || lock_asked) {
			json_object *was = json_object_new_array();

			for (c = tail; *c; c = *c == ',' ? c + 1 : c) {
				char *e;

				gtact_band(was, strtol(c, &e, 10));
				c = e == c ? c + strcspn(c, ",") : e;
			}
			if (!json_object_array_length(was) ||
			    (json_object_is_type(supported, json_type_array) && same_set(was, supported)))
				snprintf(codes, sizeof(codes), "0");
			else
				snprintf(codes, sizeof(codes), "%s", tail);
			json_object_put(was);
		}
		if (lock_asked) {
			/* the pauses: 2 s in flight mode, 8 s to register before the bands */
			json_object_array_add(steps, step("AT+CFUN=4", NULL));
			json_object_array_add(steps, step(MM_AT_WAIT "2000", NULL));
			json_object_array_add(steps, step("AT+EMMCHLCK=0", NULL));
			if (lock) {
				if (lk_pci >= 0)
					snprintf(buf, sizeof(buf), "AT+EMMCHLCK=1,7,0,%ld,%ld,3", lk_arfcn, lk_pci);
				else
					snprintf(buf, sizeof(buf), "AT+EMMCHLCK=1,7,0,%ld,,3", lk_arfcn);
				json_object_array_add(steps, step(buf, NULL));
			}
			json_object_array_add(steps, step("AT+CFUN=1", NULL));
			json_object_array_add(steps, step(MM_AT_WAIT "8000", NULL));
		}
		if (codes[0]) {
			if (!prefix[0])	/* automatic (10) has no preferences to repeat: the manual's empty ones */
				snprintf(prefix, sizeof(prefix), "%ld,,", new_rat);
			snprintf(buf, sizeof(buf), "AT+GTACT=%s,%s", prefix, codes);
			json_object_array_add(steps, step(buf, NULL));
		} else if (allowed && new_rat != cur_rat) {
			/* the mode alone: the rat with its preferences where it has two technologies, else bare */
			if (new_rat == 20 || new_rat == 17 || new_rat == 16 || new_rat == 4)
				snprintf(buf, sizeof(buf), "AT+GTACT=%s", prefix);
			else
				snprintf(buf, sizeof(buf), "AT+GTACT=%ld", new_rat);
			json_object_array_add(steps, step(buf, NULL));
		}
	} else if (!strcmp(via, "at")) {
		snprintf(buf, sizeof(buf), "writing the %s dialect is not done yet: change it with the modem's own tools",
			 dialect ? dialect : "modem's");
		return fail(why, size, buf, steps);
	} else {
		return fail(why, size, "no owner can change this modem's radio settings", steps);
	}
	if (!json_object_array_length(steps))
		return fail(why, size, "nothing to change", steps);
	out = json_object_new_object();
	json_object_object_add(out, "steps", steps);
	return out;
}


/* ---- the serving cells over AT (r34) ---- */

const char *mm_cells_command(const char *dialect)
{
	if (dialect && !strcmp(dialect, "fibocom-mtk"))
		return "AT+GTCCINFO?";
	/* spec 26.1 step 5: on the engine's own AT port only (engine_at_port() refuses ModemManager's and QModem's) */
	if (dialect && (!strcmp(dialect, "quectel") || !strcmp(dialect, "quectel-ep06")))
		return "AT+QCAINFO";
	if (dialect && !strcmp(dialect, "simcom"))
		return "AT+CPSI?";
	if (dialect && !strcmp(dialect, "telit"))
		return "AT#RFSTS";
	if (dialect && !strcmp(dialect, "sierra"))
		return "AT!GSTATUS?";
	return dialect && (!strcmp(dialect, "foxconn-sdx") || !strcmp(dialect, "foxconn-t77")) ? "AT^DEBUG?" : NULL;
}

/* a measured value: "-94.3dBm", "10.0MHz", "23.8dB"; "NA", "-" and the like are nothing */
static bool measured(const char *v, double *out)
{
	char *end = NULL;
	double d;

	while (v && *v == ' ')
		v++;
	if (!v || !*v)
		return false;
	d = strtod(v, &end);
	if (end == v)
		return false;
	*out = (double)(long)(d * 10 + (d < 0 ? -0.5 : 0.5)) / 10;
	return true;
}

static void put_num(json_object *c, const char *key, const char *v, double lo, double hi)
{
	double d;

	if (measured(v, &d) && d >= lo && d <= hi)
		json_object_object_add(c, key, json_object_new_double(d));
}

static json_object *new_cell(json_object *cells, const char *type, const char *role)
{
	json_object *c = json_object_new_object();

	json_object_object_add(c, "type", json_object_new_string(type));
	json_object_object_add(c, "serving", json_object_new_boolean(1));
	json_object_object_add(c, "role", json_object_new_string(role));
	json_object_array_add(cells, c);
	return c;
}

/* SDX55: "pcell: lte_band:7 lte_band_width:10.0MHz", "channel:3400 pci:172", "lte_rsrp:-94.3dBm,rsrq:-9.7dB", the
 * nr_* lines for 5G; the section markers stand alone or glued to the first key; key:value tokens split by blanks and commas */
static json_object *cells_sdx(const char *raw)
{
	json_object *cells = json_object_new_array(), *cur = NULL, *nr = NULL;
	char buf[8192], *save = NULL, *t;
	long tac = -1, ci = -1;
	char mcc[8] = "", mnc[8] = "";

	snprintf(buf, sizeof(buf), "%s", raw);
	for (t = strtok_r(buf, " \t\r\n,", &save); t; t = strtok_r(NULL, " \t\r\n,", &save)) {
		char *colon, *key, *val;

		if (!strncmp(t, "pcell:", 6) || !strncmp(t, "scell:", 6)) {
			cur = new_cell(cells, "lte", t[0] == 'p' ? "primary" : "secondary");
			t += 6;
			if (!*t)
				continue;
		}
		if (!(colon = strchr(t, ':')))
			continue;
		*colon = '\0';
		key = t;
		val = colon + 1;
		if (!strcmp(key, "lte_tac"))
			tac = strtol(val, NULL, 10);
		else if (!strcmp(key, "lte_cell_id"))
			ci = strtol(val, NULL, 10);
		else if (!strcmp(key, "mcc"))
			snprintf(mcc, sizeof(mcc), "%.3s", val);
		else if (!strcmp(key, "mnc"))
			snprintf(mnc, sizeof(mnc), "%.3s", val);
		else if (!strncmp(key, "nr_", 3)) {
			if (!nr)
				nr = new_cell(cells, "5gnr", "nr");	/* the role once the LTE side is known */
			if (!strcmp(key, "nr_band"))
				json_object_object_add(nr, "band", json_object_new_int(atoi(val + (*val == 'n' || *val == 'N'))));
			else if (!strcmp(key, "nr_band_width"))
				put_num(nr, "bandwidth", val, 1, 400);
			else if (!strcmp(key, "nr_pci") && isdigit((unsigned char)*val))
				json_object_object_add(nr, "pci", json_object_new_string(val));
			else if (!strcmp(key, "nr_channel") && isdigit((unsigned char)*val))
				json_object_object_add(nr, "nrarfcn", json_object_new_int64(strtol(val, NULL, 10)));
			else if (!strcmp(key, "nr_rsrp"))
				put_num(nr, "rsrp", val, -156, -31);
			else if (!strcmp(key, "nr_rsrq"))
				put_num(nr, "rsrq", val, -43, 20);
			else if (!strcmp(key, "nr_snr") || !strcmp(key, "nr_sinr"))
				put_num(nr, "sinr", val, -23, 40);
		} else if (cur) {
			if (!strcmp(key, "lte_band"))
				json_object_object_add(cur, "band", json_object_new_int(atoi(val)));
			else if (!strcmp(key, "lte_band_width"))
				put_num(cur, "bandwidth", val, 1, 100);
			else if (!strcmp(key, "channel") && isdigit((unsigned char)*val))
				json_object_object_add(cur, "earfcn", json_object_new_int64(strtol(val, NULL, 10)));
			else if (!strcmp(key, "pci") && isdigit((unsigned char)*val))
				json_object_object_add(cur, "pci", json_object_new_string(val));
			else if (!strcmp(key, "lte_rsrp") || !strcmp(key, "rsrp"))
				put_num(cur, "rsrp", val, -156, -31);
			else if (!strcmp(key, "rsrq") || !strcmp(key, "lte_rsrq"))
				put_num(cur, "rsrq", val, -43, 20);
			else if (!strcmp(key, "lte_rssi") || !strcmp(key, "rssi"))
				put_num(cur, "rssi", val, -130, 0);
			else if (!strcmp(key, "lte_snr") || !strcmp(key, "snr"))
				put_num(cur, "sinr", val, -23, 40);
		}
	}
	/* an NR leg without its band is the firmware's placeholder, not a cell (field notes: AcT 13 and
	 * nr_ keys with -256 or NA while no NR is up) */
	if (nr && json_object_get_int(json_object_object_get(nr, "band")) <= 0) {
		size_t i;

		for (i = 0; i < json_object_array_length(cells); i++)
			if (json_object_array_get_idx(cells, i) == nr) {
				json_object_array_del_idx(cells, i, 1);
				break;
			}
		nr = NULL;
	}
	if (nr) {
		bool lte = false;
		size_t i;

		for (i = 0; i < json_object_array_length(cells); i++)
			lte = lte || !strcmp(json_object_get_string(json_object_object_get(json_object_array_get_idx(cells, i), "type")), "lte");
		json_object_object_add(nr, "role", json_object_new_string(lte ? "scg-primary" : "primary"));
	}
	if (json_object_array_length(cells)) {
		json_object *p = json_object_array_get_idx(cells, 0);
		char hex[16];

		if (tac > 0 && tac < 65535) {
			snprintf(hex, sizeof(hex), "%lX", tac);
			json_object_object_add(p, "tac", json_object_new_string(hex));
		}
		if (ci > 0 && ci < 0x10000000) {
			snprintf(hex, sizeof(hex), "%lX", ci);
			json_object_object_add(p, "ci", json_object_new_string(hex));
		}
		if (isdigit((unsigned char)mcc[0]) && isdigit((unsigned char)mnc[0])) {
			snprintf(hex, sizeof(hex), "%s%s", mcc, mnc);
			json_object_object_add(p, "plmn", json_object_new_string(hex));
		}
	}
	return cells;
}

/*
 * X20's secondary carriers (spec 26.1 step 2; the layouts as answered in
 * 4pda 994474 #1273, #40320 and a user report of 20.09.2026):
 *   ^DEBUG?   "SCell1:" then " BAND:3 BW:15.0MHz" / "EARFCN:1275 PCI:151" / "RSRP:-86.6dBm,RSRQ:-13.2dB" /
 *             "RSSI:-64.4dBm,SNR:5.2dB" (some firmwares print no SCell blocks at all)
 *   ^CA_INFO? "PCC info: Band is LTE_B1, Band_width is 10.0 MHz" / "SCC1 info: Band is LTE_B3, Band_width is 15.0 MHz",
 *             indented on some firmwares
 * The SCCn line and the SCelln block are the same carrier.
 */
#define T77_SCELLS 8

static void t77_token(json_object *c, const char *key, const char *val)
{
	if (!strcasecmp(key, "BAND") && isdigit((unsigned char)*val))
		json_object_object_add(c, "band", json_object_new_int(atoi(val)));
	else if (!strcasecmp(key, "BW"))
		put_num(c, "bandwidth", val, 1, 100);
	else if (!strcasecmp(key, "EARFCN") && isdigit((unsigned char)*val))
		json_object_object_add(c, "earfcn", json_object_new_int64(strtol(val, NULL, 10)));
	else if (!strcasecmp(key, "PCI") && isdigit((unsigned char)*val) && strtol(val, NULL, 10) <= 503) {
		char pci[16];
		snprintf(pci, sizeof(pci), "%ld", strtol(val, NULL, 10));
		json_object_object_add(c, "pci", json_object_new_string(pci));
	} else if (!strcasecmp(key, "RSRP"))
		put_num(c, "rsrp", val, -156, -31);
	else if (!strcasecmp(key, "RSRQ"))
		put_num(c, "rsrq", val, -43, 20);
	else if (!strcasecmp(key, "RSSI"))
		put_num(c, "rssi", val, -130, 0);
	else if (!strcasecmp(key, "SNR") || !strcasecmp(key, "SINR"))
		put_num(c, "sinr", val, -23, 40);
}

/* "PCC info: Band is LTE_B7, Band_width is 20.0 MHz": the band and the width into c (only what c lacks) */
static void t77_ca_line(json_object *c, const char *line)
{
	const char *b = strstr(line, "Band is LTE_B"), *w = strstr(line, "Band_width is ");

	if (b && isdigit((unsigned char)b[13]) && !json_object_object_get(c, "band"))
		json_object_object_add(c, "band", json_object_new_int(atoi(b + 13)));
	if (w && !json_object_object_get(c, "bandwidth"))
		put_num(c, "bandwidth", w + 14, 1, 100);
}

/* "RSRP: -98.4dBm rx_diversity: 3 (-98.0dBm,-100.4dBm,-256.0dBm,-256.0dBm)": the mask of the chains in use and their RSRP */
static void t77_chains(json_object *c, const char *val)
{
	const char *d = strstr(val, "rx_diversity:"), *p;
	json_object *chains;
	long mask;
	int i;

	if (!d)
		return;
	mask = strtol(d + 13, NULL, 10);
	p = strchr(d, '(');
	if (mask <= 0 || mask > 15 || !p)
		return;
	chains = json_object_new_array();
	for (i = 0, p++; i < 4 && p && *p && *p != ')'; i++) {
		json_object *ch = json_object_new_object();
		double v;

		json_object_object_add(ch, "chain", json_object_new_int(i));
		if ((mask >> i) & 1 && measured(p, &v) && v >= -156 && v <= -31)
			json_object_object_add(ch, "rsrp", json_object_new_double(v));
		else
			json_object_object_add(ch, "tuned", json_object_new_boolean(0));
		json_object_array_add(chains, ch);
		p = strpbrk(p, ",)");
		p = p && *p == ',' ? p + 1 : NULL;
	}
	if (json_object_array_length(chains))
		json_object_object_add(c, "chains", chains);
	else
		json_object_put(chains);
}

/* the secondaries after the primary: the SCell blocks of ^DEBUG? and the SCC lines of ^CA_INFO?, by number */
static void t77_secondaries(json_object *cells, json_object *pcell, const char *raw)
{
	json_object *sc[T77_SCELLS + 1] = { NULL };
	char buf[8192], *rest = buf, *line;
	int cur = 0, n;

	snprintf(buf, sizeof(buf), "%s", raw);
	while ((line = strsep(&rest, "\n"))) {
		char *p = line, *tok, *save = NULL;
		bool known = false;

		while (*p == ' ' || *p == '\t')
			p++;
		p[strcspn(p, "\r")] = '\0';
		if (!strncmp(p, "PCC info:", 9)) {
			t77_ca_line(pcell, p);
			cur = 0;
			continue;
		}
		if (sscanf(p, "SCC%d info:", &n) == 1 && n >= 1 && n <= T77_SCELLS) {
			if (!sc[n])
				sc[n] = json_object_new_object();
			t77_ca_line(sc[n], p);
			cur = 0;
			continue;
		}
		if (sscanf(p, "SCell%d:", &n) == 1 && n >= 1 && n <= T77_SCELLS) {
			if (!sc[n])
				sc[n] = json_object_new_object();
			cur = n;
			p = strchr(p, ':') + 1;
		} else if (!cur)
			continue;
		/* "BAND:3 BW:15.0MHz", "RSRP:-86.6dBm,RSRQ:-13.2dB": KEY:value tokens split by blanks and commas */
		for (tok = strtok_r(p, " \t,", &save); tok; tok = strtok_r(NULL, " \t,", &save)) {
			char *colon = strchr(tok, ':');

			if (!colon || colon == tok || !colon[1])
				continue;
			*colon = '\0';
			t77_token(sc[cur], tok, colon + 1);
			known = true;
		}
		if (!known && *p)
			cur = 0;	/* the block is over */
	}
	for (n = 1; n <= T77_SCELLS; n++) {
		if (!sc[n])
			continue;
		if (!json_object_object_get(sc[n], "band") && !json_object_object_get(sc[n], "earfcn")) {
			json_object_put(sc[n]);
			continue;
		}
		json_object_object_add(sc[n], "type", json_object_new_string("lte"));
		json_object_object_add(sc[n], "serving", json_object_new_boolean(1));
		json_object_object_add(sc[n], "role", json_object_new_string("secondary"));
		json_object_object_add(sc[n], "index", json_object_new_int(n));
		json_object_array_add(cells, sc[n]);
	}
}

/* X20 (T77W968): "KEY: value" lines; the primary is the first block (a second RAT follows after a blank line, the
 * SCell blocks after it); a block with EARFCN -1, TAC 65535 or STATUS ...DEREGISTERED is no cell */
static json_object *cells_t77(const char *raw)
{
	json_object *cells = json_object_new_array(), *c = json_object_new_object();
	char buf[8192], *rest = buf, *line;
	bool any = false, off = false;
	long enb = -1, cid = -1;

	snprintf(buf, sizeof(buf), "%s", raw);
	while ((line = strsep(&rest, "\n"))) {	/* strsep keeps the blank line that ends the block */
		char *colon = strchr(line, ':'), *val;
		const char *key = line;

		while (*key == ' ' || *key == '\t')
			key++;
		if (!*key || *key == '\r' || !strncmp(key, "SCell", 5) || !strncmp(key, "PCC info", 8) || !strncmp(key, "SCC", 3)) {
			if (any)
				break;
			continue;
		}
		if (!colon)
			continue;
		*colon = '\0';
		val = colon + 1;
		while (*val == ' ')
			val++;
		val[strcspn(val, "\r")] = '\0';
		if (!strcmp(key, "RAT") && strcmp(val, "LTE")) {
			off = true;
		} else if (!strcmp(key, "EARFCN(DL/UL)")) {
			long e = strtol(val, NULL, 10);
			if (isdigit((unsigned char)*val) && e >= 0) {
				json_object_object_add(c, "earfcn", json_object_new_int64(e));
				any = true;
			} else
				off = true;
		} else if (!strcmp(key, "BAND") && isdigit((unsigned char)*val)) {
			json_object_object_add(c, "band", json_object_new_int(atoi(val)));
		} else if (!strcmp(key, "BW")) {
			put_num(c, "bandwidth", val, 1, 100);
		} else if (!strcmp(key, "PLMN") && isdigit((unsigned char)*val)) {
			char plmn[8];
			int a = 0, b = 0;
			if (sscanf(val, "%d %d", &a, &b) == 2) {
				snprintf(plmn, sizeof(plmn), "%03d%02d", a, b);
				json_object_object_add(c, "plmn", json_object_new_string(plmn));
			}
		} else if (!strcmp(key, "TAC") && isdigit((unsigned char)*val)) {
			long tac = strtol(val, NULL, 10);
			char hex[16];
			if (tac > 0 && tac < 65535) {
				snprintf(hex, sizeof(hex), "%lX", tac);
				json_object_object_add(c, "tac", json_object_new_string(hex));
			}
		} else if (!strcmp(key, "eNB ID(PCI)")) {
			/* "112239-25(7)": eNodeB, the cell in it, (PCI) */
			char *paren = strchr(val, '(');
			if (sscanf(val, "%ld-%ld", &enb, &cid) != 2)
				enb = cid = -1;
			if (paren && isdigit((unsigned char)paren[1])) {
				char pci[16];
				snprintf(pci, sizeof(pci), "%ld", strtol(paren + 1, NULL, 10));
				json_object_object_add(c, "pci", json_object_new_string(pci));
			} else
				json_object_object_del(c, "pci");
		} else if (!strcmp(key, "RSRP")) {
			put_num(c, "rsrp", val, -156, -31);
			t77_chains(c, val);
		} else if (!strcmp(key, "RI") && val[0] >= '1' && val[0] <= '4' && !isdigit((unsigned char)val[1])) {
			/* the rank: the layers the primary carries now (MIMO 2 = two streams) */
			json_object_object_add(c, "mimo_layers", json_object_new_int(val[0] - '0'));
		} else if (!strcmp(key, "RSRQ")) {
			put_num(c, "rsrq", val, -43, 20);
		} else if (!strcmp(key, "RSSI")) {
			put_num(c, "rssi", val, -130, 0);
		} else if (!strcmp(key, "RS-SINR") || !strcmp(key, "RS-SNR")) {
			put_num(c, "sinr", val, -23, 40);
		} else if (!strcmp(key, "STATUS") && strstr(val, "DEREGISTERED")) {
			off = true;
		}
	}
	if (any && !off) {
		json_object_object_add(c, "type", json_object_new_string("lte"));
		json_object_object_add(c, "serving", json_object_new_boolean(1));
		json_object_object_add(c, "role", json_object_new_string("primary"));
		if (enb > 0 && cid >= 0 && cid < 256) {
			char hex[16];
			snprintf(hex, sizeof(hex), "%lX", enb * 256 + cid);
			json_object_object_add(c, "ci", json_object_new_string(hex));
		}
		json_object_array_add(cells, c);
		t77_secondaries(cells, c, raw);
	} else {
		json_object_put(c);
	}
	return cells;
}

/*
 * Fibocom FM350 +GTCCINFO (its AT manual 11.1.15, live units and 4pda 1057776 answers):
 *   1,<rat>,<mcc>,<mnc>,<tac>,<ci>,<arfcn>,<pci>,<band>,<bw>,<sinr>,<rxlev>,<rsrp>,<rsrq>  the serving cell
 *   2,<rat>,,,<tac>,<ci>,<arfcn>,<pci>,<bw>,<rxlev>,<rsrp>,<rsrq>                         a neighbour
 * rat 4 LTE, 9 NR. Indexes: LTE rsrp idx-141, rsrq idx/2-20, sinr as is; NR rsrp idx-157, rsrq idx/2-43.5,
 * sinr idx/2-23.5; 255 is nothing. The band (serving only, connected only) as 1xx / 50xx; a neighbour's from
 * its channel. Some firmwares print a neighbour's EARFCN ten times over (18020 for 1802, 4pda 1057776).
 */
static void gtcc_level(json_object *c, const char *key, const char *v, double scale, double base, long hi)
{
	char *end;
	long n = strtol(v ? v : "", &end, 10);

	if (v && end != v && n >= 0 && n <= hi && n != 255)
		json_object_object_add(c, key, json_object_new_double(n * scale + base));
}

static json_object *cells_gtcc(const char *raw)
{
	json_object *cells = json_object_new_array();
	const char *line = raw;
	long serving_lte[8];
	int known = 0, pass;

	/* the serving channels first: a neighbour's channel ten times over is told by them */
	for (pass = 0; pass < 2; pass++)
		for (line = raw; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
			char buf[256], num[24], *f[16] = { NULL }, *p, *q;
			int n = 0, i;
			long idx, rat, arfcn, pci, band = 0;
			bool srv;
			json_object *c;

			snprintf(buf, sizeof(buf), "%.*s", (int)strcspn(line, "\r\n"), line);
			if (buf[0] != '1' && buf[0] != '2')
				continue;
			for (p = buf; n < 16; p = q + 1) {
				f[n++] = p;
				if (!(q = strchr(p, ',')))
					break;
				*q = '\0';
			}
			idx = strtol(f[0], NULL, 10);
			rat = n > 1 ? strtol(f[1], NULL, 10) : -1;
			srv = idx == 1;
			if ((idx != 1 && idx != 2) || (rat != 4 && rat != 9) || n < (srv ? 14 : 12) || (pass == 0) != srv)
				continue;
			arfcn = strtol(f[6], &p, 10);
			if (p == f[6] || arfcn < 0)
				continue;
			pci = strtol(f[7], &p, 10);
			if (p == f[7] || pci < 0 || pci > (rat == 9 ? 1007 : 503))
				continue;
			if (rat == 4 && srv && known < 8)
				serving_lte[known++] = arfcn;
			if (rat == 4 && !srv && arfcn >= 10 && arfcn % 10 == 0) {
				bool same = false, tenth = false;

				for (i = 0; i < known; i++) {
					same = same || serving_lte[i] == arfcn;
					tenth = tenth || serving_lte[i] == arfcn / 10;
				}
				if (!same && (tenth || (!mm_mmgr_lte_band(arfcn) && mm_mmgr_lte_band(arfcn / 10))))
					arfcn /= 10;
			}
			c = json_object_new_object();
			json_object_object_add(c, "type", json_object_new_string(rat == 9 ? "5gnr" : "lte"));
			json_object_object_add(c, "serving", json_object_new_boolean(srv));
			json_object_object_add(c, "role", json_object_new_string(srv ? "primary" : "neighbour"));
			json_object_object_add(c, rat == 9 ? "nrarfcn" : "earfcn", json_object_new_int64(arfcn));
			snprintf(num, sizeof(num), "%ld", pci);
			json_object_object_add(c, "pci", json_object_new_string(num));
			if (srv) {
				band = strtol(f[8], &p, 10);
				band = p == f[8] ? 0 : rat == 4 ? (band > 100 && band < 200 ? band - 100 : 0) :
				       band >= 5000 ? band - 5000 : band >= 500 && band < 600 ? band - 500 : 0;
				if (*f[4] && strspn(f[4], "0123456789abcdefABCDEF") == strlen(f[4]))
					json_object_object_add(c, "tac", json_object_new_string(f[4]));
				if (*f[5] && strspn(f[5], "0123456789abcdefABCDEF") == strlen(f[5]))
					json_object_object_add(c, "ci", json_object_new_string(f[5]));
				if (*f[2] && *f[3]) {
					char plmn[16];

					snprintf(plmn, sizeof(plmn), "%s%0*ld", f[2], (int)(strlen(f[3]) < 2 ? 2 : strlen(f[3])), strtol(f[3], NULL, 10));
					json_object_object_add(c, "plmn", json_object_new_string(plmn));
				}
			}
			if (!band)
				band = rat == 9 ? mm_mmgr_nr_band(arfcn) : mm_mmgr_lte_band(arfcn);
			if (band > 0)
				json_object_object_add(c, "band", json_object_new_int64(band));
			i = srv ? 12 : 10;	/* rsrp's field */
			gtcc_level(c, "rsrp", f[i], 1, rat == 9 ? -157 : -141, rat == 9 ? 157 : 140);
			gtcc_level(c, "rsrq", f[i + 1], 0.5, rat == 9 ? -43.5 : -20, rat == 9 ? 127 : 40);
			if (srv && rat == 9)
				gtcc_level(c, "sinr", f[10], 0.5, -23.5, 127);
			else if (srv) {
				long v = strtol(f[10], &p, 10);

				if (p != f[10] && v >= -100 && v <= 100 && v != 255)
					json_object_object_add(c, "sinr", json_object_new_double((double)v));
			}
			json_object_array_add(cells, c);
		}
	return cells;
}

/*
 * Quectel AT+QCAINFO (spec 26.1 step 5; the layout of five live
 * dumps, 4pda 994474 #1055 and 1106442 #487, #2545):
 *   +QCAINFO: "PCC",<earfcn>,<bw>,"LTE BAND 7",<state>,<pci>,<rsrp>,<rsrq>,<rssi>,<sinr>
 *   +QCAINFO: "SCC",<earfcn>,<bw>,"LTE BAND 3",<state>,<pci>,<rsrp>,<rsrq>,<rssi>,<sinr>[,<ul_cfg>,<ul_bw>,<ul_earfcn>]
 *   +QCAINFO: "SCC",<arfcn>,<bw>,"NR5G BAND 78",<pci>                       an EN-DC leg
 * LTE widths in resource blocks (6 = 1.4 MHz … 100 = 20 MHz), NR as an index (0 = 5 MHz … 12 = 100, 13 = 200, 14 = 400).
 * An LTE SCC in state 0 or 1 is set up but not used now. The tail after the SINR is dashes on some firmwares.
 */
static bool qca_num(const char *v, long lo, long hi, long *out)
{
	char *end;
	long n;

	while (v && *v == ' ')
		v++;
	n = strtol(v ? v : "", &end, 10);
	if (!v || end == v || (*end && *end != '\r' && *end != ' ') || n < lo || n > hi)
		return false;
	*out = n;
	return true;
}

static json_object *cells_qca(const char *raw)
{
	static const double nr_bw[] = { 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100, 200, 400 };
	json_object *cells = json_object_new_array();
	char buf[4096], *rest = buf, *line;
	bool primary = false;

	snprintf(buf, sizeof(buf), "%s", raw);
	while ((line = strsep(&rest, "\n"))) {
		char *f[16] = { NULL }, *p, *q, num[16];
		int n = 0;
		long v, earfcn;
		bool pcc, nr;
		json_object *c;

		p = strstr(line, "+QCAINFO:");
		if (!p)
			continue;
		p += 9;
		while (*p == ' ')
			p++;
		for (; n < 16; p = q + 1) {
			f[n++] = p;
			if (!(q = strchr(p, ',')))
				break;
			*q = '\0';
		}
		if (n < 4)
			continue;
		pcc = !strncasecmp(f[0], "\"PCC\"", 5);
		if (!pcc && strncasecmp(f[0], "\"SCC\"", 5))
			continue;
		nr = strstr(f[3], "NR5G BAND") != NULL;
		if ((!nr && !strstr(f[3], "LTE BAND")) || !qca_num(f[1], 0, 3279165, &earfcn))
			continue;
		c = json_object_new_object();
		json_object_object_add(c, "type", json_object_new_string(nr ? "5gnr" : "lte"));
		json_object_object_add(c, "serving", json_object_new_boolean(1));
		json_object_object_add(c, "role", json_object_new_string(pcc ? "primary" : nr && primary ? "scg-primary" : "secondary"));
		json_object_object_add(c, nr ? "nrarfcn" : "earfcn", json_object_new_int64(earfcn));
		v = atoi(strstr(f[3], "BAND") + 5);
		if (v > 0)
			json_object_object_add(c, "band", json_object_new_int((int)v));
		if (nr && qca_num(f[2], 0, 14, &v))
			json_object_object_add(c, "bandwidth", json_object_new_double(nr_bw[v]));
		else if (!nr && qca_num(f[2], 6, 100, &v))
			json_object_object_add(c, "bandwidth", json_object_new_double(v == 6 ? 1.4 : v / 5.0));
		if (nr) {
			if (n == 5 && qca_num(f[4], 0, 1007, &v)) {
				snprintf(num, sizeof(num), "%ld", v);
				json_object_object_add(c, "pci", json_object_new_string(num));
			}
		} else if (n >= 6) {
			if (!pcc && qca_num(f[4], 0, 2, &v) && v < 2)
				json_object_object_add(c, "state", json_object_new_string("deactivated"));
			if (qca_num(f[5], 0, 503, &v)) {
				snprintf(num, sizeof(num), "%ld", v);
				json_object_object_add(c, "pci", json_object_new_string(num));
			}
			if (n >= 7 && qca_num(f[6], -156, -31, &v))
				json_object_object_add(c, "rsrp", json_object_new_double((double)v));
			if (n >= 8 && qca_num(f[7], -43, 20, &v))
				json_object_object_add(c, "rsrq", json_object_new_double((double)v));
			if (n >= 9 && qca_num(f[8], -130, 0, &v))
				json_object_object_add(c, "rssi", json_object_new_double((double)v));
			if (n >= 10 && qca_num(f[9], -23, 40, &v))
				json_object_object_add(c, "sinr", json_object_new_double((double)v));
		}
		primary = primary || pcc;
		json_object_array_add(cells, c);
	}
	/* no PCC (idle, or «OK» alone: 4pda 994474 #1912): nothing */
	if (!primary) {
		json_object_put(cells);
		return json_object_new_array();
	}
	return cells;
}

/* the comma fields after "<prefix>" on its line, blanks trimmed; n of them */
static int fields_after(const char *raw, const char *prefix, char *buf, size_t size, char **f, int max)
{
	char *p, *q;
	int n = 0;

	if (!line_after(raw, prefix, buf, size))
		return 0;
	for (p = buf; n < max; p = q + 1) {
		while (*p == ' ')
			p++;
		f[n++] = p;
		if (!(q = strchr(p, ',')))
			break;
		*q = '\0';
		for (char *e = q - 1; e >= p && *e == ' '; e--)
			*e = '\0';
	}
	return n;
}

static void put_int(json_object *c, const char *key, const char *v, long lo, long hi)
{
	char *end = NULL;
	long n;

	if (!v || !*v)
		return;
	n = strtol(v, &end, 10);
	if (end != v && !*end && n >= lo && n <= hi)
		json_object_object_add(c, key, json_object_new_int64(n));
}

static void put_tenth(json_object *c, const char *key, const char *v, double lo, double hi)
{
	char *end = NULL;
	long n;

	if (!v || !*v)
		return;
	n = strtol(v, &end, 10);
	if (end != v && !*end && n / 10.0 >= lo && n / 10.0 <= hi)
		json_object_object_add(c, key, json_object_new_double(n / 10.0));
}

/*
 * SIMCom +CPSI (SIM7600/SIM8200 AT manuals; the layout is per RAT, read the RAT first: a GSM line's RSSI is no band):
 *   LTE:      +CPSI: LTE,Online,250-01,0x1A2B,12345678,123,EUTRAN-BAND3,1300,5,5,-94,-1050,-750,15
 *             (RAT, state, PLMN, TAC, cell, PCI, band, EARFCN, DL bw, UL bw, RSRQ, RSRP, RSSI in tenths, SNR)
 *   NR5G_SA:  +CPSI: NR5G_SA,Online,250-01,0x1A2B,123456789,501,NR5G_BAND78,627264,-950,-110,150
 *             (… PCI, band, NR-ARFCN, RSRP, RSRQ, SNR in tenths)
 *   NR5G_NSA: +CPSI: NR5G_NSA,501,NR5G_BAND78,627264,-950,-110,150 (the SCG beside the LTE line)
 *   WCDMA, GSM: no cell of ours (the page reads the 2G/3G signal from elsewhere)
 */
static json_object *cells_cpsi(const char *raw)
{
	json_object *cells = json_object_new_array(), *c;
	char buf[512], *f[16];
	const char *p;
	int n;

	for (p = raw; (p = strstr(p, "+CPSI:")); p += 6) {
		n = fields_after(p, "+CPSI:", buf, sizeof(buf), f, 16);
		if (n >= 13 && !strcmp(f[0], "LTE") && !strncmp(f[6], "EUTRAN-BAND", 11)) {
			c = new_cell(cells, "lte", "primary");
			put_int(c, "band", f[6] + 11, 1, 255);
			put_int(c, "earfcn", f[7], 0, 262143);
			if (*f[5])
				json_object_object_add(c, "pci", json_object_new_string(f[5]));
			put_tenth(c, "rsrq", f[10], -43, 20);
			put_tenth(c, "rsrp", f[11], -156, -31);
			put_tenth(c, "rssi", f[12], -130, 0);
			if (n >= 14)
				put_int(c, "sinr", f[13], -23, 40);
		} else if (n >= 11 && !strcmp(f[0], "NR5G_SA") && !strncmp(f[6], "NR5G_BAND", 9)) {
			c = new_cell(cells, "5gnr", "primary");
			put_int(c, "band", f[6] + 9, 1, 999);
			put_int(c, "nrarfcn", f[7], 0, 3279165);
			if (*f[5])
				json_object_object_add(c, "pci", json_object_new_string(f[5]));
			put_tenth(c, "rsrp", f[8], -156, -31);
			put_tenth(c, "rsrq", f[9], -43, 20);
			put_tenth(c, "sinr", f[10], -23, 40);
		} else if (n >= 7 && !strcmp(f[0], "NR5G_NSA") && !strncmp(f[2], "NR5G_BAND", 9)) {
			c = new_cell(cells, "5gnr", "scg-primary");
			put_int(c, "band", f[2] + 9, 1, 999);
			put_int(c, "nrarfcn", f[3], 0, 3279165);
			if (*f[1])
				json_object_object_add(c, "pci", json_object_new_string(f[1]));
			put_tenth(c, "rsrp", f[4], -156, -31);
			put_tenth(c, "rsrq", f[5], -43, 20);
			put_tenth(c, "sinr", f[6], -23, 40);
		}
	}
	return cells;
}

/*
 * Telit #RFSTS on LTE (LE910Cx / LM9x0 / FN9x0 AT references):
 *   #RFSTS: "250 01",1300,-95,-65,-11,4E21,255,,128,19,0,0BE4D01,"<IMSI>","MTS",3,3,...
 *   (PLMN, EARFCN, RSRP, RSSI, RSRQ, TAC, RAC, TXPWR, DRX, MM, RRC, CID, IMSI, network, SD, the active band, ...)
 * The IMSI field is skipped, never kept. WCDMA's layout puts the PSC (positive) where LTE has the RSRP: no cell then.
 */
static json_object *cells_rfsts(const char *raw)
{
	json_object *cells = json_object_new_array(), *c;
	char buf[512], *f[24];
	int n = fields_after(raw, "#RFSTS:", buf, sizeof(buf), f, 24);

	if (n >= 16 && *f[2] == '-' && *f[3] == '-' && *f[4] == '-') {
		c = new_cell(cells, "lte", "primary");
		put_int(c, "earfcn", f[1], 0, 262143);
		put_int(c, "rsrp", f[2], -156, -31);
		put_int(c, "rssi", f[3], -130, 0);
		put_int(c, "rsrq", f[4], -43, 20);
		put_int(c, "band", f[15], 1, 255);
	}
	return cells;
}

/* the number after "<key>" on the text (Sierra's !GSTATUS puts two key: value pairs on a line, tab apart) */
static const char *gstatus_val(const char *raw, const char *key, char *buf, size_t size)
{
	const char *p = strstr(raw, key);
	size_t n;

	if (!p)
		return NULL;
	p += strlen(key);
	p += strspn(p, " \t");
	n = strcspn(p, " \t\r\n");
	if (!n || n >= size)
		return NULL;
	memcpy(buf, p, n);
	buf[n] = '\0';
	return buf;
}

/* a !GSTATUS value of the LTE leg: the key not inside an «NR5G …» key of the same name (an EM9190 in ENDC puts
 * «NR5G RSRP (dBm):» below the LTE lines, and an LTE «RSRP (dBm):» it does not have) */
static const char *gstatus_lte(const char *raw, const char *key, char *buf, size_t size)
{
	const char *p = raw;

	while ((p = strstr(p, key))) {
		if (p - raw < 5 || strncmp(p - 5, "NR5G ", 5))
			return gstatus_val(p, key, buf, size);
		p += strlen(key);
	}
	return NULL;
}

/*
 * Sierra !GSTATUS? on LTE (EM74xx/MC74xx AT reference):
 *   System mode:   LTE        PS state:    Attached
 *   LTE band:      B3         LTE bw:      20 MHz
 *   LTE Rx chan:   1300       LTE Tx chan: 19300
 *   PCC RxM RSSI:  -65        RSRP (dBm):  -95
 *   RSRQ (dB):     -11.0      Cell ID:     ...
 *   SINR (dB):     12.4
 * and a secondary carrier, when aggregated, as «LTE Scell band:», «LTE Scell bw:», «LTE Scell chan:».
 * Field replies (EM7565, EM9190, EM9291) put the levels per antenna instead: «PCC RxM RSSI: -55  PCC RxM RSRP: -82» or
 * «PCC Rx0 RSRP: -113.3» — the main antenna's (RxM, Rx0) is the cell's; an EM9190 in ENDC adds the 5G leg:
 * «NR5G band: n78», «NR5G bw: 100 MHz», «NR5G Rx chan: 636666», «NR5G RSRP (dBm): -85.0», «NR5G RSRQ (dB):», «NR5G SINR (dB):».
 */
static json_object *cells_gstatus(const char *raw)
{
	json_object *cells = json_object_new_array(), *c;
	char v[32];
	const char *x;

	if (!(x = gstatus_val(raw, "LTE band:", v, sizeof(v))) || (*x != 'B' && *x != 'b'))
		return cells;
	c = new_cell(cells, "lte", "primary");
	put_int(c, "band", x + 1, 1, 255);
	if ((x = gstatus_val(raw, "LTE bw:", v, sizeof(v))))
		put_num(c, "bandwidth", x, 1, 100);
	if ((x = gstatus_val(raw, "LTE Rx chan:", v, sizeof(v))))
		put_int(c, "earfcn", x, 0, 262143);
	if ((x = gstatus_val(raw, "PCC RxM RSSI:", v, sizeof(v))) || (x = gstatus_val(raw, "PCC Rx0 RSSI:", v, sizeof(v))))
		put_num(c, "rssi", x, -130, 0);
	if ((x = gstatus_val(raw, "PCC RxM RSRP:", v, sizeof(v))) || (x = gstatus_val(raw, "PCC Rx0 RSRP:", v, sizeof(v))) ||
	    (x = gstatus_lte(raw, "RSRP (dBm):", v, sizeof(v))))
		put_num(c, "rsrp", x, -156, -31);
	if ((x = gstatus_lte(raw, "RSRQ (dB):", v, sizeof(v))))
		put_num(c, "rsrq", x, -43, 20);
	if ((x = gstatus_lte(raw, "SINR (dB):", v, sizeof(v))))
		put_num(c, "sinr", x, -23, 40);
	if ((x = gstatus_val(raw, "LTE Scell band:", v, sizeof(v))) && (*x == 'B' || *x == 'b')) {
		c = new_cell(cells, "lte", "secondary");
		put_int(c, "band", x + 1, 1, 255);
		if ((x = gstatus_val(raw, "LTE Scell bw:", v, sizeof(v))))
			put_num(c, "bandwidth", x, 1, 100);
		if ((x = gstatus_val(raw, "LTE Scell chan:", v, sizeof(v))))
			put_int(c, "earfcn", x, 0, 262143);
	}
	if ((x = gstatus_val(raw, "NR5G band:", v, sizeof(v))) && (*x == 'n' || *x == 'N') && isdigit((unsigned char)x[1])) {
		c = new_cell(cells, "5gnr", "scg-primary");
		put_int(c, "band", x + 1, 1, 1024);
		if ((x = gstatus_val(raw, "NR5G bw:", v, sizeof(v))))
			put_num(c, "bandwidth", x, 1, 400);
		if ((x = gstatus_val(raw, "NR5G Rx chan:", v, sizeof(v))))
			put_int(c, "nrarfcn", x, 0, 3279165);
		if ((x = gstatus_val(raw, "NR5G RSRP (dBm):", v, sizeof(v))))
			put_num(c, "rsrp", x, -156, -31);
		if ((x = gstatus_val(raw, "NR5G RSRQ (dB):", v, sizeof(v))))
			put_num(c, "rsrq", x, -43, 20);
		if ((x = gstatus_val(raw, "NR5G SINR (dB):", v, sizeof(v))))
			put_num(c, "sinr", x, -23, 40);
	}
	return cells;
}

/* EP06: QENG serving and neighbours. The cell lock selector needs
 * actual EARFCN+PCI, not the LTE band or CGI from another RAT. */
static int ep06_band(long earfcn)
{
	static const struct { long lo, hi; int band; } b[] = {
		{ 0, 599, 1 }, { 600, 1199, 2 }, { 1200, 1949, 3 },
		{ 1950, 2399, 4 }, { 2400, 2649, 5 }, { 2650, 2749, 6 },
		{ 2750, 3449, 7 }, { 3450, 3799, 8 }, { 3800, 4149, 9 },
		{ 6150, 6449, 20 }, { 9210, 9659, 28 }, { 9920, 10359, 32 },
		{ 37750, 38249, 38 }, { 38650, 39649, 40 },
		{ 39650, 41589, 41 }
	};
	size_t i;
	for (i = 0; i < sizeof(b) / sizeof(b[0]); i++)
		if (earfcn >= b[i].lo && earfcn <= b[i].hi)
			return b[i].band;
	return 0;
}

static json_object *cells_ep06(const char *raw)
{
	json_object *cells = cells_qca(raw), *primary = NULL;
	const char *p = raw;
	long serving_earfcn = -1, serving_pci = -1;
	size_t i;

	for (i = 0; i < json_object_array_length(cells); i++) {
		json_object *c = json_object_array_get_idx(cells, i);
		if (!strcmp(json_object_get_string(json_object_object_get(c, "role")), "primary")) {
			const char *pci = json_object_get_string(json_object_object_get(c, "pci"));
			primary = c;
			serving_earfcn = json_object_get_int64(json_object_object_get(c, "earfcn"));
			serving_pci = pci ? strtol(pci, NULL, 10) : -1;
			break;
		}
	}
	/* A QENG line is a distinct record. Never consume command echo as data. */
	while (p && (p = strstr(p, "+QENG:"))) {
		char buf[768], *f[24] = { NULL };
		long earfcn, pci, value;
		int n = fields_after(p, "+QENG:", buf, sizeof(buf), f, 24);
		bool serving = n >= 17 && !strcmp(f[0], "\"servingcell\"") && !strcmp(f[2], "\"LTE\"");
		bool neighbour = n >= 7 && (!strcmp(f[0], "\"neighbourcell intra\"") ||
					     !strcmp(f[0], "\"neighbourcell inter\"")) &&
					     !strcmp(f[1], "\"LTE\"");
		json_object *c;
		p += 6;
		if (!serving && !neighbour)
			continue;
		if (!qca_num(serving ? f[8] : f[2], 0, 262143, &earfcn) ||
		    !qca_num(serving ? f[7] : f[3], 0, 503, &pci))
			continue;
		if (neighbour && serving_earfcn == earfcn && serving_pci == pci)
			continue;
		if (serving) {
			if (primary && (serving_earfcn != earfcn || serving_pci != pci))
				continue; /* QCA primary and QENG from different snapshots */
			c = primary ? primary : new_cell(cells, "lte", "primary");
			if (!primary) {
				primary = c;
				json_object_object_add(c, "earfcn", json_object_new_int64(earfcn));
				serving_earfcn = earfcn;
				serving_pci = pci;
				json_object_object_add(c, "pci", json_object_new_string(f[7]));
			}
			put_int(c, "band", f[9], 1, 255);
			if (!json_object_object_get(c, "rsrp"))
				put_int(c, "rsrp", f[13], -156, -31);
			if (!json_object_object_get(c, "rsrq"))
				put_int(c, "rsrq", f[14], -43, 20);
			if (!json_object_object_get(c, "rssi"))
				put_int(c, "rssi", f[15], -130, 0);
			if (!json_object_object_get(c, "sinr"))
				put_int(c, "sinr", f[16], -23, 40); /* already dB */
			if (!json_object_object_get(c, "bandwidth") &&
			    qca_num(f[11], 0, 5, &value)) {
				static const double mhz[] = { 1.4, 3, 5, 10, 15, 20 };
				json_object_object_add(c, "bandwidth", json_object_new_double(mhz[value]));
			}
			continue;
		}
		c = json_object_new_object();
		json_object_object_add(c, "type", json_object_new_string("lte"));
		json_object_object_add(c, "serving", json_object_new_boolean(false));
		json_object_object_add(c, "role", json_object_new_string("neighbour"));
		json_object_object_add(c, "earfcn", json_object_new_int64(earfcn));
		json_object_object_add(c, "pci", json_object_new_string(f[3]));
		if ((value = ep06_band(earfcn)) > 0)
			json_object_object_add(c, "band", json_object_new_int((int)value));
		put_int(c, "rsrq", f[4], -43, 20); /* RSRQ before RSRP */
		put_int(c, "rsrp", f[5], -156, -31);
		put_int(c, "rssi", f[6], -130, 0);
		json_object_array_add(cells, c);
	}
	return cells;
}

json_object *mm_cells_from_at(const char *dialect, const char *raw)
{
	json_object *cells, *o;

	if (!mm_cells_command(dialect) || !raw)
		return NULL;
	if (!strcmp(dialect, "fibocom-mtk"))
		cells = cells_gtcc(raw);
	else if (!strcmp(dialect, "quectel-ep06"))
		cells = cells_ep06(raw);
	else if (!strcmp(dialect, "quectel"))
		cells = cells_qca(raw);
	else if (!strcmp(dialect, "simcom"))
		cells = cells_cpsi(raw);
	else if (!strcmp(dialect, "telit"))
		cells = cells_rfsts(raw);
	else if (!strcmp(dialect, "sierra"))
		cells = cells_gstatus(raw);
	else
		cells = !strcmp(dialect, "foxconn-t77") || strstr(raw, "EARFCN(DL/UL)") ? cells_t77(raw) : cells_sdx(raw);
	if (!json_object_array_length(cells)) {
		json_object_put(cells);
		return NULL;
	}
	o = json_object_new_object();
	json_object_object_add(o, "cells", cells);
	/* Sierra's !GSTATUS? carries the modem's temperature too (a field report): its heat with no command of its own */
	if (!strcmp(dialect, "sierra")) {
		char v[32];
		const char *t = gstatus_val(raw, "Temperature:", v, sizeof(v));
		char *end;
		long c = t ? strtol(t, &end, 10) : -1000;

		if (t && end != t && c > -40 && c < 150) {
			json_object *heat = json_object_new_object();

			json_object_object_add(heat, "modem_c", json_object_new_int((int)c));
			json_object_object_add(o, "heat", heat);
		}
	}
	return o;
}

/*
 * EP06-E AT+QTEMP: comma-separated signed sensor readings (three or four,
 * depending on firmware). Ignore absent sensors and sentinels (255, -140,
 * -32768), and do not mistake the AT echo or ERROR for a temperature.
 * Return only the maximum valid sensor temperature.
 */
json_object *mm_quectel_temp_from_at(const char *raw)
{
	const char *p;
	long highest = -1000;

	if (!raw)
		return NULL;
	for (p = raw; (p = strstr(p, "+QTEMP:")); p += 7) {
		const char *q = p + 7;
		while (*q && *q != '\r' && *q != '\n') {
			char *end;
			long n;

			while (*q == ' ' || *q == '\t' || *q == ',')
				q++;
			if (!*q || *q == '\r' || *q == '\n')
				break;
			n = strtol(q, &end, 10);
			if (end == q || (*end != ',' && *end != '\r' && *end != '\n' &&
						    *end != '\0' && *end != ' ' && *end != '\t'))
				break;
			if (n > -40 && n < 150 && n > highest)
				highest = n;
			q = end;
			while (*q == ' ' || *q == '\t')
				q++;
			if (*q && *q != ',' && *q != '\r' && *q != '\n')
				break;
		}
	}
	if (highest == -1000)
		return NULL;
	json_object *heat = json_object_new_object();
	json_object_object_add(heat, "modem_c", json_object_new_int((int)highest));
	return heat;
}

/* ---- the modem's heat (2026-11-02) ---- */

/* the number after "<name>:" on a line of its own, in degrees: "TSENS: 45C" -> 45 */
static bool heat_line(const char *raw, const char *name, long *out)
{
	const char *p = raw;
	size_t n = strlen(name);

	while (p && *p) {
		while (*p == '\r' || *p == '\n' || *p == ' ')
			p++;
		if (!strncmp(p, name, n) && p[n] == ':') {
			char *end;
			long v = strtol(p + n + 1, &end, 10);

			if (end != p + n + 1 && v > -40 && v < 150) {
				*out = v;
				return true;
			}
		}
		p = strchr(p, '\n');
	}
	return false;
}

json_object *mm_heat_from_at(const char *temp, const char *therm)
{
	json_object *o = json_object_new_object();
	long v, max = -1;
	const char *p;

	if (temp && heat_line(temp, "TSENS", &v))
		json_object_object_add(o, "modem_c", json_object_new_int((int)v));
	if (temp && heat_line(temp, "PA", &v))
		json_object_object_add(o, "pa_c", json_object_new_int((int)v));
	/* every "... level: N" line, the highest N of 0-3 */
	for (p = therm; p && (p = strstr(p, "level")); p += 5) {
		const char *c = p + 5;
		char *end;

		while (*c == ' ')
			c++;
		if (*c != ':')
			continue;
		v = strtol(c + 1, &end, 10);
		if (end != c + 1 && v >= 0 && v <= 3 && v > max)
			max = v;
	}
	if (max >= 0)
		json_object_object_add(o, "throttle", json_object_new_int((int)max));
	if (!json_object_object_length(o)) {
		json_object_put(o);
		return NULL;
	}
	return o;
}

/* ---- the QMI mode preference read back (audit of r135, 2026-11-02) ---- */

static size_t pref_tokens(const char *s, const char *seps, char out[][16], size_t max)
{
	size_t n = 0;

	while (s && *s && n < max) {
		size_t l;

		s += strspn(s, seps);
		l = strcspn(s, seps);
		if (!l)
			break;
		snprintf(out[n], sizeof(out[n]), "%.*s", (int)(l < 15 ? l : 15), s);
		n++;
		s += l;
	}
	return n;
}

bool mm_qmi_pref_matches(const char *line, const char *want)
{
	char got[8][16], need[8][16];
	const char *q = line ? strstr(line, "Mode preference:") : NULL, *a, *b;
	char inner[128];
	size_t ng, nn, i, k;

	if (!q || !want || !(a = strchr(q, '\'')) || !(b = strchr(a + 1, '\'')))
		return false;
	snprintf(inner, sizeof(inner), "%.*s", (int)(b - a - 1), a + 1);
	ng = pref_tokens(inner, ", ", got, 8);
	nn = pref_tokens(want, "|", need, 8);
	if (!nn || ng != nn)
		return false;
	for (i = 0; i < nn; i++) {
		for (k = 0; k < ng && strcmp(need[i], got[k]); k++)
			;
		if (k == ng)
			return false;
	}
	return true;
}
