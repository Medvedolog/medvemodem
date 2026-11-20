// SPDX-License-Identifier: GPL-3.0-only
/*
 * mgr_wlmode.c: the whitelist mode (TECHNICAL-SPEC §44; the owner, 2026-11-03: «в случае наступления БС режима
 * переключать заданную админом прокси как основную … и включать безусловную маршрутизацию всех клиентов роутера»).
 * Owns: whether the mode is on, the admin's manual word, the times the carrier has been limited or whole, the last
 * answer of the helper (RAM). The hands are /usr/libexec/medvemodem/wlmode (podkop-exp or forkop 2); its ledger on
 * flash is what makes a crash or a reboot in the mode give back the manager's settings.
 *
 * On: the carrier limited (an operator's whitelist) for hold_s, or the admin's «on». Off: the carrier whole for
 * clear_s, unless the admin turned it on by hand, or the admin's «off» (which holds until the carrier is whole). While on, the pinned server is checked every
 * 5 min and moved to the next of the pool that answers. One helper at a time.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libubox/uloop.h>
#include <libubox/ulog.h>

#include "daemon.h"

#define WL_CHECK_MS (5 * 60 * 1000)
#define WL_RETRY_MS 60000
#define WL_HELPER_MS 120000

static struct uloop_process proc;
static struct uloop_timeout kill_timer;
static bool running, on, manual, known, held_off;	/* held_off: the admin's «off» lasts until the carrier is whole */
static char doing[8], out_path[PATH_MAX];
static long long lim_since, ok_since, next_check, next_try, since;
static json_object *last;	/* the helper's last answer */

static json_object *conf(void)
{
	return json_object_object_get(medvemodem_uci(), "wlmode");
}

static const char *opt(const char *k, const char *def)
{
	const char *v = jstr(conf(), k);

	return v && *v ? v : def;
}

static bool enabled(void)
{
	return !strcmp(opt("enabled", "0"), "1") && strcmp(opt("manager", ""), "") && strcmp(opt("section", ""), "");
}

static void notify(const char *what)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "event", json_object_new_string(what));
	json_object_object_add(o, "on", json_object_new_boolean(on));
	if (last && jstr(last, "server"))
		json_object_object_add(o, "server", json_object_new_string(jstr(last, "server")));
	if (last && jstr(last, "label") && *jstr(last, "label"))
		json_object_object_add(o, "label", json_object_new_string(jstr(last, "label")));
	broadcast("medvemodem.wlmode", o);
	json_object_put(o);
}

static void exited(struct uloop_process *p, int ret)
{
	json_object *r = json_object_from_file(out_path);
	bool ok = WIFEXITED(ret) && WEXITSTATUS(ret) == 0;
	long long now = now_mono_ms();

	(void)p;
	running = false;
	uloop_timeout_cancel(&kill_timer);
	unlink(out_path);
	if (!strcmp(doing, "status")) {
		known = true;
		on = r && jstr(r, "manager");
		if (on) {
			since = json_object_get_int64(json_object_object_get(r, "since"));
			ULOG_INFO("wlmode: the mode was on before this engine (%s): kept until the whitelist ends\n", jstr(r, "server"));
			next_check = now + WL_CHECK_MS;
		}
	} else if (!strcmp(doing, "on")) {
		if (ok && r) {
			bool was = on;

			on = true;
			if (!was)
				since = (long long)time(NULL);
			next_check = now + WL_CHECK_MS;
			ULOG_WARN("wlmode: on, server %s%s\n", jstr(r, "server") ? jstr(r, "server") : "?", manual ? " (by the admin)" : "");
			if (!was)
				notify("on");
		} else {
			ULOG_WARN("wlmode: could not turn on: %s\n", r && jstr(r, "error") ? jstr(r, "error") : "the helper failed");
			next_try = now + WL_RETRY_MS;
		}
	} else if (!strcmp(doing, "off")) {
		if (ok) {
			on = false;
			since = 0;
			ULOG_WARN("wlmode: off, the manager's settings given back\n");
			notify("off");
		} else {
			ULOG_WARN("wlmode: could not turn off: %s\n", r && jstr(r, "error") ? jstr(r, "error") : "the helper failed");
			next_try = now + WL_RETRY_MS;
		}
	} else if (!strcmp(doing, "check")) {
		next_check = now + WL_CHECK_MS;
		if (r && json_object_get_boolean(json_object_object_get(r, "moved"))) {
			ULOG_WARN("wlmode: the pinned server stopped answering: moved to %s\n", jstr(r, "server"));
			notify("server");
		}
	}
	/* the last word kept, the error with it; a status read keeps the one before */
	if (r && strcmp(doing, "status")) {
		json_object_put(last);
		last = json_object_get(r);
	}
	json_object_put(r);
	doing[0] = 0;
}

