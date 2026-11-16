// SPDX-License-Identifier: GPL-3.0-only
/*
 * SMS alerts (spec 38.6; the owner, 2026-10-29: «отправка автоматически смс выбранному номеру или номерам через
 * настройки, что наступили белые списки через 15 мин, или что отвалился инет, или автопереход на другой модем или
 * тоннель, адаптер, и что их сняли, с гистерезисом; и о восстановлении»). alert.c keeps the episodes; this feeds it
 * each uplink pass, words its notices and hands them to the helper `sms-alert`, which sends each through the first
 * modem that takes it (ubus sms_send: ModemManager, AT, a HiLink's web, QModem, whichever the modem has). SMS go
 * out over the radio, so an alert reaches the phone while the internet is gone.
 *
 * UCI medvemodem.alerts (config alerts 'alerts'):
 *   option enabled '0'                 off by default: SMS cost money
 *   list contact '<section>'           the recipients, from the address book (config contact, shared by all modems)
 *   option whitelist|down|switch|restore '1'
 *   option whitelist_min '15'  down_min '5'  switch_min '2'  clear_min '3'
 *   option modem '<device_id>'         the sender tried first; the others after it
 *   option max_per_hour '6'
 *   option lang 'ru' | 'en'            what LuCI wrote on saving
 * Nothing on flash: an engine restart starts the episodes afresh (an outage still going on is told again after
 * down_min). The numbers are never logged; the journal says how many recipients took it.
 */
#include <fcntl.h>
#include <limits.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "alert.h"
#include "daemon.h"

#define AL_RETRY_MS 120000	/* a send that found no modem tries again */
#define AL_KEEP_MS 1800000	/* ... until the notice is this old */
#define AL_TRIES 4
#define AL_RECENT 10

static json_object *state;	/* alert.c's episodes */
static json_object *levels;	/* device_id -> this pass's verdict (alert_note) */
static json_object *queue;	/* [{text, kind, at_ms, tries, next_ms}] */
static json_object *recent;	/* the last sends: [{at, kind, text, ok, sent, of}] */
static json_object *sent_ms;	/* the times of the sends in the last hour */
static json_object *fwd_ms;	/* ... of the SMS forwarded by SMS (spec §43), capped apart */
static struct uloop_process proc;
static bool running;
static char job_path[PATH_MAX];

static json_object *section(void)
{
	return json_object_object_get(medvemodem_uci(), "alerts");
}

static const char *opt(const char *k, const char *dflt)
{
	const char *v = jstr(section(), k);

	return v && *v ? v : dflt;
}

static bool on(const char *k)
{
	return !strcmp(opt(k, "1"), "1");
}

static long long minutes(const char *k, long long dflt)
{
	long long v = atoll(opt(k, ""));

	return (v > 0 && v <= 1440 ? v : dflt) * 60000;
}

static bool ru(void)
{
	return strcmp(opt("lang", "ru"), "en") != 0;
}

/* the recipients' numbers from the address book */
static json_object *numbers(void)
{
	json_object *img = medvemodem_uci(), *list = json_object_object_get(section(), "contact"), *out = json_object_new_array();
	size_t i, n = json_object_is_type(list, json_type_array) ? json_object_array_length(list) : list ? 1 : 0;

	for (i = 0; img && i < n; i++) {
		json_object *c = json_object_object_get(img, json_object_get_string(
			json_object_is_type(list, json_type_array) ? json_object_array_get_idx(list, i) : list));
		const char *num = jstr(c, "number");

		if (c && jstr(c, ".type") && !strcmp(jstr(c, ".type"), "contact") && num && *num)
			json_object_array_add(out, json_object_new_string(num));
	}
	return out;
}

static bool enabled(void)
{
	return !strcmp(opt("enabled", "0"), "1");
}

void alert_note(const char *id, const char *level)
{
	if (!id)
		return;
	if (!levels)
		levels = json_object_new_object();
	if (level)
		json_object_object_add(levels, id, json_object_new_string(!strcmp(level, "off") ? "none" : level));
	else
		json_object_object_del(levels, id);
}

/* the modems the helper tries, the chosen sender first, then those whose link answers, then the rest; `skip` (the modem
 * a forwarded SMS came in on) only when no other would take it */
