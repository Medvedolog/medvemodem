#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "at.h"
#include "atport.h"

#define MAX_RESPONSE 8192

static long long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

/* write all of buf before the deadline */
static int write_all(int fd, const char *buf, size_t len, long long deadline)
{
	while (len) {
		ssize_t n = write(fd, buf, len);
		struct pollfd p = { fd, POLLOUT, 0 };

		if (n > 0) {
			buf += n;
			len -= (size_t)n;
			continue;
		}
		if (n < 0 && errno != EAGAIN && errno != EINTR)
			return -1;
		/* one clock read: a second one could cross the deadline and hand poll() a negative
		 * timeout, which waits for ever (audit 2026-10-15) */
		long long left = deadline - now_ms();
		if (left <= 0 || poll(&p, 1, left > 60000 ? 60000 : (int)left) < 0)
			return -1;
	}
	return 0;
}

struct mm_at_port {
	int fd;
	bool have_saved;
	struct termios saved;
	char path[256];
};

struct mm_at_port *mm_at_open(const char *path, int settle_ms, char *err, size_t err_size)
{
	struct mm_at_port *p;
	struct termios raw;

	err[0] = '\0';
	p = calloc(1, sizeof(*p));
	if (!p) {
		snprintf(err, err_size, "out of memory");
		return NULL;
	}
	snprintf(p->path, sizeof(p->path), "%s", path);
	p->fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (p->fd < 0) {
		snprintf(err, err_size, "cannot open %s: %s", path, strerror(errno));
		free(p);
		return NULL;
	}
	if (tcgetattr(p->fd, &p->saved) == 0) {
		p->have_saved = true;
		raw = p->saved;
		cfmakeraw(&raw);
		cfsetispeed(&raw, B115200);
		cfsetospeed(&raw, B115200);
		raw.c_cflag |= CLOCAL | CREAD;
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 0;
		tcsetattr(p->fd, TCSANOW, &raw);
	}
	tcflush(p->fd, TCIOFLUSH);
	if (settle_ms > 0) {
		/* the port was cut off mid-command: let its late answer arrive and drop it */
		long long until = now_ms() + settle_ms;
		char junk[512];

		for (;;) {
			/* one clock read: a second one can pass the deadline, and poll() waits for ever on a negative timeout */
			long long left = until - now_ms();
			struct pollfd q = { p->fd, POLLIN, 0 };

			if (left <= 0)
				break;
			if (poll(&q, 1, (int)left) > 0 && read(p->fd, junk, sizeof(junk)) <= 0)
				break;
		}
	}
	return p;
}


/*
 * poll() said there was something and read() brought nothing: the port went away (an error other than EAGAIN or
 * EINTR) or a spurious wake. Never a spin to the deadline (audit 2026-10-20): the first is the end, the second a
 * short pause.
 */
static bool read_lost(ssize_t n)
{
	if (n < 0 && errno != EAGAIN && errno != EINTR)
		return true;
	usleep(20000);
	return false;
}
int mm_at_command(struct mm_at_port *p, const char *cmd, int timeout_ms, char *buf, size_t size, char *err,
		  size_t err_size)
{
	long long deadline = now_ms() + timeout_ms;
	size_t len = 0, clen = strlen(cmd);
	char *line;
	bool done = false;
	int rc;

	err[0] = '\0';
	buf[0] = '\0';
	line = malloc(clen + 2);
	if (!line) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	memcpy(line, cmd, clen);
	line[clen] = '\r';
	rc = write_all(p->fd, line, clen + 1, deadline);
	free(line);
	if (rc) {
		snprintf(err, err_size, "write to %s failed: %s", p->path, strerror(errno));
		return -1;
	}
	while (!done && now_ms() < deadline && len < size - 1) {
		struct pollfd q = { p->fd, POLLIN, 0 };
		int left = (int)(deadline - now_ms());
		ssize_t n;

		int pr = poll(&q, 1, left > 0 ? left : 0);

		if (pr < 0 && errno != EINTR) {
			snprintf(err, err_size, "%s went away", p->path);
			return -1;
		}
		if (pr <= 0)
			continue;
		if (q.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			snprintf(err, err_size, "%s went away", p->path);
			return -1;
		}
		n = read(p->fd, buf + len, size - 1 - len);
		if (n > 0) {
			size_t stray;

			len += (size_t)n;
			buf[len] = '\0';
			/* a late answer to a killed worker's command is not ours */
			while ((stray = mm_at_stray_prefix(cmd, buf)) > 0) {
				memmove(buf, buf + stray, len - stray + 1);
				len -= stray;
			}
			/* a foreign answer still at the front has no final yet: keep reading */
			done = mm_at_complete(buf);
		} else if (read_lost(n)) {
			snprintf(err, err_size, "%s went away", p->path);
			return -1;
		}
	}
	if (!done)
		snprintf(err, err_size, "no answer to the command within %d ms", timeout_ms);
	return done ? 1 : 0;
}

