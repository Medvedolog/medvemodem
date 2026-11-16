#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uplink.h"

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

static long long num(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v ? json_object_get_int64(v) : 0;
}

static bool is(const char *a, const char *b)
{
	return a && b && !strcmp(a, b);
}

/* walk lines; each parser keeps the lowest metric seen */
static void best4(const char *text, char *out, size_t size, unsigned long *best, bool *found)
{
	const char *line = text;

	while (line && *line) {
		char dev[32], dst[16], gw[16], mask[16];
		unsigned long flags, metric;
		int ref, use;

		if (sscanf(line, "%31s %15s %15s %lx %d %d %lu %15s", dev, dst, gw, &flags, &ref, &use, &metric, mask) == 8 &&
		    !strcmp(dst, "00000000") && !strcmp(mask, "00000000") && (flags & RTF_UP_) && !(flags & RTF_REJECT_) &&
		    (!*found || metric < *best)) {
			snprintf(out, size, "%s", dev);
			*best = metric;
			*found = true;
		}
		line = strchr(line, '\n');
		if (line)
			line++;
	}
}

static void best6(const char *text, char *out, size_t size, unsigned long *best, bool *found)
{
	const char *line = text;

	while (line && *line) {
		char dst[40], src[40], hop[40], dev[32];
		unsigned int dlen, slen;
		unsigned long metric, ref, use, flags;

		if (sscanf(line, "%39s %x %39s %x %39s %lx %lx %lx %lx %31s", dst, &dlen, src, &slen, hop, &metric, &ref, &use,
			   &flags, dev) == 10 &&
		    dlen == 0 && strspn(dst, "0") == strlen(dst) && (flags & RTF_UP_) && !(flags & RTF_REJECT_) &&
		    strcmp(dev, "lo") && (!*found || metric < *best)) {
			snprintf(out, size, "%s", dev);
			*best = metric;
			*found = true;
		}
		line = strchr(line, '\n');
		if (line)
			line++;
	}
}

const char *mm_uplink_default_dev(const char *route4, const char *route6, char *out, size_t size)
{
	unsigned long best = 0;
	bool found = false;

	if (route4)
		best4(route4, out, size, &best, &found);
	if (!found && route6)
		best6(route6, out, size, &best, &found);
	return found ? out : NULL;
}

static json_object *bucket(json_object *state, const char *name, const char *key)
{
	json_object *b = get(state, name);

	if (!b || !is(str(b, "key"), key)) {
		b = json_object_new_object();
		json_object_object_add(b, "key", json_object_new_string(key ? key : ""));
		json_object_object_add(b, "rx_bytes", json_object_new_int64(0));
		json_object_object_add(b, "tx_bytes", json_object_new_int64(0));
		json_object_object_add(state, name, b);
	}
	return b;
}

static void add(json_object *o, const char *key, long long v)
{
	json_object_object_add(o, key, json_object_new_int64(num(o, key) + v));
}

void mm_traffic_sample(json_object *state, long long rx, long long tx, long long now_ms, bool restart,
		       const char *day, const char *month)
{
	json_object *last = get(state, "last"), *l;
	long long drx = 0, dtx = 0, dt;

	if (last) {
		/* a counter going down: the netdev was re-created, it counts from zero */
		drx = rx >= num(last, "rx") ? rx - num(last, "rx") : rx;
		dtx = tx >= num(last, "tx") ? tx - num(last, "tx") : tx;
		dt = now_ms - num(last, "ms");
		if (dt > 0) {
			json_object_object_add(state, "rx_rate", json_object_new_int64(drx * 1000 / dt));
			json_object_object_add(state, "tx_rate", json_object_new_int64(dtx * 1000 / dt));
		}
	}
	if (restart || !get(state, "rx_bytes")) {
		json_object_object_add(state, "rx_bytes", json_object_new_int64(0));
		json_object_object_add(state, "tx_bytes", json_object_new_int64(0));
	} else {
		add(state, "rx_bytes", drx);
		add(state, "tx_bytes", dtx);
	}
	if (restart) {
		json_object_object_del(state, "rx_rate");
		json_object_object_del(state, "tx_rate");
	}
	/* day and month count every byte seen, across sessions */
	l = bucket(state, "day", day);
	add(l, "rx_bytes", drx);
	add(l, "tx_bytes", dtx);
	l = bucket(state, "month", month);
	add(l, "rx_bytes", drx);
	add(l, "tx_bytes", dtx);
	l = json_object_new_object();
	json_object_object_add(l, "rx", json_object_new_int64(rx));
	json_object_object_add(l, "tx", json_object_new_int64(tx));
	json_object_object_add(l, "ms", json_object_new_int64(now_ms));
	json_object_object_add(state, "last", l);
}

