#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apdu.h"

bool mm_apdu_hex_ok(const char *s, size_t max)
{
	size_t n;

	if (!s || !*s)
		return false;
	for (n = 0; s[n]; n++)
		if (!isxdigit((unsigned char)s[n]) || n >= max)
			return false;
	return n % 2 == 0;
}

/* next line of text (without CR/LF) into line; returns the rest or NULL at the end */
static const char *next_line(const char *p, char *line, size_t size)
{
	size_t n = 0;

	if (!p || !*p)
		return NULL;
	while (*p && *p != '\r' && *p != '\n') {
		if (n + 1 < size)
			line[n++] = *p;
		p++;
	}
	line[n] = '\0';
	while (*p == '\r' || *p == '\n')
		p++;
	return p;
}

static bool all_digits(const char *s)
{
	if (!*s)
		return false;
	for (; *s; s++)
		if (!isdigit((unsigned char)*s))
			return false;
	return true;
}

int mm_apdu_ccho_channel(const char *answer, bool *bare)
{
	char line[256];
	const char *p = answer;

	if (bare)
		*bare = false;
	while ((p = next_line(p, line, sizeof(line)))) {
		const char *v = line;
		bool prefixed = false;
		long n;

		while (*v == ' ')
			v++;
		if (!strncasecmp(v, "+CCHO:", 6)) {
			v += 6;
			while (*v == ' ')
				v++;
			prefixed = true;
		}
		/* the echo starts with AT; OK/ERROR are not numbers */
		if (!all_digits(v))
			continue;
		n = strtol(v, NULL, 10);
		/* 27.007 session ids are positive; 0 would be the basic channel */
		if (n < 1 || n > 255)
			continue;
		if (bare)
			*bare = !prefixed;
		return (int)n;
	}
	return -1;
}

int mm_apdu_cgla_response(const char *answer, char *hex, size_t size)
{
	char line[1200];
	const char *p = answer;

	while ((p = next_line(p, line, sizeof(line)))) {
		const char *v = line, *end;
		size_t n, i;

		while (*v == ' ')
			v++;
		if (strncasecmp(v, "+CGLA:", 6))
			continue;
		v = strchr(v, ',');
		if (!v)
			continue;
		v++;
		while (*v == ' ')
			v++;
		if (*v == '"')
			v++;
		for (end = v; isxdigit((unsigned char)*end); end++)
			;
		n = (size_t)(end - v);
		/* at least SW1SW2, whole bytes */
		if (n < 4 || n % 2 || n + 1 > size)
			continue;
		for (i = 0; i < n; i++)
			hex[i] = (char)toupper((unsigned char)v[i]);
		hex[n] = '\0';
		return (int)n;
	}
	return -1;
}

int mm_apdu_cgla_command(int channel, const char *apdu, char *out, size_t size)
{
	int n;

	if (channel < 1 || channel > 255 || !mm_apdu_hex_ok(apdu, MM_APDU_MAX_HEX))
		return -1;
	n = snprintf(out, size, "AT+CGLA=%d,%zu,\"%s\"", channel, strlen(apdu), apdu);
	return n < 0 || (size_t)n >= size ? -1 : n;
}

const char *mm_apdu_probe_verdict(const char *answer, int complete)
{
	if (mm_apdu_ccho_channel(answer, NULL) > 0)
		return "euicc";
	return complete ? "refused" : "silent";
}
