/*
 * medvemodem: CLI client of medvemodemd.
 *
 * With the daemon running, commands read its registry over ubus. Without it,
 * the CLI runs one read-only discovery pass through the same registry code
 * and says so on stderr (spec section 4.1). Nothing here changes a device.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <json-c/json.h>

#include "diag.h"
#include "identity.h"
#include "discovery.h"
#include "mm_paths.h"
#include "registry.h"
#include "roles.h"
#include "top.h"
#include "evtext.h"
#include "version.h"
#include "dnscheck.h"
#include "websig.h"

enum { CALL_NO_DAEMON = -1, CALL_NOT_FOUND = 0, CALL_OK = 1 };

#ifdef WITH_UBUS
#include <libubox/blobmsg_json.h>
#include <libubus.h>

static void call_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
	json_object **out = req->priv;
	char *text;

	(void)type;
	if (!msg || *out)
		return;
	text = blobmsg_format_json(msg, true);
	if (text) {
		*out = json_tokener_parse(text);
		free(text);
	}
}

static int daemon_call_args(const char *method, json_object *args, json_object **out, int timeout_ms)
{
	struct ubus_context *ctx;
	struct blob_buf b = { 0 };
	uint32_t id;
	int rc;

	*out = NULL;
	ctx = ubus_connect(getenv("MEDVEMODEM_UBUS_SOCKET"));
	if (!ctx)
		return CALL_NO_DAEMON;
	if (ubus_lookup_id(ctx, "medvemodem", &id)) {
		ubus_free(ctx);
		return CALL_NO_DAEMON;
	}
	blob_buf_init(&b, 0);
	if (args)
		blobmsg_add_object(&b, args);
	rc = ubus_invoke(ctx, id, method, b.head, call_cb, out, timeout_ms);
	blob_buf_free(&b);
	ubus_free(ctx);
	if (rc == UBUS_STATUS_NOT_FOUND)
		return CALL_NOT_FOUND;
	if (rc == UBUS_STATUS_METHOD_NOT_FOUND)
		fprintf(stderr, "medvemodem: the running engine has no '%s'; it is older than this command "
			"(after an upgrade: /etc/init.d/medvemodemd restart)\n", method);
	if (rc || !*out) {
		json_object_put(*out);
		*out = NULL;
		return CALL_NO_DAEMON;
	}
	return CALL_OK;
}
#else
static int daemon_call_args(const char *method, json_object *args, json_object **out, int timeout_ms)
{
	(void)method; (void)args; (void)timeout_ms;
	*out = NULL;
	return CALL_NO_DAEMON;
}
#endif

static int daemon_call(const char *method, const char *key, const char *value, json_object **out)
{
	json_object *args = json_object_new_object();
	int rc;

	if (key)
		json_object_object_add(args, key, json_object_new_string(value));
	rc = daemon_call_args(method, args, out, 6000);	/* the engine may first read ModemManager (<= 2 s) */
	json_object_put(args);
	return rc;
}

/* MEDVEMODEM_NO_DAEMON=1 forces the one-shot path (tests, debugging). */
static bool no_daemon_forced(void)
{
	const char *off = getenv("MEDVEMODEM_NO_DAEMON");

	return off && *off && strcmp(off, "0");
}

static int ask_daemon(const char *method, const char *key, const char *value, json_object **out)
{
	if (no_daemon_forced()) {
		*out = NULL;
		return CALL_NO_DAEMON;
	}
	return daemon_call(method, key, value, out);
}

static void one_shot(struct mm_registry *reg)
{
	json_object *scan = mm_discover_all();

#ifdef WITH_UBUS
	fprintf(stderr, "medvemodem: daemon not running; one-shot read-only scan\n");
#endif
	mm_registry_init(reg);
	json_object_put(mm_registry_apply(reg, scan, (long long)time(NULL)));
	json_object_put(scan);
}

static void print(json_object *o)
{
	puts(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY));
}

static int cmd_list(void)
{
	struct mm_registry reg;
	json_object *res, *arr = NULL;

	if (ask_daemon("devices", NULL, NULL, &res) == CALL_OK &&
	    json_object_object_get_ex(res, "devices", &arr)) {
		print(arr);
		json_object_put(res);
		return 0;
	}
	json_object_put(res);
	one_shot(&reg);
	arr = mm_registry_list(&reg, false);
	print(arr);
	json_object_put(arr);
	mm_registry_free(&reg);
	return 0;
}

/* show and lookup: the daemon's answer, else a one-shot registry's. */
static int cmd_find(const char *method, const char *key, const char *arg, const char *missing,
		    json_object *(*find)(const struct mm_registry *, const char *))
{
	struct mm_registry reg;
	json_object *res, *rec;
	int rc = ask_daemon(method, key, arg, &res);

	if (rc == CALL_OK) {
		print(res);
		json_object_put(res);
		return 0;
	}
	if (rc == CALL_NOT_FOUND) {
		fprintf(stderr, "%s: %s\n", missing, arg);
		return 2;
	}
	one_shot(&reg);
	rec = find(&reg, arg);
	if (rec)
		print(rec);
	else
		fprintf(stderr, "%s: %s\n", missing, arg);
	mm_registry_free(&reg);
	return rec ? 0 : 2;
}

/* status, rescan and events only make sense against a running daemon. */
static int cmd_daemon(const char *method, const char *key, const char *arg)
{
	json_object *res, *list = NULL;
	int rc = ask_daemon(method, key, arg, &res);

	if (rc == CALL_NOT_FOUND) {
		fprintf(stderr, "device not found: %s\n", arg);
		return 2;
	}
	if (rc != CALL_OK) {
		fprintf(stderr, "medvemodem: daemon not running\n");
		return 3;
	}
	/* events: print the list itself, newest first */
	print(key && json_object_object_get_ex(res, "events", &list) ? list : res);
	json_object_put(res);
	return 0;
}

/*
 * events: the journal. On a terminal (or with --text) one plain English line per event, newest first (the owner,
 * 2026-10-17: the console in English, readable); piped (or with --json) the engine's JSON, as scripts read it.
 */
static int cmd_events(const char *id, bool text)
{
	json_object *res, *list = NULL;
	char line[320], when[32];
	size_t i;

	if (!text)
		return cmd_daemon("events", "id", id);
	switch (ask_daemon("events", "id", id, &res)) {
	case CALL_OK:
		break;
	case CALL_NOT_FOUND:
		fprintf(stderr, "device not found: %s\n", id);
		return 2;
	default:
		fprintf(stderr, "medvemodem: daemon not running\n");
		return 3;
	}
	if (json_object_object_get_ex(res, "events", &list) && json_object_is_type(list, json_type_array)) {
		for (i = 0; i < json_object_array_length(list); i++) {
			json_object *e = json_object_array_get_idx(list, i), *v;
			time_t t = json_object_object_get_ex(e, "t", &v) ? (time_t)json_object_get_int64(v) : 0;
			struct tm tm;

			mm_event_words(json_object_object_get_ex(e, "kind", &v) ? json_object_get_string(v) : NULL,
				       json_object_object_get_ex(e, "detail", &v) ? json_object_get_string(v) : NULL, line, sizeof(line));
			if (!t || !localtime_r(&t, &tm) || !strftime(when, sizeof(when), "%d.%m %H:%M:%S", &tm))
				snprintf(when, sizeof(when), "-");
			printf("%s  %s%s\n", when, line,
			       json_object_object_get_ex(e, "requested", &v) && json_object_get_boolean(v) ? "  (requested)" : "");
		}
		if (!json_object_array_length(list))
			printf("No events yet.\n");
	}
	json_object_put(res);
	return 0;
}

