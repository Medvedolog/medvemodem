#define _GNU_SOURCE
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "diag.h"

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

static json_object *copy(json_object *v)
{
	return v ? json_object_get(v) : NULL;
}

static const char *bare(const char *node)
{
	return strncmp(node, "/dev/", 5) ? node : node + 5;
}

/* ---- composition (spec 32.3) ---- */

static json_object *role_of(json_object *rec, const char *node)
{
	json_object *cands = get(get(rec, "port_roles"), "candidates");
	size_t i;

	for (i = 0; cands && i < json_object_array_length(cands); i++) {
		json_object *c = json_object_array_get_idx(cands, i);
		if (str(c, "node") && !strcmp(bare(str(c, "node")), bare(node)))
			return c;
	}
	return NULL;
}

json_object *mm_diag_composition(json_object *rec)
{
	json_object *o = json_object_new_object(), *ifs = json_object_new_array(), *src = get(rec, "interfaces");
	json_object *map = get(rec, "port_interfaces"), *mm = get(rec, "modemmanager"), *mo = json_object_new_object();
	size_t i;

	json_object_object_add(o, "schema", json_object_new_int(1));
	json_object_object_add(o, "bus", copy(get(rec, "bus")));
	json_object_object_add(o, "attachment", copy(get(rec, "attachment_id")));
	json_object_object_add(o, "vidpid", copy(get(rec, "vidpid")));
	json_object_object_add(o, "manufacturer", copy(get(rec, "manufacturer")));
	json_object_object_add(o, "product", copy(get(rec, "product")));
	json_object_object_add(o, "usb_configuration", copy(get(rec, "usb_configuration")));
	for (i = 0; src && i < json_object_array_length(src); i++) {
		json_object *it = json_object_array_get_idx(src, i), *x = json_object_new_object(), *nodes = json_object_new_array();
		const char *name = str(it, "sysfs");

		json_object_object_add(x, "number", copy(get(it, "number")));
		json_object_object_add(x, "class", copy(get(it, "class")));
		json_object_object_add(x, "subclass", copy(get(it, "subclass")));
		json_object_object_add(x, "protocol", copy(get(it, "protocol")));
		json_object_object_add(x, "driver", copy(get(it, "driver")));
		if (map && name) {
			json_object_object_foreach(map, node, iface) {
				if (strcmp(json_object_get_string(iface), name))
					continue;
				json_object *n = json_object_new_object(), *c = role_of(rec, node);
				json_object_object_add(n, "node", json_object_new_string(node));
				json_object_object_add(n, "role", c ? copy(get(c, "role")) : NULL);
				json_object_object_add(n, "role_source", c ? copy(get(c, "source")) : NULL);
				json_object_array_add(nodes, n);
			}
		}
		json_object_object_add(x, "nodes", nodes);
		json_object_array_add(ifs, x);
	}
	json_object_object_add(o, "interfaces", ifs);

	json_object_object_add(mo, "present", json_object_new_boolean(mm != NULL));
	if (mm) {
		json_object *ports = json_object_new_array(), *types = get(mm, "port_types");
		json_object_object_add(mo, "object", copy(get(mm, "object")));
		json_object_object_add(mo, "mapping", copy(get(mm, "mapping")));
		json_object_object_add(mo, "plugin", copy(get(mm, "plugin")));
		json_object_object_add(mo, "state", copy(get(mm, "state")));
		json_object_object_add(mo, "failed_reason", copy(get(mm, "failed_reason")));
		json_object_object_add(mo, "primary_port", copy(get(mm, "primary_port")));
		json_object_object_add(mo, "model", copy(get(mm, "model")));
		json_object_object_add(mo, "revision", copy(get(mm, "revision")));
		json_object_object_add(mo, "drivers", copy(get(mm, "drivers")));
		if (types) {
			json_object_object_foreach(types, port, type) {
				json_object *p = json_object_new_object();
				json_object_object_add(p, "port", json_object_new_string(port));
				json_object_object_add(p, "type", json_object_get(type));
				json_object_array_add(ports, p);
			}
		}
		json_object_object_add(mo, "ports", ports);
	}
	json_object_object_add(o, "mm", mo);
	json_object_object_add(o, "detected_profile", copy(get(get(rec, "profile"), "id")));
	return o;
}

/* ---- bundle (spec 32.2) ---- */

