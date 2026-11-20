/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "identity.h"
#include "simid.h"
#include "websig.h"

/* ---- base64: the logins send the password hashed (SHA-256 in identity.c) or encoded ---- */

static void base64(const char *in, size_t len, char *out, size_t size)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const unsigned char *p = (const unsigned char *)in;
	size_t i, o = 0;

	for (i = 0; i < len && o + 5 < size; i += 3) {
		unsigned v = p[i] << 16 | (i + 1 < len ? p[i + 1] << 8 : 0) | (i + 2 < len ? p[i + 2] : 0);

		out[o++] = t[v >> 18 & 63];
		out[o++] = t[v >> 12 & 63];
		out[o++] = i + 1 < len ? t[v >> 6 & 63] : '=';
		out[o++] = i + 2 < len ? t[v & 63] : '=';
	}
	out[o] = '\0';
}

/* ---- a plain HTTP/1.1 exchange with the modem, through its own device ---- */

#define HTTP_MAX (256 * 1024)	/* a ZTE SMS list: 100 texts in UCS-2 hex */
#define CONNECT_MS 3000
#define TOTAL_MS 6000

struct web {
	const struct mm_web_args *a;
	char cookie[512];	/* "name=value; name=value" */
	char token[160];	/* Huawei's __RequestVerificationToken */
	char status;		/* 0 none yet */
	int zte_kind;		/* r96: which of zte_paths the ZTE web answers on */
	bool zte_known;
	int code;		/* the last HTTP status */
	char vhost[64];		/* the name the web answers to (an MF150-4 redirects its address to status.megafon.ru) */
	char body[HTTP_MAX];
};

static long long mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* "name=value" from a Set-Cookie line into the jar, replacing the same name */
static void cookie_set(char *jar, size_t size, const char *line)
{
	char pair[256], name[128], out[512] = "", *save = NULL, *tok, copy[512];
	size_t n = strcspn(line, ";\r\n");
	const char *eq;

	if (n == 0 || n >= sizeof(pair) || !(eq = memchr(line, '=', n)) || eq == line)
		return;
	memcpy(pair, line, n);
	pair[n] = '\0';
	snprintf(name, sizeof(name), "%.*s", (int)(eq - line), line);
	snprintf(copy, sizeof(copy), "%s", jar);
	for (tok = strtok_r(copy, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
		while (*tok == ' ')
			tok++;
		if (!*tok || (!strncmp(tok, name, strlen(name)) && tok[strlen(name)] == '='))
			continue;
		snprintf(out + strlen(out), sizeof(out) - strlen(out), "%s%s", *out ? "; " : "", tok);
	}
	snprintf(out + strlen(out), sizeof(out) - strlen(out), "%s%s", *out ? "; " : "", pair);
	snprintf(jar, size, "%s", out);
}

static int dechunk(char *b, size_t len)
{
	size_t in = 0, out = 0;

	while (in < len) {
		size_t digits = strspn(b + in, "0123456789abcdefABCDEF"), n;
		char *crlf = strstr(b + in, "\r\n");

		/* a size of 1..8 hex digits only: an unchecked strtoul of «ffffffffffffffff» wrapped in + n and gave memmove a
		 * size of -1 (audit 2026-10-21, ASan); a modem's web or anything on its address could send it */
		if (!crlf || !digits || digits > 8)
			return -1;
		n = (size_t)strtoul(b + in, NULL, 16);
		in = (size_t)(crlf - b) + 2;
		if (n == 0)
			break;
		if (in > len)
			return -1;
		if (n > len - in)
			n = len - in;	/* cut short (the read's buffer): what came is kept */
		memmove(b + out, b + in, n);
		out += n;
		in += n + 2;
	}
	b[out] = '\0';
	return (int)out;
}

static int http_once(struct web *w, const char *method, const char *path, const char *headers, const char *body);

/*
 * One request; the body in w->body, NUL-terminated; -1 when the modem does not answer. A web that answers its own
 * address with a redirect to a name of its own (the owner's MF150-4, 2026-10-30: «http://status.megafon.ru/» instead
 * of its pages, so neither the signal nor its kind was read) is asked again under that name, still at its address,
 * and keeps it for the requests after.
 */
static int http(struct web *w, const char *method, const char *path, const char *headers, const char *body)
{
	char loc[160], name[64];
	const char *h;
	int rc = http_once(w, method, path, headers, body);

	if ((rc != 301 && rc != 302 && rc != 303 && rc != 307 && rc != 308) || *w->vhost || !*w->body)
		return rc;
	/* http_once leaves the Location of a redirect in w->body as «Location: …» (the body itself is empty or a stub) */
	if (!(h = strstr(w->body, "\x01LOC ")))
		return rc;
	snprintf(loc, sizeof(loc), "%.*s", (int)strcspn(h + 5, "\r\n"), h + 5);
	if (strncasecmp(loc, "http://", 7))
		return rc;
	snprintf(name, sizeof(name), "%.*s", (int)strcspn(loc + 7, "/:?#"), loc + 7);
	/* a DNS name of letters, digits, dots and dashes, not an address: the web's own name */
	if (!*name || strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != strlen(name) ||
	    strspn(name, "0123456789.") == strlen(name) || !strchr(name, '.'))
		return rc;
	snprintf(w->vhost, sizeof(w->vhost), "%s", name);
	return http_once(w, method, path, headers, body);
}

/* a whole HTTP answer in buf: its headers and as much body as its Content-Length says, or a chunked body's last
 * chunk; false while more may come (no length: read to the close) */
static bool http_complete(const char *buf, size_t got)
{
	const char *end = strstr(buf, "\r\n\r\n"), *line;
	size_t hlen;

	if (!end)
		return false;
	hlen = (size_t)(end - buf) + 4;
	for (line = strstr(buf, "\r\n"); line && line < end; line = strstr(line, "\r\n")) {
		line += 2;
		if (!strncasecmp(line, "Content-Length:", 15))
			return got >= hlen + strtoul(line + 15, NULL, 10);
		if (!strncasecmp(line, "Transfer-Encoding:", 18) && strcasestr(line, "chunked") && strcasestr(line, "chunked") < end)
			return got >= hlen + 5 && !memcmp(buf + got - 5, "0\r\n\r\n", 5);
	}
	/* a 204 or 304 has no body */
	return !strncmp(buf + 9, "204", 3) || !strncmp(buf + 9, "304", 3);
}

static int http_once(struct web *w, const char *method, const char *path, const char *headers, const char *body)
{
	char host[64], req[8192], *hdr_end, *line;
	const char *colon;
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(80) };
	long long deadline = mono_ms() + TOTAL_MS;
	size_t got = 0, hlen;
	int fd, n, rc = -1;
	struct pollfd pfd;

	w->code = 0;
	w->body[0] = '\0';
	snprintf(host, sizeof(host), "%s", w->a->host);
	if ((colon = strchr(host, ':'))) {
		sa.sin_port = htons((unsigned short)atoi(colon + 1));
		host[colon - host] = '\0';
	}
	if (inet_pton(AF_INET, host, &sa.sin_addr) != 1)
		return -1;
	if ((fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) < 0)
		return -1;
	if (w->a->dev && *w->a->dev && setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, w->a->dev, strlen(w->a->dev) + 1))
		goto out;
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) && errno != EINPROGRESS)
		goto out;
	pfd = (struct pollfd){ .fd = fd, .events = POLLOUT };
	if (poll(&pfd, 1, CONNECT_MS) != 1)
		goto out;
	{
		int err = 0;
		socklen_t l = sizeof(err);

		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l) || err)
			goto out;
	}
	/* a Referer of the web's own page, as a browser sends it: the owner's MegaFon M150-4 (2026-11-03) answered
	 * post_login 403 Forbidden without one and let in with «Referer: http://status.megafon.ru/»
	 * (a caller's own Referer, ZTE's /index.html, stays the only one) */
	{
		bool own = headers && strcasestr(headers, "Referer:");
		const char *h = *w->vhost ? w->vhost : w->a->host;

		n = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: %s\r\n%s%s%sConnection: close\r\nAccept: */*\r\n%s%s%s",
			     method, path, h, own ? "" : "Referer: http://", own ? "" : h, own ? "" : "/\r\n",
			     *w->cookie ? "Cookie: " : "", w->cookie, *w->cookie ? "\r\n" : "");
	}
	if (n < 0 || (size_t)n >= sizeof(req))
		goto out;
	/* each part checked before the next: a truncated one leaves n past the buffer, and «sizeof(req) - n» would then wrap
	 * to a huge size for the next snprintf, which writes past req (audit STAB-01, 2026-11-19) */
	{
		int k = snprintf(req + n, sizeof(req) - n, "%s", headers ? headers : "");

		if (k < 0 || (size_t)k >= sizeof(req) - n)
			goto out;
		n += k;
		if (body) {
			k = snprintf(req + n, sizeof(req) - n, "Content-Length: %zu\r\n", strlen(body));
			if (k < 0 || (size_t)k >= sizeof(req) - n)
				goto out;
			n += k;
		}
		k = snprintf(req + n, sizeof(req) - n, "\r\n%s", body ? body : "");
		if (k < 0 || (size_t)k >= sizeof(req) - n)
			goto out;
		n += k;
	}
	{
		int off = 0;

		while (off < n) {
			ssize_t s = send(fd, req + off, n - off, MSG_NOSIGNAL);

			if (s < 0 && errno == EAGAIN) {
				/* past the deadline the timeout would be negative: poll() would wait for ever */
				long long left = deadline - mono_ms();

				pfd = (struct pollfd){ .fd = fd, .events = POLLOUT };
				if (left <= 0 || poll(&pfd, 1, (int)left) != 1)
					goto out;
				continue;
			}
			if (s <= 0)
				goto out;
			off += s;
		}
	}
	while (got < sizeof(w->body) - 1) {
		long long left = deadline - mono_ms();
		ssize_t r;

		if (left <= 0)
			break;
		pfd = (struct pollfd){ .fd = fd, .events = POLLIN };
		if (poll(&pfd, 1, (int)left) != 1)
			break;
		r = recv(fd, w->body + got, sizeof(w->body) - 1 - got, 0);
		if (r <= 0)
			break;
		got += r;
		/* a web that keeps the connection open despite «Connection: close» (the owner's E3372-325, WEBUI 3.0.2.61,
		 * 2026-11-03: every request waited its whole 6 s, the reading ran past 25 s and was killed): the answer
		 * ends at its Content-Length or at the last chunk */
		w->body[got] = '\0';
		if (http_complete(w->body, got))
			break;
	}
	w->body[got] = '\0';
	if (sscanf(w->body, "HTTP/1.%*d %d", &w->code) != 1)
		goto out;
	/* the headers end at an empty line: CR LF as HTTP says, or a bare LF (a tester's MTS 8810FT, «Server: Sanechips-Webs»,
	 * 2026-11-11: its goform answered curl, the engine found no end of headers and read the web as unknown) */
	{
		char *crlf = strstr(w->body, "\r\n\r\n"), *lf = strstr(w->body, "\n\n");

		if (crlf && (!lf || crlf < lf)) {
			hdr_end = crlf;
			hlen = hdr_end - w->body + 4;
		} else if (lf) {
			hdr_end = lf;
			hlen = hdr_end - w->body + 2;
		} else
			goto out;
	}
	*hdr_end = '\0';
	/* the headers: cookies, Huawei's next token, chunked bodies, a redirect's Location */
	{
		bool chunked = false;
		char location[160] = "";

		for (line = strchr(w->body, '\n'); line; line = strchr(line, '\n')) {
			line += 1;
			if (!strncasecmp(line, "Set-Cookie:", 11))
				cookie_set(w->cookie, sizeof(w->cookie), line + 11 + strspn(line + 11, " "));
			else if (!strncasecmp(line, "__RequestVerificationToken", 26) && strncasecmp(line + 26, "two", 3)) {
				/* after a login the web names two tokens: the next request takes "one" */
				const char *v = strchr(line, ':');
				size_t l;

				if (v) {
					v += 1 + strspn(v + 1, " ");
					l = strcspn(v, "#\r\n");	/* a list of tokens comes joined by '#': the first */
					if (l && l < sizeof(w->token))
						snprintf(w->token, sizeof(w->token), "%.*s", (int)l, v);
				}
			} else if (!strncasecmp(line, "Transfer-Encoding:", 18) && strcasestr(line, "chunked"))
				chunked = true;
			else if (!strncasecmp(line, "Location:", 9)) {
				const char *v = line + 9 + strspn(line + 9, " ");

				snprintf(location, sizeof(location), "%.*s", (int)strcspn(v, "\r\n"), v);
			}
		}
		memmove(w->body, w->body + hlen, got - hlen + 1);
		got -= hlen;
		if (chunked && dechunk(w->body, got) < 0)
			w->body[0] = '\0';
		/* a redirect: its Location for http() (a marker no web body starts with) */
		if (w->code >= 300 && w->code < 400 && *location)
			snprintf(w->body, sizeof(w->body), "\x01LOC %s", location);
	}
	rc = w->code;
out:
	/* the request holds the login and cookies: wiped on every way out, a failed send too (audit STAB-01) */
	explicit_bzero(req, sizeof(req));
	close(fd);
	return rc;
}

/* ---- reading the answers ---- */

/* the text of <tag>...</tag>: its length, -1 when absent */
int mm_web_xml_tag(const char *xml, const char *tag, char *out, size_t size)
{
	char open[64], close[64];
	const char *s, *e;
	size_t n;

	*out = '\0';
	snprintf(open, sizeof(open), "<%s>", tag);
	snprintf(close, sizeof(close), "</%s>", tag);
	if (!xml || !(s = strstr(xml, open)))
		return -1;
	s += strlen(open);
	if (!(e = strstr(s, close)))
		return -1;
	n = e - s;
	while (n && isspace((unsigned char)*s))
		s++, n--;
	while (n && isspace((unsigned char)s[n - 1]))
		n--;
	if (n >= size)
		n = size - 1;
	memcpy(out, s, n);
	out[n] = '\0';
	return (int)n;
}

/* the first number in a value like "-95dBm", "&gt;=-51dBm", "10dB", "20MHz"; false when there is none */
static bool num(const char *v, double *out)
{
	char *end;

	for (; v && *v; v++)
		if (isdigit((unsigned char)*v) || (*v == '-' && isdigit((unsigned char)v[1]))) {
			*out = strtod(v, &end);
			return end != v;
		}
	return false;
}

static void put_num(json_object *o, const char *key, const char *v, bool integer)
{
	double d;

	if (num(v, &d))
		json_object_object_add(o, key, integer ? json_object_new_int((int)(d < 0 ? d - 0.5 : d + 0.5)) : json_object_new_double(d));
}

static void put_str(json_object *o, const char *key, const char *v)
{
	if (v && *v)
		json_object_object_add(o, key, json_object_new_string(v));
}

static void put_band(json_object *o, const char *key, const char *prefix, const char *v)
{
	double d;
	char b[16];

	if (num(v, &d) && d > 0 && d < 1000) {
		snprintf(b, sizeof(b), "%s%d", prefix, (int)d);
		json_object_object_add(o, key, json_object_new_string(b));
	}
}

/*
 * A modded HiLink web (E3372h 22.333 «Mod1.x», the owner's bench 2026-10-31) shows the band, the EARFCN, the frequencies,
 * the bandwidth and the LAC that its device/signal leaves out: a script of the mod writes them into
 * config/deviceinformation/add_param.xml {lac (hex), cell_id, band, earfcn1/2 (DL/UL), freq1/2 (0.1 MHz), bandwidth1/2
 * (kHz), rssi, cnt}. What device/signal already gave stays; an empty field says nothing.
 */
static void hw_ext_params(json_object *m, const char *xml)
{
	char v[64];
	double d;

	if (!xml || !strstr(xml, "<config>"))
		return;
	if (!json_object_object_get(m, "band") && mm_web_xml_tag(xml, "band", v, sizeof(v)) > 0)
		put_band(m, "band", "B", v);
	if (!json_object_object_get(m, "earfcn") && mm_web_xml_tag(xml, "earfcn1", v, sizeof(v)) > 0)
		put_num(m, "earfcn", v, true);
	if (!json_object_object_get(m, "ul_earfcn") && mm_web_xml_tag(xml, "earfcn2", v, sizeof(v)) > 0)
		put_num(m, "ul_earfcn", v, true);
	if (!json_object_object_get(m, "dl_mhz") && mm_web_xml_tag(xml, "freq1", v, sizeof(v)) > 0 && num(v, &d) && d > 0)
		json_object_object_add(m, "dl_mhz", json_object_new_double(d / 10));
	if (!json_object_object_get(m, "ul_mhz") && mm_web_xml_tag(xml, "freq2", v, sizeof(v)) > 0 && num(v, &d) && d > 0)
		json_object_object_add(m, "ul_mhz", json_object_new_double(d / 10));
	if (!json_object_object_get(m, "bandwidth_mhz") && mm_web_xml_tag(xml, "bandwidth1", v, sizeof(v)) > 0 && num(v, &d) && d > 0)
		json_object_object_add(m, "bandwidth_mhz", json_object_new_double(d / 1000));
	if (!json_object_object_get(m, "ul_bandwidth_mhz") && mm_web_xml_tag(xml, "bandwidth2", v, sizeof(v)) > 0 && num(v, &d) && d > 0)
		json_object_object_add(m, "ul_bandwidth_mhz", json_object_new_double(d / 1000));
	if (!json_object_object_get(m, "tac") && mm_web_xml_tag(xml, "lac", v, sizeof(v)) > 0 && strlen(v) <= 8 &&
	    strspn(v, "0123456789abcdefABCDEF") == strlen(v))
		json_object_object_add(m, "tac", json_object_new_int64(strtoll(v, NULL, 16)));
}

static const char *hw_tech(const char *signal)
{
	char v[64];

	if (mm_web_xml_tag(signal, "nrrsrp", v, sizeof(v)) > 0 && num(v, &(double){ 0 }))
		return mm_web_xml_tag(signal, "rsrp", v, sizeof(v)) > 0 && num(v, &(double){ 0 }) ? "nr-nsa" : "nr";
	if (mm_web_xml_tag(signal, "rsrp", v, sizeof(v)) > 0 && num(v, &(double){ 0 }))
		return "lte";
	if (mm_web_xml_tag(signal, "rscp", v, sizeof(v)) > 0 && num(v, &(double){ 0 }))
		return "umts";
	if (mm_web_xml_tag(signal, "rssi", v, sizeof(v)) > 0 && num(v, &(double){ 0 }))
		return "gsm";
	return NULL;
}