/* ---- diagnostics (spec 32) ---- */

static const char *env_or(const char *name, const char *def)
{
	const char *v = getenv(name);
	return v && *v ? v : def;
}

/* A one-shot record gets the roles and owner the daemon would add. */
static json_object *with_roles(json_object *rec)
{
	json_object *o = json_object_new_object(), *roles, *own = NULL;

	json_object_object_foreach(rec, key, val)
		json_object_object_add(o, key, json_object_get(val));
	roles = mm_roles_resolve(rec, NULL, "unknown");
	if (json_object_object_get_ex(roles, "owner", &own))
		json_object_object_add(o, "owner", json_object_get(own));
	json_object_object_add(o, "port_roles", roles);
	return o;
}

static json_object *system_facts(void)
{
	json_object *o = json_object_new_object();
	FILE *f = fopen(OPENWRT_RELEASE, "r");
	struct utsname u;
	char line[256];

	while (f && fgets(line, sizeof(line), f)) {
		static const char *const keys[][2] = {
			{ "DISTRIB_ID=", "distribution" }, { "DISTRIB_RELEASE=", "release" },
			{ "DISTRIB_REVISION=", "revision" }, { "DISTRIB_TARGET=", "target" }, { "DISTRIB_ARCH=", "arch" },
		};
		size_t k, n;

		for (k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
			char *v;
			if (strncmp(line, keys[k][0], strlen(keys[k][0])))
				continue;
			v = line + strlen(keys[k][0]);
			v[strcspn(v, "\n")] = '\0';
			n = strlen(v);
			if (n >= 2 && (v[0] == '\'' || v[0] == '"') && v[n - 1] == v[0]) {
				v[n - 1] = '\0';
				v++;
			}
			json_object_object_add(o, keys[k][1], json_object_new_string(v));
		}
	}
	if (f)
		fclose(f);
	if (!uname(&u)) {
		json_object_object_add(o, "kernel", json_object_new_string(u.release));
		json_object_object_add(o, "machine", json_object_new_string(u.machine));
	}
	return o;
}

/* Kernel ring buffer (or a file in tests), NULL when not readable. */
/*
 * The system log's lines of the engine, ModemManager and netifd, the newest 300 (the report button,
 * alpha): what an operator would paste by hand. MEDVEMODEM_LOGREAD names a file instead (tests).
 */
static json_object *system_log(void)
{
	const char *file = getenv("MEDVEMODEM_LOGREAD");
	json_object *all = json_object_new_array(), *out;
	char line[1024];
	size_t n, from;
	FILE *f = file ? fopen(file, "r") : popen("logread 2>/dev/null", "r");

	if (!f)
		return all;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = '\0';
		if (strstr(line, "medvemodem") || strstr(line, "ModemManager") || strstr(line, "netifd") || strstr(line, "uqmi") ||
		    strstr(line, "umbim") || strstr(line, "qmodem"))
			json_object_array_add(all, json_object_new_string(line));
	}
	if (file)
		fclose(f);
	else
		pclose(f);
	n = json_object_array_length(all);
	if (n <= 300)
		return all;
	out = json_object_new_array();
	for (from = n - 300; from < n; from++)
		json_object_array_add(out, json_object_get(json_object_array_get_idx(all, from)));
	json_object_put(all);
	return out;
}

static char *kernel_log(void)
{
	const char *file = getenv("MEDVEMODEM_KMSG_FILE");
	char *buf;
	int size, n;

	if (file) {
		FILE *f = fopen(file, "r");
		size_t got;

		if (!f)
			return NULL;
		buf = calloc(1, 1 << 20);
		got = buf ? fread(buf, 1, (1 << 20) - 1, f) : 0;
		fclose(f);
		if (buf)
			buf[got] = '\0';
		return buf;
	}
	size = klogctl(10, NULL, 0);	/* SYSLOG_ACTION_SIZE_BUFFER */
	if (size <= 0)
		return NULL;
	buf = calloc(1, (size_t)size + 1);
	if (!buf)
		return NULL;
	n = klogctl(3, buf, size);	/* SYSLOG_ACTION_READ_ALL */
	if (n < 0) {
		free(buf);
		return NULL;
	}
	buf[n] = '\0';
	return buf;
}

/* The router-local salt for hashing identifiers; created once. */
static bool read_salt(char *salt, size_t size)
{
	/* a salt not kept (a full overlay) still hides this bundle's identifiers; only the next bundle's tokens differ */
	return mm_salt_load(env_or("MEDVEMODEM_DIAG_SALT", MM_DIAG_SALT_FILE), salt, size, NULL);
}

/* netifd's interfaces (ubus network.interface dump; MEDVEMODEM_NETIFD_DUMP names a file instead, for tests) */
static json_object *netifd_dump(void)
{
	const char *file = getenv("MEDVEMODEM_NETIFD_DUMP");
	FILE *f = file ? fopen(file, "r") : popen("ubus call network.interface dump 2>/dev/null", "r");
	static char buf[1 << 18];
	size_t n;
	json_object *o, *list = NULL;

	if (!f)
		return NULL;
	n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	if (file)
		fclose(f);
	else
		pclose(f);
	o = json_tokener_parse(buf);
	if (o && json_object_object_get_ex(o, "interface", &list))
		json_object_get(list);
	json_object_put(o);
	return list;
}

/* an interface carrying a default route (IPv4 or IPv6) */
static bool has_default(json_object *i)
{
	static const char *const keys[] = { "route", "inactive", NULL };

	for (int k = 0; keys[k]; k++) {
		json_object *routes = !strcmp(keys[k], "route") ? json_object_object_get(i, "route") :
				      json_object_object_get(json_object_object_get(i, "inactive"), "route");
		for (size_t r = 0; routes && json_object_is_type(routes, json_type_array) && r < json_object_array_length(routes); r++) {
			json_object *x = json_object_array_get_idx(routes, r);
			const char *t = json_object_get_string(json_object_object_get(x, "target"));

			if (t && (!strcmp(t, "0.0.0.0") || !strcmp(t, "::")) && !json_object_get_int(json_object_object_get(x, "mask")))
				return true;
		}
	}
	return false;
}

/* the channels a manual check may use: interfaces up with a default route, their device and DNS servers */
static json_object *channels(void)
{
	json_object *list = netifd_dump(), *out = json_object_new_array();

	for (size_t i = 0; list && i < json_object_array_length(list); i++) {
		json_object *x = json_object_array_get_idx(list, i), *c, *dns;
		const char *dev = json_object_get_string(json_object_object_get(x, "l3_device"));

		if (!json_object_get_boolean(json_object_object_get(x, "up")) || !dev || !*dev || !strcmp(dev, "lo") || !has_default(x))
			continue;
		c = json_object_new_object();
		json_object_object_add(c, "interface", json_object_get(json_object_object_get(x, "interface")));
		json_object_object_add(c, "device", json_object_new_string(dev));
		json_object_object_add(c, "proto", json_object_get(json_object_object_get(x, "proto")));
		dns = json_object_object_get(x, "dns-server");
		json_object_object_add(c, "dns", dns ? json_object_get(dns) : json_object_new_array());
		json_object_array_add(out, c);
	}
	json_object_put(list);
	return out;
}

