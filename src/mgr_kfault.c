// SPDX-License-Identifier: GPL-3.0-only
/*
 * The router's health in the kernel's words (the owner, 2026-10-31, after the R3S hung with «HC died», «failed to set
 * voltage» and RCU stalls: «да обязательно и не только для r3s»). kfault.c names the lines; this module reads the
 * kernel ring every 30 s (klogctl, or MEDVEMODEM_KMSG_FILE in the tests), counts each kind once per line by the line's
 * time since boot, and says a kind the first time it shows in a boot: in the log, on the bus as medvemodem.kernel
 * {kind, critical, count, who, at} (the bot tells the admin) and in status.kernel (LuCI's banner). It acts on nothing:
 * a dead controller or a sagging supply is the owner's to see to, a USB reset does not bring a dead xHCI back.
 *
 * Files: <run>/kernel-faults.json (RAM) holds this boot's summary, so an engine restart says nothing twice; a critical
 * kind also writes it to <state>/kernel-faults.json (flash, once per kind per boot), so the next boot can tell what
 * the last one ended with: a hung router is restarted by hand, and the ring of the hung boot is gone. Kept: the kind,
 * its count, the first and last time, a short name (a controller, a regulator, a process). Never the line itself.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "daemon.h"
#include "kfault.h"

#define KF_EVERY_MS 30000
#define KF_FILE "kernel-faults.json"

static struct uloop_timeout kf_timer;
static json_object *kf_now;		/* {boot_id, kinds: {kind: {count, first, last, who?, critical}}} */
static json_object *kf_prev;		/* the last boot that had a critical kind, when it was another boot */
static double kf_last_t = -1;		/* the newest line counted, seconds since boot */
static bool kf_untimed_done;		/* lines without a time are counted on the first look only */
static char kf_boot[64];

static void kf_path(char *buf, size_t size, const char *dir)
{
	snprintf(buf, size, "%s/%s", dir, KF_FILE);
}

static void kf_save(const char *dir)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	mkdir(dir, 0755);
	kf_path(path, sizeof(path), dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	json_object_object_add(kf_now, "saved", json_object_new_int64((long long)time(NULL)));
	if (!json_object_to_file_ext(tmp, kf_now, JSON_C_TO_STRING_PLAIN))
		rename(tmp, path);
	else
		unlink(tmp);
}

static char *kf_read_log(void)
{
	const char *file = getenv("MEDVEMODEM_KMSG_FILE");
	char *log = NULL;

	if (file) {
		FILE *f = fopen(file, "r");
		long n;

		if (!f)
			return NULL;
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (n >= 0 && (log = calloc(1, (size_t)n + 1)) && fread(log, 1, (size_t)n, f) != (size_t)n) {
			free(log);
			log = NULL;
		}
		fclose(f);
		return log;
	}
	{
		int size = klogctl(10, NULL, 0), n;	/* SYSLOG_ACTION_SIZE_BUFFER */

		if (size <= 0 || !(log = calloc(1, (size_t)size + 1)))
			return NULL;
		n = klogctl(3, log, size);		/* SYSLOG_ACTION_READ_ALL */
		if (n < 0) {
			free(log);
			return NULL;
		}
		log[n] = 0;
	}
	return log;
}

