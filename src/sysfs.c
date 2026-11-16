#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <json-c/json.h>

#include "mm_paths.h"
#include "sysfs.h"

bool mm_read_line(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	if (!fgets(buf, (int)len, f)) {
		fclose(f);
		return false;
	}
	fclose(f);
	buf[strcspn(buf, "\r\n")] = '\0';
	return true;
}

bool mm_is_usb_device_dir(const char *name)
{
	if (!name || name[0] == '.' || strchr(name, ':'))
		return false;
	return strchr(name, '-') != NULL;
}

const char *mm_base_name(const char *path)
{
	const char *p = strrchr(path, '/');
	return p ? p + 1 : path;
}

/*
 * Stable port key: host controller path + port chain, without the bus number.
 * usbN is assigned in controller probe order, which is not stable on some
 * boards, and a USB3 device may land on either root hub of the same xHCI.
 */
bool mm_usb_port_key(const char *real, const char *name,
		     char *controller, size_t clen, char *chain, size_t chlen)
{
	char root[PATH_MAX], rel[PATH_MAX], *seg, *save = NULL;
	const char *dash = strchr(name, '-');
	size_t rootlen, used = 0;
	bool found = false;

	if (!dash || !dash[1] || !realpath(SYS_DEVICES, root))
		return false;
	rootlen = strlen(root);
	if (strncmp(real, root, rootlen) || real[rootlen] != '/')
		return false;
	snprintf(rel, sizeof(rel), "%s", real + rootlen + 1);

	controller[0] = '\0';
	for (seg = strtok_r(rel, "/", &save); seg; seg = strtok_r(NULL, "/", &save)) {
		char norm[NAME_MAX + 1];
		size_t n = strlen(seg);
		int len;

		if (!strncmp(seg, "usb", 3) && seg[3] && strspn(seg + 3, "0123456789") == n - 3) {
			found = true;
			break;
		}
		/* PLATFORM_DEVID_AUTO suffixes (xhci-hcd.0.auto) follow probe order too. */
		snprintf(norm, sizeof(norm), "%s", seg);
		if (n > 5 && !strcmp(norm + n - 5, ".auto")) {
			char *dot = norm + n - 5;
			char *p = dot;
			while (p > norm && p[-1] >= '0' && p[-1] <= '9')
				p--;
			if (p < dot && p > norm && p[-1] == '.')
				memmove(p - 1, dot, strlen(dot) + 1);
		}
		len = snprintf(controller + used, clen - used, "%s%s", used ? "/" : "", norm);
		if (len < 0 || (size_t)len >= clen - used)
			return false;
		used += (size_t)len;
	}
	if (!found || !used)
		return false;
	snprintf(chain, chlen, "%s", dash + 1);
	return true;
}

bool mm_owned_by_usb_device(const char *link_path, const char *device_real)
{
	char resolved[PATH_MAX];

	if (!realpath(link_path, resolved))
		return false;

	/* Match the nearest physical USB parent, never a containing hub. */
	for (;;) {
		char vendor[PATH_MAX];
		struct stat st;
		int len = snprintf(vendor, sizeof(vendor), "%s/idVendor", resolved);
		if (len > 0 && (size_t)len < sizeof(vendor) && !stat(vendor, &st))
			return !strcmp(resolved, device_real);
		char *slash = strrchr(resolved, '/');
		if (!slash || slash == resolved)
			break;
		*slash = '\0';
	}
	return false;
}

/* "1-1:1.2" from a path below a USB interface: the last component with a ':'. */
static bool interface_of(const char *link_path, char *out, size_t size)
{
	char resolved[PATH_MAX], *p, *best = NULL;
	size_t n;

	if (!realpath(link_path, resolved))
		return false;
	for (p = strtok(resolved, "/"); p; p = strtok(NULL, "/"))
		if (strchr(p, ':') && isdigit((unsigned char)p[0]))
			best = p;
	if (!best || (n = strlen(best)) >= size)
		return false;
	memcpy(out, best, n + 1);
	return true;
}

void mm_append_owned_nodes(json_object *arr, const char *class_root,
			       const char *device_real, const char *prefix,
			       json_object *iface_map)
{
	DIR *dir = opendir(class_root);
	struct dirent *de;

	if (!dir)
		return;

	while ((de = readdir(dir)) != NULL) {
		char devlink[PATH_MAX];
		char out[PATH_MAX];

		if (de->d_name[0] == '.')
			continue;

		snprintf(devlink, sizeof(devlink), "%s/%s/device",
			 class_root, de->d_name);
		if (!mm_owned_by_usb_device(devlink, device_real))
			continue;

		if (prefix)
			snprintf(out, sizeof(out), "%s%s", prefix, de->d_name);
		else
			snprintf(out, sizeof(out), "%s", de->d_name);
		json_object_array_add(arr, json_object_new_string(out));
		if (iface_map) {
			char iface[NAME_MAX + 1];
			if (interface_of(devlink, iface, sizeof(iface)))
				json_object_object_add(iface_map, out, json_object_new_string(iface));
		}
	}

	closedir(dir);
}

json_object *mm_scan_interfaces(const char *usb_name)
{
	DIR *dir = opendir(SYS_USB);
	struct dirent *de;
	json_object *arr = json_object_new_array();
	char prefix[NAME_MAX + 2];

	if (!dir)
		return arr;

	snprintf(prefix, sizeof(prefix), "%s:", usb_name);

	while ((de = readdir(dir)) != NULL) {
		char path[PATH_MAX], val[128], driver_link[PATH_MAX];
		char driver_real[PATH_MAX];
		json_object *o;

		if (strncmp(de->d_name, prefix, strlen(prefix)) != 0)
			continue;

		o = json_object_new_object();
		json_object_object_add(o, "sysfs",
				       json_object_new_string(de->d_name));

		snprintf(path, sizeof(path), "%s/%s/bInterfaceNumber",
			 SYS_USB, de->d_name);
		if (mm_read_line(path, val, sizeof(val)))
			json_object_object_add(o, "number",
					       json_object_new_string(val));

		snprintf(path, sizeof(path), "%s/%s/bInterfaceClass",
			 SYS_USB, de->d_name);
		if (mm_read_line(path, val, sizeof(val)))
			json_object_object_add(o, "class",
					       json_object_new_string(val));

		snprintf(path, sizeof(path), "%s/%s/bInterfaceSubClass",
			 SYS_USB, de->d_name);
		if (mm_read_line(path, val, sizeof(val)))
			json_object_object_add(o, "subclass",
					       json_object_new_string(val));

		snprintf(path, sizeof(path), "%s/%s/bInterfaceProtocol",
			 SYS_USB, de->d_name);
		if (mm_read_line(path, val, sizeof(val)))
			json_object_object_add(o, "protocol",
					       json_object_new_string(val));

		snprintf(driver_link, sizeof(driver_link), "%s/%s/driver",
			 SYS_USB, de->d_name);
		if (realpath(driver_link, driver_real))
			json_object_object_add(o, "driver",
				json_object_new_string(mm_base_name(driver_real)));

		json_object_array_add(arr, o);
	}

	closedir(dir);
	return arr;
}
