#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esim.h"
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

static bool eq(const char *a, const char *b)
{
	return a && b && !strcmp(a, b);
}

static bool has(json_object *list, const char *name)
{
	size_t i;

	for (i = 0; list && json_object_is_type(list, json_type_array) && i < json_object_array_length(list); i++)
		if (eq(json_object_get_string(json_object_array_get_idx(list, i)), name))
			return true;
	return false;
}

static void fail(json_object *o, const char *code, const char *reason)
{
	json_object_object_add(o, "error", json_object_new_string(code));
	json_object_object_add(o, "reason", json_object_new_string(reason));
}

/*
 * Which APDU paths the current owner leaves free (spec 12.2):
 * - nobody holds the control port (owner unresolved): qmi/uqmi/mbim directly;
 * - ModemManager: only MBIM through mbim-proxy, which ModemManager itself uses;
 *   QMI needs an owner handoff that this stage does not do;
 * - native netifd QMI/MBIM: the session holds the control port: none;
 * - QModem: nothing, its resources are QModem's;
 * - AT: when the engine resolved the AT port as its own, under a tty lease.
 */
json_object *mm_esim_target(json_object *srv, json_object *lpac, json_object *settings)
{
	json_object *o = json_object_new_object(), *res = json_object_new_object(), *cands = json_object_new_array();
	json_object *allowed = json_object_new_array(), *owner = get(srv, "owner");
	json_object *ctl = get(get(srv, "port_roles"), "control"), *at = get(get(srv, "port_roles"), "at");
	json_object *drivers = get(lpac, "drivers"), *mm = get(srv, "modemmanager");
	const char *control = str(owner, "control"), *atown = str(owner, "at"), *drv = str(ctl, "driver");
	const char *qmi = mm_is_qmi_driver(drv) ? str(ctl, "node") : NULL, *mbim = eq(drv, "cdc_mbim") ? str(ctl, "node") : NULL;
	const char *atnode = eq(atown, "medvemodem") ? str(at, "node") : NULL;
	json_object *atports = json_object_new_array(), *roles = get(get(srv, "port_roles"), "candidates");
	const char *vidpid = str(srv, "vidpid");
	const char *choice = str(settings, "backend") && *str(settings, "backend") ? str(settings, "backend") : "auto";
	const char *slot = str(settings, "apdu_slot");
	bool proxy = false, mm_owner = eq(control, "modemmanager");
	size_t i;

	json_object_object_add(o, "device_id", json_object_get(get(srv, "device_id")));
	json_object_object_add(o, "generation", json_object_get(get(srv, "generation")));
	if (str(mm, "manufacturer") || str(srv, "manufacturer"))
		json_object_object_add(o, "vendor", json_object_new_string(str(mm, "manufacturer") ? str(mm, "manufacturer")
												     : str(srv, "manufacturer")));
	if (str(mm, "model") || str(srv, "product"))
		json_object_object_add(o, "model", json_object_new_string(str(mm, "model") ? str(mm, "model") : str(srv, "product")));
	json_object_object_add(o, "owner", json_object_get(owner));
	json_object_object_add(res, "qmi", qmi ? json_object_new_string(qmi) : NULL);
	json_object_object_add(res, "mbim", mbim ? json_object_new_string(mbim) : NULL);
	/*
	 * Which tty reaches the eUICC differs per firmware (FM350: one port of
	 * several, renumbered on re-enumeration): every AT port the engine owns is a
	 * candidate, its chosen AT port first; a CCHO probe picks one. Sierra
	 * modules hang their SIM interface on CCHO and reach the eUICC over QMI/MBIM.
	 */
	if (atnode && !(vidpid && !strncmp(vidpid, "1199:", 5))) {
		json_object_array_add(atports, json_object_new_string(atnode));
		for (i = 0; roles && json_object_is_type(roles, json_type_array) && i < json_object_array_length(roles); i++) {
			json_object *c = json_object_array_get_idx(roles, i);
			const char *n = str(c, "node");

			if (n && eq(str(c, "kind"), "tty") && eq(str(c, "role"), "at") && eq(str(c, "owner"), "none") &&
			    !has(atports, n))
				json_object_array_add(atports, json_object_new_string(n));
		}
	}
	json_object_object_add(res, "at", atnode ? json_object_new_string(atnode) : NULL);
	json_object_object_add(o, "resources", res);
	json_object_object_add(o, "apdu_slot", json_object_new_int(slot && atoi(slot) > 0 ? atoi(slot) : 1));

	if (eq(control, "qmodem")) {
		fail(o, "owner_busy", "QModem owns this modem's control and AT resources");
		goto done;
	}
	if (!str(srv, "device_id") || !json_object_get_boolean(get(srv, "present"))) {
		fail(o, "transport_unavailable", "the modem is not attached");
		goto done;
	}
	/* candidates in order of preference: control channel first, AT last */
	if (qmi) {
		bool free_port = eq(control, "unresolved");
		if (free_port) {
			json_object_array_add(cands, json_object_new_string("qmi"));
			json_object_array_add(cands, json_object_new_string("uqmi"));
		}
	}
	if (mbim && (eq(control, "unresolved") || mm_owner)) {
		json_object_array_add(cands, json_object_new_string("mbim"));
		proxy = mm_owner;	/* share the channel ModemManager already holds */
	}
	if (json_object_array_length(atports))
		json_object_array_add(cands, json_object_new_string("at"));
	json_object_object_add(o, "at_ports", json_object_get(atports));
	json_object_object_add(o, "candidates", json_object_get(cands));

	for (i = 0; i < json_object_array_length(cands); i++) {
		const char *c = json_object_get_string(json_object_array_get_idx(cands, i));
		/*
		 * An unknown lpac build is not second-guessed; a known one must carry the
		 * driver. AT goes through the engine's bridge: lpac needs its stdio driver.
		 */
		if (!drivers || has(drivers, eq(c, "at") ? "stdio" : c))
			json_object_array_add(allowed, json_object_new_string(c));
	}
	json_object_object_add(o, "allowed_apdu", json_object_get(allowed));
	json_object_object_add(o, "mbim_proxy", json_object_new_boolean(proxy));

	if (!json_object_array_length(cands)) {
		if (mm_owner && qmi)
			fail(o, "owner_busy", "ModemManager holds the QMI control port; QMI APDU needs an owner handoff "
					      "(operation reservation, next stage)");
		else if (eq(control, "netifd-qmi") || eq(control, "netifd-mbim"))
			fail(o, "owner_busy", "the native session holds the control port and no engine AT port is free");
		else if (eq(control, "hilink"))
			fail(o, "transport_unavailable", "a router-mode modem exposes no control or AT channel");
		else
			fail(o, "transport_unavailable", atnode ? "logical-channel AT commands hang this vendor's SIM interface; "
							    "its eUICC is reached over QMI or MBIM"
							  : "no QMI, MBIM or engine-owned AT resource on this modem");
		goto done;
	}
	if (!json_object_array_length(allowed)) {
		fail(o, "driver_unavailable", "the installed lpac has no driver for this modem's transports");
		goto done;
	}
	if (strcmp(choice, "auto")) {
		if (!has(allowed, choice)) {
			fail(o, has(cands, choice) ? "driver_unavailable" : "transport_unavailable",
			     "the configured eSIM backend is not usable on this modem now");
			goto done;
		}
		json_object_object_add(o, "selected", json_object_new_string(choice));
	} else {
		/* native libqmi build: qmi before uqmi; otherwise the list order */
		const char *pick = json_object_get_string(json_object_array_get_idx(allowed, 0));
		if (eq(pick, "qmi") && has(allowed, "uqmi") && !eq(str(lpac, "build"), "native-qmi"))
			pick = "uqmi";
		json_object_object_add(o, "selected", json_object_new_string(pick));
	}
	if (str(o, "selected") && strcmp(str(o, "selected"), "mbim"))
		json_object_object_add(o, "mbim_proxy", json_object_new_boolean(false));
done:
	/* a refusal before the candidates still carries the fields esim.h promises, empty (review 2026-11-15) */
	if (!get(o, "at_ports"))
		json_object_object_add(o, "at_ports", json_object_new_array());
	if (!get(o, "candidates"))
		json_object_object_add(o, "candidates", json_object_new_array());
	if (!get(o, "allowed_apdu"))
		json_object_object_add(o, "allowed_apdu", json_object_new_array());
	if (!get(o, "mbim_proxy"))
		json_object_object_add(o, "mbim_proxy", json_object_new_boolean(false));
	json_object_put(atports);
	json_object_put(cands);
	json_object_put(allowed);
	return o;
}

