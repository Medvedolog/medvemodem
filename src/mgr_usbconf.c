// SPDX-License-Identifier: GPL-3.0-only
/*
 * The operator's USB configuration of a modem (ALPHA1 3, FIELD-VERIFICATION H8): config device / option
 * usb_config <n>. A module with several configurations (the DW5821e: QMI or MBIM, no AT command for it)
 * is put into the one asked for by writing bConfigurationValue; the kernel then probes the drivers of the
 * new interfaces and the record follows by itself. Bounded: USBCONF_TRIES writes per device in
 * USBCONF_WINDOW_MS (the composition may change the generation, so not counted per generation), none while
 * a recovery or a bring-up of the modem runs, none while a usb-mode.json rule of its ID sets configuration 0
 * (they would take turns), and no second write before a discovery pass saw the first; after the tries, none
 * for that number until the option changes. Nothing is written to the modem itself: a replug gives the
 * kernel's choice back for as long as the option is unset.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "daemon.h"

#define USBCONF_TRIES 2
#ifndef USBCONF_WINDOW_MS
#define USBCONF_WINDOW_MS (10 * 60 * 1000)	/* the tests shorten it */
#endif

static json_object *usbconf;	/* device_id -> {"tries": [mono ms...], "pass", "state", "why"} (RAM) */

static const char *wanted_of(const char *id)
{
	json_object *img = medvemodem_uci();

	json_object_object_foreach(img, sec, s) {
		(void)sec;
		if (jstr(s, ".type") && !strcmp(jstr(s, ".type"), "device") && jstr(s, "id") && !strcmp(jstr(s, "id"), id))
			return jstr(s, "usb_config") && *jstr(s, "usb_config") ? jstr(s, "usb_config") : NULL;
	}
	return NULL;
}

static json_object *slot(const char *id)
{
	json_object *e;

	if (!usbconf)
		usbconf = json_object_new_object();
	if (!(e = json_object_object_get(usbconf, id))) {
		e = json_object_new_object();
		json_object_object_add(e, "tries", json_object_new_array());
		json_object_object_add(usbconf, id, e);
	}
	return e;
}

static void say(json_object *e, const char *state, const char *why)
{
	json_object_object_add(e, "state", json_object_new_string(state));
	json_object_object_add(e, "why", why ? json_object_new_string(why) : NULL);
}

/* the tries still inside the window */
static int recent(json_object *e, long long now)
{
	json_object *t = json_object_object_get(e, "tries"), *keep = json_object_new_array();
	size_t i;

	for (i = 0; i < json_object_array_length(t); i++) {
		long long at = json_object_get_int64(json_object_array_get_idx(t, i));
		if (now - at < USBCONF_WINDOW_MS)
			json_object_array_add(keep, json_object_new_int64(at));
	}
	json_object_object_add(e, "tries", keep);
	return (int)json_object_array_length(keep);
}

static void usbconf_one(const char *id, json_object *rec, long long now)
{
	const char *want = wanted_of(id), *have = jstr(rec, "usb_configuration"), *att = jstr(rec, "attachment_id");
	int count = json_object_get_int(json_object_object_get(rec, "usb_configurations")), n;
	json_object *e;
	char path[512], *end;
	long v;
	int fd;

	if (!want) {
		if (usbconf)
			json_object_object_del(usbconf, id);
		return;
	}
	e = slot(id);
	v = strtol(want, &end, 10);
	if (*end || v < 1 || (count > 0 && v > count)) {
		say(e, "refused", "no such configuration");
		return;
	}
	if (have && strtol(have, NULL, 10) == v) {
		say(e, "kept", NULL);
		json_object_object_del(e, "failed_for");
		return;
	}
	if (!json_object_get_boolean(json_object_object_get(rec, "present")) || !att || strncmp(att, "usb:", 4))
		return;
	if (jstr(json_object_object_get(rec, "configuration"), "state") &&
	    !strcmp(jstr(json_object_object_get(rec, "configuration"), "state"), "modeswitch-rule")) {
		say(e, "refused", "a usb-mode.json rule sets configuration 0 for this ID");
		return;
	}
	if (rec_busy(id) || bringup_busy(id)) {
		say(e, "waiting", "a recovery or a connection of the modem runs");
		return;
	}
	/* the last write is not seen until a discovery pass read the device again */
	if (json_object_object_get(e, "pass") &&
	    (long long)registry.passes <= json_object_get_int64(json_object_object_get(e, "pass")))
		return;
	/* a configuration that did not stay is not tried again for the same wish, or the window alone would
	 * re-enumerate the modem twice every 10 minutes for good; «Автоматически» (the option unset) or another
	 * number starts over */
	if (jstr(e, "failed_for") && !strcmp(jstr(e, "failed_for"), want))
		return;
	if ((n = recent(e, now)) >= USBCONF_TRIES) {
		say(e, "failed", "the configuration did not stay after the tries");
		json_object_object_add(e, "failed_for", json_object_new_string(want));
		return;
	}
	snprintf(path, sizeof(path), "%s/%s/bConfigurationValue", SYS_USB, att + 4);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	json_object_array_add(json_object_object_get(e, "tries"), json_object_new_int64(now));
	if (fd < 0 || dprintf(fd, "%ld\n", v) < 0) {
		int err = errno;

		if (fd >= 0)
			close(fd);
		ULOG_ERR("USB configuration %ld of %s not set: %s\n", v, id, strerror(err));
		say(e, "failed", strerror(err));
		return;
	}
	close(fd);
	json_object_object_add(e, "pass", json_object_new_int64((long long)registry.passes));
	ULOG_WARN("USB configuration of %s: %s -> %ld (the operator's usb_config, try %d)\n", id, have ? have : "none", v, n + 1);
	{
		char detail[64];

		snprintf(detail, sizeof(detail), "%s -> %ld", have ? have : "none", v);
		mm_history_add(&history, id, (long long)time(NULL), now, "usb-config", detail);
		history_dirty = true;
	}
	say(e, "set", NULL);
	schedule_pass(false);	/* the new interfaces: their drivers bind in a moment */
}

