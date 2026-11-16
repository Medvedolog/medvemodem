// SPDX-License-Identifier: GPL-3.0-only
/*
 * A modem whose network lies inside the router's LAN (field 2026-10-30: an M150-4 UFI at 192.168.1.10 handing out
 * 192.168.1.100 behind a router at 192.168.1.1/24): its session is up, yet nothing goes through it, its web page is
 * the router's neighbour and its port forward points into the LAN. Checked when a session comes up, said once per
 * address in the log, served with the session for the page.
 *
 * A web the engine can change (a «Mobile.Router» MF150-4, the owner: «для MF150-4 я бы автоматически устранял
 * конфликт IP») is moved to a free subnet by itself: 192.168.100.1 (its factory one) unless the router or another
 * modem has it, once per clash; then its own interface is taken down and up for a lease of the new subnet. Its
 * web is known only after the first reading, so the move is tried every 20 s for a few minutes.
 *
 * Two modems in one subnet are the same trouble (two UFI sticks with the factory 192.168.100.1): said too, and the
 * one whose web can be changed moves.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <libubox/ulog.h>

#include "daemon.h"
#include "subnet.h"

#define CLASH_TRY_MS 20000
#define CLASH_TRIES 15

static const char *const own[] = { "lan", NULL };	/* the router's networks a modem must stay out of */
static json_object *said;	/* device_id -> the clash last written to the log */
static json_object *moving;	/* device_id -> {key, tries, asked}: a move to try or under way */
static json_object *moved_at;	/* device_id or clash key -> monotonic ms of the last move asked: one in 10 min, never a loop */
static struct uloop_timeout move_timer, bounce_timer;
static char bounce_iface[64];

static void move_tick(struct uloop_timeout *t);

/*
 * Two modems with one subnet (the owner's bench, r123: an MF150-4 and a UZ801 both hand out 192.168.100.x, their
 * factory network; the second one's probes said «Network unreachable» and its web did not open through the router):
 * the other present modem whose session overlaps, as {"interface": "modem", "modem": its id, "router": its address,
 * "address": ours, "gateway"?}, or NULL
 */
static json_object *modem_clash(const char *id, json_object *addrs, const char *gw)
{
	json_object *c = NULL;

	json_object_object_foreach(registry.devices, oid, rec) {
		json_object *srv, *a;

		if (c || !strcmp(oid, id) || !json_object_get_boolean(json_object_object_get(rec, "present")))
			continue;
		srv = served(rec);
		a = json_object_object_get(json_object_object_get(json_object_object_get(srv, "session"), "netifd"), "ipv4_addresses");
		for (size_t j = 0; !c && json_object_is_type(a, json_type_array) && j < json_object_array_length(a); j++)
			for (size_t k = 0; !c && k < json_object_array_length(addrs); k++) {
				const char *theirs = json_object_get_string(json_object_array_get_idx(a, j)),
					   *ours = json_object_get_string(json_object_array_get_idx(addrs, k));

				if (!theirs || !ours || !mm_subnet_overlap(ours, theirs))
					continue;
				c = json_object_new_object();
				json_object_object_add(c, "interface", json_object_new_string("modem"));
				json_object_object_add(c, "modem", json_object_new_string(oid));
				if (jstr(rec, "product"))
					json_object_object_add(c, "modem_name", json_object_new_string(jstr(rec, "product")));
				json_object_object_add(c, "router", json_object_new_string(theirs));
				json_object_object_add(c, "address", json_object_new_string(ours));
				if (gw)
					json_object_object_add(c, "gateway", json_object_new_string(gw));
			}
		json_object_put(srv);
	}
	return c;
}

static void queue_move(const char *id, const char *key)
{
	json_object *m = json_object_new_object();

	if (!moving)
		moving = json_object_new_object();
	json_object_object_add(m, "key", json_object_new_string(key));
	json_object_object_add(moving, id, m);
}

