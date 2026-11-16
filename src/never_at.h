/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_NEVER_AT_H
#define MEDVEMODEM_NEVER_AT_H

#include <stdbool.h>
#include <stdio.h>
#include <strings.h>

/*
 * Interfaces that are never sent AT, whatever names them (a profile, the
 * operator, a probe): "vid:pid:ifnum" (bInterfaceNumber, two hex digits).
 * The roles mark them reserved, so no lease, console, metric or SMS opens
 * them (audit 2026-10-13, F01); the probe skips them too.
 */
static const char *const MM_NEVER_AT[] = {
	"2cd2:0001:02",		/* H12: MikroTik R11e-LTE binary log */
	"05c6:9025:00",		/* Qualcomm DIAG of the T99W175 9025 composition: binary, never AT */
	NULL
};

static inline bool mm_never_at(const char *vidpid, const char *ifnum)
{
	char key[32];
	int i;

	if (!vidpid || !ifnum)
		return false;
	snprintf(key, sizeof(key), "%s:%s", vidpid, ifnum);
	for (i = 0; MM_NEVER_AT[i]; i++)
		if (!strcasecmp(key, MM_NEVER_AT[i]))
			return true;
	return false;
}

#endif