/* the first DNS server netifd has for the interface on dev (an IPv4 one first) */
static const char *dns_of(json_object *chans, const char *dev)
{
	const char *v6 = NULL;

	for (size_t i = 0; chans && i < json_object_array_length(chans); i++) {
		json_object *c = json_object_array_get_idx(chans, i), *dns = json_object_object_get(c, "dns");

		if (strcmp(json_object_get_string(json_object_object_get(c, "device")), dev))
			continue;
		for (size_t k = 0; dns && k < json_object_array_length(dns); k++) {
			const char *d = json_object_get_string(json_object_array_get_idx(dns, k));
			if (d && !strchr(d, ':'))
				return d;
			if (d && !v6)
				v6 = d;
		}
	}
	return v6;
}

/*
 * --at (LuCI's report button, the owner 2026-10-21: what a new modem's profile needs and the USB strings do not say):
 * the model's answers to read-only queries, sent through the engine's own AT path, so a modem whose AT port is
 * ModemManager's or QModem's, or a frozen one, is refused there and left alone. The family's query is its USB mode,
 * the one that tells two firmwares of one VID:PID apart (docs/modem-zoo/COMPOSITION-AND-BOOT-MODES.md).
 */
static const char *const REPORT_AT[] = { "ATI", "AT+CGMI", "AT+CGMM", "AT+CGMR", NULL };
static const struct { const char *vid, *cmd; } REPORT_AT_FAMILY[] = {
	{ "0e8d", "AT+GTUSBMODE?" }, { "2cb7", "AT+GTUSBMODE?" }, { "8087", "AT+GTUSBMODE?" },	/* Fibocom */
	{ "2c7c", "AT+QCFG=\"usbnet\"" },	/* Quectel */
	{ "12d1", "AT^SETPORT?" },		/* Huawei */
	{ "1e2d", "AT^USBSWITCH?" }, { "05c6", "AT^USBSWITCH?" },	/* Foxconn */
	{ "1bc7", "AT#USBCFG?" },		/* Telit */
	{ "1199", "AT!USBCOMP?" },		/* Sierra */
	{ NULL, NULL } };

/*
 * The modem's reception for the report (the owner, 2026-11-04: «он не показывает приём модема»): the technology, the
 * operator, the levels, the band, channel, PCI and the bandwidth — what tells a weak or a jammed link. Never the cell
 * ID, TAC or LAC (a place), nor any identifier: a whitelist of keys, the rest dropped. src: a web modem's metrics or
 * ModemManager's record (signal, signal_quality, access_tech, operator, registration)
 */
static json_object *report_radio(json_object *src)
{
	static const char *const keep[] = { "access_technology", "access_tech", "operator", "operator_code", "network_provider",
		"registration", "signal_quality", "signal", "rsrp", "rsrp_dbm", "rsrq", "rsrq_db", "sinr", "sinr_db", "rssi", "rssi_dbm",
		"rscp", "rscp_dbm", "ecio", "ecio_db", "lte_rsrp", "lte_rsrq", "lte_snr", "lte_rssi", "band", "bands", "lte_band",
		"earfcn", "ul_earfcn", "pci", "lte_pci", "bandwidth_mhz", "ul_bandwidth_mhz", "transmission_mode", "rrc", "nr_rsrp",
		"nr_rsrq", "nr_sinr", "nr_band", "ca", NULL };
	json_object *out = json_object_new_object(), *v;

	for (size_t i = 0; keep[i]; i++)
		if (json_object_object_get_ex(src, keep[i], &v) && v)
			json_object_object_add(out, keep[i], json_object_get(v));
	return out;
}

static json_object *report_at_one(const char *id, const char *cmd, const char **unavailable)
{
	json_object *args = json_object_new_object(), *r = NULL, *o = json_object_new_object();
	static const char *const keep[] = { "port", "ok", "final", "lines", "refused", "hazard", "error", NULL };

	json_object_object_add(args, "id", json_object_new_string(id));
	json_object_object_add(args, "command", json_object_new_string(cmd));
	json_object_object_add(o, "command", json_object_new_string(cmd));
	if (daemon_call_args("at_command", args, &r, 8000) != CALL_OK || !r) {
		json_object_object_add(o, "error", json_object_new_string("the engine did not answer"));
		*unavailable = "the engine did not answer";
	} else {
		for (size_t i = 0; keep[i]; i++)
			if (json_object_object_get(r, keep[i]))
				json_object_object_add(o, keep[i], json_object_get(json_object_object_get(r, keep[i])));
		/* refused without a hazard: the port is not the engine's (owner, frozen, none known); the rest would be too */
		if (json_object_object_get(r, "refused") && !json_object_object_get(r, "hazard"))
			*unavailable = json_object_get_string(json_object_object_get(r, "refused"));
	}
	json_object_put(args);
	json_object_put(r);
	return o;
}

static json_object *report_at(const char *id, const char *vidpid)
{
	json_object *o = json_object_new_object(), *answers = json_object_new_array();
	const char *unavailable = NULL;
	size_t i;

	for (i = 0; REPORT_AT[i] && !unavailable; i++)
		json_object_array_add(answers, report_at_one(id, REPORT_AT[i], &unavailable));
	for (i = 0; REPORT_AT_FAMILY[i].vid && !unavailable; i++)
		if (vidpid && !strncmp(vidpid, REPORT_AT_FAMILY[i].vid, 4) && vidpid[4] == ':')
			json_object_array_add(answers, report_at_one(id, REPORT_AT_FAMILY[i].cmd, &unavailable));
	if (unavailable) {
		/* the one refusal says why; no answers to keep */
		json_object_object_add(o, "unavailable", json_object_new_string(unavailable));
		json_object_put(answers);
	} else {
		json_object_object_add(o, "answers", answers);
	}
	return o;
}

