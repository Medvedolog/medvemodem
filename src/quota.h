/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_QUOTA_H
#define MEDVEMODEM_QUOTA_H

#include <stdbool.h>
#include <json-c/json.h>

/*
 * Monthly traffic quotas per SIM (spec 20.9). Pure: the daemon counts the
 * bytes of the modem's network device and knows the SIM in it; this keeps the
 * books and judges them.
 *
 * The billing period starts on reset_day (1..28, anything else is 1) of the
 * month: on 2026-10-13 with reset_day 15 it runs 2026-09-15 .. 2026-10-14.
 * start and next ("YYYY-MM-DD", 11 bytes) receive its first day and the first
 * day of the next one. False when today is not a "YYYY-MM-DD" date.
 */
bool mm_quota_period(const char *today, int reset_day, char *start, char *next);

/*
 * usage: {"<iccid>": {"period": "YYYY-MM-DD", "bytes", "warned"?, "exceeded"?}},
 * kept between calls (and on flash by the daemon). Adds bytes to the SIM's
 * account for the period starting on start; a new period starts from zero and
 * forgets the warnings. A period never goes back: a start before the entry's
 * (a clock set back) counts in the entry's period. Returns the SIM's entry
 * (borrowed).
 */
json_object *mm_quota_add(json_object *usage, const char *iccid, const char *start, long long bytes);

/*
 * The SIM's quota as served: {"iccid", "used_bytes", "limit_bytes"?,
 * "percent"?, "warn_percent", "state": "none" (no limit) | "ok" | "warn" |
 * "exceeded", "period_start", "resets_on", "action": "warn" | "backup"}.
 * limit_bytes <= 0 means no limit. New object.
 */
json_object *mm_quota_view(json_object *entry, const char *iccid, long long limit_bytes, int warn_percent,
			   const char *start, const char *next, const char *action);

/*
 * A state change worth a journal line, once per period: "quota-warn" or
 * "quota-exceeded" (marked in the entry so it is said once), else NULL.
 */
const char *mm_quota_news(json_object *entry, json_object *view);

/* The books' key for a SIM: "sim-" and 16 hex digits of a hash of the ICCID (an identifier is never
 * kept on flash as it is). out: 21 bytes. */
void mm_quota_key(const char *iccid, char *out);

/* "5", "2.5", "0.5" GB (decimal: 10^9 bytes, as operators count) -> bytes; 0 when unset or invalid */
long long mm_quota_bytes(const char *gb);

#endif
