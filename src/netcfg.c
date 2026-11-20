#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <arpa/inet.h>

#include "cfgplan.h"
#include "netcfg.h"
#include "classify.h"

static json_object *get(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_is_type(o, json_type_object) && json_object_object_get_ex(o, key, &v) ? v : NULL;
}

static const char *str(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static const char *first(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_array_length(v) ? json_object_get_string(json_object_array_get_idx(v, 0)) : NULL;
}

static bool eq(const char *a, const char *b)
{
	return a && b && !strcmp(a, b);
}

/*
 * The network device a native AT dial puts its address on: the one of the recipe's USB interface when it names one
 * (XMM: the first of three NCM functions, which AT+XDATACHANNEL binds), else
 * the modem's first.
 */
static const char *dial_netdev(json_object *rec)
{
	json_object *want = get(get(get(rec, "profile"), "dial"), "net_interface");
	json_object *cands = get(get(rec, "port_roles"), "candidates");
	size_t i;

	for (i = 0; want && json_object_is_type(want, json_type_int) && cands && i < json_object_array_length(cands); i++) {
		json_object *c = json_object_array_get_idx(cands, i);

		if (eq(str(c, "kind"), "net") && str(c, "number") &&
		    strtol(str(c, "number"), NULL, 16) == json_object_get_int(want))	/* bInterfaceNumber is hex in sysfs */
			return str(c, "node");
	}
	return want ? NULL : first(get(rec, "ports"), "net");
}

/* /dev/cdc-wdm0 and cdc-wdm0 name the same node */
static bool same_node(const char *a, const char *b)
{
	if (!a || !b)
		return false;
	if (!strncmp(a, "/dev/", 5))
		a += 5;
	if (!strncmp(b, "/dev/", 5))
		b += 5;
	return !strcmp(a, b);
}

json_object *mm_netcfg_decision(json_object *current, const char *device_id)
{
	json_object *p = get(current, "medvemodem");

	if (!p || !device_id)
		return NULL;
	{
		json_object_object_foreach(p, sec, val) {
			(void)sec;
			if (eq(str(val, ".type"), "device") && eq(str(val, "id"), device_id))
				return val;
		}
	}
	return NULL;
}

static bool managed_by_anyone(json_object *ledgers, const char *iface)
{
	char key[128];

	snprintf(key, sizeof(key), "network.%s", iface);
	json_object_object_foreach(ledgers, id, led) {
		(void)id;
		if (get(get(led, "sections"), key))
			return true;
	}
	return false;
}

/* a network interface the user already set up for this modem */
static const char *user_interface(json_object *rec, json_object *network, json_object *ledgers)
{
	json_object *ports = get(rec, "ports"), *nodes;
	const char *mmdev = str(get(rec, "modemmanager"), "device");
	size_t i;

	json_object_object_foreach(network, name, s) {
		const char *proto = str(s, "proto"), *dev = str(s, "device"), *ifname = str(s, "ifname");
		bool ours = false;

		if (!eq(str(s, ".type"), "interface") || managed_by_anyone(ledgers, name))
			continue;
		if (eq(proto, "modemmanager"))
			ours = eq(dev, mmdev) || mm_netcfg_same_usb_port(dev, mmdev) ||	/* last boot's bus number (F9) */
			       (str(get(rec, "modemmanager"), "lookup") && eq(dev, str(get(rec, "modemmanager"), "lookup")));
		for (i = 0; !ours && (nodes = get(ports, "control")) && i < json_object_array_length(nodes); i++)
			ours = same_node(dev, json_object_get_string(json_object_array_get_idx(nodes, i)));
		for (i = 0; !ours && (nodes = get(ports, "tty")) && i < json_object_array_length(nodes); i++)
			ours = same_node(dev, json_object_get_string(json_object_array_get_idx(nodes, i)));
		for (i = 0; !ours && (nodes = get(ports, "net")) && i < json_object_array_length(nodes); i++) {
			const char *net = json_object_get_string(json_object_array_get_idx(nodes, i));
			ours = eq(dev, net) || eq(ifname, net);
		}
		if (ours)
			return name;
	}
	return NULL;
}

/* the interface the user already set up for this modem, for the device view (connection.user_interface) */
const char *mm_netcfg_user_interface(json_object *rec, json_object *network, json_object *ledgers)
{
	return rec && network ? user_interface(rec, network, ledgers) : NULL;
}

/*
 * A session interface set up outside MedveModem on this modem's ports with a protocol that talks to the modem itself
 * (umbim, uqmi, comgt...): a second master beside ModemManager (field 2026-10-27, a Nokia on a snapshot: its own `lte`
 * over umbim and the ModemManager installed with the engine on one MBIM port). {"interface", "proto"} or NULL.
 */
json_object *mm_netcfg_foreign(json_object *rec, json_object *network, json_object *ledgers)
{
	static const char *const protos[] = { "mbim", "qmi", "ncm", "3g", "wwan", "directip", NULL };
	json_object *ports = get(rec, "ports"), *nodes, *out;
	size_t i;
	int k;

	json_object_object_foreach(network, name, s) {
		const char *proto = str(s, "proto"), *dev = str(s, "device");
		bool ours = false, session = false;

		if (!eq(str(s, ".type"), "interface") || managed_by_anyone(ledgers, name) || !proto || !dev)
			continue;
		for (k = 0; protos[k] && !session; k++)
			session = !strcmp(proto, protos[k]);
		if (!session)
			continue;
		for (i = 0; !ours && (nodes = get(ports, "control")) && i < json_object_array_length(nodes); i++)
			ours = same_node(dev, json_object_get_string(json_object_array_get_idx(nodes, i)));
		for (i = 0; !ours && (nodes = get(ports, "tty")) && i < json_object_array_length(nodes); i++)
			ours = same_node(dev, json_object_get_string(json_object_array_get_idx(nodes, i)));
		if (!ours)
			continue;
		out = json_object_new_object();
		json_object_object_add(out, "interface", json_object_new_string(name));
		json_object_object_add(out, "proto", json_object_new_string(proto));
		if (eq(str(s, "auto"), "0") || eq(str(s, "disabled"), "1"))
			json_object_object_add(out, "off", json_object_new_boolean(1));
		return out;
	}
	return NULL;
}

static const char *ledger_interface(json_object *ledgers, const char *device_id)
{
	return str(get(ledgers, device_id), "interface");
}

/* QModem's own name for an interface: the USB path with its dots and dashes made underscores (3_1_3) */
static bool slot_name(const char *s)
{
	bool digit = false, sep = false;

	for (; s && *s; s++) {
		if (isdigit((unsigned char)*s))
			digit = true;
		else if (*s == '_' && digit)
			sep = true, digit = false;
		else
			return false;
	}
	return sep && digit;
}

/* the lowest mm<N> that no section and no other device's ledger uses */
static void allocate(json_object *network, json_object *ledgers, char *out, size_t size)
{
	int n;

	for (n = 0; n < 1000; n++) {
		bool used = false;

		snprintf(out, size, "mm%d", n);
		if (get(network, out))
			continue;
		json_object_object_foreach(ledgers, id, led) {
			(void)id;
			used = used || eq(str(led, "interface"), out);
		}
		if (!used)
			return;
	}
}

/*
 * May the operator's interface name be (re)used for this modem? A free name
 * may; an existing section only when it is an interface with a mobile proto
 * that no other modem's ledger holds.
 */
static bool modem_section(json_object *network, json_object *ledgers, const char *id, const char *name)
{
	static const char *const protos[] = { "modemmanager", "qmi", "mbim", "ncm", "wwan", "3g", "directip", "qcm", NULL };
	json_object *s = get(network, name);
	const char *proto = str(s, "proto");
	int i;

	if (!s)
		return true;
	if (!eq(str(s, ".type"), "interface"))
		return false;
	json_object_object_foreach(ledgers, lid, led)
		if (strcmp(lid, id ? id : "") && eq(str(led, "interface"), name))
			return false;
	for (i = 0; protos[i]; i++)
		if (eq(proto, protos[i]))
			return true;
	return false;
}

static json_object *result(const char *action, const char *reason)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "action", json_object_new_string(action));
	json_object_object_add(o, "reason", json_object_new_string(reason));
	return o;
}

