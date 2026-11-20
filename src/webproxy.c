/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * medvemodem-webproxy: the web pages of modems in router mode through the router's own LAN address.
 *
 *   medvemodem-webproxy <listen-address> <port> <host> <netdev|-> [<port> <host> <netdev|->]...
 *
 * A HiLink answers a request whose Host is not its own with 307 to http://192.168.8.1/… (bench 2026-10-26: the
 * router's address and port in Host got «LOCATION: http://192.168.8.1/html/index.html?origin=xxx», its own got
 * 200), so the browser left the router's port for an address its proxy client sends into a tunnel. A NAT rule
 * cannot change a header: this helper relays each connection to <host>:80 with Host, Origin and Referer naming
 * the modem, and turns a Location that names the modem back into the address the browser used. One request per
 * connection (Connection: close both ways): every request's head is rewritten. A child per connection; the
 * parent only accepts. Nothing is logged of what passes.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_SITES 19
#define HEAD_MAX 16384
#define IDLE_MS 60000

struct site {
	int fd, port;
	char host[64], dev[16];
};

static int listen_on(const char *addr, int port)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons((unsigned short)port) };
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0), one = 1;

	if (fd < 0 || inet_pton(AF_INET, addr, &sa.sin_addr) != 1)
		goto fail;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) || listen(fd, 16))
		goto fail;
	return fd;
fail:
	if (fd >= 0)
		close(fd);
	return -1;
}

static int connect_to(const char *host, const char *dev)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(80) };
	struct timeval tv = { .tv_sec = 10 };
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

	if (fd < 0 || inet_pton(AF_INET, host, &sa.sin_addr) != 1)
		goto fail;
	/* the modem's own device: 192.168.8.1 may be another modem's address too */
	if (strcmp(dev, "-") && setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, dev, (socklen_t)strlen(dev) + 1))
		goto fail;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)))
		goto fail;
	return fd;
fail:
	if (fd >= 0)
		close(fd);
	return -1;
}

static bool write_all(int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t w = write(fd, p, n);

		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return false;
		p += w;
		n -= (size_t)w;
	}
	return true;
}

/* a message's head (up to the empty line) into buf; what came after it stays in buf after the head */
static ssize_t read_head(int fd, char *buf, size_t cap, size_t *got)
{
	char *end;

	*got = 0;
	while (*got < cap - 1) {
		struct pollfd p = { .fd = fd, .events = POLLIN };
		ssize_t r;

		if (poll(&p, 1, IDLE_MS) <= 0)
			return -1;
		r = read(fd, buf + *got, cap - 1 - *got);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return -1;
		*got += (size_t)r;
		buf[*got] = 0;
		if ((end = strstr(buf, "\r\n\r\n")))
			return end + 4 - buf;
	}
	return -1;
}

static bool header_is(const char *line, const char *name)
{
	size_t n = strlen(name);

	return !strncasecmp(line, name, n) && line[n] == ':';
}

/*
 * The head rewritten line by line into out: the request's Host/Origin/Referer name the modem, the answer's Location
 * names what the browser used; Connection: close either way, keep-alive dropped.
 */
