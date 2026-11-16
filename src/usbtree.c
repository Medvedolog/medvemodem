// SPDX-License-Identifier: GPL-3.0-only
/*
 * «Порты и устройства»: the USB tree from sysfs (docs/PORTS-AND-DEVICES.md). Reads only: no attribute is
 * written, no driver bound or unbound. The kernel log lines that tell a bounce are matched here too, the text
 * never kept.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mm_paths.h"
#include "sysfs.h"
#include "usbtree.h"

static bool attr(const char *dir, const char *name, char *buf, size_t size)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return mm_read_line(path, buf, size) && buf[0];
}

static void add_str(json_object *o, const char *key, const char *dir, const char *name)
{
	char v[256];

	if (attr(dir, name, v, sizeof(v)))
		json_object_object_add(o, key, json_object_new_string(v));
}

static void add_int(json_object *o, const char *key, const char *dir, const char *name)
{
	char v[64];

	if (attr(dir, name, v, sizeof(v)) && isdigit((unsigned char)v[0]))
		json_object_object_add(o, key, json_object_new_int(atoi(v)));
}

/* "usb1" -> 1, else 0 */
static int root_bus(const char *name)
{
	return !strncmp(name, "usb", 3) && name[3] && strspn(name + 3, "0123456789") == strlen(name + 3) ? atoi(name + 3) : 0;
}

bool mm_usbtree_port_child(const char *port, char *out, size_t size)
{
	const char *p = strstr(port, "-port");
	int bus, n;

	if (!p || !isdigit((unsigned char)p[5]) || strspn(p + 5, "0123456789") != strlen(p + 5))
		return false;
	n = atoi(p + 5);
	if (!strncmp(port, "usb", 3) && (bus = atoi(port + 3)) > 0 && p == port + 3 + strspn(port + 3, "0123456789"))
		return snprintf(out, size, "%d-%d", bus, n) < (int)size;
	if (!isdigit((unsigned char)port[0]) || !strchr(port, '-') || strchr(port, '-') >= p)
		return false;
	return snprintf(out, size, "%.*s.%d", (int)(p - port), port, n) < (int)size;
}

/* the sysfs name of a device's hub ports' parent interface: "usb1" -> "1-0:1.0", "1-1" -> "1-1:1.0" */
static void hub_iface(const char *name, char *out, size_t size)
{
	int bus = root_bus(name);

	if (bus)
		snprintf(out, size, "%d-0:1.0", bus);
	else
		snprintf(out, size, "%s:1.0", name);
}

static json_object *hub_ports(const char *name, int count)
{
	json_object *ports = json_object_new_array();
	char iface[NAME_MAX + 1];
	int bus = root_bus(name), n;

	hub_iface(name, iface, sizeof(iface));
	for (n = 1; n <= count && n <= 32; n++) {
		json_object *p = json_object_new_object();
		char dir[PATH_MAX], child[NAME_MAX + 1], link[PATH_MAX + 16], real[PATH_MAX], peer[NAME_MAX + 1];
		struct stat st;

		if (bus)
			snprintf(dir, sizeof(dir), "%s/%s/usb%d-port%d", SYS_USB, iface, bus, n);
		else
			snprintf(dir, sizeof(dir), "%s/%s/%s-port%d", SYS_USB, iface, name, n);
		if (bus)
			snprintf(child, sizeof(child), "%d-%d", bus, n);
		else
			snprintf(child, sizeof(child), "%s.%d", name, n);
		json_object_object_add(p, "n", json_object_new_int(n));
		snprintf(link, sizeof(link), "%s/%s", SYS_USB, child);
		json_object_object_add(p, "child", !lstat(link, &st) ? json_object_new_string(child) : NULL);
		add_int(p, "over_current", dir, "over_current_count");
		/* the USB 2 / USB 3 half of the same connector, where the kernel links them */
		snprintf(link, sizeof(link), "%s/peer", dir);
		if (realpath(link, real) && mm_usbtree_port_child(mm_base_name(real), peer, sizeof(peer)))
			json_object_object_add(p, "peer", json_object_new_string(peer));
		json_object_array_add(ports, p);
	}
	return ports;
}

