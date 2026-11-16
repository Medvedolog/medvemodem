#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "roles.h"
#include "never_at.h"
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

static const char *bare(const char *node)
{
	return strncmp(node, "/dev/", 5) ? node : node + 5;
}

static json_object *interface_named(json_object *rec, const char *sysfs)
{
	json_object *ifaces = get(rec, "interfaces");
	size_t i;

	for (i = 0; sysfs && ifaces && i < json_object_array_length(ifaces); i++) {
		json_object *it = json_object_array_get_idx(ifaces, i);
		const char *n = str(it, "sysfs");
		if (n && !strcmp(n, sysfs))
			return it;
	}
	return NULL;
}

/* ModemManager port type -> our role */
static const char *mm_role(const char *type)
{
	static const char *const map[][2] = {
		{ "at", "at" }, { "qcdm", "diag" }, { "gps", "gnss" }, { "net", "data" },
		{ "qmi", "control" }, { "mbim", "control" }, { "audio", "reserved" },
	};
	size_t i;

	for (i = 0; type && i < sizeof(map) / sizeof(map[0]); i++)
		if (!strcmp(type, map[i][0]))
			return map[i][1];
	return NULL;
}

static bool is(const char *a, const char *b)
{
	return a && b && !strcasecmp(a, b);
}

struct verdict {
	const char *role, *confidence, *source;
};

/*
 * Huawei's vendor interfaces say their function in bInterfaceProtocol, the low digit: 1 the
 * modem (PPP), 2 PCUI (the AT port), 3 DIAG, 5 GPS, 6 NCM; the high digit is the family
 * (0x, 1x, 3x, 4x, 6x, 7x). ModemManager's huawei port rules read the same (E171 12d1:1506,
 * HANDOFF 129). The AT commands go to PCUI only, never to DIAG or GPS.
 */
static struct verdict huawei_tty(json_object *iface)
{
	const char *c = str(iface, "class"), *p = str(iface, "protocol");
	struct verdict v = { "unknown", "none", "none" };

	if (!is(c, "ff") || !p || strlen(p) != 2 || !strchr("013467", p[0]))
		return v;
	switch (p[1]) {
	case '1': return (struct verdict){ "dial", "high", "interface" };
	case '2': return (struct verdict){ "at", "high", "interface" };
	case '3': return (struct verdict){ "diag", "high", "interface" };
	case '5': return (struct verdict){ "gnss", "high", "interface" };
	default: return v;
	}
}

/* What the driver and interface alone prove about a node. */
static struct verdict structural(const char *kind, json_object *iface, const char *vidpid)
{
	const char *drv = str(iface, "driver"), *c = str(iface, "class"), *sc = str(iface, "subclass"), *p = str(iface, "protocol");
	struct verdict v = { "unknown", "none", "none" };

	if (!strcmp(kind, "net")) {
		v = (struct verdict){ "data", "high", "driver" };
	} else if (!strcmp(kind, "control")) {
		if (mm_is_qmi_driver(drv) || is(drv, "cdc_mbim"))
			v = (struct verdict){ "control", "high", "driver" };
		else
			v = (struct verdict){ "control", "medium", "driver" };
	} else if (is(c, "ff") && is(sc, "42")) {
		/* Android debug bridge / fastboot interface: never an AT port */
		v = (struct verdict){ "reserved", "high", "interface" };
	} else if (is(c, "02") && is(sc, "02") && is(p, "01")) {
		/* CDC ACM, protocol 01 = V.250 AT commands */
		v = (struct verdict){ "at", "medium", "interface" };
	} else if (!strcmp(kind, "tty") && vidpid && !strncasecmp(vidpid, "12d1:", 5)) {
		v = huawei_tty(iface);
	}
	return v;
}

/* the operator's AT interface (UCI device option at_interface) as a bInterfaceNumber, or -1: none, not a number,
 * or one that is never sent AT (audit F01: the choice is ignored, the profile's stands) */