static int cmd_diag_profile(const char *id, const char *label, bool to_stdout, bool with_at)
{
	json_object *res = NULL, *rec = NULL, *events = NULL, *engine = json_object_new_object(), *bundle, *klog, *sys;
	struct mm_registry reg = { 0 };
	bool daemon = false, have_reg = false;
	char salt[64], *log, path[512], name[160];
	const char *dir = env_or("MEDVEMODEM_DIAG_DIR", MM_DIAG_DIR), *p;
	size_t i;
	int rc = ask_daemon("device", "id", id, &res);

	if (rc == CALL_NOT_FOUND) {
		fprintf(stderr, "device not found: %s\n", id);
		json_object_put(engine);
		return 2;
	}
	if (rc == CALL_OK) {
		json_object *ev = NULL;
		daemon = true;
		rec = res;
		if (ask_daemon("events", "id", id, &ev) == CALL_OK) {
			json_object *list = NULL;
			if (json_object_object_get_ex(ev, "events", &list))
				events = json_object_get(list);
			json_object_put(ev);
		}
	} else {
		json_object *found;
		one_shot(&reg);
		have_reg = true;
		found = mm_registry_find(&reg, id);
		if (!found) {
			fprintf(stderr, "device not found: %s\n", id);
			mm_registry_free(&reg);
			json_object_put(engine);
			return 2;
		}
		rec = with_roles(found);
	}
	json_object_object_add(engine, "version", json_object_new_string(MEDVEMODEM_VERSION));
	json_object_object_add(engine, "running", json_object_new_boolean(daemon));
	if (daemon) {
		/* what the modem looked like before the last disruptive recovery (spec 22.1.6) */
		json_object *rv = NULL;
		json_object *st = NULL;
		if (ask_daemon("recovery", "id", id, &rv) == CALL_OK)
			json_object_object_add(engine, "recovery", rv);
		/* how hard the engine worked the modems and ModemManager (the crash reports) */
		if (ask_daemon("status", NULL, NULL, &st) == CALL_OK) {
			json_object *ld = json_object_object_get(st, "load");
			if (ld) {
				json_object_object_add(engine, "load", json_object_get(ld));
				json_object_object_add(json_object_object_get(engine, "load"), "uptime_s",
						       json_object_get(json_object_object_get(st, "uptime")));
			}
			json_object_put(st);
		}
		/* other modem helpers on the router: packages, programs on modem ports, the operator's scripts (2026-10-28) */
		st = NULL;
		if (ask_daemon("neighbours", NULL, NULL, &st) == CALL_OK)
			json_object_object_add(engine, "neighbours", st);
	}
	if (!daemon)
		json_object_object_add(engine, "note", json_object_new_string(
			"engine not running: no ModemManager mapping, history or leases in this bundle"));
	log = kernel_log();
	klog = mm_diag_klog_filter(log ? log : "", rec, 200);
	free(log);
	sys = system_facts();
	bundle = mm_diag_bundle(rec, events, sys, klog, engine, label, (long long)time(NULL));
	/* the report (alpha): the record's diagnosis, session and uplink, the router's uplink summary and the system log */
	{
		json_object *dec = json_object_object_get(bundle, "decision"), *up = NULL;
		static const char *const keep[] = { "diagnosis", "session", "uplink", "health", "connection", "drivers_missing", NULL };
		json_object *prof = json_object_object_get(rec, "profile");

		for (i = 0; keep[i]; i++)
			if (json_object_object_get(rec, keep[i]))
				json_object_object_add(dec, keep[i], json_object_get(json_object_object_get(rec, keep[i])));
		/* how well the model is known: what a profile request starts from */
		json_object_object_add(dec, "fit", json_object_new_string(
			!prof ? "none" : json_object_object_get(prof, "fit") ? json_object_get_string(json_object_object_get(prof, "fit")) : "exact"));
		if (daemon && ask_daemon("uplink", NULL, NULL, &up) == CALL_OK)
			json_object_object_add(bundle, "router_uplink", up);
		/* a web modem's last reading, without its values (the owner 2026-11-03: a HiLink and an M150 showed no
		 * operator; the report had no word of their web): kind, login outcome, error, age, which fields came */
		{
			json_object *w = NULL, *web, *m, *names;

			const char *ctl = json_object_get_string(json_object_object_get(json_object_object_get(dec, "owner"), "control"));

			/* the engine's own reading of every modem (the owner 2026-11-04: «для не hilink модемов тоже про радио»):
			 * ModemManager's record, a web modem's web, QModem's cell_info, or the engine's AT port through its own
			 * lease, a fresh one from its cache; never a port opened here */
			json_object *margs = json_object_new_object();

			json_object_object_add(margs, "id", json_object_new_string(json_object_get_string(json_object_object_get(rec, "device_id"))));
			json_object_object_add(margs, "kept", json_object_new_boolean(1));
			if (!daemon || no_daemon_forced() || daemon_call_args("metrics", margs, &w, 6000) != CALL_OK)
				w = NULL;
			json_object_put(margs);
			if (w &&
			    (m = json_object_object_get(w, "metrics")) && json_object_is_type(m, json_type_object)) {
				json_object *radio = report_radio(m);

				if (json_object_object_get(w, "source"))
					json_object_object_add(radio, "source", json_object_get(json_object_object_get(w, "source")));
				if (json_object_object_get(w, "age_ms"))
					json_object_object_add(radio, "age_ms", json_object_get(json_object_object_get(w, "age_ms")));
				json_object_object_add(bundle, "radio", radio);
			}
			if (w && ctl && !strcmp(ctl, "hilink") && json_object_object_get(w, "web")) {
				static const char *const fields[] = { "kind", "auth", "wait_s", "password_saved", "sim", "network", "data_link", "sim_code",
									      NULL };

				web = json_object_new_object();
				for (i = 0; fields[i]; i++)
					if (json_object_object_get(json_object_object_get(w, "web"), fields[i]))
						json_object_object_add(web, fields[i], json_object_get(json_object_object_get(json_object_object_get(w, "web"), fields[i])));
				if (json_object_object_get(w, "refused"))
					json_object_object_add(web, "refused", json_object_get(json_object_object_get(w, "refused")));
				if (json_object_object_get(w, "age_ms"))
					json_object_object_add(web, "age_ms", json_object_get(json_object_object_get(w, "age_ms")));
				if ((m = json_object_object_get(w, "metrics")) && json_object_is_type(m, json_type_object)) {
					names = json_object_new_array();
					json_object_object_foreach(m, k, v) {
						(void)v;
						json_object_array_add(names, json_object_new_string(k));
					}
					json_object_object_add(web, "metrics_fields", names);
				}
				json_object_object_add(bundle, "web", web);
			}
			json_object_put(w);
		}
		/* the reception of a ModemManager modem as it reported it last: no AT port asked (the owner 2026-11-04: the
		 * report had no word of the signal) */
		if (!json_object_object_get(bundle, "radio") && json_object_object_get(rec, "modemmanager"))
			json_object_object_add(bundle, "radio", report_radio(json_object_object_get(rec, "modemmanager")));
		if (with_at && daemon)
			json_object_object_add(bundle, "at", report_at(json_object_get_string(json_object_object_get(rec, "device_id")),
								       json_object_get_string(json_object_object_get(rec, "vidpid"))));
		json_object_object_add(bundle, "system_log", system_log());
	}
	if (!read_salt(salt, sizeof(salt))) {
		fprintf(stderr, "medvemodem: no randomness for the diagnostic salt; refusing to write an unsalted bundle\n");
		rc = 1;
		goto out;
	}
	mm_diag_sanitize(bundle, salt);
	if (to_stdout) {
		print(bundle);
		rc = 0;
		goto out;
	}
	/* file name from the sanitized id: never the raw serial */
	p = json_object_get_string(json_object_object_get(json_object_object_get(bundle, "device"), "device_id"));
	for (i = 0; p && p[i] && i + 1 < sizeof(name); i++)
		name[i] = (p[i] == ':' || p[i] == '/' || p[i] == '@' || p[i] == '~') ? '_' : p[i];
	name[i] = '\0';
	mkdir("/tmp/medvemodem", 0755);
	mkdir(dir, 0700);
	snprintf(path, sizeof(path), "%s/%s-%s.json", dir, name, label);
	if (json_object_to_file_ext(path, bundle, JSON_C_TO_STRING_PRETTY)) {
		fprintf(stderr, "medvemodem: cannot write %s: %s\n", path, strerror(errno));
		rc = 1;
		goto out;
	}
	printf("Wrote %s\n", path);
	printf("Included: composition (%zu interfaces), engine decision and reasoning, history (%zu events),\n"
	       "          kernel log lines about this device (%zu), system facts.\n",
	       json_object_array_length(json_object_object_get(json_object_object_get(bundle, "composition"), "interfaces")),
	       events ? json_object_array_length(events) : 0, json_object_array_length(klog));
	printf("Removed:  USB serial (replaced by a hash salted on this router), IMEI/IMSI/ICCID/phone numbers,\n"
	       "          PINs and passwords. Review before sharing: cat %s\n", path);
	rc = 0;
out:
	json_object_put(bundle);
	json_object_put(klog);
	json_object_put(sys);
	json_object_put(engine);
	json_object_put(events);
	json_object_put(rec);
	if (have_reg)
		mm_registry_free(&reg);
	return rc;
}