int mm_at_prompt(struct mm_at_port *p, const char *cmd, const char *payload, int timeout_ms, char *buf, size_t size,
		 char *err, size_t err_size)
{
	long long deadline = now_ms() + timeout_ms;
	size_t len = 0, clen = strlen(cmd), plen = strlen(payload);
	char *line;
	bool prompted = false;
	int rc;

	err[0] = '\0';
	buf[0] = '\0';
	if (!(line = malloc(clen + plen + 2))) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	memcpy(line, cmd, clen);
	line[clen] = '\r';
	rc = write_all(p->fd, line, clen + 1, deadline);
	if (rc) {
		free(line);
		snprintf(err, err_size, "write to %s failed: %s", p->path, strerror(errno));
		return -1;
	}
	while (now_ms() < deadline && len < size - 1) {
		struct pollfd q = { p->fd, POLLIN, 0 };
		int left = (int)(deadline - now_ms());
		ssize_t n;

		int pr = poll(&q, 1, left > 0 ? left : 0);

		if (pr < 0 && errno != EINTR) {
			free(line);
			snprintf(err, err_size, "%s went away", p->path);
			return -1;
		}
		if (pr <= 0)
			continue;
		if (q.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			free(line);
			snprintf(err, err_size, "%s went away", p->path);
			return -1;
		}
		n = read(p->fd, buf + len, size - 1 - len);
		if (n <= 0) {
			if (read_lost(n)) {
				free(line);
				snprintf(err, err_size, "%s went away", p->path);
				return -1;
			}
			continue;
		}
		len += (size_t)n;
		buf[len] = '\0';
		if (!prompted && strchr(buf + (strncmp(buf, cmd, clen) ? 0 : clen), '>')) {
			/* the prompt: the payload and Ctrl-Z; what comes next is the answer */
			prompted = true;
			memcpy(line, payload, plen);
			line[plen] = 0x1a;
			if (write_all(p->fd, line, plen + 1, deadline)) {
				free(line);
				snprintf(err, err_size, "write to %s failed: %s", p->path, strerror(errno));
				return -1;
			}
			len = 0;
			buf[0] = '\0';
			continue;
		}
		if (mm_at_complete(buf)) {
			free(line);
			return 1;
		}
	}
	free(line);
	if (!prompted)
		(void)!write(p->fd, "\x1b", 1);	/* leave a prompt that came late */
	snprintf(err, err_size, prompted ? "no answer after the message within %d ms" : "no prompt within %d ms", timeout_ms);
	return 0;
}

int mm_at_wait_line(struct mm_at_port *p, const char *prefix, int timeout_ms, char *buf, size_t size)
{
	long long deadline = now_ms() + timeout_ms;
	size_t len = 0;

	buf[0] = '\0';
	while (now_ms() < deadline && len < size - 1) {
		struct pollfd q = { p->fd, POLLIN, 0 };
		int left = (int)(deadline - now_ms());
		const char *at, *end;
		ssize_t n;

		int pr = poll(&q, 1, left > 0 ? left : 0);

		if (pr < 0 && errno != EINTR)
			return 0;
		if (pr <= 0)
			continue;
		if (q.revents & (POLLERR | POLLHUP | POLLNVAL))
			return 0;
		n = read(p->fd, buf + len, size - 1 - len);
		if (n <= 0) {
			if (read_lost(n))
				return 0;
			continue;
		}
		len += (size_t)n;
		buf[len] = '\0';
		if ((at = strstr(buf, prefix)) && (end = strpbrk(at, "\r\n"))) {
			/* a quoted text may hold line breaks: the line ends after the closing quote's field */
			const char *q1 = strchr(at, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;

			if (q1 && q1 < end && !q2)
				continue;
			if (q2 && !(end = strpbrk(q2, "\r\n")))
				continue;
			memmove(buf, at, (size_t)(end - at));
			buf[end - at] = '\0';
			return 1;
		}
	}
	return 0;
}

void mm_at_close(struct mm_at_port *p)
{
	if (!p)
		return;
	if (p->have_saved)
		tcsetattr(p->fd, TCSANOW, &p->saved);
	close(p->fd);
	free(p);
}

json_object *mm_at_exchange(const char *path, const char *const *cmds, int ncmds, int timeout_ms, int settle_ms,
			    char *err, size_t err_size)
{
	struct mm_at_port *p = mm_at_open(path, settle_ms, err, err_size);
	json_object *out, *responses;
	char buf[MAX_RESPONSE], why[200];
	int i, complete = 0;

	if (!p)
		return NULL;
	out = json_object_new_object();
	responses = json_object_new_object();
	for (i = 0; i < ncmds; i++) {
		int rc;

		if (!strncmp(cmds[i], MM_AT_WAIT, strlen(MM_AT_WAIT))) {
			/* a pause between writes, not sent (the modem settles after flight mode) */
			long ms = strtol(cmds[i] + strlen(MM_AT_WAIT), NULL, 10);

			if (ms > 0 && ms <= 10000)
				usleep((useconds_t)ms * 1000);
			json_object_object_add(responses, cmds[i], json_object_new_string("OK"));
			complete++;
			continue;
		}
		rc = mm_at_command(p, cmds[i], timeout_ms, buf, sizeof(buf), why, sizeof(why));

		json_object_object_add(responses, cmds[i], json_object_new_string(buf));
		if (rc < 0) {
			snprintf(err, err_size, "%s", why);
			break;
		}
		if (!rc) {
			/* no final result: the port is not answering, stop here */
			snprintf(err, err_size, "no answer to %s within %d ms", cmds[i], timeout_ms);
			break;
		}
		complete++;
	}
	json_object_object_add(out, "responses", responses);
	json_object_object_add(out, "complete", json_object_new_int(complete));
	mm_at_close(p);
	return out;
}
