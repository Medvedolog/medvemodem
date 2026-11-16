#ifndef MEDVEMODEM_SESSION_H
#define MEDVEMODEM_SESSION_H

#include <json-c/json.h>

/*
 * Data session state from netifd (spec 16, 19). Pure.
 *
 * main is `ubus call network.interface.<iface> status`; children maps the
 * dynamic interfaces the protocol creates (<iface>_4, <iface>_6: DHCP inside
 * proto qmi, modemmanager's dhcp bearer method) to their status, or NULL.
 *
 * Returns {"state": "connected" | "connecting" | "down" | "unavailable" | "missing",
 *          "up", "ipv4", "ipv6", "default_route", "l3_device"?, "uptime"?,
 *          "errors": [codes], "autostart"?}. Connected means up with an address and a default
 * route on the interface or one of its children. New object.
 */
json_object *mm_session_assess(json_object *main, json_object *children);

/* Bring-up time budget in ms for a protocol (connect, address, route). */
int mm_session_budget_ms(const char *proto);

#endif
