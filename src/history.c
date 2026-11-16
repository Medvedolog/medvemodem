#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "history.h"

/*
 * Per device: {"events": [...oldest first], "kernel": bool, "hours": [24 slots]}.
 * "kernel" is set once the device has any usb-* event: from then on physical
 * disconnects are counted from kernel events only, never again from registry
 * passes, so one replug is never counted twice. Hour slots hold counters for
 * the 24-hour strip; slot i serves monotonic hour h with h % 24 == i.
 */

/* Interface binds this soon after a usb-add are the enumeration itself. */
#define MM_ENUM_SPREAD_MS 2000
/* Binds this close together are one rebind episode (several interfaces at once). */
#define MM_REBIND_EPISODE_MS 2000
/* ModemManager drops its object some seconds after the USB device went away. */
#define MM_MM_AFTER_USB_MS 15000
/*
 * The engine's own recovery (spec 22.1) re-enumerates on purpose: what happens
 * between recovery-start and recovery-end, and for a short grace after it, is
 * recorded but not counted as instability. A start without an end stops
 * excusing anything after the longest recovery the controller allows.
 */
#define MM_REQUESTED_MAX_MS 300000
#define MM_REQUESTED_GRACE_MS 30000
#define MM_HOUR_MS 3600000LL
#define MM_HOURS 24

static const char *str_of(json_object *e, const char *key)
{
	json_object *v = NULL;
	return json_object_object_get_ex(e, key, &v) ? json_object_get_string(v) : "";
}

static long long num_of(json_object *e, const char *key)
{
	json_object *v = NULL;
	return json_object_object_get_ex(e, key, &v) ? json_object_get_int64(v) : 0;
}

static void bump(json_object *o, const char *key)
{
	json_object_object_add(o, key, json_object_new_int64(num_of(o, key) + 1));
}

/*
 * The counting rules, shared by the sliding window and the hour slots.
 * State carries what the rules need to look back on.
 */
struct mm_count_state {
	bool kernel;
	long long last_usb_add;		/* -1: none */
	long long last_rebind;		/* -1: none */
	long long last_disconnect;	/* -1: none */
	long long requested_until;	/* -1: none; events up to here were asked for */
};

enum mm_count { MM_COUNT_NONE, MM_COUNT_DISCONNECT, MM_COUNT_REENUM, MM_COUNT_DISCONNECT_REENUM, MM_COUNT_REBIND,
		MM_COUNT_MM_CHURN };

static enum mm_count classify_raw(struct mm_count_state *st, const char *kind, const char *detail, long long m);

static enum mm_count classify(struct mm_count_state *st, const char *kind, const char *detail, long long m)
{
	enum mm_count c;

	if (!strcmp(kind, "recovery-start") || !strcmp(kind, "hostfix-start")) {
		st->requested_until = m + MM_REQUESTED_MAX_MS;
		return MM_COUNT_NONE;
	}
	if (!strcmp(kind, "recovery-end") || !strcmp(kind, "hostfix-end")) {
		st->requested_until = m + MM_REQUESTED_GRACE_MS;
		return MM_COUNT_NONE;
	}
	c = classify_raw(st, kind, detail, m);	/* keeps the look-back state either way */
	return st->requested_until >= 0 && m <= st->requested_until ? MM_COUNT_NONE : c;
}

static enum mm_count classify_raw(struct mm_count_state *st, const char *kind, const char *detail, long long m)
{
	if (!strcmp(kind, "usb-remove")) {
		st->last_disconnect = m;
		return MM_COUNT_DISCONNECT;
	}
	/* an MM object that vanished with its USB device is not churn */
	if (!strcmp(kind, "mm-remove"))
		return st->last_disconnect >= 0 && m - st->last_disconnect <= MM_MM_AFTER_USB_MS ? MM_COUNT_NONE : MM_COUNT_MM_CHURN;
	if (!strcmp(kind, "usb-add")) {
		st->last_usb_add = m;
		return MM_COUNT_REENUM;
	}
	if (!strcmp(kind, "driver-bind")) {
		if (st->last_usb_add >= 0 && m - st->last_usb_add <= MM_ENUM_SPREAD_MS)
			return MM_COUNT_NONE;
		if (st->last_rebind >= 0 && m - st->last_rebind <= MM_REBIND_EPISODE_MS)
			return MM_COUNT_NONE;
		st->last_rebind = m;
		return MM_COUNT_REBIND;
	}
	if (st->kernel)
		return MM_COUNT_NONE;
	/* registry passes stand in when the kernel never told us anything */
	if (!strcmp(kind, "removed")) {
		st->last_disconnect = m;
		return MM_COUNT_DISCONNECT;
	}
	if (!strcmp(kind, "reattached"))
		/* returned: its disconnect was counted at "removed" */
		return strcmp(detail, "returned") ? MM_COUNT_DISCONNECT_REENUM : MM_COUNT_REENUM;
	return MM_COUNT_NONE;
}