json_object *mm_diag_bundle(json_object *rec, json_object *events, json_object *system, json_object *klog,
			    json_object *engine, const char *label, long long now)
{
	json_object *o = json_object_new_object(), *dev = json_object_new_object(), *dec = json_object_new_object();

	json_object_object_add(o, "schema", json_object_new_int(1));
	json_object_object_add(o, "kind", json_object_new_string("medvemodem-diag-profile"));
	json_object_object_add(o, "label", label ? json_object_new_string(label) : NULL);
	json_object_object_add(o, "generated", json_object_new_int64(now));
	json_object_object_add(o, "engine", copy(engine));
	json_object_object_add(o, "system", copy(system));

	json_object_object_add(dev, "device_id", copy(get(rec, "device_id")));
	json_object_object_add(dev, "serial", copy(get(rec, "serial")));	/* dropped by sanitizing */
	json_object_object_add(dev, "identity_strength", copy(get(rec, "identity_strength")));
	json_object_object_add(dev, "port_id", copy(get(rec, "port_id")));
	json_object_object_add(dev, "usb_port", copy(get(rec, "usb_port")));
	json_object_object_add(dev, "generation", copy(get(rec, "generation")));
	json_object_object_add(dev, "catalog_known", copy(get(rec, "catalog_known")));
	json_object_object_add(o, "device", dev);
	json_object_object_add(o, "composition", mm_diag_composition(rec));

	/* the engine's own reasoning (spec 32.10) */
	json_object_object_add(dec, "detection_status", copy(get(rec, "detection_status")));
	json_object_object_add(dec, "configuration", copy(get(rec, "configuration")));
	json_object_object_add(dec, "profile", copy(get(rec, "profile")));
	json_object_object_add(dec, "owner", copy(get(rec, "owner")));
	json_object_object_add(dec, "port_roles", copy(get(rec, "port_roles")));
	json_object_object_add(dec, "stability", copy(get(rec, "stability")));
	/* ModemManager's words about a port it cannot drive: a second master on the modem (mgr_mmlog.c) */
	if (get(rec, "mm_log"))
		json_object_object_add(dec, "mm_log", copy(get(rec, "mm_log")));
	/* a program outside the engine's stack holding this modem's port (mgr_neighbours.c) */
	if (get(rec, "port_holders"))
		json_object_object_add(dec, "port_holders", copy(get(rec, "port_holders")));
	json_object_object_add(o, "decision", dec);
	json_object_object_add(o, "history", events ? copy(events) : json_object_new_array());
	json_object_object_add(o, "kernel_log", klog ? copy(klog) : json_object_new_array());
	return o;
}

/* ---- kernel log slice ---- */

static bool word_char(char c)
{
	return isalnum((unsigned char)c) || c == '-' || c == '_';
}

/* name occurs as its own token; for a USB name "1-1", "1-1:1.0" counts, "1-1.2" does not */
static bool mentions(const char *line, const char *name, bool usb)
{
	size_t n = strlen(name);
	const char *p = line;

	if (!n)
		return false;
	while ((p = strstr(p, name))) {
		char before = p == line ? ' ' : p[-1], after = p[n];
		bool ok = !word_char(before) && before != '.';

		if (usb)
			ok = ok && (after == ':' || after == ' ' || after == ',' || after == '\0' || after == '\n' ||
				    (after == '.' && !isdigit((unsigned char)p[n + 1])));
		else
			ok = ok && !word_char(after);
		if (ok)
			return true;
		p += n;
	}
	return false;
}

json_object *mm_diag_klog_filter(const char *text, json_object *rec, int max)
{
	json_object *all = json_object_new_array(), *out = json_object_new_array();
	json_object *nodes = json_object_new_array(), *ports = get(rec, "ports");
	const char *att = str(rec, "attachment_id"), *usb = att && !strncmp(att, "usb:", 4) ? att + 4 : NULL;
	const char *p = text;
	size_t i, k, n;

	static const char *const kinds[] = { "tty", "control", "net" };
	for (k = 0; k < 3; k++) {
		json_object *a = get(ports, kinds[k]);
		for (i = 0; a && i < json_object_array_length(a); i++)
			json_object_array_add(nodes, json_object_new_string(bare(json_object_get_string(json_object_array_get_idx(a, i)))));
	}
	while (p && *p) {
		const char *end = strchrnul(p, '\n');
		char *line = strndup(p, (size_t)(end - p));
		bool hit = usb && mentions(line, usb, true);

		for (i = 0; !hit && i < json_object_array_length(nodes); i++)
			hit = mentions(line, json_object_get_string(json_object_array_get_idx(nodes, i)), false);
		if (hit)
			json_object_array_add(all, json_object_new_string(line));
		free(line);
		p = *end ? end + 1 : end;
	}
	n = json_object_array_length(all);
	for (i = n > (size_t)max ? n - (size_t)max : 0; i < n; i++)
		json_object_array_add(out, json_object_get(json_object_array_get_idx(all, i)));
	json_object_put(all);
	json_object_put(nodes);
	return out;
}

