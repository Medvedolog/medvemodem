/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "at.h"
#include "atport.h"
#include "daemon.h"
#include "esim.h"
#include "lease.h"
#include "profile.h"
#include "ucistore.h"

/* ---- eSIM capability (docs/ESIM-INTEGRATION.md) ---- */

#define MM_ESIM_JOBS 4
#define MM_ESIM_PROBE_MS 10000
#define MM_ESIM_ACTION_MS 45000
#define MM_ESIM_PORT_MS 60000		/* the eUICC port probe: two passes over a modem's AT ports */
#define MM_ESIM_MAX_PORTS 8
static const char *esim_backend = MM_ESIM_BACKEND;
static int esim_timeout_ms = MM_ESIM_ACTION_MS;	/* -G, for tests */
static struct esim_job {
	bool active, deferred, status_mode, killed;
	char device_id[160], action[16], lease_id[24];
	char iccid[24];		/* enable: the profile to switch to; never logged, never in a reply */
	long long generation, started;
	int step;		/* 0: lpac probe, 1: eUICC port probe (AT), 2: the action on the target */
	int nports;		/* step 1: the AT ports leased for the probe */
	char port_node[MM_ESIM_MAX_PORTS][64], port_lease[MM_ESIM_MAX_PORTS][24];
	struct uloop_process proc;
	struct uloop_fd out;
	struct uloop_timeout kill;
	char *buf;
	size_t len, cap;
	struct ubus_context *ctx;
	struct ubus_request_data req;
	json_object *probe, *target;
} esim_jobs[MM_ESIM_JOBS];

/*
 * The tty that reached the eUICC, per device and attachment generation: a probe
 * costs seconds, and the port is renumbered only by a re-attachment.
 */
static struct esim_port {
	char id[160], node[64], format[12];
	long long generation;
} esim_ports[16];

static const struct esim_port *esim_port_cached(const char *id, long long generation)
{
	size_t i;

	for (i = 0; i < sizeof(esim_ports) / sizeof(esim_ports[0]); i++)
		if (esim_ports[i].id[0] && !strcmp(esim_ports[i].id, id) && esim_ports[i].generation == generation)
			return &esim_ports[i];
	return NULL;
}

static void esim_port_remember(const char *id, long long generation, const char *node, const char *format)
{
	size_t i, slot = 0;

	for (i = 0; i < sizeof(esim_ports) / sizeof(esim_ports[0]); i++) {
		if (!strcmp(esim_ports[i].id, id)) {
			slot = i;
			break;
		}
		if (!esim_ports[i].id[0])
			slot = i;
	}
	snprintf(esim_ports[slot].id, sizeof(esim_ports[slot].id), "%s", id);
	snprintf(esim_ports[slot].node, sizeof(esim_ports[slot].node), "%s", node);
	snprintf(esim_ports[slot].format, sizeof(esim_ports[slot].format), "%s", format ? format : "");
	esim_ports[slot].generation = generation;
}

static void esim_forget_port(const char *id)
{
	size_t i;

	for (i = 0; i < sizeof(esim_ports) / sizeof(esim_ports[0]); i++)
		if (!strcmp(esim_ports[i].id, id))
			memset(&esim_ports[i], 0, sizeof(esim_ports[i]));
}

/* where the engine opens a node: its device directory (-D) */
static void esim_dev_path(const char *node, char *out, size_t size)
{
	const char *base = node ? strrchr(node, '/') : NULL;

	snprintf(out, size, "%s/%s", dev_dir, base ? base + 1 : node ? node : "");
}

/* one eSIM operation per modem; it also keeps a bring-up off the modem */
bool esim_reserved(const char *id)
{
	int i;

	for (i = 0; id && i < MM_ESIM_JOBS; i++)
		if (esim_jobs[i].active && !strcmp(esim_jobs[i].device_id, id))
			return true;
	return false;
}

/* the operator's `config esim` section for this device, or NULL (new reference) */
static json_object *esim_settings(const char *id)
{
	static const char *const pk[] = { "medvemodem", NULL };
	char err[160];
	json_object *all = mm_uci_read(conf_dir, pk, err, sizeof(err)), *found = NULL;

	if (all) {
		json_object_object_foreach(json_object_object_get(all, "medvemodem"), k, v) {
			(void)k;
			if (!found && jstr(v, ".type") && !strcmp(jstr(v, ".type"), "esim") && jstr(v, "device_id") &&
			    !strcmp(jstr(v, "device_id"), id))
				found = json_object_get(v);
		}
	}
	json_object_put(all);
	return found;
}

