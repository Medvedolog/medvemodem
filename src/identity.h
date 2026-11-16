#ifndef MEDVEMODEM_IDENTITY_H
#define MEDVEMODEM_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

bool mm_valid_serial(const char *s);
/* Devices sharing a serial fall back to their port identity. */
void mm_resolve_serial_collisions(json_object *arr);


/* SHA-256 (FIPS 180-4) of data as 64 hex digits */
void mm_sha256_hex(const void *data, size_t len, bool upper, char out[65]);

/*
 * The IMEI key (HANDOFF 167): a modem's name and settings (config modem, device, esim) follow it into
 * another USB port by "<tag>:<hash>", 4 + 16 hex digits of SHA-256 over a salt of this router's own and the
 * IMEI. The IMEI itself is never written down; the tag names the salt, so a key made with another salt (a
 * new router, a lost salt file) is made again rather than trusted.
 */
#define MM_IMEI_KEY_SIZE 22

/* The router's salt from path, made (16 random bytes as hex, 0600) when missing. false: no randomness. */
bool mm_salt_load(const char *path, char *salt, size_t size, bool *kept);
/* 14-16 digits and not a firmware placeholder */
bool mm_valid_imei(const char *s);
void mm_imei_key(const char *salt, const char *imei, char out[MM_IMEI_KEY_SIZE]);
/* the sections keyed to `id` that hold the key `old` get `key` instead: a stick first keyed by its USB identity is keyed
 * by its IMEI once its web gives one (set ops for mm_uci_apply, a new array) */
json_object *mm_identity_rekey(json_object *img, const char *id, const char *old, const char *key);
/* a section keyed to `id` holds a key other than `key` (an IMEI key where the USB identity's is offered) */
bool mm_identity_keyed_other(json_object *img, const char *id, const char *key);

/*
 * The UCI ops (package medvemodem) that make the sections follow the modems present ({device_id: IMEI key}):
 * a section keyed to a modem in another port moves there; one keyed to a modem that is away while another
 * sits in its port is parked ("parked:<key>") until its own comes back; an unkeyed one of a modem whose key
 * is known gets the key. Two modems with one key move nothing. New array.
 */
json_object *mm_identity_follow(json_object *img, json_object *present);

/*
 * One IMEI on two modems (an operator who wrote the same IMEI into both, a tester 2026-11-03): its key names no single
 * modem, and while one of the two is away the other would take the away one's name and settings. A key seen on two
 * present modems at once goes into `shared` (key -> true, kept by the caller) and every shared key out of `present`:
 * those modems keep their settings by the USB port. True when `shared` grew.
 */
bool mm_identity_shared(json_object *present, json_object *shared);

/*
 * The SIM key (the owner, 2026-11-05: «дополнительный детект модема в другом USB», «пусть будет соль»): a salted hash
 * of the modem's identity (its IMEI, or its USB identity when it has none) with its SIM's IMSI, so two modems with one
 * IMEI are told apart by their SIMs and a modem without an IMEI is found in another port by the SIM in it. It moves
 * a section only: `sims` ({device_id: SIM key} of the modems present), `present_ids` (every device present). A
 * section of a modem present gets its modem's SIM key (sim_key); a section of a modem away goes to the one present
 * modem with its SIM key that has no section of that kind yet. A new SIM in the same modem changes the key and moves
 * nothing; a section is never parked by it. Set ops, a new array.
 */
json_object *mm_identity_follow_sim(json_object *img, json_object *sims, json_object *present_ids);

#endif