json_object *mm_web_hilink_metrics(const char *signal, const char *info, const char *plmn)
{
	json_object *m = json_object_new_object(), *nr = json_object_new_object();
	char v[128];
	double d;

	if (mm_web_xml_tag(signal, "rssi", v, sizeof(v)) > 0) put_num(m, "rssi_dbm", v, true);
	if (mm_web_xml_tag(signal, "rsrp", v, sizeof(v)) > 0) put_num(m, "rsrp_dbm", v, true);
	if (mm_web_xml_tag(signal, "rsrq", v, sizeof(v)) > 0) put_num(m, "rsrq_db", v, false);
	if (mm_web_xml_tag(signal, "sinr", v, sizeof(v)) > 0) put_num(m, "sinr_db", v, false);
	if (mm_web_xml_tag(signal, "rscp", v, sizeof(v)) > 0) put_num(m, "rscp_dbm", v, true);
	if (mm_web_xml_tag(signal, "ecio", v, sizeof(v)) > 0) put_num(m, "ecio_db", v, false);
	if (mm_web_xml_tag(signal, "pci", v, sizeof(v)) > 0) put_num(m, "pci", v, true);
	if (mm_web_xml_tag(signal, "tac", v, sizeof(v)) > 0) put_num(m, "tac", v, true);
	if (mm_web_xml_tag(signal, "band", v, sizeof(v)) > 0) put_band(m, "band", "B", v);
	if (mm_web_xml_tag(signal, "dlbandwidth", v, sizeof(v)) > 0) put_num(m, "bandwidth_mhz", v, false);
	/* the owner's E3372-325 HAR (2026-10-30): the uplink's width, the PUSCH power (999: none), the transmission mode and
	 * whether the radio link is up now (RRC) */
	if (mm_web_xml_tag(signal, "ulbandwidth", v, sizeof(v)) > 0) put_num(m, "ul_bandwidth_mhz", v, false);
	if (mm_web_xml_tag(signal, "txpower", v, sizeof(v)) > 0) {
		const char *pp = strstr(v, "PPusch:");

		if (pp && num(pp + 7, &d) && d > -60 && d < 40)
			json_object_object_add(m, "tx_power_dbm", json_object_new_int((int)d));
	}
	if (mm_web_xml_tag(signal, "transmode", v, sizeof(v)) > 0) {
		int tm;

		if (sscanf(v, "TM[%d]", &tm) == 1 && tm > 0 && tm < 11) {
			snprintf(v, sizeof(v), "TM%d", tm);
			put_str(m, "transmission_mode", v);
		}
	}
	if (mm_web_xml_tag(signal, "rrc_status", v, sizeof(v)) > 0 && (!strcmp(v, "0") || !strcmp(v, "1")))
		put_str(m, "rrc", *v == '1' ? "connected" : "idle");
	if (mm_web_xml_tag(signal, "earfcn", v, sizeof(v)) > 0) {
		const char *dl = strstr(v, "DL:");
		put_num(m, "earfcn", dl ? dl + 3 : v, true);
	}
	if (mm_web_xml_tag(signal, "cell_id", v, sizeof(v)) > 0 && num(v, &d) && d > 0) {
		json_object_object_add(m, "cell_id", json_object_new_int64((long long)d));
		if (json_object_object_get(m, "rsrp_dbm"))	/* an LTE ECI: eNB and the cell's own byte */
			json_object_object_add(m, "enb", json_object_new_int64((long long)d / 256));
	}
	if (mm_web_xml_tag(signal, "nrrsrp", v, sizeof(v)) > 0) put_num(nr, "rsrp_dbm", v, true);
	if (mm_web_xml_tag(signal, "nrrsrq", v, sizeof(v)) > 0) put_num(nr, "rsrq_db", v, false);
	if (mm_web_xml_tag(signal, "nrsinr", v, sizeof(v)) > 0) put_num(nr, "sinr_db", v, false);
	if (mm_web_xml_tag(signal, "nrpci", v, sizeof(v)) > 0) put_num(nr, "pci", v, true);
	if (mm_web_xml_tag(signal, "nrearfcn", v, sizeof(v)) > 0) put_num(nr, "arfcn", v, true);
	if (mm_web_xml_tag(signal, "nrband", v, sizeof(v)) > 0) put_band(nr, "band", "n", v);
	if (json_object_object_length(nr))
		json_object_object_add(m, "nr", nr);
	else
		json_object_put(nr);
	put_str(m, "access_technology", hw_tech(signal));
	if (mm_web_xml_tag(info, "DeviceName", v, sizeof(v)) > 0) put_str(m, "model", v);
	if (mm_web_xml_tag(info, "SoftwareVersion", v, sizeof(v)) > 0) put_str(m, "firmware", v);
	if (info && strstr(info, "<DeviceName>"))
		json_object_object_add(m, "manufacturer", json_object_new_string("Huawei"));
	if (mm_web_xml_tag(plmn, "FullName", v, sizeof(v)) > 0 || mm_web_xml_tag(plmn, "ShortName", v, sizeof(v)) > 0)
		put_str(m, "operator", v);
	if (mm_web_xml_tag(plmn, "Numeric", v, sizeof(v)) > 0)
		put_str(m, "operator_code", v);
	/* the SIM's own operator, by its IMSI's first digits: the network is another one for an MVNO (Yota on MegaFon) */
	{
		char imsi[24], code[8];

		if (mm_web_xml_tag(info, "Imsi", imsi, sizeof(imsi)) > 0 && simid_plmn_of_imsi(imsi, code, sizeof(code)))
			put_str(m, "sim_operator_code", code);
		explicit_bzero(imsi, sizeof(imsi));
	}
	return m;
}

/*
 * A «Mobile.Router» web (MegaFon MF150-4, 05c6:f00e; the owner's HAR 2026-10-30): mark_home.w.xml's rssi_info
 * «-99dBm,-10dB,6dB,-71dB» (RSRP, RSRQ, SINR, RSSI by their values; the page shows only the first), cell_info
 * «PLMN,ECI,PCI,PLMN+TAC(hex)+ECI(7 hex)», op_mode «LTE,LTE»; mark_title.w.xml's cspn is the operator's name.
 */
json_object *mm_web_mark_metrics(const char *home, const char *title)
{
	json_object *m = json_object_new_object();
	char v[160], f[4][64];
	const char *p;
	int n = 0;

	if (mm_web_xml_tag(home, "rssi_info", v, sizeof(v)) > 0 && strcmp(v, "-1")) {
		static const char *const keys[] = { "rsrp_dbm", "rsrq_db", "sinr_db", "rssi_dbm" };

		for (p = v; n < 4; n++) {
			size_t l = strcspn(p, ",");

			snprintf(f[n], sizeof(f[n]), "%.*s", (int)(l < sizeof(f[n]) ? l : sizeof(f[n]) - 1), p);
			put_num(m, keys[n], f[n], n != 1 && n != 2);
			if (!p[l])
				break;
			p += l + 1;
		}
	}
	if (mm_web_xml_tag(home, "cell_info", v, sizeof(v)) > 0 && strcmp(v, "-1")) {
		double d;

		memset(f, 0, sizeof(f));
		for (p = v, n = 0; n < 4; n++) {
			size_t l = strcspn(p, ",");

			snprintf(f[n], sizeof(f[n]), "%.*s", (int)(l < sizeof(f[n]) ? l : sizeof(f[n]) - 1), p);
			if (!p[l])
				break;
			p += l + 1;
		}
		if (strspn(f[0], "0123456789") == strlen(f[0]) && strlen(f[0]) >= 5)
			put_str(m, "operator_code", f[0]);
		if (num(f[1], &d) && d > 0) {
			json_object_object_add(m, "cell_id", json_object_new_int64((long long)d));
			json_object_object_add(m, "enb", json_object_new_int64((long long)d / 256));
		}
		put_num(m, "pci", f[2], true);
		/* the TAC between the PLMN and the 7 hex digits of the ECI */
		if (*f[0] && !strncmp(f[3], f[0], strlen(f[0])) && strlen(f[3]) > strlen(f[0]) + 7) {
			char tac[16];
			size_t tl = strlen(f[3]) - strlen(f[0]) - 7;

			if (tl < sizeof(tac)) {
				snprintf(tac, sizeof(tac), "%.*s", (int)tl, f[3] + strlen(f[0]));
				if (strspn(tac, "0123456789abcdefABCDEF") == tl)
					json_object_object_add(m, "tac", json_object_new_int64(strtoll(tac, NULL, 16)));
			}
		}
	}
	if (mm_web_xml_tag(home, "op_mode", v, sizeof(v)) > 0 || mm_web_xml_tag(title, "op_mode", v, sizeof(v)) > 0) {
		v[strcspn(v, ",")] = '\0';
		put_str(m, "access_technology", !strcasecmp(v, "LTE") ? "lte" : !strcasecmp(v, "WCDMA") ? "umts" :
				 !strcasecmp(v, "GSM") ? "gsm" : NULL);
	}
	/* «-1» is the stick's «nothing» (the owner's M150-4 without a SIM, 2026-11-04: the card showed «-1» for the operator) */
	if (mm_web_xml_tag(title, "cspn", v, sizeof(v)) > 0 && strcmp(v, "-1") && !strpbrk(v, "<>&\""))
		put_str(m, "operator", v);
	return m;
}

static const char *zs(json_object *z, const char *k)
{
	const char *v = json_object_get_string(json_object_object_get(z, k));

	return v && *v ? v : NULL;
}

/* ZTE's network_type: "LTE", "ENDC" (NSA), "SA", "WCDMA", "HSPA+", "GSM", "EDGE", "LTE-NSA"... */
static const char *zte_tech(const char *t)
{
	if (!t)
		return NULL;
	if (strcasestr(t, "ENDC") || strcasestr(t, "NSA"))
		return "nr-nsa";
	if (!strcasecmp(t, "SA") || strcasestr(t, "NR") || strcasestr(t, "5G"))
		return "nr";
	if (strcasestr(t, "LTE") || strcasestr(t, "4G"))
		return "lte";
	if (strcasestr(t, "WCDMA") || strcasestr(t, "HSPA") || strcasestr(t, "HSDPA") || strcasestr(t, "HSUPA") || strcasestr(t, "UMTS") ||
	    strcasestr(t, "3G") || strcasestr(t, "TD"))
		return "umts";
	if (strcasestr(t, "GSM") || strcasestr(t, "EDGE") || strcasestr(t, "GPRS") || strcasestr(t, "2G"))
		return "gsm";
	return NULL;
}

json_object *mm_web_zte_metrics(json_object *z)
{
	json_object *m = json_object_new_object(), *nr = json_object_new_object();
	const char *tech = zte_tech(zs(z, "network_type")), *v;
	bool sa = tech && !strcmp(tech, "nr");
	double d;

	if (!sa) {
		/* ZXIC's own fields when the standard ones are empty: nv_rsrp an index (RSRP = v - 141), nv_rsrq a half-dB
		 * index ((v - 40) / 2), nv_sinr in dB; a value already in dBm or dB is taken as it is */
		char nvp[16] = "", nvq[16] = "";

		if (!zs(z, "lte_rsrp") && zs(z, "nv_rsrp") && num(zs(z, "nv_rsrp"), &d))
			snprintf(nvp, sizeof(nvp), "%d", (int)(d >= 0 && d <= 97 ? d - 141 : d));
		if (!zs(z, "lte_rsrq") && zs(z, "nv_rsrq") && num(zs(z, "nv_rsrq"), &d))
			snprintf(nvq, sizeof(nvq), "%.1f", d >= 0 && d <= 34 ? (d - 40) / 2 : d);
		put_num(m, "rssi_dbm", zs(z, "lte_rssi") ? zs(z, "lte_rssi") : zs(z, "rssi"), true);
		put_num(m, "rsrp_dbm", zs(z, "lte_rsrp") ? zs(z, "lte_rsrp") : *nvp ? nvp : NULL, true);
		put_num(m, "rsrq_db", zs(z, "lte_rsrq") ? zs(z, "lte_rsrq") : *nvq ? nvq : NULL, false);
		put_num(m, "sinr_db", zs(z, "lte_snr") ? zs(z, "lte_snr") : zs(z, "nv_sinr"), false);
		put_num(m, "pci", zs(z, "lte_pci") ? zs(z, "lte_pci") : zs(z, "nv_pci"), true);
		put_num(m, "earfcn", zs(z, "lte_ca_pcell_arfcn") ? zs(z, "lte_ca_pcell_arfcn") : zs(z, "wan_active_channel") ?
			zs(z, "wan_active_channel") : zs(z, "nv_arfcn"), true);
		put_num(m, "bandwidth_mhz", zs(z, "lte_ca_pcell_bandwidth"), false);
		/* "LTE BAND 3", "LTE B3", or the primary cell's own band */
		if ((v = zs(z, "wan_active_band")) || (v = zs(z, "lte_ca_pcell_band")))
			put_band(m, "band", "B", v);
		if ((v = zs(z, "lte_ca_scell_band")) && num(v, &d) && d > 0) {
			json_object *ca = json_object_new_array();
			char b[16];

			snprintf(b, sizeof(b), "B%d", (int)d);
			json_object_array_add(ca, json_object_new_string(b));
			json_object_object_add(m, "ca", ca);
		}
		/* the cell id comes in hex: the eNB and the cell's own last byte; a ZXIC firmware (its nv_* fields) gives it in
		 * decimal (the 8810FT's 1703969 = eNB 6656 cell 33, the MTS site the M150-4 beside it read as 1703939) */
		if ((v = zs(z, "cell_id")) && strlen(v) <= 9 && strspn(v, "0123456789abcdefABCDEF") == strlen(v)) {
			long long eci = strtoll(v, NULL, zs(z, "nv_rsrp") || zs(z, "nv_pci") ? 10 : 16);

			if (eci > 0) {
				json_object_object_add(m, "cell_id", json_object_new_int64(eci));
				json_object_object_add(m, "enb", json_object_new_int64(eci >> 8));
			}
		}
		/* «0» is no TAC (the 8810FT's lac_code); ZXIC's tac_code is decimal */
		if ((v = zs(z, "lac_code")) && strcmp(v, "0") && strspn(v, "0123456789abcdefABCDEF") == strlen(v))
			json_object_object_add(m, "tac", json_object_new_int64(strtoll(v, NULL, 16)));
		else if ((v = zs(z, "tac_code")) && strcmp(v, "0") && strspn(v, "0123456789") == strlen(v))
			json_object_object_add(m, "tac", json_object_new_int64(strtoll(v, NULL, 10)));
	}
	put_num(nr, "rsrp_dbm", zs(z, "Z5g_rsrp"), true);
	put_num(nr, "rsrq_db", zs(z, "Z5g_rsrq"), false);
	put_num(nr, "sinr_db", zs(z, "Z5g_SINR") ? zs(z, "Z5g_SINR") : zs(z, "Z5g_snr"), false);
	put_num(nr, "pci", zs(z, "nr5g_pci"), true);
	put_num(nr, "arfcn", zs(z, "Z5g_dlEarfcn") ? zs(z, "Z5g_dlEarfcn") : zs(z, "nr5g_action_channel"), true);
	put_band(nr, "band", "n", zs(z, "nr5g_action_band"));
	if (json_object_object_get(nr, "rsrp_dbm") || json_object_object_get(nr, "sinr_db"))
		json_object_object_add(m, "nr", nr);
	else
		json_object_put(nr);
	put_str(m, "access_technology", tech);
	put_str(m, "operator", zs(z, "network_provider"));
	if ((zs(z, "rmcc") && zs(z, "rmnc")) || (zs(z, "mcc") && zs(z, "mnc"))) {
		char code[16];
		bool r = zs(z, "rmcc") && zs(z, "rmnc");
		const char *mnc = r ? zs(z, "rmnc") : zs(z, "mnc");

		snprintf(code, sizeof(code), "%s%s%s", r ? zs(z, "rmcc") : zs(z, "mcc"), strlen(mnc) == 1 ? "0" : "", mnc);
		if (strspn(code, "0123456789") == strlen(code))
			put_str(m, "operator_code", code);
	}
	/* the SIM's own operator, by its IMSI's first digits (the owner, 2026-11-05); the IMSI stays in the answer */
	{
		char code[8];

		if (zs(z, "sim_imsi") && simid_plmn_of_imsi(zs(z, "sim_imsi"), code, sizeof(code)))
			put_str(m, "sim_operator_code", code);
	}
	put_str(m, "model", zs(z, "model_name"));
	put_str(m, "firmware", zs(z, "wa_inner_version") ? zs(z, "wa_inner_version") : zs(z, "cr_version"));
	if (json_object_object_length(m))
		json_object_object_add(m, "manufacturer", json_object_new_string("ZTE"));
	return m;
}

/* ---- the modem's state file: cookies and the last login try ---- */

struct state {
	char cookie[512];
	long long last_login;
};

static void state_read(const char *path, struct state *s)
{
	char line[600];
	FILE *f = path ? fopen(path, "r") : NULL;

	memset(s, 0, sizeof(*s));
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = '\0';
		if (!strncmp(line, "cookie=", 7))
			snprintf(s->cookie, sizeof(s->cookie), "%s", line + 7);
		else if (!strncmp(line, "last_login=", 11))
			s->last_login = atoll(line + 11);
	}
	fclose(f);
}

static void state_write(const char *path, const struct state *s)
{
	char tmp[4096];
	FILE *f;
	int fd;

	if (!path || snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return;
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600)) < 0)
		return;
	if (!(f = fdopen(fd, "w"))) {
		close(fd);
		return;
	}
	fprintf(f, "cookie=%s\nlast_login=%lld\n", strchr(s->cookie, '\n') ? "" : s->cookie, s->last_login);
	if (fclose(f) == 0)
		rename(tmp, path);
	else
		unlink(tmp);
}

/* ---- Huawei HiLink ---- */

static const char *XHR = "X-Requested-With: XMLHttpRequest\r\n";

/* a fresh session and token (some firmwares refuse every API call without them) */
static void hw_session(struct web *w)
{
	char ses[256], tok[160];

	if (http(w, "GET", "/api/webserver/SesTokInfo", XHR, NULL) == 200 &&
	    mm_web_xml_tag(w->body, "TokInfo", tok, sizeof(tok)) > 0) {
		if (mm_web_xml_tag(w->body, "SesInfo", ses, sizeof(ses)) > 0)
			cookie_set(w->cookie, sizeof(w->cookie), ses);
		snprintf(w->token, sizeof(w->token), "%s", tok);
	} else if (http(w, "GET", "/api/webserver/token", XHR, NULL) == 200 &&
		   mm_web_xml_tag(w->body, "token", tok, sizeof(tok)) > 0) {
		size_t n = strlen(tok);

		snprintf(w->token, sizeof(w->token), "%s", n > 32 ? tok + n - 32 : tok);
	}
}