static void esim_close_out(struct esim_job *j)
{
	if (j->out.fd >= 0) {
		if (j->out.registered)
			uloop_fd_delete(&j->out);
		close(j->out.fd);
		j->out.fd = -1;
	}
}

static void esim_drain(struct esim_job *j)
{
	char chunk[4096];
	ssize_t n;

	while (j->out.fd >= 0 && (n = read(j->out.fd, chunk, sizeof(chunk))) > 0) {
		if (j->len + (size_t)n + 1 > 256 * 1024)
			continue;
		if (j->len + (size_t)n + 1 > j->cap) {
			size_t cap = j->cap ? j->cap * 2 : 8192;
			char *nb;
			while (cap < j->len + (size_t)n + 1)
				cap *= 2;
			nb = realloc(j->buf, cap);
			if (!nb)
				continue;
			j->buf = nb;
			j->cap = cap;
		}
		memcpy(j->buf + j->len, chunk, (size_t)n);
		j->len += (size_t)n;
		j->buf[j->len] = '\0';
	}
}

static void esim_readable(struct uloop_fd *u, unsigned int events)
{
	struct esim_job *j = container_of(u, struct esim_job, out);

	(void)events;
	esim_drain(j);
	if (u->eof || u->error)
		esim_close_out(j);
}

static void esim_kill_fired(struct uloop_timeout *t)
{
	struct esim_job *j = container_of(t, struct esim_job, kill);

	/* only this job's process group: never another modem's lpac */
	if (j->proc.pid > 0) {
		j->killed = true;
		kill(-j->proc.pid, SIGKILL);
	}
}

static void esim_exited(struct uloop_process *p, int ret);

/* the backend, with the engine's target in its environment (none for probe) */
static bool esim_spawn(struct esim_job *j, const char *action, int timeout_ms)
{
	json_object *t = j->step == 2 ? j->target : NULL;
	char slot[16], path[300], ports[MM_ESIM_MAX_PORTS * 300];
	int fds[2], fd;
	pid_t pid;

	if (pipe2(fds, O_CLOEXEC))
		return false;
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return false;
	}
	if (pid == 0) {
		const char *sel = jstr(t, "selected");

		setpgid(0, 0);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 2);
		}
		dup2(fds[1], 1);
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		unsetenv("MM_ESIM_APDU");
		unsetenv("MM_ESIM_DEVICE");
		unsetenv("MM_ESIM_SLOT");
		unsetenv("MM_ESIM_PROXY");
		unsetenv("MM_ESIM_PORTS");
		unsetenv("MM_ESIM_ICCID");
		if (j->step == 2 && j->iccid[0])
			setenv("MM_ESIM_ICCID", j->iccid, 1);
		if (j->step == 1) {
			int k;

			ports[0] = '\0';
			for (k = 0; k < j->nports; k++) {
				esim_dev_path(j->port_node[k], path, sizeof(path));
				if (k)
					strcat(ports, " ");
				strcat(ports, path);
			}
			setenv("MM_ESIM_PORTS", ports, 1);
		}
		if (t && sel) {
			json_object *res = json_object_object_get(t, "resources");
			const char *dev = jstr(res, !strcmp(sel, "uqmi") ? "qmi" : sel);

			setenv("MM_ESIM_APDU", sel, 1);
			/* the bridge opens the tty itself, where the engine opens it */
			if (dev && !strcmp(sel, "at")) {
				esim_dev_path(dev, path, sizeof(path));
				setenv("MM_ESIM_DEVICE", path, 1);
			} else if (dev) {
				setenv("MM_ESIM_DEVICE", dev, 1);
			}
			snprintf(slot, sizeof(slot), "%d", json_object_get_int(json_object_object_get(t, "apdu_slot")));
			setenv("MM_ESIM_SLOT", slot, 1);
			if (json_object_get_boolean(json_object_object_get(t, "mbim_proxy")))
				setenv("MM_ESIM_PROXY", "1", 1);
		}
		execl(esim_backend, esim_backend, action, (char *)NULL);
		_exit(127);
	}
	close(fds[1]);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	j->len = 0;
	if (j->buf)
		j->buf[0] = '\0';	/* a step that prints nothing must not read the previous step's answer */
	j->killed = false;
	j->out.fd = fds[0];
	j->out.cb = esim_readable;
	uloop_fd_add(&j->out, ULOOP_READ);
	j->proc.pid = pid;
	j->proc.cb = esim_exited;
	uloop_process_add(&j->proc);
	j->kill.cb = esim_kill_fired;
	uloop_timeout_set(&j->kill, timeout_ms);
	return true;
}