/* the web's own name a redirect names (http://status.megafon.ru[/…]): a DNS name, not an address and not front */
static bool own_name(const char *head, size_t len, const char *front, char *name, size_t size)
{
	const char *l = strcasestr(head, "\nLocation:"), *v;
	size_t n;

	if (!l || l >= head + len || strncmp(head + 9, "30", 2))
		return false;
	v = l + 10;
	while (*v == ' ')
		v++;
	if (strncasecmp(v, "http://", 7))
		return false;
	v += 7;
	n = strcspn(v, "/:?#\r\n");
	if (!n || n >= size || strspn(v, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") < n)
		return false;
	snprintf(name, size, "%.*s", (int)n, v);
	if (strspn(name, "0123456789.") == n || !strchr(name, '.') || (*front && !strncasecmp(front, name, n) && (!front[n] || front[n] == ':')))
		return false;
	return true;
}

/* v starts with http://<who> followed by a path, a port 80 or nothing: the length of that prefix, else 0 */
static size_t names(const char *v, const char *who)
{
	size_t m = strlen(who);

	if (!*who || strncasecmp(v, "http://", 7) || strncasecmp(v + 7, who, m))
		return 0;
	m += 7;
	if (!strncmp(v + m, ":80", 3) && (v[m + 3] == '/' || !v[m + 3]))
		return m + 3;
	return v[m] == '/' || !v[m] ? m : 0;
}

/*
 * named: the web's own name when it answers only under it (MF150-4: status.megafon.ru), or "": a Location naming it
 * is the router's address too (r123: after the login its page sent the browser to http://status.megafon.ru/…, which
 * the computer behind the router cannot reach, and the page spun «Подождите…»), and a cookie's Domain is dropped so
 * the browser keeps the login under the router's address
 */
static size_t rewrite(const char *head, size_t len, bool request, const char *host, const char *front, const char *named,
		      char *out, size_t cap)
{
	size_t used = 0;
	const char *p = head, *end = head + len;
	char modem[96];

	snprintf(modem, sizeof(modem), "http://%s", host);
	while (p < end) {
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		size_t l = eol ? (size_t)(eol - p + 1) : (size_t)(end - p);
		char line[4096];

		if (l >= sizeof(line))
			l = sizeof(line) - 1;
		memcpy(line, p, l);
		line[l] = 0;
		p += l;
		line[strcspn(line, "\r\n")] = 0;
		if (!*line)
			break;
		if (header_is(line, "Connection") || header_is(line, "Keep-Alive") || header_is(line, "Proxy-Connection"))
			continue;
		if (request && header_is(line, "Host")) {
			used += (size_t)snprintf(out + used, cap - used, "Host: %s\r\n", host);
		} else if (request && (header_is(line, "Origin") || header_is(line, "Referer")) && *front) {
			char *v = strchr(line, ':') + 1, fr[96];

			while (*v == ' ')
				v++;
			snprintf(fr, sizeof(fr), "http://%s", front);
			if (!strncmp(v, fr, strlen(fr)))
				used += (size_t)snprintf(out + used, cap - used, "%.*s%s%s\r\n", (int)(v - line), line, modem, v + strlen(fr));
			else
				used += (size_t)snprintf(out + used, cap - used, "%s\r\n", line);
		} else if (!request && header_is(line, "Location") && *front) {
			char *v = strchr(line, ':') + 1;
			size_t m;

			while (*v == ' ')
				v++;
			/* http://192.168.8.1 or http://192.168.8.1:80 (or the web's own name), then a path or nothing */
			if ((m = names(v, host)) || (m = names(v, named)))
				used += (size_t)snprintf(out + used, cap - used, "Location: http://%s%s\r\n", front, v + m);
			else
				used += (size_t)snprintf(out + used, cap - used, "%s\r\n", line);
		} else if (!request && header_is(line, "Set-Cookie") && *named) {
			char *d = strcasestr(line, "; domain=");

			if (d) {
				char *e = strchr(d + 2, ';');

				used += (size_t)snprintf(out + used, cap - used, "%.*s%s\r\n", (int)(d - line), line, e ? e : "");
			} else {
				used += (size_t)snprintf(out + used, cap - used, "%s\r\n", line);
			}
		} else {
			used += (size_t)snprintf(out + used, cap - used, "%s\r\n", line);
		}
		if (used >= cap - 64)
			return 0;
	}
	used += (size_t)snprintf(out + used, cap - used, "Connection: close\r\n\r\n");
	return used;
}

/* both ways until either side closes or is idle */
static void pipe_both(int a, int b)
{
	char buf[8192];
	struct pollfd p[2] = { { .fd = a, .events = POLLIN }, { .fd = b, .events = POLLIN } };

	for (;;) {
		int n = poll(p, 2, IDLE_MS), i;

		if (n <= 0)
			return;
		for (i = 0; i < 2; i++) {
			ssize_t r;

			if (!(p[i].revents & (POLLIN | POLLHUP | POLLERR)))
				continue;
			r = read(p[i].fd, buf, sizeof(buf));
			if (r <= 0 || !write_all(p[1 - i].fd, buf, (size_t)r))
				return;
		}
	}
}

static void serve(int client, const struct site *s)
{
	static char in[HEAD_MAX], out[HEAD_MAX + 512], orig[HEAD_MAX], named[HEAD_MAX + 512];
	char front[96] = "", name[64] = "";
	size_t orig_len = 0, orig_head = 0, named_len = 0;
	size_t got, n;
	ssize_t head;
	int up;
	const char *h;

	if ((head = read_head(client, in, sizeof(in), &got)) < 0)
		return;
	/* the browser's request as it came, for a second try under the web's own name */
	memcpy(orig, in, got);
	orig_len = got;
	orig_head = (size_t)head;
	/* the address the browser used, from its Host: what a Location to the modem becomes */
	if ((h = strcasestr(in, "\nHost:")) && h < in + head) {
		h += 6;
		while (*h == ' ')
			h++;
		snprintf(front, sizeof(front), "%.*s", (int)strcspn(h, "\r\n"), h);
		if (strpbrk(front, " \t\"<>"))
			front[0] = 0;
	}
	if (!(n = rewrite(in, (size_t)head, true, s->host, front, "", out, sizeof(out))) || (up = connect_to(s->host, s->dev)) < 0) {
		static const char bad[] = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
					  "The modem's web page does not answer.\n";
		write_all(client, bad, sizeof(bad) - 1);
		return;
	}
	if (!write_all(up, out, n) || !write_all(up, in + head, got - (size_t)head)) {
		close(up);
		return;
	}
	/* the answer's head: its Location back to the router's address */
	head = read_head(up, in, sizeof(in), &got);
	/* a web that answers its address only with a redirect to its own name (the owner's MF150-4, 2026-10-30:
	 * «302 → http://status.megafon.ru» to the router, so the link led the browser there): asked again under it, with
	 * Host, Origin and Referer naming it */
	if (head > 0 && !own_name(in, (size_t)head, front, name, sizeof(name)))
		name[0] = 0;
	else if (head > 0) {
		int again;

		if ((named_len = rewrite(orig, orig_head, true, name, front, "", named, sizeof(named))) &&
		    named_len + orig_len - orig_head <= sizeof(named) && (again = connect_to(s->host, s->dev)) >= 0) {
			memcpy(named + named_len, orig + orig_head, orig_len - orig_head);
			named_len += orig_len - orig_head;
			if (write_all(again, named, named_len)) {
				close(up);
				up = again;
				head = read_head(up, in, sizeof(in), &got);
			} else
				close(again);
		}
	}
	if (head < 0 || !(n = rewrite(in, (size_t)head, false, s->host, front, name, out, sizeof(out))) ||
	    !write_all(client, out, n) || !write_all(client, in + head, got - (size_t)head)) {
		close(up);
		return;
	}
	pipe_both(client, up);
	close(up);
}

int main(int argc, char **argv)
{
	struct site sites[MAX_SITES];
	struct pollfd p[MAX_SITES];
	int n = 0, i;

	if (argc < 5 || (argc - 2) % 3) {
		fprintf(stderr, "usage: medvemodem-webproxy <listen-address> <port> <host> <netdev|-> ...\n");
		return 2;
	}
	signal(SIGCHLD, SIG_IGN);	/* the children are not waited for */
	signal(SIGPIPE, SIG_IGN);
	for (i = 2; i + 2 < argc && n < MAX_SITES; i += 3) {
		struct site *s = &sites[n];
		struct in_addr a;

		s->port = atoi(argv[i]);
		snprintf(s->host, sizeof(s->host), "%s", argv[i + 1]);
		snprintf(s->dev, sizeof(s->dev), "%s", argv[i + 2]);
		if (s->port < 1024 || s->port > 65535 || inet_pton(AF_INET, s->host, &a) != 1)
			continue;
		if ((s->fd = listen_on(argv[1], s->port)) < 0) {
			fprintf(stderr, "medvemodem-webproxy: cannot listen on %s:%d\n", argv[1], s->port);
			continue;
		}
		p[n].fd = s->fd;
		p[n].events = POLLIN;
		n++;
	}
	if (!n)
		return 1;
	for (;;) {
		if (poll(p, (nfds_t)n, -1) < 0) {
			if (errno == EINTR)
				continue;
			return 1;
		}
		for (i = 0; i < n; i++) {
			int c;
			pid_t pid;

			if (!(p[i].revents & POLLIN) || (c = accept4(p[i].fd, NULL, NULL, SOCK_CLOEXEC)) < 0)
				continue;
			pid = fork();
			if (pid == 0) {
				int k;

				for (k = 0; k < n; k++)
					close(p[k].fd);
				serve(c, &sites[i]);
				close(c);
				_exit(0);
			}
			close(c);
		}
	}
}
