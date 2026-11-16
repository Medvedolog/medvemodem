#define _GNU_SOURCE
#include <string.h>

#include "hostfix.h"
#include "classify.h"

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

static bool listed(json_object *list, const char *number)
{
	size_t i;

	for (i = 0; number && list && i < json_object_array_length(list); i++)
		if (!strcasecmp(json_object_get_string(json_object_array_get_idx(list, i)), number))
			return true;
	return false;
}

/* the owner, 2026-10-26: «максимально универсально» — a binding the field reports (the
 * reference stacks run the same ones by themselves) runs automatically too, not only a bench-validated one */
bool mm_hostfix_automatic(json_object *bind)
{
	const char *s = str(bind, "status");

	return s && (!strcmp(s, "bench-validated") || !strcmp(s, "field-reported"));
}

static bool is_serial_driver(const char *d)
{
	return d && (!strcmp(d, "option") || !strcmp(d, "usbserial_generic") || !strcmp(d, "usb_serial_generic") ||
		     !strcmp(d, "qcserial"));
}

static bool is_vendor(json_object *f)
{
	return str(f, "class") && !strcasecmp(str(f, "class"), "ff");
}

/* ADB (ff/42/01): no driver of ours, never a serial port */
static bool is_adb(json_object *f)
{
	return is_vendor(f) && str(f, "subclass") && !strcasecmp(str(f, "subclass"), "42");
}

/*
 * A serial interface the profile asks for: listed by number, or "*": every vendor interface but ADB and the data
 * ones of the net block («new_id vid pid ff» for IDs whose port numbers vary by firmware).
 */
static bool serial_wants(json_object *bind, json_object *f)
{
	json_object *want = get(get(bind, "serial"), "interfaces");
	const char *num = str(f, "number");

	if (listed(want, num))
		return true;
	return listed(want, "*") && is_vendor(f) && !is_adb(f) && !listed(get(get(bind, "net"), "interfaces"), num);
}

json_object *mm_hostfix_plan(json_object *rec, json_object *bind)
{
	json_object *serial = get(bind, "serial"), *want = get(serial, "interfaces"), *ifs = get(rec, "interfaces");
	json_object *o, *missing, *collateral;
	const char *driver = str(serial, "driver"), *why = NULL;
	size_t i;

	if (!serial || !driver || !want)
		return NULL;
	o = json_object_new_object();
	missing = json_object_new_array();
	collateral = json_object_new_array();
	for (i = 0; ifs && i < json_object_array_length(ifs); i++) {
		json_object *f = json_object_array_get_idx(ifs, i);
		const char *drv = str(f, "driver"), *sys = str(f, "sysfs"), *cls = str(f, "class");

		if (!sys)
			continue;
		if (serial_wants(bind, f)) {
			if (!drv)
				json_object_array_add(missing, json_object_new_string(sys));
			else if (strcmp(drv, driver) && !why)
				why = "an interface the profile gives to the serial driver has another driver";
		} else if (!drv && cls && !strcasecmp(cls, "ff")) {
			/* an unbound vendor interface the new_id would take too */
			json_object_array_add(collateral, json_object_new_string(sys));
		}
	}
	json_object_object_add(o, "action", json_object_new_string(why ? "blocked" :
		json_object_array_length(missing) ? "bind" : "none"));
	if (why)
		json_object_object_add(o, "reason", json_object_new_string(why));
	json_object_object_add(o, "missing", missing);
	json_object_object_add(o, "collateral", collateral);
	json_object_object_add(o, "class", json_object_new_string("ff"));
	return o;
}

json_object *mm_hostfix_extra(json_object *rec, json_object *bind)
{
	json_object *serial = get(bind, "serial"), *want = get(serial, "interfaces"), *ifs = get(rec, "interfaces");
	json_object *out = json_object_new_array();
	const char *driver = str(serial, "driver");
	size_t i;

	for (i = 0; driver && ifs && i < json_object_array_length(ifs); i++) {
		json_object *f = json_object_array_get_idx(ifs, i);
		const char *drv = str(f, "driver"), *sys = str(f, "sysfs");

		if (sys && drv && !strcmp(drv, driver) && !serial_wants(bind, f))
			json_object_array_add(out, json_object_new_string(sys));
	}
	(void)want;
	return out;
}

/*
 * The data driver's own new_id (bind.net), for an ID no kernel table names (1bc7:1910: a field hotplug script writes qmi_wwan's
 * new_id, then gives interfaces 2-4 to the serial driver). {"action": "none" | "bind" | "rescue", "missing": [sysfs],
 * "rescue": [{"sysfs", "driver"}], "collateral": [sysfs], "class": "ff"}: rescue is a data interface a serial
 * driver took (a field report). An interface another data driver holds (cdc_mbim: the firmware
 * came up in MBIM) is left alone. NULL without a net block.
 */