/* an API GET; the error code the modem answers, 0 for none, -1 when there is no answer */
static int hw_get(struct web *w, const char *path)
{
	char hdr[300], code[16];

	snprintf(hdr, sizeof(hdr), "%s%s%s%s", XHR, *w->token ? "__RequestVerificationToken: " : "", w->token, *w->token ? "\r\n" : "");
	if (http(w, "GET", path, hdr, NULL) != 200 || !strstr(w->body, "<"))
		return -1;
	if (strstr(w->body, "<error>") && mm_web_xml_tag(w->body, "code", code, sizeof(code)) > 0)
		return atoi(code) ? atoi(code) : -1;
	return 0;
}

/* 125002 wrong session, 125003 wrong token, 100003 no rights: a login is wanted */
static bool hw_login_wanted(int code)
{
	return code == 125002 || code == 125003 || code == 100003;
}

/* auth word for a login try, or NULL when logged in */
static const char *hw_login(struct web *w, const char *password, bool chosen, int *wait_s)
{
	char user[64] = "admin", type[8] = "", lock[8] = "", wait[16] = "", h1[65], b1[100], *mix, h2[65], pw[128], body[512];
	char hdr[300], code[16];
	size_t n;

	hw_session(w);
	if (hw_get(w, "/api/user/state-login") == 0) {
		mm_web_xml_tag(w->body, "password_type", type, sizeof(type));
		mm_web_xml_tag(w->body, "lockstatus", lock, sizeof(lock));
		mm_web_xml_tag(w->body, "remainwaittime", wait, sizeof(wait));
		if (mm_web_xml_tag(w->body, "username", user, sizeof(user)) <= 0 || strpbrk(user, "<>&"))
			snprintf(user, sizeof(user), "admin");
		if (!strcmp(lock, "1")) {
			*wait_s = atoi(wait) > 0 ? atoi(wait) : MM_WEB_LOGIN_EVERY_S;
			return "locked";
		}
	}
	if (!strcmp(type, "4")) {
		/* base64(sha256hex(user + base64(sha256hex(password)) + token)), lower-case hex */
		mm_sha256_hex(password, strlen(password), false, h1);
		base64(h1, 64, b1, sizeof(b1));
		if (asprintf(&mix, "%s%s%s", user, b1, w->token) < 0)
			return "failed";
		mm_sha256_hex(mix, strlen(mix), false, h2);
		explicit_bzero(mix, strlen(mix));
		free(mix);
		base64(h2, 64, pw, sizeof(pw));
		explicit_bzero(h1, sizeof(h1));
		explicit_bzero(h2, sizeof(h2));
		explicit_bzero(b1, sizeof(b1));
	} else {
		base64(password, strlen(password), pw, sizeof(pw));
	}
	n = (size_t)snprintf(body, sizeof(body), "<?xml version=\"1.0\" encoding=\"UTF-8\"?><request><Username>%s</Username>"
			     "<Password>%s</Password><password_type>%s</password_type></request>", user, pw, *type ? type : "0");
	explicit_bzero(pw, sizeof(pw));
	snprintf(hdr, sizeof(hdr), "%s__RequestVerificationToken: %s\r\nContent-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n",
		 XHR, w->token);
	if (n >= sizeof(body) || http(w, "POST", "/api/user/login", hdr, body) != 200) {
		explicit_bzero(body, sizeof(body));
		return "failed";
	}
	explicit_bzero(body, sizeof(body));
	if (strstr(w->body, "<response>OK</response>"))
		return NULL;
	mm_web_xml_tag(w->body, "code", code, sizeof(code));
	if (!strcmp(code, "108003"))	/* already logged in */
		return NULL;
	if (!strcmp(code, "108007")) {
		*wait_s = MM_WEB_LOGIN_EVERY_S;
		return "locked";
	}
	if (!strcmp(code, "108001") || !strcmp(code, "108002") || !strcmp(code, "108006"))
		return chosen ? "wrong" : "needed";
	return "failed";
}

/* a POST with the session's token; the error code as hw_get */
static int hw_post(struct web *w, const char *path, const char *request)
{
	char hdr[400], code[16];

	snprintf(hdr, sizeof(hdr), "%s__RequestVerificationToken: %s\r\nContent-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n",
		 XHR, w->token);
	if (http(w, "POST", path, hdr, request) != 200 || !strstr(w->body, "<"))
		return -1;
	if (strstr(w->body, "<error>") && mm_web_xml_tag(w->body, "code", code, sizeof(code)) > 0)
		return atoi(code) ? atoi(code) : -1;
	return 0;
}

/* ---- Huawei HiLink SMS (r94): /api/sms/sms-list, send-sms, delete-sms ---- */

