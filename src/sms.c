#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sms.h"

static json_object *get(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_object_get_ex(o, key, &v) ? v : NULL;
}

/* mmcli prints "--" for an empty property */
static const char *val(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	const char *s = v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;

	return s && *s && strcmp(s, "--") ? s : NULL;
}

static void put_str(json_object *o, const char *key, const char *s)
{
	json_object_object_add(o, key, s ? json_object_new_string(s) : NULL);
}

long long mm_sms_epoch(const char *iso)
{
	struct tm tm;
	int y, mo, d, h, mi, s, n = 0, oh = 0, om = 0;
	const char *z;
	long long t;

	if (!iso || sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &s, &n) != 6)
		return -1;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = s;
	t = (long long)timegm(&tm);
	z = iso + n;
	if (*z == '.')			/* fractions of a second */
		while (*++z && isdigit((unsigned char)*z))
			;
	if (*z == '+' || *z == '-') {
		int sign = *z == '-' ? -1 : 1;

		/* «+03:30» or «+0330» (strftime's %z below): the form by its colon, or the minutes are lost (review 2026-11-15) */
		if ((strchr(z + 1, ':') ? sscanf(z + 1, "%2d:%2d", &oh, &om) : sscanf(z + 1, "%2d%2d", &oh, &om)) < 1)
			return -1;
		t -= sign * (oh * 3600LL + om * 60LL);
	}
	return t;
}

static int index_of(const char *path)
{
	const char *slash = path ? strrchr(path, '/') : NULL;

	return slash && isdigit((unsigned char)slash[1]) ? atoi(slash + 1) : -1;
}

static int by_index(const void *a, const void *b)
{
	int x = json_object_get_int(get(*(json_object *const *)a, "id"));
	int y = json_object_get_int(get(*(json_object *const *)b, "id"));

	return (x > y) - (x < y);
}

json_object *mm_sms_parse(const char *buf)
{
	json_object *out = json_object_new_object(), *msgs = json_object_new_array();
	const char *p = buf ? buf : "";
	int listed = -1;

	while (*p) {
		size_t n = strcspn(p, "\n");
		char *line = strndup(p, n);
		json_object *doc = line ? json_tokener_parse(line) : NULL, *sms, *list;

		free(line);
		p += n + (p[n] == '\n');
		if (!doc)
			continue;
		if ((list = get(doc, "modem.messaging.sms")) && json_object_is_type(list, json_type_array))
			listed = (int)json_object_array_length(list);
		if ((sms = get(doc, "sms"))) {
			json_object *c = get(sms, "content"), *pr = get(sms, "properties"), *m = json_object_new_object();
			int id = index_of(val(sms, "dbus-path"));
			const char *ts = val(pr, "timestamp");

			if (id < 0) {
				json_object_put(m);
				json_object_put(doc);
				continue;
			}
			json_object_object_add(m, "id", json_object_new_int(id));
			put_str(m, "number", val(c, "number"));
			put_str(m, "text", val(c, "text"));
			put_str(m, "state", val(pr, "state"));
			put_str(m, "type", val(pr, "pdu-type"));
			put_str(m, "storage", val(pr, "storage"));
			put_str(m, "timestamp", ts);
			json_object_object_add(m, "epoch", ts && mm_sms_epoch(ts) >= 0 ?
					       json_object_new_int64(mm_sms_epoch(ts)) : NULL);
			put_str(m, "class", val(pr, "class"));
			put_str(m, "delivery_state", val(pr, "delivery-state"));
			if (val(pr, "message-reference"))
				json_object_object_add(m, "reference", json_object_new_int(atoi(val(pr, "message-reference"))));
			/* a binary message (data) has no text: say so rather than show nothing */
			json_object_object_add(m, "binary", json_object_new_boolean(!val(c, "text") && val(c, "data")));
			json_object_array_add(msgs, m);
		}
		json_object_put(doc);
	}
	if (listed < 0) {
		json_object_put(msgs);
		json_object_object_add(out, "error", json_object_new_string("ModemManager did not list the messages"));
		return out;
	}
	json_object_array_sort(msgs, by_index);
	json_object_object_add(out, "listed", json_object_new_int(listed));
	json_object_object_add(out, "messages", msgs);
	return out;
}

static void inbox_key(json_object *m, char *out, size_t size)
{
	const char *parts[3] = { val(m, "number"), val(m, "timestamp"), val(m, "text") };
	unsigned long long h = 0xcbf29ce484222325ULL;	/* FNV-1a 64 */
	int i;

	for (i = 0; i < 3; i++) {
		const char *s = parts[i] ? parts[i] : "";

		for (; *s; s++) {
			h ^= (unsigned char)*s;
			h *= 0x100000001b3ULL;
		}
		h ^= 0x1f;
		h *= 0x100000001b3ULL;
	}
	snprintf(out, size, "%016llx", h);
}

