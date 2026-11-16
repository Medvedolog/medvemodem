// SPDX-License-Identifier: GPL-3.0-only
/*
 * «Порты и устройства» (docs/PORTS-AND-DEVICES.md, the owner 2026-10-22: r84): the USB tree with each device's
 * history and its bounce. A module outside the core: it reads sysfs (usbtree.c) after each discovery pass and once
 * a minute, and the kernel log from /dev/kmsg (its own cursor from the end: logread and dmesg lose nothing); it
 * writes nothing to sysfs and acts on nothing. Recovery stays the core's. RAM only: at most USBT_KEEP devices
 * (absent grey ones go first) and USBT_EVENTS events. No serial number in a view, a log line or an event.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "daemon.h"
#include "hostfix.h"
#include "kfault.h"
#include "sysfs.h"
#include "usbtree.h"

#ifndef MM_KMSG
#define MM_KMSG "/dev/kmsg"
#endif
#define USBT_KEEP 32
#define USBT_MAP_MAX 128		/* id_path, counters: a bench that swaps modems all day stays bounded */
#define USBT_EVENTS 64
#define USBT_STAMPS 64			/* per counter: the hour's stamps */
#define USBT_HOUR_MS (3600 * 1000LL)
#define USBT_BOUNCE_MS (10 * 60 * 1000LL)
#define USBT_EVERY_MS (60 * 1000)

static json_object *snap;		/* path -> node, the last scan */
static json_object *hist;		/* path -> {present, first_seen, last_seen, disconnected_at, devnum, speed, reconnects
					 *  [mono], reconnects_total, speed_changes [{at, from, to}], last {...}, said {...}} */
static json_object *counters;		/* path (or "net:<if>", "hc:<name>") -> {kind: [mono...], kind_total: n} */
static json_object *events;		/* [{t, kind, path, ...}], oldest first */
static json_object *id_path;		/* device_id (by serial) -> the path it was last seen in */
static struct uloop_timeout tick;
static struct uloop_fd kmsg = { .fd = -1 };
static char kbuf[2048];
static size_t klen;
static bool scanned_once, dirty;

static const char *const KINDS[] = { "reset", "err71", "err110", "err32", "enum", "overcurrent", "hc", "watchdog" };

static long long wall(void)
{
	return (long long)time(NULL);
}

/* the oldest key goes first (json-c keeps the insertion order) */
static void cap_map(json_object *o, int max)
{
	while (o && json_object_object_length(o) >= max)
		json_object_object_del(o, (const char *)json_object_get_object(o)->head->k);
}

static void event(const char *kind, const char *path, json_object *extra)
{
	json_object *e = extra ? extra : json_object_new_object();

	if (!events)
		events = json_object_new_array();
	json_object_object_add(e, "t", json_object_new_int64(wall()));
	json_object_object_add(e, "kind", json_object_new_string(kind));
	if (path)
		json_object_object_add(e, "path", json_object_new_string(path));
	json_object_array_add(events, e);
	while (json_object_array_length(events) > USBT_EVENTS)
		json_object_array_del_idx(events, 0, 1);
}

/* the stamps of the last hour (older ones dropped), and how many fall within ms */
static int recent(json_object *stamps, long long now, long long ms)
{
	int n = 0;
	size_t i;

	while (json_object_array_length(stamps) && now - json_object_get_int64(json_object_array_get_idx(stamps, 0)) > USBT_HOUR_MS)
		json_object_array_del_idx(stamps, 0, 1);
	for (i = 0; i < json_object_array_length(stamps); i++)
		if (now - json_object_get_int64(json_object_array_get_idx(stamps, i)) <= ms)
			n++;
	return n;
}

static void stamp(json_object *o, const char *key, long long now)
{
	json_object *a = json_object_object_get(o, key);
	char total[48];

	if (!a) {
		a = json_object_new_array();
		json_object_object_add(o, key, a);
	}
	json_object_array_add(a, json_object_new_int64(now));
	while (json_object_array_length(a) > USBT_STAMPS)
		json_object_array_del_idx(a, 0, 1);
	snprintf(total, sizeof(total), "%s_total", key);
	json_object_object_add(o, total, json_object_new_int64(json_object_get_int64(json_object_object_get(o, total)) + 1));
}

