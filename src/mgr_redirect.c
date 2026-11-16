/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Who cut the connection: one plain HTTP request through the modem when its reach check turns limited or down with
 * the TLS hello cut right after the connection opened (or answered with something that is not TLS). An operator that
 * blocks a SIM (no money, a block, its own whitelist) sends every HTTP request to its own page: «302 Location:
 * http://internet-block.t2.ru/?p=SOP01» for a Tele2 SIM with a zero balance (a tester's OPiZ3, 2026-11-05). A 3xx to
 * another host than the one asked is the operator's page; a redirect to the same host (http → https) is not.
 *
 * The tester's conditions (BUGREPORT 2026-11-05 §9): only on the cut signature, an address that never redirects to
 * another host, once per change of the verdict, 3 s at most, neutral words («the operator restricted access»), the
 * page's host as the detail. Nothing is stored but the host; the request carries no identifier.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <libubox/ulog.h>
#include <libubox/uloop.h>

#include "daemon.h"

#ifndef MM_REDIRECT_PORT
#define MM_REDIRECT_PORT 80	/* tests: another port on lo */
#endif
/* the address asked: not the reach target. 8.8.8.8:80 itself answers «302 https://dns.google/», another host, and would be
 * taken for an operator's page; 1.1.1.1 sends to itself (https://1.1.1.1/) (the tester's BUGREPORT §9.2, r163 §10:
 * nothing was named while the SIM's every HTTP request went to internet-block.t2.ru) */
#ifndef MM_REDIRECT_ADDR
#define MM_REDIRECT_ADDR "1.1.1.1"	/* tests: 127.0.0.1 */
#endif
#define REDIRECT_JOBS 4
#define REDIRECT_WAIT_S 3

struct redirect_job {
	bool active;
	char key[128];
	int fd;			/* the child's answer: the host, or nothing */
	struct uloop_process proc;
};
static struct redirect_job jobs[REDIRECT_JOBS];

bool mm_redirect_foreign(const char *resp, const char *asked, char *out, size_t size)
{
	const char *p, *loc = NULL, *h;
	size_t n = 0;
	int code;

	if (!resp || !asked || !out || size < 2 || strncmp(resp, "HTTP/1.", 7) || !(p = strchr(resp, ' ')))
		return false;
	code = atoi(p + 1);
	if (code < 300 || code > 399)
		return false;
	for (p = resp; (p = strchr(p, '\n')); p++)
		if (!strncasecmp(p + 1, "Location:", 9)) {
			loc = p + 10;
			break;
		}
	if (!loc)
		return false;
	while (*loc == ' ' || *loc == '\t')
		loc++;
	/* a relative Location stays on the asked host */
	if (!(h = strstr(loc, "://")) || h - loc > 8)
		return false;
	for (h += 3; h[n] && h[n] != '/' && h[n] != ':' && h[n] != '?' && h[n] != '\r' && h[n] != '\n'; n++)
		if (!(isalnum((unsigned char)h[n]) || h[n] == '.' || h[n] == '-') || n + 1 >= size)
			return false;
	if (!n)
		return false;
	for (size_t i = 0; i < n; i++)
		out[i] = (char)tolower((unsigned char)h[i]);
	out[n] = 0;
	return strcmp(out, asked) != 0;
}

/* the child: GET / of the address through the device, the foreign host of a redirect on the pipe */
static void redirect_child(const char *dev, const char *ip, int wfd)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(MM_REDIRECT_PORT) };
	struct timeval tv = { .tv_sec = REDIRECT_WAIT_S };
	char req[256], buf[2048], host[64];
	ssize_t got;
	size_t have = 0;
	int s;

	alarm(REDIRECT_WAIT_S + 2);
	if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1 || (s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		_exit(1);
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	if (setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, dev, (socklen_t)strlen(dev) + 1) ||
	    connect(s, (struct sockaddr *)&sa, sizeof(sa))) {
		if (write(wfd, "=no connection", 14) < 0)
			_exit(1);
		_exit(1);
	}
	snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\nConnection: close\r\n\r\n", ip);
	if (write(s, req, strlen(req)) < 0)
		_exit(1);
	while (have < sizeof(buf) - 1 && (got = read(s, buf + have, sizeof(buf) - 1 - have)) > 0) {
		have += (size_t)got;
		buf[have] = 0;
		if (strstr(buf, "\r\n\r\n"))
			break;
	}
	buf[have] = 0;
	/* the answer's first line for the journal when it names no other host: the next report says what came back */
	if (!mm_redirect_foreign(buf, ip, host, sizeof(host))) {
		size_t n = strcspn(buf, "\r\n");

		snprintf(host, sizeof(host), "=%.*s", (int)(n < 60 ? n : 60), have ? buf : "no answer");
	}
	if (write(wfd, host, strlen(host)) < 0)
		_exit(1);
	_exit(0);
}

static void redirect_exited(struct uloop_process *p, int ret)
{
	struct redirect_job *j = container_of(p, struct redirect_job, proc);
	char host[64];
	ssize_t n = read(j->fd, host, sizeof(host) - 1);

	(void)ret;
	close(j->fd);
	j->active = false;
	if (n > 0 && host[0] != '=') {
		host[n] = 0;
		ULOG_INFO("reachability %s: plain HTTP is sent to %s: the operator restricted access\n", j->key, host);
		reach_redirect_found(j->key, host);
	} else {
		/* nothing named: said, so a report shows what the probe met (r163 §10: it left no trace) */
		host[n > 0 ? n : 0] = 0;
		ULOG_INFO("reachability %s: plain HTTP to %s:%d named no operator page (%s)\n", j->key, MM_REDIRECT_ADDR,
			  MM_REDIRECT_PORT, n > 1 ? host + 1 : "no answer");
	}
}

void redirect_probe(const char *key, const char *dev, const char *target)
{
	const char *ip = MM_REDIRECT_ADDR;
	int k, fds[2];
	pid_t pid;

	(void)target;
	if (!key || !dev)
		return;
	for (k = 0; k < REDIRECT_JOBS; k++)
		if (jobs[k].active && !strcmp(jobs[k].key, key))
			return;
	for (k = 0; k < REDIRECT_JOBS && jobs[k].active; k++)
		;
	if (k == REDIRECT_JOBS || pipe(fds))
		return;
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return;
	}
	if (pid == 0) {
		close(fds[0]);
		for (int fd = 3; fd < 1024; fd++)
			if (fd != fds[1])
				close(fd);
		redirect_child(dev, ip, fds[1]);
	}
	close(fds[1]);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	jobs[k].active = true;
	jobs[k].fd = fds[0];
	snprintf(jobs[k].key, sizeof(jobs[k].key), "%s", key);
	jobs[k].proc.pid = pid;
	jobs[k].proc.cb = redirect_exited;
	uloop_process_add(&jobs[k].proc);
}

void redirect_mgr_stop(void)
{
	for (int k = 0; k < REDIRECT_JOBS; k++)
		if (jobs[k].active) {
			uloop_process_delete(&jobs[k].proc);
			kill(jobs[k].proc.pid, SIGKILL);
			close(jobs[k].fd);
			jobs[k].active = false;
		}
}
