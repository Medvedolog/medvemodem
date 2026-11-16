#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "classify.h"

bool mm_array_nonempty(json_object *arr)
{
	return arr && json_object_array_length(arr) > 0;
}

bool mm_is_qmi_driver(const char *driver)
{
	return driver && (!strcmp(driver, "qmi_wwan") || !strcmp(driver, "qmi_wwan_q") || !strcmp(driver, "qmi_wwan_f") ||
			  !strcmp(driver, "qmi_wwan_s"));
}

bool mm_interfaces_have_driver(json_object *interfaces, const char *driver)
{
	size_t i;

	for (i = 0; interfaces && i < json_object_array_length(interfaces); i++) {
		json_object *iface = json_object_array_get_idx(interfaces, i);
		json_object *o = NULL;
		const char *s;

		if (!json_object_object_get_ex(iface, "driver", &o))
			continue;
		s = json_object_get_string(o);
		if (s && (!strcmp(s, driver) || (!strcmp(driver, "qmi_wwan") && mm_is_qmi_driver(s))))
			return true;
	}
	return false;
}

bool mm_likely_modem(json_object *interfaces, json_object *tty,
			 json_object *wdm, json_object *net, bool catalog_known)
{
	if (catalog_known || mm_array_nonempty(wdm))
		return true;
	if (mm_interfaces_have_driver(interfaces, "qmi_wwan") ||
	    mm_interfaces_have_driver(interfaces, "cdc_mbim") ||
	    mm_interfaces_have_driver(interfaces, "huawei_cdc_ncm") ||
	    mm_interfaces_have_driver(interfaces, "option") ||
	    mm_interfaces_have_driver(interfaces, "qcserial"))
		return true;
	/* a modem whose AT ports are all CDC ACM (a tester's ZTE MF192, 19d2:1218, 2026-11-09: eight ttyACM, ModemManager
	 * held it, the engine dropped it): two ACM ttys or more; an Arduino or a USB-UART adapter has one */
	if (mm_interfaces_have_driver(interfaces, "cdc_acm") && tty && json_object_array_length(tty) >= 2)
		return true;
	return (mm_array_nonempty(tty) || mm_array_nonempty(net)) &&
	       (mm_interfaces_have_driver(interfaces, "cdc_ncm") ||
		mm_interfaces_have_driver(interfaces, "cdc_ether") ||
		mm_interfaces_have_driver(interfaces, "rndis_host") ||
		mm_interfaces_have_driver(interfaces, "ipheth"));	/* an iPhone in tethering (phone kind) */
}

static const char *iface_field(json_object *iface, const char *key)
{
	json_object *o = NULL;
	return json_object_object_get_ex(iface, key, &o) ? json_object_get_string(o) : NULL;
}

static bool is_vidpid(const struct mm_facts *f, const char *vid, const char *pid)
{
	return f->vid && f->pid && !strcasecmp(f->vid, vid) && !strcasecmp(f->pid, pid);
}

/* A fastboot bootloader exposes one vendor interface ff/42/03 with no driver. */
static bool fastboot_signature(json_object *interfaces)
{
	json_object *iface;
	const char *c, *s, *p;

	if (!interfaces || json_object_array_length(interfaces) != 1)
		return false;
	iface = json_object_array_get_idx(interfaces, 0);
	c = iface_field(iface, "class");
	s = iface_field(iface, "subclass");
	p = iface_field(iface, "protocol");
	return c && s && p && !strcasecmp(c, "ff") && !strcasecmp(s, "42") &&
	       !strcasecmp(p, "03") && !iface_field(iface, "driver");
}

/*
 * A Huawei stick starts as a CD-ROM (and a card reader): vendor 12d1 with mass storage only
 * (field 2026-10-15, E171 12d1:155b). Its switch rule may be missing from usb-mode.json
 * (a newer ID, or the package not installed): still a modem waiting for its switch.
 */
bool mm_huawei_storage(const char *vid, json_object *interfaces)
{
	size_t i, n = interfaces ? json_object_array_length(interfaces) : 0;

	if (!vid || strcasecmp(vid, "12d1") || !n)
		return false;
	for (i = 0; i < n; i++) {
		const char *c = iface_field(json_object_array_get_idx(interfaces, i), "class");
		if (!c || strcasecmp(c, "08"))
			return false;
	}
	return true;
}

/* True when a usb-mode.json entry would put the device in configuration 0. */
bool mm_modeswitch_unconfigures(json_object *entry)
{
	if (!entry || !json_object_is_type(entry, json_type_object))
		return false;
	json_object_object_foreach(entry, key, variant) {
		json_object *cfg = NULL;
		(void)key;
		if (json_object_is_type(variant, json_type_object) &&
		    json_object_object_get_ex(variant, "config", &cfg) &&
		    json_object_is_type(cfg, json_type_int) && json_object_get_int(cfg) == 0)
			return true;
	}
	return false;
}

static json_object *finding(const char *state, const char *evidence, const char *evidence2)
{
	json_object *o = json_object_new_object(), *ev = json_object_new_array();

	json_object_object_add(o, "state", json_object_new_string(state));
	if (evidence)
		json_object_array_add(ev, json_object_new_string(evidence));
	if (evidence2)
		json_object_array_add(ev, json_object_new_string(evidence2));
	json_object_object_add(o, "evidence", ev);
	return o;
}

static size_t count_bound(json_object *interfaces, size_t *total)
{
	size_t i, n = 0;

	*total = interfaces ? json_object_array_length(interfaces) : 0;
	for (i = 0; i < *total; i++)
		if (iface_field(json_object_array_get_idx(interfaces, i), "driver"))
			n++;
	return n;
}