/* XML text of the modem back to UTF-8 in place: the five entities and numeric ones */
static void xml_unescape(char *s)
{
	static const struct { const char *e; char c; } ents[] = {
		{ "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' }, { "&amp;", '&' } };
	char *in = s, *out = s;

	while (*in) {
		size_t k;
		bool done = false;

		if (*in == '&' && in[1] == '#') {
			char *end;
			unsigned long cp = in[2] == 'x' || in[2] == 'X' ? strtoul(in + 3, &end, 16) : strtoul(in + 2, &end, 10);

			if (*end == ';' && cp > 0 && cp < 0x110000 && (cp < 0xd800 || cp > 0xdfff)) {
				if (cp < 0x80) {
					*out++ = (char)cp;
				} else if (cp < 0x800) {
					*out++ = (char)(0xc0 | cp >> 6);
					*out++ = (char)(0x80 | (cp & 0x3f));
				} else if (cp < 0x10000) {
					*out++ = (char)(0xe0 | cp >> 12);
					*out++ = (char)(0x80 | ((cp >> 6) & 0x3f));
					*out++ = (char)(0x80 | (cp & 0x3f));
				} else {
					*out++ = (char)(0xf0 | cp >> 18);
					*out++ = (char)(0x80 | ((cp >> 12) & 0x3f));
					*out++ = (char)(0x80 | ((cp >> 6) & 0x3f));
					*out++ = (char)(0x80 | (cp & 0x3f));
				}
				in = end + 1;
				continue;
			}
		}
		for (k = 0; *in == '&' && k < sizeof(ents) / sizeof(ents[0]) && !done; k++)
			if (!strncmp(in, ents[k].e, strlen(ents[k].e))) {
				*out++ = ents[k].c;
				in += strlen(ents[k].e);
				done = true;
			}
		if (!done)
			*out++ = *in++;
	}
	*out = '\0';
}

/* text into XML; false when it does not fit */
static bool xml_escape(const char *in, char *out, size_t size)
{
	size_t used = 0;

	for (; *in; in++) {
		const char *rep = *in == '<' ? "&lt;" : *in == '>' ? "&gt;" : *in == '&' ? "&amp;" : *in == '"' ? "&quot;" :
				  *in == '\'' ? "&apos;" : NULL;
		size_t n = rep ? strlen(rep) : 1;

		if (used + n + 1 > size)
			return false;
		if (rep)
			memcpy(out + used, rep, n);
		else
			out[used] = *in;
		used += n;
	}
	out[used] = '\0';
	return true;
}

/* the characters of a UTF-8 text, as the web counts its <Length> */
static size_t utf8_chars(const char *s)
{
	size_t n = 0;

	for (; *s; s++)
		n += ((unsigned char)*s & 0xc0) != 0x80;
	return n;
}

/*
 * The messages of one box and page: BoxType 1 the modem's inbox, 2 its sent ones, 5 and 6 the same on the SIM card
 * (one record per </Message>, the text across lines; the firmware joins the parts of a
 * long message itself). A message already listed (its Index) is not listed twice. Returns how many the page had.
 */
static int hw_sms_box(struct web *w, int box, int page, json_object *msgs)
{
	char req[400], *p, *end;
	int seen = 0;

	snprintf(req, sizeof(req), "<?xml version=\"1.0\" encoding=\"UTF-8\"?><request><PageIndex>%d</PageIndex><ReadCount>50</ReadCount>"
		 "<BoxType>%d</BoxType><SortType>0</SortType><Ascending>0</Ascending><UnreadPreferred>0</UnreadPreferred></request>", page, box);
	if (hw_post(w, "/api/sms/sms-list", req))
		return 0;
	for (p = strstr(w->body, "<Message>"); p && (end = strstr(p, "</Message>")); p = strstr(end, "<Message>")) {
		char idx[16] = "", phone[64] = "", date[32] = "", stat[8] = "", *text;
		size_t len = (size_t)(end - p);
		json_object *m;
		char *block = strndup(p, len);

		if (!block)
			break;
		text = calloc(1, len + 1);
		mm_web_xml_tag(block, "Index", idx, sizeof(idx));
		mm_web_xml_tag(block, "Phone", phone, sizeof(phone));
		mm_web_xml_tag(block, "Date", date, sizeof(date));
		mm_web_xml_tag(block, "Smstat", stat, sizeof(stat));
		if (text)
			mm_web_xml_tag(block, "Content", text, len + 1);
		seen++;
		for (size_t k = 0; *idx && k < json_object_array_length(msgs); k++)
			if (json_object_get_int(json_object_object_get(json_object_array_get_idx(msgs, k), "id")) == atoi(idx))
				*idx = '\0';	/* the same message in two boxes or pages: once */
		if (*idx && strspn(idx, "0123456789") == strlen(idx) && text) {
			bool in = box == 1 || box == 5;
			struct tm tm;
			char ts[40];

			xml_unescape(phone);
			xml_unescape(text);
			m = json_object_new_object();
			json_object_object_add(m, "id", json_object_new_int(atoi(idx)));
			json_object_object_add(m, "number", json_object_new_string(phone));
			json_object_object_add(m, "text", json_object_new_string(text));
			json_object_object_add(m, "state", json_object_new_string(in ? "received" : "sent"));
			json_object_object_add(m, "type", json_object_new_string(in ? "deliver" : "submit"));
			json_object_object_add(m, "storage", json_object_new_string(box >= 5 ? "sim" : "web"));
			if (in)
				json_object_object_add(m, "unread", json_object_new_boolean(!strcmp(stat, "0")));
			/* "2026-10-24 12:00:00" in the modem's clock, the router's zone */
			memset(&tm, 0, sizeof(tm));
			if (sscanf(date, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
				time_t t;

				tm.tm_year -= 1900;
				tm.tm_mon -= 1;
				tm.tm_isdst = -1;
				t = mktime(&tm);
				strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", localtime(&t));
				json_object_object_add(m, "timestamp", json_object_new_string(ts));
				json_object_object_add(m, "epoch", json_object_new_int64((long long)t));
			}
			json_object_array_add(msgs, m);
		}
		if (text) {
			explicit_bzero(text, len + 1);
			free(text);
		}
		explicit_bzero(block, len);
		free(block);
	}
	return seen;
}

/* a count of the sms-count answer kept in w->body, 0 without it */
static int hw_count(struct web *w, const char *tag)
{
	char v[16] = "";

	mm_web_xml_tag(w->body, tag, v, sizeof(v));
	return atoi(v);
}

/* ---- ZTE goform ---- */

#define ZTE_FIELDS "network_type,network_provider,rmcc,rmnc,rssi,lte_rssi,lte_rsrp,lte_rsrq,lte_snr,lte_pci,cell_id,lac_code," \
		   "wan_active_band,wan_active_channel,lte_ca_pcell_band,lte_ca_pcell_arfcn,lte_ca_pcell_bandwidth,lte_ca_scell_band," \
		   "Z5g_rsrp,Z5g_rsrq,Z5g_SINR,Z5g_snr,nr5g_pci,nr5g_action_band,Z5g_dlEarfcn,nr5g_action_channel," \
		   "model_name,wa_inner_version,cr_version,loginfo,sim_imsi,imei"

/*
 * The two paths of a ZTE web: goform/goform_get_cmd_process + goform_set_cmd_process on most, reqproc/proc_get +
 * proc_post on some firmwares (r96). The first answer that is a JSON object settles it.
 */
static const char *const zte_paths[][2] = {
	{ "/goform/goform_get_cmd_process", "/goform/goform_set_cmd_process" },
	{ "/reqproc/proc_get", "/reqproc/proc_post" },
};

static json_object *zte_get_on(struct web *w, int kind, const char *cmd, bool multi)
{
	char path[1024], hdr[200];
	json_object *r;

	snprintf(path, sizeof(path), "%s?isTest=false&cmd=%s%s&_=%lld", zte_paths[kind][0], cmd, multi ? "&multi_data=1" : "",
		 mono_ms());
	snprintf(hdr, sizeof(hdr), "%sReferer: http://%s/index.html\r\n", XHR, w->a->host);
	if (http(w, "GET", path, hdr, NULL) != 200)
		return NULL;
	/* a goform answer is an object; anything else is another web (a field report: an uhttpd 404 page
	 * echoed the URL, «cmd=...loginfo» and all, and a substring test took it for ZTE) - and a password is never
	 * sent to a web that is not a ZTE one */
	r = json_tokener_parse(w->body);
	if (!json_object_is_type(r, json_type_object)) {
		json_object_put(r);
		return NULL;
	}
	return r;
}

static json_object *zte_get(struct web *w, const char *cmd, bool multi)
{
	json_object *r = zte_get_on(w, w->zte_kind, cmd, multi);

	if (!r && !w->zte_known && w->zte_kind == 0 && (r = zte_get_on(w, 1, cmd, multi)))
		w->zte_kind = 1;
	if (r)
		w->zte_known = true;
	return r;
}

static bool zte_has_signal(json_object *z)
{
	return zs(z, "lte_rsrp") || zs(z, "Z5g_rsrp") || zs(z, "rssi") || zs(z, "lte_rssi");
}

static const char *zte_login(struct web *w, const char *password, bool chosen, int *wait_s)
{
	json_object *s = zte_get(w, "loginfo,psw_fail_num_str,login_lock_time,check_web_conflict", true), *r;
	char h1[65], *mix, h2[65], b64[200], body[300], hdr[300];
	const char *ld, *res;
	int lock;

	if (!s)
		return "failed";
	if (zs(s, "loginfo") && !strcmp(zs(s, "loginfo"), "ok")) {
		json_object_put(s);
		return NULL;
	}
	if (zs(s, "check_web_conflict") && !strcmp(zs(s, "check_web_conflict"), "1")) {
		json_object_put(s);
		return "busy";
	}
	lock = zs(s, "login_lock_time") ? atoi(zs(s, "login_lock_time")) : 0;
	if (zs(s, "psw_fail_num_str") && !strcmp(zs(s, "psw_fail_num_str"), "0") && lock > 0) {
		*wait_s = lock;
		json_object_put(s);
		return "locked";
	}
	json_object_put(s);
	s = zte_get(w, "LD", false);
	ld = zs(s, "LD");
	if (ld && strlen(ld) < 128) {
		/* upper-case hex: sha256(sha256(password) + LD) */
		mm_sha256_hex(password, strlen(password), true, h1);
		if (asprintf(&mix, "%s%s", h1, ld) < 0) {
			json_object_put(s);
			return "failed";
		}
		mm_sha256_hex(mix, strlen(mix), true, h2);
		explicit_bzero(mix, strlen(mix));
		free(mix);
		snprintf(body, sizeof(body), "isTest=false&goformId=LOGIN&password=%s", h2);
		explicit_bzero(h1, sizeof(h1));
		explicit_bzero(h2, sizeof(h2));
	} else {
		/* older firmwares: the password in base64 */
		base64(password, strlen(password), b64, sizeof(b64));
		snprintf(body, sizeof(body), "isTest=false&goformId=LOGIN&password=");
		for (char *p = b64; *p && strlen(body) + 4 < sizeof(body); p++)	/* form-encoded */
			snprintf(body + strlen(body), sizeof(body) - strlen(body), *p == '+' ? "%%2B" : *p == '/' ? "%%2F" : *p == '=' ? "%%3D" : "%c", *p);
		explicit_bzero(b64, sizeof(b64));
	}
	json_object_put(s);
	snprintf(hdr, sizeof(hdr), "%sReferer: http://%s/index.html\r\nContent-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n",
		 XHR, w->a->host);
	if (http(w, "POST", zte_paths[w->zte_kind][1], hdr, body) != 200) {
		explicit_bzero(body, sizeof(body));
		return "failed";
	}
	explicit_bzero(body, sizeof(body));
	r = json_tokener_parse(w->body);
	res = zs(r, "result");
	if (res && (!strcmp(res, "0") || !strcmp(res, "4"))) {
		json_object_put(r);
		return NULL;
	}
	json_object_put(r);
	return chosen ? "wrong" : "needed";
}

/* ---- the reading ---- */

static void copy(char *dst, size_t size, const char *src)
{
	size_t n = strnlen(src, size - 1);

	memcpy(dst, src, n);
	dst[n] = '\0';
}


/*
 * A «UZ801» UFI's web (05c6:f00e, firmware UZ801-V2.3.x; the owner's HAR 2026-10-30): POST /ajax with a JSON body
 * {"funcNo": N}; 1000 login {username, password} (admin/admin; the session is the client's address, no cookie),
 * 1001 {oper, netstatus «Connected», netmode «LTE»/«UNKNOWN», rssi 0..5 bars}, 1029 {dbm « -71 dBm», fwversion},
 * 1003 {up_bytes, down_bytes} in KB, 3002 {device_name, sw_version}, 1013 reboot, 1014 factory reset (never sent).
 * Logged out, it answers an HTML page that sends the browser to login.htm.
 */
/*
 * A Yota web (Gemtek WLTUBA 15a9:002d/003a, GCT LU150 1076:8002, ZTE MF922 «Yota» 19d2:0565): GET /status answers plain
 * «Key=Value» lines — InterfaceType, DeviceName, FirmwareVersion, State, 3GPP.RSRP/RSRQ/SINR/RSSI, 3GPP.MCC/MNC,
 * 3GPP.CI (hex), 3GPP.LAC (hex), 3GPP.SPN, 3GPP.RoamingStatus, 3GPP.IMSI (as live sticks answer,
 * 2026-11-08). The IMSI gives the SIM's operator code and goes no further.
 */
static bool yota_val(const char *body, const char *key, char *out, size_t size)
{
	size_t k = strlen(key);

	for (const char *p = body; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {
		const char *q = p;
		size_t n;

		while (*q == ' ' || *q == '\t')
			q++;
		if (strncmp(q, key, k))
			continue;
		q += k;
		while (*q == ' ' || *q == '\t')
			q++;
		if (*q != '=')
			continue;
		q++;
		while (*q == ' ' || *q == '\t')
			q++;
		n = strcspn(q, "\r\n");
		while (n && (q[n - 1] == ' ' || q[n - 1] == '\t'))
			n--;
		if (!n || n >= size)
			return false;
		memcpy(out, q, n);
		out[n] = 0;
		return true;
	}
	return false;
}

json_object *mm_web_yota_metrics(const char *body)
{
	json_object *m = json_object_new_object();
	char v[128], mcc[8], mnc[8];

	if (!body || !strstr(body, "InterfaceType"))
		return m;
	if (yota_val(body, "3GPP.RSRP", v, sizeof(v))) put_num(m, "rsrp_dbm", v, true);
	if (yota_val(body, "3GPP.RSRQ", v, sizeof(v))) put_num(m, "rsrq_db", v, false);
	if (yota_val(body, "3GPP.SINR", v, sizeof(v))) put_num(m, "sinr_db", v, false);
	if (yota_val(body, "3GPP.RSSI", v, sizeof(v))) put_num(m, "rssi_dbm", v, true);
	if (yota_val(body, "InterfaceType", v, sizeof(v)) && !strcasecmp(v, "lte"))
		put_str(m, "access_technology", "lte");
	if (yota_val(body, "3GPP.MCC", mcc, sizeof(mcc)) && yota_val(body, "3GPP.MNC", mnc, sizeof(mnc)) &&
	    strlen(mcc) == 3 && strspn(mcc, "0123456789") == 3 && *mnc && strlen(mnc) <= 3 && strspn(mnc, "0123456789") == strlen(mnc)) {
		snprintf(v, sizeof(v), "%s%s%s", mcc, strlen(mnc) == 1 ? "0" : "", mnc);
		put_str(m, "operator_code", v);
	}
	if (yota_val(body, "3GPP.SPN", v, sizeof(v)) && !strpbrk(v, "<>&\""))
		put_str(m, "operator", v);
	if (yota_val(body, "3GPP.CI", v, sizeof(v)) && strspn(v, "0123456789abcdefABCDEF") == strlen(v) && strlen(v) <= 8) {
		long long ci = strtoll(v, NULL, 16);

		if (ci > 0) {
			json_object_object_add(m, "cell_id", json_object_new_int64(ci));
			json_object_object_add(m, "enb", json_object_new_int64(ci / 256));
		}
	}
	if (yota_val(body, "3GPP.LAC", v, sizeof(v)) && strspn(v, "0123456789abcdefABCDEF") == strlen(v) && strlen(v) <= 6) {
		long tac = strtol(v, NULL, 16);

		if (tac > 0)
			json_object_object_add(m, "tac", json_object_new_int((int)tac));
	}
	if (yota_val(body, "DeviceName", v, sizeof(v)) && !strpbrk(v, "<>&\""))
		put_str(m, "model", v);
	if (yota_val(body, "FirmwareVersion", v, sizeof(v)) && !strpbrk(v, "<>&\""))
		put_str(m, "firmware", v);
	json_object_object_add(m, "manufacturer", json_object_new_string("Yota"));
	{
		char imsi[24], code[8];

		if (yota_val(body, "3GPP.IMSI", imsi, sizeof(imsi)) && simid_plmn_of_imsi(imsi, code, sizeof(code)))
			put_str(m, "sim_operator_code", code);
		explicit_bzero(imsi, sizeof(imsi));
	}
	return m;
}

json_object *mm_web_uz_metrics(json_object *status, json_object *info)
{
	json_object *m = json_object_new_object();
	const char *v;

	if ((v = json_object_get_string(json_object_object_get(info, "dbm"))))
		put_num(m, "rssi_dbm", v, true);
	if ((v = json_object_get_string(json_object_object_get(status, "oper"))) && *v && !strpbrk(v, "<>&\""))
		put_str(m, "operator", v);
	if ((v = json_object_get_string(json_object_object_get(status, "netmode"))) && strcasecmp(v, "UNKNOWN"))
		put_str(m, "access_technology", zte_tech(v));
	if ((v = json_object_get_string(json_object_object_get(info, "fwversion"))) && *v && !strpbrk(v, "<>&\""))
		put_str(m, "firmware", v);
	return m;
}

static const char *UZ_HDR = "X-Requested-With: XMLHttpRequest\r\nContent-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n";

/* one /ajax call: its results[0] (a new reference), NULL with *login true when the web wants a login */
static json_object *uz_call(struct web *w, const char *body, bool *login)
{
	json_object *j, *r = NULL, *res;

	*login = false;
	if (http(w, "POST", "/ajax", UZ_HDR, body) != 200)
		return NULL;
	if (strstr(w->body, "login.htm")) {
		*login = true;
		return NULL;
	}
	j = json_tokener_parse(w->body);
	if (j && json_object_object_get_ex(j, "results", &res) && json_object_is_type(res, json_type_array) &&
	    json_object_array_length(res) > 0)
		r = json_object_get(json_object_array_get_idx(res, 0));
	else if (j && !strcmp(json_object_get_string(json_object_object_get(j, "flag")) ? json_object_get_string(json_object_object_get(j, "flag")) : "", "1"))
		r = json_object_new_object();	/* an action's plain «flag 1» */
	json_object_put(j);
	return r;
}

/* the login (funcNo 1000): NULL when in, else the auth word */
static const char *uz_login(struct web *w, const char *password, bool chosen)
{
	json_object *q = json_object_new_object(), *r;
	bool login;

	json_object_object_add(q, "funcNo", json_object_new_int(1000));
	json_object_object_add(q, "username", json_object_new_string("admin"));
	json_object_object_add(q, "password", json_object_new_string(password));
	r = uz_call(w, json_object_to_json_string_ext(q, JSON_C_TO_STRING_PLAIN), &login);
	json_object_put(q);
	if (r) {
		json_object_put(r);
		return NULL;
	}
	return chosen ? "wrong" : "needed";
}

/* a call that logs in once when the web asks (at most once in MM_WEB_LOGIN_EVERY_S, as the other webs) */
static json_object *uz_ask(struct web *w, const char *body, const char *pw, bool chosen, struct state *st, const char **auth, int *wait_s)
{
	bool login;
	json_object *r = uz_call(w, body, &login);

	if (r || !login)
		return r;
	if (!w->a->fresh && st->last_login && w->a->now - st->last_login < MM_WEB_LOGIN_EVERY_S) {
		*auth = "waiting";
		*wait_s = (int)(MM_WEB_LOGIN_EVERY_S - (w->a->now - st->last_login));
		return NULL;
	}
	st->last_login = w->a->now;
	if ((*auth = uz_login(w, pw, chosen)))
		return NULL;
	*auth = "ok";
	st->last_login = 0;
	return uz_call(w, body, &login);
}

#define MARK_CODE_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_- "
static bool mark_logged_in(struct web *w);
static bool mark_login(struct web *w, const char *chosen);
static bool mark_open(struct web *w);

json_object *mm_web_signal(const struct mm_web_args *a)
{
	static struct web w;	/* the body buffer is large */
	json_object *o = json_object_new_object(), *m = NULL;
	const char *pw = a->password && *a->password ? a->password : "admin", *auth = "none", *kind = a->kind ? a->kind : "auto";
	bool chosen = a->password && *a->password, tried = false;
	struct state st;
	int wait_s = 0, code;

	memset(&w, 0, sizeof(w));
	w.a = a;
	state_read(a->state, &st);
	snprintf(w.cookie, sizeof(w.cookie), "%s", st.cookie);
	json_object_object_add(o, "host", json_object_new_string(a->host));

	if (!strcmp(kind, "auto")) {
		/* HiLink answers its session call; anything else is tried as ZTE */
		if (http(&w, "GET", "/api/webserver/SesTokInfo", XHR, NULL) == 200 && strstr(w.body, "<TokInfo>"))
			kind = "huawei";
		else if (w.code == 0) {
			json_object_object_add(o, "error", json_object_new_string("the modem's web does not answer"));
			goto done;
		} else if (http(&w, "GET", "/mark_title.w.xml", NULL, NULL) == 200 && strstr(w.body, "<dsc>"))
			kind = "mark";	/* a «Mobile.Router» web (MF150-4) */
		else if (http(&w, "POST", "/ajax", UZ_HDR, "{\"funcNo\":1001}") == 200 &&
			 ((*w.body == '{' && strstr(w.body, "\"flag\"")) || (strstr(w.body, "login.htm") && strstr(w.body, "top.window.open"))))
			kind = "uz";	/* a UZ801 UFI's /ajax (2026-10-30) */
		else if (http(&w, "GET", "/status", NULL, NULL) == 200 && strstr(w.body, "InterfaceType"))
			kind = "yota";	/* a Yota stick's plain «Key=Value» /status (2026-11-08) */
		else
			kind = "zte";
	}
	json_object_object_add(o, "kind", json_object_new_string(kind));

	if (!strcmp(kind, "mark")) {
		/* logged out it answers every tag empty (the owner's router, 2026-10-30): the page's own login first */
		char title[4096] = "";

		if (!mark_logged_in(&w) && !mark_open(&w)) {
			if (!a->fresh && st.last_login && a->now - st.last_login < MM_WEB_LOGIN_EVERY_S) {
				auth = "waiting";
				wait_s = (int)(MM_WEB_LOGIN_EVERY_S - (a->now - st.last_login));
				goto done;
			}
			st.last_login = a->now;
			if (!mark_login(&w, chosen ? pw : NULL)) {
				auth = chosen ? "wrong" : "needed";
				goto done;
			}
			auth = "ok";
			tried = true;
		}
		if (http(&w, "GET", "/mark_title.w.xml", NULL, NULL) == 200)
			copy(title, sizeof(title), w.body);
		if (http(&w, "GET", "/mark_home.w.xml", NULL, NULL) != 200 || !strstr(w.body, "<status>")) {
			json_object_object_add(o, "error", json_object_new_string(w.code ? "the modem's web refused the signal" :
										  "the modem's web does not answer"));
			goto done;
		}
		/* nothing at all: «-1» for the signal, the cell and the operator (the tester's M150-4, 2026-11-09: the stick
		 * stopped seeing its SIM, the card said only «оператор пока неизвестен» and recovery restarted the session
		 * for nothing). An empty tag is a logged-out page, not this. The page has no SIM field we know: its PIN state
		 * (mark_title's pin, mark_set_pin's state) goes into the answer as it is, a short code, for the next report */
		{
			char r[160], c[160], n[64], code[64] = "", v[32];
			/* logged in (or a page open without a login) by now: an empty tag is nothing too (the tester's report of
			 * 2026-11-09: logged in, not one field read) */
			bool none = mm_web_xml_tag(w.body, "rssi_info", r, sizeof(r)) >= 0 && (!*r || !strcmp(r, "-1")) &&
				    mm_web_xml_tag(w.body, "cell_info", c, sizeof(c)) >= 0 && (!*c || !strcmp(c, "-1")) &&
				    (mm_web_xml_tag(title, "cspn", n, sizeof(n)) <= 0 || !strcmp(n, "-1"));
			char ns[16];

			/* never while the stick says its data link is up (netstatus 9), and empty tags alone prove nothing (a page
			 * that lost its login answers so): then only with a netstatus that says «not connected». A wrong «no
			 * network» ends the internet check down without a probe (regression review 2026-11-11) */
			if (none && mm_web_xml_tag(w.body, "netstatus", ns, sizeof(ns)) > 0 && !strcmp(ns, "9"))
				none = false;
			if (none && !*r && !*c && !(mm_web_xml_tag(w.body, "netstatus", ns, sizeof(ns)) > 0 && strspn(ns, "0123456789") == strlen(ns)))
				none = false;

			if (none) {
				if (mm_web_xml_tag(title, "pin", v, sizeof(v)) > 0 && strspn(v, MARK_CODE_CHARS) == strlen(v))
					snprintf(code, sizeof(code), "pin=%s", v);
				if (http(&w, "GET", "/mark_set_pin.w.xml", NULL, NULL) == 200 && mm_web_xml_tag(w.body, "state", v, sizeof(v)) > 0 &&
				    strspn(v, MARK_CODE_CHARS) == strlen(v))
					snprintf(code + strlen(code), sizeof(code) - strlen(code), "%sstate=%s", *code ? "," : "", v);
				if (*code)
					json_object_object_add(o, "sim_code", json_object_new_string(code));
				if (strcasestr(code, "absent") || strcasestr(code, "nosim") || strcasestr(code, "no_sim") || strcasestr(code, "insert")) {
					json_object_object_add(o, "sim", json_object_new_string("absent"));
					json_object_object_add(o, "error", json_object_new_string("the modem has no SIM card"));
				} else {
					json_object_object_add(o, "network", json_object_new_string("none"));
					json_object_object_add(o, "error", json_object_new_string("the modem has no mobile network"));
				}
				goto done;
			}
		}
		m = mm_web_mark_metrics(w.body, title);
	} else if (!strcmp(kind, "yota")) {
		char state[64];

		/* no login: the page is open on the stick's own network */
		if (http(&w, "GET", "/status", NULL, NULL) != 200 || !strstr(w.body, "InterfaceType")) {
			json_object_object_add(o, "error", json_object_new_string(w.code ? "the modem's web refused the signal" :
										  "the modem's web does not answer"));
			goto done;
		}
		m = mm_web_yota_metrics(w.body);
		/* its own data link, as a HiLink's (Connected / Disconnected, or the Russian words of an older firmware) */
		if (yota_val(w.body, "State", state, sizeof(state))) {
			json_object *dl = json_object_new_object();
			bool up = !strncasecmp(state, "Connected", 9) || !strncmp(state, "Подключено", strlen("Подключено"));
			bool down = !strncasecmp(state, "Disconnected", 12) || !strncmp(state, "Отключено", strlen("Отключено"));

			json_object_object_add(dl, "state", json_object_new_string(up ? "connected" : down ? "disconnected" : "connecting"));
			json_object_object_add(o, "data_link", dl);
		}
		explicit_bzero(w.body, sizeof(w.body));	/* it carries the IMEI, the IMSI and the number */
	} else if (!strcmp(kind, "uz")) {
		json_object *stat = uz_ask(&w, "{\"funcNo\":1001}", pw, chosen, &st, &auth, &wait_s), *inf;

		if (!stat) {
			if (!strcmp(auth, "none"))
				json_object_object_add(o, "error", json_object_new_string(w.code ? "the modem's web refused the signal" :
											  "the modem's web does not answer"));
			goto done;
		}
		inf = uz_call(&w, "{\"funcNo\":1029}", &(bool){ false });
		m = mm_web_uz_metrics(stat, inf);
		explicit_bzero(w.body, sizeof(w.body));	/* 1029 carries the IMEI */
		json_object_put(inf);
		json_object_put(stat);
	} else if (!strcmp(kind, "huawei")) {
		char signal[4096] = "", info[4096] = "", plmn[2048] = "";

		hw_session(&w);
		code = hw_get(&w, "/api/device/signal");
		if (hw_login_wanted(code)) {
			if (!a->fresh && st.last_login && a->now - st.last_login < MM_WEB_LOGIN_EVERY_S) {
				auth = "waiting";
				wait_s = (int)(MM_WEB_LOGIN_EVERY_S - (a->now - st.last_login));
				goto done;
			}
			st.last_login = a->now;
			tried = true;
			if ((auth = hw_login(&w, pw, chosen, &wait_s)))
				goto done;
			auth = "ok";
			code = hw_get(&w, "/api/device/signal");
		}
		if (code) {
			json_object_object_add(o, "error", json_object_new_string(code < 0 ? "the modem's web does not answer" : "the modem's web refused the signal"));
			goto done;
		}
		copy(signal, sizeof(signal), w.body);
		/* without its SIM the stick still camps on a cell and keeps the last operator: the owner's E3372 (r123) showed
		 * «T2, LTE, -86 dBm» with the SIM out. SimStatus 1 is a usable SIM; anything else is said instead */
		if (hw_get(&w, "/api/monitoring/status") == 0) {
			char sim[16];

			if (mm_web_xml_tag(w.body, "SimStatus", sim, sizeof(sim)) > 0 && strcmp(sim, "1")) {
				bool none = !strcmp(sim, "0") || !strcmp(sim, "255");

				json_object_object_add(o, "sim", json_object_new_string(none ? "absent" : "invalid"));
				json_object_object_add(o, "error", json_object_new_string(none ? "the modem has no SIM card" :
							"the modem's SIM card is not usable"));
				goto done;
			}
			/* no network: the stick keeps its last cell's numbers in /api/device/signal (the owner's E3372-325,
			 * 2026-11-03: «Нет сигнала, нет сети» on its page and a red LED, the card «отличный сигнал -86» and the
			 * operator). ServiceStatus 2 is a usable service; anything else is said instead of stale numbers */
			/* the stick's own mobile data link (HiLink ConnectionStatus, the owner 2026-11-04: «SIM и сеть ✓, Интернет ✗»
			 * on an E3372h for 63 h, and nothing said whether the stick itself was connected): 901 connected, 900
			 * connecting, 902 disconnected, 903 disconnecting, 112-115 no automatic (re)connection, at home or roaming;
			 * any other code a refusal of the network, kept as its number */
			{
				char cs[16];

				if (mm_web_xml_tag(w.body, "ConnectionStatus", cs, sizeof(cs)) > 0 && strspn(cs, "0123456789") == strlen(cs)) {
					int c = atoi(cs);
					json_object *dl = json_object_new_object();

					json_object_object_add(dl, "code", json_object_new_int(c));
					json_object_object_add(dl, "state", json_object_new_string(
						c == 901 ? "connected" : c == 900 ? "connecting" : c == 902 ? "disconnected" : c == 903 ? "disconnecting" :
						c == 112 || c == 114 ? "no_autoconnect" : c == 113 || c == 115 ? "roaming_off" : "failed"));
					json_object_object_add(o, "data_link", dl);
				}
			}
			if (mm_web_xml_tag(w.body, "ServiceStatus", sim, sizeof(sim)) > 0 && strcmp(sim, "2")) {
				json_object_object_add(o, "network", json_object_new_string("none"));
				json_object_object_add(o, "error", json_object_new_string("the modem has no mobile network"));
				goto done;
			}
		}
		if (hw_get(&w, "/api/device/information") == 0)
			copy(info, sizeof(info), w.body);
		if (hw_get(&w, "/api/net/current-plmn") == 0)
			copy(plmn, sizeof(plmn), w.body);
		m = mm_web_hilink_metrics(signal, info, plmn);
		/* the stick's IMEI for the engine alone: it keys the name and settings to the stick, not to its USB ID
		 * (the owner, 2026-10-31: «серийник и IMEI берём, а привязка просто по USB?»). The engine turns it into
		 * its salted key at once and drops it; nothing else of the information is kept */
		{
			char imei[24];

			if (mm_web_xml_tag(info, "Imei", imei, sizeof(imei)) == 15 && strspn(imei, "0123456789") == 15)
				json_object_object_add(o, "id_imei", json_object_new_string(imei));
			explicit_bzero(imei, sizeof(imei));
		}
		explicit_bzero(info, sizeof(info));	/* it carries the IMEI and the IMSI too */
		if (hw_get(&w, "/config/deviceinformation/add_param.xml") == 0)
			hw_ext_params(m, w.body);
	} else {
		json_object *z = zte_get(&w, ZTE_FIELDS, true);

		if (!z) {
			/* a web that answers HTTP but neither HiLink nor ZTE (a UFI stick's own, bench 2026-10-19) is said apart */
			json_object_object_add(o, "error", json_object_new_string(w.code ? "the modem's web is not a kind read yet" :
				"the modem's web does not answer"));
			if (w.code)
				json_object_object_add(o, "unknown_kind", json_object_new_boolean(true));
			goto done;
		}
		/* logged out: no signal and no "ok" in loginfo */
		if (!zte_has_signal(z) && !(zs(z, "loginfo") && !strcmp(zs(z, "loginfo"), "ok"))) {
			json_object_put(z);
			if (!a->fresh && st.last_login && a->now - st.last_login < MM_WEB_LOGIN_EVERY_S) {
				auth = "waiting";
				wait_s = (int)(MM_WEB_LOGIN_EVERY_S - (a->now - st.last_login));
				goto done;
			}
			st.last_login = a->now;
			tried = true;
			if ((auth = zte_login(&w, pw, chosen, &wait_s)))
				goto done;
			auth = "ok";
			if (!(z = zte_get(&w, ZTE_FIELDS, true))) {
				json_object_object_add(o, "error", json_object_new_string("the modem's web does not answer"));
				goto done;
			}
		}
		/* a ZXIC (Sanechips) firmware leaves the standard quality fields empty and keeps its own nv_* ones, open without a
		 * login (the tester's MTS 8810FT, 2026-11-11: RSRP only; field notes on the Olax MT80):
		 * asked apart, the main request stays as it was for every other ZTE */
		if (!zs(z, "lte_rsrq") && !zs(z, "lte_snr") && zs(z, "network_type")) {
			json_object *x = zte_get(&w, "nv_rsrp,nv_rsrq,nv_sinr,nv_pci,nv_arfcn,tac_code,mcc,mnc", true);

			if (x) {
				json_object_object_foreach(x, k, v)
					if (json_object_get_string(v) && *json_object_get_string(v))
						json_object_object_add(z, k, json_object_get(v));
				json_object_put(x);
			}
		}
		m = mm_web_zte_metrics(z);
		/* the stick's IMEI for the engine alone, as a HiLink's (a tester's MF833V, 2026-11-08: a ZTE in router mode has
		 * no serial of its own; on another USB port after a reboot its settings, adopt among them, stayed behind) */
		if (zs(z, "imei") && strlen(zs(z, "imei")) == 15 && strspn(zs(z, "imei"), "0123456789") == 15)
			json_object_object_add(o, "id_imei", json_object_new_string(zs(z, "imei")));
		if (zs(z, "imei"))
			explicit_bzero((char *)zs(z, "imei"), strlen(zs(z, "imei")));
		json_object_put(z);
	}
	if (tried)
		st.last_login = 0;	/* logged in: the next login, when the session ends, need not wait */

done:
	json_object_object_add(o, "auth", json_object_new_string(auth));
	if (wait_s > 0)
		json_object_object_add(o, "wait_s", json_object_new_int(wait_s));
	if (m)
		json_object_object_add(o, "metrics", m);
	snprintf(st.cookie, sizeof(st.cookie), "%s", w.cookie);
	state_write(a->state, &st);
	explicit_bzero(&w, sizeof(w));
	return o;
}

/* ---- ZTE goform SMS (r95; the API of MF79/MF833-class sticks) ---- */

/* one code point as UTF-8; the bytes written */
static size_t utf8_put(unsigned long cp, char *out)
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xc0 | cp >> 6);
		out[1] = (char)(0x80 | (cp & 0x3f));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xe0 | cp >> 12);
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[2] = (char)(0x80 | (cp & 0x3f));
		return 3;
	}
	out[0] = (char)(0xf0 | cp >> 18);
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
	out[3] = (char)(0x80 | (cp & 0x3f));
	return 4;
}

