/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_ALERT_H
#define MEDVEMODEM_ALERT_H

#include <stdbool.h>
#include <stddef.h>

#include <json-c/json.h>

/*
 * SMS alerts (spec 38.6; the owner, 2026-10-29: whitelists after 15 min, the internet gone, the traffic moved to
 * another modem, tunnel or adapter, each with hysteresis, and the recovery). Pure: the daemon tells each uplink pass
 * what every link answers and which one carries the traffic; this keeps the episodes and says what to send.
 *
 * obs: {"carrier": key or null, "links": {key: "ok" | "limited" | "none" | "off"}}; a key is a modem's device_id or
 * "dev:<netdev>" for a link that is not a modem (a wired WAN, a tunnel). A link without a verdict is left out.
 *
 * Episodes, each told once and its end told once (restore):
 *   whitelist   a link answers only the operator's whitelist for whitelist_ms; ends when it answers in full for
 *               clear_ms (a short whitelist that ended before whitelist_ms is not told at all)
 *   down        no link answers at all (none of them ok, none limited) for down_ms; ends when one answers for
 *               clear_ms. No link seen yet at all (the engine starting) is not an outage.
 *   switch      the traffic left the link that carried it (the home: the first carrier held for switch_ms) and the
 *               new one held switch_ms; a further move is told again; back on the home for clear_ms is its end.
 *
 * notice: {"kind": "whitelist" | "whitelist-end" | "down" | "down-end" | "switch" | "switch-back", "link"?, "from"?,
 *          "to"?, "for_ms"}: for_ms is how long the episode lasted (an end) or has lasted (a start).
 */
enum { MM_ALERT_WHITELIST, MM_ALERT_DOWN, MM_ALERT_SWITCH, MM_ALERT_RESTORE, MM_ALERT_KINDS };

struct mm_alert_cfg {
	bool on[MM_ALERT_KINDS];
	long long whitelist_ms, down_ms, switch_ms, clear_ms;
};

/* the notices of this pass (a new array, maybe empty); state is kept between calls (an object, empty at first) */
json_object *mm_alert_step(json_object *state, json_object *obs, const struct mm_alert_cfg *cfg, long long now_ms);

/* the SMS of a notice: names maps a key to what a person reads («модем T99», «туннель wg0»); host the router's name
 * or NULL; ru the language. Short: Cyrillic is 70 characters per SMS segment. */
void mm_alert_text(json_object *notice, json_object *names, const char *host, bool ru, char *out, size_t size);

/* the words for a link that is not a modem, by its netdev: "tunnel", "wifi" or "adapter" */
const char *mm_alert_dev_kind(const char *dev);

#endif
