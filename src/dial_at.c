#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "atport.h"
#include "dial.h"
#include "dial_at.h"

#define BUF 8192
#define REG_WAIT_MS 30000		/* a modem told to search (AT+COPS=0) registers within this, or the dial fails */
#define ACT_TRIES 3			/* CGACT=1 and CGPADDR, 2 s apart, per PDP type */

struct run {
	struct mm_at_port *p;
	long long deadline;
	char buf[BUF];
	json_object *steps;
	char *err;
	size_t err_size;
	int auth;			/* the APN login's AT+CGAUTH protocol, 0 none */
	const char *user, *pass;
	bool xgauth;			/* the login goes as AT+XGAUTH first (Intel XMM) */
	json_object *pre;		/* commands before the activation (AT+XDNS on XMM), or NULL */
};

static long long mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char *jstr(json_object *o, const char *k)
{
	json_object *v = NULL;

	return o && json_object_object_get_ex(o, k, &v) && json_object_is_type(v, json_type_string) ?
	       json_object_get_string(v) : NULL;
}

static void step(struct run *r, const char *what)
{
	json_object_array_add(r->steps, json_object_new_string(what));
}

/* a sleep that keeps to the budget; false when it is spent */
static bool pause_ms(struct run *r, int ms)
{
	long long left = r->deadline - mono_ms();

	if (left <= 0)
		return false;
	usleep((useconds_t)(ms < left ? ms : left) * 1000);
	return mono_ms() < r->deadline;
}

/*
 * One command within the budget: 1 OK, 0 a final code other than OK, -1 no answer or the port failed (err set,
 * the dialogue ends). The answer stays in r->buf.
 */
static int at(struct run *r, const char *cmd, int timeout_ms)
{
	long long left = r->deadline - mono_ms();
	int rc;

	if (left < 500) {
		snprintf(r->err, r->err_size, "%s: the dial took longer than its budget", cmd);
		return -1;
	}
	if (timeout_ms > left)
		timeout_ms = (int)left;
	{
		char e[200] = "";

		rc = mm_at_command(r->p, cmd, timeout_ms, r->buf, sizeof(r->buf), e, sizeof(e));
		if (rc != 1) {
			snprintf(r->err, r->err_size, "%s: %s", cmd, e[0] ? e : "no answer");
			return -1;
		}
	}
	return strstr(r->buf, "\nOK") || !strncmp(r->buf, "OK", 2) ? 1 : 0;
}

/* the final result code of the last answer, for an error */
static const char *final_of(const char *raw)
{
	static char f[64];
	const char *p = raw, *last = NULL;

	while ((p = strpbrk(p, "\r\n"))) {
		while (*p == '\r' || *p == '\n')
			p++;
		if (*p)
			last = p;
	}
	if (!last)
		last = raw;
	snprintf(f, sizeof(f), "%.*s", (int)strcspn(last, "\r\n"), last);
	return f;
}

/* registered (home 1, roaming 5), refused (3: SIM, subscription or IMEI) or neither within REG_WAIT_MS */
static int registration(struct run *r)
{
	static const char *const q[] = { "AT+CEREG?", "AT+C5GREG?", "AT+CREG?" };
	long long until = mono_ms() + REG_WAIT_MS;
	size_t i;

	for (;;) {
		bool refused = false;

		for (i = 0; i < sizeof(q) / sizeof(q[0]); i++) {
			int rc = at(r, q[i], 5000), st;

			if (rc < 0)
				return -1;
			st = rc ? mm_dial_reg(r->buf) : -1;
			if (st == 1 || st == 5)
				return st;
			refused = refused || st == 3;
		}
		if (refused) {
			snprintf(r->err, r->err_size, "the network refused the registration (stat 3): the SIM, its subscription or the IMEI");
			return -1;
		}
		if (mono_ms() >= until || !pause_ms(r, 3000)) {
			snprintf(r->err, r->err_size, "no network registration within %d s", REG_WAIT_MS / 1000);
			return -1;
		}
	}
}

/*
 * One login command (verb CGAUTH or XGAUTH, the same fields): 1 taken, 0 refused, -1 no answer. Its own exchange: the
 * command holds the password, so no error names the command, and the buffers that held it are wiped.
 */