static json_object *senders_of(const char *first, const char *skip)
{
	json_object *out = json_object_new_array();
	int pass;

	if (first && skip && !strcmp(first, skip))
		first = NULL;

	if (first && mm_registry_find(&registry, first) && !frozen(first))
		json_object_array_add(out, json_object_new_string(first));
	for (pass = 0; pass < 2 && registry.devices; pass++) {
		json_object_object_foreach(registry.devices, id, rec) {
			const char *lv = levels ? jstr(levels, id) : NULL;
			bool answers = lv && (!strcmp(lv, "ok") || !strcmp(lv, "limited"));

			if (!json_object_get_boolean(json_object_object_get(rec, "present")) || frozen(id) ||
			    (first && !strcmp(id, first)) || answers != (pass == 0) || (skip && !strcmp(id, skip)))
				continue;
			json_object_array_add(out, json_object_new_string(id));
		}
	}
	if (skip && !json_object_array_length(out) && mm_registry_find(&registry, skip) && !frozen(skip))
		json_object_array_add(out, json_object_new_string(skip));
	return out;
}

static json_object *senders(void)
{
	return senders_of(opt("modem", NULL), NULL);
}

static void remember(const char *kind, const char *text, bool ok)
{
	json_object *r = json_object_new_object();

	if (!recent)
		recent = json_object_new_array();
	json_object_object_add(r, "at", json_object_new_int64((long long)time(NULL)));
	json_object_object_add(r, "kind", json_object_new_string(kind));
	/* a forwarded SMS' text is the subscriber's: not kept, the page says only that one went */
	json_object_object_add(r, "text", json_object_new_string(!strcmp(kind, "forward") ? "" : text));
	json_object_object_add(r, "ok", json_object_new_boolean(ok));
	json_object_array_add(recent, r);
	while (json_object_array_length(recent) > AL_RECENT)
		json_object_array_del_idx(recent, 0, 1);
}

static void send_next(void);

static void exited(struct uloop_process *p, int ret)
{
	json_object *n = json_object_array_length(queue) ? json_object_array_get_idx(queue, 0) : NULL;
	bool ok = WIFEXITED(ret) && WEXITSTATUS(ret) == 0, some = WIFEXITED(ret) && WEXITSTATUS(ret) == 2;
	long long now = now_mono_ms();

	(void)p;
	running = false;
	unlink(job_path);
	if (!n)
		return;
	ULOG_INFO("sms alert %s: %s\n", jstr(n, "kind"), ok ? "sent" : some ? "sent to some recipients" : "not sent");
	if (jstr(n, "from") && (ok || some))
		mm_history_add(&history, jstr(n, "from"), (long long)time(NULL), now, "sms-forward", ok ? "by SMS: sent" : "by SMS: sent to some");
	if (ok || some || json_object_get_int64(json_object_object_get(n, "tries")) >= AL_TRIES ||
	    now - json_object_get_int64(json_object_object_get(n, "at_ms")) >= AL_KEEP_MS) {
		remember(jstr(n, "kind"), jstr(n, "text"), ok || some);
		json_object_array_del_idx(queue, 0, 1);
		send_next();
		return;
	}
	json_object_object_add(n, "next_ms", json_object_new_int64(now + AL_RETRY_MS));
}

