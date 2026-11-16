/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_DOWNTIME_H
#define MEDVEMODEM_DOWNTIME_H

#include <json-c/json.h>

/*
 * How long a modem was without the internet today (the owner, 2026-10-27).
 * Pure: the daemon tells each uplink pass what the modem's link was; this keeps the day's book.
 *
 * book: {"day": "YYYY-MM-DD", "ok", "limited", "none", "off": milliseconds, "outages", "longest": milliseconds,
 *        "yesterday": {...the same, the day before}?, "state", "since_ms", "last_ms"}, kept between calls.
 * state: "ok" (the internet answers), "limited" (only the operator's whitelist), "none" (connected, nothing
 * answers), "off" (its interface is there, no session), or NULL (not known yet, or the modem away: the time is not
 * counted). The time since the previous call goes to the previous call's state, at most MM_DOWNTIME_GAP_MS (an
 * engine stopped or a modem unplugged counts nothing). A new day moves the book to "yesterday". A stretch of "none"
 * or "off" is an outage once it lasted MM_DOWNTIME_OUTAGE_MS (a session coming up after the engine starts is not
 * one; its seconds still count); "longest" is the longest such stretch today.
 */
#define MM_DOWNTIME_GAP_MS 300000
#define MM_DOWNTIME_OUTAGE_MS 30000

void mm_downtime_add(json_object *book, const char *state, const char *day, long long now_ms);

/* the served view: {"day", "ok", "limited", "none", "off", "outages", "longest", "current"?: seconds of the outage
 * going on}, without the bookkeeping; NULL for an empty book (new object) */
json_object *mm_downtime_view(json_object *book, long long now_ms);

#endif