static json_object *result(bool ok)
{
	json_object *o = json_object_new_object();
	json_object_object_add(o, "ok", json_object_new_boolean(ok));
	return o;
}

static json_object *error(const char *code, const char *detail)
{
	json_object *o = result(false);

	json_object_object_add(o, "error", json_object_new_string(code));
	if (detail && *detail)
		json_object_object_add(o, "detail", json_object_new_string(detail));
	return o;
}

/* the last non-empty line that parses as JSON (lpac prints progress lines before) */
static json_object *last_json(const char *text)
{
	const char *p = text ? text + strlen(text) : NULL, *end;
	json_object *j = NULL;

	while (p && p > text && !j) {
		end = p;
		while (end > text && (end[-1] == '\n' || end[-1] == '\r'))
			end--;
		p = end;
		while (p > text && p[-1] != '\n')
			p--;
		if (end > p) {
			char *line = strndup(p, (size_t)(end - p));
			j = line ? json_tokener_parse(line) : NULL;
			free(line);
		}
	}
	return j;
}

json_object *mm_esim_result(const char *output, int exit_status, int timed_out)
{
	json_object *last, *payload, *o;
	const char *msg;
	int code;

	/* a timeout says nothing about the eUICC: never "absent" */
	if (timed_out)
		return error("timeout", "the operation did not finish in time");
	if (exit_status == 127)
		return error("lpac_missing", "lpac is not installed");
	/* the AT bridge's own outcomes (esim-apdu run) */
	if (exit_status == 124)
		return error("driver_unavailable", "lpac's stdio APDU driver does not wait for answers (lpac 2.3.0; fixed "
						   "upstream after that release): install a build with the fix or lpac 2.2");
	if (exit_status == 125)
		return error("transport_unavailable", "the eUICC port cannot be opened");
	last = last_json(output);
	payload = get(last, "payload");
	/* lpac prints this as plain text and stops: its build lacks a driver */
	if (!payload && output && (strstr(output, "No HTTP driver found") || strstr(output, "No APDU driver found"))) {
		json_object_put(last);
		return error("driver_unavailable", strstr(output, "No HTTP driver found") ? "lpac has no HTTP driver"
											  : "lpac has no APDU driver");
	}
	if (!payload) {
		o = error("lpac_error", exit_status ? "lpac failed without an answer" : "lpac gave no answer");
		json_object_put(last);
		return o;
	}
	/* an answer without a numeric code is no success (review 2026-11-15: json_object_get_int(NULL) is 0) */
	code = json_object_is_type(get(payload, "code"), json_type_int) ? json_object_get_int(get(payload, "code")) : -1;
	msg = str(payload, "message");
	if (code == 0) {
		o = result(true);
		json_object_object_add(o, "data", json_object_get(get(payload, "data")));
		json_object_put(last);
		return o;
	}
	/* lpac reports a channel it could not open as euicc_init; a wrong transport
	 * is excluded by the engine's choice, so here it means no eUICC answered */
	if (msg && strstr(msg, "euicc_init"))
		o = error("euicc_absent", msg);
	else
		o = error("lpac_error", msg ? msg : "lpac answered without a result code");
	json_object_put(last);
	return o;
}

int mm_esim_action_known(const char *action)
{
	static const char *const known[] = { "version", "probe", "chip", "profiles", "enable", NULL };
	int i;

	for (i = 0; action && known[i]; i++)
		if (!strcmp(action, known[i]))
			return 1;
	return 0;
}