static bool valid_name(const char *s)
{
	size_t n = s ? strlen(s) : 0;

	return n > 0 && n <= 15 && strspn(s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == n;
}

const char *mm_netcfg_ledger_proto(json_object *ledger)
{
	char key[64];
	const char *iface = str(ledger, "interface");

	if (!iface)
		return NULL;
	snprintf(key, sizeof(key), "network.%s", iface);
	return str(get(get(get(ledger, "sections"), key), "fields"), "proto");
}

void mm_netcfg_qmodem_name(json_object *rec, char *out, size_t size)
{
	const char *att = str(rec, "attachment_id");
	size_t j = 0;

	out[0] = '\0';
	if (!att || strncmp(att, "usb:", 4))
		return;
	for (att += 4; *att && j + 1 < size; att++)
		out[j++] = (*att == '.' || *att == ':' || *att == '-') ? '_' : *att;
	out[j] = '\0';
}

const char *mm_netcfg_qmodem_section(json_object *current, json_object *rec)
{
	json_object *q = get(current, "qmodem");
	const char *att = str(rec, "attachment_id");
	char name[64];

	if (!q || !att || strncmp(att, "usb:", 4))
		return NULL;
	mm_netcfg_qmodem_name(rec, name, sizeof(name));
	{
		json_object_object_foreach(q, sec, val) {
			const char *path = str(val, "path"), *base = path ? strrchr(path, '/') : NULL;
			if (!eq(str(val, ".type"), "modem-device"))
				continue;
			if (!strcmp(sec, name) || (base && !strcmp(base + 1, att + 4)))
				return sec;
		}
	}
	return NULL;
}

static bool has_proto(json_object *env, const char *proto)
{
	json_object *list = get(env, "protos");
	size_t i;

	for (i = 0; list && i < json_object_array_length(list); i++)
		if (eq(json_object_get_string(json_object_array_get_idx(list, i)), proto))
			return true;
	return false;
}

/*
 * operator options copied for a session protocol; iptype is pdptype for uqmi/umbim. With
 * option apn_auto '1' (the owner's request, test report 2026-10-15) the APN comes from the
 * SIM's suggestion (the record's connection.apn_suggestion: the APN database by the home
 * network, IMSI, SPN, GID1), and its login with it, while the operator typed none: an APN the
 * operator typed always wins, the field stays theirs (the owner, 2026-10-16).
 */
json_object *mm_netcfg_manual_dns(json_object *decision)
{
	json_object *v = get(decision, "dns"), *out = json_object_new_array();
	char buf[512], *save = NULL, *w;
	size_t i, n = v && json_object_is_type(v, json_type_array) ? json_object_array_length(v) : 1;

	for (i = 0; v && i < n && json_object_array_length(out) < MM_NETCFG_DNS_MAX; i++) {
		snprintf(buf, sizeof(buf), "%s", json_object_get_string(json_object_is_type(v, json_type_array) ?
									  json_object_array_get_idx(v, i) : v));
		for (w = strtok_r(buf, " ,\t", &save); w && json_object_array_length(out) < MM_NETCFG_DNS_MAX;
		     w = strtok_r(NULL, " ,\t", &save)) {
			unsigned char a[16];

			if (inet_pton(AF_INET, w, a) == 1 || inet_pton(AF_INET6, w, a) == 1)
				json_object_array_add(out, json_object_new_string(w));
		}
	}
	if (!json_object_array_length(out)) {
		json_object_put(out);
		return NULL;
	}
	return out;
}

/* the servers the operator typed (option dns): netifd's list dns, beside the operator's from the session (field request
 * 2026-10-23). The operator's stay unless option dns_mode is 'replace' (peerdns '0'): under an operator's whitelist
 * foreign resolvers (8.8.8.8, 1.1.1.1) do not answer, and alone they leave the router without names
 * (for a session that came up with no DNS at all) */
static void manual_dns(json_object *fields, json_object *decision)
{
	json_object *dns = mm_netcfg_manual_dns(decision);

	if (!dns)
		return;
	if (eq(str(decision, "dns_mode"), "replace"))
		json_object_object_add(fields, "peerdns", json_object_new_string("0"));
	json_object_object_add(fields, "dns", dns);
}

/* set by mm_netcfg_desired for the call: the modem has no interface of ours yet */
static bool copy_fresh;

static void copy_options(json_object *fields, json_object *decision, bool native, bool pin, json_object *rec)
{
	static const char *const opts[] = { "apn", "auth", "username", "password", "metric", NULL };
	json_object *sug = get(get(rec, "connection"), "apn_suggestion");
	/* '1' is the operator's; unset is on only where the interface is made (the owner, 2026-10-17: a new modem
	 * connects with its SIM's APN): a working setup with an empty APN keeps the network's default (review) */
	bool auto_apn = (eq(str(decision, "apn_auto"), "1") || (copy_fresh && !str(decision, "apn_auto"))) &&
			!(str(decision, "apn") && *str(decision, "apn")) &&
			str(sug, "apn") && *str(sug, "apn");
	int i;

	for (i = 0; opts[i]; i++) {
		const char *v = str(decision, opts[i]);

		if (auto_apn && strcmp(opts[i], "metric")) {
			v = str(sug, opts[i]);
			/* the database says "pap-or-chap" and "none"; netifd's qmi says "both" and needs no "none" */
			if (!strcmp(opts[i], "auth") && v)
				v = eq(v, "pap-or-chap") ? "both" : eq(v, "none") ? NULL : v;
		}
		if (v && *v)
			json_object_object_add(fields, opts[i], json_object_new_string(v));
	}
	if (str(decision, "iptype") && *str(decision, "iptype"))
		json_object_object_add(fields, native ? "pdptype" : "iptype", json_object_new_string(str(decision, "iptype")));
	/* the SIM PIN the operator chose to keep (option pin): OpenWrt's proto modemmanager, qmi and mbim
	 * unlock the SIM with option pincode at every ifup, so a locked SIM comes up by itself after a
	 * reboot. Digits only, 4 to 8: anything else is not a PIN and never reaches a proto script. */
	if (pin && str(decision, "pin")) {
		const char *p = str(decision, "pin");
		size_t n = strlen(p);

		if (n >= 4 && n <= 8 && strspn(p, "0123456789") == n)
			json_object_object_add(fields, "pincode", json_object_new_string(p));
	}
	manual_dns(fields, decision);
}

/*
 * The SIM PIN (option pincode) and the APN password (option password) in a
 * served copy of a plan, a ledger or a transaction log: the value becomes
 * "(hidden)". A key named pincode, pin or password, and the value, expect,
 * previous, current, ours and wanted of an op or a conflict about either option.
 */
static bool pin_key(const char *k)
{
	return !strcmp(k, "pincode") || !strcmp(k, "pin") || !strcmp(k, "password");
}

static bool ends_with(const char *s, size_t n, const char *tail)
{
	size_t t = strlen(tail);

	return n > t && !strcmp(s + n - t, tail);
}

void mm_netcfg_hide_pin(json_object *o)
{
	static const char *const vals[] = { "value", "expect", "previous", "current", "ours", "wanted", NULL };
	size_t i;

	if (json_object_is_type(o, json_type_array)) {
		for (i = 0; i < json_object_array_length(o); i++)
			mm_netcfg_hide_pin(json_object_array_get_idx(o, i));
		return;
	}
	if (!json_object_is_type(o, json_type_object))
		return;
	{
		const char *opt = str(o, "option"), *path = str(o, "path");
		size_t pl = path ? strlen(path) : 0;
		bool about = (opt && pin_key(opt)) || (path && (ends_with(path, pl, ".pincode") || ends_with(path, pl, ".password")));

		for (i = 0; about && vals[i]; i++)
			if (get(o, vals[i]))
				json_object_object_add(o, vals[i], json_object_new_string("(hidden)"));
	}
	json_object_object_foreach(o, k, v) {
		if (pin_key(k) && v && !json_object_is_type(v, json_type_boolean))
			json_object_object_add(o, k, json_object_new_string("(hidden)"));
		else
			mm_netcfg_hide_pin(v);
	}
}

static json_object *desired_for(json_object *rec, json_object *current, json_object *ledgers, json_object *env)
{
	const char *id = str(rec, "device_id"), *control = str(get(rec, "owner"), "control"), *iface, *zone, *proto;
	json_object *network = get(current, "network"), *decision = mm_netcfg_decision(current, id);
	json_object *out, *desired, *sections, *sec, *fields, *items;
	char name[32], key[64];
	const char *own = ledger_interface(ledgers, id), *user, *owner, *backend = str(env, "modemmanager"), *adopt = NULL;
	const char *choice = str(decision, "backend") && *str(decision, "backend") ? str(decision, "backend") : "auto";
	const char *qsec = mm_netcfg_qmodem_section(current, rec);
	json_object *ctl = get(get(rec, "port_roles"), "control");
	bool mm_running = eq(backend, "available"), native_ok, preferred = false, fallback = false;

	copy_fresh = !own;
	if (!id)
		return result("none", "record without device_id");
	if (eq(str(decision, "managed"), "0"))
		return result("none", "the operator disabled configuration for this modem (managed '0')");
	if (!get(rec, "present") || !json_object_get_boolean(get(rec, "present")))
		return result("none", "the modem is not attached");
	if (strcmp(choice, "auto") && strcmp(choice, "modemmanager") && strcmp(choice, "native") && strcmp(choice, "qmodem"))
		return result("none", "option backend must be auto, modemmanager, native or qmodem");
	/* auto: a profile that prefers QModem (FM350, Quectel) hands the modem to it where it is installed */
	if (!strcmp(choice, "auto") && eq(str(get(rec, "profile"), "prefer_owner"), "qmodem") &&
	    json_object_get_boolean(get(env, "qmodem")) && str(rec, "attachment_id") &&
	    !strncmp(str(rec, "attachment_id"), "usb:", 4)) {
		choice = "qmodem";
		preferred = true;
	}
	/* QModem chosen and then removed (R10-04, 2026-11-19): the modem was left with no owner at all; it is managed as
	 * under auto until QModem is back, the choice itself stays the operator's */
	if (!strcmp(choice, "qmodem") && !json_object_get_boolean(get(env, "qmodem")))
		choice = "auto";
	if (!strcmp(choice, "qmodem")) {
		char name[64];

		mm_netcfg_qmodem_name(rec, name, sizeof(name));
		if (!*name)
			return result("none", "backend qmodem: only USB modems are handed to QModem here");
		if (qsec)
			snprintf(name, sizeof(name), "%s", qsec);
		desired = json_object_new_object();
		{
			/*
			 * The interface's name, one standard with ours (the owner, 2026-10-16): QModem names it after the
			 * USB path (3-1.3 -> 3_1_3, 3_1_3v6), which a board renumbering its buses at boot changes (1_1_3,
			 * 3_1_3, 6_1_3 for one FM350, field 2026-10-15). An alias the operator gave QModem stands; else the
			 * operator's interface option, else the one this modem had, else the lowest free mm<N>. The job
			 * sets it as QModem's alias.
			 */
			const char *alias = str(get(get(current, "qmodem"), name), "alias"), *mine = str(decision, "interface");
			char fresh[16];

			if (alias && *alias && strcmp(alias, "-"))
				mine = alias;
			else if (!(mine && valid_name(mine) && strlen(mine) <= 12)) {
				mine = own && !slot_name(own) && strlen(own) <= 12 ? own : NULL;
				if (!mine) {
					allocate(network, ledgers, fresh, sizeof(fresh));
					mine = fresh;
				}
			}
			json_object_object_add(desired, "interface", json_object_new_string(mine));
		}
		json_object_object_add(desired, "backend", json_object_new_string("qmodem"));
		json_object_object_add(desired, "qmodem_section", json_object_new_string(name));
		json_object_object_add(desired, "sections", json_object_new_object());
		json_object_object_add(desired, "list_items", json_object_new_array());
		json_object_object_add(desired, "options", json_object_new_array());
		out = result("configure", preferred ? (qsec ? "the profile prefers QModem for this model; QModem already has this modem"
							    : "the profile prefers QModem for this model and QModem is installed; it gets this modem")
					    : qsec ? "the operator chose QModem; QModem already has this modem"
					       : "the operator chose QModem; it gets this modem and only this one");
		json_object_object_add(out, "interface", json_object_get(get(desired, "interface")));
		json_object_object_add(out, "proto", json_object_new_string("qmodem"));
		json_object_object_add(out, "owner", json_object_new_string("qmodem"));
		json_object_object_add(out, "backend", json_object_new_string("qmodem"));
		json_object_object_add(out, "qmodem_section", json_object_new_string(name));
		json_object_object_add(out, "inhibit_mm", json_object_new_boolean(mm_running));
		json_object_object_add(out, "desired", desired);
		return out;
	}
	if (qsec && !strcmp(choice, "auto")) {
		out = result("none", "QModem manages this modem; choose `option backend` to change that");
		json_object_object_add(out, "qmodem_section", json_object_new_string(qsec));
		return out;
	}
	if (qsec)
		return result("none", "QModem still has a section for this modem; release it from QModem first");
	/*
	 * auto: a profile whose next owner is the engine's own path takes it where QModem is not installed (FM350:
	 * ModemManager has no plugin that dials it, it took the modem and no session came; the vendor's own tool
	 * brings it up with a netifd AT proto). The same path as the operator's «native»: ModemManager lets go of it.
	 */
	/* ... and one whose first owner is the engine (FM350 since 2026-10-21: QModem guessed its gateway as x.y.z.1, the
	 * modem did not answer for it and the RNDIS queue hung; the engine's on-link route held) takes it anyway */
	if (!strcmp(choice, "auto") && (eq(str(get(rec, "profile"), "fallback_owner"), "native") ||
	    eq(str(get(rec, "profile"), "prefer_owner"), "native")) && get(get(rec, "profile"), "dial")) {
		choice = "native";
		fallback = true;
	}
	if (!strcmp(choice, "modemmanager") && !eq(control, "modemmanager"))
		return result("none", mm_running ? "backend modemmanager: ModemManager has not taken this modem (yet)"
						 : "backend modemmanager: ModemManager is not running");
	if (!own && (user = user_interface(rec, network, ledgers))) {
		/* the decision's adopt, or a hand-over asked for now (env "adopt": mgr_foreign.c) */
		if (!eq(str(decision, "adopt"), "1") && !eq(str(env, "adopt"), user)) {
			out = result("none", "an existing network interface already serves this modem; it is left as is "
					     "(option adopt '1' hands it to MedveModem)");
			json_object_object_add(out, "user_interface", json_object_new_string(user));
			return out;
		}
		adopt = user;	/* the operator handed it over: same name, the engine's fields */
	}

	fields = json_object_new_object();
	/* explicit native takes the modem from a running ModemManager (inhibited) */
	/* auto: native only without ModemManager; an installed one that does not answer yet (boot, restart) is
	 * waited for, else the engine's native interface and ModemManager fight over the control port (field 2026-10-13) */
	native_ok = eq(backend, "disabled") || (eq(backend, "unavailable") && !json_object_get_boolean(get(env, "mm_installed"))) ||
		    !strcmp(choice, "native");
	if (eq(control, "modemmanager") && strcmp(choice, "native")) {
		const char *dev = str(get(rec, "modemmanager"), "lookup") ? str(get(rec, "modemmanager"), "lookup") :
				  str(get(rec, "modemmanager"), "device");

		if (!dev) {
			json_object_put(fields);
			return result("none", "ModemManager owns the modem but reported no device path yet");
		}
		proto = "modemmanager";
		owner = "modemmanager";
		json_object_object_add(fields, "proto", json_object_new_string(proto));
		json_object_object_add(fields, "device", json_object_new_string(dev));
		/*
		 * OpenWrt's proto modemmanager disables the modem on ifdown unless
		 * disable_modem is 0: a modem deactivated on its card (or kept down by
		 * netifd) then took no SMS and no USSD (bench 2026-10-16, the T77).
		 * Deactivated means no data through it; the radio stays registered.
		 */
		json_object_object_add(fields, "disable_modem", json_object_new_string("0"));
		copy_options(fields, decision, false, true, rec);
	} else if (ctl && str(ctl, "node") && (mm_is_qmi_driver(str(ctl, "driver")) || eq(str(ctl, "driver"), "cdc_mbim")) &&
		   native_ok) {
		/* native owner only where ModemManager cannot be a second one */
		bool qmi = mm_is_qmi_driver(str(ctl, "driver"));

		proto = qmi ? "qmi" : "mbim";
		owner = qmi ? "netifd-qmi" : "netifd-mbim";
		if (!has_proto(env, proto)) {
			json_object_put(fields);
			return result("none", qmi ? "native QMI needs the uqmi package (netifd proto qmi)"
						  : "native MBIM needs the umbim package (netifd proto mbim)");
		}
		json_object_object_add(fields, "proto", json_object_new_string(proto));
		json_object_object_add(fields, "device", json_object_new_string(str(ctl, "node")));
		copy_options(fields, decision, true, true, rec);
	} else if (get(get(rec, "profile"), "dial") && native_ok && str(get(get(rec, "port_roles"), "at"), "node") &&
		   dial_netdev(rec) &&
		   /* a recipe never run on a modem of ours (XMM): only on the operator's explicit «native» */
		   (!eq(str(get(get(rec, "profile"), "dial"), "status"), "unverified") || !strcmp(choice, "native"))) {
		/* native AT dial (spec 19.4): netifd proto medvemodem asks the engine to dial */
		proto = "medvemodem";
		owner = "medvemodem-at";
		if (!has_proto(env, proto)) {
			json_object_put(fields);
			return result("none", "native AT dial needs the netifd proto handler of the medvemodem package");
		}
		json_object_object_add(fields, "proto", json_object_new_string(proto));
		json_object_object_add(fields, "device", json_object_new_string(dial_netdev(rec)));
		json_object_object_add(fields, "device_id", json_object_new_string(id));
		copy_options(fields, decision, true, false, rec);
	} else if (eq(control, "hilink")) {
		const char *net = first(get(rec, "ports"), "net");

		if (!net) {
			json_object_put(fields);
			return result("none", "router-mode modem without a network device");
		}
		proto = "dhcp";
		owner = "hilink";
		json_object_object_add(fields, "proto", json_object_new_string(proto));
		json_object_object_add(fields, "device", json_object_new_string(net));
		if (str(decision, "metric") && *str(decision, "metric"))
			json_object_object_add(fields, "metric", json_object_new_string(str(decision, "metric")));
		manual_dns(fields, decision);
	} else {
		json_object_put(fields);
		/* said apart (an FM350 tester, r135: one line for three causes sent them to QModem): a recipe without the
		 * network device its data goes through is a driver missing, not a recipe missing */
		if (!strcmp(choice, "native") && get(get(rec, "profile"), "dial") && !dial_netdev(rec))
			return result("none", "backend native: the modem has no network device: its network interface has no "
					      "driver (see the missing drivers on the modem's page)");
		if (!strcmp(choice, "native") && get(get(rec, "profile"), "dial") && !str(get(get(rec, "port_roles"), "at"), "node"))
			return result("none", "backend native: the modem's AT port is not known yet");
		if (!strcmp(choice, "native"))
			return result("none", "backend native: no QMI/MBIM control port and no AT dial recipe for this "
					      "modem (or its AT port is not known yet)");
		if (eq(backend, "available"))
			return result("none", "ModemManager is running but has not taken this modem (yet); "
					      "a native owner beside it would be a second owner");
		if (!eq(backend, "disabled") && !eq(backend, "unavailable"))
			return result("none", "ModemManager state is not known yet; waiting before choosing an owner");
		return result("none", "no supported session path: no ModemManager, no QMI/MBIM control port, not router mode");
	}

	iface = str(decision, "interface");
	if (iface && !valid_name(iface)) {
		json_object_put(fields);
		return result("none", "the operator's interface name is not a valid UCI name of at most 15 characters");
	}
	/* a name that is another section of the network config (lan, wan, a tunnel) is never taken over */
	if (iface && !eq(iface, own) && !modem_section(network, ledgers, id, iface)) {
		json_object_put(fields);
		return result("none", "the operator's interface name belongs to a network section that is not this modem's");
	}
	if (adopt)
		iface = adopt;
	if (!iface)
		iface = own;
	if (!iface) {
		allocate(network, ledgers, name, sizeof(name));
		iface = name;
	}
	zone = str(decision, "zone") ? str(decision, "zone") : "wan";
	/* an interface the engine makes by itself (auto-connect) stays behind the router's other uplinks until the
	 * operator orders the modems: a backup metric (review 2026-10-17: it took the default route from a wired
	 * WAN, metered SIMs carried traffic) */
	/* ... and so does one brought up by the operator's button, and our own interface made without a metric, where the
	 * router has a wired uplink: at metric 0 the modem's default route replaced the wired WAN's (the owner's R3S,
	 * 2026-10-25: «Проверка каналов» through eth0 found no route; mm0 had no metric) */
	bool wired_up = json_object_get_boolean(get(env, "wired_up"));	/* a wired uplink up now (netifd) */
	/* an interface the operator handed over keeps the metric it has (adopt) */
	if (!get(fields, "metric") && !(adopt && str(get(network, adopt), "metric")) &&
	    (!own || (eq(iface, own) && !str(get(network, iface), "metric") && wired_up))) {
		/* a new modem goes behind the modems already ordered (bench 2026-10-19: a 4G stick plugged in later came up
		 * with metric 0 and took the default route from the T99 the operator had made the main one) */
		long top = -1;

		json_object_object_foreach(ledgers, lid, led) {
			const char *li = str(led, "interface"), *m = li ? str(get(network, li), "metric") : NULL;

			if (strcmp(lid, id) && m && *m && atol(m) > top)
				top = atol(m);
		}
		/* and behind every wired uplink: a WAN the operator set to 100 tied with the fixed backup 100 and lost the
		 * default route to a new modem (the owner's R3S, 2026-10-30) */
		long wired = get(env, "wired_metric") ? json_object_get_int(get(env, "wired_metric")) : -1;
		char m[24];

		if (top >= 0) {
			snprintf(m, sizeof(m), "%ld", top + 10 > wired + 10 ? top + 10 : wired + 10);
			json_object_object_add(fields, "metric", json_object_new_string(m));
		} else if (json_object_get_boolean(get(env, "auto_connect")) || wired_up) {
			snprintf(m, sizeof(m), "%ld", wired + 10 > 100 ? wired + 10 : 100);
			json_object_object_add(fields, "metric", json_object_new_string(m));
		}
	}
	/* a metric already on our interface (the backup one above, or the modems' priority) stays where the
	 * operator typed none: a later transaction never hands the default route back by dropping it */
	else if (own && eq(iface, own) && !get(fields, "metric") && str(get(network, iface), "metric")) {
		/* ... unless it ties a wired uplink's: two default routes of one metric, the kernel keeps one (the owner's
		 * R3S, 2026-10-30: mm1 at 100 beside a WAN at 100 took the router's traffic); the modems' priority never
		 * picks a wired metric (medvemodemd.c wired_metric), so a tie is the old fixed backup 100 */
		long have = atol(str(get(network, iface), "metric")), wired = get(env, "wired_metric") ?
			json_object_get_int(get(env, "wired_metric")) : -1;
		char m[24];

		if (wired >= 0 && have == wired) {
			snprintf(m, sizeof(m), "%ld", wired + 10);
			json_object_object_add(fields, "metric", json_object_new_string(m));
		} else
			json_object_object_add(fields, "metric", json_object_new_string(str(get(network, iface), "metric")));
	}
	/* with apn_auto unset, the APN (and its login) our interface was made with stays: taken from the SIM at
	 * the creation, never dropped by a later transaction, never replaced by a newer suggestion */
	if (own && eq(iface, own) && !str(decision, "apn_auto") && !(str(decision, "apn") && *str(decision, "apn")) &&
	    !get(fields, "apn") && str(get(network, iface), "apn")) {
		static const char *const keep[] = { "apn", "auth", "username", "password", NULL };
		int k;

		for (k = 0; keep[k]; k++)
			if (str(get(network, iface), keep[k]) && *str(get(network, iface), keep[k]))
				json_object_object_add(fields, keep[k], json_object_new_string(str(get(network, iface), keep[k])));
	}

	desired = json_object_new_object();
	sections = json_object_new_object();
	sec = json_object_new_object();
	json_object_object_add(sec, "type", json_object_new_string("interface"));
	json_object_object_add(sec, "fields", fields);
	/* the stale device the repair found (env stale_device): the planner rewrites it even when the ledger has another */
	if (str(env, "stale_device") && get(fields, "device") && eq(str(get(network, iface), "device"), str(env, "stale_device"))) {
		json_object *t = json_object_new_object();

		json_object_object_add(t, "device", json_object_new_string(str(env, "stale_device")));
		json_object_object_add(sec, "takes", t);
	}
	snprintf(key, sizeof(key), "network.%s", iface);
	json_object_object_add(sections, key, sec);
	json_object_object_add(desired, "interface", json_object_new_string(iface));
	json_object_object_add(desired, "sections", sections);
	items = json_object_new_array();
	if (*zone) {
		json_object *it = json_object_new_object();
		json_object_object_add(it, "package", json_object_new_string("firewall"));
		json_object_object_add(it, "type", json_object_new_string("zone"));
		json_object_object_add(it, "name", json_object_new_string(zone));
		json_object_object_add(it, "option", json_object_new_string("network"));
		json_object_object_add(it, "value", json_object_new_string(iface));
		json_object_array_add(items, it);
	}
	json_object_object_add(desired, "list_items", items);
	json_object_object_add(desired, "backend", json_object_new_string(
		eq(proto, "modemmanager") ? "modemmanager" : eq(proto, "dhcp") ? "device" : "native"));

	out = result("configure", eq(proto, "modemmanager") ? "ModemManager owns the modem: netifd proto modemmanager"
				  : eq(proto, "dhcp") ? "router-mode modem: DHCP on its network device"
				  : fallback ? "QModem is not installed: the profile's next owner, the engine dials over AT "
					     "(netifd proto medvemodem)"
				  : eq(proto, "medvemodem") ? "native: the engine dials over AT, netifd proto medvemodem"
				  : "no ModemManager on this router: netifd drives the control port natively");
	json_object_object_add(out, "interface", json_object_new_string(iface));
	json_object_object_add(out, "proto", json_object_new_string(proto));
	json_object_object_add(out, "owner", json_object_new_string(owner));
	json_object_object_add(out, "backend", json_object_get(get(desired, "backend")));
	if (fallback)
		json_object_object_add(out, "fallback", json_object_new_boolean(1));
	json_object_object_add(out, "inhibit_mm", json_object_new_boolean(mm_running && !eq(proto, "modemmanager") &&
									  !eq(proto, "dhcp")));
	json_object_object_add(out, "desired", desired);
	if (adopt) {
		json_object *a = json_object_new_array();
		json_object_array_add(a, json_object_new_string(key));
		json_object_object_add(out, "adopt", a);
	}
	return out;
}

/* one path component without its bus number: "usb2" -> "usb", "2-1.4" -> "1.4" */
static size_t port_part(const char *c, size_t n, char *out, size_t size)
{
	size_t i = 0;

	if (n > 3 && !strncmp(c, "usb", 3) && strspn(c + 3, "0123456789") == n - 3) {
		snprintf(out, size, "usb");
		return 3;
	}
	while (i < n && c[i] >= '0' && c[i] <= '9')
		i++;
	if (i && i < n && c[i] == '-' && strspn(c + i + 1, "0123456789.") == n - i - 1) {
		snprintf(out, size, "%.*s", (int)(n - i - 1), c + i + 1);
		return n - i - 1;
	}
	snprintf(out, size, "%.*s", (int)n, c);
	return n;
}

static void port_key(const char *p, char *out, size_t size)
{
	size_t used = 0;

	out[0] = '\0';
	while (*p && used + 1 < size) {
		size_t n = strcspn(p, "/");
		char part[128];

		if (n) {
			port_part(p, n < sizeof(part) ? n : sizeof(part) - 1, part, sizeof(part));
			used += (size_t)snprintf(out + used, size - used, "/%s", part);
		}
		p += n;
		if (*p == '/')
			p++;
	}
}

bool mm_netcfg_same_usb_port(const char *a, const char *b)
{
	char ka[512], kb[512];

	if (!a || !b || !strcmp(a, b) || strncmp(a, "/sys/devices/", 13) || strncmp(b, "/sys/devices/", 13))
		return false;
	port_key(a, ka, sizeof(ka));
	port_key(b, kb, sizeof(kb));
	return !strcmp(ka, kb);
}

bool mm_netcfg_usb_ancestor(const char *a, const char *b)
{
	char ka[512], kb[512];
	size_t n;

	if (!a || !b || strncmp(a, "/sys/devices/", 13) || strncmp(b, "/sys/devices/", 13))
		return false;
	port_key(a, ka, sizeof(ka));
	port_key(b, kb, sizeof(kb));
	n = strlen(ka);
	/* a USB device below the hub: «…/usbN/N-1» before «…/usbN/N-1/N-1.3», never the controller itself */
	return n && strstr(ka, "/usb") && strncmp(kb, ka, n) == 0 && kb[n] == '/' && strchr(a + 13, '-');
}

/* how many of the present modems' paths sit behind a */
static int behind(const char *a, json_object *paths)
{
	int n = 0;

	for (size_t i = 0; paths && i < json_object_array_length(paths); i++)
		if (mm_netcfg_usb_ancestor(a, json_object_get_string(json_object_array_get_idx(paths, i))))
			n++;
	return n;
}

/* the zone that lists this interface in its "network" (list or space-separated string) */
static const char *zone_of(json_object *firewall, const char *iface)
{
	json_object_object_foreach(firewall, sec, z) {
		json_object *nets = get(z, "network");
		size_t i;

		(void)sec;
		if (!eq(str(z, ".type"), "zone") || !nets)
			continue;
		if (json_object_is_type(nets, json_type_array)) {
			for (i = 0; i < json_object_array_length(nets); i++)
				if (eq(json_object_get_string(json_object_array_get_idx(nets, i)), iface))
					return str(z, "name");
		} else {
			const char *t = json_object_get_string(nets);
			size_t n = strlen(iface);

			while (t && *t) {
				size_t w = strcspn(t, " ");
				if (w == n && !strncmp(t, iface, n))
					return str(z, "name");
				t += w;
				t += strspn(t, " ");
			}
		}
	}
	return NULL;
}

/* the network device the engine's own AT dial uses (dial_netdev), for the device view's checks */
const char *mm_netcfg_dial_netdev(json_object *rec)
{
	return rec ? dial_netdev(rec) : NULL;
}

/*
 * An interface naming a device the kernel numbers in the order things come up: a router-mode stick's eth/usb netdev,
 * the engine's own AT dial's netdev, a direct QMI/MBIM control node (/dev/cdc-wdmN), a PPP tty. A tester's OPiZ3,
 * 2026-11-09: after a reboot the FM350's RNDIS came up as eth2, its mm3 (proto medvemodem) still named eth1, now another
 * stick's, and it dialled nothing all weekend. The owner: «не только на fm350 случай». A sysfs path (proto
 * modemmanager) has its own repair (mm_netcfg_repair).
 */
static bool netdev_named(json_object *s)
{
	const char *d = str(s, "device");

	return d && *d && strncmp(d, "/sys/", 5) && !eq(str(s, "proto"), "modemmanager");
}

/* a ModemManager interface naming another USB port than the modem's (a tester's OPiZ3, 2026-11-10: the MF192, known by
 * its serial, moved from hub port 2 to port 4 and the M150-4 took port 2; mm8 kept the old port's path, ModemManager
 * found no modem there for 129 tries, and the M150-4 read mm8 as its own). The same port under another bus number is
 * the path repair's (mm_netcfg_repair) */
static bool mm_path_moved(json_object *s, const char *want)
{
	const char *d = str(s, "device");

	return d && want && eq(str(s, "proto"), "modemmanager") && !strncmp(d, "/sys/", 5) && !strncmp(want, "/sys/", 5) &&
	       !eq(d, want) && !mm_netcfg_same_usb_port(d, want);
}

json_object *mm_netcfg_netdev_check(json_object *current, json_object *ledgers, const char *device_id, const char *net,
				    const char *want)
{
	json_object *network = get(current, "network"), *o = json_object_new_object(), *sq = json_object_new_array();
	const char *mine = ledger_interface(ledgers, device_id);
	json_object *s = mine ? get(network, mine) : NULL;

	/* want: the device the planner would write for this modem now (its netdev, its control node, its tty). Without one
	 * (the operator's managed '0', a modem handed to QModem: the planner writes nothing) only a router-mode DHCP
	 * interface is compared with the netdev, as before r175: a repair must not run a transaction that has nothing to
	 * write (regression review 2026-11-11) */
	bool planned = want != NULL;

	if (!want)
		want = net;
	net = want;
	json_object_object_add(o, "stale", s && want && ((planned ? netdev_named(s) : eq(str(s, "proto"), "dhcp") && str(s, "device") != NULL) ?
					   !eq(str(s, "device"), want) : planned && mm_path_moved(s, want)) ? json_object_new_string(mine) : NULL);
	if (net && ledgers && json_object_is_type(ledgers, json_type_object)) {
		json_object_object_foreach(ledgers, id, led) {
			const char *name = str(led, "interface");
			json_object *other = name ? get(network, name) : NULL;

			if (!eq(id, device_id) && other && (planned ? netdev_named(other) : eq(str(other, "proto"), "dhcp")) &&
			    eq(str(other, "device"), net))
				json_object_array_add(sq, json_object_new_string(id));
		}
	}
	json_object_object_add(o, "squatters", sq);
	return o;
}

#define MM_OBJECT "/org/freedesktop/ModemManager1/Modem/"

static bool listed(json_object *list, const char *s)
{
	size_t i;

	for (i = 0; list && i < json_object_array_length(list); i++)
		if (eq(json_object_get_string(json_object_array_get_idx(list, i)), s))
			return true;
	return false;
}

/* path: where netifd must point (the sysfs path, or ModemManager's object for a modem it gave no physdev); alias:
 * the modem's sysfs path when path is the object, else NULL */
json_object *mm_netcfg_repair(json_object *current, json_object *ledgers, const char *path,
			      bool (*exists)(const char *path), json_object *paths, const char *alias)
{
	json_object *network = get(current, "network"), *firewall = get(current, "firewall");
	json_object *out = json_object_new_array();
	bool wan = mm_cfg_find_named(current, "firewall", "zone", "wan") != NULL;

	if (!path)
		return out;
	json_object_object_foreach(network, name, s) {
		const char *dev = str(s, "device"), *zone;
		json_object *it, *findings, *ops;
		bool stale, hub, engine;

		if (!eq(str(s, ".type"), "interface") || !eq(str(s, "proto"), "modemmanager") || !dev)
			continue;
		stale = mm_netcfg_same_usb_port(dev, path) && !(exists && exists(dev));
		/* a modem ModemManager gave no physdev: its sysfs path (or that of last boot's bus) names nothing for the
		 * proto, and an object path of an earlier ModemManager run names no modem now */
		if (alias && !eq(dev, path))
			stale = eq(dev, alias) || mm_netcfg_same_usb_port(dev, alias) ||
				(!strncmp(dev, MM_OBJECT, strlen(MM_OBJECT)) && !listed(paths, dev));
		/* the hub the modem sits behind: ModemManager never finds a modem there; moved only when this modem is the
		 * one modem behind it */
		hub = !stale && paths && mm_netcfg_usb_ancestor(dev, path) && behind(dev, paths) == 1;
		if (!eq(dev, path) && !stale && !hub)
			continue;
		engine = managed_by_anyone(ledgers, name);
		zone = zone_of(firewall, name);
		it = json_object_new_object();
		findings = json_object_new_array();
		ops = json_object_new_array();
		json_object_object_add(it, "name", json_object_new_string(name));
		json_object_object_add(it, "engine", json_object_new_boolean(engine));
		if (stale || hub)
			json_object_object_add(it, "stale_device", json_object_new_string(dev));
		json_object_object_add(it, "zone", zone ? json_object_new_string(zone) : NULL);
		if (str(s, "apn"))
			json_object_object_add(it, "apn", json_object_new_string(str(s, "apn")));
		if (str(s, "iptype"))
			json_object_object_add(it, "iptype", json_object_new_string(str(s, "iptype")));
		if (stale || hub) {
			json_object_array_add(findings, json_object_new_string(hub ? "hub-device" : "stale-device"));
			if (!engine) {
				json_object *op = json_object_new_object();
				json_object_object_add(op, "op", json_object_new_string("set"));
				json_object_object_add(op, "package", json_object_new_string("network"));
				json_object_object_add(op, "section", json_object_new_string(name));
				json_object_object_add(op, "option", json_object_new_string("device"));
				json_object_object_add(op, "value", json_object_new_string(path));
				json_object_object_add(op, "expect", json_object_new_string(dev));
				json_object_array_add(ops, op);
			}
		}
		if (!zone) {
			json_object_array_add(findings, json_object_new_string("no-zone"));
			if (!engine && wan) {
				json_object *op = json_object_new_object(), *m = json_object_new_object();
				json_object_object_add(op, "op", json_object_new_string("add-list"));
				json_object_object_add(op, "package", json_object_new_string("firewall"));
				json_object_object_add(op, "option", json_object_new_string("network"));
				json_object_object_add(op, "value", json_object_new_string(name));
				json_object_object_add(m, "type", json_object_new_string("zone"));
				json_object_object_add(m, "name", json_object_new_string("wan"));
				json_object_object_add(op, "match", m);
				json_object_array_add(ops, op);
			}
		} else if (!eq(zone, "wan")) {
			char f[96];
			snprintf(f, sizeof(f), "zone-not-wan:%s", zone);
			json_object_array_add(findings, json_object_new_string(f));
		}
		json_object_object_add(it, "findings", findings);
		json_object_object_add(it, "ops", ops);
		json_object_array_add(out, it);
	}
	return out;
}

/*
 * The router's own uplinks beside the modems (a wired WAN, PPPoE, a Wi-Fi client): what «Приоритет» may put
 * among the modems (field request 2026-10-23: the wire first, the modems as its backup). An interface that
 * reaches the internet by itself: dhcp, pppoe and the other tunnels to a provider, static with a gateway.
 * Not a modem's (skip: the modems' interfaces, ours or the operator's), not QModem's, not a session protocol
 * of a modem. Its IPv6 twins (proto dhcpv6 on '@name' or on the same device) go with it.
 */
static bool wired_proto(const char *proto, json_object *sec)
{
	static const char *const uplinks[] = { "dhcp", "pppoe", "pppoa", "l2tp", "pptp", NULL };
	int i;

	for (i = 0; proto && uplinks[i]; i++)
		if (!strcmp(proto, uplinks[i]))
			return true;
	return eq(proto, "static") && str(sec, "gateway") && *str(sec, "gateway");
}


json_object *mm_netcfg_wired(json_object *network, json_object *skip)
{
	json_object *out = json_object_new_array();

	if (!network)
		return out;
	json_object_object_foreach(network, name, sec) {
		const char *dev = str(sec, "device") ? str(sec, "device") : str(sec, "ifname");
		json_object *w, *twins;

		if (!eq(str(sec, ".type"), "interface") || !strcmp(name, "loopback") || listed(skip, name) ||
		    !wired_proto(str(sec, "proto"), sec) || str(sec, "modem_config") || str(sec, "device_id") || eq(str(sec, "disabled"), "1"))
			continue;
		w = json_object_new_object();
		twins = json_object_new_array();
		json_object_object_add(w, "interface", json_object_new_string(name));
		json_object_object_add(w, "proto", json_object_new_string(str(sec, "proto")));
		json_object_object_add(w, "metric", json_object_new_int(str(sec, "metric") ? atoi(str(sec, "metric")) : 0));
		{
			json_object_object_foreach(network, n2, s2) {
				const char *d2 = str(s2, "device") ? str(s2, "device") : str(s2, "ifname");

				if (strcmp(n2, name) && eq(str(s2, ".type"), "interface") && eq(str(s2, "proto"), "dhcpv6") && d2 &&
				    ((d2[0] == '@' && !strcmp(d2 + 1, name)) || (dev && !strcmp(d2, dev))))
					json_object_array_add(twins, json_object_new_string(n2));
			}
		}
		json_object_object_add(w, "twins", twins);
		json_object_array_add(out, w);
	}
	return out;
}

json_object *mm_netcfg_desired(json_object *rec, json_object *current, json_object *ledgers, json_object *env)
{
	json_object *out = desired_for(rec, current, ledgers, env), *decision = mm_netcfg_decision(current, str(rec, "device_id"));
	char why[300];

	/* QModem chosen and removed since (R10-04): said in the reason, managed as under auto */
	if (eq(str(decision, "backend"), "qmodem") && !json_object_get_boolean(get(env, "qmodem")) && out) {
		snprintf(why, sizeof(why), "QModem is not installed (the operator chose it): managed as under auto; %s",
			 str(out, "reason") ? str(out, "reason") : "");
		json_object_object_add(out, "reason", json_object_new_string(why));
	}
	return out;	/* decision is borrowed from current */
}