static void esim_finish(struct esim_job *j, json_object *reply)
{
	int k;

	for (k = 0; k < j->nports; k++)
		if (j->port_lease[k][0])
			mm_lease_release(&leases, j->port_lease[k]);
	if (j->lease_id[0])
		mm_lease_release(&leases, j->lease_id);
	if (j->lease_id[0] || j->nports)
		lease_rearm();
	json_object_object_add(reply, "elapsed_ms", json_object_new_int64(now_mono_ms() - j->started));
	/* no command, device argument or secret goes to the log: action and outcome only */
	ULOG_INFO("esim %s %s: %s\n", j->action, j->device_id,
		  json_object_get_boolean(json_object_object_get(reply, "ok")) ? "ok" :
		  jstr(reply, "error") ? jstr(reply, "error") : "?");
	if (j->deferred) {
		reply_json(j->ctx, &j->req, NULL, reply);
		ubus_complete_deferred_request(j->ctx, &j->req, UBUS_STATUS_OK);
	}
	json_object_put(reply);
	free(j->buf);
	json_object_put(j->probe);
	json_object_put(j->target);
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
}

static json_object *esim_reply(struct esim_job *j, const char *error, const char *detail)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "device_id", json_object_new_string(j->device_id));
	json_object_object_add(o, "action", json_object_new_string(j->action));
	json_object_object_add(o, "ok", json_object_new_boolean(!error));
	if (error)
		json_object_object_add(o, "error", json_object_new_string(error));
	if (detail)
		json_object_object_add(o, "detail", json_object_new_string(detail));
	if (j->probe)
		json_object_object_add(o, "lpac", json_object_get(j->probe));
	if (j->target)
		json_object_object_add(o, "target", json_object_get(j->target));
	return o;
}

/* an exclusive tty lease on node for this operation; NULL and the lease id, or why not */
static const char *esim_lease(struct esim_job *j, json_object *rec, const char *node, int ttl_ms, char *id,
			      size_t id_size, char *detail, size_t size)
{
	json_object *srv, *roles = NULL, *req, *lease, *revoked = NULL;
	const char *why = NULL;

	detail[0] = '\0';
	if (!node || !node_still_owned(node, jstr(rec, "attachment_id")))
		return "port is not on this device any more";
	srv = served(rec);
	json_object_object_get_ex(srv, "port_roles", &roles);
	req = json_object_new_object();
	json_object_object_add(req, "device_id", json_object_new_string(j->device_id));
	json_object_object_add(req, "port", json_object_new_string(node));
	json_object_object_add(req, "holder", json_object_new_string("medvemodemd-esim"));
	json_object_object_add(req, "purpose", json_object_new_string("esim"));
	json_object_object_add(req, "mode", json_object_new_string("exclusive"));
	json_object_object_add(req, "ttl_ms", json_object_new_int(ttl_ms));
	lease = mm_lease_acquire(&leases, req, roles, now_mono_ms(), &why, detail, size, &revoked);
	json_object_put(req);
	json_object_put(srv);
	leases_revoked(revoked, "preempted");
	json_object_put(revoked);
	if (!lease)
		return why;
	snprintf(id, id_size, "%s", jstr(lease, "lease_id"));
	json_object_put(lease);
	lease_rearm();
	return NULL;
}

/* the transport's resource is the engine's to give: AT goes under a tty lease */
static const char *esim_claim(struct esim_job *j, json_object *rec, char *detail, size_t size)
{
	detail[0] = '\0';
	if (strcmp(jstr(j->target, "selected"), "at"))
		return NULL;
	return esim_lease(j, rec, jstr(json_object_object_get(j->target, "resources"), "at"), esim_timeout_ms + 5000,
			  j->lease_id, sizeof(j->lease_id), detail, size);
}