static int cmd_diag_diff(const char *a, const char *b)
{
	json_object *ja = json_object_from_file(a), *jb = json_object_from_file(b), *d;

	if (!ja || !jb) {
		fprintf(stderr, "medvemodem: cannot read %s\n", ja ? b : a);
		json_object_put(ja);
		json_object_put(jb);
		return 2;
	}
	d = mm_diag_diff(ja, jb);
	print(d);
	json_object_put(d);
	json_object_put(ja);
	json_object_put(jb);
	return 0;
}

/* ---- configuration (spec 17): only through the engine, which serializes writers ---- */

static int cmd_config(int argc, char **argv)
{
	const char *sub = argc >= 3 ? argv[2] : "", *method = NULL, *status;
	json_object *args = json_object_new_object(), *res = NULL;
	int rc;

	if (!strcmp(sub, "state") && argc <= 4) {
		method = "config_state";
	} else if (argc == 4 || (argc == 5 && !strcmp(sub, "plan") && !strcmp(argv[4], "--release"))) {
		if (!strcmp(sub, "plan"))
			method = "config_plan";
		else if (!strcmp(sub, "apply"))
			method = "config_apply";
		else if (!strcmp(sub, "release"))
			method = "config_release";
		else if (!strcmp(sub, "rollback"))
			method = "config_rollback";
		if (argc == 5)
			json_object_object_add(args, "release", json_object_new_boolean(1));
	}
	if (!method) {
		json_object_put(args);
		return -1;
	}
	if (argc >= 4)
		json_object_object_add(args, "id", json_object_new_string(argv[3]));
	/* apply waits for the network reload, bounded by the engine */
	rc = no_daemon_forced() ? CALL_NO_DAEMON : daemon_call_args(method, args, &res, 45000);
	json_object_put(args);
	if (rc == CALL_NOT_FOUND) {
		fprintf(stderr, "device not found: %s\n", argv[3]);
		return 2;
	}
	if (rc != CALL_OK) {
		fprintf(stderr, "medvemodem: configuration changes need the running engine\n");
		return 3;
	}
	print(res);
	status = json_object_get_string(json_object_object_get(res, "status"));
	rc = !status || !strcmp(status, "planned") || !strcmp(status, "committed") || !strcmp(status, "unchanged") ||
	     !strcmp(status, "rolled-back") ? 0 : 1;
	json_object_put(res);
	return rc;
}

/* AT console, metrics and probe (spec 11.5): through the engine's leases only */
static void usage(FILE *f);

static int cmd_dial(int argc, char **argv)
{
	json_object *args = json_object_new_object(), *res = NULL;
	bool hangup = !strcmp(argv[1], "hangup");
	int i, rc;

	json_object_object_add(args, "id", json_object_new_string(argv[2]));
	for (i = 3; i < argc; i++) {
		if (!hangup && i + 1 < argc && (!strcmp(argv[i], "--apn") || !strcmp(argv[i], "--iptype") ||
						 !strcmp(argv[i], "--interface"))) {
			json_object_object_add(args, !strcmp(argv[i], "--apn") ? "apn" : !strcmp(argv[i], "--iptype") ? "pdptype" : "interface",
					       json_object_new_string(argv[i + 1]));
			i++;
		} else {
			json_object_put(args);
			usage(stderr);
			return 2;
		}
	}
	rc = no_daemon_forced() ? CALL_NO_DAEMON : daemon_call_args(argv[1], args, &res, 180000);
	json_object_put(args);
	if (rc == CALL_NOT_FOUND) {
		fprintf(stderr, "device not found: %s\n", argv[2]);
		return 2;
	}
	if (rc != CALL_OK) {
		fprintf(stderr, "medvemodem: this needs the running engine\n");
		return 3;
	}
	print(res);
	rc = json_object_object_get(res, "refused") || json_object_object_get(res, "error") ? 1 : 0;
	json_object_put(res);
	return rc;
}

/* one engine call with {id, ...}; prints the answer; 1 when it refused or failed. Takes args. */
static int cmd_call(const char *method, const char *id, json_object *args, json_object **keep)
{
	json_object *res = NULL;
	int rc;

	json_object_object_add(args, "id", json_object_new_string(id));
	rc = no_daemon_forced() ? CALL_NO_DAEMON : daemon_call_args(method, args, &res, 60000);
	json_object_put(args);
	if (rc == CALL_NOT_FOUND) {
		fprintf(stderr, "device not found: %s\n", id);
		return 2;
	}
	if (rc != CALL_OK) {
		fprintf(stderr, "medvemodem: this needs the running engine\n");
		return 3;
	}
	if (!keep)
		print(res);
	rc = json_object_object_get(res, "refused") || json_object_object_get(res, "error") ||
	     (json_object_object_get(res, "ok") && !json_object_get_boolean(json_object_object_get(res, "ok"))) ? 1 : 0;
	if (keep)
		*keep = res;
	else
		json_object_put(res);
	return rc;
}

static int cmd_at(const char *method, const char *id, const char *key, const char *value, int number_value)
{
	json_object *args = json_object_new_object();

	if (key && value)
		json_object_object_add(args, key, json_object_new_string(value));
	else if (key)
		json_object_object_add(args, key, json_object_new_int(number_value));
	return cmd_call(method, id, args, NULL);
}

/* SMS and USSD (spec 38): through the engine, which knows which modem ModemManager holds */
static int cmd_sms(int argc, char **argv)
{
	json_object *args = json_object_new_object();

	if (argc == 3) {
		json_object_object_add(args, "refresh", json_object_new_boolean(true));
		return cmd_call("sms_list", argv[2], args, NULL);
	}
	if (argc == 6 && !strcmp(argv[2], "send")) {
		json_object_object_add(args, "number", json_object_new_string(argv[4]));
		json_object_object_add(args, "text", json_object_new_string(argv[5]));
		return cmd_call("sms_send", argv[3], args, NULL);
	}
	if (argc == 5 && !strcmp(argv[2], "delete") && strspn(argv[4], "0123456789") == strlen(argv[4]) && *argv[4]) {
		json_object_object_add(args, "sms", json_object_new_int(atoi(argv[4])));
		return cmd_call("sms_delete", argv[3], args, NULL);
	}
	json_object_put(args);
	return -1;
}

/* a USSD request runs in the engine; this waits for its end like the page does */
static int cmd_ussd(int argc, char **argv)
{
	json_object *args = json_object_new_object(), *res = NULL;
	const char *id, *method;
	int rc, i;

	if (argc == 4 && (!strcmp(argv[2], "status") || !strcmp(argv[2], "cancel"))) {
		json_object_put(args);
		return cmd_call(!strcmp(argv[2], "status") ? "ussd_status" : "ussd_cancel", argv[3], json_object_new_object(), NULL);
	}
	if (argc == 5 && !strcmp(argv[2], "reply")) {
		id = argv[3];
		method = "ussd_respond";
		json_object_object_add(args, "reply", json_object_new_string(argv[4]));
	} else if (argc == 4) {
		id = argv[2];
		method = "ussd_start";
		json_object_object_add(args, "code", json_object_new_string(argv[3]));
	} else {
		json_object_put(args);
		return -1;
	}
	rc = cmd_call(method, id, args, &res);
	if (rc) {
		if (res)
			print(res);
		json_object_put(res);
		return rc;
	}
	json_object_put(res);
	/* the engine bounds the request (75 s); a little longer here */
	for (i = 0; i < 1700; i++) {
		const char *st;

		res = NULL;
		if (cmd_call("ussd_status", id, json_object_new_object(), &res) > 1)
			return 3;
		st = json_object_get_string(json_object_object_get(res, "state"));
		if (!st || strcmp(st, "running")) {
			print(res);
			rc = st && !strcmp(st, "done") ? 0 : 1;
			json_object_put(res);
			return rc;
		}
		json_object_put(res);
		usleep(50000);
	}
	fprintf(stderr, "medvemodem: no USSD answer in time\n");
	return 1;
}

