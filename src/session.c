#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "session.h"

static json_object *get(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_is_type(o, json_type_object) && json_object_object_get_ex(o, key, &v) ? v : NULL;
}

static bool flag(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_get_boolean(v);
}

static bool nonempty(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_is_type(v, json_type_array) && json_object_array_length(v) > 0;
}

/* a 0.0.0.0/0 or ::/0 entry in the interface's routes */
static bool default_route(json_object *st)
{
	static const char *const lists[] = { "route", "ipv6-route", NULL };
	int l;
	size_t i;

	for (l = 0; lists[l]; l++) {
		json_object *routes = get(st, lists[l]);
		for (i = 0; routes && json_object_is_type(routes, json_type_array) && i < json_object_array_length(routes); i++) {
			json_object *r = json_object_array_get_idx(routes, i), *t = get(r, "target"), *m = get(r, "mask");
			const char *target = t ? json_object_get_string(t) : "";

			if (m && json_object_get_int(m) == 0 && (!strcmp(target, "0.0.0.0") || !strcmp(target, "::")))
				return true;
		}
	}
	return false;
}

/* the nexthop of the first default route, the DNS servers in order, without repeats */
static void network_of(json_object *st, const char **gateway, json_object *dns)
{
	static const char *const lists[] = { "route", "ipv6-route", NULL };
	json_object *servers = get(st, "dns-server");
	int l;
	size_t i, j;

	for (l = 0; !*gateway && lists[l]; l++) {
		json_object *routes = get(st, lists[l]);
		for (i = 0; routes && json_object_is_type(routes, json_type_array) && i < json_object_array_length(routes); i++) {
			json_object *r = json_object_array_get_idx(routes, i), *h = get(r, "nexthop");
			const char *hop = h ? json_object_get_string(h) : NULL;

			if (json_object_get_int(get(r, "mask")) == 0 && hop && strcmp(hop, "0.0.0.0") && strcmp(hop, "::")) {
				*gateway = hop;
				break;
			}
		}
	}
	for (i = 0; servers && json_object_is_type(servers, json_type_array) && i < json_object_array_length(servers); i++) {
		const char *a = json_object_get_string(json_object_array_get_idx(servers, i));
		bool seen = false;

		for (j = 0; j < json_object_array_length(dns); j++)
			seen = seen || !strcmp(a, json_object_get_string(json_object_array_get_idx(dns, j)));
		if (a && *a && !seen)
			json_object_array_add(dns, json_object_new_string(a));
	}
}

/* "10.1.2.3/30" for each address of an up interface, without repeats */
static void addresses_of(json_object *st, const char *key, json_object *out)
{
	json_object *list = get(st, key);
	size_t i, j;

	for (i = 0; list && json_object_is_type(list, json_type_array) && i < json_object_array_length(list); i++) {
		json_object *a = json_object_array_get_idx(list, i), *ad = get(a, "address"), *m = get(a, "mask");
		char buf[80];
		bool seen = false;

		if (!ad || !json_object_get_string(ad) || !*json_object_get_string(ad))
			continue;
		snprintf(buf, sizeof(buf), m ? "%s/%d" : "%s", json_object_get_string(ad), m ? json_object_get_int(m) : 0);
		for (j = 0; j < json_object_array_length(out); j++)
			seen = seen || !strcmp(buf, json_object_get_string(json_object_array_get_idx(out, j)));
		if (!seen)
			json_object_array_add(out, json_object_new_string(buf));
	}
}

/* the nexthop of the first default route of one family */
static const char *gateway_of(json_object *st, const char *list)
{
	json_object *routes = get(st, list);
	size_t i;

	for (i = 0; routes && json_object_is_type(routes, json_type_array) && i < json_object_array_length(routes); i++) {
		json_object *r = json_object_array_get_idx(routes, i), *h = get(r, "nexthop");
		const char *hop = h ? json_object_get_string(h) : NULL;

		if (json_object_get_int(get(r, "mask")) == 0 && hop && strcmp(hop, "0.0.0.0") && strcmp(hop, "::"))
			return hop;
	}
	return NULL;
}

