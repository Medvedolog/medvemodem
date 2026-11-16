#ifndef MEDVEMODEM_PDU_H
#define MEDVEMODEM_PDU_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/*
 * SMS in PDU mode (3GPP TS 23.040, 23.038) for the AT path of SMS (spec 38,
 * spec 38): a modem whose AT port is the engine's, or the
 * fallback when ModemManager cannot send. Pure; no modem state changes here.
 */

/* One PDU as AT+CMGL / AT+CMGR print it (with the SMSC part). Returns
 * {type deliver|submit|status, number, text (null for data), binary, timestamp
 * (ISO 8601 with offset), epoch, class, concat {ref, total, part} | null} or
 * {"error": ...}. */
json_object *mm_pdu_decode(const char *hex);

/* SMS-SUBMIT PDUs for a text (GSM 7-bit when it fits the alphabet, else UCS-2;
 * several parts with a concatenation header when long). Writes up to max hex
 * strings (each with the default-SMSC octet "00") into pdus[i] (size bytes each)
 * and their AT+CMGS lengths (TPDU octets) into lens[i]. Returns the number of
 * parts, or -1 (bad number, text not valid UTF-8, too many parts, no room). */
int mm_pdu_submit(const char *number, const char *smsc, const char *text, int ref, char (*pdus)[400], int *lens, int max);

/* The text of a USSD string as +CUSD gives it: dcs per 23.038 (15 GSM 7-bit,
 * 72 UCS-2, …); hex is decoded when the string is hex (UCS-2 in either byte
 * order, packed GSM 7-bit), otherwise taken as text. */
void mm_ussd_text(const char *str, int dcs, char *out, size_t size);

/* A +CUSD line (27.007 7.15): {m, reply} — m 0 done, 1 the network waits for a
 * reply, 2 ended by the network, 4 not supported, 5 timed out; reply decoded
 * with mm_ussd_text, absent when the line has none. NULL when it is no +CUSD. */
json_object *mm_ussd_urc(const char *line);

/* `AT+CMGL=4` and `AT+CPMS?` answers to {storage, used, total, messages: [...]}:
 * every PDU decoded, parts of one message joined (id = the lowest index, parts =
 * all indices, have/total when some are missing), state from the listing's stat
 * (received, sent, stored). */
json_object *mm_sms_at_list(const char *cpms_raw, const char *cmgl_raw);

#endif