static long operator_choice(json_object *rec)
{
	const char *want = str(rec, "operator_at");
	char *end, num[8];
	long w;

	if (!want)
		return -1;
	w = strtol(want, &end, 16);
	if (end == want || *end || w < 0 || w > 255)
		return -1;
	snprintf(num, sizeof(num), "%02lx", w);
	return mm_never_at(str(rec, "vidpid"), num) ? -1 : w;
}

/* the interface number is the one the operator chose as the AT port */
static bool operator_at(json_object *rec, json_object *iface)
{
	const char *num = str(iface, "number");
	long w = operator_choice(rec);

	return w >= 0 && num && strtol(num, NULL, 16) == w;	/* bInterfaceNumber is hex in sysfs */
}

/* the interface number is the data port of the profile's dial recipe (XMM over NCM): data only, never AT, never probed */
static bool profile_data(json_object *rec, json_object *iface)
{
	json_object *n = get(get(get(get(rec, "profile"), "dial"), "data_port"), "interface"), *num = get(iface, "number");

	return n && num && json_object_is_type(n, json_type_int) &&
	       strtol(json_object_get_string(num), NULL, 16) == json_object_get_int(n);	/* bInterfaceNumber is hex */
}

/* the interface number is one the device's profile names as an AT port */
static bool profile_at(json_object *rec, json_object *iface)
{
	json_object *list = get(get(rec, "profile"), "at_interfaces"), *num = get(iface, "number");
	size_t i;
	long n;

	if (!list || !num || !json_object_is_type(list, json_type_array))
		return false;
	n = strtol(json_object_get_string(num), NULL, 16);	/* bInterfaceNumber is hex in sysfs */
	for (i = 0; i < json_object_array_length(list); i++)
		if (json_object_get_int(json_object_array_get_idx(list, i)) == n)
			return true;
	return false;
}

static void add_candidates(json_object *out, json_object *rec, json_object *mm, const char *kind, const char *list_key)
{
	json_object *ports = get(get(rec, "ports"), list_key), *ifmap = get(rec, "port_interfaces"), *types = get(mm, "port_types");
	size_t i;

	for (i = 0; ports && i < json_object_array_length(ports); i++) {
		const char *node = json_object_get_string(json_object_array_get_idx(ports, i));
		const char *ifname = str(ifmap, node), *mmtype = str(types, bare(node));
		json_object *iface = interface_named(rec, ifname), *c = json_object_new_object();
		struct verdict v = structural(kind, iface, str(rec, "vidpid"));
		const char *probe = str(get(rec, "probes"), node);
		/* never AT, whoever asks: nothing below may make it the AT port (audit F01) */
		bool never = !strcmp(kind, "tty") && mm_never_at(str(rec, "vidpid"), str(iface, "number"));
		char klass[16];

		/* the operator's choice replaces the profile's: a firmware may number its interfaces otherwise */
		if (!strcmp(kind, "tty") && !strcmp(v.role, "unknown") && operator_at(rec, iface))
			v = (struct verdict){ "at", "high", "operator" };
		/* the profile names the AT interface (bench-validated): better than any probe (spec 11.5) */
		else if (!strcmp(kind, "tty") && !strcmp(v.role, "unknown") && operator_choice(rec) < 0 && profile_at(rec, iface))
			v = (struct verdict){ "at", "high", "profile" };
		/* a bounded probe of this attachment generation (spec 11.5) */
		if (!strcmp(kind, "tty") && !strcmp(v.role, "unknown") && probe)
			v = !strcmp(probe, "at") ? (struct verdict){ "at", "medium", "probe" }
						 : (struct verdict){ "unknown", "none", "probe" };
		/* «ignored» is no role: the engine's own rule (mm_ports "control") makes ModemManager ignore the very AT ports
		 * the engine needs (bench 2026-10-27, a T77 after a ModemManager restart: ttyUSB0/1/3 «ignored», taken for
		 * reserved, neither probed nor choosable) */
		if (mm_role(mmtype) && strcmp(mmtype, "ignored"))
			v = (struct verdict){ mm_role(mmtype), "high", "modemmanager" };
		else if (!strcmp(kind, "tty") && profile_data(rec, iface))
			v = (struct verdict){ "data", "high", "profile" };
		if (never)
			v = (struct verdict){ "reserved", "high", "never-at" };
		json_object_object_add(c, "node", json_object_new_string(node));
		json_object_object_add(c, "kind", json_object_new_string(kind));
		json_object_object_add(c, "interface", ifname ? json_object_new_string(ifname) : NULL);
		json_object_object_add(c, "number", json_object_get(get(iface, "number")));
		if (iface && str(iface, "class")) {
			snprintf(klass, sizeof(klass), "%s/%s/%s", str(iface, "class"),
				 str(iface, "subclass") ? str(iface, "subclass") : "?",
				 str(iface, "protocol") ? str(iface, "protocol") : "?");
			json_object_object_add(c, "class", json_object_new_string(klass));
		} else {
			json_object_object_add(c, "class", NULL);
		}
		json_object_object_add(c, "driver", json_object_get(get(iface, "driver")));
		json_object_object_add(c, "role", json_object_new_string(v.role));
		json_object_object_add(c, "confidence", json_object_new_string(v.confidence));
		json_object_object_add(c, "source", json_object_new_string(v.source));
		/* a port ModemManager lists as «ignored» is not open in it: the engine's to use (the owner, 2026-10-28: the T77's
		 * profile port ttyUSB1 was «ModemManager's», no AT, no metrics) */
		json_object_object_add(c, "owner", json_object_new_string(mmtype && strcmp(mmtype, "ignored") ? "modemmanager" : "none"));
		json_object_array_add(out, c);
	}
}

