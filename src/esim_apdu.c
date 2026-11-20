/*
 * esim-apdu: APDU to an eUICC over AT logical channels for the managed eSIM
 * backend (docs/ESIM-INTEGRATION.md). Started by esim-lpac in the process group
 * medvemodemd supervises, on ports the engine leased for this operation.
 *
 *   esim-apdu probe [-f fast_ms] [-s slow_ms] <tty>...
 *       Which of the modem's ports reaches the eUICC: AT+CCHO to the ISD-R on
 *       each port, closing the channel it got. When none opens, leaked logical
 *       channels are closed through the first port that answers AT and the
 *       ports are tried once more, with more patience. Prints one JSON object:
 *       {"port", "format", "cleaned", "ports": [{"node", "verdict"}]}.
 *       Exit 0 found, 1 not found, 2 usage.
 *
 *   esim-apdu run [-t apdu_ms] <tty> -- <lpac> [args...]
 *       Runs lpac with LPAC_APDU=stdio (set by the caller) and answers its APDU
 *       requests on <tty>, kept open for the whole operation. lpac's other
 *       output lines are passed through. Exit: lpac's status; 124 when lpac's
 *       stdio APDU driver does not wait for answers (lpac 2.3.0); 125 when the
 *       port cannot be opened.
 *
 * Nothing is logged: APDUs carry profile data and, during a download, secrets.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <json-c/json.h>

#include "apdu.h"
#include "atport.h"

#define ANSWER_MAX 8192
#define CCHO_MS 10000
#define CCHC_MS 3000
#define CLEANUP_CHANNELS 4

static int apdu_ms = 60000;

static void usage(void)
{
	fprintf(stderr, "usage: esim-apdu probe [-f fast_ms] [-s slow_ms] <tty>...\n"
			"       esim-apdu run [-t apdu_ms] <tty> -- <lpac> [args...]\n");
	exit(2);
}

static int at(struct mm_at_port *p, const char *cmd, int ms, char *answer)
{
	char err[200];

	return mm_at_command(p, cmd, ms, answer, ANSWER_MAX, err, sizeof(err));
}

/* close the logical channels a killed operation may have left open */
static void cleanup_channels(struct mm_at_port *p, int ms)
{
	char cmd[32], answer[ANSWER_MAX];
	int c;

	for (c = 1; c <= CLEANUP_CHANNELS; c++) {
		snprintf(cmd, sizeof(cmd), "AT+CCHC=%d", c);
		at(p, cmd, ms, answer);
	}
}

/* ---- probe ---- */

struct probe_port {
	const char *node;
	const char *verdict;
};

/* one CCHO to the ISD-R on node; channel number or -1, verdict set */
static int probe_one(struct probe_port *pp, int ms, bool *bare)
{
	char err[200], answer[ANSWER_MAX], cmd[64];
	struct mm_at_port *p = mm_at_open(pp->node, 0, err, sizeof(err));
	int rc, ch;

	if (!p) {
		pp->verdict = "unopenable";
		return -1;
	}
	rc = at(p, "AT+CCHO=\"" MM_APDU_ISDR_AID "\"", ms, answer);
	pp->verdict = rc < 0 ? "unopenable" : mm_apdu_probe_verdict(answer, rc);
	ch = mm_apdu_ccho_channel(answer, bare);
	if (ch > 0) {
		/* a probe leaves nothing open behind it */
		snprintf(cmd, sizeof(cmd), "AT+CCHC=%d", ch);
		at(p, cmd, CCHC_MS, answer);
	}
	mm_at_close(p);
	return ch;
}

/* close leaked channels through the first port that answers AT */
static bool probe_cleanup(struct probe_port *ports, int n)
{
	char err[200], answer[ANSWER_MAX];
	int i;

	for (i = 0; i < n; i++) {
		struct mm_at_port *p = mm_at_open(ports[i].node, 0, err, sizeof(err));
		bool live;

		if (!p)
			continue;
		live = at(p, "AT", 1000, answer) == 1 && strstr(answer, "OK");
		if (live)
			cleanup_channels(p, 1000);
		mm_at_close(p);
		if (live)
			return true;
	}
	return false;
}