static int login_once(struct run *r, const char *verb, int cid)
{
	char cmd[200], e[200] = "";
	long long left = r->deadline - mono_ms();
	int rc;

	if (left < 500) {
		snprintf(r->err, r->err_size, "AT+%s: the dial took longer than its budget", verb);
		return -1;
	}
	snprintf(cmd, sizeof(cmd), "AT+%s=%d,%d,\"%s\",\"%s\"", verb, cid, r->auth, r->user, r->pass);
	rc = mm_at_command(r->p, cmd, left < 10000 ? (int)left : 10000, r->buf, sizeof(r->buf), e, sizeof(e));
	explicit_bzero(cmd, sizeof(cmd));
	if (rc == 1 && !(strstr(r->buf, "\nOK") || !strncmp(r->buf, "OK", 2))) {
		/* the final code alone; never a line that could be the echo of the command */
		const char *f = final_of(r->buf);

		snprintf(r->err, r->err_size, "AT+%s: %s (the modem did not take the APN login)", verb,
			 strstr(f, verb) || (*r->pass && strstr(f, r->pass)) ? "ERROR" : f);
		rc = 0;
	} else if (rc != 1) {
		snprintf(r->err, r->err_size, "AT+%s: no answer (the modem did not take the APN login)", verb);
		rc = -1;
	}
	explicit_bzero(r->buf, sizeof(r->buf));
	return rc;
}

/*
 * The APN login, sent after the context is defined and before it is activated. Intel XMM takes it as AT+XGAUTH,
 * AT+CGAUTH when the firmware refuses that (a field report).
 */
static bool send_auth(struct run *r, int cid)
{
	int rc;

	if (!r->auth)
		return true;
	if (r->xgauth) {
		if ((rc = login_once(r, "XGAUTH", cid)) == 1) {
			step(r, r->auth == 2 ? "login: CHAP (XGAUTH)" : "login: PAP (XGAUTH)");
			return true;
		}
		if (rc < 0)
			return false;
		step(r, "XGAUTH refused: CGAUTH");
		r->err[0] = '\0';
	}
	if (login_once(r, "CGAUTH", cid) != 1)
		return false;
	step(r, r->auth == 2 ? "login: CHAP" : "login: PAP");
	return true;
}

/* the session the modem holds, when it is the one asked for: active, same APN and PDP type, an address, attached */
static bool reusable(struct run *r, int cid, const char *apn, const char *pdp, char ip[16])
{
	char cmd[32], cur_pdp[16], cur_apn[80], user[80];

	snprintf(cmd, sizeof(cmd), "AT+CGACT?");
	if (at(r, cmd, 10000) != 1 || !mm_dial_cgact(r->buf, cid))
		return false;
	if (at(r, "AT+CGDCONT?", 10000) != 1 || !mm_dial_cgdcont(r->buf, cid, cur_pdp, sizeof(cur_pdp), cur_apn, sizeof(cur_apn)))
		return false;
	/* IP and IPV4 are one type to modems that accept the non-standard name */
	if (strcasecmp(cur_pdp, pdp) && !(!strcasecmp(pdp, "IP") && !strcasecmp(cur_pdp, "IPV4")))
		return false;
	if (*apn && strcasecmp(cur_apn, apn))
		return false;
	/* a modem reboot forgets the login, and the network may still activate the context without one: the login the
	 * modem holds must be ours (its password is never shown back) */
	if (r->auth && !r->xgauth && (at(r, "AT+CGAUTH?", 10000) != 1 ||
					   mm_dial_cgauth(r->buf, cid, user, sizeof(user)) != r->auth || strcmp(user, r->user)))
		return false;	/* an XGAUTH login is not shown by AT+CGAUTH?: not checked there */
	snprintf(cmd, sizeof(cmd), "AT+CGPADDR=%d", cid);
	if (at(r, cmd, 10000) != 1 || !mm_dial_cgpaddr(r->buf, ip))
		return false;
	/* an address on a context the network dropped: CGACT and CGPADDR still say so, CGATT does not */
	return at(r, "AT+CGATT?", 10000) == 1 && mm_dial_cgatt(r->buf) == 1;
}

