/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "failover.h"

#define RTF_UP_ 0x0001
#define RTF_REJECT_ 0x0200

static json_object *get(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_object_get_ex(o, key, &v) ? v : NULL;
}

static const char *str(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

/* one route per device, the lowest metric */
static void keep(json_object *routes, const char *dev, unsigned long metric)
{
	json_object *seen = NULL;
	size_t i;

	for (i = 0; i < json_object_array_length(routes); i++)
		if (!strcmp(str(json_object_array_get_idx(routes, i), "dev"), dev))
			seen = json_object_array_get_idx(routes, i);
	if (!seen) {
		seen = json_object_new_object();
		json_object_object_add(seen, "dev", json_object_new_string(dev));
		json_object_object_add(seen, "metric", json_object_new_int64((long long)metric));
		json_object_array_add(routes, seen);
	} else if ((long long)metric < json_object_get_int64(get(seen, "metric"))) {
		json_object_object_add(seen, "metric", json_object_new_int64((long long)metric));
	}
}

json_object *mm_failover_routes(const char *route4)
{
	json_object *routes = json_object_new_array();
	const char *line = route4;

	while (line && *line) {
		char dev[32], dst[16], gw[16], mask[16];
		unsigned long flags, metric;
		int ref, use;

		if (sscanf(line, "%31s %15s %15s %lx %d %d %lu %15s", dev, dst, gw, &flags, &ref, &use, &metric, mask) == 8 &&
		    !strcmp(dst, "00000000") && !strcmp(mask, "00000000") && (flags & RTF_UP_) && !(flags & RTF_REJECT_))
			keep(routes, dev, metric);
		line = strchr(line, '\n');
		if (line)
			line++;
	}
	return routes;
}

json_object *mm_failover_routes6(const char *route6)
{
	json_object *routes = json_object_new_array();
	const char *line = route6;

	while (line && *line) {
		char dst[40], src[40], hop[40], dev[32];
		unsigned int dlen, slen;
		unsigned long metric, ref, use, flags;

		if (sscanf(line, "%39s %x %39s %x %39s %lx %lx %lx %lx %31s", dst, &dlen, src, &slen, hop, &metric, &ref, &use,
			   &flags, dev) == 10 &&
		    dlen == 0 && slen == 0 && strspn(dst, "0") == strlen(dst) && (flags & RTF_UP_) && !(flags & RTF_REJECT_))
			keep(routes, dev, metric);
		line = strchr(line, '\n');
		if (line)
			line++;
	}
	return routes;
}

char *mm_failover_main6(void)
{
	struct {
		struct nlmsghdr nh;
		struct rtmsg rt;
	} req = {
		.nh = { .nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg)), .nlmsg_type = RTM_GETROUTE,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP, .nlmsg_seq = 1 },
		.rt = { .rtm_family = AF_INET6 },
	};
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
	char buf[16384], *out = NULL;
	size_t used = 0, cap = 0;
	bool done = false;
	int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);

	struct timeval tv = { .tv_sec = 2 };

	if (fd < 0)
		return NULL;
	/* the engine's main loop reads this: a dump that never ends must not hold it (review 2026-11-15) */
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (sendto(fd, &req, req.nh.nlmsg_len, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(fd);
		return NULL;
	}
	out = calloc(1, 1);
	while (out && !done) {
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		struct nlmsghdr *nh;
		size_t left;

		if (n <= 0)
			break;
		left = (size_t)n;
		for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, left); nh = NLMSG_NEXT(nh, left)) {
			struct rtmsg *rt = NLMSG_DATA(nh);
			struct rtattr *a;
			int alen, table, oif = 0;
			unsigned int metric = 0;
			char dev[IF_NAMESIZE], line[160];
			size_t len;

			if (nh->nlmsg_type == NLMSG_DONE || nh->nlmsg_type == NLMSG_ERROR) {
				done = true;
				break;
			}
			if (nh->nlmsg_type != RTM_NEWROUTE || rt->rtm_dst_len || rt->rtm_src_len || rt->rtm_type != RTN_UNICAST)
				continue;
			table = rt->rtm_table;
			alen = (int)RTM_PAYLOAD(nh);
			for (a = RTM_RTA(rt); RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
				if (a->rta_type == RTA_TABLE)
					table = (int)*(unsigned int *)RTA_DATA(a);
				else if (a->rta_type == RTA_OIF)
					oif = *(int *)RTA_DATA(a);
				else if (a->rta_type == RTA_PRIORITY)
					metric = *(unsigned int *)RTA_DATA(a);
			}
			/* main table only, one device (a multipath route has none) */
			if (table != RT_TABLE_MAIN || !oif || !if_indextoname((unsigned int)oif, dev))
				continue;
			len = (size_t)snprintf(line, sizeof(line), "%032x 00 %032x 00 %032x %08x 00000000 00000000 00000001 %s\n",
					       0, 0, 0, metric, dev);
			if (used + len + 1 > cap) {
				char *more = realloc(out, cap = (used + len + 1) * 2);

				if (!more) {
					free(out);
					out = NULL;
					break;
				}
				out = more;
			}
			memcpy(out + used, line, len + 1);
			used += len;
		}
	}
	close(fd);
	return out;
}