static void send_next(void)
{
	json_object *n, *job, *ids, *nums;
	char path[PATH_MAX];
	const char *prog = getenv("MEDVEMODEM_SMS_ALERT");
	pid_t pid;

	int fd;

	if (running || !queue)
		return;
	/* a notice no modem took for AL_KEEP_MS is old news: dropped, and the page says it was not sent */
	while (json_object_array_length(queue) &&
	       now_mono_ms() - json_object_get_int64(json_object_object_get(json_object_array_get_idx(queue, 0), "at_ms")) >= AL_KEEP_MS) {
		n = json_object_array_get_idx(queue, 0);
		ULOG_INFO("sms alert %s: not sent, no modem took it\n", jstr(n, "kind"));
		remember(jstr(n, "kind"), jstr(n, "text"), false);
		json_object_array_del_idx(queue, 0, 1);
	}
	if (!json_object_array_length(queue))
		return;
	n = json_object_array_get_idx(queue, 0);
	if (now_mono_ms() < json_object_get_int64(json_object_object_get(n, "next_ms")))
		return;
	nums = json_object_object_get(n, "numbers") ? json_object_get(json_object_object_get(n, "numbers")) : numbers();
	ids = json_object_object_get(n, "from") ? senders_of(jstr(json_object_object_get(medvemodem_uci(), "sms_forward"), "sms_modem"), jstr(n, "from"))
						: senders();
	if (!json_object_array_length(nums) || !json_object_array_length(ids)) {
		/* nobody to send to, or no modem now: wait for one */
		json_object_object_add(n, "next_ms", json_object_new_int64(now_mono_ms() + AL_RETRY_MS));
		json_object_put(nums);
		json_object_put(ids);
		return;
	}
	job = json_object_new_object();
	json_object_object_add(job, "ids", ids);
	json_object_object_add(job, "numbers", nums);
	json_object_object_add(job, "text", json_object_new_string(jstr(n, "text")));
	snprintf(job_path, sizeof(job_path), "%s/sms-alert.json", run_dir);
	mkdir(run_dir, 0755);
	/* the numbers: readable by root only */
	fd = open(job_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0 || json_object_to_fd(fd, job, JSON_C_TO_STRING_PLAIN)) {
		if (fd >= 0)
			close(fd);
		json_object_put(job);
		return;
	}
	close(fd);
	json_object_put(job);
	json_object_object_add(n, "tries", json_object_new_int64(json_object_get_int64(json_object_object_get(n, "tries")) + 1));
	if (prog && *prog)
		snprintf(path, sizeof(path), "%s", prog);
	else
		helper_path("sms-alert", path, sizeof(path));
	pid = fork();
	if (pid == 0) {
		int fd = open("/dev/null", O_RDWR);

		setpgid(0, 0);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execl(path, path, job_path, (char *)NULL);
		_exit(127);
	}
	if (pid < 0)
		return;
	running = true;
	proc.pid = pid;
	proc.cb = exited;
	uloop_process_add(&proc);
}

/* at most max_per_hour a rolling hour: SMS cost money, a flapping link must not empty the balance */
static bool within_rate(long long now)
{
	long long cap = atoll(opt("max_per_hour", "6"));

	if (!sent_ms)
		sent_ms = json_object_new_array();
	while (json_object_array_length(sent_ms) &&
	       now - json_object_get_int64(json_object_array_get_idx(sent_ms, 0)) >= 3600000)
		json_object_array_del_idx(sent_ms, 0, 1);
	if (cap <= 0)
		cap = 6;
	if ((long long)json_object_array_length(sent_ms) >= cap)
		return false;
	json_object_array_add(sent_ms, json_object_new_int64(now));
	return true;
}

static void enqueue(const char *kind, const char *text)
{
	json_object *n = json_object_new_object();
	long long now = now_mono_ms();

	if (!within_rate(now)) {
		ULOG_WARN("sms alert %s: over max_per_hour, not sent\n", kind);
		remember(kind, text, false);
		json_object_put(n);
		return;
	}
	if (!queue)
		queue = json_object_new_array();
	json_object_object_add(n, "kind", json_object_new_string(kind));
	json_object_object_add(n, "text", json_object_new_string(text));
	json_object_object_add(n, "at_ms", json_object_new_int64(now));
	json_object_array_add(queue, n);
	send_next();
}

static void host_name(char *buf, size_t size)
{
	FILE *f = fopen("/proc/sys/kernel/hostname", "r");

	buf[0] = 0;
	if (f) {
		if (fgets(buf, (int)size, f))
			buf[strcspn(buf, "\n")] = 0;
		fclose(f);
	}
	if (!strcmp(buf, "OpenWrt"))
		buf[0] = 0;	/* the default says nothing */
}

/* what a person reads for a link: «модем T99», «туннель wg0», «адаптер eth1» */
static void name_one(json_object *names, const char *key, bool r)
{
	char b[160];

	if (!key || json_object_object_get(names, key))
		return;
	if (!strncmp(key, "dev:", 4)) {
		const char *k = mm_alert_dev_kind(key + 4);

		snprintf(b, sizeof(b), "%s %s", !strcmp(k, "tunnel") ? (r ? "туннель" : "tunnel") :
			 !strcmp(k, "wifi") ? "Wi-Fi" : (r ? "адаптер" : "adapter"), key + 4);
	} else {
		char lb[64];

		snprintf(b, sizeof(b), "%s %s", r ? "модем" : "modem", daemon_modem_label(key, lb, sizeof(lb)));
	}
	json_object_object_add(names, key, json_object_new_string(b));
}