/* bring-up (spec 16): waits for the result unless --no-wait */
static int cmd_bringup(const char *id, bool wait)
{
	json_object *args = json_object_new_object(), *res = NULL;
	const char *result;
	int rc;

	json_object_object_add(args, "id", json_object_new_string(id));
	json_object_object_add(args, "wait", json_object_new_boolean(wait));
	rc = no_daemon_forced() ? CALL_NO_DAEMON : daemon_call_args("bringup", args, &res, 200000);
	json_object_put(args);
	if (rc == CALL_NOT_FOUND) {
		fprintf(stderr, "device not found: %s\n", id);
		return 2;
	}
	if (rc != CALL_OK) {
		fprintf(stderr, "medvemodem: bring-up needs the running engine\n");
		return 3;
	}
	print(res);
	result = json_object_get_string(json_object_object_get(res, "result"));
	rc = result && (!strcmp(result, "connected") || !strcmp(result, "started")) ? 0 : 1;
	json_object_put(res);
	return rc;
}

/* recovery (spec 22.1.12): one ladder level through the engine's controller */
static int cmd_recover(int argc, char **argv)
{
	json_object *args = json_object_new_object(), *res = NULL;
	const char *result;
	bool wait = true;
	int rc, i;

	json_object_object_add(args, "id", json_object_new_string(argv[2]));
	json_object_object_add(args, "level", json_object_new_string(argv[3]));
	for (i = 4; i < argc; i++) {
		if (!strcmp(argv[i], "--no-wait"))
			wait = false;
		else if (!strcmp(argv[i], "--reason") && i + 1 < argc)
			json_object_object_add(args, "reason", json_object_new_string(argv[++i]));
		else {
			json_object_put(args);
			usage(stderr);
			return 2;
		}
	}
	json_object_object_add(args, "wait", json_object_new_boolean(wait));
	json_object_object_add(args, "source", json_object_new_string("cli"));
	if (wait)
		fprintf(stderr, "medvemodem: %s recovery of %s running; a reset can take up to 3 minutes\n", argv[3], argv[2]);
	/* the engine first refreshes a stale ModemManager view and asks to be asked again */
	for (i = 0; i < 6; i++) {
		json_object_put(res);
		res = NULL;
		rc = no_daemon_forced() ? CALL_NO_DAEMON : daemon_call_args("recover", args, &res, 200000);
		result = rc == CALL_OK ? json_object_get_string(json_object_object_get(res, "result")) : NULL;
		if (!result || strcmp(result, "retry"))
			break;
		sleep(2);
	}
	json_object_put(args);
	if (rc == CALL_NOT_FOUND) {
		fprintf(stderr, "device not found: %s\n", argv[2]);
		return 2;
	}
	if (rc != CALL_OK) {
		fprintf(stderr, "medvemodem: recovery needs the running engine\n");
		return 3;
	}
	print(res);
	result = json_object_get_string(json_object_object_get(res, "result"));
	rc = result && (!strcmp(result, "succeeded") || !strcmp(result, "started")) ? 0 : 1;
	json_object_put(res);
	return rc;
}

static void cell(const char *s, int w)
{
	printf("%-*.*s ", w, w, s ? s : "-");
}

static int cmd_diag_quick(void)
{
	struct mm_registry reg;
	json_object *res = NULL, *list = NULL, *rows, *own = NULL;
	size_t i;

	if (ask_daemon("devices", NULL, NULL, &res) == CALL_OK && json_object_object_get_ex(res, "devices", &list)) {
		rows = mm_diag_quick(list);
	} else {
		json_object *raw;
		json_object_put(res);
		res = NULL;
		one_shot(&reg);
		raw = mm_registry_list(&reg, false);
		own = json_object_new_array();
		for (i = 0; i < json_object_array_length(raw); i++)
			json_object_array_add(own, with_roles(json_object_array_get_idx(raw, i)));
		rows = mm_diag_quick(own);
		json_object_put(raw);
		mm_registry_free(&reg);
	}
	printf("%-34s %-18s %-9s %-13s %-12s %-14s %-9s %s\n", "DEVICE", "PRODUCT", "VID:PID", "CONFIG", "OWNER", "AT", "STABLE", "MM");
	for (i = 0; i < json_object_array_length(rows); i++) {
		json_object *r = json_object_array_get_idx(rows, i);
#define F(k) json_object_get_string(json_object_object_get(r, k))
		cell(F("device_id"), 34); cell(F("product"), 18); cell(F("vidpid"), 9); cell(F("configuration"), 13);
		cell(F("owner"), 12); cell(F("at_port"), 14); cell(F("stability"), 9);
		printf("%s\n", F("modemmanager") ? F("modemmanager") : "-");
#undef F
	}
	if (!json_object_array_length(rows))
		printf("no modems\n");
	json_object_put(rows);
	json_object_put(own);
	json_object_put(res);
	return 0;
}

static int cmd_diag(int argc, char **argv)
{
	const char *label = "profile";
	bool to_stdout = false, with_at = false;
	int i;

	if (argc == 3 && !strcmp(argv[2], "quick"))
		return cmd_diag_quick();
	if (argc == 5 && !strcmp(argv[2], "diff"))
		return cmd_diag_diff(argv[3], argv[4]);
	if (argc >= 4 && !strcmp(argv[2], "profile")) {
		for (i = 4; i < argc; i++) {
			if (!strcmp(argv[i], "--before"))
				label = "before";
			else if (!strcmp(argv[i], "--after"))
				label = "after";
			else if (!strcmp(argv[i], "--stdout"))
				to_stdout = true;
			else if (!strcmp(argv[i], "--at"))
				with_at = true;
			else if (!strcmp(argv[i], "--label") && i + 1 < argc && strspn(argv[i + 1], "abcdefghijklmnopqrstuvwxyz0123456789-_") == strlen(argv[i + 1]) && *argv[i + 1])
				label = argv[++i];
			else
				return -1;
		}
		return cmd_diag_profile(argv[3], label, to_stdout, with_at);
	}
	return -1;
}

/* the terminal view asks the engine like any other frontend */
static int top_call(const char *method, json_object *args, json_object **out, int timeout_ms)
{
	return daemon_call_args(method, args, out, timeout_ms) == CALL_OK;
}

/* this tool's version, and the running engine's when it answers (an upgrade not restarted shows the old one) */
static int cmd_version(void)
{
	json_object *res = NULL;
	const char *v = NULL;

	printf("medvemodem %s\n", MEDVEMODEM_VERSION);
	if (ask_daemon("status", NULL, NULL, &res) == CALL_OK && json_object_object_get_ex(res, "version", NULL))
		v = json_object_get_string(json_object_object_get(res, "version"));
	if (v)
		printf("engine %s%s\n", v, strcmp(v, MEDVEMODEM_VERSION) ? " (running; restart it: /etc/init.d/medvemodemd restart)" : "");
	else
		printf("engine not running\n");
	json_object_put(res);
	return 0;
}