static void move(json_object *moves, const char *dev, long long from, long long to, const char *why)
{
	json_object *m = json_object_new_object();

	json_object_object_add(m, "dev", json_object_new_string(dev));
	json_object_object_add(m, "from", json_object_new_int64(from));
	json_object_object_add(m, "to", json_object_new_int64(to));
	json_object_object_add(m, "why", json_object_new_string(why));
	json_object_array_add(moves, m);
}

/*
 * Is metric held by another device's default route, now or after a move
 * planned this turn? The kernel keeps one IPv4 default route per metric in a
 * table: a second one with the same metric is refused.
 */
static bool taken(json_object *routes, json_object *moves, const char *dev, long long metric)
{
	size_t i;

	for (i = 0; i < json_object_array_length(routes); i++) {
		json_object *r = json_object_array_get_idx(routes, i);
		if (strcmp(str(r, "dev"), dev) && json_object_get_int64(get(r, "metric")) == metric)
			return true;
	}
	for (i = 0; i < json_object_array_length(moves); i++) {
		json_object *m = json_object_array_get_idx(moves, i);
		if (strcmp(str(m, "dev"), dev) && json_object_get_int64(get(m, "to")) == metric)
			return true;
	}
	return false;
}

static bool listed(json_object *list, const char *dev)
{
	size_t i;

	for (i = 0; list && json_object_is_type(list, json_type_array) && i < json_object_array_length(list); i++)
		if (!strcmp(json_object_get_string(json_object_array_get_idx(list, i)), dev))
			return true;
	return false;
}

static bool is_alive(const char *level)
{
	return level && (!strcmp(level, "ok") || !strcmp(level, "limited"));
}

/* the operator's manual link that is not first (the lowest base) */
static bool manual_held(json_object *routes, json_object *state, json_object *manual, const char *dev)
{
	long long base = json_object_get_int64(get(get(state, dev), "base"));
	size_t i;

	if (!listed(manual, dev))
		return false;
	for (i = 0; i < json_object_array_length(routes); i++)
		if (json_object_get_int64(get(get(state, str(json_object_array_get_idx(routes, i), "dev")), "base")) < base)
			return true;
	return false;
}

/*
 * Why a live route is held behind the others, or NULL. A manual link is held
 * unless the operator put it first (the lowest base); a link whose SIM spent
 * its quota (opts.quota) while another live route, neither manual nor over its
 * quota, can carry the traffic; a demoted one (back from dead, failback
 * "demote") while another live route, neither manual nor demoted, can carry
 * the traffic: neither is ever held behind dead ones alone.
 */
static const char *held(json_object *routes, json_object *levels, json_object *state, json_object *manual, bool demote,
			json_object *quota, const char *dev)
{
	size_t i;

	if (manual_held(routes, state, manual, dev))
		return "manual";
	/* an operator's whitelist on this link while another reaches the whole internet: that one carries (the owner,
	 * 2026-10-27: a whitelisted T99 kept the traffic beside a free T77); all limited, nothing moves for it */
	if (str(levels, dev) && !strcmp(str(levels, dev), "limited"))
		for (i = 0; i < json_object_array_length(routes); i++) {
			const char *other = str(json_object_array_get_idx(routes, i), "dev");

			if (strcmp(other, dev) && str(levels, other) && !strcmp(str(levels, other), "ok") && !listed(quota, other) &&
			    !manual_held(routes, state, manual, other))
				return "limited";
		}
	if (listed(quota, dev))
		for (i = 0; i < json_object_array_length(routes); i++) {
			const char *other = str(json_object_array_get_idx(routes, i), "dev");

			if (strcmp(other, dev) && is_alive(str(levels, other)) && !listed(quota, other) &&
			    !manual_held(routes, state, manual, other))
				return "quota";
		}
	if (demote && get(get(state, dev), "demoted"))
		for (i = 0; i < json_object_array_length(routes); i++) {
			const char *other = str(json_object_array_get_idx(routes, i), "dev");

			if (strcmp(other, dev) && is_alive(str(levels, other)) && !get(get(state, other), "demoted") &&
			    !manual_held(routes, state, manual, other))
				return "demoted";
		}
	return NULL;
}