/* ---- sanitizing (spec 32.5) ---- */

static uint64_t fnv1a(const char *a, const char *b)
{
	uint64_t h = 1469598103934665603ULL;
	const char *s;

	for (s = a; s && *s; s++)
		h = (h ^ (unsigned char)*s) * 1099511628211ULL;
	h = (h ^ 0xff) * 1099511628211ULL;	/* separator */
	for (s = b; s && *s; s++)
		h = (h ^ (unsigned char)*s) * 1099511628211ULL;
	return h;
}

struct scrub {
	const char *serial;
	char token[24];		/* "~" + 12 hex */
	int removed, replaced;
};

static const char *const secret_keys[] = {
	"serial", "imei", "imsi", "iccid", "msisdn", "equipment-identifier", "equipment_identifier",
	"own-numbers", "own_numbers", "pin", "pincode", "puk", "password", "web_password", "imei_key", "sim_key", "apn_password", "username", "psk", "key", "token", NULL
};

static bool secret(const char *key)
{
	size_t i;

	for (i = 0; secret_keys[i]; i++)
		if (!strcasecmp(key, secret_keys[i]))
			return true;
	return false;
}

/* Replace the serial and long digit runs; NULL when nothing changed. */
static char *scrub_text(const char *s, struct scrub *sc)
{
	/* worst case: a 4-character serial becomes a 13-character token */
	size_t cap = strlen(s) * 4 + 64, o = 0, slen = sc->serial ? strlen(sc->serial) : 0;
	char *out = malloc(cap);
	bool changed = false;
	const char *p = s;

	if (!out)
		return NULL;
	while (*p) {
		/* kernel "SerialNumber: xyz" of any USB device */
		if (!strncmp(p, "SerialNumber: ", 14) && p[14] && !isspace((unsigned char)p[14])) {
			o += (size_t)snprintf(out + o, cap - o, "SerialNumber: [serial]");
			p += 14;
			p += strcspn(p, " \t\n");
			changed = true;
			continue;
		}
		if (slen && !strncmp(p, sc->serial, slen)) {
			o += (size_t)snprintf(out + o, cap - o, "%s", sc->token);
			p += slen;
			changed = true;
			continue;
		}
		/* a link's path and query: a webhook's or an ntfy topic's secret, a bot token (a field report elsewhere,
		 * 2026-11-11: diagnostic bundles leaked webhook URLs); the scheme and the host stay */
		if (!strncmp(p, "http://", 7) || !strncmp(p, "https://", 8)) {
			size_t sch = p[4] == 's' ? 8 : 7, host = strcspn(p + sch, "/?#\" '<> \t\n");
			const char *end = p + sch + host;

			const char *at = memrchr(p + sch, '@', host);

			/* user:password@ before the host (a webhook with basic auth, review 2026-11-15): hidden too */
			if (at) {
				memcpy(out + o, p, sch);
				o += sch;
				o += (size_t)snprintf(out + o, cap - o, "[hidden]@");
				memcpy(out + o, at + 1, (size_t)(end - at - 1));
				o += (size_t)(end - at - 1);
				changed = true;
			} else {
				memcpy(out + o, p, sch + host);
				o += sch + host;
			}
			p = end;
			if (*p == '/' || *p == '?' || *p == '#') {
				size_t rest = strcspn(p, "\" '<> \t\n");

				if (rest > 1) {
					o += (size_t)snprintf(out + o, cap - o, "/[hidden]");
					changed = true;
				} else {
					out[o++] = *p;
				}
				p += rest > 1 ? rest : 1;
			}
			continue;
		}
		if (isdigit((unsigned char)*p) && (p == s || !isdigit((unsigned char)p[-1]))) {
			size_t run = strspn(p, "0123456789");
			bool plus = p > s && p[-1] == '+';
			const char *tag = NULL;

			if (plus && run >= 10 && run <= 15)
				tag = "[phone]";
			else if (run >= 18 && run <= 20 && p[0] == '8' && p[1] == '9')
				tag = "[iccid]";	/* 18 digits too: umbim prints them padded with F (a Nokia report, 2026-10-27) */
			else if (run == 15)
				tag = "[imei-or-imsi]";
			if (tag) {
				if (plus && o > 0)
					o--;	/* drop the '+' already copied */
				o += (size_t)snprintf(out + o, cap - o, "%s", tag);
				p += run;
				if (tag[1] == 'i' && tag[2] == 'c')
					while (*p == 'F' || *p == 'f')
						p++;
				changed = true;
				continue;
			}
			memcpy(out + o, p, run);
			o += run;
			p += run;
			continue;
		}
		out[o++] = *p++;
	}
	out[o] = '\0';
	if (!changed) {
		free(out);
		return NULL;
	}
	sc->replaced++;
	return out;
}