/* the forwarding's recipients: the address book's ticked contacts and the numbers typed in, at most 5, no repeats */
static json_object *forward_numbers(json_object *sf)
{
	json_object *img = medvemodem_uci(), *out = json_object_new_array();
	static const char *const keys[] = { "sms_contact", "sms_number" };
	size_t k, i, j;

	for (k = 0; k < 2; k++) {
		json_object *list = json_object_object_get(sf, keys[k]);
		size_t n = json_object_is_type(list, json_type_array) ? json_object_array_length(list) : list ? 1 : 0;

		for (i = 0; i < n && json_object_array_length(out) < 5; i++) {
			const char *v = json_object_get_string(json_object_is_type(list, json_type_array) ? json_object_array_get_idx(list, i) : list);
			const char *num = k ? v : jstr(json_object_object_get(img, v ? v : ""), "number");
			bool dup = false;

			if (!num || !*num || strspn(num, "+0123456789") != strlen(num) || strlen(num) > 20)
				continue;
			for (j = 0; j < json_object_array_length(out) && !dup; j++)
				dup = !strcmp(json_object_get_string(json_object_array_get_idx(out, j)), num);
			if (!dup)
				json_object_array_add(out, json_object_new_string(num));
		}
	}
	return out;
}

/* the first `max` characters of a UTF-8 text (a cut never splits a letter), «…» when cut */
static void cut_chars(const char *in, size_t max, char *out, size_t size)
{
	size_t chars = 0, i = 0;

	while (in[i] && chars < max) {
		size_t l = (unsigned char)in[i] < 0x80 ? 1 : ((unsigned char)in[i] >> 5) == 6 ? 2 : ((unsigned char)in[i] >> 4) == 14 ? 3 : 4;

		/* a sequence cut short before the end (a text cut by snprintf): never step over the NUL (review 2026-11-15) */
		for (size_t k = 1; k < l; k++)
			if (!in[i + k]) {
				l = k;
				break;
			}
		if (i + l + 4 >= size)
			break;
		i += l;
		chars++;
	}
	memcpy(out, in, i);
	out[i] = 0;
	if (in[i] && i + 4 < size)
		strcat(out, "\xe2\x80\xa6");	/* … */
}

void alert_forward(const char *from, const char *sender, const char *text)
{
	json_object *sf = json_object_object_get(medvemodem_uci(), "sms_forward"), *n, *nums;
	char lb[64];
	const char *name = daemon_modem_label(from, lb, sizeof(lb)), *v = jstr(sf, "sms_parts");
	long long now = now_mono_ms(), cap = atoll(jstr(sf, "sms_max_per_hour") ? jstr(sf, "sms_max_per_hour") : "10");
	int parts = v && *v ? atoi(v) : 3;
	char whole[1200], body[600];

	if (!from || !text || !(nums = forward_numbers(sf)))
		return;
	if (!json_object_array_length(nums)) {
		json_object_put(nums);
		return;
	}
	if (cap <= 0)
		cap = 10;
	if (!fwd_ms)
		fwd_ms = json_object_new_array();
	while (json_object_array_length(fwd_ms) && now - json_object_get_int64(json_object_array_get_idx(fwd_ms, 0)) >= 3600000)
		json_object_array_del_idx(fwd_ms, 0, 1);
	if ((long long)json_object_array_length(fwd_ms) >= cap) {
		ULOG_WARN("sms forward by SMS: over sms_max_per_hour, not sent\n");
		mm_history_add(&history, from, (long long)time(NULL), now, "sms-forward", "by SMS: over the hourly limit");
		json_object_put(nums);
		return;
	}
	json_object_array_add(fwd_ms, json_object_new_int64(now));
	/* «↪ T99 · MegaFon: text», up to 3 parts of Cyrillic by default (67 characters a part, the phone joins them; the owner,
	 * 2026-11-06: «надо бы по-умолчанию, чтоб склеивалось на телефоне»), one SMS (70) when asked */
	snprintf(whole, sizeof(whole), MM_FORWARD_MARK "%s \xc2\xb7 %s: %s", name && *name ? name : from, sender && *sender ? sender : "?", text);
	cut_chars(whole, parts <= 1 ? 70 : (size_t)(parts > 3 ? 3 : parts) * 67, body, sizeof(body));
	if (!queue)
		queue = json_object_new_array();
	n = json_object_new_object();
	json_object_object_add(n, "kind", json_object_new_string("forward"));
	json_object_object_add(n, "text", json_object_new_string(body));
	json_object_object_add(n, "numbers", nums);
	json_object_object_add(n, "from", json_object_new_string(from));
	json_object_object_add(n, "at_ms", json_object_new_int64(now));
	json_object_array_add(queue, n);
	send_next();
}

