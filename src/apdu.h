#ifndef MEDVEMODEM_APDU_H
#define MEDVEMODEM_APDU_H

#include <stdbool.h>
#include <stddef.h>

/*
 * APDU to an eUICC over AT logical channels (3GPP TS 27.007 +CCHO/+CGLA/+CCHC),
 * the pure part of the eSIM bridge (docs/ESIM-INTEGRATION.md). The bridge, not
 * lpac's own AT driver, talks to the tty: that driver stalls on the long CGLA
 * chains of some firmware (FM350-GL field logs).
 */

/* ISD-R application identifier (SGP.22): what the probe opens a channel to */
#define MM_APDU_ISDR_AID "A0000005591010FFFFFFFF8900000100"

/* longest command APDU accepted from lpac, in hex characters (5 + 255 + 1 bytes) */
#define MM_APDU_MAX_HEX 522

/*
 * s is an even-length string of hex digits of at most max characters. Anything
 * else from lpac (quotes, separators, line ends) never reaches the AT command.
 */
bool mm_apdu_hex_ok(const char *s, size_t max);

/*
 * The channel number in a +CCHO answer, -1 without one. Firmware differs:
 * the standard "+CCHO: <n>" and a bare "<n>" line (Fibocom FM350-GL) are both
 * accepted; *bare tells which. The command's echo is not taken for a number.
 */
int mm_apdu_ccho_channel(const char *answer, bool *bare);

/*
 * The response APDU (data + SW1SW2) of a +CGLA answer, upper-case hex, quoted or
 * not. Returns its length in characters, -1 when there is none.
 */
int mm_apdu_cgla_response(const char *answer, char *hex, size_t size);

/*
 * AT+CGLA=<channel>,<length>,"<apdu>": the length counts the hex characters of
 * the command (27.007; FM350 does not answer a byte count). -1 on a bad APDU.
 */
int mm_apdu_cgla_command(int channel, const char *apdu, char *out, size_t size);

/*
 * What a CCHO probe of one port showed: "euicc" (a channel opened), "refused"
 * (the port answered with an error: it handles the command, no ISD-R behind
 * it), "silent" (no final result: the port swallows logical-channel commands).
 */
const char *mm_apdu_probe_verdict(const char *answer, int complete);

#endif
