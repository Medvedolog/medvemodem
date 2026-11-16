// SPDX-License-Identifier: GPL-3.0-only
/*
 * The modem services beside the engine: ModemManager and QModem (the owner, 2026-10-30: «память MedveModem,
 * ModemManager, QModem и самого бота … и про падения ModemManager и QModem, если используется, за сутки статистику и в
 * реальном времени присылать факт рестарта»).
 *
 * Every 30 s the process table is looked at: ModemManager by its name, QModem by its scripts (a command line under
 * /usr/share/qmodem/ or a program named qmodem*). Served: running, the main pid, the memory (RSS, summed for QModem's
 * processes), the time up, the restarts and the times it went away in the last 24 h. A restart (a new pid, or back
 * after it was gone) and a stop are broadcast as medvemodem.service {name, what, by}: "medvemodem" when the engine
 * itself had just asked for it (its ModemManager restart, a QModem hook), else "unknown" (a crash or someone's hand).
 * QModem is served only once its processes were seen: a router without it says nothing about it.
 *
 * The owner, 2026-10-31 («RSS всех движков и процессов вокруг Медвемодема, их pid, rss и количество респаунов»): the
 * Telegram bot is looked at the same way (its ucode by the script's path; its restarts are said in the log and on the
 * bus, not in the modems' event history), and the engine's own starts since boot are kept in <run>/starts (RAM: one
 * line «s <epoch>» a start, «x <epoch>» a clean stop, the last PROCS_STARTS lines). A start after a start with no
 * stop between is a crash that procd respawned.
 */
#include <ctype.h>
#include <limits.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <libubox/ulog.h>

#include "daemon.h"

#define PROCS_EVERY_MS 30000
#define PROCS_EXPECT_MS 120000
#define PROCS_RING 32
#define PROCS_STARTS 64
#ifndef MM_PROC_UPTIME
#define PROCS_UPTIME "/proc/uptime"
#else
#define PROCS_UPTIME MM_PROC_UPTIME
#endif

struct svc {
	const char *name;
	bool seen, running;
	int pid, count;			/* the main (oldest) pid, the processes */
	long long rss_kb, start_ticks;
	char sig[256];			/* QModem: its processes' command lines and pids, to tell a restart */
	long long expect_until;		/* the engine asked for a restart until then (monotonic ms) */
	long long restarts[PROCS_RING], downs[PROCS_RING];	/* epoch seconds */
	unsigned nr, nd;
	long long last_at;
	char last_by[16];
	bool quiet;			/* not written in the modems' event history */
};

static struct svc svcs[] = { { .name = "modemmanager" }, { .name = "qmodem" }, { .name = "bot", .quiet = true } };
#define NSVC (sizeof(svcs) / sizeof(svcs[0]))

/* the engine's own starts since boot (<run>/starts) */
static struct { int starts, crashes; long long first, last, last_crash, recent[PROCS_RING]; unsigned nr; } self_starts;
static struct uloop_timeout procs_timer;

static struct svc *svc_by(const char *name)
{
	for (size_t i = 0; i < sizeof(svcs) / sizeof(svcs[0]); i++)
		if (!strcmp(svcs[i].name, name))
			return &svcs[i];
	return NULL;
}

void procs_expect(const char *name)
{
	struct svc *s = svc_by(name);

	if (s)
		s->expect_until = now_mono_ms() + PROCS_EXPECT_MS;
}

static bool read_file(const char *path, char *buf, size_t size, size_t *len)
{
	FILE *f = fopen(path, "r");
	size_t n;

	if (!f)
		return false;
	n = fread(buf, 1, size - 1, f);
	fclose(f);
	buf[n] = 0;
	if (len)
		*len = n;
	return true;
}

/* VmRSS in kB and the start time in clock ticks of one process */
static void proc_usage(const char *pid, long long *rss_kb, long long *ticks)
{
	char path[PATH_MAX], buf[2048], *p;
	size_t n;

	snprintf(path, sizeof(path), "%s/%s/status", proc_dir, pid);
	if (read_file(path, buf, sizeof(buf), &n) && (p = strstr(buf, "VmRSS:")))
		*rss_kb += atoll(p + 6);
	snprintf(path, sizeof(path), "%s/%s/stat", proc_dir, pid);
	if (read_file(path, buf, sizeof(buf), &n) && (p = strrchr(buf, ')'))) {
		/* after «) »: state is field 3, starttime field 22 */
		int field = 2;

		for (p++; *p && field < 22; p++)
			if (*p == ' ')
				field++;
		if (field == 22)
			*ticks = atoll(p);
	}
}

static bool is_bot(const char *cmdline)
{
	return strstr(cmdline, "/medvemodem-bot/main.uc") != NULL;
}

static bool is_qmodem(const char *comm, const char *cmdline)
{
	return !strncmp(comm, "qmodem", 6) || strstr(cmdline, "/usr/share/qmodem/") || strstr(cmdline, "/usr/libexec/qmodem");
}

static void ring_add(long long *ring, unsigned *n, long long at)
{
	ring[*n % PROCS_RING] = at;
	(*n)++;
}

