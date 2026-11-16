/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_FAILOVER_H
#define MEDVEMODEM_FAILOVER_H

#include <json-c/json.h>

/*
 * Uplink failover by route metrics (spec 20.8), off by default. Pure: the
 * daemon reads the routes, runs the reach checks and moves the routes; this
 * decides.
 *
 * The IPv4 default routes of the main table from /proc/net/route, one per
 * device (the lowest metric), in table order: [{"dev", "metric"}]. New array.
 */
json_object *mm_failover_routes(const char *route4);

/*
 * The same for IPv6 from /proc/net/ipv6_route's format: default routes
 * without a source prefix (netifd's source-specific "default from" routes
 * stay netifd's). New array.
 */
json_object *mm_failover_routes6(const char *route6);

/*
 * The IPv6 routes of the main table in /proc/net/ipv6_route's format, read
 * over rtnetlink (the proc file mixes every table): unicast default routes
 * through one device, without a source prefix. The one call here that reads
 * the system. NULL when netlink fails; free().
 */
char *mm_failover_main6(void);

/*
 * The moves for this turn, one address family at a time. routes:
 * mm_failover_routes(6). levels: {dev: "ok" | "limited" | "down"} from the
 * reach checks (a device without a verdict is neither alive nor dead).
 * state: {dev: {"base", "set"?, "down"?, "demoted"?}} kept between turns
 * (updated here): base is the metric netifd gave the route, set the one we
 * put. A metric we did not set is the new base (netifd re-created the route).
 * opts: {"active", "penalty", "manual": [dev], "demote", "quota": [dev]}.
 *
 * Rules: with fewer than two default routes, or with failover off (active
 * false), every route we moved goes back to its base. While at least one
 * route is alive, a dead one goes to base + penalty (traffic steers away; its
 * own route stays, so its check keeps running) and a live one to its base.
 * When none is alive (the upstream, the targets or every link is down)
 * nothing moves. The kernel keeps one IPv4 default route per metric (and
 * IPv6 would make one multipath route of two): a route behind takes the
 * first metric from base + penalty up that no other route holds, and a route
 * waits at its penalty while another holds its base.
 *
 * Held behind like a dead one, though alive: a manual link (opts.manual) that
 * the operator has not put first (the lowest base); with opts.demote, a link
 * back from a death we steered around ("demoted" until mm_failover_forgive)
 * while another live link, neither manual nor demoted, carries the traffic.
 *
 * Held behind the same way while another live link, neither manual nor over
 * its quota, carries the traffic: a link whose SIM spent its monthly quota
 * with action "backup" (opts.quota: [dev], spec 20.9). And a "limited" link
 * (an operator's whitelist) while another, neither manual nor over its quota,
 * reaches the whole internet ("ok"); with every link limited nothing moves.
 *
 * Returns [{"dev", "from", "to", "why": "down" | "manual" | "demoted" |
 * "quota" | "limited" | "back" | "off"}] (new).
 */
json_object *mm_failover_plan(json_object *routes, json_object *levels, json_object *state, json_object *opts);

/*
 * The device that carries the traffic (the lowest metric; the first of a tie, as the kernel picks it) once
 * the moves (or NULL) have landed: the route switch that drops the connections tracked through the device it
 * had (spec 20.8, conntrack). NULL without routes; points into routes.
 */
const char *mm_failover_top(json_object *routes, json_object *moves);

/* the operator takes a demoted link back (dev) or every one (dev NULL) */
void mm_failover_forgive(json_object *state, const char *dev);

#endif