static json_object *device(struct mm_history *h, const char *id, bool create)
{
	json_object *dev = NULL, *hours;
	int i;

	if (json_object_object_get_ex(h->devices, id, &dev) || !create)
		return dev;
	dev = json_object_new_object();
	json_object_object_add(dev, "events", json_object_new_array());
	json_object_object_add(dev, "kernel", json_object_new_boolean(0));
	json_object_object_add(dev, "mm", json_object_new_boolean(0));
	hours = json_object_new_array();
	for (i = 0; i < MM_HOURS; i++)
		json_object_array_add(hours, json_object_new_object());
	json_object_object_add(dev, "hours", hours);
	json_object_object_add(h->devices, id, dev);
	return dev;
}

static json_object *member(json_object *dev, const char *key)
{
	json_object *v = NULL;
	json_object_object_get_ex(dev, key, &v);
	return v;
}

void mm_history_init(struct mm_history *h, int cap)
{
	h->devices = json_object_new_object();
	h->cap = cap > 0 ? cap : 64;
}

void mm_history_free(struct mm_history *h)
{
	json_object_put(h->devices);
	h->devices = NULL;
}

/* Hour slots count at add time with the same rules; this is the look-back state. */
static struct mm_count_state slot_state(json_object *dev)
{
	struct mm_count_state st = { json_object_get_boolean(member(dev, "kernel")), -1, -1, -1, -1 };
	json_object *list = member(dev, "events");
	size_t i, n = json_object_array_length(list);
	bool requested_seen = false;

	for (i = n; i-- > 0;) {
		json_object *e = json_object_array_get_idx(list, i);
		long long m = num_of(e, "m");

		if (!requested_seen && (!strcmp(str_of(e, "kind"), "recovery-start") || !strcmp(str_of(e, "kind"), "hostfix-start"))) {
			st.requested_until = m + MM_REQUESTED_MAX_MS;
			requested_seen = true;
		}
		if (!requested_seen && (!strcmp(str_of(e, "kind"), "recovery-end") || !strcmp(str_of(e, "kind"), "hostfix-end"))) {
			st.requested_until = m + MM_REQUESTED_GRACE_MS;
			requested_seen = true;
		}

		if (st.last_usb_add < 0 && !strcmp(str_of(e, "kind"), "usb-add"))
			st.last_usb_add = m;
		if (st.last_rebind < 0 && !strcmp(str_of(e, "kind"), "driver-bind"))
			st.last_rebind = num_of(e, "rebind") ? m : -1;
		if (st.last_disconnect < 0 && (!strcmp(str_of(e, "kind"), "usb-remove") ||
					       (!st.kernel && !strcmp(str_of(e, "kind"), "removed"))))
			st.last_disconnect = m;
		if (st.last_usb_add >= 0 && st.last_rebind >= 0 && st.last_disconnect >= 0 && requested_seen)
			break;
	}
	return st;
}

void mm_history_add(struct mm_history *h, const char *id, long long wall, long long mono_ms,
		    const char *kind, const char *detail)
{
	json_object *dev, *list, *slot, *e;
	struct mm_count_state st;
	long long hour = mono_ms / MM_HOUR_MS;
	enum mm_count c;

	if (!id || !kind)
		return;
	dev = device(h, id, true);
	if (!strncmp(kind, "usb-", 4))
		json_object_object_add(dev, "kernel", json_object_new_boolean(1));
	if (!strncmp(kind, "mm-", 3))
		json_object_object_add(dev, "mm", json_object_new_boolean(1));
	st = slot_state(dev);
	c = classify(&st, kind, detail ? detail : "", mono_ms);

	list = member(dev, "events");
	e = json_object_new_object();
	json_object_object_add(e, "t", json_object_new_int64(wall));
	json_object_object_add(e, "m", json_object_new_int64(mono_ms));
	json_object_object_add(e, "kind", json_object_new_string(kind));
	if (detail && *detail)
		json_object_object_add(e, "detail", json_object_new_string(detail));
	if (c == MM_COUNT_REBIND)
		json_object_object_add(e, "rebind", json_object_new_boolean(1));
	if (c == MM_COUNT_NONE && st.requested_until >= 0 && mono_ms <= st.requested_until && strncmp(kind, "recovery-", 9) && strncmp(kind, "hostfix-", 8))
		json_object_object_add(e, "requested", json_object_new_boolean(1));
	json_object_array_add(list, e);
	if ((int)json_object_array_length(list) > h->cap)
		json_object_array_del_idx(list, 0, json_object_array_length(list) - (size_t)h->cap);

	slot = json_object_array_get_idx(member(dev, "hours"), (size_t)(hour % MM_HOURS));
	if (num_of(slot, "h") != hour || !member(slot, "h")) {
		json_object_object_add(slot, "h", json_object_new_int64(hour));
		json_object_object_del(slot, "disconnects");
		json_object_object_del(slot, "reenumerations");
		json_object_object_del(slot, "rebinds");
		json_object_object_del(slot, "mm_churn");
	}
	if (c == MM_COUNT_DISCONNECT || c == MM_COUNT_DISCONNECT_REENUM)
		bump(slot, "disconnects");
	if (c == MM_COUNT_REENUM || c == MM_COUNT_DISCONNECT_REENUM)
		bump(slot, "reenumerations");
	if (c == MM_COUNT_REBIND)
		bump(slot, "rebinds");
	if (c == MM_COUNT_MM_CHURN)
		bump(slot, "mm_churn");
}