json_object *mm_traffic_view(json_object *state)
{
	json_object *o = json_object_new_object();

	json_object_object_foreach(state, k, v)
		if (strcmp(k, "last")) {
			json_object *c = NULL;	/* a copy: the buckets change in place */
			json_object_deep_copy(v, &c, NULL);
			json_object_object_add(o, k, c);
		}
	return o;
}

static const char *level_of(json_object *r)
{
	if (json_object_get_boolean(get(r, "ok")))
		return "ok";
	return json_object_get_boolean(get(r, "limited")) ? "limited" : "down";
}

static json_object *verdict(json_object *from, const char *level)
{
	json_object *o = json_object_new_object();

	json_object_object_foreach(from, k, v)
		if (strcmp(k, "pending") && strcmp(k, "level"))
			json_object_object_add(o, k, json_object_get(v));
	json_object_object_add(o, "ok", json_object_new_boolean(is(level, "ok")));
	json_object_object_del(o, "limited");
	if (is(level, "limited"))
		json_object_object_add(o, "limited", json_object_new_boolean(true));
	json_object_object_add(o, "level", json_object_new_string(level));
	return o;
}

json_object *mm_reach_judge(json_object *prev, json_object *raw, bool *recheck)
{
	const char *now = level_of(raw), *was = prev ? str(prev, "level") : NULL, *plevel;
	json_object *pend = get(prev, "pending"), *o, *p;
	int count;

	*recheck = false;
	if (!was && prev)
		was = level_of(prev);
	if (!was || is(now, was))
		return verdict(raw, now);
	plevel = str(pend, "level");
	count = (is(plevel, now) ? (int)num(pend, "count") : 0) + 1;
	if (count >= (is(was, "ok") ? 3 : 2))
		return verdict(raw, now);
	o = verdict(prev, was);
	p = json_object_new_object();
	json_object_object_add(p, "level", json_object_new_string(now));
	json_object_object_add(p, "count", json_object_new_int(count));
	if (str(raw, "error"))
		json_object_object_add(p, "error", json_object_new_string(str(raw, "error")));
	json_object_object_add(o, "pending", p);
	*recheck = true;
	return o;
}

const char *mm_uplink_role(const char *session_state, const char *dev, const char *default_dev, json_object *reach)
{
	bool carrying = is(dev, default_dev);
	const char *level;

	if (!dev || !is(session_state, "connected"))
		return "down";
	if (!reach || !get(reach, "ok"))
		return "checking";
	level = level_of(reach);
	if (carrying)
		return is(level, "ok") ? "carrying" : is(level, "limited") ? "limited" : "no_internet";
	return is(level, "ok") ? "backup_ready" : is(level, "limited") ? "backup_limited" : "backup_not_ready";
}

json_object *mm_uplink_summary(json_object *paths, const char *default_dev)
{
	json_object *o = json_object_new_object(), *carrier = NULL;
	const char *internet = "unavailable";
	bool backup = false;
	size_t i;

	for (i = 0; paths && i < json_object_array_length(paths); i++) {
		json_object *p = json_object_array_get_idx(paths, i), *reach = get(p, "reach");

		if (is(str(p, "role"), "backup_ready"))
			backup = true;
		if (!is(str(p, "device"), default_dev) || carrier)
			continue;
		carrier = json_object_new_object();
		json_object_object_add(carrier, "device", json_object_new_string(default_dev));
		if (str(p, "device_id"))
			json_object_object_add(carrier, "device_id", json_object_new_string(str(p, "device_id")));
		if (str(p, "name"))
			json_object_object_add(carrier, "name", json_object_new_string(str(p, "name")));
		if (get(p, "dns") && !json_object_get_boolean(get(get(p, "dns"), "ok")))
			json_object_object_add(o, "dns_ok", json_object_new_boolean(false));
		internet = !reach || !get(reach, "ok") ? "checking" :
			   json_object_get_boolean(get(reach, "ok")) ? "available" :
			   json_object_get_boolean(get(reach, "limited")) ? "limited" : "unavailable";
	}
	if (!carrier && default_dev) {
		/* a default route through something nobody probed */
		carrier = json_object_new_object();
		json_object_object_add(carrier, "device", json_object_new_string(default_dev));
		internet = "checking";
	}
	json_object_object_add(o, "internet", json_object_new_string(internet));
	json_object_object_add(o, "carrier", carrier);
	json_object_object_add(o, "backup_ready", json_object_new_boolean(backup));
	return o;
}

