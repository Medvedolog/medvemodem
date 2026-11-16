#define _GNU_SOURCE
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <json-c/json.h>

#include "mm_paths.h"
#include "classify.h"
#include "discovery.h"
#include "identity.h"
#include "profile.h"
#include "sysfs.h"

static json_object *held_paths, *force_modem, *force_not, *suspects;

/*
 * A USB serial seen on two devices at once is no identity, then or later (an audit of r181, 2026-11-12: two sticks with
 * one serial both got their USB port's id, and when one left the other went back to «usb-serial:…»: its id, its
 * settings and its interface's owner jumped with every plug of its twin; such serials are kept for good).
 * Kept by VID:PID and serial, on flash, written only when a new one is seen.
 */
static json_object *serial_dups;
static char dups_path[512];

void mm_discovery_serial_dups_file(const char *path)
{
	snprintf(dups_path, sizeof(dups_path), "%s", path ? path : "");
	json_object_put(serial_dups);
	serial_dups = *dups_path ? json_object_from_file(dups_path) : NULL;
	if (!serial_dups || !json_object_is_type(serial_dups, json_type_object)) {
		json_object_put(serial_dups);
		serial_dups = json_object_new_object();
	}
}

static void serial_dups_apply(json_object *arr)
{
	bool grew = false;
	size_t i;

	if (!serial_dups)
		serial_dups = json_object_new_object();
	for (i = 0; i < json_object_array_length(arr); i++) {
		json_object *dev = json_object_array_get_idx(arr, i);
		const char *serial = json_object_get_string(json_object_object_get(dev, "serial"));
		const char *vp = json_object_get_string(json_object_object_get(dev, "vidpid"));
		const char *strength = json_object_get_string(json_object_object_get(dev, "identity_strength"));
		char key[200];

		if (!serial || !vp)
			continue;
		snprintf(key, sizeof(key), "%s|%s", vp, serial);
		if (strength && !strcmp(strength, "serial-collision")) {
			if (!json_object_object_get(serial_dups, key)) {
				json_object_object_add(serial_dups, key, json_object_new_boolean(1));
				grew = true;
			}
		} else if (strength && !strcmp(strength, "serial") && json_object_object_get(serial_dups, key)) {
			json_object_object_add(dev, "device_id", json_object_get(json_object_object_get(dev, "port_id")));
			json_object_object_add(dev, "identity_strength", json_object_new_string("serial-collision"));
			json_object_object_add(dev, "identity_ambiguous", json_object_new_boolean(true));
		}
	}
	if (grew && *dups_path) {
		char tmp[600];

		snprintf(tmp, sizeof(tmp), "%s.tmp", dups_path);
		if (!json_object_to_file_ext(tmp, serial_dups, JSON_C_TO_STRING_PLAIN))
			rename(tmp, dups_path);
	}
}

void mm_discovery_overrides(json_object *modem, json_object *not_modem)
{
	json_object_put(force_modem);
	json_object_put(force_not);
	force_modem = modem ? json_object_get(modem) : NULL;
	force_not = not_modem ? json_object_get(not_modem) : NULL;
}

json_object *mm_discovery_suspects(void)
{
	return suspects;
}

/* vendors that make cellular modems and little else on a router's port: a dropped device of theirs is asked about, never
 * taken (the owner, 2026-11-09). Not Qualcomm's 05c6 or u-blox's 1546: phones in service modes, GPS receivers */
static bool cellular_vendor(const char *vid)
{
	/* makers of cellular modules and sticks only (regression review 2026-11-11: MediaTek 0e8d is Wi-Fi dongles too,
	 * Dell 413c keyboards and hubs, Foxconn 0489 Bluetooth, Unisoc 1782 phones) */
	static const char *const v[] = { "12d1", "19d2", "2c7c", "1e0e", "1bc7", "2cb7", "1199", "2dee", "1e2d", NULL };
	int i;

	for (i = 0; v[i]; i++)
		if (!strcasecmp(vid, v[i]))
			return true;
	return false;
}

/* a model profile of ours names this vid:pid (its pid in the match, not a family's vendor only): a modem as a catalog entry
 * is, with or without its driver bound. A tester's ZTE MF100 (19d2:0031, 2026-11-12) before kmod-usb-serial-option was
 * dropped as «not a modem» and came back only by hand, under the generic profile */