static bool inbound(json_object *m)
{
	const char *st = val(m, "state"), *ty = val(m, "type");

	return st && !strcmp(st, "received") && (!ty || !strcmp(ty, "deliver"));
}

void mm_sms_key(json_object *m, char *out, size_t size)
{
	inbox_key(m, out, size);
}

json_object *mm_sms_inbox(json_object *msgs)
{
	json_object *out = json_object_new_array();
	size_t i;

	for (i = 0; msgs && json_object_is_type(msgs, json_type_array) && i < json_object_array_length(msgs); i++) {
		json_object *m = json_object_array_get_idx(msgs, i);
		char key[24];

		if (!inbound(m))
			continue;
		inbox_key(m, key, sizeof(key));
		json_object_array_add(out, json_object_new_string(key));
	}
	return out;
}

int mm_sms_unread(json_object *msgs, json_object *seen)
{
	json_object *keys = mm_sms_inbox(msgs);
	size_t i, k;
	int n = 0;

	for (i = 0; i < json_object_array_length(keys); i++) {
		const char *key = json_object_get_string(json_object_array_get_idx(keys, i));
		bool known = false;

		for (k = 0; seen && json_object_is_type(seen, json_type_array) && k < json_object_array_length(seen) && !known; k++)
			known = !strcmp(json_object_get_string(json_object_array_get_idx(seen, k)), key);
		n += !known;
	}
	json_object_put(keys);
	return n;
}

bool mm_sms_number_ok(const char *number)
{
	size_t digits = 0;

	if (!number)
		return false;
	if (*number == '+')
		number++;
	for (; *number; number++, digits++)
		if (!isdigit((unsigned char)*number))
			return false;
	return digits >= 3 && digits <= 20;
}

/* the next code point of valid UTF-8 (no overlongs, no surrogates), or -1 */
static long next_cp(const unsigned char **s)
{
	const unsigned char *p = *s;
	long cp;
	int n, i;

	if (p[0] < 0x80)
		n = 0, cp = p[0];
	else if ((p[0] & 0xe0) == 0xc0)
		n = 1, cp = p[0] & 0x1f;
	else if ((p[0] & 0xf0) == 0xe0)
		n = 2, cp = p[0] & 0x0f;
	else if ((p[0] & 0xf8) == 0xf0)
		n = 3, cp = p[0] & 0x07;
	else
		return -1;
	for (i = 1; i <= n; i++) {
		if ((p[i] & 0xc0) != 0x80)
			return -1;
		cp = (cp << 6) | (p[i] & 0x3f);
	}
	if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10ffff ||
	    (cp >= 0xd800 && cp <= 0xdfff))
		return -1;
	*s = p + n + 1;
	return cp;
}

bool mm_sms_text_ok(const char *text)
{
	const unsigned char *p = (const unsigned char *)text;
	size_t len = text ? strlen(text) : 0;

	if (!len || len > 1600)
		return false;
	while (*p) {
		long cp = next_cp(&p);

		if (cp < 0 || (cp < 0x20 && cp != '\n' && cp != '\t') || cp == 0x7f)
			return false;
	}
	return true;
}

bool mm_sms_create_arg(const char *number, const char *text, const char *smsc, char *out, size_t size)
{
	char q;
	int n;

	if (!mm_sms_number_ok(number) || !mm_sms_text_ok(text) || (smsc && *smsc && !mm_sms_number_ok(smsc)))
		return false;
	if (!strchr(text, '\''))
		q = '\'';
	else if (!strchr(text, '"'))
		q = '"';
	else {
		/* both quotes: ModemManager's key=value parser has no escape, so the apostrophes go as ’ (U+2019) and the
		 * text is quoted with ' (the message turns UCS-2; sent rather than refused) */
		size_t len = strlen(text), i, o = 0;
		char *t = malloc(len * 3 + 1);
		bool ok;

		if (!t)
			return false;
		for (i = 0; i < len; i++)
			if (text[i] == '\'') {
				t[o++] = (char)0xe2; t[o++] = (char)0x80; t[o++] = (char)0x99;
			} else
				t[o++] = text[i];
		t[o] = '\0';
		n = snprintf(out, size, "number='%s',text='%s'%s%s%s", number, t,
			     smsc && *smsc ? ",smsc='" : "", smsc && *smsc ? smsc : "", smsc && *smsc ? "'" : "");
		ok = n > 0 && (size_t)n < size;
		free(t);
		return ok;
	}
	n = snprintf(out, size, "number='%s',text=%c%s%c%s%s%s", number, q, text, q,
		     smsc && *smsc ? ",smsc='" : "", smsc && *smsc ? smsc : "", smsc && *smsc ? "'" : "");
	return n > 0 && (size_t)n < size;
}