int mm_dns_query(unsigned char *buf, size_t size, const char *name, unsigned int id)
{
	size_t n = 12, label;
	const char *p = name, *dot;

	if (!name || !*name || size < 12 + strlen(name) + 2 + 4)
		return 0;
	memset(buf, 0, 12);
	buf[0] = (unsigned char)(id >> 8);
	buf[1] = (unsigned char)id;
	buf[2] = 0x01;			/* recursion desired */
	buf[5] = 1;			/* one question */
	while (*p) {
		dot = strchr(p, '.');
		label = dot ? (size_t)(dot - p) : strlen(p);
		if (!label || label > 63)
			return 0;
		buf[n++] = (unsigned char)label;
		memcpy(buf + n, p, label);
		n += label;
		p += label + (dot ? 1 : 0);
	}
	buf[n++] = 0;
	buf[n++] = 0;
	buf[n++] = 1;			/* type A */
	buf[n++] = 0;
	buf[n++] = 1;			/* class IN */
	return (int)n;
}

int mm_dns_answer(const unsigned char *buf, size_t len, unsigned int id, int *rcode)
{
	int code;

	if (len < 12 || buf[0] != (unsigned char)(id >> 8) || buf[1] != (unsigned char)id || !(buf[2] & 0x80))
		return -1;
	code = buf[3] & 0x0f;
	if (rcode)
		*rcode = code;
	return !code && (buf[6] || buf[7]) ? 1 : 0;
}

static bool power_words(const char *text)
{
	return strstr(text, "error -71") || strstr(text, "Cannot enable") || strstr(text, "over-current") ||
	       strstr(text, "unable to enumerate") || strstr(text, "not accepting address");
}

/* "usb 1-1.4:" names the device; "usb 1-1-port4:" or "usb usb1-port4:" its port on the hub */
static bool names_path(const char *token, size_t n, const char *path)
{
	char port[96];
	const char *dot = strrchr(path, '.'), *dash;

	if (strlen(path) == n && !strncmp(token, path, n))
		return true;
	if (dot)
		snprintf(port, sizeof(port), "%.*s-port%s", (int)(dot - path), path, dot + 1);
	else if ((dash = strchr(path, '-')))
		snprintf(port, sizeof(port), "usb%.*s-port%s", (int)(dash - path), path, dash + 1);
	else
		return false;
	return strlen(port) == n && !strncmp(token, port, n);
}

int mm_usb_power_events(const char *log, const char *usb_path, double now_s, double window_s, char *last, size_t size)
{
	const char *line = log;
	int count = 0;

	if (last && size)
		*last = 0;
	while (line && *line && usb_path && *usb_path) {
		const char *end = strchr(line, '\n'), *open = strchr(line, '['), *close, *text, *colon;
		size_t len = end ? (size_t)(end - line) : strlen(line);

		if (open && open < line + len && (close = strchr(open, ']')) && close < line + len) {
			double ts = strtod(open + 1, NULL);

			text = close + 1;
			while (*text == ' ')
				text++;
			if (ts <= now_s && now_s - ts <= window_s && !strncmp(text, "usb ", 4) &&
			    (colon = strchr(text + 4, ':')) && colon < line + len &&
			    names_path(text + 4, (size_t)(colon - text - 4), usb_path)) {
				char buf[256];
				size_t tl = (size_t)(line + len - text);

				snprintf(buf, sizeof(buf), "%.*s", (int)(tl < sizeof(buf) ? tl : sizeof(buf) - 1), text);
				if (power_words(buf)) {
					count++;
					if (last && size)
						snprintf(last, size, "%s", buf);
				}
			}
		}
		line = end ? end + 1 : NULL;
	}
	return count;
}

/*
 * The kernel's "NETDEV WATCHDOG: ... transmit queue N timed out" for a network interface of the device at
 * usb_path ("rndis_host 3-1.3:1.0 eth1: NETDEV WATCHDOG: ..."): the data path hangs in the driver, which a
 * reconnect does not cure (bench 2026-10-20: an FM350 over RNDIS, every 2-3 minutes for the whole uptime)
 */
int mm_usb_tx_stalls(const char *log, const char *usb_path, double now_s, double window_s, char *last, size_t size)
{
	const char *line = log;
	size_t pl = usb_path ? strlen(usb_path) : 0;
	int count = 0;

	if (last && size)
		*last = 0;
	while (line && *line && pl) {
		const char *end = strchr(line, '\n'), *open = strchr(line, '['), *close, *text, *sp, *wd;
		size_t len = end ? (size_t)(end - line) : strlen(line);

		if (open && open < line + len && (close = strchr(open, ']')) && close < line + len) {
			double ts = strtod(open + 1, NULL);

			text = close + 1;
			while (*text == ' ')
				text++;
			sp = strchr(text, ' ');
			wd = strstr(text, "NETDEV WATCHDOG");
			if (ts <= now_s && now_s - ts <= window_s && sp && sp < line + len && wd && wd < line + len &&
			    !strncmp(sp + 1, usb_path, pl) && sp[1 + pl] == ':') {
				size_t tl = (size_t)(line + len - text);

				count++;
				if (last && size)
					snprintf(last, size, "%.*s", (int)(tl < size ? tl : size - 1), text);
			}
		}
		line = end ? end + 1 : NULL;
	}
	return count;
}