/*
 * The text and the number come as UCS-2 hex (UTF-16BE, surrogate pairs for emoji); some firmwares write short
 * Latin text plainly. A number is taken for hex only when every unit starts with "00" (ASCII): «9000» is a live
 * short number and valid hex alike.
 */
static void zte_ucs2(const char *in, bool number, char *out, size_t size)
{
	size_t n = strlen(in), used = 0, i;
	bool hex = n && n % 4 == 0 && strspn(in, "0123456789abcdefABCDEF") == n;

	for (i = 0; hex && number && i < n; i += 4)
		hex = in[i] == '0' && in[i + 1] == '0';
	if (hex && number && n < 8)
		hex = false;
	if (!hex) {
		snprintf(out, size, "%s", in);
		return;
	}
	for (i = 0; i + 4 <= n && used + 5 < size; i += 4) {
		char u[5] = { in[i], in[i + 1], in[i + 2], in[i + 3], 0 };
		unsigned long cp = strtoul(u, NULL, 16);

		if (cp >= 0xd800 && cp < 0xdc00 && i + 8 <= n) {
			char l[5] = { in[i + 4], in[i + 5], in[i + 6], in[i + 7], 0 };
			unsigned long lo = strtoul(l, NULL, 16);

			if (lo >= 0xdc00 && lo < 0xe000) {
				cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
				i += 4;
			}
		}
		if (cp == 0 || (cp >= 0xd800 && cp < 0xe000))
			continue;
		used += utf8_put(cp, out + used);
	}
	out[used] = '\0';
}

/* UTF-8 to UCS-2 hex, upper case, surrogate pairs above the BMP; false when it does not fit or is not UTF-8 */
static bool zte_hex(const char *in, char *out, size_t size)
{
	const unsigned char *p = (const unsigned char *)in;
	size_t used = 0;

	while (*p) {
		unsigned long cp;
		int more = *p < 0x80 ? 0 : (*p & 0xe0) == 0xc0 ? 1 : (*p & 0xf0) == 0xe0 ? 2 : (*p & 0xf8) == 0xf0 ? 3 : -1;

		if (more < 0)
			return false;
		cp = more == 0 ? *p : *p & (0x3f >> more);
		for (p++; more > 0; more--, p++) {
			if ((*p & 0xc0) != 0x80)
				return false;
			cp = cp << 6 | (*p & 0x3f);
		}
		if (cp > 0xffff) {
			if (used + 9 > size)
				return false;
			cp -= 0x10000;
			used += (size_t)snprintf(out + used, size - used, "%04lX%04lX", 0xd800 + (cp >> 10), 0xdc00 + (cp & 0x3ff));
		} else {
			if (used + 5 > size)
				return false;
			used += (size_t)snprintf(out + used, size - used, "%04lX", cp);
		}
	}
	out[used] = '\0';
	return true;
}

/* a goform write; true when the web says "success" */
static bool zte_post(struct web *w, const char *form)
{
	char hdr[300];
	json_object *r;
	const char *res;
	bool ok;

	snprintf(hdr, sizeof(hdr), "%sReferer: http://%s/index.html\r\nContent-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n",
		 XHR, w->a->host);
	if (http(w, "POST", zte_paths[w->zte_kind][1], hdr, form) != 200)
		return false;
	r = json_tokener_parse(w->body);
	res = zs(r, "result");
	ok = res && (!strcmp(res, "success") || !strcmp(res, "0"));
	json_object_put(r);
	return ok;
}

/*
 * A set command; one the web refuses goes again with AD, the token newer MF79U/MF833-class firmwares ask of every
 * write but the login: md5(md5(wa_inner_version + cr_version) + RD), RD a fresh nonce from the web (the ZTE web's own
 * page script, as public ZTE API clients describe it; not on our bench). An older firmware never asked is never sent it
 */
static bool zte_set(struct web *w, const char *form)
{
	json_object *z;
	char h1[33], h2[33], *mix = NULL, *more = NULL;
	const char *wa, *cr, *rd;
	bool ok;

	if (zte_post(w, form))
		return true;
	if (w->code != 200)	/* no answer, or not a goform one: nothing to sign */
		return false;
	if (!(z = zte_get(w, "wa_inner_version,cr_version,RD", true)))
		return false;
	wa = zs(z, "wa_inner_version");
	cr = zs(z, "cr_version");
	rd = zs(z, "RD");
	if (!rd || !*rd || strlen(rd) > 64 || asprintf(&mix, "%s%s", wa ? wa : "", cr ? cr : "") < 0) {
		json_object_put(z);
		return false;
	}
	mm_md5_hex(mix, strlen(mix), h1);
	free(mix);
	mix = NULL;
	if (asprintf(&mix, "%s%s", h1, rd) < 0) {
		json_object_put(z);
		return false;
	}
	json_object_put(z);
	mm_md5_hex(mix, strlen(mix), h2);
	free(mix);
	if (asprintf(&more, "%s&AD=%s", form, h2) < 0)
		return false;
	ok = zte_post(w, more);
	explicit_bzero(more, strlen(more));
	free(more);
	return ok;
}

/* "26,10,24,12,30,00,+12" (the modem's local time, the zone in quarter hours) to the epoch and an ISO time */
static void zte_date(json_object *m, const char *d)
{
	int f[6], q = 0, n = 0;
	char sign = '+';
	struct tm tm;
	long long t;
	char ts[40];

	if (!d || sscanf(d, "%d,%d,%d,%d,%d,%d,%n", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &n) != 6)
		return;
	if (d[n] == '+' || d[n] == '-') {
		sign = d[n];
		q = atoi(d + n + 1);
	}
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 2000 + f[0] - 1900;
	tm.tm_mon = f[1] - 1;
	tm.tm_mday = f[2];
	tm.tm_hour = f[3];
	tm.tm_min = f[4];
	tm.tm_sec = f[5];
	t = (long long)timegm(&tm) - (sign == '-' ? -1 : 1) * q * 900LL;
	snprintf(ts, sizeof(ts), "20%02d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d", f[0], f[1], f[2], f[3], f[4], f[5], sign, q / 4, q % 4 * 15);
	json_object_object_add(m, "timestamp", json_object_new_string(ts));
	json_object_object_add(m, "epoch", json_object_new_int64(t));
}

/*
 * The messages: the modem's own storage first, the SIM's when that is empty (firmwares differ in which they use).
 * tag 0 read, 1 unread (the inbox), 2 sent, 3 not sent; 4, a draft, is left out. Never marked read here.
 */
static void zte_sms_list(struct web *w, json_object *msgs)
{
	int store;

	/*
	 * Both stores, the modem's and the SIM's: a sent message in the modem's hid the inbox older firmware keeps on the
	 * SIM (review 2026-11-15). A firmware that ignores mem_store gives the same list twice: an id is taken once.
	 */
	for (store = 1; store >= 0; store--) {
		char cmd[160];
		json_object *r, *list;
		size_t i;

		snprintf(cmd, sizeof(cmd), "sms_data_total&page=0&data_per_page=100&mem_store=%d&tags=10&order_by=order%%20by%%20id%%20desc", store);
		r = zte_get(w, cmd, false);
		list = json_object_object_get(r, "messages");
		for (i = 0; json_object_is_type(list, json_type_array) && i < json_object_array_length(list); i++) {
			json_object *x = json_object_array_get_idx(list, i), *m;
			const char *id = zs(x, "id"), *tag = zs(x, "tag");
			char *buf;
			size_t len;
			int t = tag ? atoi(tag) : -1;

			if (!id || strspn(id, "0123456789") != strlen(id) || t < 0 || t > 3)
				continue;
			{
				bool seen = false;

				for (size_t k = 0; k < json_object_array_length(msgs) && !seen; k++)
					seen = json_object_get_int(json_object_object_get(json_object_array_get_idx(msgs, k), "id")) == atoi(id);
				if (seen)
					continue;
			}
			len = strlen(zs(x, "content") ? zs(x, "content") : "") + strlen(zs(x, "number") ? zs(x, "number") : "") + 8;
			if (!(buf = malloc(len)))
				continue;
			m = json_object_new_object();
			json_object_object_add(m, "id", json_object_new_int(atoi(id)));
			zte_ucs2(zs(x, "number") ? zs(x, "number") : "", true, buf, len);
			json_object_object_add(m, "number", json_object_new_string(buf));
			zte_ucs2(zs(x, "content") ? zs(x, "content") : "", false, buf, len);
			json_object_object_add(m, "text", json_object_new_string(buf));
			explicit_bzero(buf, len);
			free(buf);
			json_object_object_add(m, "state", json_object_new_string(t <= 1 ? "received" : t == 2 ? "sent" : "unsent"));
			json_object_object_add(m, "type", json_object_new_string(t <= 1 ? "deliver" : "submit"));
			json_object_object_add(m, "storage", json_object_new_string("web"));
			if (t <= 1)
				json_object_object_add(m, "unread", json_object_new_boolean(t == 1));
			zte_date(m, zs(x, "date"));
			json_object_array_add(msgs, m);
		}
		json_object_put(r);
	}
}

static void zte_sms(struct web *w, const char *op, const char *number, const char *text, int index, json_object *o)
{
	if (!strcmp(op, "list")) {
		json_object *msgs = json_object_new_array();

		zte_sms_list(w, msgs);
		json_object_object_add(o, "messages", msgs);
	} else if (!strcmp(op, "send") && number && text) {
		/* the time as the firmware writes it: "yy;mm;dd;hh;mm;ss;+<quarter hours>" */
		size_t need = strlen(text) * 4 + 8, n;
		char *hex = malloc(need), *form = malloc(need + 300), num[64] = "", when[40];
		time_t now = time(NULL);
		struct tm lt;
		long off;

		localtime_r(&now, &lt);
		off = lt.tm_gmtoff / 900;
		n = strftime(when, sizeof(when), "%y;%m;%d;%H;%M;%S;", &lt);
		snprintf(when + n, sizeof(when) - n, "%s%ld", off < 0 ? "-" : "%2B", off < 0 ? -off : off);	/* form-encoded, as the number's + */
		for (const char *p = number; *p && strlen(num) + 4 < sizeof(num); p++)
			snprintf(num + strlen(num), sizeof(num) - strlen(num), *p == '+' ? "%%2B" : "%c", *p);
		if (!hex || !form || !zte_hex(text, hex, need)) {
			json_object_object_add(o, "error", json_object_new_string("the message does not fit"));
		} else {
			snprintf(form, need + 300, "isTest=false&goformId=SEND_SMS&notCallback=true&Number=%s&sms_time=%s&MessageBody=%s"
				 "&ID=-1&encode_type=UNICODE", num, when, hex);
			json_object_object_add(o, "ok", json_object_new_boolean(zte_set(w, form)));
			if (!json_object_get_boolean(json_object_object_get(o, "ok")))
				json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web refused it" :
											  "the modem's web did not answer"));
		}
		if (hex) {
			explicit_bzero(hex, need);
			free(hex);
		}
		if (form) {
			explicit_bzero(form, need + 300);
			free(form);
		}
	} else if (!strcmp(op, "delete") && index >= 0) {
		char form[160];

		snprintf(form, sizeof(form), "isTest=false&goformId=DELETE_SMS&msg_id=%d;&notCallback=true", index);
		json_object_object_add(o, "ok", json_object_new_boolean(zte_set(w, form)));
		if (!json_object_get_boolean(json_object_object_get(o, "ok")))
			json_object_object_add(o, "error", json_object_new_string("the modem's web refused it"));
	} else {
		json_object_object_add(o, "error", json_object_new_string("list, send or delete"));
	}
}

/*
 * SMS of a HiLink modem through its web (r94): op "list" (both boxes),
 * "send" (number, text), "delete" (index). The login as for the signal, the same once-in-5-minutes rule.
 * {"kind", "auth", "messages"? [...], "ok"?, "error"?}. Nothing here is logged; the text lives in the answer only.
 */
static void mark_sms(struct web *w, const char *op, const char *number, const char *text, int index, json_object *o);