/* Drivers that always create a control node and a netdev together. */
static bool paired_driver(json_object *interfaces)
{
	return mm_interfaces_have_driver(interfaces, "qmi_wwan") ||
	       mm_interfaces_have_driver(interfaces, "cdc_mbim") ||
	       mm_interfaces_have_driver(interfaces, "huawei_cdc_ncm");
}

/*
 * Configuration health that needs no validated expectation (spec 14.1.1):
 * only invariants of the kernel drivers themselves, so a PPP-only modem or a
 * vendor interface that is legitimately left without a driver is not flagged.
 */
static json_object *structural_health(const struct mm_facts *f)
{
	size_t total, bound = count_bound(f->interfaces, &total);
	bool tty = mm_array_nonempty(f->tty), wdm = mm_array_nonempty(f->wdm), net = mm_array_nonempty(f->net);
	char buf[160];

	if (total && !bound) {
		snprintf(buf, sizeof(buf), "%zu interface%s, none with a driver bound", total, total == 1 ? "" : "s");
		return finding("unbound", buf, "probable cause: this image has no driver entry for the device (inferred)");
	}
	if (paired_driver(f->interfaces) && wdm && !net)
		return finding("incomplete", "control channel (cdc-wdm) without its network interface",
			       "the data interface did not come up; check the driver binding");
	if (paired_driver(f->interfaces) && net && !wdm)
		return finding("incomplete", "network interface without its control channel (cdc-wdm)", NULL);
	if (net && !wdm && !tty &&
	    (mm_interfaces_have_driver(f->interfaces, "cdc_ether") || mm_interfaces_have_driver(f->interfaces, "rndis_host") ||
	     mm_interfaces_have_driver(f->interfaces, "ipheth")))
		return finding("suboptimal", "router mode: network only (cdc_ether/rndis_host), no control channel",
			       "the modem routes and NATs itself; signal, SMS and recovery stay inside the device");
	return NULL;
}

enum mm_detect mm_classify(const struct mm_facts *f, json_object **configuration)
{
	char buf[160];
	bool likely, unconfigured;

	*configuration = NULL;

	/* Service modes are reported and never touched. */
	if (is_vidpid(f, "05c6", "9008")) {
		*configuration = finding("service-mode", "Qualcomm emergency download (EDL)",
					 "not touched by MedveModem");
		return MM_DETECT_SERVICE_MODE;
	}
	if (is_vidpid(f, "05c6", "900e")) {
		*configuration = finding("service-mode", "Qualcomm crash dump mode",
					 "not touched by MedveModem");
		return MM_DETECT_SERVICE_MODE;
	}
	/* a firmware loader is no modem (an audit of r181, D-06): only the IDs our own notes name as loaders, never a whole
	 * list found elsewhere (Sierra 1199:9078 is an EM74xx modem composition in our profile) */
	{
		static const struct { const char *vid, *pid, *what; } loaders[] = {
			{ "1199", "907a", "Sierra EM74xx firmware download (QDL)" },	/* profiles/generic/sierra-em74xx.json */
			{ "1199", "9090", "Sierra EM7565 firmware download (QDL)" },
			{ "0e8d", "0003", "MediaTek boot ROM download mode" },
		};
		size_t i;

		for (i = 0; i < sizeof(loaders) / sizeof(loaders[0]); i++)
			if (is_vidpid(f, loaders[i].vid, loaders[i].pid)) {
				*configuration = finding("service-mode", loaders[i].what, "not touched by MedveModem");
				return MM_DETECT_SERVICE_MODE;
			}
	}
	if (fastboot_signature(f->interfaces)) {
		*configuration = finding("service-mode", "fastboot bootloader (interface ff/42/03 without driver)",
					 NULL);
		return MM_DETECT_SERVICE_MODE;
	}

	/* ModemManager holding a modem on the device settles it (the owner, 2026-11-09: «при подозрении почему мы не умеем
	 * спрашивать через mmcli?»; the tester's MF192 was ModemManager's and still dropped) */
	likely = f->mm_held || mm_likely_modem(f->interfaces, f->tty, f->wdm, f->net, f->catalog_known);
	unconfigured = !f->config_value || !*f->config_value || !strcmp(f->config_value, "0");

	/* A switching rule that unconfigures a modem breaks it on every hotplug. */
	if (mm_modeswitch_unconfigures(f->modeswitch) && (likely || f->catalog_known || unconfigured)) {
		snprintf(buf, sizeof(buf), "usb-mode.json sets configuration 0 for %s:%s",
			 f->vid ? f->vid : "?", f->pid ? f->pid : "?");
		*configuration = finding("modeswitch-rule", buf,
					 unconfigured ? "device is unconfigured: no interface drivers bound" : NULL);
		return MM_DETECT_CANDIDATE;
	}
	if (likely) {
		*configuration = structural_health(f);
		if (!*configuration)
			*configuration = finding("unknown", "no validated expected composition for this model", NULL);
		return MM_DETECT_CANDIDATE;
	}
	if (f->modeswitch) {
		*configuration = finding("service-mode", "installer or storage mode; usb_modeswitch switches it",
					 NULL);
		return MM_DETECT_MODESWITCH_PENDING;
	}
	if (mm_huawei_storage(f->vid, f->interfaces)) {
		*configuration = finding("service-mode", "Huawei storage mode (CD-ROM); usb_modeswitch switches it",
					 "usb-mode.json has no rule for this ID");
		return MM_DETECT_MODESWITCH_PENDING;
	}
	return MM_DETECT_DROP;
}

const char *mm_detect_status(enum mm_detect d)
{
	switch (d) {
	case MM_DETECT_CANDIDATE:		return "candidate";
	case MM_DETECT_MODESWITCH_PENDING:	return "modeswitch-pending";
	case MM_DETECT_SERVICE_MODE:		return "service-mode";
	default:				return "dropped";
	}
}
