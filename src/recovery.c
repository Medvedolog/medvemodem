#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recovery.h"

/*
 * Device state: {"attempts": [{t, m, level, name, reason, result, detail?}...oldest first],
 * "consecutive_failures", "cooldown_m", "manual_required", "last_success", "last_failure"}.
 */
#define MM_REC_KEEP 20
#define MM_5M_MS 300000LL
#define MM_1H_MS 3600000LL

static const char *const names[MM_REC_LEVELS] = {
	"observe", "session", "control", "radio", "soft-reset", "rebind", "usb-reset", "power"
};

/* the profile key that declares each disruptive level (22.1.3) */
static const char *const capability[MM_REC_LEVELS] = {
	NULL, "session_restart", NULL, "radio_cycle", "soft_reset", "driver_rebind", "usb_reauthorize", "power_cycle"
};

static const char *const reasons[] = {
	"registration_stuck", "session_stuck", "control_port_unresponsive", "mm_object_churn", "owner_conflict",
	"tty_missing", "wdm_missing", "netdev_missing", "modem_firmware_hang", "usb_link_unstable", "user_requested",
	"mm_not_holding", "no_internet", "no_data_2g", "registration_denied", "usb_power", "mm_bearer_stuck",
	NULL
};

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

static long long num(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v ? json_object_get_int64(v) : 0;
}

const char *mm_recovery_level_name(int level)
{
	return level >= 0 && level < MM_REC_LEVELS ? names[level] : NULL;
}

int mm_recovery_level_parse(const char *s)
{
	int i;

	if (!s || !*s)
		return -1;
	for (i = 0; i < MM_REC_LEVELS; i++)
		if (!strcmp(s, names[i]))
			return i;
	if (s[0] >= '0' && s[0] < '0' + MM_REC_LEVELS && !s[1])
		return s[0] - '0';
	if ((s[0] == 'L' || s[0] == 'l') && s[1] >= '0' && s[1] < '0' + MM_REC_LEVELS && !s[2])
		return s[1] - '0';
	return -1;
}

bool mm_recovery_reason_valid(const char *reason)
{
	int i;

	for (i = 0; reason && reasons[i]; i++)
		if (!strcmp(reason, reasons[i]))
			return true;
	return false;
}

const char *mm_recovery_support(json_object *profile, const char *owner, int level, bool automatic)
{
	json_object *rec = get(profile, "recovery"), *cap;
	const char *status = str(rec, "status");

	if (level < 0 || level >= MM_REC_LEVELS)
		return "unknown recovery level";
	if (level == MM_REC_OBSERVE || level == MM_REC_CONTROL)
		return NULL;
	if (level == MM_REC_SESSION) {
		cap = get(rec, "session_restart");
		return cap && !json_object_get_boolean(cap) ? "the profile says a session restart does not help this modem" : NULL;
	}
	if (level == MM_REC_POWER) {
		/* a switchable hub port (R9): by the button; by itself only once the profile's power_cycle is bench-validated */
		if (!json_object_get_boolean(get(profile, "port_power")))
			return "neither a power-cycle plugin nor a switchable USB port on this router";
		if (automatic && !(json_object_get_boolean(get(rec, "power_cycle")) && status && !strcmp(status, "bench-validated")))
			return "only bench-validated capabilities run without an operator";
		return owner && !strcmp(owner, "qmodem") ? "QModem owns this modem; recover it through QModem" : NULL;
	}
	if (owner && !strcmp(owner, "qmodem"))
		return "QModem owns this modem; recover it through QModem";
	/* a HiLink's own web reboots it (r117's button, the owner 2026-10-30: one button, in the ladder): whatever its
	 * profile declares, by the operator only until the field says it recovers a modem. Its web reconnects the mobile
	 * session too (net/reconnect, the E3372-325 HAR 2026-10-30): the radio step, gentle enough to run by itself */
	if (owner && (!strcmp(owner, "web") || !strcmp(owner, "web-reboot")))	/* web-reboot: a web with no reconnect (MF150-4) */
		return level == MM_REC_RADIO ? (!strcmp(owner, "web") ? NULL : "this modem's web has no reconnect of its own") :
		       level != MM_REC_SOFT_RESET ? mm_recovery_support(profile, NULL, level, automatic) :
		       automatic ? "a reboot through the modem's web runs by the operator's button only" : NULL;
	if (!str(profile, "id") || (str(profile, "status") && strcmp(str(profile, "status"), "matched")))
		return "no specific profile matches this modem, so no disruptive level is declared";
	cap = get(rec, capability[level]);
	if (!cap || !json_object_get_boolean(cap))
		return "the profile does not declare this level for the modem";
	if (!status || (strcmp(status, "field-reported") && strcmp(status, "bench-validated")))
		return "the profile's recovery capabilities are not validated";
	/* field-reported: radio, modem reset and USB reset run by themselves too, the cover for the field-reported
	 * bindings (the owner, 2026-10-27); a driver rebind still waits for the bench (kernel faults, 2026-10-09) */
	if (automatic && strcmp(status, "bench-validated") && level != MM_REC_RADIO && level != MM_REC_SOFT_RESET &&
	    level != MM_REC_USB_RESET)
		return "only bench-validated capabilities run without an operator";
	if ((level == MM_REC_RADIO || level == MM_REC_SOFT_RESET) && (!owner || strcmp(owner, "modemmanager")))
		return "this level is carried out through ModemManager, which does not own the modem";
	return NULL;
}