/* define, release, activate while not active, read the address; true with ip */
static bool activate(struct run *r, int cid, const char *pdp, const char *apn, char ip[16])
{
	char cmd[160];
	int t, rc;

	snprintf(cmd, sizeof(cmd), "AT+CGDCONT=%d,\"%s\",\"%s\"", cid, pdp, apn);
	if ((rc = at(r, cmd, 10000)) != 1) {
		if (!rc)
			snprintf(r->err, r->err_size, "%s: %s", cmd, final_of(r->buf));
		return false;
	}
	if (!send_auth(r, cid))
		return false;
	/* the family's steps before the activation; an error answer does not stop the dial */
	for (t = 0; r->pre && t < (int)json_object_array_length(r->pre); t++)
		if (at(r, json_object_get_string(json_object_array_get_idx(r->pre, t)), 10000) < 0)
			return false;
	snprintf(cmd, sizeof(cmd), "AT+CGACT=0,%d", cid);
	if (at(r, cmd, 20000) < 0)
		return false;
	if (!pause_ms(r, 2000))
		return false;
	for (t = 0; t < ACT_TRIES; t++) {
		if ((rc = at(r, "AT+CGACT?", 10000)) < 0)
			return false;
		if (!rc || !mm_dial_cgact(r->buf, cid)) {
			snprintf(cmd, sizeof(cmd), "AT+CGACT=1,%d", cid);
			if ((rc = at(r, cmd, 30000)) < 0)
				return false;
			if (!rc)
				snprintf(r->err, r->err_size, "%s: %s", cmd, final_of(r->buf));
		}
		if (!pause_ms(r, 2000))
			return false;
		snprintf(cmd, sizeof(cmd), "AT+CGPADDR=%d", cid);
		if ((rc = at(r, cmd, 10000)) < 0)
			return false;
		if (rc && mm_dial_cgpaddr(r->buf, ip))
			return true;
	}
	if (!r->err[0])
		snprintf(r->err, r->err_size, "AT+CGPADDR=%d: no IPv4 address with %s", cid, pdp);
	return false;
}

/*
 * The data port's commands (Intel XMM over NCM: AT+XDATACHANNEL, AT+CGDATA="M-RAW_IP"), sent on that port once an
 * address is there. The port goes over to data after them (4PDA 994474-28248), so their answers are only noted, as
 * other projects do (CONNECT, OK or nothing).
 */
static bool data_channel(struct run *r, const char *path, int settle_ms, json_object *cmds)
{
	struct mm_at_port *d;
	char e[200] = "", line[120];
	size_t i;

	if (!(d = mm_at_open(path, settle_ms, e, sizeof(e)))) {
		snprintf(r->err, r->err_size, "the data port %s: %s", path, e[0] ? e : "not opened");
		return false;
	}
	for (i = 0; i < json_object_array_length(cmds); i++) {
		const char *cmd = json_object_get_string(json_object_array_get_idx(cmds, i));
		long long left = r->deadline - mono_ms();
		int rc = left > 500 ? mm_at_command(d, cmd, left < 5000 ? (int)left : 5000, r->buf, sizeof(r->buf), e, sizeof(e)) : -1;

		snprintf(line, sizeof(line), "data port: %.*s %s", (int)strcspn(cmd, "="), cmd, rc == 1 ? final_of(r->buf) : "no answer");
		step(r, line);
	}
	mm_at_close(d);
	return true;
}