static bool model_profile_names(const char *vid, const char *pid, json_object *interfaces)
{
	struct mm_profile_match m;
	bool named = false;
	size_t i;

	if (!cellular_vendor(vid) || mm_profile_resolve_usb(PROFILE_GENERIC_DIR, vid, pid, NULL, NULL, interfaces, &m) < 0)
		return false;
	for (i = 0; !m.ambiguous && m.reasons && i < json_object_array_length(m.reasons); i++)
		if (!strcmp(json_object_get_string(json_object_array_get_idx(m.reasons, i)) ?: "", "pid"))
			named = !strcmp(mm_profile_fit(&m), "exact");
	mm_profile_match_clear(&m);
	return named;
}

void mm_discovery_held(json_object *paths)
{
	json_object_put(held_paths);
	held_paths = paths ? json_object_get(paths) : NULL;
}

/* usb-mode.json "devices" entry for vid:pid (lowercase key), or NULL. */
static json_object *modeswitch_entry(json_object *devices, const char *vid, const char *pid)
{
	json_object *o = NULL;
	char key[40]; /* vid[16] + ":" + pid[16] */

	if (!devices)
		return NULL;
	snprintf(key, sizeof(key), "%s:%s", vid, pid);
	for (char *c = key; *c; c++)
		if (*c >= 'A' && *c <= 'F')
			*c = (char)(*c - 'A' + 'a');
	return json_object_object_get_ex(devices, key, &o) ? o : NULL;
}

/*
 * What switches a stick out of its storage mode here (field 2026-10-15, E171): the rule in
 * usb-mode.json and its target ID, and the tool. OpenWrt's usb-modeswitch package is usbmode
 * (hotplug runs it with the rules); some builds carry the classic usb_modeswitch instead.
 */
static json_object *modeswitch_view(json_object *entry)
{
	json_object *o = json_object_new_object(), *v = NULL, *tv = NULL, *tp = NULL;
	char target[16];

	json_object_object_add(o, "rule", json_object_new_boolean(entry != NULL));
	/* the first variant (usually "*") names the mode and the target */
	if (entry && json_object_is_type(entry, json_type_object)) {
		json_object_object_foreach(entry, k, var) {
			(void)k;
			v = var;
			break;
		}
	}
	if (v && json_object_object_get_ex(v, "mode", &tv) && json_object_is_type(tv, json_type_string))
		json_object_object_add(o, "mode", json_object_get(tv));
	if (v && json_object_object_get_ex(v, "t_vendor", &tv) && json_object_object_get_ex(v, "t_product", &tp)) {
		json_object *p0 = json_object_is_type(tp, json_type_array) ? json_object_array_get_idx(tp, 0) : tp;
		if (p0 && json_object_get_int(tv) > 0 && json_object_get_int(p0) > 0) {
			snprintf(target, sizeof(target), "%04x:%04x", json_object_get_int(tv) & 0xffff, json_object_get_int(p0) & 0xffff);
			json_object_object_add(o, "target", json_object_new_string(target));
		}
	}
	json_object_object_add(o, "tool", access(USBMODE_BIN, X_OK) == 0 ? json_object_new_string("usbmode") :
			       access(USB_MODESWITCH_BIN, X_OK) == 0 ? json_object_new_string("usb_modeswitch") : NULL);
	return o;
}