json_object *mm_history_events(const struct mm_history *h, const char *id, int limit)
{
	json_object *dev = NULL, *list, *out;
	size_t n, i;

	if (!id || !json_object_object_get_ex(h->devices, id, &dev))
		return NULL;
	list = member(dev, "events");
	out = json_object_new_array();
	n = json_object_array_length(list);
	for (i = 0; i < n && (limit <= 0 || (int)i < limit); i++) {
		json_object *e = json_object_array_get_idx(list, n - 1 - i), *copy = json_object_new_object();

		json_object_object_foreach(e, key, val)
			if (strcmp(key, "rebind"))
				json_object_object_add(copy, key, json_object_get(val));
		json_object_array_add(out, copy);
	}
	return out;
}

json_object *mm_history_stability(const struct mm_history *h, const char *id, long long now_mono,
				  const struct mm_stability_cfg *cfg)
{
	json_object *dev = NULL, *list = NULL, *o = json_object_new_object(), *ev = json_object_new_array();
	json_object *hourly = json_object_new_array();
	long long since = now_mono - (long long)cfg->window_s * 1000, now_h = now_mono / MM_HOUR_MS;
	struct mm_count_state st = { false, -1, -1, -1, -1 };
	int disconnects = 0, reenum = 0, rebinds = 0, churn = 0, age;
	bool mm = false;
	const char *state = "stable", *cause = NULL;
	size_t n = 0, i;
	char line[160];

	if (id && json_object_object_get_ex(h->devices, id, &dev)) {
		list = member(dev, "events");
		n = json_object_array_length(list);
		st.kernel = json_object_get_boolean(member(dev, "kernel"));
		mm = json_object_get_boolean(member(dev, "mm"));
	}
	for (i = 0; i < n; i++) {
		json_object *e = json_object_array_get_idx(list, i);
		long long m = num_of(e, "m");
		enum mm_count c = classify(&st, str_of(e, "kind"), str_of(e, "detail"), m);

		if (m < since)
			continue;
		if (c == MM_COUNT_DISCONNECT || c == MM_COUNT_DISCONNECT_REENUM)
			disconnects++;
		if (c == MM_COUNT_REENUM || c == MM_COUNT_DISCONNECT_REENUM)
			reenum++;
		if (c == MM_COUNT_REBIND)
			rebinds++;
		if (c == MM_COUNT_MM_CHURN)
			churn++;
	}

	if (disconnects >= cfg->flap_disconnects) {
		state = "flapping";
		cause = "usb-link-unstable";
	} else if (rebinds >= cfg->flap_rebinds) {
		state = "flapping";
		cause = "driver-rebind-churn";
	} else if (churn >= cfg->flap_mm_churn) {
		/* spec 25.5.1: the USB device stayed, the control owner did not */
		state = "flapping";
		cause = "control-owner-race";
	} else if (disconnects + rebinds + churn >= cfg->degraded_events) {
		state = "degraded";
		cause = disconnects >= rebinds && disconnects >= churn ? "usb-link-unstable"
			: rebinds >= churn ? "driver-rebind-churn" : "control-owner-race";
	}
	/* Factual wording only: a disconnect is proven, its power cause is not. */
	if (disconnects) {
		snprintf(line, sizeof(line), "USB device left the bus %d time%s in %d s", disconnects,
			 disconnects == 1 ? "" : "s", cfg->window_s);
		json_object_array_add(ev, json_object_new_string(line));
	}
	if (rebinds) {
		snprintf(line, sizeof(line), "interface drivers re-bound %d time%s in %d s while the USB device stayed present",
			 rebinds, rebinds == 1 ? "" : "s", cfg->window_s);
		json_object_array_add(ev, json_object_new_string(line));
	}

	if (churn) {
		snprintf(line, sizeof(line), "ModemManager object recreated %d time%s in %d s while the USB device stayed present",
			 churn, churn == 1 ? "" : "s", cfg->window_s);
		json_object_array_add(ev, json_object_new_string(line));
	}

	/* 24-hour strip, oldest first; hours with nothing are zero, not absent */
	for (age = MM_HOURS - 1; age >= 0; age--) {
		long long hr = now_h - age;
		json_object *slot = dev && hr >= 0 ? json_object_array_get_idx(member(dev, "hours"), (size_t)(hr % MM_HOURS)) : NULL;
		json_object *b = json_object_new_object();
		bool live = slot && member(slot, "h") && num_of(slot, "h") == hr;

		json_object_object_add(b, "hours_ago", json_object_new_int(age));
		json_object_object_add(b, "disconnects", json_object_new_int64(live ? num_of(slot, "disconnects") : 0));
		json_object_object_add(b, "rebinds", json_object_new_int64(live ? num_of(slot, "rebinds") : 0));
		if (mm)
			json_object_object_add(b, "mm_churn", json_object_new_int64(live ? num_of(slot, "mm_churn") : 0));
		json_object_array_add(hourly, b);
	}

	json_object_object_add(o, "state", json_object_new_string(state));
	json_object_object_add(o, "window_seconds", json_object_new_int(cfg->window_s));
	json_object_object_add(o, "physical_disconnects", json_object_new_int(disconnects));
	json_object_object_add(o, "usb_reenumerations", json_object_new_int(reenum));
	json_object_object_add(o, "driver_rebinds", json_object_new_int(rebinds));
	/* a counter appears once its source exists for this device */
	if (mm)
		json_object_object_add(o, "mm_object_churn", json_object_new_int(churn));
	json_object_object_add(o, "probable_cause", cause ? json_object_new_string(cause) : NULL);
	json_object_object_add(o, "evidence", ev);
	json_object_object_add(o, "evidence_source", json_object_new_string(st.kernel ? "kernel" : "registry"));
	json_object_object_add(o, "hourly", hourly);
	json_object_object_add(o, "first_event", n ? json_object_new_int64(num_of(json_object_array_get_idx(list, 0), "t")) : NULL);
	json_object_object_add(o, "last_event", n ? json_object_new_int64(num_of(json_object_array_get_idx(list, n - 1), "t")) : NULL);
	return o;
}