json_object *mm_web_sms(const struct mm_web_args *a, const char *op, const char *number, const char *text, int index)
{
	static struct web w;
	json_object *o = json_object_new_object();
	const char *pw = a->password && *a->password ? a->password : "admin", *auth = "none";
	bool chosen = a->password && *a->password, tried = false;
	struct state st;
	int wait_s = 0, code;

	memset(&w, 0, sizeof(w));
	w.a = a;
	state_read(a->state, &st);
	snprintf(w.cookie, sizeof(w.cookie), "%s", st.cookie);
	if (a->kind && !strcmp(a->kind, "mark")) {
		/* a «Mobile.Router» web asks no login (MF150-4, 2026-10-30) */
		json_object_object_add(o, "kind", json_object_new_string("mark"));
		if (!mark_logged_in(&w) && !mark_open(&w) && !mark_login(&w, chosen ? pw : NULL)) {
			auth = chosen ? "wrong" : "needed";
			goto done;
		}
		mark_sms(&w, op, number, text, index, o);
		goto done;
	}
	if (a->kind && !strcmp(a->kind, "zte")) {
		json_object *z = zte_get(&w, "loginfo", true);
		bool in = zs(z, "loginfo") && !strcmp(zs(z, "loginfo"), "ok");

		json_object_object_add(o, "kind", json_object_new_string("zte"));
		if (!z) {
			json_object_object_add(o, "error", json_object_new_string(w.code ? "the modem's web is not a kind read yet" :
										      "the modem's web does not answer"));
			goto done;
		}
		json_object_put(z);
		if (!in) {
			if (!a->fresh && st.last_login && a->now - st.last_login < MM_WEB_LOGIN_EVERY_S) {
				auth = "waiting";
				wait_s = (int)(MM_WEB_LOGIN_EVERY_S - (a->now - st.last_login));
				goto done;
			}
			st.last_login = a->now;
			tried = true;
			if ((auth = zte_login(&w, pw, chosen, &wait_s)))
				goto done;
			auth = "ok";
		}
		zte_sms(&w, op, number, text, index, o);
		if (tried)
			st.last_login = 0;
		goto done;
	}
	json_object_object_add(o, "kind", json_object_new_string("huawei"));
	hw_session(&w);
	code = hw_get(&w, "/api/sms/sms-count");
	if (hw_login_wanted(code)) {
		if (!a->fresh && st.last_login && a->now - st.last_login < MM_WEB_LOGIN_EVERY_S) {
			auth = "waiting";
			wait_s = (int)(MM_WEB_LOGIN_EVERY_S - (a->now - st.last_login));
			goto done;
		}
		st.last_login = a->now;
		tried = true;
		if ((auth = hw_login(&w, pw, chosen, &wait_s)))
			goto done;
		auth = "ok";
		code = hw_get(&w, "/api/sms/sms-count");
	}
	if (code) {
		json_object_object_add(o, "error", json_object_new_string(code < 0 ? "the modem's web does not answer" :
									  "the modem's web has no SMS"));
		goto done;
	}
	if (!strcmp(op, "list")) {
		json_object *msgs = json_object_new_array();
		/* the counts of the sms-count asked above: the SIM card's boxes too (the owner's E3372, 2026-11-06: Yota's SMS
		 * of 3 November in the stick's web, none in the list: it sat on the SIM), and the inbox past its first 50 */
		int local_in = hw_count(&w, "LocalInbox"), sim_in = hw_count(&w, "SimInbox"), sim_out = hw_count(&w, "SimOutbox");
		int page;

		for (page = 1; page <= 4 && hw_sms_box(&w, 1, page, msgs) == 50 && page * 50 < local_in; page++)
			;
		hw_sms_box(&w, 2, 1, msgs);
		if (sim_in > 0)
			hw_sms_box(&w, 5, 1, msgs);
		if (sim_out > 0)
			hw_sms_box(&w, 6, 1, msgs);
		json_object_object_add(o, "messages", msgs);
	} else if (!strcmp(op, "send") && number && text) {
		char num[96], *body, *esc;
		size_t need = strlen(text) * 6 + 1;
		char date[24];
		time_t now = time(NULL);

		esc = malloc(need);
		body = malloc(need + 512);
		strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&now));
		if (!esc || !body || !xml_escape(number, num, sizeof(num)) || !xml_escape(text, esc, need)) {
			json_object_object_add(o, "error", json_object_new_string("the message does not fit"));
		} else {
			snprintf(body, need + 512, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><request><Index>-1</Index><Phones><Phone>%s</Phone>"
				 "</Phones><Sca></Sca><Content>%s</Content><Length>%zu</Length><Reserved>1</Reserved><Date>%s</Date></request>",
				 num, esc, utf8_chars(text), date);
			code = hw_post(&w, "/api/sms/send-sms", body);
			json_object_object_add(o, "ok", json_object_new_boolean(code == 0 && strstr(w.body, "<response>OK</response>")));
			if (code) {
				char e[80];

				snprintf(e, sizeof(e), code < 0 ? "the modem's web did not answer" : "the modem's web refused it (%d)", code);
				json_object_object_add(o, "error", json_object_new_string(e));
			}
		}
		if (esc) {
			explicit_bzero(esc, need);
			free(esc);
		}
		if (body) {
			explicit_bzero(body, need + 512);
			free(body);
		}
	} else if (!strcmp(op, "delete") && index >= 0) {
		char body[160];

		snprintf(body, sizeof(body), "<?xml version=\"1.0\" encoding=\"UTF-8\"?><request><Index>%d</Index></request>", index);
		code = hw_post(&w, "/api/sms/delete-sms", body);
		json_object_object_add(o, "ok", json_object_new_boolean(code == 0));
		if (code)
			json_object_object_add(o, "error", json_object_new_string(code < 0 ? "the modem's web did not answer" :
										  "the modem's web refused it"));
	} else {
		json_object_object_add(o, "error", json_object_new_string("list, send or delete"));
	}
	if (tried)
		st.last_login = 0;
done:
	json_object_object_add(o, "auth", json_object_new_string(auth));
	if (wait_s > 0)
		json_object_object_add(o, "wait_s", json_object_new_int(wait_s));
	snprintf(st.cookie, sizeof(st.cookie), "%s", w.cookie);
	state_write(a->state, &st);
	explicit_bzero(&w, sizeof(w));
	return o;
}

/* ---- r117: more of a HiLink's web (the owner, 2026-10-28: «там вон сколько полезного для вывода в luci») ---- */

/* a number of the answer's tag, -1 without one */
static long long hw_num(const char *xml, const char *tag)
{
	char v[32];

	return mm_web_xml_tag(xml, tag, v, sizeof(v)) > 0 && strspn(v, "0123456789") == strlen(v) ? strtoll(v, NULL, 10) : -1;
}

static void act_num(json_object *o, const char *key, long long v)
{
	if (v >= 0)
		json_object_object_add(o, key, json_object_new_int64(v));
}

/* a short text of the answer's tag: printable, no markup */
static void act_text(json_object *o, const char *key, const char *xml, const char *tag)
{
	char v[128];

	if (mm_web_xml_tag(xml, tag, v, sizeof(v)) > 0 && !strpbrk(v, "<>&\"") && strlen(v) < 96)
		json_object_object_add(o, key, json_object_new_string(v));
}

/* the session's traffic, the month's and the whole time's: bytes and seconds */
static void hw_traffic(struct web *w, json_object *o)
{
	json_object *t = json_object_new_object(), *s = json_object_new_object(), *m = json_object_new_object(),
		    *all = json_object_new_object();

	if (hw_get(w, "/api/monitoring/traffic-statistics") == 0) {
		act_num(s, "rx", hw_num(w->body, "CurrentDownload"));
		act_num(s, "tx", hw_num(w->body, "CurrentUpload"));
		act_num(s, "s", hw_num(w->body, "CurrentConnectTime"));
		act_num(all, "rx", hw_num(w->body, "TotalDownload"));
		act_num(all, "tx", hw_num(w->body, "TotalUpload"));
		act_num(all, "s", hw_num(w->body, "TotalConnectTime"));
	}
	if (hw_get(w, "/api/monitoring/month_statistics") == 0) {
		act_num(m, "rx", hw_num(w->body, "CurrentMonthDownload"));
		act_num(m, "tx", hw_num(w->body, "CurrentMonthUpload"));
		act_num(m, "s", hw_num(w->body, "MonthDuration"));
		act_text(m, "cleared", w->body, "MonthLastClearTime");
		/* today's (the E3372-325 HAR, 2026-10-30) */
		{
			json_object *d = json_object_new_object();

			act_num(d, "bytes", hw_num(w->body, "CurrentDayUsed"));
			act_num(d, "s", hw_num(w->body, "CurrentDayDuration"));
			if (json_object_object_length(d))
				json_object_object_add(t, "day", d);
			else
				json_object_put(d);
		}
	}
	/* the billing month as the modem counts it: its first day, the limit and whether the limit cuts the data */
	if (hw_get(w, "/api/monitoring/start_date") == 0) {
		json_object *plan = json_object_new_object();
		char lim[32];

		act_num(plan, "start_day", hw_num(w->body, "StartDay"));
		if (mm_web_xml_tag(w->body, "DataLimit", lim, sizeof(lim)) > 0 && strcmp(lim, "0MB") && strspn(lim, "0123456789.MGB") == strlen(lim))
			put_str(plan, "limit", lim);
		act_num(plan, "threshold_pct", hw_num(w->body, "MonthThreshold"));
		if (hw_num(w->body, "turnoffdataenable") == 1)
			json_object_object_add(plan, "limit_cuts_data", json_object_new_boolean(hw_num(w->body, "turnoffdataswitch") == 1));
		json_object_object_add(t, "plan", plan);
	}
	json_object_object_add(t, "session", s);
	json_object_object_add(t, "month", m);
	json_object_object_add(t, "total", all);
	json_object_object_add(o, "traffic", t);
}

/* a USSD request and its answer: sent, then the status asked until the answer is there (at most ~18 s) */
static void hw_ussd(struct web *w, const char *code, json_object *o)
{
	char body[256], answer[1024];
	long long until = mono_ms() + 18000;
	int rc;

	if (!code || !*code || strlen(code) > 32 || strspn(code, "0123456789*#+") != strlen(code)) {
		json_object_object_add(o, "error", json_object_new_string("the code: digits, *, # and + only"));
		return;
	}
	snprintf(body, sizeof(body), "<?xml version=\"1.0\" encoding=\"UTF-8\"?><request><content>%s</content>"
		 "<codeType>CodeType</codeType><timeout></timeout></request>", code);
	if ((rc = hw_post(w, "/api/ussd/send", body))) {
		char e[80];

		snprintf(e, sizeof(e), rc < 0 ? "the modem's web did not answer" : "the modem's web refused the request (%d)", rc);
		json_object_object_add(o, "error", json_object_new_string(e));
		return;
	}
	while (mono_ms() < until) {
		struct timespec half = { 0, 500000000 };

		nanosleep(&half, NULL);
		/* <result>1</result>: the network has not answered yet */
		if (hw_get(w, "/api/ussd/status") == 0 && hw_num(w->body, "result") == 1)
			continue;
		if (hw_get(w, "/api/ussd/get") == 0 && mm_web_xml_tag(w->body, "content", answer, sizeof(answer)) > 0) {
			xml_unescape(answer);
			json_object_object_add(o, "answer", json_object_new_string(answer));
			explicit_bzero(answer, sizeof(answer));
			return;
		}
	}
	json_object_object_add(o, "error", json_object_new_string("no answer from the network in time"));
}

/*
 * A HiLink's network mode and bands (the E3372-325 HAR, 2026-10-30): net/net-mode «NetworkMode» 00 auto, 01 2G only,
 * 02 3G only, 03 4G only; «NetworkBand» (2G/3G) and «LTEBand» hex masks; net/net-mode-list names what this unit
 * allows. Read into "netmode" {mode, lte_band, band, modes [..], lte_bands [{name, value}]}.
 */
static bool hexmask(const char *v)
{
	return v && *v && strlen(v) <= 16 && strspn(v, "0123456789abcdefABCDEF") == strlen(v);
}

static void hw_netmode(struct web *w, json_object *o)
{
	json_object *nm = json_object_new_object(), *modes = json_object_new_array(), *bands = json_object_new_array();
	char v[64];
	const char *p, *e;

	if (hw_get(w, "/api/net/net-mode") == 0) {
		if (mm_web_xml_tag(w->body, "NetworkMode", v, sizeof(v)) > 0 && strspn(v, "0123456789") == strlen(v))
			put_str(nm, "mode", v);
		if (mm_web_xml_tag(w->body, "LTEBand", v, sizeof(v)) > 0 && hexmask(v))
			put_str(nm, "lte_band", v);
		if (mm_web_xml_tag(w->body, "NetworkBand", v, sizeof(v)) > 0 && hexmask(v))
			put_str(nm, "band", v);
	}
	if (hw_get(w, "/api/net/net-mode-list") == 0) {
		for (p = strstr(w->body, "<Access>"); p && (e = strstr(p, "</Access>")); p = strstr(e, "<Access>")) {
			snprintf(v, sizeof(v), "%.*s", (int)(e - p - 8 < 8 ? e - p - 8 : 8), p + 8);
			if (*v && strspn(v, "0123456789") == strlen(v))
				json_object_array_add(modes, json_object_new_string(v));
		}
		for (p = strstr(w->body, "<LTEBand>"); p && (e = strstr(p, "</LTEBand>")); p = strstr(e, "<LTEBand>")) {
			char item[512], name[256], val[32];

			snprintf(item, sizeof(item), "%.*s", (int)(e - p < 511 ? e - p : 511), p);
			if (mm_web_xml_tag(item, "Name", name, sizeof(name)) > 0 && mm_web_xml_tag(item, "Value", val, sizeof(val)) > 0 &&
			    hexmask(val)) {
				json_object *b = json_object_new_object();

				xml_unescape(name);
				json_object_object_add(b, "name", json_object_new_string(name));
				json_object_object_add(b, "value", json_object_new_string(val));
				json_object_array_add(bands, b);
			}
		}
	}
	json_object_object_add(nm, "modes", modes);
	json_object_object_add(nm, "lte_bands", bands);
	json_object_object_add(o, "netmode", nm);
}

/* code «MODE,LTEHEX» (an empty mask keeps the current one); the 2G/3G mask stays as the modem has it */
static void hw_netmode_set(struct web *w, const char *code, json_object *o)
{
	char mode[8] = "", lte[24] = "", cur_lte[24] = "", cur_band[24] = "", req[512];
	const char *comma = code ? strchr(code, ',') : NULL;
	int rc;

	if (comma && comma - code < (int)sizeof(mode)) {
		snprintf(mode, sizeof(mode), "%.*s", (int)(comma - code), code);
		snprintf(lte, sizeof(lte), "%s", comma + 1);
	}
	/* 00 auto, 01 2G, 02 3G, 03 4G only; the preferred orders the modded webs offer (the owner, 2026-11-01): 0302
	 * LTE then UMTS, 0301 LTE then GSM, 0201 UMTS then GSM */
	if (!(strlen(mode) == 2 && strspn(mode, "0123") == 2 && mode[0] == '0') && strcmp(mode, "0302") && strcmp(mode, "0301") &&
	    strcmp(mode, "0201")) {
		json_object_object_add(o, "error", json_object_new_string("a mode 00 to 03, 0302, 0301 or 0201 and a hex LTE mask"));
		return;
	}
	if (*lte && !hexmask(lte)) {
		json_object_object_add(o, "error", json_object_new_string("a mode 00 to 03, 0302, 0301 or 0201 and a hex LTE mask"));
		return;
	}
	if (hw_get(w, "/api/net/net-mode") != 0) {
		json_object_object_add(o, "error", json_object_new_string("the modem's web did not give its mode"));
		return;
	}
	if (mm_web_xml_tag(w->body, "LTEBand", cur_lte, sizeof(cur_lte)) <= 0 || !hexmask(cur_lte))
		*cur_lte = '\0';
	if (mm_web_xml_tag(w->body, "NetworkBand", cur_band, sizeof(cur_band)) <= 0 || !hexmask(cur_band))
		snprintf(cur_band, sizeof(cur_band), "3FFFFFFF");
	snprintf(req, sizeof(req), "<?xml version=\"1.0\" encoding=\"UTF-8\"?><request><NetworkMode>%s</NetworkMode>"
		 "<NetworkBand>%s</NetworkBand><LTEBand>%s</LTEBand></request>", mode, cur_band, *lte ? lte : *cur_lte ? cur_lte : "7FFFFFFFFFFFFFFF");
	rc = hw_post(w, "/api/net/net-mode", req);
	json_object_object_add(o, "ok", json_object_new_boolean(rc == 0 && strstr(w->body, "<response>OK</response>")));
	if (rc)
		json_object_object_add(o, "error", json_object_new_string(rc < 0 ? "the modem's web did not answer" :
									"the modem's web refused the mode"));
}

/* ---- a «Mobile.Router» web's actions (MF150-4): form posts, no login asked ---- */

static const char *FORM = "X-Requested-With: XMLHttpRequest\r\nContent-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n";

/* name=value&… with the value percent-encoded; false when it does not fit */
static bool form_add(char *out, size_t size, const char *name, const char *value)
{
	size_t n = strlen(out);

	if (n + strlen(name) + 2 >= size)
		return false;
	n += (size_t)snprintf(out + n, size - n, "%s%s=", n ? "&" : "", name);
	for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; p++) {
		if (n + 4 >= size)
			return false;
		if (isalnum(*p) || strchr("-_.~", *p))
			out[n++] = (char)*p;
		else
			n += (size_t)snprintf(out + n, size - n, "%%%02X", *p);
	}
	out[n] = '\0';
	return true;
}

/* «162.58 KBytes» in bytes; -1 when not a size */
static long long mark_bytes(const char *v)
{
	static const struct { const char *unit; double mul; } u[] = {
		{ "GBytes", 1e9 }, { "MBytes", 1e6 }, { "KBytes", 1e3 }, { "Bytes", 1 } };
	double d;

	if (!num(v, &d))
		return -1;
	for (size_t i = 0; i < sizeof(u) / sizeof(u[0]); i++)
		if (strstr(v, u[i].unit))
			return (long long)(d * u[i].mul + 0.5);
	return -1;
}

/* «hh:mm:ss» in seconds */
static long long mark_secs(const char *v)
{
	int h, m, sec;

	return sscanf(v, "%d:%d:%d", &h, &m, &sec) == 3 ? (long long)h * 3600 + m * 60 + sec : -1;
}

static void mark_put(json_object *o, const char *key, long long v)
{
	if (v >= 0)
		json_object_object_add(o, key, json_object_new_int64(v));
}

/* a private a.b.c.1 for the stick's own address: its DHCP hands out .100 to .200 (the page does the same) */
static bool mark_subnet_ok(const char *ip)
{
	struct in_addr in;
	unsigned a;

	if (!ip || inet_pton(AF_INET, ip, &in) != 1)
		return false;
	a = ntohl(in.s_addr);
	return (a & 0xff) == 1 && ((a >> 16) == 0xc0a8 || (a >> 24) == 10 || (a >> 20) == 0xac1);
}

/* the stick's address changed through its system settings: the form posted back as the web filled it, with the login
 * the web gave (the owner, 2026-10-30: «мы будем это использовать»), never written anywhere */
