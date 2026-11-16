#define _GNU_SOURCE
#include <ctype.h>
#include <string.h>

#include "uevent.h"

bool mm_uevent_parse(const char *buf, size_t len, struct mm_uevent *ev)
{
	const char *p = buf, *end = buf + len, *at;
	size_t n;

	memset(ev, 0, sizeof(*ev));
	if (!buf || !len || buf[len - 1] != '\0')
		return false;
	/* udev re-broadcasts start with "libudev"; we only take the kernel's. */
	at = strchr(p, '@');
	if (!at || !strncmp(p, "libudev", 7))
		return false;
	n = strlen(p) + 1;
	for (p += n; p < end; p += n) {
		n = strlen(p) + 1;
		if (!strncmp(p, "ACTION=", 7))
			ev->action = p + 7;
		else if (!strncmp(p, "DEVPATH=", 8))
			ev->devpath = p + 8;
		else if (!strncmp(p, "SUBSYSTEM=", 10))
			ev->subsystem = p + 10;
		else if (!strncmp(p, "DEVTYPE=", 8))
			ev->devtype = p + 8;
		else if (!strncmp(p, "DEVNAME=", 8))
			ev->devname = p + 8;
		else if (!strncmp(p, "DRIVER=", 7))
			ev->driver = p + 7;
		else if (!strncmp(p, "INTERFACE=", 10))
			ev->interface = p + 10;
	}
	return ev->action && ev->devpath && ev->subsystem;
}

static bool on_usb(const char *devpath)
{
	return strstr(devpath, "/usb") != NULL;
}

bool mm_uevent_relevant(const struct mm_uevent *ev)
{
	static const char *const actions[] = { "add", "remove", "bind", "unbind", "change", "move" };
	bool action = false;
	size_t i;

	for (i = 0; i < sizeof(actions) / sizeof(actions[0]); i++)
		if (!strcmp(ev->action, actions[i]))
			action = true;
	if (!action)
		return false;
	if (!strcmp(ev->subsystem, "usb"))
		return true;
	/* Class devices matter only when they hang off a USB interface. */
	if (!strcmp(ev->subsystem, "tty") || !strcmp(ev->subsystem, "usbmisc") ||
	    !strcmp(ev->subsystem, "net") || !strcmp(ev->subsystem, "usb-serial"))
		return on_usb(ev->devpath);
	return false;
}

/* "3-1.2": bus, dash, port chain of digits and dots; no ':' (that is an interface). */
static bool is_usb_device_name(const char *s, size_t n)
{
	size_t i = 0;
	bool dash = false;

	if (!n || !isdigit((unsigned char)s[0]))
		return false;
	for (; i < n; i++) {
		if (s[i] == '-') {
			if (dash || i == 0 || i + 1 == n)
				return false;
			dash = true;
		} else if (s[i] != '.' && !isdigit((unsigned char)s[i])) {
			return false;
		}
	}
	return dash;
}

bool mm_uevent_usb_name(const char *devpath, char *out, size_t size)
{
	const char *p = devpath, *best = NULL;
	size_t best_n = 0;

	if (!devpath || !size)
		return false;
	while (*p) {
		const char *end;

		while (*p == '/')
			p++;
		end = strchrnul(p, '/');
		if (is_usb_device_name(p, (size_t)(end - p))) {
			best = p;
			best_n = (size_t)(end - p);
		}
		p = end;
	}
	if (!best || best_n >= size)
		return false;
	memcpy(out, best, best_n);
	out[best_n] = '\0';
	return true;
}

const char *mm_uevent_kind(const struct mm_uevent *ev)
{
	bool add = !strcmp(ev->action, "add"), remove = !strcmp(ev->action, "remove");

	if (!strcmp(ev->subsystem, "usb")) {
		if (ev->devtype && !strcmp(ev->devtype, "usb_device"))
			return add ? "usb-add" : remove ? "usb-remove" : NULL;
		/* every plug binds the device to "usb"; only interface drivers count */
		if (!ev->devtype || strcmp(ev->devtype, "usb_interface"))
			return NULL;
		if (!strcmp(ev->action, "bind"))
			return "driver-bind";
		if (!strcmp(ev->action, "unbind"))
			return "driver-unbind";
		return NULL;
	}
	if (!mm_uevent_relevant(ev))
		return NULL;
	return add ? "node-add" : remove ? "node-remove" : NULL;
}
