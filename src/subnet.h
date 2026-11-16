/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_SUBNET_H
#define MEDVEMODEM_SUBNET_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/*
 * A modem's IPv4 network inside one of the router's own (field 2026-10-30: an M150-4 UFI handed out 192.168.1.100
 * from 192.168.1.10 while the router's LAN was 192.168.1.1/24; the replies to the internet went to the LAN, its web
 * page and its port forward pointed at the LAN). Pure.
 *
 * addrs: the session's "a.b.c.d/len" strings (session.c's ipv4_addresses); gw: its IPv4 gateway or NULL;
 * name: the router's interface; status: its netifd status ("ipv4-address": [{"address", "mask"}]).
 * Returns {"interface": name, "router": "a.b.c.d/len", "address": "a.b.c.d/len", "gateway"?} for the first overlap
 * (the shorter prefix of the two decides), or NULL.
 */
json_object *mm_subnet_clash(json_object *addrs, const char *gw, const char *name, json_object *status);

/* two "a.b.c.d/len" networks overlap (the shorter prefix decides); false when either does not read */
bool mm_subnet_overlap(const char *a, const char *b);

/* a modem's new a.b.c.1 outside every "a.b.c.d/len" of taken: 192.168.100.1, else .101 to .120; NULL when none */
const char *mm_subnet_pick(json_object *taken, char *out, size_t size);

/* the first host of "a.b.c.d/len" (a UFI's own address when its DHCP gives no gateway: 192.168.100.1); NULL when
 * the prefix is shorter than /8 or longer than /30 */
const char *mm_subnet_first_host(const char *cidr, char *out, size_t size);

#endif