static void mark_subnet(struct web *w, const char *ip, json_object *o)
{
	char form[2048] = "", user[96], pwd[96], v[96], start[24], end[24], now[32];
	static const char *const keep[][2] = { { "timeout", "login_timeout" }, { "save", "save" }, { "timezone", "timezone" },
					       { "daylight", "daylight" } };
	time_t t = time(NULL);
	struct tm tm;
	bool ok = true;
	size_t i;
	int rc;

	if (!mark_subnet_ok(ip)) {
		json_object_object_add(o, "error", json_object_new_string("a private address ending in .1"));
		return;
	}
	if (http(w, "GET", "/mark_set_sys.w.xml", NULL, NULL) != 200 || mm_web_xml_tag(w->body, "login_username", user, sizeof(user)) <= 0 ||
	    mm_web_xml_tag(w->body, "login_pwd", pwd, sizeof(pwd)) <= 0) {
		json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web refused its settings" :
									    "the modem's web does not answer"));
		return;
	}
	xml_unescape(user);	/* the page's jQuery .text() gives the plain text */
	xml_unescape(pwd);
	ok = form_add(form, sizeof(form), "username", user) && form_add(form, sizeof(form), "password", pwd);
	for (i = 0; ok && i < sizeof(keep) / sizeof(keep[0]); i++)
		ok = mm_web_xml_tag(w->body, keep[i][1], v, sizeof(v)) >= 0 && form_add(form, sizeof(form), keep[i][0], v);
	localtime_r(&t, &tm);
	strftime(now, sizeof(now), "%Y-%m-%d %H:%M:%S", &tm);
	ok = ok && form_add(form, sizeof(form), "time", now);
	ok = ok && mm_web_xml_tag(w->body, "ntp_enabled", v, sizeof(v)) >= 0 && form_add(form, sizeof(form), "ntp_enabled", v);
	ok = ok && mm_web_xml_tag(w->body, "server_id", v, sizeof(v)) >= 0 && form_add(form, sizeof(form), "ntp_id", v);
	ok = ok && mm_web_xml_tag(w->body, "server_ip1", v, sizeof(v)) >= 0 && form_add(form, sizeof(form), "ip1", v);
	ok = ok && mm_web_xml_tag(w->body, "server_ip2", v, sizeof(v)) >= 0 &&
	     form_add(form, sizeof(form), "ip2", strcmp(v, "-") ? v : "");
	snprintf(start, sizeof(start), "%.*s100", (int)(strrchr(ip, '.') - ip + 1), ip);
	snprintf(end, sizeof(end), "%.*s200", (int)(strrchr(ip, '.') - ip + 1), ip);
	ok = ok && form_add(form, sizeof(form), "srv_ip", ip) && form_add(form, sizeof(form), "submask", "255.255.255.0") &&
	     form_add(form, sizeof(form), "enable", "1") && form_add(form, sizeof(form), "start", start) &&
	     form_add(form, sizeof(form), "end", end) && form_add(form, sizeof(form), "lease_time", "86400");
	explicit_bzero(user, sizeof(user));
	explicit_bzero(pwd, sizeof(pwd));
	explicit_bzero(w->body, sizeof(w->body));
	if (!ok) {
		explicit_bzero(form, sizeof(form));
		json_object_object_add(o, "error", json_object_new_string("the modem's settings did not read as expected"));
		return;
	}
	rc = http(w, "POST", "/wxml/set_sys_dhcp.xml", FORM, form);
	explicit_bzero(form, sizeof(form));
	/* <user>1</user><pwd>1</pwd><timeout>1</timeout>: applied (the page says «Настройки применены») */
	ok = rc == 200 && mm_web_xml_tag(w->body, "user", v, sizeof(v)) > 0 && atoi(v) &&
	     mm_web_xml_tag(w->body, "pwd", v, sizeof(v)) > 0 && atoi(v);
	json_object_object_add(o, "ok", json_object_new_boolean(ok));
	if (ok)
		json_object_object_add(o, "address", json_object_new_string(ip));
	else
		json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web refused the address" :
									    "the modem's web did not answer (it may be restarting its network)"));
}

/* MD5 (RFC 1321), only for the «Mobile.Router» login's hash of the web's own random and its default password */
static void md5_block(uint32_t h[4], const unsigned char *p)
{
	static const uint32_t k[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
	static const unsigned char r[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20,
		5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21,
		6, 10, 15, 21, 6, 10, 15, 21 };
	uint32_t m[16], a = h[0], b = h[1], c = h[2], d = h[3];
	int i;

	for (i = 0; i < 16; i++)
		m[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
	for (i = 0; i < 64; i++) {
		uint32_t f, t;
		int g;

		if (i < 16) { f = (b & c) | (~b & d); g = i; }
		else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
		else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
		else { f = c ^ (b | ~d); g = (7 * i) % 16; }
		t = d;
		d = c;
		c = b;
		f += a + k[i] + m[g];
		b += f << r[i] | f >> (32 - r[i]);
		a = t;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
}

void mm_md5_hex(const char *s, size_t n, char out[33])
{
	uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
	unsigned char block[64];
	uint64_t bits = (uint64_t)n * 8;
	size_t i = 0, rest;

	for (; n - i >= 64; i += 64)
		md5_block(h, (const unsigned char *)s + i);
	rest = n - i;
	memset(block, 0, sizeof(block));
	memcpy(block, s + i, rest);
	block[rest] = 0x80;
	if (rest >= 56) {
		md5_block(h, block);
		memset(block, 0, sizeof(block));
	}
	for (i = 0; i < 8; i++)
		block[56 + i] = (unsigned char)(bits >> (8 * i));
	md5_block(h, block);
	for (i = 0; i < 16; i++)
		snprintf(out + i * 2, 3, "%02x", (unsigned)(h[i / 4] >> (8 * (i % 4))) & 0xff);
}

/*
 * The «Mobile.Router» login (the owner's MF150-4 on the router, 2026-10-30: logged out, mark_home.w.xml answers every
 * tag empty; his PC was logged in by the page itself): the page asks mark_lang.w.xml for <rand> and posts
 * /wxml/post_login.xml Name=admin, password=md5(rand[0:3] + "admin" + rand[3:8]), rand. Logged in, mark_title's
 * <login> is 2 or more. The page's own login uses «admin» whatever the web password, so the default is tried first,
 * then the password the web itself gives (the owner: «мы будем это использовать»).
 */
static bool mark_logged_in(struct web *w)
{
	char v[16];

	return http(w, "GET", "/mark_title.w.xml", NULL, NULL) == 200 && mm_web_xml_tag(w->body, "login", v, sizeof(v)) > 0 && atoi(v) >= 2;
}

/* a web that asks no login at all answers its page logged out (the owner's M150-4, 2026-11-02: the page opens with
 * no password, and the engine, waiting for a login that never takes, read nothing): its signal there is the proof */
static bool mark_open(struct web *w)
{
	char v[160];

	return http(w, "GET", "/mark_home.w.xml", NULL, NULL) == 200 && mm_web_xml_tag(w->body, "rssi_info", v, sizeof(v)) > 0 &&
	       strcmp(v, "-1") && strspn(v, ",") != strlen(v);
}

static bool mark_login_with(struct web *w, const char *pw)
{
	char rand[64] = "", mix[160], hash[33], form[256] = "", v[16];
	size_t rl, pre;

	/* an empty <rand> is taken as it is, as the page takes it (the owner's MegaFon M150-4, 2026-11-03: its
	 * mark_lang answers <rand></rand>, the page posts md5("admin") and an empty rand; the engine gave up) */
	if (http(w, "GET", "/mark_lang.w.xml", NULL, NULL) != 200 || mm_web_xml_tag(w->body, "rand", rand, sizeof(rand)) < 0)
		return false;
	rl = strlen(rand);
	pre = rl < 3 ? rl : 3;
	snprintf(mix, sizeof(mix), "%.*s%s%.5s", (int)pre, rand, pw, rl > 3 ? rand + 3 : "");
	for (char *c = mix + pre; *c && c < mix + pre + strlen(pw); c++)
		*c = (char)tolower((unsigned char)*c);	/* the page lower-cases the password */
	mm_md5_hex(mix, strlen(mix), hash);
	explicit_bzero(mix, sizeof(mix));
	if (!form_add(form, sizeof(form), "Name", "admin") || !form_add(form, sizeof(form), "password", hash) ||
	    !form_add(form, sizeof(form), "rand", rand) || http(w, "POST", "/wxml/post_login.xml", FORM, form) != 200) {
		explicit_bzero(form, sizeof(form));
		return false;
	}
	explicit_bzero(form, sizeof(form));
	/* <login_check> 4 or 7 as the page reads it; mark_title says for sure */
	(void)(mm_web_xml_tag(w->body, "login_check", v, sizeof(v)));
	return mark_logged_in(w);
}

/* the password set for the modem in MedveModem first (it was never tried: only "admin" and the web's own login_pwd
 * were), then "admin", then none, then the one the web keeps in its settings (the owner's M150-4, 2026-11-02:
 * «Веб модема просит пароль» on a stick with no password set) */
static bool mark_login(struct web *w, const char *chosen)
{
	char user[96], pwd[96];
	bool ok;

	if (mark_logged_in(w))
		return true;
	/* the password set in MedveModem first; a wrong one falls through to the stick's own (the owner's M150-4, r144:
	 * «mark login: wrong» every 5 min while the page opened with no password) */
	if (chosen && *chosen && strcmp(chosen, "admin") && mark_login_with(w, chosen))
		return true;
	/* admin/admin, then admin with no password (the owner: «пароля нет, пусть сперва admin/admin или admin без пароля») */
	if (mark_login_with(w, "admin") || mark_login_with(w, ""))
		return true;
	if (http(w, "GET", "/mark_set_sys.w.xml", NULL, NULL) != 200 || mm_web_xml_tag(w->body, "login_pwd", pwd, sizeof(pwd)) <= 0 ||
	    mm_web_xml_tag(w->body, "login_username", user, sizeof(user)) < 0) {
		explicit_bzero(w->body, sizeof(w->body));
		return false;
	}
	explicit_bzero(w->body, sizeof(w->body));
	xml_unescape(pwd);
	ok = strcmp(pwd, "admin") && mark_login_with(w, pwd);
	explicit_bzero(pwd, sizeof(pwd));
	explicit_bzero(user, sizeof(user));
	return ok;
}

/* a tag's text with a CDATA wrapper taken off; its length or -1 */
static int mark_tag(const char *xml, const char *tag, char *out, size_t size)
{
	int n = mm_web_xml_tag(xml, tag, out, size);

	if (n > 12 && !strncmp(out, "<![CDATA[", 9) && !strcmp(out + n - 3, "]]>")) {
		memmove(out, out + 9, (size_t)n - 12);
		out[n - 12] = '\0';
		return n - 12;
	}
	return n;
}

/* «1,<text>»: the page shows what follows the first comma */
static const char *mark_after_comma(const char *v)
{
	const char *c = strchr(v, ',');

	return c && c - v <= 3 ? c + 1 : v;
}

/*
 * SMS of a «Mobile.Router» web (the owner's HAR 2026-10-30): /wxml/sms_box.xml href=<box>&page=<n> answers 20 at a
 * time as a flat run of sms_id, sms_src «1,<sender>» (a leading 0: none), contact_name, sms_body CDATA «1,<text>»,
 * sms_time «dd-mm-yyyy hh:mm:ss», sms_status (1 unread); sms_count «unread,inbox,outbox,drafts». Box 0 the inbox,
 * 1 the sent ones (their ids served + 100000 so a delete knows the box). Never sms_show.xml: it marks a message read
 * (the bot does not).
 */
static void mark_sms_box(struct web *w, int box, int count, json_object *msgs)
{
	for (int page = 0; page * 20 < count && page < 10; page++) {
		char form[64], *p, *next;

		snprintf(form, sizeof(form), "href=%d&page=%d", box, page);
		if (http(w, "POST", "/wxml/sms_box.xml", FORM, form) != 200)
			return;
		for (p = strstr(w->body, "<sms_id>"); p; p = next) {
			char id[16] = "", src[96] = "", when[40] = "", stat[8] = "", *text, *block;
			size_t len;
			struct tm tm;
			json_object *m;

			next = strstr(p + 8, "<sms_id>");
			len = next ? (size_t)(next - p) : strlen(p);
			if (!(block = strndup(p, len)))
				return;
			text = calloc(1, len + 1);
			mm_web_xml_tag(block, "sms_id", id, sizeof(id));
			mark_tag(block, "sms_src", src, sizeof(src));
			mm_web_xml_tag(block, "sms_time", when, sizeof(when));
			mm_web_xml_tag(block, "sms_status", stat, sizeof(stat));
			if (text && *id && strspn(id, "0123456789") == strlen(id) && mark_tag(block, "sms_body", text, len + 1) >= 0) {
				const char *num = src[0] == '0' ? "" : mark_after_comma(src);

				xml_unescape(text);
				m = json_object_new_object();
				json_object_object_add(m, "id", json_object_new_int(atoi(id) + (box ? 100000 : 0)));
				json_object_object_add(m, "number", json_object_new_string(num));
				json_object_object_add(m, "text", json_object_new_string(mark_after_comma(text)));
				json_object_object_add(m, "state", json_object_new_string(box ? "sent" : "received"));
				json_object_object_add(m, "type", json_object_new_string(box ? "submit" : "deliver"));
				json_object_object_add(m, "storage", json_object_new_string("web"));
				if (!box)
					json_object_object_add(m, "unread", json_object_new_boolean(!strcmp(stat, "1")));
				memset(&tm, 0, sizeof(tm));
				if (sscanf(when, "%d-%d-%d %d:%d:%d", &tm.tm_mday, &tm.tm_mon, &tm.tm_year, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
					char ts[40];
					time_t t;

					tm.tm_year -= 1900;
					tm.tm_mon -= 1;
					tm.tm_isdst = -1;
					t = mktime(&tm);
					strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", localtime(&t));
					json_object_object_add(m, "timestamp", json_object_new_string(ts));
					json_object_object_add(m, "epoch", json_object_new_int64((long long)t));
				}
				json_object_array_add(msgs, m);
			}
			if (text) {
				explicit_bzero(text, len + 1);
				free(text);
			}
			explicit_bzero(block, len);
			free(block);
		}
		explicit_bzero(w->body, sizeof(w->body));
	}
}

static void mark_sms(struct web *w, const char *op, const char *number, const char *text, int index, json_object *o)
{
	char v[64];
	int counts[4] = { 0, 0, 0, 0 };

	if (!strcmp(op, "list")) {
		json_object *msgs = json_object_new_array();

		/* the counts first: page 0 of the inbox carries them */
		if (http(w, "POST", "/wxml/sms_box.xml", FORM, "href=0&page=0") != 200 || mm_web_xml_tag(w->body, "sms_count", v, sizeof(v)) <= 0) {
			json_object_put(msgs);
			json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web has no SMS" :
										    "the modem's web does not answer"));
			return;
		}
		explicit_bzero(w->body, sizeof(w->body));
		sscanf(v, "%d,%d,%d,%d", &counts[0], &counts[1], &counts[2], &counts[3]);
		mark_sms_box(w, 0, counts[1], msgs);
		mark_sms_box(w, 1, counts[2], msgs);
		json_object_object_add(o, "messages", msgs);
	} else if (!strcmp(op, "send") && number && text) {
		size_t need = strlen(text) * 3 + strlen(number) * 3 + 96;
		char *form = calloc(1, need), when[16];
		time_t now = time(NULL);
		struct tm tm;
		bool ok;

		if (!form) {
			json_object_object_add(o, "error", json_object_new_string("out of memory"));
			return;
		}
		localtime_r(&now, &tm);
		strftime(when, sizeof(when), "%y%m%d%H%M%S", &tm);
		ok = form_add(form, need, "d_number", number) && form_add(form, need, "d_content", text) &&
		     form_add(form, need, "d_time", when) && http(w, "POST", "/wxml/sms_send.xml", FORM, form) == 200;
		explicit_bzero(form, need);
		free(form);
		/* <submit>1</submit>: sent; -1 the web could not (the owner's test to a short number) */
		ok = ok && mm_web_xml_tag(w->body, "submit", v, sizeof(v)) > 0 && atoi(v) == 1;
		json_object_object_add(o, "ok", json_object_new_boolean(ok));
		if (!ok)
			json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web could not send it" :
										    "the modem's web does not answer"));
	} else if (!strcmp(op, "delete") && index >= 0) {
		char form[48];

		snprintf(form, sizeof(form), "href=%d&no20=%d", index >= 100000 ? 1 : 0, index % 100000);
		json_object_object_add(o, "ok", json_object_new_boolean(http(w, "POST", "/wxml/sms_del.xml", FORM, form) == 200));
	} else {
		json_object_object_add(o, "error", json_object_new_string("list, send or delete"));
	}
}

/* USSD (ussd.html): the code sent, then ussd_res.w.xml asked until its «res» is not «-» (at most ~20 s); the session
 * closed after the answer. The answer is never logged */
static void mark_ussd(struct web *w, const char *code, json_object *o)
{
	char form[96] = "", answer[1024];
	long long until = mono_ms() + 20000;
	const struct timespec half = { 0, 500000000 };

	if (!code || !*code || strspn(code, "0123456789*#+") != strlen(code) || strlen(code) > 32) {
		json_object_object_add(o, "error", json_object_new_string("a USSD code is digits, * and # only"));
		return;
	}
	form_add(form, sizeof(form), "send", code);
	if (http(w, "POST", "/wxml/ussd_send.xml", FORM, form) != 200) {
		json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web refused the code" :
									    "the modem's web does not answer"));
		return;
	}
	while (mono_ms() < until) {
		nanosleep(&half, NULL);
		if (http(w, "GET", "/ussd_res.w.xml", NULL, NULL) == 200 && mark_tag(w->body, "res", answer, sizeof(answer)) > 0 &&
		    strcmp(answer, "-")) {
			xml_unescape(answer);
			json_object_object_add(o, "answer", json_object_new_string(answer));
			explicit_bzero(answer, sizeof(answer));
			explicit_bzero(w->body, sizeof(w->body));
			http(w, "POST", "/wxml/ussd_cancel.xml", FORM, "cancel=1");
			return;
		}
	}
	http(w, "POST", "/wxml/ussd_cancel.xml", FORM, "cancel=1");
	json_object_object_add(o, "error", json_object_new_string("no answer from the network in time"));
}

static void mark_act(struct web *w, const char *op, const char *code, json_object *o)
{
	char v[96];

	if (!strcmp(op, "info")) {
		json_object *t = json_object_new_object(), *s = json_object_new_object(), *all = json_object_new_object(),
			    *dev = json_object_new_object();

		if (http(w, "GET", "/mark_home.w.xml", NULL, NULL) == 200)
			act_text(o, "wan_ip", w->body, "wan");
		if (http(w, "GET", "/mark_conn.w.xml", NULL, NULL) == 200) {
			if (mm_web_xml_tag(w->body, "rx", v, sizeof(v)) > 0) mark_put(s, "rx", mark_bytes(v));
			if (mm_web_xml_tag(w->body, "tx", v, sizeof(v)) > 0) mark_put(s, "tx", mark_bytes(v));
			if (mm_web_xml_tag(w->body, "active_time", v, sizeof(v)) > 0) mark_put(s, "s", mark_secs(v));
			if (mm_web_xml_tag(w->body, "t_rx", v, sizeof(v)) > 0) mark_put(all, "rx", mark_bytes(v));
			if (mm_web_xml_tag(w->body, "t_tx", v, sizeof(v)) > 0) mark_put(all, "tx", mark_bytes(v));
			if (mm_web_xml_tag(w->body, "t_active_time", v, sizeof(v)) > 0) mark_put(all, "s", mark_secs(v));
		}
		json_object_object_add(t, "session", s);
		json_object_object_add(t, "total", all);
		json_object_object_add(o, "traffic", t);
		if (http(w, "GET", "/mark_about.w.xml", NULL, NULL) == 200) {
			act_text(dev, "firmware", w->body, "swver");
			act_text(dev, "hardware", w->body, "hwver");
			act_text(dev, "webui", w->body, "uiver");
			explicit_bzero(w->body, sizeof(w->body));	/* it carries the IMEI and the IMSI too */
		}
		json_object_object_add(o, "device", dev);
	} else if (!strcmp(op, "reboot")) {
		/* «reset=1» restarts the stick; «restore=1» is the factory reset and is never sent */
		int rc = http(w, "POST", "/wxml/set_restore.xml", FORM, "reset=1");

		json_object_object_add(o, "ok", json_object_new_boolean(rc == 200));
		if (rc != 200)
			json_object_object_add(o, "error", json_object_new_string(rc < 0 ? "the modem's web did not answer" :
										"the modem's web refused it"));
	} else if (!strcmp(op, "subnet")) {
		mark_subnet(w, code, o);
	} else if (!strcmp(op, "ussd")) {
		mark_ussd(w, code, o);
	} else {
		json_object_object_add(o, "error", json_object_new_string("info, ussd, reboot or subnet"));
	}
}

