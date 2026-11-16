#define _GNU_SOURCE
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apn.h"

#define COLS 12
enum { C_MCCMNC, C_IMSI, C_ICCID, C_GID1, C_SPN, C_PROVIDER, C_APN, C_PRIO, C_USER, C_PASS, C_AUTH, C_IPTYPE };

static bool wild(const char *v)
{
	return !v || !*v || !strcmp(v, "-") || !strcmp(v, "*");
}

static bool given(const char *v)
{
	return v && *v;
}

/* digits must equal, an "x" in the pattern is any one */
static bool digits_match(const char *value, const char *pattern)
{
	size_t i, n = strlen(pattern);

	if (strlen(value) < n)
		return false;
	for (i = 0; i < n; i++)
		if (pattern[i] != 'x' && pattern[i] != 'X' && pattern[i] != value[i])
			return false;
	return true;
}

/* lower case for ASCII and Cyrillic in UTF-8, into out */
static void lower_utf8(const char *s, char *out, size_t size)
{
	size_t o = 0;
	const unsigned char *p = (const unsigned char *)s;

	while (*p && o + 2 < size) {
		if (p[0] == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F) {		/* А..П */
			out[o++] = (char)0xD0;
			out[o++] = (char)(p[1] + 0x20);
			p += 2;
		} else if (p[0] == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF) {	/* Р..Я */
			out[o++] = (char)0xD1;
			out[o++] = (char)(p[1] - 0x20);
			p += 2;
		} else if (p[0] == 0xD0 && p[1] == 0x81) {			/* Ё */
			out[o++] = (char)0xD1;
			out[o++] = (char)0x91;
			p += 2;
		} else {
			out[o++] = (char)tolower(*p++);
		}
	}
	out[o] = '\0';
}

/* an APN a netifd option can hold */
static bool sane_apn(const char *a)
{
	for (; a && *a; a++)
		if ((unsigned char)*a < 0x20 || *a == '"' || *a == ';' || *a == '\\' || *a == '\'' || *a == ' ')
			return false;
	return true;
}

/* split one line into TAB fields (in place); count */
static int fields_of(char *line, char **f)
{
	int n = 0;

	f[n++] = line;
	while (n < COLS && (line = strchr(line, '\t'))) {
		*line++ = '\0';
		f[n++] = line;
	}
	return n;
}

static json_object *result(const char *apn, const char *source, char **f)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "apn", json_object_new_string(apn));
	json_object_object_add(o, "source", json_object_new_string(source));
	if (f) {
		if (!wild(f[C_PROVIDER]))
			json_object_object_add(o, "provider", json_object_new_string(f[C_PROVIDER]));
		if (!wild(f[C_USER]))
			json_object_object_add(o, "username", json_object_new_string(f[C_USER]));
		if (!wild(f[C_PASS]))
			json_object_object_add(o, "password", json_object_new_string(f[C_PASS]));
		if (!wild(f[C_AUTH]))
			json_object_object_add(o, "auth", json_object_new_string(f[C_AUTH]));
		if (!wild(f[C_IPTYPE]))
			json_object_object_add(o, "ip_type", json_object_new_string(f[C_IPTYPE]));
	}
	return o;
}

/*
 * The most specific matching row. With an IMSI the network code must be its
 * prefix (the SIM's home, also for an MVNO); without one, the code given.
 */
static json_object *db_pick(const char *db, const struct mm_apn_sim *sim, const char *code, const char *source, long *scored)
{
	const char *line = db, *imsi = given(sim->imsi) && strspn(sim->imsi, "0123456789") == strlen(sim->imsi) ? sim->imsi : NULL;
	char spn[96] = "", gid1[40] = "";
	long best = -1, best_prio = 0;
	json_object *out = NULL;
	size_t i;

	if (!db || (!imsi && !given(code)))
		return NULL;
	if (given(sim->spn))
		lower_utf8(sim->spn, spn, sizeof(spn));
	for (i = 0; given(sim->gid1) && sim->gid1[i] && i + 1 < sizeof(gid1); i++)
		gid1[i] = (char)toupper((unsigned char)sim->gid1[i]);
	while (line && *line) {
		const char *end = strchr(line, '\n');
		size_t n = end ? (size_t)(end - line) : strlen(line);
		char buf[1024], *f[COLS], lspn[96];
		long score = 0, prio;
		int nf;

		if (n && n < sizeof(buf) && line[0] != '#') {
			memcpy(buf, line, n);
			buf[n] = '\0';
			if (n && buf[n - 1] == '\r')
				buf[n - 1] = '\0';
			nf = fields_of(buf, f);
			if (nf < 8 || wild(f[C_APN]) || !sane_apn(f[C_APN]))
				goto next;
			for (; nf < COLS; nf++)
				f[nf] = (char *)"-";
			if (!wild(f[C_MCCMNC])) {
				if (imsi ? strncmp(imsi, f[C_MCCMNC], strlen(f[C_MCCMNC])) : strcmp(code, f[C_MCCMNC]))
					goto next;
				score += 100 + (long)strlen(f[C_MCCMNC]);
			}
			if (!wild(f[C_IMSI])) {
				if (!imsi || !digits_match(imsi, f[C_IMSI]))
					goto next;
				score += 200 + (long)strlen(f[C_IMSI]);
			}
			if (!wild(f[C_ICCID])) {
				if (!given(sim->iccid) || !digits_match(sim->iccid, f[C_ICCID]))
					goto next;
				score += 200 + (long)strlen(f[C_ICCID]);
			}
			if (!wild(f[C_GID1])) {
				size_t k;
				for (k = 0; f[C_GID1][k]; k++)
					f[C_GID1][k] = (char)toupper((unsigned char)f[C_GID1][k]);
				if (!*gid1 || strncmp(gid1, f[C_GID1], strlen(f[C_GID1])))
					goto next;
				score += 300 + (long)strlen(f[C_GID1]);
			}
			if (!wild(f[C_SPN])) {
				lower_utf8(f[C_SPN], lspn, sizeof(lspn));
				if (!*spn || strcmp(spn, lspn))
					goto next;
				score += 300 + (long)strlen(lspn);
			}
			prio = strspn(f[C_PRIO], "0123456789") == strlen(f[C_PRIO]) && *f[C_PRIO] ? atol(f[C_PRIO]) : 100;
			if (score > best || (score == best && prio < best_prio)) {
				json_object_put(out);
				out = result(f[C_APN], source, f);
				best = score;
				best_prio = prio;
			}
		}
next:
		line = end ? end + 1 : NULL;
	}
	if (scored)
		*scored = best;
	return out;
}