static void errors_of(json_object *st, json_object *out)
{
	json_object *errs = get(st, "errors");
	size_t i;

	for (i = 0; errs && json_object_is_type(errs, json_type_array) && i < json_object_array_length(errs); i++) {
		json_object *c = get(json_object_array_get_idx(errs, i), "code");
		if (c)
			json_object_array_add(out, json_object_new_string(json_object_get_string(c)));
	}
}

json_object *mm_session_assess(json_object *main, json_object *children)
{
	json_object *out = json_object_new_object(), *errors = json_object_new_array(), *dns = json_object_new_array();
	json_object *a4 = json_object_new_array(), *a6 = json_object_new_array();
	const char *gateway = NULL, *gw4 = gateway_of(main, "route"), *gw6 = gateway_of(main, "ipv6-route");
	long long uptime = get(main, "uptime") ? json_object_get_int64(get(main, "uptime")) : -1;
	bool up = flag(main, "up"), v4 = nonempty(main, "ipv4-address"), v6 = nonempty(main, "ipv6-address");
	bool route = default_route(main);
	const char *state;

	errors_of(main, errors);
	network_of(main, &gateway, dns);
	addresses_of(main, "ipv4-address", a4);
	addresses_of(main, "ipv6-address", a6);
	if (children) {
		json_object_object_foreach(children, name, st) {
			(void)name;
			if (!st || !flag(st, "up"))
				continue;
			v4 = v4 || nonempty(st, "ipv4-address");
			v6 = v6 || nonempty(st, "ipv6-address");
			route = route || default_route(st);
			errors_of(st, errors);
			network_of(st, &gateway, dns);
			addresses_of(st, "ipv4-address", a4);
			addresses_of(st, "ipv6-address", a6);
			gw4 = gw4 ? gw4 : gateway_of(st, "route");
			gw6 = gw6 ? gw6 : gateway_of(st, "ipv6-route");
			/* proto modemmanager keeps its addresses on dynamic children (mm0_4): their uptime when the main has none */
			if (uptime <= 0 && get(st, "uptime"))
				uptime = json_object_get_int64(get(st, "uptime"));
		}
	}
	if (!main)
		state = "missing";
	else if (get(main, "available") && !flag(main, "available"))
		state = "unavailable";
	else if (up && (v4 || v6) && route)
		state = "connected";
	else if (up || flag(main, "pending"))
		state = "connecting";
	else
		state = "down";

	json_object_object_add(out, "state", json_object_new_string(state));
	json_object_object_add(out, "up", json_object_new_boolean(up));
	json_object_object_add(out, "ipv4", json_object_new_boolean(v4));
	json_object_object_add(out, "ipv6", json_object_new_boolean(v6));
	json_object_object_add(out, "default_route", json_object_new_boolean(route));
	if (get(main, "l3_device"))
		json_object_object_add(out, "l3_device", json_object_get(get(main, "l3_device")));
	if (uptime >= 0)
		json_object_object_add(out, "uptime", json_object_new_int64(uptime));
	if (gateway)
		json_object_object_add(out, "gateway", json_object_new_string(gateway));
	if (gw4)
		json_object_object_add(out, "gateway4", json_object_new_string(gw4));
	if (gw6)
		json_object_object_add(out, "gateway6", json_object_new_string(gw6));
	json_object_object_add(out, "ipv4_addresses", a4);
	json_object_object_add(out, "ipv6_addresses", a6);
	json_object_object_add(out, "dns", dns);
	if (get(main, "metric"))
		json_object_object_add(out, "metric", json_object_get(get(main, "metric")));
	/* false after an operator's ifdown: a session someone took down on purpose */
	if (get(main, "autostart"))
		json_object_object_add(out, "autostart", json_object_get(get(main, "autostart")));
	json_object_object_add(out, "errors", errors);
	return out;
}

int mm_session_budget_ms(const char *proto)
{
	/* ModemManager may still be enabling and registering; QModem scans, then dials */
	if (proto && (!strcmp(proto, "modemmanager") || !strcmp(proto, "qmodem")))
		return 120000;
	if (proto && (!strcmp(proto, "qmi") || !strcmp(proto, "mbim")))
		return 90000;
	return 60000;
}
