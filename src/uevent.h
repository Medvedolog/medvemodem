#ifndef MEDVEMODEM_UEVENT_H
#define MEDVEMODEM_UEVENT_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Kernel uevents are hints (spec section 4.1): they only say that the USB
 * picture may have changed. sysfs stays the truth; a relevant event schedules
 * a discovery pass.
 */
struct mm_uevent {
	const char *action;	/* add, remove, bind, unbind, change, move */
	const char *devpath;
	const char *subsystem;
	const char *devtype;
	const char *devname;
	const char *driver;	/* set on bind */
	const char *interface;	/* netdev name */
};

/*
 * Parse one netlink message "action@devpath\0KEY=VALUE\0...". Pointers borrow
 * from buf. Returns false for anything that is not a kernel uevent.
 */
bool mm_uevent_parse(const char *buf, size_t len, struct mm_uevent *ev);

/* True when the event can change what discovery sees. */
bool mm_uevent_relevant(const struct mm_uevent *ev);

/*
 * The USB device ("3-1.2") the event belongs to: the last devpath component
 * naming a USB device, so interface and class-device events resolve to their
 * physical device. Returns false when the path holds none.
 */
bool mm_uevent_usb_name(const char *devpath, char *out, size_t size);

/*
 * History kind for a relevant event: usb-add, usb-remove, driver-bind,
 * driver-unbind, node-add, node-remove; NULL when it is only a hint.
 */
const char *mm_uevent_kind(const struct mm_uevent *ev);

#endif