json_object *clash_check(const char *id, json_object *assessment)
{
	json_object *c = NULL, *addrs = NULL;
	char key[160];
	int i;

	if (!id || !json_object_object_get_ex(assessment, "ipv4_addresses", &addrs) ||
	    !json_object_is_type(addrs, json_type_array))
		return NULL;
	for (i = 0; !c && own[i]; i++) {
		json_object *st = netifd_status(own[i]);

		c = mm_subnet_clash(addrs, jstr(assessment, "gateway4"), own[i], st);
		json_object_put(st);
	}
	if (!c)
		c = modem_clash(id, addrs, jstr(assessment, "gateway4"));
	if (!said)
		said = json_object_new_object();
	if (!c) {
		json_object_object_del(said, id);
		return NULL;
	}
	/* between two modems one key whichever of them looks: the lower address first */
	if (jstr(c, "modem") && strcmp(jstr(c, "address"), jstr(c, "router")) > 0)
		snprintf(key, sizeof(key), "%s %s", jstr(c, "router"), jstr(c, "address"));
	else
		snprintf(key, sizeof(key), "%s %s", jstr(c, "address"), jstr(c, "router"));
	if (!jstr(said, id) || strcmp(jstr(said, id), key)) {
		if (jstr(c, "modem"))
			ULOG_WARN("session %s: its address %s%s%s lies in the network %s of the modem %s: the router cannot tell "
				  "the two apart; give one of them another subnet in its web\n", id, jstr(c, "address"),
				  jstr(c, "gateway") ? " from " : "", jstr(c, "gateway") ? jstr(c, "gateway") : "",
				  jstr(c, "router"), jstr(c, "modem"));
		else
			ULOG_WARN("session %s: its address %s%s%s lies in the router's %s network %s: nothing reaches the internet "
				  "through it; give the modem another subnet in its web (e.g. 192.168.8.1)\n", id, jstr(c, "address"),
				  jstr(c, "gateway") ? " from " : "", jstr(c, "gateway") ? jstr(c, "gateway") : "",
				  jstr(c, "interface"), jstr(c, "router"));
		json_object_object_add(said, id, json_object_new_string(key));
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "subnet-clash", jstr(c, "address"));
		history_dirty = true;
		/* a new clash: a web that can be changed is moved (move_tick); between two modems either one may be the
		 * one whose web can be changed, the first moved settles it */
		queue_move(id, key);
		if (jstr(c, "modem")) {
			json_object_object_add(said, jstr(c, "modem"), json_object_new_string(key));
			queue_move(jstr(c, "modem"), key);
		}
		move_timer.cb = move_tick;
		uloop_timeout_set(&move_timer, uplink_override_ms ? uplink_override_ms : 1000);
	}
	return c;
}

/* a move of this clash already asked of one of the two modems */
static bool key_asked(const char *key)
{
	json_object_object_foreach(moving, oid, m) {
		(void)oid;
		if (json_object_get_boolean(json_object_object_get(m, "asked")) && jstr(m, "key") && !strcmp(jstr(m, "key"), key))
			return true;
	}
	return false;
}

/* every IPv4 network of the router and of the other modems' sessions */
static json_object *taken_but(const char *id)
{
	json_object *t = json_object_new_array(), *list;
	int i;

	for (i = 0; own[i]; i++) {
		json_object *st = netifd_status(own[i]);

		if (json_object_object_get_ex(st, "ipv4-address", &list) && json_object_is_type(list, json_type_array))
			for (size_t j = 0; j < json_object_array_length(list); j++) {
				json_object *e = json_object_array_get_idx(list, j);
				char a[64];

				if (jstr(e, "address")) {
					snprintf(a, sizeof(a), "%s/%d", jstr(e, "address"),
						 json_object_get_int(json_object_object_get(e, "mask")));
					json_object_array_add(t, json_object_new_string(a));
				}
			}
		json_object_put(st);
	}
	json_object_object_foreach(registry.devices, oid, rec) {
		json_object *srv, *a;

		if (!strcmp(oid, id) || !json_object_get_boolean(json_object_object_get(rec, "present")))
			continue;
		srv = served(rec);
		a = json_object_object_get(json_object_object_get(json_object_object_get(srv, "session"), "netifd"), "ipv4_addresses");
		for (size_t j = 0; json_object_is_type(a, json_type_array) && j < json_object_array_length(a); j++)
			json_object_array_add(t, json_object_get(json_object_array_get_idx(a, j)));
		json_object_put(srv);
	}
	return t;
}

static bool moved_recently(const char *k)
{
	json_object *t = moved_at && k ? json_object_object_get(moved_at, k) : NULL;

	return t && now_mono_ms() - json_object_get_int64(t) < 600000;
}