/*
 * After a profile switch some modems keep the old profile until restarted: they ignore the eUICC's REFRESH
 * (a field report: the Foxconn SDX55 family, T99W175 and DW5930e among them). The reply says so; the restart
 * is the operator's press on «Перезапустить модем» or the recovery ladder's, never the eSIM module's own.
 */
static bool esim_restart_after_switch(const char *vidpid)
{
	static const char *const ids[] = { "05c6:90d5", "05c6:9025", "413c:81d7", "413c:81e0", "413c:81e4", "0489:e0b5",
					    "1e2d:00b3", "1e2d:00b7", "1e2d:00b8", "1e2d:00b9", "0489:e0f0", "1e2d:00f1",
					    "1e2d:00f2", "1e2d:00f3", "1e2d:00f4", NULL };

	for (int i = 0; vidpid && ids[i]; i++)
		if (!strcasecmp(vidpid, ids[i]))
			return true;
	return false;
}

/* the answer to the operation from a normalized result r (taken) */
static void esim_conclude(struct esim_job *j, json_object *r)
{
	json_object *o;

	if (!strcmp(j->action, "status")) {
		/* status: an eUICC answered, none did, or it is not known why not */
		json_object *eu = json_object_new_object(), *data = json_object_object_get(r, "data");
		bool ok = json_object_get_boolean(json_object_object_get(r, "ok"));
		bool absent = !ok && !strcmp(jstr(r, "error"), "euicc_absent");

		o = esim_reply(j, ok || absent ? NULL : jstr(r, "error"), jstr(r, "detail"));
		json_object_object_add(eu, "state", json_object_new_string(ok ? "present" : absent ? "absent" : "unknown"));
		if (ok && jstr(data, "eidValue"))
			json_object_object_add(eu, "eid", json_object_new_string(jstr(data, "eidValue")));
		json_object_object_add(o, "euicc", eu);
	} else {
		o = esim_reply(j, json_object_get_boolean(json_object_object_get(r, "ok")) ? NULL : jstr(r, "error"),
			       jstr(r, "detail"));
		if (json_object_object_get(r, "data") && strcmp(j->action, "enable"))
			json_object_object_add(o, "data", json_object_get(json_object_object_get(r, "data")));
		/* a switch: the SIM is read anew, the connection drops for a while; some modems need a restart for it */
		if (!strcmp(j->action, "enable") && json_object_get_boolean(json_object_object_get(r, "ok"))) {
			json_object *rec = mm_registry_find(&registry, j->device_id);

			json_object_object_add(o, "restart_advised",
					       json_object_new_boolean(esim_restart_after_switch(jstr(rec, "vidpid"))));
			daemon_sim_reread(j->device_id);
			mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "esim-switch", NULL);
		}
	}
	json_object_put(r);
	esim_finish(j, o);
}

static json_object *esim_failure(const char *error, const char *detail)
{
	json_object *r = json_object_new_object();

	json_object_object_add(r, "ok", json_object_new_boolean(false));
	json_object_object_add(r, "error", json_object_new_string(error));
	json_object_object_add(r, "detail", json_object_new_string(detail));
	return r;
}

/*
 * AT transport: which of the modem's AT ports reaches the eUICC. A port found
 * for this attachment generation is used again; otherwise every candidate port
 * the engine can lease now is probed (step 1). True: the known port is the
 * target, go on; false: the probe runs, or the job ended.
 */
static bool esim_choose_port(struct esim_job *j, json_object *rec)
{
	json_object *ports = json_object_object_get(j->target, "at_ports");
	const struct esim_port *known = esim_port_cached(j->device_id, j->generation);
	char detail[160];
	const char *why = NULL;
	size_t i;

	for (i = 0; known && i < json_object_array_length(ports); i++) {
		if (!strcmp(json_object_get_string(json_object_array_get_idx(ports, i)), known->node)) {
			json_object *e = json_object_new_object();

			json_object_object_add(json_object_object_get(j->target, "resources"), "at",
					       json_object_new_string(known->node));
			json_object_object_add(e, "node", json_object_new_string(known->node));
			json_object_object_add(e, "format", json_object_new_string(known->format));
			json_object_object_add(e, "source", json_object_new_string("cache"));
			json_object_object_add(j->target, "euicc_port", e);
			return true;
		}
	}
	j->nports = 0;
	for (i = 0; i < json_object_array_length(ports) && j->nports < MM_ESIM_MAX_PORTS; i++) {
		const char *node = json_object_get_string(json_object_array_get_idx(ports, i));

		/* a port another operation holds is not probed under it */
		if ((why = esim_lease(j, rec, node, MM_ESIM_PORT_MS + 5000, j->port_lease[j->nports],
				      sizeof(j->port_lease[0]), detail, sizeof(detail))))
			continue;
		snprintf(j->port_node[j->nports], sizeof(j->port_node[0]), "%s", node);
		j->nports++;
	}
	if (!j->nports) {
		esim_finish(j, esim_reply(j, "operation_busy", "every AT port of this modem is held by another operation"));
		return false;
	}
	j->step = 1;
	if (!esim_spawn(j, "euicc-port", MM_ESIM_PORT_MS)) {
		esim_finish(j, esim_reply(j, "lpac_error", "cannot start the eSIM backend"));
		return false;
	}
	return false;
}

