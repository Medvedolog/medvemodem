// SPDX-License-Identifier: GPL-3.0-only
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kfault.h"

static const struct { const char *needle, *kind; } patterns[] = {
	{ "HC died", "hc-died" },
	{ "not responding, assume dead", "hc-died" },
	{ "Host halt failed", "hc-died" },
	{ "failed to set voltage", "voltage" },
	{ "Under-voltage detected", "undervoltage" },
	{ "Undervoltage detected", "undervoltage" },
	{ "self-detected stall", "stall" },
	{ "detected stalls", "stall" },
	{ "soft lockup", "lockup" },
	{ "hard LOCKUP", "lockup" },
	{ "Internal error: Oops", "oops" },
	{ "Unable to handle kernel", "oops" },
	{ "BUG: unable to handle", "oops" },
	{ "Kernel panic", "panic" },
	{ "critical temperature", "overheat" },
	{ "blocked for more than", "hung-task" },
	{ "Out of memory: Kill", "oom" },
	{ "invoked oom-killer", "oom" },
	{ "EXT4-fs error", "fs-error" },
	{ "SQUASHFS error", "fs-error" },
	{ "UBIFS error", "fs-error" },
	{ "I/O error, dev ", "fs-error" },
};

static const char *const critical[] = { "hc-died", "voltage", "undervoltage", "stall", "lockup", "oops", "panic", "overheat",
					"hung-task" };

bool mm_kfault_critical(const char *kind)
{
	for (size_t i = 0; kind && i < sizeof(critical) / sizeof(*critical); i++)
		if (!strcmp(kind, critical[i]))
			return true;
	return false;
}

/* a short name: letters, digits and «._:-», at most size-1 of them */
static void copy_name(const char *s, size_t n, char *who, size_t size)
{
	size_t k = 0;

	for (size_t i = 0; i < n && s[i] && k + 1 < size; i++)
		if (isalnum((unsigned char)s[i]) || strchr("._:-", s[i]))
			who[k++] = s[i];
	who[k] = 0;
}

/* three numbers and their commas, then fields without a space up to the semicolon */
static bool kmsg_prefix(const char *p, const char *semi)
{
	for (int f = 0; f < 3; f++) {
		size_t d = strspn(p, "0123456789");

		if (!d || p[d] != ',')
			return false;
		p += d + 1;
	}
	return p <= semi && (size_t)(semi - p) == strcspn(p, " ;");
}

double mm_kfault_split(const char *line, const char **text)
{
	const char *p = line, *semi;
	double t = -1;

	*text = line;
	if (!line)
		return -1;
	if (*p == '<') {			/* «<3>[  950.000000] text» (klogctl, dmesg -r) */
		const char *gt = strchr(p, '>');

		if (gt)
			p = gt + 1;
	}
	if (*p == '[') {			/* «[  950.000000] text» */
		const char *end = strchr(p, ']');

		if (end) {
			t = strtod(p + 1, NULL);
			p = end + 1;
			while (*p == ' ')
				p++;
		}
		*text = p;
		return t;
	}
	/* /dev/kmsg: «pri,seq,usec,flags[,caller=T123];text» (CONFIG_PRINTK_CALLER adds the caller, review 2026-11-15) */
	if ((semi = strchr(line, ';')) && kmsg_prefix(line, semi)) {
		const char *c1 = strchr(line, ','), *c2 = c1 ? strchr(c1 + 1, ',') : NULL;

		if (c2 && c2 < semi)
			t = strtod(c2 + 1, NULL) / 1e6;
		*text = semi + 1;
	}
	return t;
}

const char *mm_kfault_kind(const char *text, char *who, size_t size)
{
	const char *kind = NULL, *p;

	if (size)
		*who = 0;
	for (size_t i = 0; text && i < sizeof(patterns) / sizeof(*patterns) && !kind; i++)
		if (strstr(text, patterns[i].needle))
			kind = patterns[i].kind;
	if (!kind || !size)
		return kind;
	if (!strcmp(kind, "oom") && (p = strstr(text, "Killed process "))) {
		/* «Killed process 1234 (name)»: the name */
		const char *o = strchr(p, '('), *c = o ? strchr(o, ')') : NULL;

		if (o && c)
			copy_name(o + 1, (size_t)(c - o - 1), who, size);
	} else if (!strcmp(kind, "oom") && (p = strstr(text, " invoked oom-killer"))) {
		const char *s = p;

		while (s > text && s[-1] != ' ')
			s--;
		copy_name(s, (size_t)(p - s), who, size);
	} else if (!strcmp(kind, "hung-task") && (p = strstr(text, "task "))) {
		const char *e = strpbrk(p + 5, " :/");

		copy_name(p + 5, e ? (size_t)(e - p - 5) : strlen(p + 5), who, size);
	} else if (!strcmp(kind, "fs-error")) {
		/* «I/O error, dev mmcblk0, sector …»: the device; a file system's error names none */
		if ((p = strstr(text, "error, dev ")))
			copy_name(p + 11, strcspn(p + 11, " ,"), who, size);
	} else if ((p = strstr(text, ": ")) && !strstr(kind, "stall") && strcmp(kind, "lockup") && strcmp(kind, "oops") &&
		   strcmp(kind, "panic")) {
		/* «xhci-hcd xhci-hcd.9.auto: HC died», «cpu cpu0: failed to set voltage»: the device before the colon */
		const char *s = p;

		while (s > text && s[-1] != ' ')
			s--;
		copy_name(s, (size_t)(p - s), who, size);
	} else if (!strcmp(kind, "stall") && (p = strstr(text, "CPU"))) {
		/* «CPU: 3 PID: …», «stall on CPU 3»: the CPU and its number (review 2026-11-15: «CPU:» lost it) */
		const char *d = p + 3;
		size_t n;

		d += strspn(d, ": ");
		n = strspn(d, "0123456789");
		if (n && size > 4) {
			snprintf(who, size, "CPU%.*s", (int)(n < size - 4 ? n : size - 4), d);
		} else {
			copy_name(p, 3, who, size);
		}
	}
	return kind;
}