/* the registry's present record of a USB path, or NULL */
static json_object *rec_of(const char *path)
{
	char att[NAME_MAX + 8];

	if (!registry.devices)
		return NULL;
	snprintf(att, sizeof(att), "usb:%s", path);
	json_object_object_foreach(registry.devices, id, rec) {
		(void)id;
		if (json_object_get_boolean(json_object_object_get(rec, "present")) && jstr(rec, "attachment_id") &&
		    !strcmp(jstr(rec, "attachment_id"), att))
			return rec;
	}
	return NULL;
}

/* the path whose interface made a network device, or NULL */
static const char *path_of_net(const char *ifname)
{
	if (!snap)
		return NULL;
	json_object_object_foreach(snap, path, node) {
		json_object *ifs = json_object_object_get(node, "interfaces");
		size_t i, k;

		for (i = 0; i < json_object_array_length(ifs); i++) {
			json_object *nodes = json_object_object_get(json_object_array_get_idx(ifs, i), "nodes");

			for (k = 0; k < json_object_array_length(nodes); k++)
				if (!strcmp(json_object_get_string(json_object_array_get_idx(nodes, k)), ifname))
					return path;
		}
	}
	return NULL;
}

static double speed_of(json_object *node)
{
	json_object *v = json_object_object_get(node, "speed_mbps");

	return v ? json_object_get_double(v) : 0;
}

/* a USB 3 device on a 480 Mbit/s link (or slower): USB 3 lost on the way (cable, connector, a USB 2 hub) */
static bool downgraded(json_object *node)
{
	const char *v = jstr(node, "version");
	double s = speed_of(node);

	return v && atof(v) >= 3.0 && s > 0 && s <= 480;
}

static json_object *slot(const char *path)
{
	json_object *h;

	if (!hist)
		hist = json_object_new_object();
	if (!(h = json_object_object_get(hist, path))) {
		h = json_object_new_object();
		json_object_object_add(h, "first_seen", json_object_new_int64(wall()));
		json_object_object_add(h, "reconnects", json_object_new_array());
		json_object_object_add(h, "speed_changes", json_object_new_array());
		json_object_object_add(hist, path, h);
	}
	return h;
}

/* a bounce said once a minute at most per device, the hour's counts with it */
static void bounce_said(const char *path, json_object *h, long long now)
{
	long long said = json_object_get_int64(json_object_object_get(h, "bounce_said"));
	int n = recent(json_object_object_get(h, "reconnects"), now, USBT_BOUNCE_MS);
	json_object *e;

	if (n < 3 || (said && now - said < 60000))
		return;
	json_object_object_add(h, "bounce_said", json_object_new_int64(now));
	e = json_object_new_object();
	json_object_object_add(e, "count", json_object_new_int(n));
	json_object_object_add(e, "minutes", json_object_new_int((int)(USBT_BOUNCE_MS / 60000)));
	event("bounce", path, e);
	ULOG_WARN("USB %s reconnected %d times in %lld min\n", path, n, USBT_BOUNCE_MS / 60000);
}

/* the last picture of a device, for its port while it is away («был: …») */
static void remember(json_object *h, json_object *node, json_object *rec)
{
	json_object *last = json_object_new_object();
	static const char *const keep[] = { "vidpid", "manufacturer", "product", "class_kind", "kind", NULL };
	int i;

	for (i = 0; keep[i]; i++)
		if (jstr(node, keep[i]))
			json_object_object_add(last, keep[i], json_object_new_string(jstr(node, keep[i])));
	if (json_object_object_get(node, "speed_mbps"))
		json_object_object_add(last, "speed_mbps", json_object_get(json_object_object_get(node, "speed_mbps")));
	json_object_object_add(last, "modem", json_object_new_boolean(rec != NULL));
	if (rec) {
		const char *id = jstr(rec, "device_id"), *name = daemon_modem_name(id);
		json_object *own = json_object_object_get(rec, "owner");

		json_object_object_add(last, "device_id", json_object_new_string(id));
		if (name)
			json_object_object_add(last, "name", json_object_new_string(name));
		if (jstr(own, "control"))
			json_object_object_add(last, "control", json_object_new_string(jstr(own, "control")));
	}
	json_object_object_add(h, "last", last);
}