void usbconf_sync(void)
{
	long long now = now_mono_ms();

	json_object_object_foreach(registry.devices, id, rec)
		usbconf_one(id, rec, now);
}

/*
 * What each configuration carries, from the device's raw descriptors (the kernel keeps all of them in
 * <device>/descriptors, the inactive ones too): the data channel and how many serial (COM) ports, so the
 * choice reads «2 — MBIM, 4 COM» rather than a bare number (the owner, 2026-10-19). Alternate settings
 * other than 0 are not counted; ADB (ff/42/01), CDC data (0a) and storage are neither.
 */
static const char *data_kind(int cls, int sub, int proto, const char *now)
{
	if (cls == 0x02 && sub == 0x0e)
		return "MBIM";
	if (cls == 0x02 && sub == 0x0d)
		return "NCM";
	if (cls == 0x02 && sub == 0x06)
		return "ECM";
	if ((cls == 0xe0 && sub == 0x01 && proto == 0x03) || (cls == 0x02 && sub == 0x02 && proto == 0xff) ||
	    (cls == 0xef && sub == 0x04 && proto == 0x01))
		return "RNDIS";
	if (cls == 0xff && sub == 0xff && proto == 0x50)
		return "QMI";
	return now;
}

static json_object *configs_of(json_object *rec)
{
	const char *att = jstr(rec, "attachment_id");
	unsigned char buf[8192];
	char path[600];
	json_object *list, *cur = NULL;
	const char *data = NULL;
	int serial = 0, cfg = 0;
	ssize_t n;
	size_t i;
	int fd;

	if (!att || strncmp(att, "usb:", 4))
		return NULL;
	snprintf(path, sizeof(path), "%s/%s/descriptors", SYS_USB, att + 4);
	if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0)
		return NULL;
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n < 18)
		return NULL;
	list = json_object_new_array();
	for (i = buf[0] ? buf[0] : 18; i + 2 <= (size_t)n && buf[i] >= 2; i += buf[i]) {
		if (i + buf[i] > (size_t)n)
			break;
		if (buf[i + 1] == 2 && buf[i] >= 9) {		/* a configuration */
			if (cur) {
				json_object_object_add(cur, "data", data ? json_object_new_string(data) : NULL);
				json_object_object_add(cur, "serial", json_object_new_int(serial));
			}
			cur = json_object_new_object();
			cfg = buf[i + 5];
			json_object_object_add(cur, "value", json_object_new_int(cfg));
			json_object_array_add(list, cur);
			data = NULL;
			serial = 0;
		} else if (buf[i + 1] == 4 && buf[i] >= 9 && cur && buf[i + 3] == 0) {	/* an interface, setting 0 */
			int cls = buf[i + 5], sub = buf[i + 6], proto = buf[i + 7];

			data = data_kind(cls, sub, proto, data);
			if ((cls == 0xff && !(sub == 0x42 && proto == 0x01) && !(sub == 0xff && proto == 0x50)) ||
			    (cls == 0x02 && sub == 0x02 && proto >= 0x01 && proto <= 0x06))
				serial++;
		}
	}
	if (cur) {
		json_object_object_add(cur, "data", data ? json_object_new_string(data) : NULL);
		json_object_object_add(cur, "serial", json_object_new_int(serial));
	}
	(void)cfg;
	if (!json_object_array_length(list)) {
		json_object_put(list);
		return NULL;
	}
	return list;
}

json_object *usbconf_view(const char *id, json_object *rec)
{
	json_object *o, *e = usbconf && id ? json_object_object_get(usbconf, id) : NULL;
	int count = json_object_get_int(json_object_object_get(rec, "usb_configurations"));
	const char *want = id ? wanted_of(id) : NULL;

	if (count < 2 && !want)
		return NULL;
	o = json_object_new_object();
	json_object_object_add(o, "count", json_object_new_int(count));
	if (jstr(rec, "usb_configuration"))
		json_object_object_add(o, "current", json_object_new_int(atoi(jstr(rec, "usb_configuration"))));
	if (want)
		json_object_object_add(o, "wanted", json_object_new_int(atoi(want)));
	if (e && jstr(e, "state"))
		json_object_object_add(o, "state", json_object_new_string(jstr(e, "state")));
	if (e && jstr(e, "why"))
		json_object_object_add(o, "why", json_object_new_string(jstr(e, "why")));
	if (jstr(rec, "vidpid"))
		json_object_object_add(o, "usb_id", json_object_new_string(jstr(rec, "vidpid")));
	{
		json_object *cf = configs_of(rec), *names = json_object_object_get(json_object_object_get(rec, "profile"), "usb_configurations");
		size_t i;

		/* the profile's name of each one (QMI, MBIM: the descriptors alone do not always tell, a DW5821e's QMI is ff/ff/ff) */
		for (i = 0; cf && names && i < json_object_array_length(cf); i++) {
			json_object *c = json_object_array_get_idx(cf, i);
			char k[8];

			snprintf(k, sizeof(k), "%d", json_object_get_int(json_object_object_get(c, "value")));
			if (jstr(names, k))
				json_object_object_add(c, "name", json_object_new_string(jstr(names, k)));
		}
		if (cf)
			json_object_object_add(o, "configs", cf);
	}
	return o;
}

void usbconf_stop(void)
{
	json_object_put(usbconf);
	usbconf = NULL;
}