/* overrides.list: kind "plmn" by the home code ("250-62" = "25062"), "name" by a substring of the SPN */
static json_object *override_pick(const char *list, const char *kind, const char *value)
{
	const char *line = list;
	char want[96];

	if (!list || !given(value))
		return NULL;
	if (!strcmp(kind, "name"))
		lower_utf8(value, want, sizeof(want));
	else
		snprintf(want, sizeof(want), "%s", value);
	while (line && *line) {
		const char *end = strchr(line, '\n');
		size_t n = end ? (size_t)(end - line) : strlen(line), k = strlen(kind);
		char buf[256], *pat, *apn, code[16];

		/* an empty value ("plmn:") is no rule: buf[len - 1] below needs one byte (review 2026-11-15) */
		if (n > k + 1 && n < sizeof(buf) && !strncmp(line, kind, k) && line[k] == ':') {
			size_t len = n - k - 1;

			memcpy(buf, line + k + 1, len);
			buf[len] = '\0';
			if (buf[len - 1] == '\r')
				buf[len - 1] = '\0';
			pat = buf;
			if ((apn = strrchr(buf, ':')) && apn != pat && sane_apn(apn + 1) && apn[1]) {
				*apn++ = '\0';
				if (!strcmp(kind, "plmn")) {
					size_t o = 0;
					for (; *pat && o + 1 < sizeof(code); pat++)
						if (*pat != '-')
							code[o++] = *pat;
					code[o] = '\0';
					if (!strcmp(code, want))
						return result(apn, "override", NULL);
				} else if (*pat && strstr(want, pat)) {
					return result(apn, "name", NULL);
				}
			}
		}
		line = end ? end + 1 : NULL;
	}
	return NULL;
}

json_object *mm_apn_pick(const char *db, const char *overrides, const struct mm_apn_sim *sim)
{
	json_object *o;
	char home[8] = "";

	if (!sim)
		return NULL;
	if (given(sim->home))
		snprintf(home, sizeof(home), "%s", sim->home);
	else if (given(sim->imsi) && strlen(sim->imsi) >= 5)
		snprintf(home, sizeof(home), "%.5s", sim->imsi);	/* a 3-digit MNC is matched by the database */
	long score = -1;
	json_object *d = (given(sim->imsi) || *home) ? db_pick(db, sim, home, "database", &score) : NULL;

	/* a row that matches the SIM itself (IMSI range, GID1, SPN: an MVNO on a host's network) beats an override by network code */
	if (d && score >= 200)
		return d;
	if ((o = override_pick(overrides, "plmn", home))) {
		json_object_put(d);
		return o;
	}
	if (d)
		return d;
	if ((o = override_pick(overrides, "name", sim->spn)))
		return o;
	if (!*home && given(sim->network)) {
		struct mm_apn_sim net = { 0 };
		return db_pick(db, &net, sim->network, "network", NULL);
	}
	return NULL;
}

json_object *mm_apn_db_info(const char *db)
{
	json_object *o = json_object_new_object();
	const char *line = db, *v;
	char version[32] = "";
	long count = 0;

	while (line && *line) {
		const char *end = strchr(line, '\n');
		size_t n = end ? (size_t)(end - line) : strlen(line), tabs = 0, i;

		if (line[0] == '#') {
			if (!*version && (v = strstr(line, "database-version:")) && (!end || v < end)) {
				v += strlen("database-version:");
				while (*v == ' ')
					v++;
				for (i = 0; i + 1 < sizeof(version) && v[i] && v[i] != '\n' && v[i] != '\r' && v[i] != ' '; i++)
					version[i] = v[i];
				version[i] = '\0';
			}
		} else {
			for (i = 0; i < n; i++)
				tabs += line[i] == '\t';
			count += tabs >= 7;
		}
		line = end ? end + 1 : NULL;
	}
	json_object_object_add(o, "version", *version ? json_object_new_string(version) : NULL);
	json_object_object_add(o, "count", json_object_new_int64(count));
	return o;
}