json_object *mm_hostfix_net_plan(json_object *rec, json_object *bind)
{
	json_object *net = get(bind, "net"), *want = get(net, "interfaces"), *ifs = get(rec, "interfaces");
	json_object *o, *missing, *rescue, *collateral;
	const char *driver = str(net, "driver");
	size_t i;

	if (!net || !driver || !want)
		return NULL;
	o = json_object_new_object();
	missing = json_object_new_array();
	rescue = json_object_new_array();
	collateral = json_object_new_array();
	for (i = 0; ifs && i < json_object_array_length(ifs); i++) {
		json_object *f = json_object_array_get_idx(ifs, i);
		const char *num = str(f, "number"), *drv = str(f, "driver"), *sys = str(f, "sysfs");

		if (!sys)
			continue;
		if (listed(want, num)) {
			if (!drv) {
				json_object_array_add(missing, json_object_new_string(sys));
			} else if (is_serial_driver(drv)) {
				json_object *r = json_object_new_object();

				json_object_object_add(r, "sysfs", json_object_new_string(sys));
				json_object_object_add(r, "driver", json_object_new_string(drv));
				json_object_array_add(rescue, r);
			}
		} else if (!drv && is_vendor(f)) {
			json_object_array_add(collateral, json_object_new_string(sys));
		}
	}
	json_object_object_add(o, "action", json_object_new_string(json_object_array_length(rescue) ? "rescue" :
		json_object_array_length(missing) ? "bind" : "none"));
	json_object_object_add(o, "missing", missing);
	json_object_object_add(o, "rescue", rescue);
	json_object_object_add(o, "collateral", collateral);
	json_object_object_add(o, "class", json_object_new_string("ff"));
	return o;
}

/* Interfaces the data driver holds that the net block did not ask for (its new_id took them). New array. */
json_object *mm_hostfix_net_extra(json_object *rec, json_object *bind)
{
	json_object *net = get(bind, "net"), *want = get(net, "interfaces"), *ifs = get(rec, "interfaces");
	json_object *out = json_object_new_array();
	const char *driver = str(net, "driver");
	size_t i;

	for (i = 0; driver && ifs && i < json_object_array_length(ifs); i++) {
		json_object *f = json_object_array_get_idx(ifs, i);
		const char *drv = str(f, "driver"), *sys = str(f, "sysfs");

		if (sys && drv && !strcmp(drv, driver) && !listed(want, str(f, "number")))
			json_object_array_add(out, json_object_new_string(sys));
	}
	return out;
}

/* which kernel driver an unbound interface waits for, or NULL (spec 15.2: what the router lacks) */
static const char *wanted_driver(const char *vidpid, json_object *f)
{
	const char *cls = str(f, "class"), *sub = str(f, "subclass"), *proto = str(f, "protocol");

	if (!cls)
		return NULL;
	/* an iPhone's tethering interface (ff/fd/01, the ipheth driver); its other vendor ones (usbmux) are no modem's */
	if (vidpid && !strncasecmp(vidpid, "05ac:", 5))
		return !strcasecmp(cls, "ff") && sub && proto && !strcasecmp(sub, "fd") && !strcasecmp(proto, "01") ? "ipheth" : NULL;
	if (!strcasecmp(cls, "02") && sub) {
		/* RNDIS on a CDC ACM class with the vendor protocol (02/02/ff): the FM350's 00 (its 01 is the 0a data
		 * interface), Android tethering; the kernel's rndis_host claims it. Read as nothing, a router without
		 * kmod-usb-net-rndis was never told (an FM350 tester, 2026-11-03) */
		if (!strcasecmp(sub, "02") && proto && !strcasecmp(proto, "ff"))
			return "rndis_host";
		if (!strcasecmp(sub, "0e"))
			return "cdc_mbim";
		if (!strcasecmp(sub, "0d"))
			return "cdc_ncm";
		if (!strcasecmp(sub, "06"))
			return "cdc_ether";
		return NULL;
	}
	if ((!strcasecmp(cls, "e0") && sub && proto && !strcasecmp(sub, "01") && !strcasecmp(proto, "03")) ||
	    (!strcasecmp(cls, "ef") && sub && proto && !strcasecmp(sub, "04") && !strcasecmp(proto, "01")))
		return "rndis_host";
	if (strcasecmp(cls, "ff") || (sub && !strcasecmp(sub, "42")))	/* 42: ADB, no driver of ours */
		return NULL;
	/* Huawei's NCM data interface: protocol 0x16, 0x46 or 0x76 */
	if (vidpid && !strncasecmp(vidpid, "12d1:", 5) && proto &&
	    (!strcasecmp(proto, "16") || !strcasecmp(proto, "46") || !strcasecmp(proto, "76")))
		return "huawei_cdc_ncm";
	/* any other vendor interface left alone: the serial driver first (QMI, MBIM and NCM ones
	 * belong to drivers routers carry with the firmware's own modem support) */
	return "option";
}

static int by_string(const void *a, const void *b)
{
	return strcmp(json_object_get_string(*(json_object *const *)a), json_object_get_string(*(json_object *const *)b));
}

