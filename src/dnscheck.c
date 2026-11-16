// SPDX-License-Identifier: GPL-3.0-only
#include "dnscheck.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DNSCHECK_WAIT_MS 3000
#define DNSCHECK_TRIES 2

int mm_dnsq(unsigned char *buf, size_t size, const char *name, unsigned int id, int type, int klass)
{
	size_t n = 12, label;
	const char *p = name, *dot;

	if (!name || !*name || size < 12 + strlen(name) + 2 + 4)
		return 0;
	memset(buf, 0, 12);
	buf[0] = (unsigned char)(id >> 8);
	buf[1] = (unsigned char)id;
	buf[2] = 0x01;			/* recursion desired */
	buf[5] = 1;			/* one question */
	while (*p) {
		dot = strchr(p, '.');
		label = dot ? (size_t)(dot - p) : strlen(p);
		if (!label || label > 63)
			return 0;
		buf[n++] = (unsigned char)label;
		memcpy(buf + n, p, label);
		n += label;
		p += label + (dot ? 1 : 0);
	}
	buf[n++] = 0;
	buf[n++] = (unsigned char)(type >> 8);
	buf[n++] = (unsigned char)type;
	buf[n++] = (unsigned char)(klass >> 8);
	buf[n++] = (unsigned char)klass;
	return (int)n;
}

/* past a name at off (labels, or a pointer that ends it); 0 when it runs out of the message */
static size_t skip_name(const unsigned char *buf, size_t len, size_t off)
{
	while (off < len) {
		if (!buf[off])
			return off + 1;
		if ((buf[off] & 0xc0) == 0xc0)
			return off + 2 <= len ? off + 2 : 0;
		if (buf[off] & 0xc0)
			return 0;
		off += 1 + buf[off];
	}
	return 0;
}

int mm_dns_first(const unsigned char *buf, size_t len, unsigned int id, int type, char *out, size_t size)
{
	size_t off = 12;
	int q, an, i;

	if (out && size)
		*out = '\0';
	if (len < 12 || buf[0] != (unsigned char)(id >> 8) || buf[1] != (unsigned char)id || !(buf[2] & 0x80))
		return -1;
	q = buf[4] << 8 | buf[5];
	an = buf[6] << 8 | buf[7];
	for (i = 0; i < q; i++) {
		if (!(off = skip_name(buf, len, off)) || off + 4 > len)
			return buf[3] & 0x0f;
		off += 4;
	}
	for (i = 0; i < an; i++) {
		int t;
		size_t rd;

		if (!(off = skip_name(buf, len, off)) || off + 10 > len)
			break;
		t = buf[off] << 8 | buf[off + 1];
		rd = (size_t)(buf[off + 8] << 8 | buf[off + 9]);
		off += 10;
		if (off + rd > len)
			break;
		if (t == type && out && size) {
			size_t used = strlen(out);

			if (t == 1 && rd == 4 && used + 17 < size)
				snprintf(out + used, size - used, "%s%u.%u.%u.%u", used ? "," : "", buf[off], buf[off + 1], buf[off + 2], buf[off + 3]);
			else if (t == 16 && rd >= 1 && (size_t)buf[off] + 1 <= rd && !used)
				snprintf(out, size, "%.*s", (int)buf[off], (const char *)buf + off + 1);
			if (*out && t != 1)
				break;
		}
		off += rd;
	}
	return buf[3] & 0x0f;
}

/* "1.1.1.1", "1.1.1.1:5353", "2001:db8::1" or "[2001:db8::1]:53" */
static bool server_addr(const char *s, struct sockaddr_storage *sa, socklen_t *len)
{
	char host[64];
	const char *colon;
	long port = 53;

	memset(sa, 0, sizeof(*sa));
	if (!s || !*s || strlen(s) >= sizeof(host))
		return false;
	if (*s == '[') {
		const char *end = strchr(s, ']');
		if (!end)
			return false;
		snprintf(host, sizeof(host), "%.*s", (int)(end - s - 1), s + 1);
		if (end[1] == ':')
			port = strtol(end + 2, NULL, 10);
	} else if ((colon = strchr(s, ':')) && !strchr(colon + 1, ':')) {
		snprintf(host, sizeof(host), "%.*s", (int)(colon - s), s);
		port = strtol(colon + 1, NULL, 10);
	} else {
		snprintf(host, sizeof(host), "%s", s);
	}
	if (port <= 0 || port > 65535)
		return false;
	if (inet_pton(AF_INET, host, &((struct sockaddr_in *)sa)->sin_addr) == 1) {
		((struct sockaddr_in *)sa)->sin_family = AF_INET;
		((struct sockaddr_in *)sa)->sin_port = htons((unsigned short)port);
		*len = sizeof(struct sockaddr_in);
		return true;
	}
	if (inet_pton(AF_INET6, host, &((struct sockaddr_in6 *)sa)->sin6_addr) == 1) {
		((struct sockaddr_in6 *)sa)->sin6_family = AF_INET6;
		((struct sockaddr_in6 *)sa)->sin6_port = htons((unsigned short)port);
		*len = sizeof(struct sockaddr_in6);
		return true;
	}
	return false;
}