json_object *mm_recovery_levels(json_object *profile, const char *owner, bool automatic)
{
	json_object *out = json_object_new_array();
	int i;

	for (i = 0; i < MM_REC_LEVELS; i++) {
		json_object *l = json_object_new_object();
		const char *why = mm_recovery_support(profile, owner, i, automatic);

		json_object_object_add(l, "level", json_object_new_int(i));
		json_object_object_add(l, "name", json_object_new_string(names[i]));
		json_object_object_add(l, "supported", json_object_new_boolean(!why));
		if (why)
			json_object_object_add(l, "why", json_object_new_string(why));
		json_object_array_add(out, l);
	}
	return out;
}

/* attempts at or above min_level since now - window */
static int count_since(json_object *state, int min_level, int max_level, long long since)
{
	json_object *list = get(state, "attempts");
	size_t i, n = list ? json_object_array_length(list) : 0;
	int c = 0;

	for (i = 0; i < n; i++) {
		json_object *a = json_object_array_get_idx(list, i);
		int lv = (int)num(a, "level");
		if (num(a, "m") >= since && lv >= min_level && lv <= max_level)
			c++;
	}
	return c;
}

const char *mm_recovery_gate(json_object *state, json_object *profile, const char *owner, json_object *stability,
			     int level, bool automatic, long long now_ms, const struct mm_recovery_cfg *cfg,
			     char *buf, size_t size)
{
	const char *why = mm_recovery_support(profile, owner, level, automatic);
	long long cool = num(state, "cooldown_m");
	int n;

	if (why) {
		snprintf(buf, size, "%s", why);
		return buf;
	}
	if (level == MM_REC_SESSION &&
	    (n = count_since(state, MM_REC_SESSION, MM_REC_SESSION, now_ms - MM_5M_MS)) >= cfg->session_cap_5m) {
		snprintf(buf, size, "%d session restarts in the last 5 minutes", n);
		return buf;
	}
	if (level < MM_REC_DISRUPTIVE)
		return NULL;
	if (automatic && json_object_get_boolean(get(state, "manual_required"))) {
		snprintf(buf, size, "manual recovery required after %lld failed attempts",
			 num(state, "consecutive_failures"));
		return buf;
	}
	if (cool > now_ms) {
		snprintf(buf, size, "cooling down for %lld s after a failed attempt", (cool - now_ms + 999) / 1000);
		return buf;
	}
	if ((n = count_since(state, MM_REC_DISRUPTIVE, MM_REC_LEVELS, now_ms - MM_5M_MS)) >= cfg->cap_5m) {
		snprintf(buf, size, "%d disruptive recoveries in the last 5 minutes", n);
		return buf;
	}
	if ((n = count_since(state, MM_REC_DISRUPTIVE, MM_REC_LEVELS, now_ms - MM_1H_MS)) >= cfg->cap_1h) {
		snprintf(buf, size, "%d disruptive recoveries in the last hour", n);
		return buf;
	}
	/* 22.1.10: resetting a modem whose USB link keeps dropping only hammers it */
	if (automatic && level >= MM_REC_SOFT_RESET && str(stability, "state") && !strcmp(str(stability, "state"), "flapping") &&
	    str(stability, "probable_cause") && !strcmp(str(stability, "probable_cause"), "usb-link-unstable")) {
		snprintf(buf, size, "the USB link keeps dropping (possibly insufficient power); a reset would not help");
		return buf;
	}
	return NULL;
}

