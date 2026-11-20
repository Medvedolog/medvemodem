// SPDX-License-Identifier: GPL-3.0-only
/*
 * Interfaces whose modem is gone (the owner, 2026-11-20: «а почему после всех перетыканий mm плодятся уже аж до mm10»;
 * «Оба варианта. Список с кнопками сразу, автоматическая уборка через 10 дней.»).
 *
 * Every modem the engine configured keeps its interface name in the ledger while it is away, so a stick back in another
 * port finds its mm0 again. A modem that comes back under another identity (another port for one with no serial, a
 * firmware that changed its USB serial) and a modem gone for good keep theirs for ever, and each new one takes the
 * next free name: mm0..mm10 on a router with two modems. And a release keeps a section it left with the user's fields
 * (the metric «Сделать основным» writes): no proto, the name still taken.
 *
 * Listed here: the interface of each ledger device the registry does not show present (adopted ones excepted: the
 * operator's own interface), and each mmN section no ledger knows and with no proto. «Удалить» removes one at once;
 * one away ORPHAN_AUTO_DAYS (core.orphan_days, 0 = never) with no field of the user's but the metric goes by itself.
 * Since when each is away is on flash (orphans.json): the registry is RAM, a reboot forgets an absent modem. The wall
 * clock is trusted only past 2026-01-01: a router without RTC boots in its build year until NTP answers.
 */
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <libubox/ulog.h>

#include "daemon.h"
#include "ucistore.h"

#define ORPHAN_AUTO_DAYS 10
#define ORPHAN_EVERY_MS 3600000		/* the auto clean-up looks once an hour */
#define ORPHAN_FIRST_MS 300000		/* ... first five minutes after the start: the modems found at boot */
#define ORPHAN_SANE_CLOCK 1767225600LL	/* 2026-01-01 */

static json_object *since;		/* key (device_id, or "network.<iface>" for a section no ledger knows) -> wall s */
static struct uloop_timeout orphan_timer;
static bool since_dirty;

static void since_path(char *buf, size_t size)
{
	snprintf(buf, size, "%s/orphans.json", state_dir);
}

static void since_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	if (!since_dirty || !since)
		return;
	since_path(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(state_dir, 0755);
	if (!json_object_to_file_ext(tmp, since, JSON_C_TO_STRING_PLAIN) && !rename(tmp, path))
		since_dirty = false;
}

static bool present(const char *id)
{
	json_object *rec = mm_registry_find(&registry, id);

	return rec && json_object_get_boolean(json_object_object_get(rec, "present"));
}

static bool adopted(json_object *led)
{
	json_object_object_foreach(json_object_object_get(led, "sections"), k, s) {
		(void)k;
		if (json_object_get_boolean(json_object_object_get(s, "adopted")))
			return true;
	}
	return false;
}

/* mm<digits>: a name the engine gives */
static bool our_name(const char *s)
{
	if (strncmp(s, "mm", 2) || !s[2])
		return false;
	for (s += 2; *s; s++)
		if (!isdigit((unsigned char)*s))
			return false;
	return true;
}

static bool ledger_names(json_object *devs, const char *iface)
{
	json_object_object_foreach(devs, id, led) {
		(void)id;
		if (jstr(led, "interface") && !strcmp(jstr(led, "interface"), iface))
			return true;
	}
	return false;
}

/* the section's fields the engine did not write, but the metric: they keep an interface from the auto clean-up */
static json_object *kept_fields(json_object *sec, json_object *led, const char *iface)
{
	json_object *out = json_object_new_array(), *lf = NULL;
	char key[80];

	snprintf(key, sizeof(key), "network.%s", iface);
	lf = json_object_object_get(json_object_object_get(json_object_object_get(led, "sections"), key), "fields");
	json_object_object_foreach(sec, f, v) {
		json_object *mine = lf ? json_object_object_get(lf, f) : NULL;

		if (f[0] == '.' || !strcmp(f, "metric") ||
		    (mine && !strcmp(json_object_get_string(mine), json_object_get_string(v))))
			continue;
		json_object_array_add(out, json_object_new_string(f));
	}
	return out;
}

static long long since_of(const char *key, long long now)
{
	json_object *v = since ? json_object_object_get(since, key) : NULL;
	long long t = v ? json_object_get_int64(v) : 0;

	return t >= ORPHAN_SANE_CLOCK && t <= now ? t : 0;
}