/* at most USBT_KEEP devices: absent grey ones go first, then the longest gone */
static void trim(void)
{
	while (hist && json_object_object_length(hist) > USBT_KEEP) {
		const char *drop = NULL;
		long long best = 0;
		int pass;

		for (pass = 0; pass < 2 && !drop; pass++) {
			json_object_object_foreach(hist, path, h) {
				long long gone = json_object_get_int64(json_object_object_get(h, "disconnected_at"));
				bool modem = json_object_get_boolean(json_object_object_get(json_object_object_get(h, "last"), "modem"));

				if (json_object_get_boolean(json_object_object_get(h, "present")) || (!pass && modem))
					continue;
				if (!drop || gone < best) {
					drop = path;
					best = gone;
				}
			}
		}
		if (!drop)
			break;
		json_object_object_del(hist, drop);
	}
}

static void refresh(void)
{
	json_object *now_snap = mm_usbtree_scan(), *gone = json_object_new_array();
	long long now = now_mono_ms();
	size_t i;

	json_object_object_foreach(now_snap, path, node) {
		json_object *h = hist ? json_object_object_get(hist, path) : NULL, *rec = rec_of(path), *e;
		bool known = h != NULL, was = known && json_object_get_boolean(json_object_object_get(h, "present"));
		int devnum = json_object_get_int(json_object_object_get(node, "devnum"));
		int had = known ? json_object_get_int(json_object_object_get(h, "devnum")) : 0;
		double sp = speed_of(node), sp0 = known ? json_object_get_double(json_object_object_get(h, "speed")) : 0;

		h = slot(path);
		if (scanned_once && (!was || (devnum && had && devnum != had))) {
			e = json_object_new_object();
			if (jstr(node, "product"))
				json_object_object_add(e, "product", json_object_new_string(jstr(node, "product")));
			json_object_object_add(e, "modem", json_object_new_boolean(rec != NULL));
			/* a devnum that moved under a present device: it re-enumerated between two looks */
			event(was ? "reconnected" : "connected", path, e);
			if (known) {
				stamp(h, "reconnects", now);
				bounce_said(path, h, now);
			}
		}
		if (sp > 0 && sp0 > 0 && sp != sp0) {
			json_object *c = json_object_new_object(), *list = json_object_object_get(h, "speed_changes");

			json_object_object_add(c, "at", json_object_new_int64(wall()));
			json_object_object_add(c, "from", json_object_new_double(sp0));
			json_object_object_add(c, "to", json_object_new_double(sp));
			json_object_array_add(list, c);
			while (json_object_array_length(list) > 8)
				json_object_array_del_idx(list, 0, 1);
			if (sp < sp0) {
				e = json_object_new_object();
				json_object_object_add(e, "from", json_object_new_double(sp0));
				json_object_object_add(e, "to", json_object_new_double(sp));
				event("speed-down", path, e);
				ULOG_WARN("USB %s link down from %g to %g Mbit/s\n", path, sp0, sp);
			}
		} else if (!sp0 && downgraded(node) && !json_object_object_get(h, "said_downgrade")) {
			/* seen first at 480 while it is a USB 3 device */
			json_object_object_add(h, "said_downgrade", json_object_new_boolean(1));
			e = json_object_new_object();
			json_object_object_add(e, "to", json_object_new_double(sp));
			event("speed-down", path, e);
		}
		/* a modem waiting for a kernel driver the router lacks, said once per enumeration. Not any unbound interface: a
		 * modem's ADB or diagnostic one stays unbound while its data works (audit r101 R04; the bench's FM350 and T77) */
		if (rec && json_object_get_int(json_object_object_get(h, "said_nodriver")) != devnum + 1) {
			json_object *miss = mm_hostfix_missing_drivers(rec, kernel_has_driver), *first = json_object_array_get_idx(miss, 0);

			if (first) {
				json_object_object_add(h, "said_nodriver", json_object_new_int(devnum + 1));
				e = json_object_new_object();
				json_object_object_add(e, "driver", json_object_get(json_object_object_get(first, "driver")));
				if (json_object_object_get(first, "package"))
					json_object_object_add(e, "package", json_object_get(json_object_object_get(first, "package")));
				event("no-driver", path, e);
			}
			json_object_put(miss);
		}
		/* the same modem (its own serial) in another port: the Orange Pi Zero 3 renumbers its buses */
		if (rec && jstr(rec, "identity_strength") && !strcmp(jstr(rec, "identity_strength"), "serial")) {
			const char *id = jstr(rec, "device_id"), *before;

			if (!id_path)
				id_path = json_object_new_object();
			before = jstr(id_path, id);
			if (scanned_once && before && strcmp(before, path) && !json_object_object_get(now_snap, before)) {
				e = json_object_new_object();
				json_object_object_add(e, "from", json_object_new_string(before));
				json_object_object_add(e, "device_id", json_object_new_string(id));
				event("moved", path, e);
			}
			if (!before)
				cap_map(id_path, USBT_MAP_MAX);
			json_object_object_add(id_path, id, json_object_new_string(path));
		}
		json_object_object_add(h, "present", json_object_new_boolean(1));
		json_object_object_add(h, "last_seen", json_object_new_int64(wall()));
		json_object_object_del(h, "disconnected_at");
		if (devnum)
			json_object_object_add(h, "devnum", json_object_new_int(devnum));
		if (sp > 0)
			json_object_object_add(h, "speed", json_object_new_double(sp));
		remember(h, node, rec);
	}
	if (hist) {
		json_object_object_foreach(hist, path, h) {
			if (json_object_get_boolean(json_object_object_get(h, "present")) && !json_object_object_get(now_snap, path))
				json_object_array_add(gone, json_object_new_string(path));
		}
	}
	for (i = 0; i < json_object_array_length(gone); i++) {
		const char *path = json_object_get_string(json_object_array_get_idx(gone, i));
		json_object *h = json_object_object_get(hist, path), *last = json_object_object_get(h, "last"), *e;
		bool modem = json_object_get_boolean(json_object_object_get(last, "modem"));

		json_object_object_add(h, "present", json_object_new_boolean(0));
		json_object_object_add(h, "disconnected_at", json_object_new_int64(wall()));
		e = json_object_new_object();
		if (jstr(last, "product"))
			json_object_object_add(e, "product", json_object_new_string(jstr(last, "product")));
		json_object_object_add(e, "modem", json_object_new_boolean(modem));
		event("disconnected", path, e);
		/* a hub gone with its devices: said once, with how many modems went with it */
		if (!strcmp(jstr(last, "kind") ? jstr(last, "kind") : "", "hub")) {
			size_t k, plen = strlen(path);
			int modems = 0, all = 0;

			for (k = 0; k < json_object_array_length(gone); k++) {
				const char *c = json_object_get_string(json_object_array_get_idx(gone, k));

				if (!strncmp(c, path, plen) && c[plen] == '.') {
					all++;
					if (json_object_get_boolean(json_object_object_get(json_object_object_get(
						json_object_object_get(hist, c), "last"), "modem")))
						modems++;
				}
			}
			if (all) {
				e = json_object_new_object();
				json_object_object_add(e, "devices", json_object_new_int(all));
				json_object_object_add(e, "modems", json_object_new_int(modems));
				event("hub-gone", path, e);
				ULOG_WARN("USB hub %s gone with %d devices (%d modems)\n", path, all, modems);
			}
		}
	}
	json_object_put(gone);
	json_object_put(snap);
	snap = now_snap;
	scanned_once = true;
	dirty = false;
	trim();
}