/*
 * A ZTE web's actions (2026-11-03, the owner: «по ZTE донглам надо бы пройтись»): what a HiLink's web gives (r117)
 * where a ZTE goform has it. info: the operator's address, the traffic (realtime_* the session, monthly_* the month),
 * the model and firmware, the network mode (net_select); reconnect: DISCONNECT_NETWORK then CONNECT_NETWORK; netmode
 * (00 auto, 01 2G, 02 3G, 03 4G): SET_BEARER_PREFERENCE, with the stick's own net_priority after a line break where it
 * keeps one; reboot: REBOOT_DEVICE (the factory reset never); ussd:
 * USSD_PROCESS ussd_send, ussd_write_flag asked until 16 (the answer in ussd_data_info, UCS-2 hex) at most ~20 s, then
 * ussd_cancel, as the stick's own page does. The IMEI, IMSI and the phone number are never asked. Not on our bench.
 */
static const char *const zte_modes[][2] = {
	{ "00", "NETWORK_auto" }, { "01", "Only_GSM" }, { "02", "Only_WCDMA" }, { "03", "Only_LTE" },
};

static void zte_put_bytes(json_object *o, const char *key, const char *v)
{
	if (v && *v && strspn(v, "0123456789") == strlen(v) && strlen(v) < 20)
		json_object_object_add(o, key, json_object_new_int64(strtoll(v, NULL, 10)));
}

static void zte_ussd(struct web *w, const char *code, json_object *o)
{
	char form[240], num[112] = "";
	long long until = mono_ms() + 20000;
	const struct timespec half = { 0, 500000000 };
	json_object *z;

	if (!code || !*code || strspn(code, "0123456789*#+") != strlen(code) || strlen(code) > 32) {
		json_object_object_add(o, "error", json_object_new_string("a USSD code is digits, * and # only"));
		return;
	}
	for (const char *p = code; *p && strlen(num) + 4 < sizeof(num); p++)	/* form-encoded */
		snprintf(num + strlen(num), sizeof(num) - strlen(num), *p == '#' ? "%%23" : *p == '+' ? "%%2B" : *p == '*' ? "%%2A" : "%c", *p);
	snprintf(form, sizeof(form), "isTest=false&goformId=USSD_PROCESS&USSD_operator=ussd_send&USSD_send_number=%s&notCallback=true", num);
	if (!zte_set(w, form)) {
		json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web refused the code" :
									    "the modem's web does not answer"));
		return;
	}
	while (mono_ms() < until) {
		const char *flag;

		nanosleep(&half, NULL);
		if (!(z = zte_get(w, "ussd_write_flag", false)))
			continue;
		flag = zs(z, "ussd_write_flag");
		if (flag && !strcmp(flag, "16")) {
			json_object *d;

			json_object_put(z);
			if ((d = zte_get(w, "ussd_data_info", false))) {
				json_object *info = json_object_object_get(d, "ussd_data_info");
				const char *data = zs(info ? info : d, "ussd_data");
				size_t len = data ? strlen(data) + 8 : 0;
				char *text = len ? malloc(len) : NULL;

				if (text) {
					zte_ucs2(data, false, text, len);
					json_object_object_add(o, "answer", json_object_new_string(text));
					explicit_bzero(text, len);
					free(text);
				}
				json_object_put(d);
				explicit_bzero(w->body, sizeof(w->body));
			}
			zte_set(w, "isTest=false&goformId=USSD_PROCESS&USSD_operator=ussd_cancel&notCallback=true");
			if (!json_object_object_get(o, "answer"))
				json_object_object_add(o, "error", json_object_new_string("the modem's web gave no answer text"));
			return;
		}
		/* 15 (or nothing yet): the network still answering; anything else is the web's refusal */
		if (flag && *flag && strcmp(flag, "15") && strcmp(flag, "0")) {
			char why[64];

			snprintf(why, sizeof(why), "the network did not answer (code %.8s)", flag);
			json_object_put(z);
			json_object_object_add(o, "error", json_object_new_string(why));
			zte_set(w, "isTest=false&goformId=USSD_PROCESS&USSD_operator=ussd_cancel&notCallback=true");
			return;
		}
		json_object_put(z);
	}
	zte_set(w, "isTest=false&goformId=USSD_PROCESS&USSD_operator=ussd_cancel&notCallback=true");
	json_object_object_add(o, "error", json_object_new_string("no answer from the network in time"));
}

static void zte_act(struct web *w, const char *op, const char *code, json_object *o)
{
	if (!strcmp(op, "info")) {
		json_object *z = zte_get(w, "wan_ipaddr,ipv6_wan_ipaddr,realtime_tx_bytes,realtime_rx_bytes,realtime_time,"
					    "monthly_tx_bytes,monthly_rx_bytes,monthly_time,model_name,wa_inner_version,hardware_version,"
					    "net_select", true);
		json_object *t, *s, *m, *dev, *nm, *modes;
		const char *v;

		if (!z) {
			json_object_object_add(o, "error", json_object_new_string("the modem's web does not answer"));
			return;
		}
		if ((v = zs(z, "wan_ipaddr")) && *v && strlen(v) < 40 && !strpbrk(v, "<>&\""))
			json_object_object_add(o, "wan_ip", json_object_new_string(v));
		if ((v = zs(z, "ipv6_wan_ipaddr")) && *v && strlen(v) < 64 && !strpbrk(v, "<>&\""))
			json_object_object_add(o, "wan_ipv6", json_object_new_string(v));
		t = json_object_new_object();
		s = json_object_new_object();
		m = json_object_new_object();
		zte_put_bytes(s, "rx", zs(z, "realtime_rx_bytes"));
		zte_put_bytes(s, "tx", zs(z, "realtime_tx_bytes"));
		zte_put_bytes(s, "s", zs(z, "realtime_time"));
		zte_put_bytes(m, "rx", zs(z, "monthly_rx_bytes"));
		zte_put_bytes(m, "tx", zs(z, "monthly_tx_bytes"));
		zte_put_bytes(m, "s", zs(z, "monthly_time"));
		json_object_object_add(t, "session", s);
		json_object_object_add(t, "month", m);
		json_object_object_add(o, "traffic", t);
		dev = json_object_new_object();
		if ((v = zs(z, "model_name")) && *v && strlen(v) < 64 && !strpbrk(v, "<>&\""))
			json_object_object_add(dev, "name", json_object_new_string(v));
		if ((v = zs(z, "wa_inner_version")) && *v && strlen(v) < 96 && !strpbrk(v, "<>&\""))
			json_object_object_add(dev, "firmware", json_object_new_string(v));
		if ((v = zs(z, "hardware_version")) && *v && strlen(v) < 64 && !strpbrk(v, "<>&\""))
			json_object_object_add(dev, "hardware", json_object_new_string(v));
		json_object_object_add(o, "device", dev);
		nm = json_object_new_object();
		modes = json_object_new_array();
		for (size_t i = 0; i < sizeof(zte_modes) / sizeof(zte_modes[0]); i++) {
			json_object_array_add(modes, json_object_new_string(zte_modes[i][0]));
			if ((v = zs(z, "net_select")) && !strcmp(v, zte_modes[i][1]))
				put_str(nm, "mode", zte_modes[i][0]);
		}
		json_object_object_add(nm, "modes", modes);
		json_object_object_add(o, "netmode", nm);
		json_object_put(z);
	} else if (!strcmp(op, "reconnect")) {
		bool ok = zte_set(w, "isTest=false&goformId=DISCONNECT_NETWORK&notCallback=true");
		const struct timespec two = { 2, 0 };

		nanosleep(&two, NULL);
		ok = zte_set(w, "isTest=false&goformId=CONNECT_NETWORK&notCallback=true") && ok;
		json_object_object_add(o, "ok", json_object_new_boolean(ok));
		if (!ok)
			json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web refused it" :
										"the modem's web did not answer"));
	} else if (!strcmp(op, "netmode")) {
		const char *mode = NULL;
		char form[200], prio[48] = "";
		json_object *z;

		for (size_t i = 0; code && i < sizeof(zte_modes) / sizeof(zte_modes[0]); i++)
			if (!strncmp(code, zte_modes[i][0], 2) && (code[2] == '\0' || code[2] == ','))
				mode = zte_modes[i][1];
		if (!mode) {
			json_object_object_add(o, "error", json_object_new_string("a mode 00 to 03 (a ZTE web takes no band mask)"));
			return;
		}
		if ((z = zte_get(w, "net_priority", false))) {
			const char *p = zs(z, "net_priority");

			if (p && *p && strlen(p) < 32 && strspn(p, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_") == strlen(p))
				snprintf(prio, sizeof(prio), "%%0A%s", p);
			json_object_put(z);
		}
		snprintf(form, sizeof(form), "isTest=false&goformId=SET_BEARER_PREFERENCE&BearerPreference=%s%s", mode, prio);
		json_object_object_add(o, "ok", json_object_new_boolean(zte_set(w, form)));
		if (!json_object_get_boolean(json_object_object_get(o, "ok")))
			json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web refused the mode" :
										"the modem's web did not answer"));
	} else if (!strcmp(op, "reboot")) {
		bool ok = zte_set(w, "isTest=false&goformId=REBOOT_DEVICE");

		json_object_object_add(o, "ok", json_object_new_boolean(ok));
		if (!ok)
			json_object_object_add(o, "error", json_object_new_string(w->code ? "the modem's web refused it" :
										"the modem's web did not answer"));
	} else if (!strcmp(op, "ussd")) {
		zte_ussd(w, code, o);
	} else {
		json_object_object_add(o, "error", json_object_new_string("info, ussd, reconnect, netmode or reboot"));
	}
}

json_object *mm_web_act(const struct mm_web_args *a, const char *op, const char *code)
{
	static struct web w;
	json_object *o = json_object_new_object();
	const char *pw = a->password && *a->password ? a->password : "admin", *auth = "none";
	bool chosen = a->password && *a->password, tried = false;
	struct state st;
	int wait_s = 0, rc;

	memset(&w, 0, sizeof(w));
	w.a = a;
	state_read(a->state, &st);
	snprintf(w.cookie, sizeof(w.cookie), "%s", st.cookie);
	if (a->kind && !strcmp(a->kind, "mark")) {
		json_object_object_add(o, "kind", json_object_new_string("mark"));
		if (!mark_logged_in(&w) && !mark_open(&w) && !mark_login(&w, chosen ? pw : NULL)) {
			auth = chosen ? "wrong" : "needed";
			goto done;
		}
		mark_act(&w, op, code, o);
		goto done;
	}
	if (a->kind && !strcmp(a->kind, "uz")) {
		json_object *r;

		json_object_object_add(o, "kind", json_object_new_string("uz"));
		if (!strcmp(op, "info")) {
			json_object *t = json_object_new_object(), *s2 = json_object_new_object(), *dev = json_object_new_object();

			if (!(r = uz_ask(&w, "{\"funcNo\":1003}", pw, chosen, &st, &auth, &wait_s))) {
				json_object_put(t);
				json_object_put(s2);
				json_object_put(dev);
				if (!strcmp(auth, "none"))
					json_object_object_add(o, "error", json_object_new_string("the modem's web does not answer"));
				goto done;
			}
			/* KB as the page shows them */
			if (json_object_object_get(r, "down_bytes"))
				json_object_object_add(s2, "rx", json_object_new_int64(json_object_get_int64(json_object_object_get(r, "down_bytes")) * 1024));
			if (json_object_object_get(r, "up_bytes"))
				json_object_object_add(s2, "tx", json_object_new_int64(json_object_get_int64(json_object_object_get(r, "up_bytes")) * 1024));
			json_object_object_add(t, "total", s2);
			json_object_object_add(o, "traffic", t);
			json_object_put(r);
			if ((r = uz_call(&w, "{\"funcNo\":3002}", &(bool){ false }))) {
				const char *v;

				if ((v = json_object_get_string(json_object_object_get(r, "device_name"))) && *v && !strpbrk(v, "<>&\""))
					json_object_object_add(dev, "name", json_object_new_string(v));
				if ((v = json_object_get_string(json_object_object_get(r, "sw_version"))) && *v && !strpbrk(v, "<>&\""))
					json_object_object_add(dev, "firmware", json_object_new_string(v));
				json_object_put(r);
			}
			json_object_object_add(o, "device", dev);
		} else if (!strcmp(op, "reboot")) {
			/* 1013 restarts it; 1014 is the factory reset and is never sent */
			r = uz_ask(&w, "{\"funcNo\":1013}", pw, chosen, &st, &auth, &wait_s);
			json_object_object_add(o, "ok", json_object_new_boolean(r != NULL));
			json_object_put(r);
		} else
			json_object_object_add(o, "error", json_object_new_string("info or reboot"));
		goto done;
	}
	if (a->kind && !strcmp(a->kind, "zte")) {
		json_object *z = zte_get(&w, "loginfo", true);
		bool in = zs(z, "loginfo") && !strcmp(zs(z, "loginfo"), "ok");

		json_object_object_add(o, "kind", json_object_new_string("zte"));
		if (!z) {
			json_object_object_add(o, "error", json_object_new_string("the modem's web does not answer"));
			goto done;
		}
		json_object_put(z);
		if (!in) {
			if (!a->fresh && st.last_login && a->now - st.last_login < MM_WEB_LOGIN_EVERY_S) {
				auth = "waiting";
				wait_s = (int)(MM_WEB_LOGIN_EVERY_S - (a->now - st.last_login));
				goto done;
			}
			st.last_login = a->now;
			tried = true;
			if ((auth = zte_login(&w, pw, chosen, &wait_s)))
				goto done;
			auth = "ok";
		}
		zte_act(&w, op, code, o);
		if (tried)
			st.last_login = 0;
		goto done;
	}
	if (!a->kind || strcmp(a->kind, "huawei")) {
		json_object_object_add(o, "error", json_object_new_string("only a HiLink, ZTE or Mobile.Router web is asked so"));
		goto done;
	}
	json_object_object_add(o, "kind", json_object_new_string("huawei"));
	hw_session(&w);
	rc = hw_get(&w, "/api/monitoring/status");
	if (hw_login_wanted(rc)) {
		if (!a->fresh && st.last_login && a->now - st.last_login < MM_WEB_LOGIN_EVERY_S) {
			auth = "waiting";
			wait_s = (int)(MM_WEB_LOGIN_EVERY_S - (a->now - st.last_login));
			goto done;
		}
		st.last_login = a->now;
		tried = true;
		if ((auth = hw_login(&w, pw, chosen, &wait_s)))
			goto done;
		auth = "ok";
		rc = hw_get(&w, "/api/monitoring/status");
	}
	if (rc) {
		json_object_object_add(o, "error", json_object_new_string(rc < 0 ? "the modem's web does not answer" :
									"the modem's web refused it"));
		goto done;
	}
	if (!strcmp(op, "info")) {
		json_object *dev = json_object_new_object();

		/* the operator's address of the modem; never the IMEI, IMSI, serial or phone number */
		act_text(o, "wan_ip", w.body, "WanIPAddress");
		act_text(o, "wan_ipv6", w.body, "WanIPv6Address");
		hw_traffic(&w, o);
		if (hw_get(&w, "/api/device/information") == 0) {
			act_text(dev, "name", w.body, "DeviceName");
			act_text(dev, "firmware", w.body, "SoftwareVersion");
			act_text(dev, "hardware", w.body, "HardwareVersion");
			act_text(dev, "webui", w.body, "WebUIVersion");
			memset(w.body, 0, sizeof(w.body));	/* it carries the IMEI and the IMSI too */
		}
		json_object_object_add(o, "device", dev);
		hw_netmode(&w, o);
		/* the radio link now: the PUSCH power, the uplink's width, the transmission mode, RRC (the E3372-325 HAR) */
		if (hw_get(&w, "/api/device/signal") == 0) {
			static const char *const keep[] = { "tx_power_dbm", "ul_bandwidth_mhz", "bandwidth_mhz", "transmission_mode", "rrc" };
			json_object *m = mm_web_hilink_metrics(w.body, NULL, NULL), *link = json_object_new_object(), *v;

			for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++)
				if (json_object_object_get_ex(m, keep[i], &v))
					json_object_object_add(link, keep[i], json_object_get(v));
			json_object_put(m);
			json_object_object_add(o, "link", link);
		}
	} else if (!strcmp(op, "ussd")) {
		hw_ussd(&w, code, o);
	} else if (!strcmp(op, "reconnect") || !strcmp(op, "clear-traffic")) {
		/* the mobile session again, the modem not restarted (the ladder's radio step); the traffic counters cleared */
		bool rec = !strcmp(op, "reconnect");

		rc = hw_post(&w, rec ? "/api/net/reconnect" : "/api/monitoring/clear-traffic",
			     rec ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?><request><reconnect_action>1</reconnect_action></request>" :
				   "<?xml version=\"1.0\" encoding=\"UTF-8\"?><request><ClearTraffic>1</ClearTraffic></request>");
		json_object_object_add(o, "ok", json_object_new_boolean(rc == 0 && strstr(w.body, "<response>OK</response>")));
		if (rc)
			json_object_object_add(o, "error", json_object_new_string(rc < 0 ? "the modem's web did not answer" :
										"the modem's web refused it"));
	} else if (!strcmp(op, "netmode")) {
		hw_netmode_set(&w, code, o);
	} else if (!strcmp(op, "reboot")) {
		rc = hw_post(&w, "/api/device/control", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><request><Control>1</Control></request>");
		json_object_object_add(o, "ok", json_object_new_boolean(rc == 0 && strstr(w.body, "<response>OK</response>")));
		if (rc)
			json_object_object_add(o, "error", json_object_new_string(rc < 0 ? "the modem's web did not answer" :
										"the modem's web refused it"));
	} else {
		json_object_object_add(o, "error", json_object_new_string("info, ussd, reconnect, clear-traffic, netmode or reboot"));
	}
	if (tried)
		st.last_login = 0;
done:
	json_object_object_add(o, "auth", json_object_new_string(auth));
	if (wait_s > 0)
		json_object_object_add(o, "wait_s", json_object_new_int(wait_s));
	snprintf(st.cookie, sizeof(st.cookie), "%s", w.cookie);
	state_write(a->state, &st);
	explicit_bzero(&w, sizeof(w));
	return o;
}