static bool any_iface_class(json_object *ifaces, const char *klass)
{
	size_t i;

	for (i = 0; i < json_object_array_length(ifaces); i++) {
		json_object *v = json_object_object_get(json_object_array_get_idx(ifaces, i), "class");

		if (v && !strcasecmp(json_object_get_string(v), klass))
			return true;
	}
	return false;
}

/* a grey device's word: hub, storage, wifi, net, input or other (the registry decides what is a modem) */
static const char *class_kind(const char *dir, json_object *ifaces, json_object *nets)
{
	char v[16];
	size_t i;

	if ((attr(dir, "bDeviceClass", v, sizeof(v)) && !strcmp(v, "09")) || any_iface_class(ifaces, "09"))
		return "hub";
	/* a storage function beside others is a modem's drivers disk, not a flash drive (a tester's ZTE MF192, 2026-11-09:
	 * eight serial ports and a disk read «Накопитель») */
	if (any_iface_class(ifaces, "08")) {
		for (i = 0; i < json_object_array_length(ifaces); i++) {
			json_object *k = json_object_object_get(json_object_array_get_idx(ifaces, i), "class");

			if (k && strcasecmp(json_object_get_string(k), "08"))
				return "other";
		}
		return "storage";
	}
	for (i = 0; i < json_object_array_length(nets); i++) {
		char p[PATH_MAX];
		struct stat st;

		snprintf(p, sizeof(p), "%s/%s/phy80211", SYS_NET, json_object_get_string(json_object_array_get_idx(nets, i)));
		if (!stat(p, &st) || !strncmp(json_object_get_string(json_object_array_get_idx(nets, i)), "wlan", 4))
			return "wifi";
	}
	if (json_object_array_length(nets))
		return "net";
	if (any_iface_class(ifaces, "03"))
		return "input";
	return "other";
}