static void counted(const char *kind, const char *who)
{
	long long now = now_mono_ms();
	char key[96];
	const char *path = NULL;
	json_object *c;

	if (!strcmp(kind, "watchdog")) {
		path = path_of_net(who);
		snprintf(key, sizeof(key), "%s%.80s", path ? "" : "net:", path ? path : who);
	} else if (!strcmp(kind, "hc")) {
		snprintf(key, sizeof(key), "hc:%.80s", who);
	} else {
		snprintf(key, sizeof(key), "%.90s", who);
	}
	if (!counters)
		counters = json_object_new_object();
	if (!(c = json_object_object_get(counters, key))) {
		cap_map(counters, USBT_MAP_MAX);
		c = json_object_new_object();
		json_object_object_add(counters, key, c);
	}
	stamp(c, kind, now);
	/* one event a minute at most per device: what the log said in the last 10 min */
	if (now - json_object_get_int64(json_object_object_get(c, "said")) >= 60000 || !json_object_object_get(c, "said")) {
		json_object *e = json_object_new_object();
		size_t k;

		json_object_object_add(c, "said", json_object_new_int64(now));
		for (k = 0; k < sizeof(KINDS) / sizeof(KINDS[0]); k++) {
			json_object *a = json_object_object_get(c, KINDS[k]);
			int n = a ? recent(a, now, USBT_BOUNCE_MS) : 0;

			if (n)
				json_object_object_add(e, KINDS[k], json_object_new_int(n));
		}
		event("usb-errors", key, e);
	}
	if (strcmp(kind, "reset") && strcmp(kind, "watchdog"))
		dirty = true;
}

