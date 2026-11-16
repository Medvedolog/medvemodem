#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "at.h"
#include "atport.h"
#include "sms_at.h"

#define BUF 65536

static const char *jstr(json_object *o, const char *k)
{
	json_object *v = NULL;

	return o && json_object_object_get_ex(o, k, &v) && json_object_is_type(v, json_type_string) ?
	       json_object_get_string(v) : NULL;
}

/* one command; its raw answer under key; false when the port failed or gave no final code */
static bool cmd(struct mm_at_port *p, json_object *out, const char *key, const char *line, int timeout_ms, char *buf,
		char *err, size_t err_size)
{
	int rc = mm_at_command(p, line, timeout_ms, buf, BUF, err, err_size);

	json_object_object_add(out, key, json_object_new_string(buf));
	return rc == 1;
}

json_object *mm_sms_at_run(const char *path, json_object *job, int settle_ms, char *err, size_t err_size)
{
	struct mm_at_port *p = mm_at_open(path, settle_ms, err, err_size);
	const char *op = jstr(job, "op");
	json_object *out;
	char *buf;

	if (!p)
		return NULL;
	out = json_object_new_object();
	if (!(buf = malloc(BUF))) {
		snprintf(err, err_size, "out of memory");
		mm_at_close(p);
		return out;
	}
	if (!op) {
		snprintf(err, err_size, "no operation");
	} else if (!strcmp(op, "list")) {
		/* r96: a DW5821e/T77W968 resets CNMI to 0,0 now and then, and its incoming SMS are not stored any more
		 * (a field report): set back to 2,1 (stored, indicated) only when it is 0,0 */
		if (json_object_get_boolean(json_object_object_get(job, "cnmi_fix")) &&
		    cmd(p, out, "cnmi", "AT+CNMI?", 5000, buf, err, err_size) && strstr(buf, "+CNMI:")) {
			char *c = strstr(buf, "+CNMI:") + 6;
			int mode = -1, mt = -1;

			if (sscanf(c, " %d , %d", &mode, &mt) == 2 && mode == 0 && mt == 0 &&
			    cmd(p, out, "cnmi_set", "AT+CNMI=2,1,0,0,0", 5000, buf, err, err_size))
				json_object_object_add(out, "cnmi_fixed", json_object_new_boolean(1));
		}
		/* PDU mode for the listing; the storages stay as the modem has them (S5: CPMS persists) */
		if (cmd(p, out, "cmgf", "AT+CMGF=0", 5000, buf, err, err_size) &&
		    cmd(p, out, "cpms", "AT+CPMS?", 5000, buf, err, err_size))
			cmd(p, out, "cmgl", "AT+CMGL=4", 20000, buf, err, err_size);
	} else if (!strcmp(op, "cnmi")) {
		/* the owner's T77W968 in MBIM (r122, 2026-10-30): after one sent SMS the replies stopped showing, the sent ones
		 * went on: its CNMI dropped to 0,0 and ModemManager, which lists it, never puts it back. Only CNMI, set to
		 * 2,1 when it is 0,0 */
		if (cmd(p, out, "cnmi", "AT+CNMI?", 5000, buf, err, err_size) && strstr(buf, "+CNMI:")) {
			char *c = strstr(buf, "+CNMI:") + 6;
			int mode = -1, mt = -1;

			if (sscanf(c, " %d , %d", &mode, &mt) == 2 && mode == 0 && mt == 0 &&
			    cmd(p, out, "cnmi_set", "AT+CNMI=2,1,0,0,0", 5000, buf, err, err_size))
				json_object_object_add(out, "cnmi_fixed", json_object_new_boolean(1));
		}
	} else if (!strcmp(op, "delete")) {
		json_object *idx = json_object_object_get(job, "indices"), *done = json_object_new_array(), *failed = json_object_new_array();
		size_t i;

		for (i = 0; idx && i < json_object_array_length(idx); i++) {
			int n = json_object_get_int(json_object_array_get_idx(idx, i)), rc;
			char line[32];

			snprintf(line, sizeof(line), "AT+CMGD=%d", n);
			rc = mm_at_command(p, line, 10000, buf, BUF, err, err_size);
			json_object_array_add(rc == 1 && strstr(buf, "OK") && !strstr(buf, "ERROR") ? done : failed, json_object_new_int(n));
			if (rc < 0)
				break;
		}
		json_object_object_add(out, "deleted", done);
		json_object_object_add(out, "failed", failed);
	} else if (!strcmp(op, "send")) {
		json_object *pdus = json_object_object_get(job, "pdus"), *refs = json_object_new_array();
		size_t i;

		json_object_object_add(out, "refs", refs);
		if (cmd(p, out, "cmgf", "AT+CMGF=0", 5000, buf, err, err_size)) {
			for (i = 0; pdus && i < json_object_array_length(pdus); i++) {
				json_object *one = json_object_array_get_idx(pdus, i);
				char line[32];
				const char *ref;
				int rc;

				snprintf(line, sizeof(line), "AT+CMGS=%d", json_object_get_int(json_object_object_get(one, "len")));
				/* the network answers a submit within seconds; 60 s covers a slow one */
				rc = mm_at_prompt(p, line, jstr(one, "pdu") ? jstr(one, "pdu") : "", 60000, buf, BUF, err, err_size);
				if (rc != 1 || !(ref = strstr(buf, "+CMGS:"))) {
					json_object_object_add(out, "answer", json_object_new_string(buf));
					break;
				}
				json_object_array_add(refs, json_object_new_int(atoi(ref + 6)));
			}
		}
	} else if (!strcmp(op, "clear")) {
		/* everything in the storage reads come from (CPMS mem1), parts the listing never showed too (field
		 * 2026-10-17, an E171: 5 of 5 used, one message listed): what it holds, the delete, what it holds then */
		char after[96], m1[8] = "", m2[8] = "", m3[8] = "", line[64];
		const char *h;

		if (cmd(p, out, "before", "AT+CPMS?", 5000, buf, err, err_size) &&
		    cmd(p, out, "cmgd", "AT+CMGD=1,4", 30000, buf, err, err_size) && (h = strstr(json_object_get_string(
		    json_object_object_get(out, "before")), "+CPMS:")) &&
		    sscanf(h, "+CPMS: \"%7[A-Z]\",%*d,%*d,\"%7[A-Z]\",%*d,%*d,\"%7[A-Z]\"", m1, m2, m3) == 3 && strcmp(m1, m3)) {
			/* incoming messages go to mem3: where it differs from the storage read and cleared (mem1), it is
			 * cleared too, then the storages are set back as they were (review 2026-10-17) */
			snprintf(line, sizeof(line), "AT+CPMS=\"%s\",\"%s\",\"%s\"", m3, m2, m3);
			if (cmd(p, out, "to_mem3", line, 10000, buf, err, err_size))
				cmd(p, out, "cmgd3", "AT+CMGD=1,4", 30000, buf, err, err_size);
			snprintf(line, sizeof(line), "AT+CPMS=\"%s\",\"%s\",\"%s\"", m1, m2, m3);
			cmd(p, out, "back", line, 10000, buf, after, sizeof(after));
		}
		/* the count afterwards is for the page; its failure must not hide the delete's */
		cmd(p, out, "cpms", "AT+CPMS?", 5000, buf, after, sizeof(after));
	} else if (!strcmp(op, "storage")) {
		/* the T99's one-shot storage switch (a field recipe from a router vendor, F0.6.0.0.6+ after a flash; docs/HANDOFF.md 150): SIM for
		 * all three storages, then the modem's own again, then what the modem says it uses */
		if (cmd(p, out, "sm", "AT+CPMS=\"SM\",\"SM\",\"SM\"", 10000, buf, err, err_size))
			cmd(p, out, "me", "AT+CPMS=\"ME\",\"ME\",\"ME\"", 10000, buf, err, err_size);
		cmd(p, out, "cpms", "AT+CPMS?", 5000, buf, err, err_size);
	} else if (!strcmp(op, "center")) {
		/* the SIM's SMS centre: written when a value came, then read back. In a UCS2 character set the
		 * number goes as hex UTF-16 (the page parses either form back) */
		const char *value = jstr(job, "value");
		bool ucs2 = cmd(p, out, "cscs", "AT+CSCS?", 5000, buf, err, err_size) && strstr(buf, "UCS2");

		if (value && *value) {
			char line[200], num[120] = "";
			size_t i, n = 0;

			for (i = 0; value[i] && n + 5 < sizeof(num); i++)
				n += (size_t)snprintf(num + n, sizeof(num) - n, ucs2 ? "%04X" : "%c", (unsigned char)value[i]);
			snprintf(line, sizeof(line), "AT+CSCA=\"%s\",%d", num, value[0] == '+' ? 145 : 129);
			cmd(p, out, "set", line, 10000, buf, err, err_size);
		}
		cmd(p, out, "csca", "AT+CSCA?", 5000, buf, err, err_size);
	} else if (!strcmp(op, "ussd")) {
		const char *verb = jstr(job, "verb"), *value = jstr(job, "value");
		int wait = json_object_get_int(json_object_object_get(job, "wait_ms"));

		if (verb && !strcmp(verb, "cancel")) {
			cmd(p, out, "cusd", "AT+CUSD=2", 10000, buf, err, err_size);
		} else if (value) {
			char line[160];

			/* the code as text (U4); the answer comes as +CUSD, during the command or after it */
			snprintf(line, sizeof(line), "AT+CUSD=1,\"%s\",15", value);
			if (cmd(p, out, "cusd", line, 10000, buf, err, err_size) && strstr(buf, "OK") && !strstr(buf, "+CUSD:")) {
				if (mm_at_wait_line(p, "+CUSD:", wait > 0 ? wait : 40000, buf, BUF))
					json_object_object_add(out, "urc", json_object_new_string(buf));
				else
					snprintf(err, err_size, "no answer from the network in time");
			}
		}
	} else {
		snprintf(err, err_size, "unknown operation %s", op);
	}
	free(buf);
	mm_at_close(p);
	return out;
}
