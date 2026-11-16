#ifndef MEDVEMODEM_UPLINK_H
#define MEDVEMODEM_UPLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/*
 * Uplink facts for the dashboard (spec 25.9.10, 20.4). Pure: the daemon reads
 * /proc and sysfs and runs the reachability probes; this parses, accounts
 * and decides the role words.
 */

/*
 * The device of the default route the kernel's main table prefers (lowest
 * metric) from the text of /proc/net/route, else /proc/net/ipv6_route; either
 * may be NULL. Policy routing (mwan3) is not seen here. NULL when none.
 */
const char *mm_uplink_default_dev(const char *route4, const char *route6, char *out, size_t size);

/*
 * One counter sample of an interface. state is the uplink's traffic object,
 * kept between samples:
 *   {"rx_bytes", "tx_bytes" (session totals), "rx_rate", "tx_rate" (bytes/s),
 *    "day": {"key", "rx_bytes", "tx_bytes"}, "month": {...}, "last": {...}}
 * restart starts a new session total (a new session or another netdev);
 * counters going down (the netdev was re-created) count from zero. day and
 * month are the local date keys ("2026-10-02", "2026-10").
 */
void mm_traffic_sample(json_object *state, long long rx, long long tx, long long now_ms, bool restart,
		       const char *day, const char *month);
/* the served copy without the bookkeeping. New object. */
json_object *mm_traffic_view(json_object *state);

/*
 * A probe result judged against the path's previous verdict (hysteresis). raw:
 * {"ok": bool, "limited"?: true, ...the probe's fields}; "limited" means the
 * world targets failed and a restricted (operator whitelist) target answered.
 * The level is "ok", "limited" or "down". The first result counts at once; a
 * change away from "ok" takes 3 results in a row, any other change 2 (a lost
 * packet on a cellular link is no verdict). While a change is unconfirmed the
 * previous verdict stays, with "pending": {"level", "count", "error"?} and
 * *recheck set (probe again soon). Returns the new reach object (new); its
 * "ok" is level == "ok", "limited" is level == "limited".
 */
json_object *mm_reach_judge(json_object *prev, json_object *raw, bool *recheck);

/*
 * The modem's role: "down" (no connected session), "checking" (connected, no
 * probe yet), "carrying" (default route through it and its probe passed),
 * "no_internet" (default route through it, probe failed), "limited" (default
 * route through it, only restricted targets answer), "backup_ready",
 * "backup_not_ready", "backup_limited" (not the default route; its own probe).
 * reach: {"ok": bool, "limited"?: bool, ...} or NULL.
 */
const char *mm_uplink_role(const char *session_state, const char *dev, const char *default_dev, json_object *reach);

/*
 * The router's summary from paths [{"device", "device_id"?, "name"?, "role"?,
 * "reach"?}] and the default device:
 *   {"internet": "available" | "limited" | "unavailable" | "checking",
 *    "carrier": {"device", "device_id"?, "name"?} | null, "backup_ready": bool,
 *    "dns_ok": false when the carrier's path has "dns": {"ok": false}}
 * New object.
 */
json_object *mm_uplink_summary(json_object *paths, const char *default_dev);

/*
 * The router's resolver check (recovery study R34): probes by IP may pass
 * while DNS is dead and no site opens. mm_dns_query writes a standard A query
 * for name with the given id into buf; returns its length, 0 when the name
 * does not fit. mm_dns_answer reads a reply: 1 an answer for that id
 * (RCODE 0, at least one answer record), 0 a reply without one (NXDOMAIN,
 * SERVFAIL, REFUSED, empty), -1 not a reply to that query. rcode gets the
 * code when not NULL.
 */
int mm_dns_query(unsigned char *buf, size_t size, const char *name, unsigned int id);
int mm_dns_answer(const unsigned char *buf, size_t len, unsigned int id, int *rcode);

/*
 * USB power trouble of one modem from the kernel log (recovery study R11): an
 * under-powered modem enumerates partly or not at all, and the kernel says
 * "error -71", "Cannot enable", "over-current", "unable to enumerate" or
 * "device not accepting address" for its device ("usb 1-1.4:") or its hub
 * port ("usb 1-1-port4:", "usb usb1-port1:" for a root port). log is the
 * ring buffer text ("<3>[  123.456789] usb 1-1.4: ..."), usb_path the
 * modem's ("1-1.4"); only lines no older than window_s before now_s count.
 * Returns the count; the last matching line (text after the timestamp) is
 * copied to last.
 */
int mm_usb_power_events(const char *log, const char *usb_path, double now_s, double window_s, char *last, size_t size);
int mm_usb_tx_stalls(const char *log, const char *usb_path, double now_s, double window_s, char *last, size_t size);

#endif