/* step 1 ended: keep the lease of the port that answered, release the others */
static bool esim_port_found(struct esim_job *j, int status)
{
	json_object *r = j->buf ? json_tokener_parse(j->buf) : NULL, *e, *list, *verdicts;
	const char *found = jstr(r, "port");
	bool refused = false;
	int k, hit = -1;
	size_t i;

	if (j->killed) {
		json_object_put(r);
		esim_conclude(j, esim_failure("timeout", "the eUICC port probe did not finish in time"));
		return false;
	}
	if (status == 127 || !r) {
		json_object_put(r);
		esim_conclude(j, esim_failure("transport_unavailable", "the eSIM AT bridge is missing"));
		return false;
	}
	e = json_object_new_object();
	verdicts = json_object_new_array();
	list = json_object_object_get(r, "ports");
	for (i = 0; list && i < json_object_array_length(list); i++) {
		json_object *p = json_object_array_get_idx(list, i), *v = json_object_new_object();
		const char *verdict = jstr(p, "verdict");
		char path[300];

		for (k = 0; k < j->nports; k++) {
			esim_dev_path(j->port_node[k], path, sizeof(path));
			if (jstr(p, "node") && !strcmp(jstr(p, "node"), path)) {
				json_object_object_add(v, "node", json_object_new_string(j->port_node[k]));
				if (found && !strcmp(found, path))
					hit = k;
			}
		}
		json_object_object_add(v, "verdict", json_object_new_string(verdict ? verdict : "?"));
		refused = refused || (verdict && !strcmp(verdict, "refused"));
		json_object_array_add(verdicts, v);
	}
	json_object_object_add(e, "node", hit >= 0 ? json_object_new_string(j->port_node[hit]) : NULL);
	if (hit >= 0)
		json_object_object_add(e, "format", json_object_get(json_object_object_get(r, "format")));
	json_object_object_add(e, "source", json_object_new_string("probe"));
	json_object_object_add(e, "cleaned", json_object_get(json_object_object_get(r, "cleaned")));
	json_object_object_add(e, "ports", verdicts);
	json_object_object_add(j->target, "euicc_port", e);
	if (hit < 0) {
		json_object_put(r);
		/* a port that refused CCHO handles SIM commands and has no ISD-R; silence proves nothing */
		esim_conclude(j, refused ? esim_failure("euicc_absent", "no AT port opened a logical channel to the ISD-R")
					 : esim_failure("transport_unavailable", "no AT port of this modem answered AT+CCHO"));
		return false;
	}
	esim_port_remember(j->device_id, j->generation, j->port_node[hit], jstr(r, "format"));
	json_object_put(r);
	json_object_object_add(json_object_object_get(j->target, "resources"), "at",
			       json_object_new_string(j->port_node[hit]));
	snprintf(j->lease_id, sizeof(j->lease_id), "%s", j->port_lease[hit]);
	for (k = 0; k < j->nports; k++)
		if (k != hit)
			mm_lease_release(&leases, j->port_lease[k]);
	j->nports = 0;
	/* the action's own time on the port that answered */
	json_object_put(mm_lease_renew(&leases, j->lease_id, esim_timeout_ms + 5000, now_mono_ms(), j->generation));
	lease_rearm();
	return true;
}