void alert_tick(const char *default_dev)
{
	json_object *obs, *links, *got, *names;
	const char *carrier = NULL;
	char host[64];
	struct mm_alert_cfg cfg;
	size_t i;

	if (!enabled()) {
		/* turned off: what waits goes, but a test the page asked for */
		for (i = queue ? json_object_array_length(queue) : 0; i > 0; i--)
			if (strcmp(jstr(json_object_array_get_idx(queue, i - 1), "kind"), "test") &&
			    strcmp(jstr(json_object_array_get_idx(queue, i - 1), "kind"), "forward") && !(running && i == 1))
				json_object_array_del_idx(queue, i - 1, 1);
		send_next();
		json_object_put(state);
		state = NULL;
		json_object_put(levels);
		levels = NULL;
		return;
	}
	if (!state)
		state = json_object_new_object();
	cfg.on[MM_ALERT_WHITELIST] = on("whitelist");
	cfg.on[MM_ALERT_DOWN] = on("down");
	cfg.on[MM_ALERT_SWITCH] = on("switch");
	cfg.on[MM_ALERT_RESTORE] = on("restore");
	cfg.whitelist_ms = minutes("whitelist_min", 15);
	cfg.down_ms = minutes("down_min", 5);
	cfg.switch_ms = minutes("switch_min", 2);
	cfg.clear_ms = minutes("clear_min", 3);
	obs = json_object_new_object();
	links = json_object_new_object();
	if (levels) {
		json_object_object_foreach(levels, id, lv)
			json_object_object_add(links, id, json_object_get(lv));
	}
	if (uplinks) {
		json_object_object_foreach(uplinks, key, st) {
			const char *dev = jstr(st, "dev"), *lv = jstr(json_object_object_get(st, "reach"), "level");

			/* the carrier: the link of the default route, a modem's key before the bare device's */
			if (dev && default_dev && !strcmp(dev, default_dev) && (strncmp(key, "dev:", 4) || !carrier))
				carrier = key;
			if (!strncmp(key, "dev:", 4) && lv)
				json_object_object_add(links, key, json_object_new_string(!strcmp(lv, "ok") || !strcmp(lv, "limited") ? lv : "none"));
		}
	}
	if (carrier)
		json_object_object_add(obs, "carrier", json_object_new_string(carrier));
	else if (default_dev && *default_dev) {
		char k[IFNAMSIZ + 8];

		snprintf(k, sizeof(k), "dev:%s", default_dev);
		json_object_object_add(obs, "carrier", json_object_new_string(k));
	}
	json_object_object_add(obs, "links", links);
	got = mm_alert_step(state, obs, &cfg, now_mono_ms());
	if (json_object_array_length(got)) {
		bool r = ru();

		names = json_object_new_object();
		host_name(host, sizeof(host));
		for (i = 0; i < json_object_array_length(got); i++) {
			json_object *e = json_object_array_get_idx(got, i);
			char text[320];

			name_one(names, jstr(e, "link"), r);
			name_one(names, jstr(e, "from"), r);
			name_one(names, jstr(e, "to"), r);
			mm_alert_text(e, names, host, r, text, sizeof(text));
			enqueue(jstr(e, "kind"), text);
		}
		json_object_put(names);
	}
	json_object_put(got);
	json_object_put(obs);
	send_next();
}

void alert_mgr_stop(void)
{
	if (running) {
		uloop_process_delete(&proc);
		kill(-proc.pid, SIGTERM);
		running = false;
		unlink(job_path);
	}
	json_object_put(state);
	json_object_put(levels);
	json_object_put(queue);
	json_object_put(recent);
	json_object_put(sent_ms);
	json_object_put(fwd_ms);
	state = levels = queue = recent = sent_ms = fwd_ms = NULL;
}