static int ring_24h(const long long *ring, unsigned n, long long now)
{
	int c = 0;

	for (unsigned i = 0; i < n && i < PROCS_RING; i++)
		if (now - ring[i] < 86400)
			c++;
	return c;
}

/* one of the pids of old (",1,2,") is still in now */
static bool any_left(const char *old, const char *now)
{
	char tok[24];

	for (const char *p = old; *p == ','; ) {
		size_t n = strcspn(p + 1, ",");

		if (!n || n + 3 > sizeof(tok))
			break;
		snprintf(tok, sizeof(tok), ",%.*s,", (int)n, p + 1);
		if (strstr(now, tok))
			return true;
		p += n + 1;
	}
	return false;
}

static void say(struct svc *s, const char *what, const char *by)
{
	json_object *e = json_object_new_object();

	ULOG_WARN("%s: %s%s%s\n", s->name, what, strcmp(by, "medvemodem") ? "" : " (asked by MedveModem)",
		  strcmp(by, "medvemodem") ? " (not by MedveModem: a crash or by hand)" : "");
	json_object_object_add(e, "name", json_object_new_string(s->name));
	json_object_object_add(e, "what", json_object_new_string(what));
	json_object_object_add(e, "by", json_object_new_string(by));
	json_object_object_add(e, "restarts_24h", json_object_new_int(ring_24h(s->restarts, s->nr, (long long)time(NULL))));
	if (s->pid)
		json_object_object_add(e, "pid", json_object_new_int(s->pid));
	broadcast("medvemodem.service", e);
	json_object_put(e);
	/* the event history of every modem attached now (LuCI's «Журнал событий», as the engine's own ModemManager
	 * restart): «modemmanager by=unknown» */
	if (registry.devices && !s->quiet) {
		char detail[64];

		snprintf(detail, sizeof(detail), "%s %s", s->name, by);
		json_object_object_foreach(registry.devices, id, rec)
			if (json_object_get_boolean(json_object_object_get(rec, "present")))
				mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(),
					       !strcmp(what, "stopped") ? "service-stop" : "service-restart", detail);
		history_dirty = true;
	}
}

static void procs_look(void)
{
	struct { int pid, count; long long rss, ticks; char sig[256]; } now[NSVC] = { { 0 } };
	DIR *d = opendir(proc_dir);
	struct dirent *de;
	long long t = (long long)time(NULL), mono = now_mono_ms();

	if (!d)
		return;
	while ((de = readdir(d))) {
		char path[PATH_MAX], comm[64] = "", cmd[512] = "";
		size_t n = 0;
		int k = -1, pid;
		long long ticks = 0;

		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		pid = atoi(de->d_name);
		snprintf(path, sizeof(path), "%s/%s/comm", proc_dir, de->d_name);
		if (!read_file(path, comm, sizeof(comm), NULL))
			continue;
		comm[strcspn(comm, "\n")] = 0;
		snprintf(path, sizeof(path), "%s/%s/cmdline", proc_dir, de->d_name);
		if (read_file(path, cmd, sizeof(cmd), &n))
			for (size_t i = 0; i + 1 < n; i++)
				if (!cmd[i])
					cmd[i] = ' ';
		if (!strncmp(comm, mm_process, 15))
			k = 0;
		else if (is_qmodem(comm, cmd))
			k = 1;
		else if (is_bot(cmd))
			k = 2;
		if (k < 0)
			continue;
		proc_usage(de->d_name, &now[k].rss, &ticks);
		now[k].count++;
		if (!now[k].pid || ticks < now[k].ticks) {
			now[k].pid = pid;
			now[k].ticks = ticks;
		}
		/* QModem's set: a restart of its service gives each of its scripts a new pid */
		if (k == 1 && strlen(now[k].sig) + 16 < sizeof(now[k].sig))
			snprintf(now[k].sig + strlen(now[k].sig), sizeof(now[k].sig) - strlen(now[k].sig), "%s%d,",
				 now[k].sig[0] ? "" : ",", pid);
	}
	closedir(d);
	for (int k = 0; k < (int)NSVC; k++) {
		struct svc *s = &svcs[k];
		bool up = now[k].count > 0, ours = mono < s->expect_until;
		const char *by = ours ? "medvemodem" : "unknown";
		bool restarted = false;

		if (up && s->seen && !s->running)
			restarted = true;	/* back after it was gone */
		else if (up && s->running && k != 1 && now[k].pid != s->pid)
			restarted = true;	/* a new ModemManager, a new bot */
		else if (up && s->running && k == 1 && now[k].pid != s->pid && !any_left(s->sig, now[k].sig))
			restarted = true;	/* none of QModem's old processes left */
		if (!up && s->running) {
			ring_add(s->downs, &s->nd, t);
			s->running = false;
			s->rss_kb = 0;
			s->count = 0;
			say(s, "stopped", by);
			continue;
		}
		if (!up)
			continue;
		s->pid = now[k].pid;
		s->count = now[k].count;
		s->rss_kb = now[k].rss;
		s->start_ticks = now[k].ticks;
		snprintf(s->sig, sizeof(s->sig), "%s", now[k].sig);
		if (restarted) {
			ring_add(s->restarts, &s->nr, t);
			s->last_at = t;
			snprintf(s->last_by, sizeof(s->last_by), "%s", by);
			s->running = true;
			say(s, "restarted", by);
		}
		s->running = s->seen = true;
	}
}

