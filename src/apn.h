#ifndef MEDVEMODEM_APN_H
#define MEDVEMODEM_APN_H

#include <json-c/json.h>

/*
 * APN suggestion for a SIM (spec 25.9.10, HANDOFF 65). Pure: the daemon reads
 * the files. db is providers.tsv (apn-autoconfig, from GNOME MBPI + AOSP; TAB
 * columns: mccmnc imsi_pattern iccid_pattern gid1 spn provider apn priority
 * username password auth ip_type; "-" is any), overrides the overrides.list
 * ("plmn:250-62:tt", "name:тинькоф:tt").
 *
 * The SIM's identity is the home network (operator code from the SIM), the
 * IMSI when known (never kept or logged by the caller beyond this call), the
 * SPN (operator name on the SIM), GID1 and ICCID; network is the network the
 * modem is registered in (a fallback for a SIM that told nothing).
 */
struct mm_apn_sim {
	const char *home, *imsi, *spn, *gid1, *iccid, *network;
};

/*
 * In order: an override for the home network; the database row matching the
 * SIM most specifically (IMSI pattern, GID1, SPN, ICCID over the bare
 * network code; then the lower priority); an override by the SPN; the
 * database by the registered network. {"apn", "source": "override" |
 * "database" | "name" | "network", "provider"?, "username"?, "password"?,
 * "auth"?, "ip_type"?} or NULL. New object.
 */
json_object *mm_apn_pick(const char *db, const char *overrides, const struct mm_apn_sim *sim);

/* {"version": "2026.09.21" | null, "count": rows} of a providers.tsv text. New object. */
json_object *mm_apn_db_info(const char *db);

#endif