static void esim_exited(struct uloop_process *p, int ret)
{
	struct esim_job *j = container_of(p, struct esim_job, proc);
	json_object *rec = mm_registry_find(&registry, j->device_id), *r;
	int status = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
	bool stale;

	uloop_timeout_cancel(&j->kill);
	esim_drain(j);
	esim_close_out(j);
	stale = !rec || !json_object_get_boolean(json_object_object_get(rec, "present")) ||
		json_object_get_int64(json_object_object_get(rec, "generation")) != j->generation;
	if (j->step == 0) {
		/* probe: which lpac is installed and which drivers it carries */
		j->probe = j->buf ? json_tokener_parse(j->buf) : NULL;
		if (status == 127) {
			esim_finish(j, esim_reply(j, "lpac_missing", "lpac is not installed"));
			return;
		}
		if (j->killed) {
			esim_finish(j, esim_reply(j, "timeout", "the lpac probe did not finish in time"));
			return;
		}
		if (!strcmp(j->action, "probe") || !strcmp(j->action, "version")) {
			esim_finish(j, esim_reply(j, j->probe ? NULL : "lpac_error", j->probe ? NULL : "no probe answer"));
			return;
		}
		if (stale) {
			esim_finish(j, esim_reply(j, "stale_generation", "the modem was re-attached"));
			return;
		}
		{
			json_object *srv = served(rec), *settings = esim_settings(j->device_id);
			char detail[160];
			const char *why;

			j->target = mm_esim_target(srv, j->probe, settings);
			json_object_put(srv);
			json_object_put(settings);
			if (jstr(j->target, "error")) {
				esim_finish(j, esim_reply(j, jstr(j->target, "error"), jstr(j->target, "reason")));
				return;
			}
			/* AT: the port found for this attachment, or a probe first (step 1) */
			if (!strcmp(jstr(j->target, "selected"), "at") && !esim_choose_port(j, rec))
				return;
			if ((why = esim_claim(j, rec, detail, sizeof(detail)))) {
				esim_finish(j, esim_reply(j, !strcmp(why, "busy") ? "operation_busy" : "transport_unavailable",
							  detail[0] ? detail : why));
				return;
			}
		}
		j->step = 2;
		if (!esim_spawn(j, !strcmp(j->action, "status") ? "chip" : j->action, esim_timeout_ms))
			esim_finish(j, esim_reply(j, "lpac_error", "cannot start the eSIM backend"));
		return;
	}
	if (stale) {
		esim_finish(j, esim_reply(j, "stale_generation", "the modem was re-attached during the operation"));
		return;
	}
	if (j->step == 1) {
		/* the eUICC port probe */
		if (!esim_port_found(j, status))
			return;
		j->step = 2;
		if (!esim_spawn(j, !strcmp(j->action, "status") ? "chip" : j->action, esim_timeout_ms))
			esim_finish(j, esim_reply(j, "lpac_error", "cannot start the eSIM backend"));
		return;
	}
	/* the action on the target */
	r = mm_esim_result(j->buf, status, j->killed);
	/* the port that answered before does not any more: probe again next time */
	if (!json_object_get_boolean(json_object_object_get(r, "ok")) && !strcmp(jstr(j->target, "selected"), "at"))
		esim_forget_port(j->device_id);
	esim_conclude(j, r);
}