static void usage(FILE *f)
{
	fprintf(f,
		"Usage: medvemodem top|tui [<device-id>] [--once] [--show-identifiers] [--no-color] [--ascii] [--width N] [--lang en|ru]\n"
		"       medvemodem version\n"
		"       medvemodem list\n"
		"       medvemodem show <device-id|attachment-id>\n"
		"       medvemodem resolve\n"
		"       medvemodem lookup <tty|wdm|netdev>\n"
		"       medvemodem status\n"
		"       medvemodem rescan\n"
		"       medvemodem uplink\n"
		"       medvemodem events <device-id|attachment-id> [--text|--json]\n"
		"       medvemodem diag quick\n"
		"       medvemodem diag profile <device-id|attachment-id> [--before|--after|--label NAME] [--stdout] [--at]\n"
		"       medvemodem diag diff <bundle-a.json> <bundle-b.json>\n"
		"       medvemodem dns-check <netdev> [<operator-dns>] [--path-ms=N]\n"
		"       medvemodem web-signal <host> <netdev|-> <huawei|zte|mark|auto> <state-file> [fresh]   (the web password on stdin)\n"
		"       medvemodem web-sms <host> <netdev|-> <huawei|zte|mark|auto> <state-file> <list|send|delete> [index] [fresh]\n"
		"                (the web password, then for send {\"number\", \"text\"}, one line each on stdin)\n"
		"       medvemodem web-act <host> <netdev|-> <huawei|zte|mark|auto> <state-file> <info|ussd|reboot> [fresh]\n"
		"                (the web password, then for ussd {\"code\"}, one line each on stdin)\n"
		"       medvemodem channels\n"
		"       medvemodem config plan <device-id> [--release]\n"
		"       medvemodem config apply|release|rollback <device-id>\n"
		"       medvemodem config state [<device-id>]\n"
		"       medvemodem bringup <device-id> [--no-wait]\n"
		"       medvemodem recover <device-id> observe|session|control|radio|soft-reset|rebind|usb-reset|power\n"
		"                          [--reason REASON] [--no-wait]\n"
		"       medvemodem at <device-id> <command>\n"
		"       medvemodem sms <device-id>\n"
		"       medvemodem sms send <device-id> <number> <text>\n"
		"       medvemodem sms delete <device-id> <n>\n"
		"       medvemodem ussd <device-id> <code>\n"
		"       medvemodem ussd reply <device-id> <text>\n"
		"       medvemodem ussd status|cancel <device-id>\n"
		"       medvemodem metrics <device-id> [--max-age ms]\n"
		"       medvemodem probe <device-id>\n"
		"       medvemodem dial <device-id> [--apn APN] [--iptype ipv4|ipv6|ipv4v6] [--interface NAME]\n"
		"       medvemodem fcc-unlock <device-id> on|off\n"
		"       medvemodem freeze <device-id> on|off\n"
		"       medvemodem handover <device-id>   (an interface set up by hand drives it: MedveModem takes it)\n"
		"       medvemodem hangup <device-id>\n"
		"       medvemodem esim status <device-id>\n"
		"       medvemodem esim version|probe|chip|profiles <device-id>\n");
}

/*
 * rpcd's file exec hands the caller's environment over (LuCI's fs.exec(cmd, args, env)) and runs this as root:
 * the commands LuCI may call (diag, channels, dns-check) take nothing from it when installed (review
 * 2026-10-17: MEDVEMODEM_CURL named the program dns-check runs, MEDVEMODEM_LOGREAD the file a report reads).
 * A build elsewhere (the tests) keeps its overrides.
 */
static void drop_callers_environment(void)
{
	char exe[64];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);

	if (n <= 0)
		return;
	exe[n] = '\0';
	if (strcmp(exe, "/usr/sbin/medvemodem"))
		return;
	clearenv();
	setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
}

