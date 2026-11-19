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


