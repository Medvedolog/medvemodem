// SPDX-License-Identifier: GPL-3.0-only
#include "chain.h"

#include <stdbool.h>
#include <string.h>

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

static bool is(const char *a, const char *b)
{
	return a && b && !strcmp(a, b);
}

/* the first string of an array */
static const char *first(json_object *a)
{
	json_object *v = a && json_object_is_type(a, json_type_array) && json_object_array_length(a) ? json_object_array_get_idx(a, 0) : NULL;
	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static const char *const LINKS[] = { "device", "network", "session", "internet", "dns", NULL };

/* the links after the broken one are skipped; the ones before it are ok unless said otherwise */
static json_object *verdict(const char *link, const char *reason, const char *detail, const char *states[5], bool standby)
{
	json_object *o = json_object_new_object(), *links = json_object_new_array();
	bool after = false;
	int i;

	for (i = 0; LINKS[i]; i++) {
		json_object *l = json_object_new_object();
		const char *st = states[i];

		if (after)
			st = "skipped";
		else if (link && !strcmp(LINKS[i], link))
			st = "fail", after = true;
		json_object_object_add(l, "link", json_object_new_string(LINKS[i]));
		json_object_object_add(l, "state", json_object_new_string(st ? st : "unknown"));
		json_object_array_add(links, l);
	}
	json_object_object_add(o, "ok", json_object_new_boolean(!link));
	json_object_object_add(o, "link", link ? json_object_new_string(link) : NULL);
	json_object_object_add(o, "reason", reason ? json_object_new_string(reason) : NULL);
	json_object_object_add(o, "detail", detail && *detail ? json_object_new_string(detail) : NULL);
	json_object_object_add(o, "standby", json_object_new_boolean(standby));
	json_object_object_add(o, "links", links);
	return o;
}

json_object *mm_chain(json_object *rec)
{
	json_object *mm = get(rec, "modemmanager"), *sess = get(rec, "session"), *up = get(rec, "uplink");
	json_object *reach = get(up, "reach"), *dns = get(up, "dns"), *present = get(rec, "present");
	const char *conf = str(get(rec, "configuration"), "state"), *reg = str(mm, "registration"), *fail = str(mm, "failed_reason");
	const char *state = str(sess, "state"), *role = str(up, "role");
	const char *states[5] = { "ok", NULL, NULL, NULL, NULL };
	bool connected = is(state, "connected");

	/* 1. the device: on the bus, in a usable composition */
	if (!present || !json_object_get_boolean(present))
		return verdict("device", "absent", NULL, states, false);
	if (is(conf, "unbound") || is(conf, "incomplete"))
		return verdict("device", "composition", first(get(get(rec, "configuration"), "evidence")), states, false);
	if (is(conf, "service-mode") || is(conf, "modeswitch-rule"))
		return verdict("device", conf, first(get(get(rec, "configuration"), "evidence")), states, false);

	/* 2. SIM and network: what ModemManager says, when it holds the modem; otherwise a session vouches for it */
	if (is(fail, "sim-missing"))
		return verdict("network", "sim-missing", NULL, states, false);
	if (is(str(mm, "state"), "locked") || (str(mm, "unlock_required") && strcmp(str(mm, "unlock_required"), "none") &&
					       strcmp(str(mm, "unlock_required"), "sim-pin2")))
		return verdict("network", "sim-locked", str(mm, "unlock_required"), states, false);
	if (is(reg, "denied"))
		return verdict("network", "registration-denied", NULL, states, false);
	if (!connected && (is(reg, "searching") || is(reg, "idle") || is(reg, "unknown")))
		return verdict("network", is(reg, "searching") ? "searching" : "not-registered", NULL, states, false);
	states[1] = connected || is(reg, "home") || is(reg, "roaming") ? "ok" : NULL;

	/* 3. the session */
	if (!connected)
		return verdict("session", is(state, "failed") ? "session-failed" : "no-session",
			       str(sess, "reason") ? str(sess, "reason") : first(get(get(sess, "netifd"), "errors")), states, false);
	states[2] = "ok";

	/* 4. the internet through this modem's own interface; not yet checked is unknown, not broken */
	if (reach && get(reach, "ok") && !json_object_get_boolean(get(reach, "ok"))) {
		bool limited = json_object_get_boolean(get(reach, "limited"));

		/* plain HTTP goes to the operator's own page (r156: a Tele2 SIM with no money, internet-block.t2.ru) */
		if (str(reach, "redirect"))
			return verdict("internet", "restricted", str(reach, "redirect"), states, false);
		return verdict("internet", limited ? "whitelist" : "no-answer", str(reach, "target"), states, false);
	}
	states[3] = reach && json_object_get_boolean(get(reach, "ok")) ? "ok" : NULL;

	/* 5. DNS, checked on the router's resolver while this modem carries the traffic */
	if (dns && get(dns, "ok") && !json_object_get_boolean(get(dns, "ok")))
		return verdict("dns", "dns-failed", str(dns, "error"), states, false);
	states[4] = dns && json_object_get_boolean(get(dns, "ok")) ? "ok" : NULL;

	/* the operator touches DNS (dns-check): the link works, so a warning, never a broken link */
	{
		json_object *chk = get(up, "dns_check"), *o;
		const char *why = is(str(get(chk, "intercept"), "state"), "intercepted") ? "dns-intercepted" :
				  is(str(get(chk, "blocked"), "state"), "stub") ? "dns-stub" :
				  is(str(get(chk, "nxdomain"), "state"), "rewritten") ? "dns-rewritten" : NULL;
		const char *part = !why ? NULL : !strcmp(why, "dns-intercepted") ? "intercept" : !strcmp(why, "dns-stub") ? "blocked" : "nxdomain";

		o = verdict(NULL, NULL, NULL, states, role && !strncmp(role, "backup", 6));
		if (why) {
			json_object *w = json_object_new_object(), *l = json_object_array_get_idx(get(o, "links"), 4);

			json_object_object_add(w, "link", json_object_new_string("dns"));
			json_object_object_add(w, "reason", json_object_new_string(why));
			if (str(get(chk, part), "detail"))
				json_object_object_add(w, "detail", json_object_new_string(str(get(chk, part), "detail")));
			json_object_object_add(o, "warning", w);
			json_object_object_add(l, "state", json_object_new_string("warn"));
		}
		return o;
	}
}