static json_object *scan_one(const char *name)
{
	json_object *o = json_object_new_object(), *ifaces, *list, *map, *nets;
	char dir[PATH_MAX], real[PATH_MAX], v[256], parent[NAME_MAX + 1], prefix[NAME_MAX + 1];
	int bus = root_bus(name), children = 0;
	size_t i;

	snprintf(dir, sizeof(dir), "%s/%s", SYS_USB, name);
	if (!realpath(dir, real)) {
		json_object_put(o);
		return NULL;
	}
	json_object_object_add(o, "path", json_object_new_string(name));
	if (bus) {
		json_object_object_add(o, "parent", NULL);
	} else {
		const char *dot = strrchr(name, '.');

		if (dot)
			snprintf(parent, sizeof(parent), "%.*s", (int)(dot - name), name);
		else
			snprintf(parent, sizeof(parent), "usb%d", atoi(name));
		json_object_object_add(o, "parent", json_object_new_string(parent));
	}
	add_int(o, "bus", dir, "busnum");
	if (!json_object_object_get(o, "bus"))
		json_object_object_add(o, "bus", json_object_new_int(bus ? bus : atoi(name)));
	add_int(o, "devnum", dir, "devnum");
	if (attr(dir, "speed", v, sizeof(v)) && (isdigit((unsigned char)v[0])))
		json_object_object_add(o, "speed_mbps", json_object_new_double(strtod(v, NULL)));
	add_str(o, "version", dir, "version");
	if (attr(dir, "idVendor", v, sizeof(v))) {
		char pid[16] = "", vp[40];

		attr(dir, "idProduct", pid, sizeof(pid));
		snprintf(vp, sizeof(vp), "%.15s:%.15s", v, pid);
		json_object_object_add(o, "vidpid", json_object_new_string(vp));
	}
	add_str(o, "manufacturer", dir, "manufacturer");
	add_str(o, "product", dir, "product");
	json_object_object_add(o, "has_serial", json_object_new_boolean(attr(dir, "serial", v, sizeof(v))));
	/* what the configuration asks for, «запрошено до», never what the port gives */
	if (attr(dir, "bMaxPower", v, sizeof(v)) && isdigit((unsigned char)v[0]))
		json_object_object_add(o, "max_power_ma", json_object_new_int(atoi(v)));
	add_int(o, "config", dir, "bConfigurationValue");
	add_str(o, "runtime", dir, "power/runtime_status");
	if (attr(dir, "maxchild", v, sizeof(v)))
		children = atoi(v);

	if (bus)
		snprintf(prefix, sizeof(prefix), "%d-0", bus);
	else
		snprintf(prefix, sizeof(prefix), "%s", name);
	ifaces = mm_scan_interfaces(prefix);
	map = json_object_new_object();
	nets = json_object_new_array();
	list = json_object_new_array();
	mm_append_owned_nodes(nets, SYS_NET, real, NULL, map);
	mm_append_owned_nodes(list, SYS_TTY, real, NULL, map);
	mm_append_owned_nodes(list, SYS_USBMISC, real, NULL, map);
	json_object_put(list);
	{
		json_object *out = json_object_new_array();

		for (i = 0; i < json_object_array_length(ifaces); i++) {
			json_object *in = json_object_array_get_idx(ifaces, i), *x = json_object_new_object(), *nodes = json_object_new_array();
			const char *sys = json_object_get_string(json_object_object_get(in, "sysfs"));
			const char *num = json_object_get_string(json_object_object_get(in, "number"));

			json_object_object_add(x, "n", json_object_new_int(num ? (int)strtol(num, NULL, 16) : (int)i));
			json_object_object_foreach(in, k, val) {
				if (!strcmp(k, "class") || !strcmp(k, "subclass") || !strcmp(k, "protocol") || !strcmp(k, "driver"))
					json_object_object_add(x, k, json_object_get(val));
			}
			json_object_object_foreach(map, node, owner) {
				if (sys && !strcmp(json_object_get_string(owner), sys))
					json_object_array_add(nodes, json_object_new_string(node));
			}
			json_object_object_add(x, "nodes", nodes);
			json_object_array_add(out, x);
		}
		json_object_object_add(o, "class_kind", json_object_new_string(class_kind(dir, ifaces, nets)));
		json_object_object_add(o, "interfaces", out);
	}
	json_object_put(nets);
	json_object_put(map);
	if (bus) {
		char ctl[PATH_MAX + 16], drv[PATH_MAX];
		json_object *c = json_object_new_object();

		json_object_object_add(o, "kind", json_object_new_string("controller"));
		snprintf(ctl, sizeof(ctl), "%s/../driver", real);
		if (realpath(ctl, drv))
			json_object_object_add(c, "driver", json_object_new_string(mm_base_name(drv)));
		snprintf(ctl, sizeof(ctl), "%s/..", real);
		if (realpath(ctl, drv))
			json_object_object_add(c, "name", json_object_new_string(mm_base_name(drv)));
		json_object_object_add(o, "controller", c);
	} else {
		const char *ck = json_object_get_string(json_object_object_get(o, "class_kind"));

		json_object_object_add(o, "kind", json_object_new_string(children > 0 || !strcmp(ck, "hub") ? "hub" : "device"));
	}
	json_object_put(ifaces);
	if (children > 0)
		json_object_object_add(o, "ports", hub_ports(name, children));
	return o;
}