static json_object *scan_usb_device(const char *name, json_object *msw_devices)
{
	char root[512], real[PATH_MAX], path[1024], val[256];
	char vid[16] = "", pid[16] = "", serial[128] = "";
	char manufacturer[256] = "", product[256] = "", config[16] = "", vp[40] = "";
	char controller[PATH_MAX], chain[NAME_MAX + 1];
	char port_id[PATH_MAX + NAME_MAX + 16];
	json_object *o, *ports, *interfaces;
	bool have_port;
	
	snprintf(root, sizeof(root), "%s/%s", SYS_USB, name);
	if (!realpath(root, real))
		return NULL;

	snprintf(path, sizeof(path), "%s/idVendor", root);
	if (!mm_read_line(path, vid, sizeof(vid)))
		return NULL;
	snprintf(path, sizeof(path), "%s/idProduct", root);
	if (!mm_read_line(path, pid, sizeof(pid)))
		return NULL;

	o = json_object_new_object();
	json_object_object_add(o, "schema", json_object_new_int(1));
	snprintf(val, sizeof(val), "usb:%s", name);
	json_object_object_add(o, "attachment_id", json_object_new_string(val));
	json_object_object_add(o, "bus", json_object_new_string("usb"));

	have_port = mm_usb_port_key(real, name, controller, sizeof(controller),
				 chain, sizeof(chain));
	if (have_port) {
		json_object *port = json_object_new_object();
		json_object_object_add(port, "controller", json_object_new_string(controller));
		json_object_object_add(port, "chain", json_object_new_string(chain));
		json_object_object_add(o, "usb_port", port);
		snprintf(port_id, sizeof(port_id), "usb-port:%s@%s", controller, chain);
	} else {
		snprintf(port_id, sizeof(port_id), "usb-path:%s", name);
	}
	/* Also the duplicate-serial fallback in scan_all(). */
	json_object_object_add(o, "port_id", json_object_new_string(port_id));

	snprintf(path, sizeof(path), "%s/serial", root);
	/* an Apple phone or tablet (05ac) by its USB port, never its serial: that is its UDID, a person's identifier, and
	 * the owner's field report of 01.11 saw an iPhone come back under another id, its interface released as an absent
	 * modem's and the phone left with none («Модем не подключён: интерфейса ещё нет») */
	if (strcasecmp(vid, "05ac") && mm_read_line(path, serial, sizeof(serial)) && mm_valid_serial(serial)) {
		snprintf(val, sizeof(val), "usb-serial:%s", serial);
		json_object_object_add(o, "device_id", json_object_new_string(val));
		json_object_object_add(o, "serial", json_object_new_string(serial));
		json_object_object_add(o, "identity_strength",
				       json_object_new_string("serial"));
	} else {
		json_object_object_add(o, "device_id", json_object_new_string(port_id));
		json_object_object_add(o, "identity_strength",
				       json_object_new_string(have_port ? "port" : "bus-path"));
	}

	snprintf(val, sizeof(val), "%s:%s", vid, pid);
	snprintf(vp, sizeof(vp), "%.4s:%.4s", vid, pid);	/* val is read into again below */
	json_object_object_add(o, "vidpid", json_object_new_string(val));

	snprintf(path, sizeof(path), "%s/manufacturer", root);
	if (mm_read_line(path, manufacturer, sizeof(manufacturer)))
		json_object_object_add(o, "manufacturer",
				       json_object_new_string(manufacturer));

	snprintf(path, sizeof(path), "%s/product", root);
	if (mm_read_line(path, product, sizeof(product)))
		json_object_object_add(o, "product",
				       json_object_new_string(product));

	interfaces = mm_scan_interfaces(name);
	json_object_object_add(o, "interfaces", interfaces);

	ports = json_object_new_object();
	json_object *tty = json_object_new_array();
	json_object *wdm = json_object_new_array();
	json_object *net = json_object_new_array();
	json_object *port_ifaces = json_object_new_object();
	mm_append_owned_nodes(tty, SYS_TTY, real, "/dev/", port_ifaces);
	mm_append_owned_nodes(wdm, SYS_USBMISC, real, "/dev/", port_ifaces);
	mm_append_owned_nodes(net, SYS_NET, real, NULL, port_ifaces);
	json_object_object_add(ports, "tty", tty);
	json_object_object_add(ports, "control", wdm);
	json_object_object_add(ports, "net", net);
	json_object_object_add(o, "ports", ports);
	/* node -> USB interface, so roles can use the interface's class and driver */
	json_object_object_add(o, "port_interfaces", port_ifaces);

	snprintf(path, sizeof(path), "%s/bConfigurationValue", root);
	if (!mm_read_line(path, config, sizeof(config)))
		config[0] = '\0';
	json_object_object_add(o, "usb_configuration", config[0] ? json_object_new_string(config) : NULL);
	snprintf(path, sizeof(path), "%s/bNumConfigurations", root);
	if (mm_read_line(path, val, sizeof(val)) && atoi(val) > 0)
		json_object_object_add(o, "usb_configurations", json_object_new_int(atoi(val)));

	{
		struct mm_profile_match match;
		bool catalog_known = mm_catalog_known_usb(vid, pid) || model_profile_names(vid, pid, interfaces);
		json_object *configuration = NULL;
		struct mm_facts facts = {
			.vid = vid, .pid = pid, .interfaces = interfaces,
			.tty = tty, .wdm = wdm, .net = net,
			.catalog_known = catalog_known, .config_value = config,
			.modeswitch = modeswitch_entry(msw_devices, vid, pid),
			.mm_held = (held_paths && strrchr(real, '/') && json_object_object_get(held_paths, strrchr(real, '/') + 1)) ||
				   (force_modem && json_object_object_get(force_modem, vp)),
		};
		enum mm_detect detect = force_not && json_object_object_get(force_not, vp) ? MM_DETECT_DROP :
					mm_classify(&facts, &configuration);

		if (detect == MM_DETECT_DROP) {
			/* looks like one all the same: ports of its own, or a modem maker's ID; asked about on the dashboard */
			/* a CDC ACM port (one: two make a modem already), not a USB-UART adapter's, an Ethernet adapter's or a
			 * Wi-Fi dongle's; a modem maker's device with a vendor or CDC function, not its hub, keyboard or disk */
			bool ports = mm_interfaces_have_driver(interfaces, "cdc_acm") && mm_array_nonempty(tty), vendorish = false;
			size_t q;

			for (q = 0; cellular_vendor(vid) && q < json_object_array_length(interfaces); q++) {
				const char *c = json_object_get_string(json_object_object_get(json_object_array_get_idx(interfaces, q), "class"));

				if (c && (!strcasecmp(c, "ff") || !strcasecmp(c, "02") || !strcasecmp(c, "0a")))
					vendorish = true;
			}
			if (!(force_not && json_object_object_get(force_not, vp)) && (ports || vendorish) && suspects) {
				json_object *s = json_object_new_object();

				json_object_object_add(s, "usb", json_object_new_string(name));
				json_object_object_add(s, "vidpid", json_object_new_string(vp));
				if (manufacturer[0])
					json_object_object_add(s, "manufacturer", json_object_new_string(manufacturer));
				if (product[0])
					json_object_object_add(s, "product", json_object_new_string(product));
				json_object_object_add(s, "why", json_object_new_string(ports ? "ports" : "vendor"));
				json_object_array_add(suspects, s);
			}
			if (configuration)
				json_object_put(configuration);
			json_object_put(o);
			return NULL;
		}
		if (force_modem && json_object_object_get(force_modem, vp))
			json_object_object_add(o, "class_forced", json_object_new_boolean(true));
		json_object_object_add(o, "catalog_known",
				       json_object_new_boolean(catalog_known));
		if (facts.mm_held)
			json_object_object_add(o, "modemmanager_held", json_object_new_boolean(true));
		json_object_object_add(o, "detection_status",
				       json_object_new_string(mm_detect_status(detect)));
		if (configuration)
			json_object_object_add(o, "configuration", configuration);
		if (detect == MM_DETECT_MODESWITCH_PENDING)
			json_object_object_add(o, "modeswitch", modeswitch_view(facts.modeswitch));
		/* Nothing to resolve for service modes or a device without interfaces. */
		if (detect != MM_DETECT_CANDIDATE || !mm_array_nonempty(interfaces))
			goto unresolved;

		if (mm_profile_resolve_usb(PROFILE_GENERIC_DIR, vid, pid,
					   manufacturer[0] ? manufacturer : NULL,
					   product[0] ? product : NULL,
					   interfaces, &match) >= 0) {
			json_object *decision = json_object_new_object();
			json_object_object_add(decision, "id",
				match.ambiguous ? NULL : json_object_new_string(match.id));
			json_object_object_add(decision, "status", json_object_new_string(
				match.ambiguous ? "ambiguous" : "matched"));
			json_object_object_add(decision, "candidates", json_object_get(match.candidates));
			json_object_object_add(decision, "score",
				json_object_new_int(match.score));
			json_object_object_add(decision, "fit", json_object_new_string(mm_profile_fit(&match)));
			json_object_object_add(decision, "reasons",
				match.ambiguous ? NULL : json_object_get(match.reasons));
			{
				/* the model's AT dial recipe (spec 19.4) and recovery capabilities (22.1.3) */
				json_object *dial = NULL;
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "dial", &dial))
					json_object_object_add(decision, "dial", json_object_get(dial));
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "recovery", &dial))
					json_object_object_add(decision, "recovery", json_object_get(dial));
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "bind", &dial))
					json_object_object_add(decision, "bind", json_object_get(dial));
				/* the owner the profile prefers (option backend 'auto' honours a first 'qmodem') */
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "policy", &dial) &&
				    json_object_is_type(json_object_object_get(dial, "owner_priority"), json_type_array) &&
				    json_object_array_length(json_object_object_get(dial, "owner_priority")))
					json_object_object_add(decision, "prefer_owner", json_object_get(
						json_object_array_get_idx(json_object_object_get(dial, "owner_priority"), 0)));
				/* the owner after it, when it is the engine's own path: option backend 'auto' takes it where the first
				 * is not installed (FM350 without QModem: ModemManager has no plugin that dials it) */
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "policy", &dial) &&
				    json_object_is_type(json_object_object_get(dial, "owner_priority"), json_type_array) &&
				    json_object_array_length(json_object_object_get(dial, "owner_priority")) > 1 &&
				    !strcmp(json_object_get_string(json_object_array_get_idx(
					    json_object_object_get(dial, "owner_priority"), 1)), "native"))
					json_object_object_add(decision, "fallback_owner", json_object_new_string("native"));
				/* ModemManager's ports by the USB ID alone, before it has built the modem (T77 first boot) */
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "policy", &dial) &&
				    json_object_is_type(json_object_object_get(dial, "mm_ports"), json_type_string))
					json_object_object_add(decision, "mm_ports",
						json_object_get(json_object_object_get(dial, "mm_ports")));
				/* the FCC unlock the operator may turn on (HANDOFF 176) */
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "policy", &dial) &&
				    json_object_is_type(json_object_object_get(dial, "fcc_unlock"), json_type_string))
					json_object_object_add(decision, "fcc_unlock",
						json_object_get(json_object_object_get(dial, "fcc_unlock")));
				/* how the radio settings are read over AT (spec 25.9.12) */
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "radio", &dial))
					json_object_object_add(decision, "radio", json_object_get(dial));
				/* the interfaces a validated profile names as AT: no probe needed (spec 11.5) */
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "hints", &dial) &&
				    json_object_is_type(json_object_object_get(dial, "usb_configurations"), json_type_object))
					json_object_object_add(decision, "usb_configurations",
						json_object_get(json_object_object_get(dial, "usb_configurations")));
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "hints", &dial)) {
					json_object *v;

					if (json_object_object_get_ex(dial, "title", &v) && json_object_is_type(v, json_type_string))
						json_object_object_add(decision, "title", json_object_get(v));
					if (json_object_object_get_ex(dial, "router_mode", &v) && json_object_is_type(v, json_type_string)) {
						json_object *conf, *st;

						json_object_object_add(decision, "router_mode", json_object_get(v));
						/* a device whose profile knows no mode but its router (a phone, a HiLink or UFI stick) is
						 * as it should be, not «suboptimal» (the owner, 2026-11-02, a vivo phone's report) */
						if (!strcmp(json_object_get_string(v), "only") && json_object_object_get_ex(o, "configuration", &conf) &&
						    json_object_object_get_ex(conf, "state", &st) && !strcmp(json_object_get_string(st), "suboptimal")) {
							json_object *ev = json_object_new_array();

							json_object_array_add(ev, json_object_new_string("router mode is this device's only mode (its profile)"));
							json_object_array_add(ev, json_object_new_string("the device routes and NATs itself; signal, SMS and recovery stay inside it"));
							json_object_object_add(conf, "state", json_object_new_string("expected"));
							json_object_object_add(conf, "evidence", ev);
						}
					}
					if (json_object_object_get_ex(dial, "ussd", &v) && json_object_is_type(v, json_type_string))
						json_object_object_add(decision, "ussd", json_object_get(v));
					/* the session protocols: «ppp» marks a serial-only modem, whose vendor interfaces are no QMI */
					if (json_object_object_get_ex(dial, "protocols", &v) && json_object_is_type(v, json_type_array))
						json_object_object_add(decision, "protocols", json_object_get(v));
				}
				if (!match.ambiguous && json_object_object_get_ex(match.profile, "hints", &dial) &&
				    json_object_is_type(json_object_object_get(dial, "at_interface_numbers"), json_type_array))
					json_object_object_add(decision, "at_interfaces",
						json_object_get(json_object_object_get(dial, "at_interface_numbers")));
			}
			json_object_object_add(o, "profile", decision);
			mm_profile_match_clear(&match);
		}
	}