int main(int argc, char **argv)
{
	if (argc >= 2 && (!strcmp(argv[1], "diag") || !strcmp(argv[1], "channels") || !strcmp(argv[1], "dns-check") ||
			  !strcmp(argv[1], "web-signal") || !strcmp(argv[1], "web-sms") || !strcmp(argv[1], "web-act")))
		drop_callers_environment();
	if (argc < 2) {
		usage(stderr);
		return 2;
	}

	if ((!strcmp(argv[1], "version") || !strcmp(argv[1], "--version") || !strcmp(argv[1], "-v")) && argc == 2)
		return cmd_version();
	/* tui: the same screen under the name the spec gave it (the owner, 2026-10-21) */
	if (!strcmp(argv[1], "top") || !strcmp(argv[1], "tui"))
		return mm_top_main(argc, argv, top_call);
	if (!strcmp(argv[1], "list") || !strcmp(argv[1], "resolve"))
		return cmd_list();
	if (!strcmp(argv[1], "show") && argc == 3)
		return cmd_find("device", "id", argv[2], "device not found", mm_registry_find);
	if (!strcmp(argv[1], "lookup") && argc == 3)
		return cmd_find("lookup", "node", argv[2], "node not mapped", mm_registry_lookup_node);
	if ((!strcmp(argv[1], "status") || !strcmp(argv[1], "rescan") || !strcmp(argv[1], "uplink")) && argc == 2)
		return cmd_daemon(argv[1], NULL, NULL);
	if (!strcmp(argv[1], "events") && (argc == 3 || (argc == 4 && (!strcmp(argv[3], "--json") || !strcmp(argv[3], "--text")))))
		return cmd_events(argv[2], argc == 4 ? !strcmp(argv[3], "--text") : isatty(STDOUT_FILENO));
	if (!strcmp(argv[1], "at") && argc == 4)
		return cmd_at("at_command", argv[2], "command", argv[3], 0);
	if (!strcmp(argv[1], "metrics") && argc == 3)
		return cmd_at("metrics", argv[2], NULL, NULL, 0);
	if (!strcmp(argv[1], "metrics") && argc == 5 && !strcmp(argv[3], "--max-age"))
		return cmd_at("metrics", argv[2], "max_age_ms", NULL, atoi(argv[4]));
	if ((!strcmp(argv[1], "dial") || !strcmp(argv[1], "hangup")) && argc >= 3)
		return cmd_dial(argc, argv);
	if (!strcmp(argv[1], "probe") && argc == 3)
		return cmd_at("probe", argv[2], NULL, NULL, 0);
	/* the operator's FCC unlock hook for ModemManager (HANDOFF 176) */
	if (!strcmp(argv[1], "fcc-unlock") && argc == 4 && (!strcmp(argv[3], "on") || !strcmp(argv[3], "off"))) {
		json_object *args = json_object_new_object(), *res = NULL;
		const char *r;
		int rc;

		json_object_object_add(args, "enable", json_object_new_boolean(!strcmp(argv[3], "on")));
		rc = cmd_call("fcc_unlock", argv[2], args, &res);
		if (res) {
			print(res);
			r = json_object_get_string(json_object_object_get(res, "result"));
			rc = rc || !r || (strcmp(r, "enabled") && strcmp(r, "disabled"));
			json_object_put(res);
		}
		return rc;
	}
	/* «Заморозить»: no service touches the modem until «off» */
	if (!strcmp(argv[1], "freeze") && argc == 4 && (!strcmp(argv[3], "on") || !strcmp(argv[3], "off"))) {
		json_object *args = json_object_new_object(), *res = NULL;
		const char *r;
		int rc;

		json_object_object_add(args, "on", json_object_new_boolean(!strcmp(argv[3], "on")));
		json_object_object_add(args, "who", json_object_new_string("console"));
		rc = cmd_call("freeze", argv[2], args, &res);
		if (res) {
			print(res);
			r = json_object_get_string(json_object_object_get(res, "result"));
			rc = rc || !r || (strcmp(r, "done") && strcmp(r, "unchanged"));
			json_object_put(res);
		}
		return rc;
	}
	/* a session interface set up outside MedveModem drives the modem: hand it over (mgr_foreign.c) */
	if (!strcmp(argv[1], "handover") && argc == 3) {
		json_object *res = NULL;
		const char *r;
		int rc = cmd_call("handover", argv[2], json_object_new_object(), &res);

		if (res) {
			print(res);
			r = json_object_get_string(json_object_object_get(res, "result"));
			rc = rc || !r || strcmp(r, "started");
			json_object_put(res);
		}
		return rc;
	}
	if (!strcmp(argv[1], "esim") && argc == 4 && !strcmp(argv[2], "status"))
		return cmd_at("esim_status", argv[3], NULL, NULL, 0);
	if (!strcmp(argv[1], "esim") && argc == 4)
		return cmd_at("esim_action", argv[3], "action", argv[2], 0);
	/* esim enable <id> <iccid>: switch to a profile already on the chip */
	if (!strcmp(argv[1], "esim") && argc == 5 && !strcmp(argv[2], "enable")) {
		json_object *args = json_object_new_object();

		json_object_object_add(args, "action", json_object_new_string("enable"));
		json_object_object_add(args, "iccid", json_object_new_string(argv[4]));
		return cmd_call("esim_action", argv[3], args, NULL);
	}
	if (!strcmp(argv[1], "bringup") && (argc == 3 || (argc == 4 && !strcmp(argv[3], "--no-wait"))))
		return cmd_bringup(argv[2], argc == 3);
	if (!strcmp(argv[1], "recover") && argc >= 4)
		return cmd_recover(argc, argv);
	if (!strcmp(argv[1], "sms") || !strcmp(argv[1], "ussd")) {
		int rc = !strcmp(argv[1], "sms") ? cmd_sms(argc, argv) : cmd_ussd(argc, argv);
		if (rc >= 0)
			return rc;
	}
	if (!strcmp(argv[1], "config")) {
		int rc = cmd_config(argc, argv);
		if (rc >= 0)
			return rc;
	}
	/* does the operator touch DNS (spec 25.9.20): the engine runs it per modem, its output in a file */
	/* the engine names the path's round trip last (its own environment never reaches an installed CLI) */
	if (!strcmp(argv[1], "dns-check") && argc >= 4 && !strncmp(argv[argc - 1], "--path-ms=", 10)) {
		const char *v = argv[argc - 1] + 10;

		if (*v && strspn(v, "0123456789") == strlen(v) && strlen(v) < 7)
			setenv("MEDVEMODEM_DNSCHECK_PATH_MS", v, 1);
		argc--;
	}
	if (!strcmp(argv[1], "dns-check") && (argc == 3 || argc == 4)) {
		/* without a server: the one netifd has for the interface on that device (a manual check of any channel) */
		json_object *chans = argc == 4 ? NULL : channels();
		json_object *r = mm_dns_check(argv[2], env_or("MEDVEMODEM_DNSCHECK_LIST", "/usr/share/medvemodem/dnscheck.list"),
					      argc == 4 ? argv[3] : dns_of(chans, argv[2]),
					      (unsigned int)time(NULL) ^ (unsigned int)getpid() << 8);
		puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PLAIN));
		json_object_put(r);
		json_object_put(chans);
		return 0;
	}
	/* the signal from a HiLink or ZTE modem's own web (HANDOFF 164): the engine runs it, the password on stdin */
	if (!strcmp(argv[1], "web-signal") && (argc == 6 || (argc == 7 && !strcmp(argv[6], "fresh")))) {
		char pw[160] = "";
		struct mm_web_args a = { .host = argv[2], .dev = strcmp(argv[3], "-") ? argv[3] : NULL, .kind = argv[4],
					 .password = pw, .state = argv[5], .now = (long long)time(NULL),
					 .fresh = argc == 7 };
		json_object *r;

		if (!isatty(STDIN_FILENO) && fgets(pw, sizeof(pw), stdin))
			pw[strcspn(pw, "\r\n")] = '\0';
		r = mm_web_signal(&a);
		explicit_bzero(pw, sizeof(pw));
		puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PLAIN));
		json_object_put(r);
		return 0;
	}
	/* r94: a HiLink modem's SMS through its web: the password, then for send a JSON line {number, text}, on stdin */
	if (!strcmp(argv[1], "web-sms") && argc >= 7 && argc <= 9) {
		char pw[160] = "", line[8192] = "";
		bool fresh = !strcmp(argv[argc - 1], "fresh");
		struct mm_web_args a = { .host = argv[2], .dev = strcmp(argv[3], "-") ? argv[3] : NULL, .kind = argv[4],
					 .password = pw, .state = argv[5], .now = (long long)time(NULL), .fresh = fresh };
		json_object *r, *req = NULL;
		int index = argc - fresh > 7 ? atoi(argv[7]) : -1;

		if (!isatty(STDIN_FILENO) && fgets(pw, sizeof(pw), stdin)) {
			pw[strcspn(pw, "\r\n")] = '\0';
			if (fgets(line, sizeof(line), stdin))
				req = json_tokener_parse(line);
		}
		r = mm_web_sms(&a, argv[6], json_object_get_string(json_object_object_get(req, "number")),
			       json_object_get_string(json_object_object_get(req, "text")), index);
		explicit_bzero(pw, sizeof(pw));
		explicit_bzero(line, sizeof(line));
		json_object_put(req);
		puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PLAIN));
		json_object_put(r);
		return 0;
	}
	/* r117: more of a HiLink's web: info, ussd (a JSON line {code} after the password), reboot */
	if (!strcmp(argv[1], "web-act") && (argc == 7 || (argc == 8 && !strcmp(argv[7], "fresh")))) {
		char pw[160] = "", line[512] = "";
		struct mm_web_args a = { .host = argv[2], .dev = strcmp(argv[3], "-") ? argv[3] : NULL, .kind = argv[4],
					 .password = pw, .state = argv[5], .now = (long long)time(NULL), .fresh = argc == 8 };
		json_object *r, *req = NULL;

		if (!isatty(STDIN_FILENO) && fgets(pw, sizeof(pw), stdin)) {
			pw[strcspn(pw, "\r\n")] = '\0';
			if (fgets(line, sizeof(line), stdin))
				req = json_tokener_parse(line);
		}
		r = mm_web_act(&a, argv[6], json_object_get_string(json_object_object_get(req, "code")));
		explicit_bzero(pw, sizeof(pw));
		json_object_put(req);
		puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PLAIN));
		json_object_put(r);
		return 0;
	}
	if (!strcmp(argv[1], "channels") && argc == 2) {
		json_object *c = channels();
		puts(json_object_to_json_string_ext(c, JSON_C_TO_STRING_PLAIN));
		json_object_put(c);
		return 0;
	}
	if (!strcmp(argv[1], "diag")) {
		int rc = cmd_diag(argc, argv);
		if (rc >= 0)
			return rc;
	}

	usage(stderr);
	return 2;
}