json_object *mm_usbtree_scan(void)
{
	json_object *nodes = json_object_new_object();
	DIR *d = opendir(SYS_USB);
	struct dirent *de;

	if (!d)
		return nodes;
	while ((de = readdir(d))) {
		json_object *o;

		if (de->d_name[0] == '.' || strchr(de->d_name, ':') || (!root_bus(de->d_name) && !mm_is_usb_device_dir(de->d_name)))
			continue;
		if ((o = scan_one(de->d_name)))
			json_object_object_add(nodes, de->d_name, o);
	}
	closedir(d);
	return nodes;
}

const char *mm_usbtree_protocol(const char *driver)
{
	static const char *const map[][2] = {
		{ "qmi_wwan", "QMI" }, { "qmi_wwan_q", "QMI" }, { "qmi_wwan_f", "QMI" }, { "qmi_wwan_s", "QMI" }, { "cdc_mbim", "MBIM" }, { "cdc_ncm", "NCM" },
		{ "huawei_cdc_ncm", "NCM" }, { "rndis_host", "RNDIS" }, { "cdc_ether", "ECM" }, { "ipheth", "iPhone" },
		{ "option", "serial" }, { "qcserial", "serial" }, { "cdc_acm", "ACM" }, { "usb_wwan", "serial" },
		{ "mhi_net", "MHI" }, { "cdc_wdm", "WDM" },
	};
	size_t i;

	for (i = 0; driver && i < sizeof(map) / sizeof(map[0]); i++)
		if (!strcmp(driver, map[i][0]))
			return map[i][1];
	return NULL;
}

/* "usb 1-1.2: …" -> "1-1.2" in who; the text after the colon, or NULL */
static const char *usb_subject(const char *line, char *who, size_t size)
{
	const char *p = line, *colon;
	size_t n;

	while (*p == ' ')
		p++;
	if (strncmp(p, "usb ", 4))
		return NULL;
	p += 4;
	colon = strstr(p, ": ");
	if (!colon || (n = (size_t)(colon - p)) == 0 || n >= size || memchr(p, ' ', n))
		return NULL;
	memcpy(who, p, n);
	who[n] = '\0';
	return colon + 2;
}

const char *mm_usbtree_kmsg(const char *line, char *who, size_t size)
{
	char subject[NAME_MAX + 1];
	const char *rest;

	if (!line)
		return NULL;
	if ((rest = strstr(line, "NETDEV WATCHDOG: "))) {
		const char *s = rest + 17, *e = strpbrk(s, " (:");
		size_t n = e ? (size_t)(e - s) : strlen(s);

		if (!n || n >= size)
			return NULL;
		memcpy(who, s, n);
		who[n] = '\0';
		return "watchdog";
	}
	if (!strncmp(line, "xhci", 4) || !strncmp(line, "ehci", 4) || !strncmp(line, "ohci", 4)) {
		const char *sp = strchr(line, ' '), *colon = sp ? strstr(sp + 1, ": ") : NULL;

		if (!colon || !(strstr(colon, "WARN") || strstr(colon, "HC died") || strstr(colon, "Set TR Deq Ptr")))
			return NULL;
		if ((size_t)(colon - sp - 1) >= size)
			return NULL;
		snprintf(who, size, "%.*s", (int)(colon - sp - 1), sp + 1);
		return "hc";
	}
	if (!(rest = usb_subject(line, subject, sizeof(subject))))
		return NULL;
	if (strstr(subject, "-port")) {
		if (!mm_usbtree_port_child(subject, who, size))
			return NULL;
		if (strstr(rest, "over-current"))
			return "overcurrent";
		if (strstr(rest, "unable to enumerate"))
			return "enum";
		return NULL;
	}
	if (snprintf(who, size, "%s", subject) >= (int)size)
		return NULL;
	if (!strncmp(rest, "reset ", 6))
		return "reset";
	if (strstr(rest, "error -71"))
		return "err71";
	if (strstr(rest, "error -110"))
		return "err110";
	if (strstr(rest, "error -32"))
		return "err32";
	if (strstr(rest, "unable to enumerate"))
		return "enum";
	return NULL;
}
