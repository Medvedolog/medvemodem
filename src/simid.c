/* SPDX-License-Identifier: GPL-3.0-only */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "simid.h"

static bool digits_only(const char *s, size_t min, size_t max)
{
	size_t n = strlen(s);

	if (n < min || n > max)
		return false;
	for (; *s; s++)
		if (!isdigit((unsigned char)*s))
			return false;
	return true;
}

bool simid_imsi_from_cimi(const char *raw, char *out, size_t size)
{
	const char *p = raw ? raw : "";

	while (*p) {
		size_t n = 0;

		while (isdigit((unsigned char)p[n]))
			n++;
		if (n >= 14 && n <= 15 && n < size) {
			memcpy(out, p, n);
			out[n] = '\0';
			return true;
		}
		p += n ? n : 1;
	}
	return false;
}

bool simid_iccid_from_crsm(const char *raw, char *out, size_t size)
{
	const char *p = raw ? strstr(raw, "+CRSM:") : NULL, *c1, *c2, *q;
	int sw1 = -1, sw2 = -1;
	char digits[24];
	size_t n = 0;

	if (!p || sscanf(p, "+CRSM: %d,%d", &sw1, &sw2) != 2 || sw1 != 144 || sw2 != 0)
		return false;
	c1 = strchr(p, ',');
	c2 = c1 ? strchr(c1 + 1, ',') : NULL;
	if (!c2)
		return false;
	for (q = c2 + 1; *q == ' ' || *q == '"'; q++)
		;
	/* a byte is two digits, the low one first; the filler F ends the number */
	for (; isxdigit((unsigned char)q[0]) && isxdigit((unsigned char)q[1]) && n + 2 < sizeof(digits); q += 2) {
		if (toupper((unsigned char)q[1]) == 'F')
			break;
		digits[n++] = q[1];
		if (toupper((unsigned char)q[0]) == 'F')
			break;
		digits[n++] = q[0];
	}
	digits[n] = '\0';
	if (!digits_only(digits, 18, 20) || n + 1 > size)
		return false;
	memcpy(out, digits, n + 1);
	return true;
}

static const char *modem_info_value(json_object *info, const char *key)
{
	size_t i;

	for (i = 0; info && json_object_is_type(info, json_type_array) && i < json_object_array_length(info); i++) {
		json_object *e = json_object_array_get_idx(info, i), *k = NULL, *v = NULL;

		if (json_object_object_get_ex(e, "key", &k) && json_object_object_get_ex(e, "value", &v) &&
		    json_object_is_type(k, json_type_string) && !strcmp(json_object_get_string(k), key) &&
		    json_object_is_type(v, json_type_string))
			return json_object_get_string(v);
	}
	return NULL;
}

bool simid_from_qmodem(json_object *responses, char *imsi, size_t imsi_size, char *iccid, size_t iccid_size)
{
	json_object *sim = NULL, *info = NULL;
	const char *i = NULL, *c = NULL;
	char buf[24];
	size_t n;
	bool got = false;

	if (!responses || !json_object_object_get_ex(responses, "sim_info", &sim) ||
	    !json_object_object_get_ex(sim, "modem_info", &info))
		return false;
	i = modem_info_value(info, "IMSI");
	c = modem_info_value(info, "ICCID");
	if (i && digits_only(i, 14, 15) && strlen(i) < imsi_size) {
		snprintf(imsi, imsi_size, "%s", i);
		got = true;
	}
	if (c) {
		/* some modems pad the ICCID with F (BCD filler) */
		n = strlen(c);
		while (n && (c[n - 1] == 'F' || c[n - 1] == 'f'))
			n--;
		if (n < sizeof(buf)) {
			memcpy(buf, c, n);
			buf[n] = '\0';
			if (digits_only(buf, 18, 20) && n < iccid_size) {
				memcpy(iccid, buf, n + 1);
				got = true;
			}
		}
	}
	return got;
}