/* The best candidate with this role: ModemManager's primary port first, then confidence. */
static json_object *pick(json_object *cands, const char *role, const char *primary)
{
	static const char *const rank[] = { "high", "medium", "low" };
	size_t r, i;

	for (i = 0; primary && i < json_object_array_length(cands); i++) {
		json_object *c = json_object_array_get_idx(cands, i);
		if (!strcmp(str(c, "role"), role) && !strcmp(bare(str(c, "node")), primary))
			return c;
	}
	for (r = 0; r < sizeof(rank) / sizeof(rank[0]); r++)
		for (i = 0; i < json_object_array_length(cands); i++) {
			json_object *c = json_object_array_get_idx(cands, i);
			if (!strcmp(str(c, "role"), role) && !strcmp(str(c, "confidence"), rank[r]))
				return c;
		}
	return NULL;
}

static json_object *summary(json_object *c)
{
	json_object *o;

	if (!c)
		return NULL;
	o = json_object_new_object();
	json_object_object_add(o, "node", json_object_get(get(c, "node")));
	json_object_object_add(o, "driver", json_object_get(get(c, "driver")));
	json_object_object_add(o, "confidence", json_object_get(get(c, "confidence")));
	json_object_object_add(o, "source", json_object_get(get(c, "source")));
	json_object_object_add(o, "owner", json_object_get(get(c, "owner")));
	return o;
}

