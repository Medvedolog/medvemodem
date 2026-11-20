// SPDX-License-Identifier: GPL-3.0-only
#include <stdio.h>
#include <string.h>

#include "alert.h"

#define MM_ALERT_FORGET_MS 3600000LL	/* a link away this long is forgotten, its episode with it */

static long long num(json_object *o, const char *k)
{
	return o ? json_object_get_int64(json_object_object_get(o, k)) : 0;
}

static const char *str(json_object *o, const char *k)
{
	json_object *v = o ? json_object_object_get(o, k) : NULL;

	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static bool is(const char *a, const char *b)
{
	return a && b && !strcmp(a, b);
}

static json_object *child(json_object *o, const char *k)
{
	json_object *c = json_object_object_get(o, k);

	if (!c) {
		c = json_object_new_object();
		json_object_object_add(o, k, c);
	}
	return c;
}

static void set(json_object *o, const char *k, long long v)
{
	json_object_object_add(o, k, json_object_new_int64(v));
}

static void notice(json_object *out, const char *kind, const char *link, const char *from, const char *to, long long ms)
{
	json_object *n = json_object_new_object();

	json_object_object_add(n, "kind", json_object_new_string(kind));
	if (link)
		json_object_object_add(n, "link", json_object_new_string(link));
	if (from)
		json_object_object_add(n, "from", json_object_new_string(from));
	if (to)
		json_object_object_add(n, "to", json_object_new_string(to));
	set(n, "for_ms", ms > 0 ? ms : 0);
	json_object_array_add(out, n);
}

/* one link's whitelist: told after whitelist_ms of "limited", ended after clear_ms of "ok" */
static void whitelist_step(json_object *e, const char *key, const char *level, const struct mm_alert_cfg *cfg,
			   long long now, json_object *out, bool *drop)
{
	bool told = json_object_get_boolean(json_object_object_get(e, "told"));

	set(e, "seen", now);
	if (is(level, "limited")) {
		json_object_object_del(e, "ok_since");
		if (!json_object_object_get(e, "since"))
			set(e, "since", now);
		if (!told && now - num(e, "since") >= cfg->whitelist_ms) {
			json_object_object_add(e, "told", json_object_new_boolean(1));
			if (cfg->on[MM_ALERT_WHITELIST])
				notice(out, "whitelist", key, NULL, NULL, now - num(e, "since"));
		}
		return;
	}
	if (!is(level, "ok")) {
		/* nothing answers: the whitelist neither ends nor starts */
		json_object_object_del(e, "ok_since");
		return;
	}
	if (!told) {
		*drop = true;
		return;
	}
	if (!json_object_object_get(e, "ok_since"))
		set(e, "ok_since", now);
	if (now - num(e, "ok_since") >= cfg->clear_ms) {
		if (cfg->on[MM_ALERT_WHITELIST] && cfg->on[MM_ALERT_RESTORE])
			notice(out, "whitelist-end", key, NULL, NULL, num(e, "ok_since") - num(e, "since"));
		*drop = true;
	}
}

static void whitelists(json_object *state, json_object *links, const struct mm_alert_cfg *cfg, long long now,
		       json_object *out)
{
	json_object *book = child(state, "links"), *gone = json_object_new_array();
	size_t i;

	if (links) {
		json_object_object_foreach(links, key, lv) {
			const char *level = json_object_get_string(lv);
			json_object *e = json_object_object_get(book, key);
			bool drop = false;

			if (!e) {
				if (!is(level, "limited"))
					continue;
				e = child(book, key);
			}
			whitelist_step(e, key, level, cfg, now, out, &drop);
			if (drop)
				json_object_array_add(gone, json_object_new_string(key));
		}
	}
	json_object_object_foreach(book, key, e) {
		/* away: an episode not told yet is gone with it; a told one waits an hour for the link to come back */
		if (!links || !json_object_object_get(links, key))
			if (!json_object_get_boolean(json_object_object_get(e, "told")) || now - num(e, "seen") >= MM_ALERT_FORGET_MS)
				json_object_array_add(gone, json_object_new_string(key));
	}
	for (i = 0; i < json_object_array_length(gone); i++)
		json_object_object_del(book, json_object_get_string(json_object_array_get_idx(gone, i)));
	json_object_put(gone);
}

/* the router as a whole: no link answers at all */
static void outage(json_object *state, json_object *links, const struct mm_alert_cfg *cfg, long long now, json_object *out)
{
	json_object *e = child(state, "down");
	bool any_ok = false, any_limited = false, any = false, told = json_object_get_boolean(json_object_object_get(e, "told"));

	if (links) {
		json_object_object_foreach(links, key, lv) {
			(void)key;
			any = true;
			any_ok = any_ok || is(json_object_get_string(lv), "ok");
			any_limited = any_limited || is(json_object_get_string(lv), "limited");
		}
	}
	if (any)
		json_object_object_add(state, "seen", json_object_new_boolean(1));
	if (!any && !json_object_get_boolean(json_object_object_get(state, "seen")))
		return;
	if (!any_ok && !any_limited) {
		json_object_object_del(e, "ok_since");
		if (!json_object_object_get(e, "since"))
			set(e, "since", now);
		if (!told && now - num(e, "since") >= cfg->down_ms) {
			json_object_object_add(e, "told", json_object_new_boolean(1));
			if (cfg->on[MM_ALERT_DOWN])
				notice(out, "down", NULL, NULL, NULL, now - num(e, "since"));
		}
		return;
	}
	if (!told) {
		/* the whitelist alone, or back before down_ms: no outage to tell */
		json_object_object_del(e, "since");
		return;
	}
	if (!any_ok) {
		json_object_object_del(e, "ok_since");
		return;
	}
	if (!json_object_object_get(e, "ok_since"))
		set(e, "ok_since", now);
	if (now - num(e, "ok_since") >= cfg->clear_ms) {
		if (cfg->on[MM_ALERT_DOWN] && cfg->on[MM_ALERT_RESTORE])
			notice(out, "down-end", NULL, NULL, NULL, num(e, "ok_since") - num(e, "since"));
		json_object_object_del(state, "down");
	}
}

/* the link that carries the traffic: moved away from the home and held, moved again, back home */
static void carrier(json_object *state, const char *c, const struct mm_alert_cfg *cfg, long long now, json_object *out)
{
	json_object *e = child(state, "switch");
	const char *home = str(e, "home"), *told = str(e, "told_to");
	long long held;

	if (!c)
		return;		/* no default route: the outage tells that */
	if (!is(str(e, "cur"), c)) {
		json_object_object_add(e, "cur", json_object_new_string(c));
		set(e, "cur_since", now);
	}
	held = now - num(e, "cur_since");
	if (!home) {
		if (held >= cfg->switch_ms)
			json_object_object_add(e, "home", json_object_new_string(c));
		return;
	}
	if (!is(c, home)) {
		if (held >= cfg->switch_ms && !is(told, c)) {
			if (!told)
				set(e, "left", num(e, "cur_since"));
			if (cfg->on[MM_ALERT_SWITCH])
				notice(out, "switch", NULL, told ? told : home, c, now - num(e, "left"));
			json_object_object_add(e, "told_to", json_object_new_string(c));
		}
		return;
	}
	if (told && held >= cfg->clear_ms) {
		if (cfg->on[MM_ALERT_SWITCH] && cfg->on[MM_ALERT_RESTORE])
			notice(out, "switch-back", NULL, told, home, num(e, "cur_since") - num(e, "left"));
		json_object_object_del(e, "told_to");
		json_object_object_del(e, "left");
	}
}

json_object *mm_alert_step(json_object *state, json_object *obs, const struct mm_alert_cfg *cfg, long long now_ms)
{
	json_object *out = json_object_new_array(), *links = json_object_object_get(obs, "links");

	if (links && !json_object_is_type(links, json_type_object))
		links = NULL;
	whitelists(state, links, cfg, now_ms, out);
	outage(state, links, cfg, now_ms, out);
	carrier(state, str(obs, "carrier"), cfg, now_ms, out);
	return out;
}

const char *mm_alert_dev_kind(const char *dev)
{
	static const char *const tunnels[] = { "wg", "awg", "tun", "tap", "tailscale", "zt", "ovpn", "ppp", "gre", "ipsec",
					       "vti", "sing", "singbox", "xray", NULL };
	size_t i;

	if (!dev)
		return "adapter";
	for (i = 0; tunnels[i]; i++)
		if (!strncmp(dev, tunnels[i], strlen(tunnels[i])))
			return "tunnel";
	if (!strncmp(dev, "wlan", 4) || !strncmp(dev, "phy", 3) || strstr(dev, "-sta"))
		return "wifi";
	return "adapter";
}

static void span(long long ms, bool ru, char *out, size_t size)
{
	long long m = (ms + 30000) / 60000;

	if (m < 1)
		snprintf(out, size, ru ? "меньше минуты" : "under a minute");
	else if (m < 60)
		snprintf(out, size, ru ? "%lld мин" : "%lld min", m);
	else if (m < 1440 && m % 60)
		snprintf(out, size, ru ? "%lld ч %lld мин" : "%lld h %lld min", m / 60, m % 60);
	else if (m < 1440)
		snprintf(out, size, ru ? "%lld ч" : "%lld h", m / 60);
	else
		snprintf(out, size, ru ? "%lld д %lld ч" : "%lld d %lld h", m / 1440, m % 1440 / 60);
}

static const char *name_of(json_object *names, const char *key)
{
	const char *n = key ? str(names, key) : NULL;

	return n && *n ? n : key ? key : "?";
}

/* why failover moved the traffic away (medvemodemd.c failover_reason, r14), or NULL for a code it does not know */
static const char *cause_words(const char *c, bool ru)
{
	static const char *const map[][3] = {
		{ "absent", "the modem is not on USB", "модем не обнаружен на USB" }, { "recovery", "the modem was being recovered", "шло восстановление модема" },
		{ "paused", "switched off by the administrator", "выключен администратором" },
		{ "session-failed", "the connection dropped", "соединение оборвалось" }, { "no-session", "no session with the operator", "сеанс с оператором не установлен" },
		{ "sim-missing", "no SIM card", "нет SIM-карты" }, { "sim-locked", "the SIM needs its PIN", "для SIM-карты требуется PIN-код" },
		{ "registration-denied", "the network refused the registration", "оператор отказал в регистрации" },
		{ "searching", "searching for a network", "модем ищет сеть" }, { "not-registered", "not registered in a network", "модем не зарегистрирован в сети" },
		{ "no-link", "no cable link", "нет линка по кабелю" }, { "whitelist", "only the operator's whitelist works", "доступны только сайты из белого списка оператора" },
		{ "restricted", "the operator restricted access", "оператор ограничил доступ" },
		{ "no-answer", "the check addresses did not answer", "проверочные адреса не отвечают" },
	};

	for (size_t i = 0; c && i < sizeof(map) / sizeof(map[0]); i++)
		if (!strcmp(map[i][0], c))
			return map[i][ru ? 2 : 1];
	return NULL;
}

void mm_alert_text(json_object *notice, json_object *names, const char *host, bool ru, char *out, size_t size)
{
	const char *kind = str(notice, "kind"), *link = name_of(names, str(notice, "link"));
	const char *from = name_of(names, str(notice, "from")), *to = name_of(names, str(notice, "to"));
	char d[48], head[96];
	int n;

	span(num(notice, "for_ms"), ru, d, sizeof(d));
	n = snprintf(head, sizeof(head), "%s%s%s: ", ru ? "МедвеМодем" : "MedveModem", host && *host ? " " : "", host ? host : "");
	if (n < 0 || (size_t)n >= sizeof(head))
		snprintf(head, sizeof(head), "%s: ", ru ? "МедвеМодем" : "MedveModem");
	if (is(kind, "whitelist"))
		snprintf(out, size, ru ? "%sбелые списки на %s уже %s" : "%s%s whitelist-only for %s", head, link, d);
	else if (is(kind, "whitelist-end"))
		snprintf(out, size, ru ? "%sбелые списки на %s сняты, длились %s" : "%s%s full access restored (whitelisted %s)",
			 head, link, d);
	else if (is(kind, "down"))
		snprintf(out, size, ru ? "%sнет интернета уже %s" : "%sno internet for %s", head, d);
	else if (is(kind, "down-end"))
		snprintf(out, size, ru ? "%sинтернет восстановлен, не было %s" : "%sinternet restored after %s", head, d);
	else if (is(kind, "switch") && cause_words(str(notice, "cause"), ru))
		snprintf(out, size, ru ? "%sтрафик перешёл с %s на %s: %s" : "%straffic switched from %s to %s: %s", head, from, to,
			 cause_words(str(notice, "cause"), ru));
	else if (is(kind, "switch"))
		snprintf(out, size, ru ? "%sтрафик перешёл с %s на %s" : "%straffic switched from %s to %s", head, from, to);
	else if (is(kind, "switch-back"))
		snprintf(out, size, ru ? "%sтрафик вернулся на %s спустя %s" : "%straffic returned to %s after %s", head, to, d);
	else
		snprintf(out, size, ru ? "%sпроверка SMS-оповещений" : "%sSMS alert test", head);
}