static void move_tick(struct uloop_timeout *t)
{
	bool again = false;

	(void)t;
	if (!moving)
		return;
	json_object_object_foreach(moving, id, m) {
		json_object *rec = mm_registry_find(&registry, id), *srv, *taken;
		const char *why = NULL;
		char ip[24];
		int tries = json_object_get_int(json_object_object_get(m, "tries"));

		if (json_object_get_boolean(json_object_object_get(m, "asked")) || tries >= CLASH_TRIES || !rec ||
		    !json_object_get_boolean(json_object_object_get(rec, "present")) || !jstr(said, id) ||
		    strcmp(jstr(said, id), jstr(m, "key")) || key_asked(jstr(m, "key")))
			continue;	/* asked, given up, gone, the clash ended, or the other modem moves */
		if (moved_recently(id) || moved_recently(jstr(m, "key")))
			continue;	/* this modem or this clash moved a moment ago and still clashing: said, not moved again */
		json_object_object_add(m, "tries", json_object_new_int(tries + 1));
		srv = served(rec);
		if (!web_mark(rec, srv)) {
			json_object_put(srv);
			again = true;	/* its web is not read yet, or it is a web the engine cannot change */
			continue;
		}
		taken = taken_but(id);
		if (!mm_subnet_pick(taken, ip, sizeof(ip))) {
			ULOG_WARN("session %s: no free subnet from 192.168.100.0 to 192.168.120.0 to move the modem to\n", id);
			json_object_object_add(m, "asked", json_object_new_boolean(1));
		} else if (web_act(rec, srv, "subnet", ip, NULL, NULL, &why)) {
			ULOG_WARN("session %s: the modem's web is asked to move it to %s/24 (its network clashed with the router's or another modem's)\n", id, ip);
			json_object_object_add(m, "asked", json_object_new_boolean(1));
			if (!moved_at)
				moved_at = json_object_new_object();
			json_object_object_add(moved_at, id, json_object_new_int64(now_mono_ms()));
			json_object_object_add(moved_at, jstr(m, "key"), json_object_new_int64(now_mono_ms()));
		} else
			again = true;	/* the web reader is busy: next time */
		json_object_put(taken);
		json_object_put(srv);
	}
	if (again)
		uloop_timeout_set(&move_timer, uplink_override_ms ? uplink_override_ms : CLASH_TRY_MS);
}

static void bounce_up(struct uloop_timeout *t)
{
	(void)t;
	if (*bounce_iface)
		netifd_do(bounce_iface, "up");
	*bounce_iface = '\0';
}

void clash_moved(const char *id, json_object *reply)
{
	json_object *rec = mm_registry_find(&registry, id), *srv;
	const char *iface;

	if (!json_object_get_boolean(json_object_object_get(reply, "ok"))) {
		ULOG_WARN("session %s: the modem's web did not take the new subnet: %s\n", id,
			  jstr(reply, "error") ? jstr(reply, "error") : "no answer");
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "subnet-move-failed", NULL);
		history_dirty = true;
		return;
	}
	ULOG_WARN("session %s: the modem moved to %s/24; its interface is brought up again for a new lease\n", id,
		  jstr(reply, "address") ? jstr(reply, "address") : "?");
	mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "subnet-moved",
		       jstr(reply, "address") ? jstr(reply, "address") : NULL);
	history_dirty = true;
	if (!rec)
		return;
	/* only our own interface, down and up (never the network) */
	srv = served(rec);
	iface = jstr(json_object_object_get(srv, "session"), "interface");
	/* one bounce at a time: another interface still down comes up now, not never (review 2026-11-15) */
	if (iface && *bounce_iface && strcmp(bounce_iface, iface)) {
		uloop_timeout_cancel(&bounce_timer);
		netifd_do(bounce_iface, "up");
		*bounce_iface = '\0';
	}
	if (iface && netifd_do(iface, "down")) {
		snprintf(bounce_iface, sizeof(bounce_iface), "%s", iface);
		bounce_timer.cb = bounce_up;
		uloop_timeout_set(&bounce_timer, 3000);	/* the stick restarts its DHCP server meanwhile */
	}
	json_object_put(srv);
}

void clash_mgr_stop(void)
{
	uloop_timeout_cancel(&move_timer);
	uloop_timeout_cancel(&bounce_timer);
	json_object_put(said);
	json_object_put(moving);
	json_object_put(moved_at);
	said = moving = moved_at = NULL;
}