/* GSM 03.38 default alphabet (beyond printable ASCII, which is in it but for a few) */
static int gsm7_units(long cp)
{
	static const long basic[] = { 0x40, 0xa3, 0x24, 0xa5, 0xe8, 0xe9, 0xf9, 0xec, 0xf2, 0xc7, 0x0a, 0xd8, 0xf8,
		0x0d, 0xc5, 0xe5, 0x394, 0x5f, 0x3a6, 0x393, 0x39b, 0x3a9, 0x3a0, 0x3a8, 0x3a3, 0x398, 0x39e, 0xc6,
		0xe6, 0xdf, 0xc9, 0xa4, 0xa1, 0xc4, 0xd6, 0xd1, 0xdc, 0xa7, 0xbf, 0xe4, 0xf6, 0xf1, 0xfc, 0xe0 };
	static const long ext[] = { 0x0c, 0x5e, 0x7b, 0x7d, 0x5c, 0x5b, 0x7e, 0x5d, 0x7c, 0x20ac };
	size_t i;

	for (i = 0; i < sizeof(ext) / sizeof(ext[0]); i++)
		if (cp == ext[i])
			return 2;
	for (i = 0; i < sizeof(basic) / sizeof(basic[0]); i++)
		if (cp == basic[i])
			return 1;
	/* printable ASCII but '`' (and the extension characters above) */
	if (cp >= 0x20 && cp < 0x7f && cp != 0x60)
		return 1;
	return 0;
}

void mm_sms_measure(const char *text, bool *gsm7, int *units, int *segments)
{
	const unsigned char *p = (const unsigned char *)(text ? text : "");
	int g = 0, u = 0;
	bool is7 = true;

	while (*p) {
		long cp = next_cp(&p);
		int k;

		if (cp < 0)
			break;
		k = gsm7_units(cp);
		if (!k)
			is7 = false;
		g += k;
		u += cp > 0xffff ? 2 : 1;
	}
	*gsm7 = is7;
	*units = is7 ? g : u;
	if (is7)
		*segments = *units <= 160 ? (*units ? 1 : 0) : (*units + 152) / 153;
	else
		*segments = *units <= 70 ? (*units ? 1 : 0) : (*units + 66) / 67;
}

bool mm_ussd_code_ok(const char *code)
{
	size_t n = code ? strlen(code) : 0;

	if (n < 2 || n > 32)
		return false;
	for (; *code; code++)
		if (!isdigit((unsigned char)*code) && *code != '*' && *code != '#' && *code != '+')
			return false;
	return true;
}

bool mm_ussd_reply_ok(const char *reply)
{
	const unsigned char *p = (const unsigned char *)reply;
	size_t n = reply ? strlen(reply) : 0;

	if (!n || n > 64)
		return false;
	while (*p) {
		long cp = next_cp(&p);

		if (cp < 0x20 || cp == '\'' || cp == '"' || cp == 0x7f)
			return false;
	}
	return true;
}

json_object *mm_ussd_parse(const char *out, int exit_code)
{
	json_object *o = json_object_new_object();
	const char *s = out ? out : "", *mark = "new reply from network: '", *p = strstr(s, mark);
	char msg[1024];

	if (exit_code == 0 && p) {
		const char *a = p + strlen(mark), *b = strrchr(a, '\'');
		size_t n = b ? (size_t)(b - a) : strcspn(a, "\n");

		snprintf(msg, sizeof(msg), "%.*s", (int)(n < sizeof(msg) ? n : sizeof(msg) - 1), a);
		json_object_object_add(o, "reply", json_object_new_string(msg));
		return o;
	}
	if (exit_code == 0) {
		json_object_object_add(o, "reply", json_object_new_string(""));
		return o;
	}
	/* "error: couldn't initiate USSD session: 'GDBus.Error:…: reason'": the reason */
	{
		const char *e = strrchr(s, ':'), *start = s;
		size_t n;

		if (strstr(s, "GDBus.Error:") && e)
			start = e + 1;
		while (*start == ' ' || *start == '\'')
			start++;
		n = strcspn(start, "\n");
		while (n && (start[n - 1] == '\'' || start[n - 1] == ' ' || start[n - 1] == '\r'))
			n--;
		snprintf(msg, sizeof(msg), "%.*s", (int)(n < 200 ? n : 200), *start ? start : "no answer");
		json_object_object_add(o, "error", json_object_new_string(n ? msg : "no answer from the network"));
	}
	return o;
}