static void kill_fired(struct uloop_timeout *t)
{
	(void)t;
	if (running && proc.pid > 0)
		kill(-proc.pid, SIGKILL);
}

/* the helper with its words; argv ends with NULL */
static bool run(const char *what, const char *const *args)
{
	const char *prog = getenv("MEDVEMODEM_WLMODE");
	char path[PATH_MAX];
	pid_t pid;

	if (running)
		return false;
	if (prog && *prog)
		snprintf(path, sizeof(path), "%s", prog);
	else
		helper_path("wlmode", path, sizeof(path));
	mkdir(run_dir, 0755);
	snprintf(out_path, sizeof(out_path), "%s/wlmode.out", run_dir);
	pid = fork();
	if (pid == 0) {
		int fd = open("/dev/null", O_RDWR), out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

		setpgid(0, 0);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 2);
		}
		if (out >= 0)
			dup2(out, 1);
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execv(path, (char *const *)args);
		_exit(127);
	}
	if (pid < 0)
		return false;
	snprintf(doing, sizeof(doing), "%s", what);
	running = true;
	proc.pid = pid;
	proc.cb = exited;
	uloop_process_add(&proc);
	kill_timer.cb = kill_fired;
	uloop_timeout_set(&kill_timer, WL_HELPER_MS);
	return true;
}

static void turn_on(void)
{
	const char *args[24];
	json_object *list = json_object_object_get(conf(), "server");
	size_t n = 0, i;

	args[n++] = "wlmode";
	args[n++] = "on";
	args[n++] = opt("manager", "");
	args[n++] = opt("section", "");
	args[n++] = !strcmp(opt("lan", "0"), "1") ? "1" : "0";
	args[n++] = !strcmp(opt("router", "0"), "1") ? "1" : "0";
	if (json_object_is_type(list, json_type_array)) {
		for (i = 0; i < json_object_array_length(list) && n < 22; i++) {
			args[n++] = json_object_get_string(json_object_array_get_idx(list, i));
			if (!strcmp(opt("mode", "pool"), "one"))
				break;
		}
	} else if (json_object_is_type(list, json_type_string))
		args[n++] = json_object_get_string(list);
	/* podkop-exp's rule with fallback rows needs none: its fallbacks choose, the mode changes the routing only (the
	 * helper says «no server given» for a rule without them) */
	if (n == 6 && strcmp(opt("manager", ""), "podkop")) {
		ULOG_WARN("wlmode: no server chosen: the mode cannot turn on\n");
		next_try = now_mono_ms() + WL_CHECK_MS;
		return;
	}
	args[n] = NULL;
	run("on", args);
}

static void turn_off(void)
{
	static const char *const args[] = { "wlmode", "off", NULL };

	run("off", args);
}

void wlmode_tick(const char *level)
{
	long long now = now_mono_ms();
	bool limited = level && !strcmp(level, "limited"), whole = level && !strcmp(level, "ok");

	if (running)
		return;
	if (!known) {
		static const char *const args[] = { "wlmode", "status", NULL };

		run("status", args);
		return;
	}
	lim_since = limited ? (lim_since ? lim_since : now) : 0;
	ok_since = whole ? (ok_since ? ok_since : now) : 0;
	if (whole)
		held_off = false;
	if (now < next_try)
		return;
	if (!enabled()) {
		/* switched off with the mode on: the manager's settings back */
		if (on)
			turn_off();
		return;
	}
	if (!on && !manual && !held_off && !strcmp(opt("auto", "1"), "1") && lim_since &&
	    now - lim_since >= atoll(opt("hold_s", "60")) * 1000) {
		turn_on();
		return;
	}
	if (on && !manual && ok_since && now - ok_since >= atoll(opt("clear_s", "180")) * 1000) {
		turn_off();
		return;
	}
	if (on && now >= next_check) {
		static const char *const args[] = { "wlmode", "check", NULL };

		run("check", args);
	}
}