enum { AL_TEST, AL_FORWARD, __AL_MAX };
const struct blobmsg_policy alerts_policy[__AL_MAX] = {
	[AL_TEST] = { .name = "test", .type = BLOBMSG_TYPE_BOOL },
	[AL_FORWARD] = { .name = "forward", .type = BLOBMSG_TYPE_BOOL },
};

/* {enabled, recipients, senders: [{device_id, name}], queued, recent}; test: a test SMS to the recipients now */
int m_alerts(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	     const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__AL_MAX];
	json_object *o = json_object_new_object(), *nums = numbers(), *ids = senders(), *who = json_object_new_array();
	size_t i;
	int rc;

	(void)obj;
	(void)method;
	blobmsg_parse(alerts_policy, __AL_MAX, tb, blob_data(msg), blob_len(msg));
	/* spec §43: the forwarding's test SMS, to its own recipients */
	if (tb[AL_TEST] && blobmsg_get_bool(tb[AL_TEST]) && tb[AL_FORWARD] && blobmsg_get_bool(tb[AL_FORWARD])) {
		json_object *fn = forward_numbers(json_object_object_get(medvemodem_uci(), "sms_forward"));

		if (!json_object_array_length(fn))
			json_object_object_add(o, "refused", json_object_new_string("no recipients: choose them from the address book"));
		else if (!json_object_array_length(ids))
			json_object_object_add(o, "refused", json_object_new_string("no modem to send through"));
		else {
			json_object *n = json_object_new_object();

			json_object_object_add(n, "kind", json_object_new_string("test"));
			json_object_object_add(n, "text", json_object_new_string(ru() ? MM_FORWARD_MARK "MedveModem: проверка пересылки SMS" :
										MM_FORWARD_MARK "MedveModem: a test of the SMS forwarding"));
			json_object_object_add(n, "numbers", json_object_get(fn));
			json_object_object_add(n, "at_ms", json_object_new_int64(now_mono_ms()));
			if (!queue)
				queue = json_object_new_array();
			json_object_array_add(queue, n);
			send_next();
			json_object_object_add(o, "test", json_object_new_string("queued"));
		}
		json_object_put(fn);
		tb[AL_TEST] = NULL;
	}
	if (tb[AL_TEST] && blobmsg_get_bool(tb[AL_TEST])) {
		if (!json_object_array_length(nums))
			json_object_object_add(o, "refused", json_object_new_string("no recipients: choose them from the address book"));
		else if (!json_object_array_length(ids))
			json_object_object_add(o, "refused", json_object_new_string("no modem to send through"));
		else {
			json_object *e = json_object_new_object();
			char host[64], text[320];

			host_name(host, sizeof(host));
			json_object_object_add(e, "kind", json_object_new_string("test"));
			mm_alert_text(e, NULL, host, ru(), text, sizeof(text));
			json_object_put(e);
			enqueue("test", text);
			json_object_object_add(o, "test", json_object_new_string("queued"));
		}
	}
	for (i = 0; i < json_object_array_length(ids); i++) {
		char lb[64];
		/* the owner's r161 screenshot: «Отправит: usb-port:platform/soc/5310000.usb@1, …»: an unnamed modem by its model */
		const char *id = json_object_get_string(json_object_array_get_idx(ids, i)), *n = daemon_modem_label(id, lb, sizeof(lb));
		json_object *s = json_object_new_object();

		json_object_object_add(s, "device_id", json_object_new_string(id));
		if (n && *n)
			json_object_object_add(s, "name", json_object_new_string(n));
		json_object_array_add(who, s);
	}
	json_object_object_add(o, "enabled", json_object_new_boolean(enabled()));
	json_object_object_add(o, "recipients", json_object_new_int64((long long)json_object_array_length(nums)));
	json_object_object_add(o, "senders", who);
	json_object_object_add(o, "queued", json_object_new_int64(queue ? (long long)json_object_array_length(queue) : 0));
	json_object_object_add(o, "recent", recent ? json_object_get(recent) : json_object_new_array());
	json_object_put(nums);
	json_object_put(ids);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}
