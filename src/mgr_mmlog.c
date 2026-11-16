// SPDX-License-Identifier: GPL-3.0-only
/*
 * ModemManager's own words about a port it cannot drive (the owner, 2026-10-27, as field recovery
 * triggers): «no longer controllable», «could not grab port», MBIM «NotOpened», «connection broken, reprobing» are
 * what a second master on the modem's port looks like (F24, the Nokia's umbim `lte`, HANDOFF 238). Shown in
 * «Диагностика» and the report as hints; nothing is started by them.
 *
 * One `logread -f -e ModemManager` (MEDVEMODEM_MMLOG names another program for the tests) follows the log while
 * ModemManager is in use; a line is tied to a modem by a port name it carries or by «[modemN]». Kept per device in RAM:
 * the kind, the port, the time and a count, at most MMLOG_MAX kinds; never the line itself (it may hold identifiers).
 */
#define _GNU_SOURCE	/* pipe2 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "daemon.h"
#include "mmlog.h"

#define MMLOG_MAX 8
#define MMLOG_RESPAWN_MS 30000
#define MMLOG_BACKLOG_MS 1500	/* logread -f prints the buffer's tail first: old lines are not news */

static json_object *hints;	/* device_id -> [{kind, port?, at, count}] */
static struct uloop_fd out = { .fd = -1 };
static struct uloop_process proc;
static struct uloop_timeout respawn;
static long long started_ms;
static char buf[2048];
static size_t len;

static const char *device_of(const char *line)
{
	char port[32];
	json_object *rec;
	int n;

	if (mm_mmlog_port(line, port, sizeof(port)) && (rec = mm_registry_lookup_node(&registry, port)))
		return jstr(rec, "device_id");
	if ((n = mm_mmlog_modem(line)) >= 0 && mm_devices) {
		char tail[24];

		snprintf(tail, sizeof(tail), "/Modem/%d", n);
		json_object_object_foreach(mm_devices, id, v) {
			const char *obj = jstr(v, "object");
			size_t ol = obj ? strlen(obj) : 0, tl = strlen(tail);

			if (ol >= tl && !strcmp(obj + ol - tl, tail))
				return id;
		}
	}
	return NULL;
}

static void note(const char *line)
{
	const char *kind = mm_mmlog_kind(line), *id;
	json_object *list, *e = NULL;
	char port[32] = "";
	size_t i;

	if (!kind || !(id = device_of(line)))
		return;
	mm_mmlog_port(line, port, sizeof(port));
	if (!hints)
		hints = json_object_new_object();
	if (!(list = json_object_object_get(hints, id))) {
		list = json_object_new_array();
		json_object_object_add(hints, id, list);
	}
	for (i = 0; i < json_object_array_length(list); i++) {
		json_object *h = json_object_array_get_idx(list, i);

		if (!strcmp(jstr(h, "kind"), kind) && !strcmp(jstr(h, "port") ? jstr(h, "port") : "", port)) {
			e = h;
			break;
		}
	}
	if (!e) {
		if (json_object_array_length(list) >= MMLOG_MAX)
			json_object_array_del_idx(list, 0, 1);
		e = json_object_new_object();
		json_object_object_add(e, "kind", json_object_new_string(kind));
		if (*port)
			json_object_object_add(e, "port", json_object_new_string(port));
		json_object_array_add(list, e);
		ULOG_INFO("%s: ModemManager says %s%s%s\n", id, kind, *port ? " on " : "", port);
	}
	json_object_object_add(e, "at", json_object_new_int64((long long)time(NULL)));
	json_object_object_add(e, "count", json_object_new_int64(json_object_get_int64(json_object_object_get(e, "count")) + 1));
}

static void readable(struct uloop_fd *f, unsigned int events)
{
	ssize_t n;

	(void)events;
	while ((n = read(f->fd, buf + len, sizeof(buf) - 1 - len)) > 0) {
		char *start = buf, *nl;

		len += (size_t)n;
		buf[len] = '\0';
		while ((nl = strchr(start, '\n'))) {
			*nl = '\0';
			if (now_mono_ms() - started_ms >= MMLOG_BACKLOG_MS)
				note(start);
			start = nl + 1;
		}
		len -= (size_t)(start - buf);
		memmove(buf, start, len);
		if (len == sizeof(buf) - 1)
			len = 0;	/* a line longer than the buffer: dropped */
	}
	if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
		uloop_fd_delete(f);
		close(f->fd);
		f->fd = -1;
	}
}

static void exited(struct uloop_process *p, int status)
{
	(void)p;
	(void)status;
	proc.pid = 0;
	if (out.fd >= 0) {
		uloop_fd_delete(&out);
		close(out.fd);
		out.fd = -1;
	}
	uloop_timeout_set(&respawn, MMLOG_RESPAWN_MS);
}

static void spawn(struct uloop_timeout *t)
{
	const char *prog = getenv("MEDVEMODEM_MMLOG") ? getenv("MEDVEMODEM_MMLOG") : "logread";
	int fds[2], fd;
	pid_t pid;

	(void)t;
	if (proc.pid || pipe2(fds, O_CLOEXEC))
		return;
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		uloop_timeout_set(&respawn, MMLOG_RESPAWN_MS);
		return;
	}
	if (pid == 0) {
		setpgid(0, 0);
		dup2(fds[1], 1);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 2);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execlp(prog, prog, "-f", "-l", "1", "-e", "ModemManager", (char *)NULL);
		_exit(127);
	}
	close(fds[1]);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	len = 0;
	started_ms = now_mono_ms();
	out.fd = fds[0];
	out.cb = readable;
	uloop_fd_add(&out, ULOOP_READ);
	memset(&proc, 0, sizeof(proc));
	proc.pid = pid;
	proc.cb = exited;
	uloop_process_add(&proc);
}

void mmlog_start(void)
{
	respawn.cb = spawn;
	spawn(NULL);
}

void mmlog_stop(void)
{
	uloop_timeout_cancel(&respawn);
	if (proc.pid) {
		uloop_process_delete(&proc);
		kill(-proc.pid, SIGTERM);
		kill(proc.pid, SIGTERM);
		waitpid(proc.pid, NULL, 0);
		proc.pid = 0;
	}
	if (out.fd >= 0) {
		uloop_fd_delete(&out);
		close(out.fd);
		out.fd = -1;
	}
	json_object_put(hints);
	hints = NULL;
}

void mmlog_retain(json_object *devices)
{
	json_object *gone = json_object_new_array();

	if (hints) {
		json_object_object_foreach(hints, id, v) {
			(void)v;
			if (!devices || !json_object_object_get(devices, id))
				json_object_array_add(gone, json_object_new_string(id));
		}
		for (size_t i = 0; i < json_object_array_length(gone); i++)
			json_object_object_del(hints, json_object_get_string(json_object_array_get_idx(gone, i)));
	}
	json_object_put(gone);
}

json_object *mmlog_of(const char *id)
{
	json_object *l = hints && id ? json_object_object_get(hints, id) : NULL;

	return l && json_object_array_length(l) ? json_object_get(l) : NULL;
}