static double kf_uptime(void)
{
	const char *env = getenv("MEDVEMODEM_KMSG_NOW");
	struct timespec ts;

	if (env)
		return strtod(env, NULL);
	clock_gettime(CLOCK_BOOTTIME, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void kf_say(const char *kind, json_object *k)
{
	json_object *e = json_object_new_object();
	const char *who = jstr(k, "who");

	ULOG_WARN("kernel: %s%s%s (%s)\n", kind, who ? " on " : "", who ? who : "",
		  mm_kfault_critical(kind) ? "the router needs a look: power, cooling or a restart" : "a warning");
	json_object_object_add(e, "kind", json_object_new_string(kind));
	json_object_object_add(e, "critical", json_object_new_boolean(mm_kfault_critical(kind)));
	json_object_object_add(e, "count", json_object_get(json_object_object_get(k, "count")));
	if (who)
		json_object_object_add(e, "who", json_object_new_string(who));
	json_object_object_add(e, "at", json_object_get(json_object_object_get(k, "first")));
	broadcast("medvemodem.kernel", e);
	json_object_put(e);
}

static void kf_look(void)
{
	char *log = kf_read_log(), *line, *save = NULL;
	double up = kf_uptime(), newest = kf_last_t;
	long long now = (long long)time(NULL);
	json_object *kinds = json_object_object_get(kf_now, "kinds");
	bool fresh_critical = false, changed = false;

	if (!log)
		return;
	for (line = strtok_r(log, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		const char *text, *kind;
		char who[48];
		double t = mm_kfault_split(line, &text);
		json_object *k;
		long long at;

		if ((t >= 0 && t <= kf_last_t) || (t < 0 && kf_untimed_done))
			continue;
		if (t > newest)
			newest = t;
		if (!(kind = mm_kfault_kind(text, who, sizeof(who))))
			continue;
		at = t >= 0 && up >= t ? now - (long long)(up - t) : now;
		if (!(k = json_object_object_get(kinds, kind))) {
			k = json_object_new_object();
			json_object_object_add(k, "count", json_object_new_int(0));
			json_object_object_add(k, "first", json_object_new_int64(at));
			json_object_object_add(k, "critical", json_object_new_boolean(mm_kfault_critical(kind)));
			if (*who)
				json_object_object_add(k, "who", json_object_new_string(who));
			json_object_object_add(kinds, kind, k);
			json_object_object_add(k, "said", json_object_new_boolean(0));
		}
		json_object_object_add(k, "count", json_object_new_int(json_object_get_int(json_object_object_get(k, "count")) + 1));
		json_object_object_add(k, "last", json_object_new_int64(at));
		changed = true;
	}
	kf_untimed_done = true;
	kf_last_t = newest;
	json_object_object_add(kf_now, "last_t", json_object_new_double(kf_last_t));
	free(log);
	json_object_object_foreach(kinds, kind, k) {
		if (json_object_get_boolean(json_object_object_get(k, "said")))
			continue;
		json_object_object_add(k, "said", json_object_new_boolean(1));
		kf_say(kind, k);
		if (mm_kfault_critical(kind))
			fresh_critical = true;
	}
	if (changed)
		kf_save(run_dir);
	if (fresh_critical)
		kf_save(state_dir);
}

static void kf_tick(struct uloop_timeout *t)
{
	kf_look();
	uloop_timeout_set(t, uplink_override_ms ? uplink_override_ms : KF_EVERY_MS);
}

void kfault_start(void)
{
	const char *bf = getenv("MEDVEMODEM_BOOT_ID_FILE");
	char path[PATH_MAX];
	FILE *f = fopen(bf ? bf : "/proc/sys/kernel/random/boot_id", "r");
	json_object *run, *flash;

	*kf_boot = 0;
	if (f) {
		if (fgets(kf_boot, sizeof(kf_boot), f))
			kf_boot[strcspn(kf_boot, "\n")] = 0;
		fclose(f);
	}
	kf_path(path, sizeof(path), run_dir);
	run = json_object_from_file(path);
	kf_path(path, sizeof(path), state_dir);
	flash = json_object_from_file(path);
	/* this boot's summary after an engine restart: nothing said twice */
	if (run && *kf_boot && jstr(run, "boot_id") && !strcmp(jstr(run, "boot_id"), kf_boot) &&
	    json_object_is_type(json_object_object_get(run, "kinds"), json_type_object)) {
		kf_now = run;
		kf_last_t = json_object_get_double(json_object_object_get(run, "last_t"));
		kf_untimed_done = true;
		run = NULL;
	} else {
		kf_now = json_object_new_object();
		json_object_object_add(kf_now, "boot_id", json_object_new_string(kf_boot));
		json_object_object_add(kf_now, "kinds", json_object_new_object());
	}
	json_object_put(run);
	/* the flash copy of another boot: what the last troubled boot ended with */
	if (flash && jstr(flash, "boot_id") && strcmp(jstr(flash, "boot_id"), kf_boot) &&
	    json_object_is_type(json_object_object_get(flash, "kinds"), json_type_object))
		kf_prev = flash;
	else
		json_object_put(flash);
	kf_timer.cb = kf_tick;
	uloop_timeout_set(&kf_timer, uplink_override_ms ? uplink_override_ms : 5000);
}

void kfault_stop(void)
{
	uloop_timeout_cancel(&kf_timer);
	json_object_put(kf_now);
	json_object_put(kf_prev);
	kf_now = kf_prev = NULL;
}

/* a copy of a summary's kinds without the bookkeeping («said») */
static json_object *kf_kinds(json_object *summary)
{
	json_object *out = json_object_new_object(), *kinds = summary ? json_object_object_get(summary, "kinds") : NULL;

	if (!kinds)
		return out;
	json_object_object_foreach(kinds, kind, k) {
		json_object *c = json_object_new_object();

		json_object_object_foreach(k, key, v)
			if (strcmp(key, "said"))
				json_object_object_add(c, key, json_object_get(v));
		json_object_object_add(out, kind, c);
	}
	return out;
}

/* {kinds: {kind: {count, first, last, who?, critical}}, previous_boot?: {kinds, saved}} (a new object) */
json_object *kfault_status(void)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "kinds", kf_kinds(kf_now));
	if (kf_prev) {
		json_object *p = json_object_new_object();

		json_object_object_add(p, "kinds", kf_kinds(kf_prev));
		json_object_object_add(p, "saved", json_object_get(json_object_object_get(kf_prev, "saved")));
		json_object_object_add(o, "previous_boot", p);
	}
	return o;
}