void mm_recovery_record(json_object *state, int level, const char *reason, const char *result, const char *detail,
			long long now_ms, long long wall, const struct mm_recovery_cfg *cfg)
{
	json_object *list = get(state, "attempts"), *a = json_object_new_object();
	bool ok = result && !strcmp(result, "succeeded");
	long long fails;

	if (!list) {
		list = json_object_new_array();
		json_object_object_add(state, "attempts", list);
	}
	json_object_object_add(a, "t", json_object_new_int64(wall));
	json_object_object_add(a, "m", json_object_new_int64(now_ms));
	json_object_object_add(a, "level", json_object_new_int(level));
	json_object_object_add(a, "name", json_object_new_string(mm_recovery_level_name(level) ? mm_recovery_level_name(level) : "?"));
	json_object_object_add(a, "reason", json_object_new_string(reason ? reason : ""));
	json_object_object_add(a, "result", json_object_new_string(ok ? "succeeded" : "failed"));
	if (detail && *detail)
		json_object_object_add(a, "detail", json_object_new_string(detail));
	json_object_array_add(list, json_object_get(a));
	if (json_object_array_length(list) > MM_REC_KEEP)
		json_object_array_del_idx(list, 0, json_object_array_length(list) - MM_REC_KEEP);

	if (ok) {
		json_object_object_add(state, "consecutive_failures", json_object_new_int(0));
		json_object_object_add(state, "cooldown_m", json_object_new_int64(0));
		json_object_object_add(state, "manual_required", json_object_new_boolean(0));
		json_object_object_add(state, "last_success", a);
		return;
	}
	json_object_object_add(state, "last_failure", a);
	if (level == MM_REC_OBSERVE)
		return;		/* looking again is not a failed repair */
	fails = num(state, "consecutive_failures") + 1;
	json_object_object_add(state, "consecutive_failures", json_object_new_int64(fails));
	if (level >= MM_REC_DISRUPTIVE) {
		long long s = cfg->base_cooldown_s;
		int i;

		for (i = 1; i < fails && s < cfg->max_cooldown_s; i++)
			s *= 2;
		if (s > cfg->max_cooldown_s)
			s = cfg->max_cooldown_s;
		json_object_object_add(state, "cooldown_m", json_object_new_int64(now_ms + s * 1000));
	}
	if (fails >= cfg->manual_after)
		json_object_object_add(state, "manual_required", json_object_new_boolean(1));
}

static json_object *public_attempt(json_object *a)
{
	json_object *o;

	if (!a)
		return NULL;
	o = json_object_new_object();
	json_object_object_foreach(a, k, v)
		if (strcmp(k, "m"))
			json_object_object_add(o, k, json_object_get(v));
	return o;
}

json_object *mm_recovery_view(json_object *state, json_object *profile, const char *owner, int active_level,
			      const char *active_reason, long long now_ms, long long wall_now,
			      const struct mm_recovery_cfg *cfg)
{
	json_object *o = json_object_new_object(), *supported = json_object_new_array(), *recent = json_object_new_array();
	json_object *list = get(state, "attempts");
	long long cool = num(state, "cooldown_m");
	const char *st = "idle";
	char buf[160];
	size_t i, n = list ? json_object_array_length(list) : 0;
	int l;

	if (active_level >= 0)
		st = "running";
	else if (json_object_get_boolean(get(state, "manual_required")))
		st = "manual_required";
	else if (cool > now_ms)
		st = "cooldown";
	for (l = 0; l < MM_REC_LEVELS; l++)
		if (!mm_recovery_support(profile, owner, l, false))
			json_object_array_add(supported, json_object_new_string(names[l]));
	for (i = n > 5 ? n - 5 : 0; i < n; i++)
		json_object_array_add(recent, public_attempt(json_object_array_get_idx(list, i)));

	json_object_object_add(o, "state", json_object_new_string(st));
	json_object_object_add(o, "current_level", active_level >= 0 ? json_object_new_string(names[active_level]) : NULL);
	json_object_object_add(o, "reason", active_level >= 0 && active_reason ? json_object_new_string(active_reason) : NULL);
	json_object_object_add(o, "attempts_1h", json_object_new_int(count_since(state, MM_REC_SESSION, MM_REC_LEVELS, now_ms - MM_1H_MS)));
	json_object_object_add(o, "consecutive_failures", json_object_new_int64(num(state, "consecutive_failures")));
	json_object_object_add(o, "cooldown_until", cool > now_ms ? json_object_new_int64(wall_now + (cool - now_ms + 999) / 1000) : NULL);
	json_object_object_add(o, "last_success", public_attempt(get(state, "last_success")));
	json_object_object_add(o, "last_failure", public_attempt(get(state, "last_failure")));
	json_object_object_add(o, "supported_levels", supported);
	/* what loop protection holds back right now */
	buf[0] = '\0';
	if (active_level < 0 && json_object_get_boolean(get(state, "manual_required")))
		snprintf(buf, sizeof(buf), "manual recovery required after %lld failed attempts",
			 num(state, "consecutive_failures"));
	else if (active_level < 0 && cool > now_ms)
		snprintf(buf, sizeof(buf), "cooling down for %lld s after a failed attempt", (cool - now_ms + 999) / 1000);
	else if (active_level < 0 && count_since(state, MM_REC_DISRUPTIVE, MM_REC_LEVELS, now_ms - MM_5M_MS) >= cfg->cap_5m)
		snprintf(buf, sizeof(buf), "%d disruptive recoveries in the last 5 minutes",
			 count_since(state, MM_REC_DISRUPTIVE, MM_REC_LEVELS, now_ms - MM_5M_MS));
	json_object_object_add(o, "blocked_reason", buf[0] ? json_object_new_string(buf) : NULL);
	json_object_object_add(o, "recent", recent);
	return o;
}