/* one question through dev, twice at most; the reply's RCODE, -2 no answer, -3 cannot ask (why in err) */
static int ask(const char *dev, const char *server, const char *name, int type, int klass, unsigned int id,
	       char *out, size_t size, char *err, size_t esize, int *ms)
{
	struct sockaddr_storage sa;
	socklen_t sl;
	unsigned char q[300], r[1500];
	int n = mm_dnsq(q, sizeof(q), name, id, type, klass), fd, tries, rc = -2;

	if (!n || !server_addr(server, &sa, &sl)) {
		snprintf(err, esize, "bad server or name");
		return -3;
	}
	fd = socket(sa.ss_family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0 || (dev && setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, dev, (socklen_t)strlen(dev) + 1))) {
		snprintf(err, esize, "%s", strerror(errno));
		if (fd >= 0)
			close(fd);
		return -3;
	}
	for (tries = 0; tries < DNSCHECK_TRIES && rc == -2; tries++) {
		struct timespec t0, t1;
		int left = DNSCHECK_WAIT_MS;

		if (sendto(fd, q, (size_t)n, 0, (struct sockaddr *)&sa, sl) < 0) {
			snprintf(err, esize, "%s", strerror(errno));
			rc = -3;
			break;
		}
		clock_gettime(CLOCK_MONOTONIC, &t0);
		while (left > 0) {
			struct pollfd p = { .fd = fd, .events = POLLIN };
			ssize_t got;
			int first;

			if (poll(&p, 1, left) <= 0)
				break;
			got = recv(fd, r, sizeof(r), 0);
			clock_gettime(CLOCK_MONOTONIC, &t1);
			if (got > 0 && (first = mm_dns_first(r, (size_t)got, id, type, out, size)) >= 0) {
				rc = first;
				if (ms)
					*ms = (int)((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
				break;
			}
			left = DNSCHECK_WAIT_MS - (int)((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
		}
	}
	close(fd);
	return rc;
}

static json_object *part(const char *state, const char *detail)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "state", json_object_new_string(state));
	if (detail && *detail)
		json_object_object_add(o, "detail", json_object_new_string(detail));
	return o;
}

/* a TCP handshake to addr:port through dev, a refusal counts (the far end answered); ms or -1 */
static int tcp_ms(const char *dev, const char *server, int port)
{
	struct sockaddr_storage sa;
	socklen_t sl;
	struct timespec t0, t1;
	struct pollfd p;
	int fd, ms = -1, err = 0;
	socklen_t el = sizeof(err);

	if (!server_addr(server, &sa, &sl))
		return -1;
	if (sa.ss_family == AF_INET)
		((struct sockaddr_in *)&sa)->sin_port = htons((unsigned short)port);
	else
		((struct sockaddr_in6 *)&sa)->sin6_port = htons((unsigned short)port);
	fd = socket(sa.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	if (dev && setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, dev, (socklen_t)strlen(dev) + 1)) {
		close(fd);
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (connect(fd, (struct sockaddr *)&sa, sl) && errno != EINPROGRESS && errno != ECONNREFUSED) {
		close(fd);
		return -1;
	}
	p.fd = fd;
	p.events = POLLOUT;
	if (poll(&p, 1, DNSCHECK_WAIT_MS) == 1 && !getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) && (!err || err == ECONNREFUSED)) {
		clock_gettime(CLOCK_MONOTONIC, &t1);
		ms = (int)((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
	}
	close(fd);
	return ms;
}

static int median3(int a, int b, int c)
{
	return a > b ? (b > c ? b : a > c ? c : a) : (a > c ? a : b > c ? c : b);
}

const char *mm_dns_rtt_verdict(int udp_ms, int tcp_ms)
{
	if (udp_ms < 0 || tcp_ms < 0)
		return "unclear";
	/* one round trip each; an answer in under half the handshake came from nearer than the server */
	return tcp_ms - udp_ms >= 10 && udp_ms * 2 < tcp_ms ? "intercepted" : "ok";
}

/* 0/8, 10/8, 100.64/10, 127/8, 169.254/16, 172.16/12, 192.168/16, 198.18/15, 224/3: never a public site */
bool mm_dns_bogon(const char *ip)
{
	unsigned int a, b;

	if (sscanf(ip, "%u.%u", &a, &b) != 2)
		return false;
	return a == 0 || a == 10 || a == 127 || a >= 224 || (a == 100 && b >= 64 && b < 128) || (a == 169 && b == 254) ||
	       (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168) || (a == 198 && (b == 18 || b == 19));
}

/* comma lists share an address */
static bool overlap(const char *x, const char *y)
{
	char a[256], *save = NULL, *w;

	snprintf(a, sizeof(a), "%s", x);
	for (w = strtok_r(a, ",", &save); w; w = strtok_r(NULL, ",", &save)) {
		size_t n = strlen(w);
		const char *p = y;

		while ((p = strstr(p, w))) {
			if ((p == y || p[-1] == ',') && (p[n] == ',' || !p[n]))
				return true;
			p += n;
		}
	}
	return false;
}

static bool safe_word(const char *s)
{
	return *s && strspn(s, "abcdefghijklmnopqrstuvwxyz0123456789.-:/[]ABCDEFGHIJKLMNOPQRSTUVWXYZ_") == strlen(s);
}

/* the truth through HTTPS (DoH, TLS verified), which a box on the way cannot rewrite; "" none, NULL cannot ask */
static char *doh(const char *dev, const char *url, const char *name, char *out, size_t size)
{
	char cmd[512];
	FILE *f;
	json_object *j, *ans;
	static char buf[8192];
	size_t n;
	const char *curl = getenv("MEDVEMODEM_CURL");

	*out = '\0';
	if (!dev || !safe_word(dev))
		return NULL;
	snprintf(cmd, sizeof(cmd), "%s -s --interface '%s' --max-time 8 -H 'accept: application/dns-json' '%s?name=%s&type=A' 2>/dev/null",
		 curl && *curl ? curl : "curl", dev, url, name);
	if (!(f = popen(cmd, "r")))
		return NULL;
	n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	if (pclose(f) != 0 || !(j = json_tokener_parse(buf)))
		return NULL;
	if (json_object_object_get_ex(j, "Answer", &ans) && json_object_is_type(ans, json_type_array)) {
		for (size_t i = 0; i < json_object_array_length(ans); i++) {
			json_object *r = json_object_array_get_idx(ans, i), *t, *d;
			size_t used = strlen(out);

			if (json_object_object_get_ex(r, "type", &t) && json_object_get_int(t) == 1 && json_object_object_get_ex(r, "data", &d) &&
			    used + strlen(json_object_get_string(d)) + 2 < size)
				snprintf(out + used, size - used, "%s%s", used ? "," : "", json_object_get_string(d));
		}
	}
	json_object_put(j);
	return out;
}


json_object *mm_dns_check(const char *dev, const char *list, const char *operator_dns, unsigned int seed)
{
	json_object *o = json_object_new_object(), *p, *res = json_object_new_array(), *names = json_object_new_array();
	char out[256], err[128] = "", name[64], detail[400], kinds[8][16], addrs[8][64], blocked[8][64], url[256] = "", line[256];
	char intercepted[400] = "", stub[800] = "", stubs[8][256], local[200] = "";
	int rc, i, nres = 0, nnames = 0, operator_ms = -1, answered = 0, nlocal = 0;
	FILE *f = fopen(list, "r");
	/* the device's round trip to the internet, measured by the engine (its reach check): unknown is -1 */
	const char *pm = getenv("MEDVEMODEM_DNSCHECK_PATH_MS");
	int path_ms = pm && *pm ? atoi(pm) : -1;

	/* the list: resolver|kind|address, name|<host>, doh|<url> */
	while (f && fgets(line, sizeof(line), f)) {
		char *a = strtok(line, "|\n"), *b = strtok(NULL, "|\n"), *c = strtok(NULL, "|\n");

		if (!a || *a == '#')
			continue;
		if (!strcmp(a, "resolver") && b && c && nres < 8 && safe_word(b) && safe_word(c)) {
			snprintf(kinds[nres], sizeof(kinds[nres]), "%s", b);
			snprintf(addrs[nres++], sizeof(addrs[0]), "%s", c);
		} else if (!strcmp(a, "name") && b && nnames < 8 && safe_word(b)) {
			snprintf(blocked[nnames++], sizeof(blocked[0]), "%s", b);
		} else if (!strcmp(a, "doh") && b && safe_word(b) && (!c || safe_word(c))) {
			snprintf(url, sizeof(url), "%s%s%s", b, c ? ":" : "", c ? c : "");
		}
	}
	if (f)
		fclose(f);

	json_object_object_add(o, "device", json_object_new_string(dev ? dev : ""));
	json_object_object_add(o, "at", json_object_new_int64((long long)time(NULL)));
	if (path_ms >= 0)
		json_object_object_add(o, "path_ms", json_object_new_int(path_ms));	/* what the local-resolver verdict used */

	/* 1. each public resolver: who answers, and how fast against a TCP handshake to the same address */
	for (i = 0; i < nres; i++) {
		json_object *r = json_object_new_object();
		int u[3] = { -1, -1, -1 }, t[3], k, um, tm;
		const char *idq = !strcmp(kinds[i], "google") ? "o-o.myaddr.l.google.com" : "id.server";
		const char *v;

		json_object_object_add(r, "kind", json_object_new_string(kinds[i]));
		json_object_object_add(r, "address", json_object_new_string(addrs[i]));
		*err = '\0';
		rc = ask(dev, addrs[i], idq, 16, strcmp(kinds[i], "google") ? 3 : 1, (seed + (unsigned)i) & 0xffff, out, sizeof(out), err,
			 sizeof(err), NULL);
		json_object_object_add(r, "identity", json_object_new_string(rc == -3 ? "error" : rc == -2 ? "no-answer" : *out ? out :
									   rc ? "refused" : "empty"));
		/* timed only when it answered at all: a silent server would cost 3 × 6 s for nothing */
		for (k = 0; k < 3 && rc >= 0; k++) {
			char x[256];

			ask(dev, addrs[i], "www.google.com", 1, 1, (seed * 7 + (unsigned)(i * 3 + k)) & 0xffff, x, sizeof(x), err, sizeof(err), &u[k]);
			t[k] = tcp_ms(dev, addrs[i], 443);
		}
		um = rc < 0 ? -1 : median3(u[0], u[1], u[2]);
		tm = rc < 0 ? -1 : median3(t[0], t[1], t[2]);
		json_object_object_add(r, "udp_ms", json_object_new_int(um));
		json_object_object_add(r, "tcp_ms", json_object_new_int(tm));
		v = mm_dns_rtt_verdict(um, tm);
		answered += rc >= 0;
		/*
		 * bench 2026-10-20: every modem «intercepted», 1.1.1.1 refusing id.server, DNS in 1 ms and TCP to 1.1.1.1:443
		 * in 0 ms. No operator answers from a cell in 3 ms: the router itself takes these connections (a proxy manager
		 * redirecting DNS and TCP), so nothing here tells about the operator.
		 */
		if (rc >= 0 && um >= 0 && um <= 3 && tm >= 0 && tm <= 3 && path_ms >= 15) {
			v = "local";
			if (!nlocal++)
				snprintf(local, sizeof(local), "%s answers in %d ms and takes TCP in %d ms, the path takes %d ms: this router answers, not the operator",
					 addrs[i], um, tm, path_ms);
		}
		/* Cloudflare names its site in three capitals: anything else answered on its behalf */
		else if (!strcmp(kinds[i], "cloudflare") && rc >= 0) {
			bool site = strlen(out) == 3;

			for (k = 0; site && out[k]; k++)
				site = out[k] >= 'A' && out[k] <= 'Z';
			if (!site) {
				v = "intercepted";
				if (!*intercepted)
					snprintf(intercepted, sizeof(intercepted), "%s does not answer as Cloudflare: %s", addrs[i],
						 *out ? out : rc ? "refused" : "empty answer");
			}
		}
		if (!strcmp(v, "intercepted") && !*intercepted)
			snprintf(intercepted, sizeof(intercepted), "%s answers in %d ms, TCP to it takes %d ms: the answer comes from closer than the server", addrs[i], um, tm);
		json_object_object_add(r, "verdict", json_object_new_string(v));
		json_object_array_add(res, r);
	}
	json_object_object_add(o, "resolvers", res);
	/* every server that answered is the router itself: the operator's DNS is not what is asked, nor seen */
	if (nlocal && nlocal == answered)
		p = part("local", local);
	else
		p = part(*intercepted ? "intercepted" : nres ? "ok" : "skipped", intercepted);
	json_object_object_add(o, "intercept", p);

	/* 2. a name that does not exist, asked of the operator's own server */
	if (nlocal && nlocal == answered) {
		p = part("skipped", "this router answers DNS itself");
	} else if (!operator_dns || !*operator_dns) {
		p = part("skipped", "no DNS server from the operator");
	} else {
		snprintf(name, sizeof(name), "mm-%08x%04x.com", seed * 2654435761u, (seed >> 7) & 0xffff);
		*err = '\0';
		rc = ask(dev, operator_dns, name, 1, 1, (seed >> 3) & 0xffff, out, sizeof(out), err, sizeof(err), &operator_ms);
		if (rc == -3)
			p = part("error", err);
		else if (rc == -2)
			p = part("no-answer", NULL);
		else if (rc == 0 && *out) {
			snprintf(detail, sizeof(detail), "%s → %s", name, out);
			p = part("rewritten", detail);
		} else {
			p = part("ok", NULL);
		}
		json_object_object_add(p, "server", json_object_new_string(operator_dns));
		if (operator_ms >= 0)
			json_object_object_add(p, "ms", json_object_new_int(operator_ms));
	}
	json_object_object_add(o, "nxdomain", p);

	/* 3. names a regulator blocks: plain DNS (the operator's, then each resolver) against DoH. A stub is an
	 * answer DoH does not give that no public site has (a bogon), or the same address for two blocked
	 * names; a CDN legitimately gives other resolvers other addresses, so a difference alone is nothing */
	{
		char cand[8][256], via[8][64], truth[8][256];
		bool have[8];
		json_object *plains[8];

		for (i = 0; i < nnames; i++) {
			int k;

			*cand[i] = *stubs[i] = *via[i] = '\0';
			have[i] = *url && doh(dev, url, blocked[i], truth[i], sizeof(truth[i]));
			plains[i] = json_object_new_object();
			for (k = -1; k < nres; k++) {
				const char *srv = k < 0 ? operator_dns : addrs[k];

				if (!srv || !*srv)
					continue;
				*out = '\0';
				rc = ask(dev, srv, blocked[i], 1, 1, (seed * 13 + (unsigned)(i * 9 + k + 1)) & 0xffff, out, sizeof(out), err,
					 sizeof(err), NULL);
				json_object_object_add(plains[i], srv, json_object_new_string(rc == -2 ? "no-answer" : rc == -3 ? "error" :
										     rc == 3 ? "NXDOMAIN" : rc ? "refused" : out));
				if (!have[i] || !*truth[i] || *stubs[i])
					continue;
				if (rc == 0 && *out && !overlap(out, truth[i])) {
					if (mm_dns_bogon(out))
						snprintf(stubs[i], sizeof(stubs[i]), "%s", out);
					else if (!*cand[i])
						snprintf(cand[i], sizeof(cand[i]), "%s", out);
					if (!*via[i])
						snprintf(via[i], sizeof(via[i]), "%s", srv);
				} else if (rc == 3) {
					snprintf(stubs[i], sizeof(stubs[i]), "NXDOMAIN");
					snprintf(via[i], sizeof(via[i]), "%s", srv);
				}
			}
		}
		for (i = 0; i < nnames; i++)
			for (int m = 0; m < nnames && !*stubs[i] && *cand[i]; m++)
				if (m != i && *cand[m] && overlap(cand[i], cand[m]))
					snprintf(stubs[i], sizeof(stubs[i]), "%s", cand[i]);
		for (i = 0; i < nnames; i++) {
			json_object *n = json_object_new_object();

			json_object_object_add(n, "name", json_object_new_string(blocked[i]));
			json_object_object_add(n, "doh", have[i] ? json_object_new_string(truth[i]) : NULL);
			json_object_object_add(n, "plain", plains[i]);
			json_object_object_add(n, "verdict", json_object_new_string(!have[i] ? "skipped" : *stubs[i] ? "stub" : "ok"));
			json_object_array_add(names, n);
			if (*stubs[i] && !*stub)
				snprintf(stub, sizeof(stub), "%s via %s → %s, DoH says %.100s", blocked[i], via[i], stubs[i],
					 *truth[i] ? truth[i] : "no address");
		}
	}
	json_object_object_add(o, "names", names);
	json_object_object_add(o, "blocked", nlocal && nlocal == answered ? part("skipped", "this router answers DNS itself") :
				  part(*stub ? "stub" : !nnames || !*url ? "skipped" : "ok", stub));
	return o;
}