/* [{interface, device_id?, name?, since?, kept, auto_after?}]; note: record since when each one is away */
static json_object *orphans_list(bool note)
{
	static const char *const npk[] = { "network", NULL };
	json_object *out = json_object_new_array(), *devs = ledger_devices(), *all, *net, *seen = json_object_new_object();
	long long now = (long long)time(NULL), days = atoll(core_opt("orphan_days", "10"));
	bool sane = now >= ORPHAN_SANE_CLOCK;
	char err[160], buf[64];

	if (days < 0)
		days = 0;
	all = mm_uci_read(conf_dir, npk, err, sizeof(err));
	net = all ? json_object_object_get(all, "network") : NULL;
	if (devs && registry.devices) {
		json_object_object_foreach(devs, id, led) {
			const char *iface = jstr(led, "interface");
			json_object *o, *sec, *kept;
			long long t;

			if (id[0] == '@' || !iface || present(id) || adopted(led))
				continue;
			sec = net ? json_object_object_get(net, iface) : NULL;
			t = since_of(id, now);
			if (!t && note && sane) {
				json_object *rec = mm_registry_find(&registry, id);
				long long lc = rec ? json_object_get_int64(json_object_object_get(rec, "last_change")) : 0;

				t = lc >= ORPHAN_SANE_CLOCK && lc <= now ? lc : now;
				json_object_object_add(since, id, json_object_new_int64(t));
				since_dirty = true;
			}
			json_object_object_add(seen, id, NULL);
			o = json_object_new_object();
			json_object_object_add(o, "interface", json_object_new_string(iface));
			json_object_object_add(o, "device_id", json_object_new_string(id));
			json_object_object_add(o, "name", json_object_new_string(daemon_modem_label(id, buf, sizeof(buf))));
			if (ledger_backend(id))
				json_object_object_add(o, "backend", json_object_new_string(ledger_backend(id)));
			if (!sec)
				json_object_object_add(o, "section", json_object_new_boolean(0));
			kept = sec ? kept_fields(sec, led, iface) : json_object_new_array();
			if (frozen(id))
				json_object_array_add(kept, json_object_new_string("frozen"));
			if (t)
				json_object_object_add(o, "since", json_object_new_int64(t));
			if (t && days && !json_object_array_length(kept))
				json_object_object_add(o, "auto_after", json_object_new_int64(t + days * 86400));
			json_object_object_add(o, "kept", kept);
			json_object_array_add(out, o);
		}
	}
	if (net) {
		json_object_object_foreach(net, iface, sec) {
			char key[80];
			json_object *o, *kept;
			long long t;

			if (!jstr(sec, ".type") || strcmp(jstr(sec, ".type"), "interface") || !our_name(iface) ||
			    jstr(sec, "proto") || (devs && ledger_names(devs, iface)) || strlen(iface) > 40)
				continue;
			snprintf(key, sizeof(key), "network.%s", iface);
			t = since_of(key, now);
			if (!t && note && sane) {
				t = now;
				json_object_object_add(since, key, json_object_new_int64(t));
				since_dirty = true;
			}
			json_object_object_add(seen, key, NULL);
			o = json_object_new_object();
			json_object_object_add(o, "interface", json_object_new_string(iface));
			kept = kept_fields(sec, NULL, iface);
			if (t)
				json_object_object_add(o, "since", json_object_new_int64(t));
			if (t && days && !json_object_array_length(kept))
				json_object_object_add(o, "auto_after", json_object_new_int64(t + days * 86400));
			json_object_object_add(o, "kept", kept);
			json_object_array_add(out, o);
		}
	}
	/* a modem back, an interface gone: no time kept for it */
	if (note && since && registry.devices) {
		json_object *drop = json_object_new_array();

		json_object_object_foreach(since, key, v) {
			(void)v;
			if (!json_object_object_get_ex(seen, key, NULL))
				json_object_array_add(drop, json_object_new_string(key));
		}
		for (size_t i = 0; i < json_object_array_length(drop); i++)
			json_object_object_del(since, json_object_get_string(json_object_array_get_idx(drop, i)));
		since_dirty = since_dirty || json_object_array_length(drop);
		json_object_put(drop);
	}
	json_object_put(seen);
	json_object_put(all);
	return out;
}

static const char *status_str(json_object *r)
{
	const char *s = jstr(r, "status");

	return s ? s : "";
}