static int by_first_interface(const void *a, const void *b)
{
	json_object *x = get(*(json_object *const *)a, "interfaces"), *y = get(*(json_object *const *)b, "interfaces");
	const char *p = json_object_array_length(x) ? json_object_get_string(json_object_array_get_idx(x, 0)) : "";
	const char *q = json_object_array_length(y) ? json_object_get_string(json_object_array_get_idx(y, 0)) : "";

	return strcmp(p, q);
}

static const char *const driver_packages[][2] = {
	{ "option", "kmod-usb-serial-option" }, { "qmi_wwan", "kmod-usb-net-qmi-wwan" },
	{ "cdc_mbim", "kmod-usb-net-cdc-mbim" }, { "cdc_ncm", "kmod-usb-net-cdc-ncm" },
	{ "huawei_cdc_ncm", "kmod-usb-net-huawei-cdc-ncm" }, { "cdc_ether", "kmod-usb-net-cdc-ether" },
	{ "rndis_host", "kmod-usb-net-rndis" }, { "ipheth", "kmod-usb-net-ipheth" },
};

static bool is_net_driver(const char *d)
{
	return d && (mm_is_qmi_driver(d) || !strcmp(d, "cdc_mbim") || !strcmp(d, "cdc_ncm") || !strcmp(d, "huawei_cdc_ncm") ||
		     !strcmp(d, "cdc_ether") || !strcmp(d, "rndis_host") || !strcmp(d, "ipheth"));
}

json_object *mm_hostfix_missing_drivers(json_object *rec, bool (*have)(const char *driver))
{
	json_object *ifs = get(rec, "interfaces"), *out = json_object_new_array();
	const char *vidpid = str(rec, "vidpid"), *qmi = NULL;
	bool netless = true;
	size_t i, k, n;

	/*
	 * A Qualcomm-style modem's QMI data interface is a vendor one too (ff/ff/ff, ff/ff/50 on the T99): with
	 * qmi_wwan missing and no network driver holding any interface, the highest-numbered such interface is
	 * taken for QMI, the others for the serial driver (review 2026-10-17: all of them went to option, and
	 * once option was in the warning went away with the data interface still unbound)
	 */
	for (i = 0; ifs && i < json_object_array_length(ifs); i++)
		if (is_net_driver(str(json_object_array_get_idx(ifs, i), "driver")))
			netless = false;
	/* a serial-only modem by its profile (protocols «ppp»: a ZTE MF100 dials PPP on its AT port) has no QMI interface:
	 * every vendor one is the serial driver's (a tester's MF100, 2026-11-12, was told to install kmod-usb-net-qmi-wwan) */
	{
		json_object *protos = get(get(rec, "profile"), "protocols");

		for (i = 0; protos && json_object_is_type(protos, json_type_array) && i < json_object_array_length(protos); i++)
			if (!strcmp(json_object_get_string(json_object_array_get_idx(protos, i)) ?: "", "ppp"))
				netless = false;
	}
	for (i = 0; netless && ifs && i < json_object_array_length(ifs) && !(have && have("qmi_wwan")); i++) {
		json_object *f = json_object_array_get_idx(ifs, i);
		const char *c = str(f, "class"), *sc = str(f, "subclass"), *p = str(f, "protocol"), *num = str(f, "number");

		if (!str(f, "driver") && c && sc && p && !strcasecmp(c, "ff") && !strcasecmp(sc, "ff") &&
		    (!strcasecmp(p, "ff") || !strcasecmp(p, "50")) && num && (!qmi || strcmp(num, qmi) > 0) &&
		    wanted_driver(vidpid, f) && !strcmp(wanted_driver(vidpid, f), "option"))
			qmi = num;
	}
	for (i = 0; ifs && i < json_object_array_length(ifs); i++) {
		json_object *f = json_object_array_get_idx(ifs, i), *entry = NULL, *nums;
		const char *drv = wanted_driver(vidpid, f), *num = str(f, "number");

		if (drv && qmi && num && !strcmp(num, qmi))
			drv = "qmi_wwan";

		if (str(f, "driver") || !drv || (have && have(drv)))
			continue;
		for (k = 0; k < json_object_array_length(out); k++)
			if (!strcmp(str(json_object_array_get_idx(out, k), "driver"), drv))
				entry = json_object_array_get_idx(out, k);
		if (!entry) {
			entry = json_object_new_object();
			json_object_object_add(entry, "driver", json_object_new_string(drv));
			for (n = 0; n < sizeof(driver_packages) / sizeof(driver_packages[0]); n++)
				if (!strcmp(driver_packages[n][0], drv))
					json_object_object_add(entry, "package", json_object_new_string(driver_packages[n][1]));
			json_object_object_add(entry, "interfaces", json_object_new_array());
			json_object_array_add(out, entry);
		}
		nums = get(entry, "interfaces");
		if (num)
			json_object_array_add(nums, json_object_new_string(num));
	}
	/* sysfs lists interfaces in no order: by interface number, the drivers by their first one */
	for (k = 0; k < json_object_array_length(out); k++)
		json_object_array_sort(get(json_object_array_get_idx(out, k), "interfaces"), by_string);
	json_object_array_sort(out, by_first_interface);
	return out;
}