static void procs_tick(struct uloop_timeout *t)
{
	procs_look();
	uloop_timeout_set(t, uplink_override_ms ? uplink_override_ms : PROCS_EVERY_MS);
}

static void starts_path(char *buf, size_t size)
{
	snprintf(buf, size, "%s/starts", run_dir);
}

/* read <run>/starts, count it, and append this start (or, stopping, «x») keeping the last PROCS_STARTS lines */
static void starts_note(char what)
{
	char path[PATH_MAX], line[64], keep[PROCS_STARTS][24];
	int n = 0, i;
	char prev = 0;
	FILE *f;

	starts_path(path, sizeof(path));
	memset(&self_starts, 0, sizeof(self_starts));
	if ((f = fopen(path, "r"))) {
		while (fgets(line, sizeof(line), f)) {
			long long at = atoll(line + 1);

			if ((line[0] != 's' && line[0] != 'x') || line[1] != ' ' || at <= 0)
				continue;
			if (n == PROCS_STARTS) {
				memmove(keep[0], keep[1], sizeof(keep[0]) * (PROCS_STARTS - 1));
				n--;
			}
			snprintf(keep[n++], sizeof(keep[0]), "%c %lld", line[0], at);
		}
		fclose(f);
	}
	if (n == PROCS_STARTS) {
		memmove(keep[0], keep[1], sizeof(keep[0]) * (PROCS_STARTS - 1));
		n--;
	}
	snprintf(keep[n++], sizeof(keep[0]), "%c %lld", what, (long long)time(NULL));
	for (i = 0; i < n; i++) {
		long long at = atoll(keep[i] + 2);

		if (keep[i][0] == 's') {
			self_starts.starts++;
			if (!self_starts.first)
				self_starts.first = at;
			self_starts.last = at;
			if (prev == 's') {
				self_starts.crashes++;
				self_starts.last_crash = at;
			}
			if (prev)
				ring_add(self_starts.recent, &self_starts.nr, at);
		}
		prev = keep[i][0];
	}
	mkdir(run_dir, 0755);
	if ((f = fopen(path, "w"))) {
		for (i = 0; i < n; i++)
			fprintf(f, "%s\n", keep[i]);
		fclose(f);
	}
}

void procs_start(void)
{
	starts_note('s');
	procs_timer.cb = procs_tick;
	uloop_timeout_set(&procs_timer, uplink_override_ms ? uplink_override_ms : 10000);	/* not at once: an idle engine stays idle */
}

void procs_stop(void)
{
	uloop_timeout_cancel(&procs_timer);
	starts_note('x');
}

json_object *procs_status(void)
{
	json_object *o = json_object_new_object();
	long long t = (long long)time(NULL), hz = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100;
	double boot_up = 0;
	char buf[64];

	if (read_file(PROCS_UPTIME, buf, sizeof(buf), NULL))
		boot_up = atof(buf);
	for (size_t i = 0; i < sizeof(svcs) / sizeof(svcs[0]); i++) {
		struct svc *s = &svcs[i];
		json_object *e;

		if (!s->seen)
			continue;
		e = json_object_new_object();
		json_object_object_add(e, "running", json_object_new_boolean(s->running));
		if (s->running) {
			json_object_object_add(e, "pid", json_object_new_int(s->pid));
			json_object_object_add(e, "processes", json_object_new_int(s->count));
			json_object_object_add(e, "rss_kb", json_object_new_int64(s->rss_kb));
			if (boot_up > 0 && s->start_ticks > 0 && boot_up - (double)s->start_ticks / (double)hz >= 0)
				json_object_object_add(e, "up_s", json_object_new_int64((long long)(boot_up - (double)s->start_ticks / (double)hz)));
		}
		json_object_object_add(e, "restarts_24h", json_object_new_int(ring_24h(s->restarts, s->nr, t)));
		json_object_object_add(e, "stops_24h", json_object_new_int(ring_24h(s->downs, s->nd, t)));
		if (s->last_at) {
			json_object_object_add(e, "last_restart", json_object_new_int64(s->last_at));
			json_object_object_add(e, "last_by", json_object_new_string(s->last_by));
		}
		json_object_object_add(o, s->name, e);
	}
	{
		json_object *e = json_object_new_object();

		json_object_object_add(e, "starts_since_boot", json_object_new_int(self_starts.starts));
		json_object_object_add(e, "crashes_since_boot", json_object_new_int(self_starts.crashes));
		json_object_object_add(e, "restarts_24h", json_object_new_int(ring_24h(self_starts.recent, self_starts.nr, t)));
		if (self_starts.last_crash)
			json_object_object_add(e, "last_crash", json_object_new_int64(self_starts.last_crash));
		if (boot_up > 0)
			json_object_object_add(e, "router_up_s", json_object_new_int64((long long)boot_up));
		json_object_object_add(o, "medvemodem", e);
	}
	return o;
}