json_object *mm_sms_qmodem_list(const char *raw)
{
	const char *brace = raw ? strchr(raw, '{') : NULL;
	json_object *doc = brace ? json_tokener_parse(brace) : NULL, *list = NULL, *out;
	size_t i, k;

	if (!doc || !json_object_object_get_ex(doc, "msg", &list) || !json_object_is_type(list, json_type_array)) {
		json_object_put(doc);
		return NULL;
	}
	out = json_object_new_array();
	for (i = 0; i < json_object_array_length(list); i++) {
		json_object *x = json_object_array_get_idx(list, i), *m = NULL, *parts;
		const char *from = val(x, "sender") ? val(x, "sender") : "";
		int ref = json_object_get_int(get(x, "reference")), total = json_object_get_int(get(x, "total"));
		int part = json_object_get_int(get(x, "part")), idx = json_object_get_int(get(x, "index"));

		if (!get(x, "index"))
			continue;
		/* a part of a long message: the one already started with the same sender and reference */
		for (k = 0; ref && total > 1 && k < json_object_array_length(out) && !m; k++) {
			json_object *c = json_object_array_get_idx(out, k);

			char pk[16];

			/* the same part twice is another message under a reused reference (as pdu.c joins, review 2026-11-15) */
			snprintf(pk, sizeof(pk), "%d", part > 0 ? part : 1);
			if (json_object_get_int(get(c, "reference")) == ref && val(c, "number") && !strcmp(val(c, "number"), from) &&
			    json_object_get_int(get(c, "total")) == total && !get(get(c, "pieces"), pk))
				m = c;
		}
		if (!m) {
			char ts[40];
			time_t t = (time_t)json_object_get_int64(get(x, "timestamp"));
			struct tm lt;

			m = json_object_new_object();
			json_object_object_add(m, "id", json_object_new_int(idx));
			json_object_object_add(m, "number", json_object_new_string(from));
			json_object_object_add(m, "state", json_object_new_string("received"));
			json_object_object_add(m, "type", json_object_new_string("deliver"));
			json_object_object_add(m, "storage", json_object_new_string("qmodem"));
			json_object_object_add(m, "path", json_object_new_string("qmodem"));
			if (t > 0 && localtime_r(&t, &lt) && strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", &lt)) {
				json_object_object_add(m, "timestamp", json_object_new_string(ts));
				json_object_object_add(m, "epoch", json_object_new_int64((long long)t));
			}
			json_object_object_add(m, "parts", json_object_new_array());
			json_object_object_add(m, "pieces", json_object_new_object());
			if (ref && total > 1) {
				json_object_object_add(m, "reference", json_object_new_int(ref));
				json_object_object_add(m, "total", json_object_new_int(total));
			}
			json_object_array_add(out, m);
		}
		parts = get(m, "parts");
		json_object_array_add(parts, json_object_new_int(idx));
		{
			char key[16];

			snprintf(key, sizeof(key), "%d", part > 0 ? part : 1);
			json_object_object_add(get(m, "pieces"), key, json_object_new_string(val(x, "content") ? val(x, "content") : ""));
		}
		if (idx < json_object_get_int(get(m, "id")))
			json_object_object_add(m, "id", json_object_new_int(idx));
	}
	/* the text in part order; what is missing is said by have/total */
	for (i = 0; i < json_object_array_length(out); i++) {
		json_object *m = json_object_array_get_idx(out, i), *pieces = get(m, "pieces");
		int total = get(m, "total") ? json_object_get_int(get(m, "total")) : 1, have = 0, n;
		size_t len = 1;
		char *text, key[16];

		json_object_object_foreach(pieces, pk, pv) {
			(void)pk;
			len += strlen(json_object_get_string(pv));
		}
		text = calloc(1, len);
		for (n = 1; text && n <= (total > 1 ? total : 1); n++) {
			snprintf(key, sizeof(key), "%d", n);
			if (get(pieces, key)) {
				strcat(text, json_object_get_string(get(pieces, key)));
				have++;
			}
		}
		json_object_object_add(m, "text", json_object_new_string(text ? text : ""));
		free(text);
		if (total > 1 && have < total) {
			json_object_object_add(m, "have", json_object_new_int(have));
		} else {
			json_object_object_del(m, "total");
		}
		json_object_object_del(m, "pieces");
		json_object_object_del(m, "reference");
	}
	json_object_put(doc);
	return out;
}