void mm_history_retain(struct mm_history *h, json_object *registry_devices)
{
	for (;;) {
		const char *gone = NULL;

		json_object_object_foreach(h->devices, id, dev) {
			(void)dev;
			if (!json_object_object_get_ex(registry_devices, id, NULL)) {
				gone = id;
				break;
			}
		}
		if (!gone)
			return;
		json_object_object_del(h->devices, gone);
	}
}

json_object *mm_history_snapshot(const struct mm_history *h, const char *boot_id)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "schema", json_object_new_int(2));
	json_object_object_add(o, "boot_id", json_object_new_string(boot_id ? boot_id : ""));
	json_object_object_add(o, "devices", json_object_get(h->devices));
	return o;
}

bool mm_history_restore(struct mm_history *h, json_object *snapshot, const char *boot_id)
{
	json_object *v = NULL, *devs = NULL;

	if (!snapshot || !boot_id || !*boot_id ||
	    !json_object_object_get_ex(snapshot, "schema", &v) || json_object_get_int(v) != 2 ||
	    !json_object_object_get_ex(snapshot, "boot_id", &v) || strcmp(json_object_get_string(v), boot_id) ||
	    !json_object_object_get_ex(snapshot, "devices", &devs) || !json_object_is_type(devs, json_type_object))
		return false;
	/* replaying rebuilds the hour slots and flags with the current rules */
	json_object_object_foreach(devs, id, dev) {
		json_object *list = member(dev, "events");
		size_t i;

		if (!list || !json_object_is_type(list, json_type_array))
			continue;
		for (i = 0; i < json_object_array_length(list); i++) {
			json_object *e = json_object_array_get_idx(list, i);
			mm_history_add(h, id, num_of(e, "t"), num_of(e, "m"), str_of(e, "kind"), str_of(e, "detail"));
		}
	}
	return true;
}
