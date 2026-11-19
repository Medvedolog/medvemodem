/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SIMID_H
#define SIMID_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <json-c/json.h>

/*
 * The SIM's identity read by the engine (r64): the IMSI and the ICCID of a modem ModemManager does not hold
 * (QModem's, or the native AT path). Both are answers to the operator's page and the SIM's label, traffic
 * counter and APN pick; they are kept in RAM only, never logged, and the counters on flash take a hash.
 */

/* AT+CIMI: the IMSI, 14 or 15 digits, in whatever line of the answer */
bool simid_imsi_from_cimi(const char *raw, char *out, size_t size);

/* AT+CRSM=176,12258,0,0,10 (EF_ICCID, 27.007): "+CRSM: 144,0,\"<hex>\"" with the digits swapped in each byte and
 * F as filler: 18 to 20 digits, or false */
bool simid_iccid_from_crsm(const char *raw, char *out, size_t size);

/*
 * The SIM's home operator code (MCC+MNC, «25011» for Yota) from its IMSI: the first 5 digits, 6 where the country's
 * operators have three-digit network codes (North America, India's 405, parts of Latin America and the Caribbean).
 * Only the code leaves: the IMSI itself stays where it was read (the owner, 2026-11-05: Yota's SIM on MegaFon's
 * network, the HiLink's web said «МегаФон» while it picked Yota's APN).
 */
static inline bool simid_plmn_of_imsi(const char *imsi, char *out, size_t size)
{
	static const char three[][4] = { "302", "310", "311", "312", "313", "314", "315", "316", "334", "338", "342",
					 "344", "346", "348", "354", "356", "358", "360", "365", "366", "376", "405",
					 "708", "722", "732", "750" };
	size_t n = 5, i, len = imsi ? strlen(imsi) : 0;

	if (size < 7 || len < 14 || len > 15 || strspn(imsi, "0123456789") != len)
		return false;
	for (i = 0; i < sizeof(three) / sizeof(three[0]); i++)
		if (!strncmp(imsi, three[i], 3))
			n = 6;
	memcpy(out, imsi, n);
	out[n] = 0;
	return true;
}

#endif