static int cmd_probe(int argc, char **argv)
{
	int fast = 2000, slow = 4000, opt, i, n, found = -1, pass;
	struct probe_port *ports;
	bool bare = false, cleaned = false;
	json_object *out, *list;

	while ((opt = getopt(argc, argv, "f:s:")) != -1) {
		if (opt == 'f')
			fast = atoi(optarg);
		else if (opt == 's')
			slow = atoi(optarg);
		else
			usage();
	}
	n = argc - optind;
	if (n < 1 || fast < 1 || slow < 1)
		usage();
	ports = calloc((size_t)n, sizeof(*ports));
	if (!ports)
		return 1;
	for (i = 0; i < n; i++) {
		ports[i].node = argv[optind + i];
		if (ports[i].node[0] != '/')
			usage();
	}
	/* a short first pass: the eUICC port answers at once, others are silent */
	for (pass = 0; pass < 2 && found < 0; pass++) {
		if (pass == 1) {
			/* nothing opened: maybe every channel leaked, not "no eUICC" */
			cleaned = probe_cleanup(ports, n);
			if (!cleaned)
				break;
		}
		for (i = 0; i < n && found < 0; i++)
			if (probe_one(&ports[i], pass ? slow : fast, &bare) > 0)
				found = i;
	}

	out = json_object_new_object();
	list = json_object_new_array();
	json_object_object_add(out, "port", found >= 0 ? json_object_new_string(ports[found].node) : NULL);
	if (found >= 0)
		json_object_object_add(out, "format", json_object_new_string(bare ? "bare" : "prefixed"));
	json_object_object_add(out, "cleaned", json_object_new_boolean(cleaned));
	for (i = 0; i < n; i++) {
		json_object *e = json_object_new_object();

		json_object_object_add(e, "node", json_object_new_string(ports[i].node));
		json_object_object_add(e, "verdict", json_object_new_string(ports[i].verdict ? ports[i].verdict
											     : "untried"));
		json_object_array_add(list, e);
	}
	json_object_object_add(out, "ports", list);
	printf("%s\n", json_object_to_json_string_ext(out, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE));
	json_object_put(out);
	free(ports);
	return found >= 0 ? 0 : 1;
}

/* ---- run ---- */

struct bridge {
	struct mm_at_port *port;
	int channel;		/* the channel lpac opened, -1 none */
	int requests;
	bool first_was_connect;
};

static void reply_apdu(FILE *to, int ecode, const char *data)
{
	if (data)
		fprintf(to, "{\"type\":\"apdu\",\"payload\":{\"ecode\":%d,\"data\":\"%s\"}}\n", ecode, data);
	else
		fprintf(to, "{\"type\":\"apdu\",\"payload\":{\"ecode\":%d}}\n", ecode);
	fflush(to);
}

static int open_channel(struct bridge *b, const char *aid)
{
	char cmd[128], answer[ANSWER_MAX];
	int ch;

	if (!mm_apdu_hex_ok(aid, 32))
		return -1;
	snprintf(cmd, sizeof(cmd), "AT+CCHO=\"%s\"", aid);
	at(b->port, cmd, CCHO_MS, answer);
	ch = mm_apdu_ccho_channel(answer, NULL);
	if (ch < 0) {
		/* the card has a handful of channels: a killed operation's may still be open */
		cleanup_channels(b->port, 1000);
		at(b->port, cmd, CCHO_MS, answer);
		ch = mm_apdu_ccho_channel(answer, NULL);
	}
	if (ch > 0)
		b->channel = ch;
	return ch;
}

static void close_channel(struct bridge *b, int ch)
{
	char cmd[32], answer[ANSWER_MAX];

	if (ch < 1)
		return;
	snprintf(cmd, sizeof(cmd), "AT+CCHC=%d", ch);
	at(b->port, cmd, CCHC_MS, answer);
	if (ch == b->channel)
		b->channel = -1;
}

/* one command APDU; the response hex into rx, or -1 */
static int transmit(struct bridge *b, const char *apdu, char *rx, size_t size)
{
	char *cmd, answer[ANSWER_MAX];
	size_t len = strlen(apdu) + 48;
	int rc, n = -1, attempt;

	if (b->channel < 1 || !mm_apdu_hex_ok(apdu, MM_APDU_MAX_HEX))
		return -1;
	cmd = malloc(len);
	if (!cmd || mm_apdu_cgla_command(b->channel, apdu, cmd, len) < 0) {
		free(cmd);
		return -1;
	}
	for (attempt = 0; attempt < 2 && n < 0; attempt++) {
		rc = at(b->port, cmd, apdu_ms, answer);
		n = mm_apdu_cgla_response(answer, rx, size);
		/*
		 * Repeat only an exchange that never started (nothing came back, not even
		 * the echo): a command the card may have executed is not sent twice.
		 */
		if (n < 0 && !(rc == 0 && !answer[0]))
			break;
		if (n < 0 && !attempt)
			sleep(1);
	}
	free(cmd);
	return n;
}

