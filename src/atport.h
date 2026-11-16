#ifndef MEDVEMODEM_ATPORT_H
#define MEDVEMODEM_ATPORT_H

#include <stddef.h>
#include <json-c/json.h>

/*
 * One AT exchange on a serial node (spec 11.5). Blocking with bounded waits;
 * the daemon runs it in a worker process under a lease, so a stuck USB tty
 * cannot stall the engine.
 *
 * The node is opened non-blocking; terminal settings are set raw for the
 * exchange and restored. Exclusivity comes from the engine's lease, not from
 * TIOCEXCL, which would outlive a killed worker. Echo and other settings of
 * the modem are not changed. A late answer to another command (a killed
 * worker's) is recognised by its echo and dropped; settle_ms > 0 first reads and
 * discards whatever the port still sends for that long.
 *
 * Returns {"responses": {"<command>": raw text}, "complete": n} with one entry per
 * command sent; a command without a final result code within timeout_ms ends
 * the exchange and sets err. NULL with err when the node cannot be opened.
 * A command "~wait <ms>" (up to 10 s) is not sent: the exchange pauses and answers OK.
 */
#define MM_AT_WAIT "~wait "
json_object *mm_at_exchange(const char *path, const char *const *cmds, int ncmds, int timeout_ms, int settle_ms,
			    char *err, size_t err_size);

/*
 * The same exchange as a session: one open node for several commands (the eSIM
 * APDU bridge keeps a logical channel open across them). mm_at_command returns
 * 1 with a final result code, 0 without one within timeout_ms, -1 when the node
 * failed; buf holds what arrived either way.
 */
struct mm_at_port;
struct mm_at_port *mm_at_open(const char *path, int settle_ms, char *err, size_t err_size);
int mm_at_command(struct mm_at_port *p, const char *cmd, int timeout_ms, char *buf, size_t size, char *err,
		  size_t err_size);
void mm_at_close(struct mm_at_port *p);

/*
 * A command that prompts ("> ") for a payload, as AT+CMGS in PDU mode: the
 * payload follows the prompt, ended by Ctrl-Z. 1 with a final result code, 0
 * without one within timeout_ms (ESC is sent to leave the prompt), -1 when the
 * node failed; buf holds what arrived.
 */
int mm_at_prompt(struct mm_at_port *p, const char *cmd, const char *payload, int timeout_ms, char *buf, size_t size,
		 char *err, size_t err_size);

/* An unsolicited line starting with prefix ("+CUSD:"): 1 when a whole one came within timeout_ms, else 0; buf holds it. */
int mm_at_wait_line(struct mm_at_port *p, const char *prefix, int timeout_ms, char *buf, size_t size);

#endif
