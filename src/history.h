#ifndef MEDVEMODEM_HISTORY_H
#define MEDVEMODEM_HISTORY_H

#include <stdbool.h>
#include <json-c/json.h>

/*
 * Bounded per-device event history and stability classification (spec 25.5).
 *
 * Pure: callers pass both clocks. Wall time ("t", seconds) is for display;
 * windows use the monotonic clock ("m", milliseconds), so an NTP jump after
 * boot cannot fake or hide instability. Lives in RAM; a snapshot survives a
 * daemon restart within the same boot.
 */
struct mm_history {
	json_object *devices;	/* device_id -> array of events, oldest first */
	int cap;		/* events kept per device */
};

struct mm_stability_cfg {
	int window_s;		/* sliding window for the counters */
	int flap_disconnects;	/* physical disconnects in the window -> flapping */
	int flap_rebinds;	/* driver rebinds with USB present -> flapping */
	int degraded_events;	/* disconnects + rebinds + MM churn in the window -> degraded */
	int flap_mm_churn;	/* ModemManager objects recreated with USB present -> flapping */
};
#define MM_STABILITY_DEFAULTS { 120, 3, 4, 2, 3 }

void mm_history_init(struct mm_history *h, int cap);
void mm_history_free(struct mm_history *h);

/* Append one event. kind: usb-add, usb-remove, driver-bind, driver-unbind,
 * node-add, node-remove (kernel), added, reattached, changed, removed (registry),
 * mm-add, mm-remove, mm-state (ModemManager owner), recovery-start, recovery-end
 * (the engine's own recovery: what happens in between, and 30 s after the end,
 * is recorded with "requested" and not counted as instability). */
void mm_history_add(struct mm_history *h, const char *id, long long wall, long long mono_ms,
		    const char *kind, const char *detail);

/* New array, newest first; limit <= 0 means all. NULL for an unknown device. */
json_object *mm_history_events(const struct mm_history *h, const char *id, int limit);

/* New stability object for the device (stable with zero counters when unknown). */
json_object *mm_history_stability(const struct mm_history *h, const char *id, long long now_mono,
				  const struct mm_stability_cfg *cfg);

/* Drop histories of devices the registry no longer keeps. */
void mm_history_retain(struct mm_history *h, json_object *registry_devices);

/* Snapshot for /tmp; restore only accepts one taken in the same boot. */
json_object *mm_history_snapshot(const struct mm_history *h, const char *boot_id);
bool mm_history_restore(struct mm_history *h, json_object *snapshot, const char *boot_id);

#endif