json_object *mm_roles_resolve(json_object *rec, json_object *mm, const char *mm_state)
{
	json_object *out = json_object_new_object(), *cands = json_object_new_array(), *owner = json_object_new_object();
	json_object *at, *ctl, *ctl_ports, *ifaces = get(rec, "interfaces"), *ttys = get(get(rec, "ports"), "tty");
	const char *control, *session, *atown;
	bool router = false, any_unknown_tty = false;
	/* a web modem whose profile keeps its serial ports from ModemManager (HiLink debug, ZTE with AT functions): the
	 * ttys do not make it a modem; its data stays DHCP from the stick and its web API (profiles audit 2026-10-19) */
	json_object *prof = get(rec, "profile");
	bool pinned = is(str(prof, "prefer_owner"), "hilink") && is(str(prof, "mm_ports"), "control");
	size_t i;

	add_candidates(cands, rec, mm, "tty", "tty");
	add_candidates(cands, rec, mm, "control", "control");
	add_candidates(cands, rec, mm, "net", "net");
	at = pick(cands, "at", str(mm, "primary_port"));
	/*
	 * Two AT ports of the profile at one confidence: the profile's order decides, not the tty numbering of this boot
	 * (an LN960 tester, 2026-11-03: the pick moved between 03, which answers, and 04, which stays silent, with each
	 * replug). ModemManager's primary port, an operator's choice and a probe still come first (they rank higher).
	 */
	if (at && !strcmp(str(at, "source") ? str(at, "source") : "", "profile")) {
		json_object *order = get(prof, "at_interfaces");
		size_t k, j;

		for (k = 0; order && json_object_is_type(order, json_type_array) && k < json_object_array_length(order); k++) {
			json_object *best = NULL;

			for (j = 0; j < json_object_array_length(cands) && !best; j++) {
				json_object *c = json_object_array_get_idx(cands, j);

				if (!strcmp(str(c, "role"), "at") && str(c, "source") && !strcmp(str(c, "source"), "profile") &&
				    !strcmp(str(c, "confidence"), str(at, "confidence")) && str(c, "number") &&
				    strtol(str(c, "number"), NULL, 16) == json_object_get_int(json_object_array_get_idx(order, k)))
					best = c;
			}
			if (best) {
				at = best;
				break;
			}
		}
	}
	ctl = pick(cands, "control", str(mm, "primary_port"));
	for (i = 0; i < json_object_array_length(cands); i++) {
		json_object *c = json_object_array_get_idx(cands, i);
		if (!strcmp(str(c, "kind"), "tty") && !strcmp(str(c, "role"), "unknown"))
			any_unknown_tty = true;
	}
	for (i = 0; ifaces && i < json_object_array_length(ifaces); i++) {
		const char *d = str(json_object_array_get_idx(ifaces, i), "driver");
		if (is(d, "cdc_ether") || is(d, "rndis_host") || is(d, "ipheth"))
			router = true;
	}
	/* a removed modem keeps its interfaces but its ports are {} (registry.c): no "control" array, not an empty one
	 * (field 2026-10-30: a HiLink taken off the bus crashed the engine here) */
	ctl_ports = get(get(rec, "ports"), "control");
	router = router && !(ctl_ports && json_object_array_length(ctl_ports)) &&
		 (pinned || !(ttys && json_object_array_length(ttys)));

	/* spec 13 */
	if (router && pinned) {
		control = "hilink";
		session = "device";
		atown = "unavailable";
	} else if (mm) {
		control = "modemmanager";
		session = "modemmanager";
		atown = at ? (!strcmp(str(at, "owner"), "modemmanager") ? "modemmanager" : "medvemodem")
			   : any_unknown_tty ? "unresolved" : "unavailable";
	} else if (router) {
		control = "hilink";
		session = "device";
		atown = "unavailable";
	} else {
		/* without ModemManager, a native owner is a configuration decision (M2) */
		bool waiting = !mm_state || !strcmp(mm_state, "unknown") || !strcmp(mm_state, "available");
		control = "unresolved";
		session = "unresolved";
		atown = at ? "medvemodem" : any_unknown_tty || waiting ? "unresolved" : "unavailable";
	}
	json_object_object_add(owner, "control", json_object_new_string(control));
	json_object_object_add(owner, "session", json_object_new_string(session));
	json_object_object_add(owner, "at", json_object_new_string(atown));

	json_object_object_add(out, "generation", json_object_get(get(rec, "generation")));
	json_object_object_add(out, "owner", owner);
	json_object_object_add(out, "at", summary(at));
	json_object_object_add(out, "control", summary(ctl));
	json_object_object_add(out, "candidates", cands);
	return out;
}

json_object *mm_roles_candidate(json_object *roles, const char *node)
{
	json_object *cands = get(roles, "candidates");
	size_t i;

	for (i = 0; node && cands && i < json_object_array_length(cands); i++) {
		json_object *c = json_object_array_get_idx(cands, i);
		if (!strcmp(bare(str(c, "node")), bare(node)))
			return c;
	}
	return NULL;
}
