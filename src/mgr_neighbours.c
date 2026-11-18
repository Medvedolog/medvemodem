// SPDX-License-Identifier: GPL-3.0-only
/*
 * Other modem helpers on this router (the owner, 2026-10-28: testers install MedveModem over routers already set up
 * with other modem managers, USB re-init helpers, the Polish packages, their own qmi scripts). The helper `neighbours` lists the
 * known packages found by their files, the processes outside the engine's stack holding a modem's node open, and the
 * operator's own hotplug, rc.local and cron scripts that touch modems. Run every NB_EVERY_MS and on request; nothing
 * is stopped or changed by it (the owner: «не молча все сносить»). An AT job already refuses a port another program
 * holds (at_lease's tty_holder); this says which program and which package it came with.
 */
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "daemon.h"

#define NB_FIRST_MS 20000
#define NB_EVERY_MS 600000

static json_object *last;	/* the helper's answer, holders tied to devices */
static long long last_at = -1;
static struct uloop_process proc;
static struct uloop_timeout tick;
static bool running;
static char out[512];

/* a holder's node to the device whose port it is */
static void tie(json_object *r)
{
	json_object *hs = json_object_object_get(r, "holders");

	for (size_t i = 0; hs && json_object_is_type(hs, json_type_array) && i < json_object_array_length(hs); i++) {
		json_object *h = json_object_array_get_idx(hs, i), *rec;
		const char *node = jstr(h, "node"), *b = node ? strrchr(node, '/') : NULL;

		if (b && (rec = mm_registry_lookup_node(&registry, b + 1)) && jstr(rec, "device_id"))
			json_object_object_add(h, "device_id", json_object_new_string(jstr(rec, "device_id")));
	}
}

/* what is new since the last run goes to the log once: a package or a program on a port */
static void tell(json_object *was, json_object *now)
{
	const char *lists[] = { "packages", "holders" }, *keys[] = { "id", "comm" };

	for (int l = 0; l < 2; l++) {
		json_object *a = json_object_object_get(now, lists[l]), *b = json_object_object_get(was, lists[l]);

		for (size_t i = 0; a && i < json_object_array_length(a); i++) {
			json_object *entry = json_object_array_get_idx(a, i);
			const char *k = jstr(entry, keys[l]);
			bool seen = false;
			/* A QModem-owned AT port held by its own tom_modem is expected,
			 * not an unauthorized third-party modem helper. */
			if (l == 1 && k && !strcmp(k, "tom_modem") && jstr(entry, "device_id")) {
				json_object *rec = mm_registry_find(&registry, jstr(entry, "device_id"));
				/* The live QModem section identifies its own AT helper.
				 * Registry records do not store the resolved owner field. */
				if (rec && qmodem_section_of(rec))
					continue;
			}

			for (size_t j = 0; k && b && j < json_object_array_length(b); j++)
				seen = seen || (jstr(json_object_array_get_idx(b, j), keys[l]) &&
						!strcmp(jstr(json_object_array_get_idx(b, j), keys[l]), k));
			if (k && !seen)
				ULOG_WARN(l ? "another program holds a modem port: %s (%s)\n" : "another modem helper is installed: %s\n",
					  k, l ? jstr(json_object_array_get_idx(a, i), "node") : "");
		}
	}
}

static void exited(struct uloop_process *p, int ret)
{
	json_object *r = json_object_from_file(out);

	(void)p;
	running = false;
	unlink(out);
	if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0 && r && json_object_is_type(r, json_type_object)) {
		tie(r);
		tell(last, r);
		json_object_put(last);
		last = r;
		r = NULL;
		last_at = now_mono_ms();
	}
	json_object_put(r);
}

void neighbours_refresh(void)
{
	char path[PATH_MAX];
	const char *prog = getenv("MEDVEMODEM_NEIGHBOURS");
	pid_t pid;

	if (running)
		return;
	if (prog && *prog)
		snprintf(path, sizeof(path), "%s", prog);
	else
		helper_path("neighbours", path, sizeof(path));
	snprintf(out, sizeof(out), "%s/neighbours.json", run_dir);
	mkdir(run_dir, 0755);
	pid = fork();
	if (pid == 0) {
		int fd = open(out, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

		setpgid(0, 0);
		if (fd < 0)
			_exit(127);
		dup2(fd, 1);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 2);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execl(path, path, (char *)NULL);
		_exit(127);
	}
	if (pid < 0)
		return;
	running = true;
	proc.pid = pid;
	proc.cb = exited;
	uloop_process_add(&proc);
}

static void ticked(struct uloop_timeout *t)
{
	neighbours_refresh();
	uloop_timeout_set(t, NB_EVERY_MS);
}

void neighbours_start(void)
{
	tick.cb = ticked;
	uloop_timeout_set(&tick, getenv("MEDVEMODEM_NEIGHBOURS") ? 200 : NB_FIRST_MS);
}

void neighbours_stop(void)
{
	uloop_timeout_cancel(&tick);
	if (running) {
		uloop_process_delete(&proc);
		kill(-proc.pid, SIGTERM);
		running = false;
	}
	json_object_put(last);
	last = NULL;
}

/* the programs on this modem's ports, or NULL (new array) */
json_object *neighbours_of(const char *id)
{
	json_object *hs = last ? json_object_object_get(last, "holders") : NULL, *o = NULL;

	for (size_t i = 0; id && hs && i < json_object_array_length(hs); i++) {
		json_object *h = json_object_array_get_idx(hs, i);

		if (!jstr(h, "device_id") || strcmp(jstr(h, "device_id"), id))
			continue;
		if (jstr(h, "comm") && !strcmp(jstr(h, "comm"), "tom_modem")) {
			json_object *rec = mm_registry_find(&registry, id);
			if (rec && qmodem_section_of(rec))
				continue;
		}
		if (!o)
			o = json_object_new_array();
		json_object_array_add(o, json_object_get(h));
	}
	return o;
}

enum { NB_REFRESH, __NB_MAX };
const struct blobmsg_policy neighbours_policy[1] = {
	[NB_REFRESH] = { .name = "refresh", .type = BLOBMSG_TYPE_BOOL },
};

/* {packages, holders, scripts, age_s, running}; refresh: run the helper now (the answer is the last one) */
int m_neighbours(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__NB_MAX];
	json_object *o = json_object_new_object();
	int rc;

	(void)obj;
	(void)method;
	blobmsg_parse(neighbours_policy, __NB_MAX, tb, blob_data(msg), blob_len(msg));
	if (tb[NB_REFRESH] && blobmsg_get_bool(tb[NB_REFRESH]))
		neighbours_refresh();
	if (last) {
		json_object_object_foreach(last, k, v)
			json_object_object_add(o, k, json_object_get(v));
	}
	json_object_object_add(o, "age_s", last_at >= 0 ? json_object_new_int64((now_mono_ms() - last_at) / 1000) : NULL);
	json_object_object_add(o, "running", json_object_new_boolean(running));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}
