/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_CHAIN_H
#define MEDVEMODEM_CHAIN_H

#include <json-c/json.h>

/*
 * The diagnosis by chain (alpha, audit 2026-10-13): device → SIM and network → session →
 * internet → DNS, each link ok / fail / unknown / skipped, and the first broken one with its
 * reason. Pure: reads a served device record (present, configuration, stability,
 * modemmanager, session, uplink) and writes nothing.
 *
 * {"ok": bool, "link": "device"|"network"|"session"|"internet"|"dns"|null,
 *  "reason": "absent"|"composition"|"service-mode"|"modeswitch-rule"|"sim-missing"|"sim-locked"|
 *            "registration-denied"|"searching"|"not-registered"|"no-session"|"session-failed"|
 *            "whitelist"|"no-answer"|"dns-failed"|null,
 *  "detail": text or null (the engine's own words: a session error, a check's target),
 *  "standby": bool (the session is up and checked, but the default route goes elsewhere),
 *  "links": [{"link", "state": "ok"|"fail"|"unknown"|"skipped"|"warn"}],
 *  "warning": {"link": "dns", "reason": "dns-intercepted"|"dns-stub"|"dns-rewritten", "detail"?} when all
 *             works but the operator answers DNS for others, gives a stub for blocked names or
 *             rewrites NXDOMAIN (dns-check; in that order)}
 */
json_object *mm_chain(json_object *rec);

#endif