/* one removed: the modem's history says it (a section no ledger knows has no modem: the log only) */
static json_object *drop_one(const char *id, const char *iface, bool whole, const char *why)
{
	json_object *r = iface_drop(id, iface, whole);
	char detail[96];

	ULOG_INFO("interface %s without its modem%s%s: %s (%s)\n", iface, id ? " " : "", id ? id : "", status_str(r), why);
	if (!strcmp(status_str(r), "committed") || !strcmp(status_str(r), "unchanged")) {
		char key[80];

		snprintf(key, sizeof(key), "network.%s", iface);
		if (since) {
			json_object_object_del(since, id ? id : key);
			since_dirty = true;
		}
		if (id) {
			snprintf(detail, sizeof(detail), "%s: %s", iface, why);
			mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "iface-removed", detail);
			history_dirty = true;
		}
	}
	since_save();
	return r;
}

static void orphan_tick(struct uloop_timeout *t)
{
	json_object *list;
	long long now = (long long)time(NULL);

	uloop_timeout_set(t, uplink_override_ms ? 2 * uplink_override_ms : ORPHAN_EVERY_MS);
	if (!registry.devices)
		return;
	list = orphans_list(true);
	since_save();
	for (size_t i = 0; i < json_object_array_length(list); i++) {
		json_object *o = json_object_array_get_idx(list, i), *a = json_object_object_get(o, "auto_after");
		json_object *r;

		if (!a || json_object_get_int64(a) > now || now < ORPHAN_SANE_CLOCK)
			continue;
		/* QModem's own sections go by its stale pass; the ledger alone here, one a tick (a reload each) */
		char why[64];

		snprintf(why, sizeof(why), "removed after %lld days without the modem", atoll(core_opt("orphan_days", "10")));
		r = drop_one(jstr(o, "device_id"), jstr(o, "interface"), false, why);
		json_object_put(r);
		break;
	}
	json_object_put(list);
}

void orphans_sync(void)
{
	json_object *list;

	if (!since || !registry.devices || now_mono_ms() < (uplink_override_ms ? 0 : ORPHAN_FIRST_MS))
		return;
	list = orphans_list(true);
	since_save();
	json_object_put(list);
}

int m_orphans(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	      const char *method, struct blob_attr *msg)
{
	json_object *list = orphans_list(false), *o = json_object_new_object();
	int rc;

	(void)obj; (void)method; (void)msg;
	json_object_object_add(o, "orphans", list);
	json_object_object_add(o, "auto_days", json_object_new_int64(atoll(core_opt("orphan_days", "10")) > 0 ?
								      atoll(core_opt("orphan_days", "10")) : 0));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

const struct blobmsg_policy orphan_remove_policy[1] = {
	{ .name = "interface", .type = BLOBMSG_TYPE_STRING },
};

int m_orphan_remove(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		    const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[1];
	const char *want;
	json_object *list, *r = NULL;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(orphan_remove_policy, 1, tb, blob_data(msg), blob_len(msg));
	if (!tb[0])
		return UBUS_STATUS_INVALID_ARGUMENT;
	want = blobmsg_get_string(tb[0]);
	list = orphans_list(false);
	for (size_t i = 0; i < json_object_array_length(list) && !r; i++) {
		json_object *o = json_object_array_get_idx(list, i);

		/* only an interface the list shows: a modem present, or the operator's own interface, is never one */
		if (!strcmp(jstr(o, "interface"), want))
			r = drop_one(jstr(o, "device_id"), want, true, "removed by the operator");
	}
	json_object_put(list);
	if (!r)
		return UBUS_STATUS_NOT_FOUND;
	rc = reply_json(ctx, req, NULL, r);
	json_object_put(r);
	return rc;
}

void orphans_start(void)
{
	char path[PATH_MAX];

	since_path(path, sizeof(path));
	since = json_object_from_file(path);
	if (!json_object_is_type(since, json_type_object)) {
		json_object_put(since);
		since = json_object_new_object();
	}
	orphan_timer.cb = orphan_tick;
	uloop_timeout_set(&orphan_timer, uplink_override_ms ? 2 * uplink_override_ms : ORPHAN_FIRST_MS);
}

void orphans_stop(void)
{
	uloop_timeout_cancel(&orphan_timer);
	since_save();
	json_object_put(since);
	since = NULL;
}