enum { ES_ID, ES_ACTION, ES_ICCID, __ES_MAX };
const struct blobmsg_policy esim_policy[__ES_MAX] = {
	[ES_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[ES_ACTION] = { .name = "action", .type = BLOBMSG_TYPE_STRING },
	[ES_ICCID] = { .name = "iccid", .type = BLOBMSG_TYPE_STRING },
};

/* an ICCID as the eUICC lists it: 18-20 digits, a trailing F of the padding allowed */
static bool esim_iccid_ok(const char *s)
{
	size_t n = s ? strspn(s, "0123456789") : 0;

	return s && n >= 18 && n <= 20 && (!s[n] || ((s[n] == 'F' || s[n] == 'f') && !s[n + 1] && n == 19));
}



static int esim_begin(struct ubus_context *ctx, struct ubus_request_data *req, struct blob_attr *msg, bool status)
{
	struct blob_attr *tb[__ES_MAX];
	json_object *rec, *o;
	struct esim_job *j = NULL;
	const char *action, *id;
	int i, rc;

	blobmsg_parse(esim_policy, __ES_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[ES_ID] || (!status && !tb[ES_ACTION]))
		return UBUS_STATUS_INVALID_ARGUMENT;
	rec = mm_registry_find(&registry, blobmsg_get_string(tb[ES_ID]));
	if (!rec)
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	action = status ? "status" : blobmsg_get_string(tb[ES_ACTION]);
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "action", json_object_new_string(action));
	json_object_object_add(o, "ok", json_object_new_boolean(false));
	if (!status && !mm_esim_action_known(action)) {
		json_object_object_add(o, "error", json_object_new_string("unsupported_action"));
		json_object_object_add(o, "detail", json_object_new_string("this stage offers version, probe, chip, profiles and enable"));
		goto reply;
	}
	/* enable: a profile already on the chip, by its ICCID; download, delete and the rest wait for Alpha 2 */
	if (!status && !strcmp(action, "enable") && (!tb[ES_ICCID] || !esim_iccid_ok(blobmsg_get_string(tb[ES_ICCID])))) {
		json_object_object_add(o, "error", json_object_new_string("bad_iccid"));
		json_object_object_add(o, "detail", json_object_new_string("enable needs the ICCID of a profile on the chip"));
		goto reply;
	}
	if (!status && !strcmp(action, "enable") && frozen(id)) {
		json_object_object_add(o, "error", json_object_new_string("frozen"));
		json_object_object_add(o, "detail", json_object_new_string("the modem is frozen by the operator"));
		goto reply;
	}
	if (esim_reserved(id)) {
		json_object_object_add(o, "error", json_object_new_string("operation_busy"));
		goto reply;
	}
	if (bringup_busy(id)) {
		json_object_object_add(o, "error", json_object_new_string("owner_busy"));
		json_object_object_add(o, "detail", json_object_new_string("a bring-up or release of this modem is running"));
		goto reply;
	}
	for (i = 0; i < MM_ESIM_JOBS && !j; i++)
		if (!esim_jobs[i].active)
			j = &esim_jobs[i];
	if (!j) {
		json_object_object_add(o, "error", json_object_new_string("operation_busy"));
		json_object_object_add(o, "detail", json_object_new_string("too many eSIM operations are running"));
		goto reply;
	}
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
	j->active = true;
	j->status_mode = status;
	j->started = now_mono_ms();
	j->generation = json_object_get_int64(json_object_object_get(rec, "generation"));
	snprintf(j->device_id, sizeof(j->device_id), "%s", id);
	snprintf(j->action, sizeof(j->action), "%s", action);
	if (!strcmp(action, "enable"))
		snprintf(j->iccid, sizeof(j->iccid), "%s", blobmsg_get_string(tb[ES_ICCID]));
	if (!esim_spawn(j, "probe", esim_timeout_ms < MM_ESIM_PROBE_MS ? esim_timeout_ms : MM_ESIM_PROBE_MS)) {
		memset(j, 0, sizeof(*j));
		j->out.fd = -1;
		json_object_object_add(o, "error", json_object_new_string("lpac_error"));
		json_object_object_add(o, "detail", json_object_new_string("cannot start the eSIM backend"));
		goto reply;
	}
	json_object_put(o);
	j->ctx = ctx;
	ubus_defer_request(ctx, req, &j->req);
	j->deferred = true;
	return UBUS_STATUS_OK;
reply:
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

int m_esim_status(struct ubus_context *ctx, struct ubus_object *obj,
			 struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	(void)obj; (void)method;
	return esim_begin(ctx, req, msg, true);
}

int m_esim_action(struct ubus_context *ctx, struct ubus_object *obj,
			 struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	(void)obj; (void)method;
	return esim_begin(ctx, req, msg, false);
}

void esim_set_backend(const char *path)
{
	esim_backend = path;
}

void esim_set_timeout(int ms)
{
	esim_timeout_ms = ms > 0 ? ms : MM_ESIM_ACTION_MS;
}

void esim_mgr_start(void)
{
	for (int k = 0; k < MM_ESIM_JOBS; k++)
		esim_jobs[k].out.fd = -1;
}

void esim_mgr_stop(void)
{
	for (int k = 0; k < MM_ESIM_JOBS; k++)
		if (esim_jobs[k].active) {
			if (esim_jobs[k].proc.pid > 0)
				kill(-esim_jobs[k].proc.pid, SIGKILL);
			esim_close_out(&esim_jobs[k]);
			free(esim_jobs[k].buf);
			json_object_put(esim_jobs[k].probe);
			json_object_put(esim_jobs[k].target);
		}
}