static void walk(json_object *o, struct scrub *sc)
{
	size_t i;

	if (json_object_is_type(o, json_type_object)) {
		json_object *drop = json_object_new_array();

		{
			json_object_object_foreach(o, key, val) {
				if (secret(key)) {
					json_object_array_add(drop, json_object_new_string(key));
					continue;
				}
				if (json_object_is_type(val, json_type_string)) {
					char *t = scrub_text(json_object_get_string(val), sc);
					if (t) {
						json_object_object_add(o, key, json_object_new_string(t));
						free(t);
					}
				} else {
					walk(val, sc);
				}
			}
		}
		for (i = 0; i < json_object_array_length(drop); i++) {
			json_object_object_del(o, json_object_get_string(json_object_array_get_idx(drop, i)));
			sc->removed++;
		}
		json_object_put(drop);
	} else if (json_object_is_type(o, json_type_array)) {
		for (i = 0; i < json_object_array_length(o); i++) {
			json_object *v = json_object_array_get_idx(o, i);
			if (json_object_is_type(v, json_type_string)) {
				char *t = scrub_text(json_object_get_string(v), sc);
				if (t) {
					json_object_array_put_idx(o, i, json_object_new_string(t));
					free(t);
				}
			} else {
				walk(v, sc);
			}
		}
	}
}

/* the USB serial: the device's own field, or the tail of a usb-serial: id */
static const char *find_serial(json_object *bundle)
{
	const char *s = str(get(bundle, "device"), "serial"), *id = str(get(bundle, "device"), "device_id");

	if (s && *s)
		return s;
	if (id && !strncmp(id, "usb-serial:", 11) && id[11] != '~')
		return id + 11;
	return NULL;
}

void mm_diag_sanitize(json_object *bundle, const char *salt)
{
	struct scrub sc = { 0 };
	json_object *r = json_object_new_object(), *keys = json_object_new_array();
	char *serial = NULL;
	size_t i;

	if (find_serial(bundle))
		serial = strdup(find_serial(bundle));
	sc.serial = serial && strlen(serial) >= 4 ? serial : NULL;
	snprintf(sc.token, sizeof(sc.token), "~%012llx",
		 (unsigned long long)(fnv1a(salt, sc.serial ? sc.serial : "") & 0xffffffffffffULL));
	walk(bundle, &sc);
	for (i = 0; secret_keys[i]; i++)
		json_object_array_add(keys, json_object_new_string(secret_keys[i]));
	json_object_object_add(r, "removed_keys", keys);
	json_object_object_add(r, "removed", json_object_new_int(sc.removed));
	json_object_object_add(r, "texts_changed", json_object_new_int(sc.replaced));
	json_object_object_add(r, "serial", json_object_new_string(sc.serial ? "replaced by a salted hash (salt stays on the router)" : "none"));
	json_object_object_add(r, "patterns", json_object_new_string("15-digit (IMEI/IMSI), ICCID-like and +phone numbers replaced"));
	json_object_object_add(bundle, "redaction", r);
	free(serial);
}

/* ---- diff (spec 32.4) ---- */

static bool same(json_object *a, json_object *b)
{
	const char *x = a ? json_object_to_json_string_ext(a, JSON_C_TO_STRING_PLAIN) : "null";
	char *xs = strdup(x);
	bool eq = !strcmp(xs, b ? json_object_to_json_string_ext(b, JSON_C_TO_STRING_PLAIN) : "null");

	free(xs);
	return eq;
}

static void change(json_object *out, const char *kind, const char *iface, json_object *before, json_object *after)
{
	json_object *c = json_object_new_object();

	json_object_object_add(c, "kind", json_object_new_string(kind));
	if (iface)
		json_object_object_add(c, "interface", json_object_new_string(iface));
	json_object_object_add(c, "before", copy(before));
	json_object_object_add(c, "after", copy(after));
	json_object_array_add(out, c);
}

static json_object *iface_by_number(json_object *ifs, const char *num)
{
	size_t i;

	for (i = 0; ifs && i < json_object_array_length(ifs); i++) {
		json_object *x = json_object_array_get_idx(ifs, i);
		if (str(x, "number") && !strcmp(str(x, "number"), num))
			return x;
	}
	return NULL;
}