static void kmsg_line(char *line)
{
	char who[NAME_MAX + 1];
	const char *text = line;
	const char *kind;

	/* a /dev/kmsg record: "pri,seq,usec,flags;text"; its continuation lines (" KEY=value") say nothing here */
	if (line[0] == ' ')
		return;
	/* the same reading of the record as the kernel faults' (caller=… included, review 2026-11-15) */
	mm_kfault_split(line, &text);
	if ((kind = mm_usbtree_kmsg(text, who, sizeof(who))))
		counted(kind, who);
}

static void kmsg_readable(struct uloop_fd *u, unsigned int ev)
{
	(void)ev;
	for (;;) {
		ssize_t n = read(u->fd, kbuf + klen, sizeof(kbuf) - 1 - klen);
		char *nl, *start;

		if (n < 0) {
			if (errno == EPIPE)	/* the ring overwrote records we had not read: go on */
				continue;
			if (errno == EINTR)
				continue;
			return;
		}
		if (n == 0) {
			uloop_fd_delete(u);
			close(u->fd);
			u->fd = -1;
			return;
		}
		klen += (size_t)n;
		kbuf[klen] = '\0';
		start = kbuf;
		while ((nl = strchr(start, '\n'))) {
			*nl = '\0';
			kmsg_line(start);
			start = nl + 1;
		}
		klen = strlen(start);
		memmove(kbuf, start, klen + 1);
		if (klen >= sizeof(kbuf) - 1)
			klen = 0;	/* a line longer than the buffer: dropped */
	}
}

static void tick_fired(struct uloop_timeout *t)
{
	refresh();
	uloop_timeout_set(t, USBT_EVERY_MS);	/* not shortened in tests: a pass or a request refreshes it there */
}