json_object *mm_failover_plan(json_object *routes, json_object *levels, json_object *state, json_object *opts)
{
	json_object *moves = json_object_new_array(), *gone = json_object_new_array(), *manual = get(opts, "manual");
	size_t n = json_object_array_length(routes), i;
	int alive = 0;
	bool active = get(opts, "active") && json_object_get_boolean(get(opts, "active"));
	bool demote = get(opts, "demote") && json_object_get_boolean(get(opts, "demote"));
	long long penalty = json_object_get_int64(get(opts, "penalty"));
	const char *pin = str(opts, "pin");
	bool pinned = false;

	/* bases: a metric we did not set is netifd's */
	for (i = 0; i < n; i++) {
		json_object *r = json_object_array_get_idx(routes, i), *st = get(state, str(r, "dev"));
		long long metric = json_object_get_int64(get(r, "metric"));
		const char *lv = str(levels, str(r, "dev"));

		if (!st) {
			st = json_object_new_object();
			json_object_object_add(st, "base", json_object_new_int64(metric));
			json_object_object_add(state, str(r, "dev"), st);
		} else if (!(get(st, "set") && json_object_get_int64(get(st, "set")) == metric) &&
			   json_object_get_int64(get(st, "base")) != metric) {
			/*
			 * At the penalty or beyond it is a route already put behind, never netifd's base (bench 2026-10-20:
			 * two interfaces on usb1 gave it default routes 20 and 50; the lowest alternated between them as
			 * each was moved, every one was taken as a new base and pushed again, +10000 every 5 s): kept as set.
			 */
			if (penalty > 0 && metric >= penalty) {
				json_object_object_add(st, "set", json_object_new_int64(metric));
			} else {
				json_object_object_add(st, "base", json_object_new_int64(metric));
				json_object_object_del(st, "set");
			}
		}
		alive += is_alive(lv);
		/* back from a death we steered around: demoted until the operator says otherwise */
		if (is_alive(lv) && get(st, "down")) {
			json_object_object_del(st, "down");
			if (demote && active)
				json_object_object_add(st, "demoted", json_object_new_boolean(1));
		}
		if (!demote || !active)
			json_object_object_del(st, "demoted");
	}
	/* routes that went away take their memory with them */
	json_object_object_foreach(state, dev, st) {
		bool present = false;

		(void)st;
		for (i = 0; i < n; i++)
			present = present || !strcmp(str(json_object_array_get_idx(routes, i), "dev"), dev);
		if (!present)
			json_object_array_add(gone, json_object_new_string(dev));
	}
	for (i = 0; i < json_object_array_length(gone); i++)
		json_object_object_del(state, json_object_get_string(json_object_array_get_idx(gone, i)));
	json_object_put(gone);

	/* the operator's pin (opts.pin, «Закрепить»): its route alone carries the traffic, alive or not, failover on or off;
	 * every other one waits behind it. Only while the pinned link has a route: without one the others carry (the kernel
	 * would have nothing else) */
	for (i = 0; pin && i < n; i++)
		pinned = pinned || !strcmp(str(json_object_array_get_idx(routes, i), "dev"), pin);
	for (i = 0; i < n; i++) {
		json_object *r = json_object_array_get_idx(routes, i), *st = get(state, str(r, "dev"));
		const char *dev = str(r, "dev"), *lv = str(levels, dev), *why, *hold = NULL;
		long long metric = json_object_get_int64(get(r, "metric")), base = json_object_get_int64(get(st, "base")), to;

		if (pinned && n >= 2) {
			if (!strcmp(dev, pin)) {
				to = base;
				why = "pinned";
			} else {
				why = "pin";
				if (get(st, "set") && json_object_get_int64(get(st, "set")) == metric)
					continue;
				for (to = base + penalty; taken(routes, moves, dev, to); to++)
					;
			}
		} else if (!active || n < 2) {
			to = base;
			why = "off";
		} else if (!alive) {
			continue;	/* nothing to steer towards: leave the routes as they are */
		} else if ((lv && !strcmp(lv, "down")) || (hold = held(routes, levels, state, manual, demote, get(opts, "quota"), dev))) {
			why = hold ? hold : "down";
			if (!hold)
				json_object_object_add(st, "down", json_object_new_boolean(1));
			/* already behind: it stays where it is */
			if (get(st, "set") && json_object_get_int64(get(st, "set")) == metric)
				continue;
			/* the first metric above base + penalty no other route holds */
			for (to = base + penalty; taken(routes, moves, dev, to); to++)
				;
		} else {
			to = base;
			why = "back";
		}
		if (to == metric)
			continue;
		/* back to a base another device holds now: the kernel would refuse it; wait */
		if (to == base && taken(routes, moves, dev, to))
			continue;
		move(moves, dev, metric, to, why);
		if (to == base)
			json_object_object_del(st, "set");
		else
			json_object_object_add(st, "set", json_object_new_int64(to));
	}
	return moves;
}

void mm_failover_forgive(json_object *state, const char *dev)
{
	json_object_object_foreach(state, d, st)
		if (!dev || !strcmp(d, dev))
			json_object_object_del(st, "demoted");
}

const char *mm_failover_top(json_object *routes, json_object *moves)
{
	const char *top = NULL;
	long long best = 0;
	size_t i, j;

	for (i = 0; routes && i < json_object_array_length(routes); i++) {
		json_object *r = json_object_array_get_idx(routes, i);
		const char *dev = str(r, "dev");
		long long m = json_object_get_int64(get(r, "metric"));

		for (j = 0; moves && j < json_object_array_length(moves); j++) {
			json_object *mv = json_object_array_get_idx(moves, j);

			if (dev && str(mv, "dev") && !strcmp(str(mv, "dev"), dev) && json_object_get_int64(get(mv, "from")) == m)
				m = json_object_get_int64(get(mv, "to"));
		}
		if (dev && (!top || m < best)) {
			top = dev;
			best = m;
		}
	}
	return top;
}