static void handle_apdu(struct bridge *b, json_object *payload, FILE *to)
{
	json_object *v = NULL;
	const char *func = json_object_object_get_ex(payload, "func", &v) ? json_object_get_string(v) : "";
	const char *param = json_object_object_get_ex(payload, "param", &v) && json_object_is_type(v, json_type_string)
				    ? json_object_get_string(v)
				    : NULL;
	char rx[2 * 260 + 8];

	if (!b->requests++)
		b->first_was_connect = !strcmp(func, "connect");
	if (!strcmp(func, "connect")) {
		reply_apdu(to, 0, NULL);
	} else if (!strcmp(func, "disconnect")) {
		close_channel(b, b->channel);
		reply_apdu(to, 0, NULL);
	} else if (!strcmp(func, "logic_channel_open")) {
		reply_apdu(to, param ? open_channel(b, param) : -1, NULL);
	} else if (!strcmp(func, "logic_channel_close")) {
		if (mm_apdu_hex_ok(param, 2))
			close_channel(b, (int)strtol(param, NULL, 16));
		reply_apdu(to, 0, NULL);
	} else if (!strcmp(func, "transmit")) {
		if (param && transmit(b, param, rx, sizeof(rx)) > 0)
			reply_apdu(to, 0, rx);
		else
			reply_apdu(to, -1, NULL);
	} else {
		reply_apdu(to, -1, NULL);
	}
}

static int cmd_run(int argc, char **argv)
{
	int opt, to_lpac[2], from_lpac[2], status = 0, rc;
	char err[200], *line = NULL;
	size_t cap = 0;
	struct bridge b = { NULL, -1, 0, false };
	const char *node;
	FILE *from, *to;
	pid_t pid;

	while ((opt = getopt(argc, argv, "t:")) != -1) {
		if (opt == 't' && atoi(optarg) > 0)
			apdu_ms = atoi(optarg);
		else
			usage();
	}
	/* <tty> -- <lpac> [args] */
	if (argc - optind < 2)
		usage();
	node = argv[optind];
	if (node[0] != '/')
		usage();
	optind++;
	if (optind < argc && !strcmp(argv[optind], "--"))
		optind++;
	if (optind >= argc)
		usage();

	b.port = mm_at_open(node, 0, err, sizeof(err));
	if (!b.port) {
		printf("{\"type\":\"lpa\",\"payload\":{\"code\":-1,\"message\":\"apdu_port\",\"data\":\"%s\"}}\n",
		       "cannot open the eUICC port");
		return 125;
	}
	signal(SIGPIPE, SIG_IGN);
	/* an early way out gives the port its own settings back (review 2026-11-15) */
	if (pipe2(to_lpac, O_CLOEXEC) || pipe2(from_lpac, O_CLOEXEC)) {
		mm_at_close(b.port);
		return 125;
	}
	pid = fork();
	if (pid < 0) {
		mm_at_close(b.port);
		return 125;
	}
	if (pid == 0) {
		signal(SIGPIPE, SIG_DFL);
		dup2(to_lpac[0], 0);
		dup2(from_lpac[1], 1);
		execv(argv[optind], argv + optind);
		_exit(127);
	}
	close(to_lpac[0]);
	close(from_lpac[1]);
	from = fdopen(from_lpac[0], "r");
	to = fdopen(to_lpac[1], "w");
	if (!from || !to) {
		kill(pid, SIGKILL);
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)	/* a signal must not leave a zombie (audit STAB-03) */
			;
		mm_at_close(b.port);
		return 125;
	}

	while (getline(&line, &cap, from) > 0) {
		json_object *msg, *type = NULL, *payload = NULL;

		line[strcspn(line, "\r\n")] = '\0';
		msg = json_tokener_parse(line);
		json_object_object_get_ex(msg, "type", &type);
		json_object_object_get_ex(msg, "payload", &payload);
		if (type && !strcmp(json_object_get_string(type), "apdu")) {
			handle_apdu(&b, payload, to);
		} else if (type && !strcmp(json_object_get_string(type), "http")) {
			/* HTTP goes through lpac's own backend in this stage */
			fprintf(to, "{\"type\":\"http\",\"payload\":{\"rcode\":500,\"rx\":\"\"}}\n");
			fflush(to);
		} else {
			printf("%s\n", line);
			fflush(stdout);
		}
		json_object_put(msg);
	}
	free(line);
	fclose(to);
	fclose(from);
	while ((rc = waitpid(pid, &status, 0)) < 0 && errno == EINTR)
		;
	/* lpac died with its channel open: leave the card usable for the next one */
	close_channel(&b, b.channel);
	mm_at_close(b.port);
	if (rc < 0 || !WIFEXITED(status))
		return 1;
	if (WEXITSTATUS(status) == 127)
		return 127;
	/*
	 * lpac 2.3.0's stdio driver treats a sent request as a failure and returns
	 * before reading the answer: one "connect", then an error exit. Fixed
	 * upstream after 2.3.0 (lpac 977c324).
	 */
	if (WEXITSTATUS(status) != 0 && b.requests == 1 && b.first_was_connect)
		return 124;
	return WEXITSTATUS(status);
}

int main(int argc, char **argv)
{
	if (argc < 2)
		usage();
	if (!strcmp(argv[1], "probe"))
		return cmd_probe(argc - 1, argv + 1);
	if (!strcmp(argv[1], "run"))
		return cmd_run(argc - 1, argv + 1);
	usage();
	return 2;
}