static json_object *class_of(json_object *x)
{
	char buf[16];

	snprintf(buf, sizeof(buf), "%s/%s/%s", str(x, "class") ? str(x, "class") : "?",
		 str(x, "subclass") ? str(x, "subclass") : "?", str(x, "protocol") ? str(x, "protocol") : "?");
	return json_object_new_string(buf);
}

/* nodes as "role" list without node numbers: numbering is not a change */
static json_object *roles_of(json_object *x)
{
	json_object *nodes = get(x, "nodes"), *out = json_object_new_array();
	size_t i;

	for (i = 0; nodes && i < json_object_array_length(nodes); i++) {
		const char *r = str(json_object_array_get_idx(nodes, i), "role");
		json_object_array_add(out, json_object_new_string(r ? r : "unknown"));
	}
	return out;
}

json_object *mm_diag_diff(json_object *a, json_object *b)
{
	json_object *ca = get(a, "composition"), *cb = get(b, "composition"), *out = json_object_new_array();
	json_object *ia = get(ca, "interfaces"), *ib = get(cb, "interfaces"), *ma = get(ca, "mm"), *mb = get(cb, "mm");
	static const char *const top[] = { "vidpid", "usb_configuration", "product", "detected_profile" };
	static const char *const mmk[] = { "present", "plugin", "state", "primary_port" };
	size_t i;

	for (i = 0; i < sizeof(top) / sizeof(top[0]); i++)
		if (!same(get(ca, top[i]), get(cb, top[i])))
			change(out, top[i], NULL, get(ca, top[i]), get(cb, top[i]));
	for (i = 0; ia && i < json_object_array_length(ia); i++) {
		json_object *x = json_object_array_get_idx(ia, i), *y;
		const char *num = str(x, "number");
		json_object *k1, *k2, *r1, *r2;

		if (!num)
			continue;
		y = iface_by_number(ib, num);
		k1 = class_of(x);
		if (!y) {
			change(out, "interface-removed", num, k1, NULL);
			json_object_put(k1);
			continue;
		}
		k2 = class_of(y);
		if (!same(k1, k2))
			change(out, "class", num, k1, k2);
		if (!same(get(x, "driver"), get(y, "driver")))
			change(out, "driver", num, get(x, "driver"), get(y, "driver"));
		r1 = roles_of(x);
		r2 = roles_of(y);
		if (!same(r1, r2))
			change(out, "roles", num, r1, r2);
		json_object_put(k1);
		json_object_put(k2);
		json_object_put(r1);
		json_object_put(r2);
	}
	for (i = 0; ib && i < json_object_array_length(ib); i++) {
		json_object *y = json_object_array_get_idx(ib, i), *k;
		const char *num = str(y, "number");

		if (num && !iface_by_number(ia, num)) {
			k = class_of(y);
			change(out, "interface-added", num, NULL, k);
			json_object_put(k);
		}
	}
	for (i = 0; i < sizeof(mmk) / sizeof(mmk[0]); i++)
		if (!same(get(ma, mmk[i]), get(mb, mmk[i]))) {
			char kind[32];
			snprintf(kind, sizeof(kind), "modemmanager.%s", mmk[i]);
			change(out, kind, NULL, get(ma, mmk[i]), get(mb, mmk[i]));
		}
	return out;
}

/* ---- quick ---- */

json_object *mm_diag_quick(json_object *records)
{
	json_object *out = json_object_new_array();
	size_t i;

	for (i = 0; records && i < json_object_array_length(records); i++) {
		json_object *r = json_object_array_get_idx(records, i), *row = json_object_new_object();

		json_object_object_add(row, "device_id", copy(get(r, "device_id")));
		json_object_object_add(row, "product", copy(get(r, "product")));
		json_object_object_add(row, "vidpid", copy(get(r, "vidpid")));
		json_object_object_add(row, "detection", copy(get(r, "detection_status")));
		json_object_object_add(row, "configuration", copy(get(get(r, "configuration"), "state")));
		json_object_object_add(row, "owner", copy(get(get(r, "owner"), "control")));
		json_object_object_add(row, "at_port", copy(get(get(get(r, "port_roles"), "at"), "node")));
		json_object_object_add(row, "stability", copy(get(get(r, "stability"), "state")));
		json_object_object_add(row, "modemmanager", copy(get(get(r, "modemmanager"), "state")));
		json_object_array_add(out, row);
	}
	return out;
}