json_object *mm_dial_at_run(const char *path, json_object *job, int settle_ms, char *err, size_t err_size)
{
	struct run r = { .err = err, .err_size = err_size };
	json_object *out, *dns, *v = NULL;
	int cid = json_object_object_get_ex(job, "cid", &v) ? json_object_get_int(v) : 1, mode;
	const char *apn = jstr(job, "apn") ? jstr(job, "apn") : "", *pdp = jstr(job, "pdp"), *alt = jstr(job, "alt");
	const char *was = jstr(job, "plmn_was"), *used = NULL;
	bool cold = json_object_object_get_ex(job, "cold", &v) && json_object_get_boolean(v), reused = false;
	char plmn[8] = "", ip[16] = "", gw[16] = "", cmd[64];

	err[0] = '\0';
	if (!pdp) {
		snprintf(err, err_size, "no PDP type");
		return NULL;
	}
	r.deadline = mono_ms() + (json_object_object_get_ex(job, "budget_ms", &v) ? json_object_get_int(v) : 60000);
	r.auth = json_object_object_get_ex(job, "auth", &v) ? json_object_get_int(v) : 0;
	r.user = jstr(job, "username") ? jstr(job, "username") : "";
	r.pass = jstr(job, "password") ? jstr(job, "password") : "";
	r.xgauth = jstr(job, "login") && !strcmp(jstr(job, "login"), "xgauth");
	r.pre = json_object_object_get(job, "pre");
	if (!(r.p = mm_at_open(path, settle_ms, err, err_size)))
		return NULL;
	out = json_object_new_object();
	r.steps = json_object_new_array();
	json_object_object_add(out, "steps", r.steps);

	/* the network: a modem left deregistered (+COPS: 2) by another tool is told to search again */
	if (at(&r, "AT+COPS?", 10000) < 0)
		goto done;
	mode = mm_dial_cops(r.buf, plmn);
	step(&r, "cops");
	if (mode == 2) {
		step(&r, "deregistered: AT+COPS=0");
		if (at(&r, "AT+COPS=0", 60000) < 0)
			goto done;
	}
	if (registration(&r) < 0)
		goto done;
	step(&r, "registered");
	if (!plmn[0] && at(&r, "AT+COPS?", 10000) == 1)
		mm_dial_cops(r.buf, plmn);
	if (plmn[0])
		json_object_object_add(out, "plmn", json_object_new_string(plmn));

	/* the session already up stays (a metric change took the interface down and up), unless a recovery asked for a
	 * new one or the network changed (another operator, back from roaming); a tracking area change does not count */
	if (cold)
		step(&r, "cold: asked for a new session");
	else if (was && plmn[0] && strcmp(was, plmn))
		step(&r, "cold: the network changed");
	else if (reusable(&r, cid, apn, pdp, ip))
		reused = true, used = pdp;
	if (!reused) {
		ip[0] = '\0';
		err[0] = '\0';
		if (activate(&r, cid, pdp, apn, ip)) {
			used = pdp;
		} else if (alt && !strstr(err, "budget") && !strstr(err, "no answer")) {
			char first[200];

			snprintf(first, sizeof(first), "%s", err);
			step(&r, "no address: the other PDP type");
			err[0] = '\0';
			if (activate(&r, cid, alt, apn, ip)) {
				used = alt;
			} else {
				char second[200];

				snprintf(second, sizeof(second), "%s", err);
				snprintf(err, err_size, "%s; with %s: %s", first, alt, second);
			}
		}
		if (!used)
			goto done;
	}
	step(&r, reused ? "reused" : "activated");
	err[0] = '\0';
	/* a new session's data bound to its port (a reused one is bound already) */
	if (!reused && json_object_object_get(job, "post") &&
	    !data_channel(&r, jstr(job, "data_node") ? jstr(job, "data_node") : "", settle_ms, json_object_object_get(job, "post")))
		goto done;
	json_object_object_add(out, "ipv4", json_object_new_string(ip));
	json_object_object_add(out, "pdp", json_object_new_string(used));
	json_object_object_add(out, "reused", json_object_new_boolean(reused));
	dns = json_object_new_array();
	snprintf(cmd, sizeof(cmd), "AT+CGCONTRDP=%d", cid);
	if (at(&r, cmd, 10000) == 1)
		mm_dial_cgcontrdp(r.buf, gw, dns);
	err[0] = '\0';
	if (gw[0])
		json_object_object_add(out, "gw", json_object_new_string(gw));
	if (!json_object_array_length(dns) && jstr(job, "dns_cmd") && jstr(job, "dns_prefix") &&
	    at(&r, jstr(job, "dns_cmd"), 10000) == 1) {
		/* the vendor's own query (AT+GTDNS on the FM350) when CGCONTRDP named none */
		json_object *more = mm_dial_ipv4s(r.buf, jstr(job, "dns_prefix"));
		size_t i;

		for (i = 0; i < json_object_array_length(more); i++)
			json_object_array_add(dns, json_object_get(json_object_array_get_idx(more, i)));
		json_object_put(more);
		err[0] = '\0';
	}
	json_object_object_add(out, "dns", dns);
done:
	mm_at_close(r.p);
	return out;
}