void usbtree_start(void)
{
	tick.cb = tick_fired;
	uloop_timeout_set(&tick, 0);
	kmsg.fd = open(MM_KMSG, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (kmsg.fd < 0) {
		ULOG_INFO("USB tree: no kernel log (%s): the bounce counts reconnects only\n", strerror(errno));
		return;
	}
	lseek(kmsg.fd, 0, SEEK_END);	/* from now: what happened before is logread's */
	kmsg.cb = kmsg_readable;
	uloop_fd_add(&kmsg, ULOOP_READ);
}

void usbtree_sync(void)
{
	dirty = true;
	refresh();
}

void usbtree_stop(void)
{
	uloop_timeout_cancel(&tick);
	if (kmsg.fd >= 0) {
		uloop_fd_delete(&kmsg);
		close(kmsg.fd);
		kmsg.fd = -1;
	}
	json_object_put(snap);
	json_object_put(hist);
	json_object_put(counters);
	json_object_put(events);
	json_object_put(id_path);
	snap = hist = counters = events = id_path = NULL;
}

/* the hour's counts and the totals of one key, and a word for them */
static json_object *bounce_view(const char *key, json_object *h, long long now)
{
	json_object *o = json_object_new_object(), *hour = json_object_new_object(), *total = json_object_new_object();
	json_object *c = counters ? json_object_object_get(counters, key) : NULL;
	int reconnects = h ? recent(json_object_object_get(h, "reconnects"), now, USBT_HOUR_MS) : 0, bad = reconnects;
	size_t k;

	json_object_object_add(hour, "reconnects", json_object_new_int(reconnects));
	json_object_object_add(total, "reconnects", json_object_new_int64(json_object_get_int64(json_object_object_get(h, "reconnects_total"))));
	for (k = 0; k < sizeof(KINDS) / sizeof(KINDS[0]); k++) {
		json_object *a = c ? json_object_object_get(c, KINDS[k]) : NULL;
		char t[48];
		int n = a ? recent(a, now, USBT_HOUR_MS) : 0;

		snprintf(t, sizeof(t), "%s_total", KINDS[k]);
		json_object_object_add(hour, KINDS[k], json_object_new_int(n));
		json_object_object_add(total, KINDS[k], json_object_new_int64(c ? json_object_get_int64(json_object_object_get(c, t)) : 0));
		bad += n;
	}
	json_object_object_add(o, "hour", hour);
	json_object_object_add(o, "total", total);
	/* wording only: the module never acts on it */
	json_object_object_add(o, "state", json_object_new_string(!bad ? "stable" : reconnects >= 3 ? "unstable" : "trouble"));
	return o;
}

/* the operator's names of ports (config usb_port / option path / option name), by path */
static const char *port_label(const char *path)
{
	json_object *img = medvemodem_uci();

	if (!img)
		return NULL;
	json_object_object_foreach(img, sec, s) {
		(void)sec;
		if (jstr(s, ".type") && !strcmp(jstr(s, ".type"), "usb_port") && jstr(s, "path") && !strcmp(jstr(s, "path"), path) &&
		    jstr(s, "name") && *jstr(s, "name"))
			return jstr(s, "name");
	}
	return NULL;
}

static void modem_view(json_object *o, json_object *rec)
{
	const char *id = jstr(rec, "device_id"), *name = daemon_modem_name(id);
	json_object *srv = served(rec), *own = json_object_object_get(srv, "owner"), *roles = json_object_new_array();
	json_object *cand = json_object_object_get(json_object_object_get(srv, "port_roles"), "candidates");
	size_t i;

	json_object_object_add(o, "modem", json_object_new_boolean(1));
	json_object_object_add(o, "device_id", json_object_new_string(id));
	if (name)
		json_object_object_add(o, "name", json_object_new_string(name));
	if (jstr(own, "control"))
		json_object_object_add(o, "control", json_object_new_string(jstr(own, "control")));
	if (frozen(id))
		json_object_object_add(o, "frozen", json_object_new_boolean(1));
	if (jstr(json_object_object_get(srv, "profile"), "id"))
		json_object_object_add(o, "profile", json_object_new_string(jstr(json_object_object_get(srv, "profile"), "id")));
	/* a role only with its evidence: an unproven tty stays «порт», never «AT» */
	for (i = 0; i < json_object_array_length(cand); i++) {
		json_object *c = json_object_array_get_idx(cand, i), *r;

		if (!jstr(c, "node"))
			continue;
		r = json_object_new_object();
		json_object_object_add(r, "node", json_object_new_string(mm_base_name(jstr(c, "node"))));
		if (jstr(c, "role"))
			json_object_object_add(r, "role", json_object_new_string(jstr(c, "role")));
		if (jstr(c, "confidence"))
			json_object_object_add(r, "confidence", json_object_new_string(jstr(c, "confidence")));
		json_object_array_add(roles, r);
	}
	json_object_object_add(o, "roles", roles);
	json_object_put(srv);
}

int m_usbtree(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	      const char *method, struct blob_attr *msg)
{
	json_object *o = json_object_new_object(), *nodes = json_object_new_array(), *absent = json_object_new_array();
	long long now = now_mono_ms();
	int connected = 0, away = 0, rc;
	double best = 0;

	(void)obj; (void)method; (void)msg;
	if (dirty || !snap)
		refresh();
	json_object_object_foreach(snap, path, node) {
		/* a copy: the view adds to it */
		json_object *n = json_tokener_parse(json_object_to_json_string(node));
		json_object *h = hist ? json_object_object_get(hist, path) : NULL, *rec = rec_of(path);
		json_object *ifs = json_object_object_get(node, "interfaces");
		const char *label = port_label(path);
		size_t i;

		if (rec) {
			modem_view(n, rec);
			connected++;
			if (speed_of(node) > best)
				best = speed_of(node);
		}
		for (i = 0; i < json_object_array_length(ifs); i++) {
			const char *p = mm_usbtree_protocol(jstr(json_object_array_get_idx(ifs, i), "driver"));

			if (p && strcmp(p, "serial") && strcmp(p, "WDM") && strcmp(p, "ACM") && !json_object_object_get(n, "protocol"))
				json_object_object_add(n, "protocol", json_object_new_string(p));
		}
		if (label)
			json_object_object_add(n, "label", json_object_new_string(label));
		if (downgraded(node))
			json_object_object_add(n, "downgraded", json_object_new_boolean(1));
		if (h) {
			json_object *hv = json_object_new_object();

			json_object_object_add(hv, "first_seen", json_object_get(json_object_object_get(h, "first_seen")));
			json_object_object_add(hv, "last_seen", json_object_get(json_object_object_get(h, "last_seen")));
			json_object_object_add(hv, "speed_changes", json_object_get(json_object_object_get(h, "speed_changes")));
			json_object_object_add(n, "history", hv);
		}
		json_object_object_add(n, "bounce", bounce_view(path, h, now));
		json_object_array_add(nodes, n);
	}
	if (hist) {
		json_object_object_foreach(hist, path, h) {
			json_object *a;

			if (json_object_get_boolean(json_object_object_get(h, "present")))
				continue;
			a = json_object_new_object();
			json_object_object_add(a, "path", json_object_new_string(path));
			if (port_label(path))	/* a named port keeps its name while it stands empty */
				json_object_object_add(a, "label", json_object_new_string(port_label(path)));
			json_object_object_add(a, "disconnected_at", json_object_get(json_object_object_get(h, "disconnected_at")));
			json_object_object_add(a, "last_seen", json_object_get(json_object_object_get(h, "last_seen")));
			json_object_object_add(a, "last", json_object_get(json_object_object_get(h, "last")));
			json_object_object_add(a, "bounce", bounce_view(path, h, now));
			if (json_object_get_boolean(json_object_object_get(json_object_object_get(h, "last"), "modem")))
				away++;
			json_object_array_add(absent, a);
		}
	}
	json_object_object_add(o, "at", json_object_new_int64(wall()));
	json_object_object_add(o, "nodes", nodes);
	json_object_object_add(o, "absent", absent);
	json_object_object_add(o, "events", events ? json_object_get(events) : json_object_new_array());
	{
		json_object *s = json_object_new_object();

		json_object_object_add(s, "connected", json_object_new_int(connected));
		json_object_object_add(s, "disconnected", json_object_new_int(away));
		if (best > 0)
			json_object_object_add(s, "speed_mbps", json_object_new_double(best));
		json_object_object_add(o, "summary", s);
	}
	json_object_object_add(o, "kernel_log", json_object_new_boolean(kmsg.fd >= 0));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

enum { UD_PATH, __UD_MAX };
const struct blobmsg_policy usbtree_detail_policy[1] = {
	[UD_PATH] = { .name = "path", .type = BLOBMSG_TYPE_STRING },
};

static void read_into(json_object *o, const char *key, const char *dir, const char *name)
{
	char path[PATH_MAX], v[256];

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (mm_read_line(path, v, sizeof(v)) && v[0])
		json_object_object_add(o, key, json_object_new_string(v));
}

/* «по запросу»: the serial, the configurations, the runtime power and the network counters of one device */
int m_usbtree_detail(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		     const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__UD_MAX];
	json_object *o, *nets;
	const char *path;
	char dir[PATH_MAX];
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(usbtree_detail_policy, __UD_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[UD_PATH])
		return UBUS_STATUS_INVALID_ARGUMENT;
	path = blobmsg_get_string(tb[UD_PATH]);
	/* a sysfs name only: "usb1", "1-1", "1-1.2" — nothing that walks out of /sys/bus/usb/devices */
	if (!*path || strlen(path) > 64 || strspn(path, "usb0123456789-.") != strlen(path) || strstr(path, ".."))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!snap || !json_object_object_get(snap, path))
		refresh();
	if (!snap || !json_object_object_get(snap, path))
		return UBUS_STATUS_NOT_FOUND;
	snprintf(dir, sizeof(dir), "%s/%s", SYS_USB, path);
	o = json_object_new_object();
	json_object_object_add(o, "path", json_object_new_string(path));
	read_into(o, "serial", dir, "serial");
	read_into(o, "configurations", dir, "bNumConfigurations");
	read_into(o, "configuration", dir, "bConfigurationValue");
	read_into(o, "device_class", dir, "bDeviceClass");
	read_into(o, "power_control", dir, "power/control");
	read_into(o, "runtime_status", dir, "power/runtime_status");
	read_into(o, "runtime_active_ms", dir, "power/runtime_active_time");
	read_into(o, "runtime_suspended_ms", dir, "power/runtime_suspended_time");
	read_into(o, "max_power", dir, "bMaxPower");
	read_into(o, "removable", dir, "removable");
	read_into(o, "ltm", dir, "ltm_capable");
	read_into(o, "quirks", dir, "quirks");
	nets = json_object_new_object();
	{
		json_object *ifs = json_object_object_get(json_object_object_get(snap, path), "interfaces");
		size_t i, k;

		for (i = 0; i < json_object_array_length(ifs); i++) {
			json_object *list = json_object_object_get(json_object_array_get_idx(ifs, i), "nodes");

			for (k = 0; k < json_object_array_length(list); k++) {
				const char *node = json_object_get_string(json_object_array_get_idx(list, k));
				static const char *const stats[] = { "rx_bytes", "tx_bytes", "rx_errors", "tx_errors", "rx_dropped", "tx_dropped", NULL };
				char sd[PATH_MAX], v[64];
				json_object *c;
				int s;

				snprintf(sd, sizeof(sd), "%s/%s/statistics", SYS_NET, node);
				c = json_object_new_object();
				for (s = 0; stats[s]; s++) {
					char p2[PATH_MAX + 32];

					snprintf(p2, sizeof(p2), "%s/%s", sd, stats[s]);
					if (mm_read_line(p2, v, sizeof(v)))
						json_object_object_add(c, stats[s], json_object_new_int64(atoll(v)));
				}
				if (json_object_object_length(c))
					json_object_object_add(nets, node, c);
				else
					json_object_put(c);
			}
		}
	}
	json_object_object_add(o, "net", nets);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}