unresolved:;
	json_object *owner = json_object_new_object();
	json_object_object_add(owner, "control",
			       json_object_new_string("unresolved"));
	json_object_object_add(owner, "session",
			       json_object_new_string("unresolved"));
	json_object_object_add(owner, "at",
			       json_object_new_string("unresolved"));
	json_object_object_add(o, "owner", owner);

	return o;
}

json_object *mm_discover_all(void)
{
	DIR *dir = opendir(SYS_USB);
	struct dirent *de;
	json_object *arr = json_object_new_array();
	json_object *msw = json_object_from_file(USB_MODE_JSON), *msw_devices = NULL;

	json_object_put(suspects);
	suspects = json_object_new_array();

	if (msw && !(json_object_object_get_ex(msw, "devices", &msw_devices) &&
		     json_object_is_type(msw_devices, json_type_object)))
		msw_devices = NULL;
	if (dir) {
		while ((de = readdir(dir)) != NULL) {
			json_object *dev;
			if (!mm_is_usb_device_dir(de->d_name))
				continue;
			dev = scan_usb_device(de->d_name, msw_devices);
			if (dev)
				json_object_array_add(arr, dev);
		}
		closedir(dir);
	}
	if (msw)
		json_object_put(msw);
	mm_resolve_serial_collisions(arr);
	serial_dups_apply(arr);
	return arr;
}