json_object *wlmode_view(void)
{
	json_object *o;

	if (!enabled() && !on)
		return NULL;
	o = json_object_new_object();
	json_object_object_add(o, "enabled", json_object_new_boolean(enabled()));
	json_object_object_add(o, "auto", json_object_new_boolean(!strcmp(opt("auto", "1"), "1")));
	json_object_object_add(o, "on", json_object_new_boolean(on));
	json_object_object_add(o, "manual", json_object_new_boolean(manual));
	if (since)
		json_object_object_add(o, "since", json_object_new_int64(since));
	json_object_object_add(o, "manager", json_object_new_string(opt("manager", "")));
	json_object_object_add(o, "section", json_object_new_string(opt("section", "")));
	json_object_object_add(o, "lan", json_object_new_boolean(!strcmp(opt("lan", "0"), "1")));
	json_object_object_add(o, "router", json_object_new_boolean(!strcmp(opt("router", "0"), "1")));
	if (running)
		json_object_object_add(o, "busy", json_object_new_string(doing));
	if (last) {
		if (jstr(last, "server"))
			json_object_object_add(o, "server", json_object_new_string(jstr(last, "server")));
		/* what the manager's own page calls it (podkop-exp: «Akenai_Priests: …» for «src1: …», the owner 2026-11-06) */
		if (jstr(last, "label") && *jstr(last, "label"))
			json_object_object_add(o, "label", json_object_new_string(jstr(last, "label")));
		if (json_object_object_get(last, "delay_ms"))
			json_object_object_add(o, "delay_ms", json_object_get(json_object_object_get(last, "delay_ms")));
		if (jstr(last, "error"))
			json_object_object_add(o, "error", json_object_new_string(jstr(last, "error")));
	}
	return o;
}

enum { WL_ACTION, __WL_MAX };
const struct blobmsg_policy wlmode_policy[1] = {
	[WL_ACTION] = { .name = "action", .type = BLOBMSG_TYPE_STRING },
};

/* ubus wlmode {action: status|on|off|auto}: the admin's word; «auto» gives the decision back to the whitelist */
int m_wlmode(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req, const char *method,
	     struct blob_attr *msg)
{
	struct blob_attr *tb[__WL_MAX];
	const char *a;
	json_object *o, *v;
	int rc;

	(void)obj;
	(void)method;
	blobmsg_parse(wlmode_policy, __WL_MAX, tb, blob_data(msg), blob_len(msg));
	a = tb[WL_ACTION] ? blobmsg_get_string(tb[WL_ACTION]) : "status";
	o = json_object_new_object();
	if (!strcmp(a, "on") || !strcmp(a, "off") || !strcmp(a, "auto")) {
		if (!enabled() && strcmp(a, "off")) {
			json_object_object_add(o, "result", json_object_new_string("refused"));
			json_object_object_add(o, "reason", json_object_new_string("the whitelist mode is not set up"));
		} else if (running) {
			json_object_object_add(o, "result", json_object_new_string("busy"));
		} else {
			manual = !strcmp(a, "on");
			held_off = !strcmp(a, "off");
			next_try = 0;
			if (!strcmp(a, "on"))
				turn_on();
			else if (!strcmp(a, "off") && on)
				turn_off();
			if (strcmp(a, "on"))
				manual = false;
			ULOG_INFO("wlmode: %s asked by the operator\n", a);
			json_object_object_add(o, "result", json_object_new_string("started"));
		}
	} else if (strcmp(a, "status")) {
		json_object_put(o);
		return UBUS_STATUS_INVALID_ARGUMENT;
	}
	v = wlmode_view();
	json_object_object_add(o, "wlmode", v ? v : json_object_new_object());
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

void wlmode_mgr_stop(void)
{
	if (running && proc.pid > 0) {
		uloop_process_delete(&proc);
		kill(-proc.pid, SIGKILL);
	}
	uloop_timeout_cancel(&kill_timer);
	json_object_put(last);
	last = NULL;
}
