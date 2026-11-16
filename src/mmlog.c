// SPDX-License-Identifier: GPL-3.0-only
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmlog.h"

static const struct { const char *needle, *kind; } patterns[] = {
	{ "no longer controllable", "port-lost" },
	{ "grab port", "port-busy" },
	{ "NotOpened", "mbim-not-opened" },
	{ "connection broken, reprobing", "reprobe" },
	{ "primary AT port", "no-at" },
	{ "unhandled port type", "unhandled-port" },
	{ "SIM identifier has changed", "sim-changed" },
	/* an X20 (T77W968, DW5821e) on a cold start: its MBIM start timed out, MM marked it unusable and does not try
	 * again by itself (a field report, 27.10.2026) */
	{ "couldn't start initialization", "init-timeout" },
	{ "is unusable", "unusable" },
};

const char *mm_mmlog_kind(const char *line)
{
	for (size_t i = 0; line && i < sizeof(patterns) / sizeof(*patterns); i++)
		if (strstr(line, patterns[i].needle))
			return patterns[i].kind;
	return NULL;
}

int mm_mmlog_port(const char *line, char *buf, size_t size)
{
	static const char *const prefixes[] = { "cdc-wdm", "ttyUSB", "ttyACM", "wwan" };

	for (const char *p = line; p && *p; p++) {
		for (size_t i = 0; i < sizeof(prefixes) / sizeof(*prefixes); i++) {
			size_t n = strlen(prefixes[i]), len;

			/* a name starts the word: not «mywwan0» */
			if (strncmp(p, prefixes[i], n) || !isdigit((unsigned char)p[n]) || (p > line && isalnum((unsigned char)p[-1])))
				continue;
			len = n;
			while (isalnum((unsigned char)p[len]))
				len++;
			if (len + 1 > size)
				return 0;
			memcpy(buf, p, len);
			buf[len] = '\0';
			return 1;
		}
	}
	return 0;
}

int mm_mmlog_modem(const char *line)
{
	const char *p = line ? strstr(line, "[modem") : NULL;
	char *end;
	long n;

	if (!p || !isdigit((unsigned char)p[6]))
		return -1;
	n = strtol(p + 6, &end, 10);
	return *end == ']' && n >= 0 && n < 100000 ? (int)n : -1;
}
