#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <json-c/json.h>

#include "identity.h"

/* Firmware placeholders shared by many units; never a physical identity. */
static const char *const placeholder_serials[] = {
	"unknown", "none", "null", "serial", "0",
	"12345678", "123456789", "0123456789", "1234567890",
	"0123456789abcdef", "123456789abcdef", "1234567890abcdef",
	"abcdef0123456789", "0123456789abcdef0",
	NULL,
};

bool mm_valid_serial(const char *s)
{
	size_t i;
	bool any_nonzero = false, all_same = true;

	if (!s || strlen(s) < 4)
		return false;
	for (i = 0; placeholder_serials[i]; i++)
		if (!strcasecmp(s, placeholder_serials[i]))
			return false;
	for (i = 0; s[i]; i++) {
		if ((unsigned char)s[i] <= 32 || s[i] == '@')
			return false;
		if (s[i] != '0' && s[i] != '-')
			any_nonzero = true;
		if (s[i] != s[0])
			all_same = false;
	}
	return any_nonzero && !all_same;
}

/* Duplicate USB serials are evidence, not a unique persistent identity. */
void mm_resolve_serial_collisions(json_object *arr)
{
	for (size_t i = 0; i < json_object_array_length(arr); i++) {
		json_object *dev = json_object_array_get_idx(arr, i), *serial = NULL;
		if (!json_object_object_get_ex(dev, "serial", &serial))
			continue;
		for (size_t j = 0; j < json_object_array_length(arr); j++) {
			json_object *peer = json_object_array_get_idx(arr, j), *other = NULL;
			if (i == j || !json_object_object_get_ex(peer, "serial", &other) ||
			    strcmp(json_object_get_string(serial), json_object_get_string(other)))
				continue;
			json_object *port = NULL;
			json_object_object_get_ex(dev, "port_id", &port);
			json_object_object_add(dev, "device_id", json_object_get(port));
			json_object_object_add(dev, "identity_strength", json_object_new_string("serial-collision"));
			json_object_object_add(dev, "identity_ambiguous", json_object_new_boolean(true));
			break;
		}
	}
}

/* ---- SHA-256 (FIPS 180-4): the web logins hash the password, the IMEI key hashes the IMEI ---- */

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const unsigned char *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, k, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (; i < 64; i++)
		w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
		       (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
	a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; k = h[7];
	for (i = 0; i < 64; i++) {
		t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

void mm_sha256_hex(const void *data, size_t len, bool upper, char out[65])
{
	uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	const unsigned char *p = data;
	unsigned char tail[128];
	unsigned long long bits = (unsigned long long)len * 8;
	size_t rest, n;
	int i;

	for (; len >= 64; p += 64, len -= 64)
		sha256_block(h, p);
	rest = len;
	memset(tail, 0, sizeof(tail));
	memcpy(tail, p, rest);
	tail[rest] = 0x80;
	n = rest < 56 ? 64 : 128;
	for (i = 0; i < 8; i++)
		tail[n - 1 - i] = (unsigned char)(bits >> (8 * i));
	sha256_block(h, tail);
	if (n == 128)
		sha256_block(h, tail + 64);
	for (i = 0; i < 8; i++)
		snprintf(out + 8 * i, 9, upper ? "%08X" : "%08x", h[i]);
	explicit_bzero(tail, sizeof(tail));
}

/* ---- the IMEI key: settings that follow a modem with no USB serial of its own ---- */

bool mm_salt_load(const char *path, char *salt, size_t size, bool *kept)
{
	FILE *f = fopen(path, "r");
	unsigned char rnd[16];
	char dir[512], tmp[600];
	const char *slash;
	size_t i;
	int fd;
	bool ok;

	if (kept)
		*kept = false;
	if (!size)
		return false;
	salt[0] = '\0';
	if (f) {
		/* an empty file (power lost while it was written) leaves salt untouched by fgets: it stays "" (review 2026-11-15) */
		ok = fgets(salt, (int)size, f) != NULL;
		fclose(f);
		if (!ok)
			salt[0] = '\0';
		salt[strcspn(salt, "\n")] = '\0';
		if (ok && strlen(salt) >= 16) {
			if (kept)
				*kept = true;
			return true;
		}
	}
	if (size < 2 * sizeof(rnd) + 1)
		return false;
	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0 || read(fd, rnd, sizeof(rnd)) != (ssize_t)sizeof(rnd)) {
		if (fd >= 0)
			close(fd);
		return false;
	}
	close(fd);
	for (i = 0; i < sizeof(rnd); i++)
		snprintf(salt + 2 * i, 3, "%02x", rnd[i]);
	slash = strrchr(path, '/');
	if (slash && (size_t)(slash - path) < sizeof(dir)) {
		snprintf(dir, sizeof(dir), "%.*s", (int)(slash - path), path);
		mkdir(dir, 0755);
	}
	/* written beside and renamed over, flushed first: a power cut leaves the old file or the whole new one */
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	ok = fd >= 0 && write(fd, salt, strlen(salt)) == (ssize_t)strlen(salt) && write(fd, "\n", 1) == 1 && !fsync(fd);
	if (fd >= 0)
		close(fd);
	ok = ok && !rename(tmp, path);
	if (!ok)
		unlink(tmp);
	if (kept)
		*kept = ok;
	return true;	/* a salt for this run in any case; *kept says whether the next run has the same */
}

bool mm_valid_imei(const char *s)
{
	size_t i, n = s ? strlen(s) : 0;
	bool all_same = true;

	if (n < 14 || n > 16)
		return false;
	for (i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9')
			return false;
		if (s[i] != s[0])
			all_same = false;
	}
	/* firmware placeholders: one digit over and over, or 0123456789... */
	return !all_same && strncmp(s, "12345678901234", 14) && strncmp(s, "01234567890123", 14);
}

void mm_imei_key(const char *salt, const char *imei, char out[MM_IMEI_KEY_SIZE])
{
	char buf[256], h[65], tag[65];

	snprintf(buf, sizeof(buf), "medvemodem-salt-tag:%s", salt);
	mm_sha256_hex(buf, strlen(buf), false, tag);
	snprintf(buf, sizeof(buf), "%s:imei:%s", salt, imei);
	mm_sha256_hex(buf, strlen(buf), false, h);
	explicit_bzero(buf, sizeof(buf));
	snprintf(out, MM_IMEI_KEY_SIZE, "%.4s:%.16s", tag, h);
}

/* the sections that belong to one modem, and the option naming it */
static const struct { const char *type, *opt; } keyed[] = {
	{ "modem", "device_id" }, { "device", "id" }, { "esim", "device_id" },
};

static const char *ostr(json_object *o, const char *k)
{
	json_object *v = NULL;

	return json_object_object_get_ex(o, k, &v) && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

/* the device_id now holding key, NULL when none or two (two modems with one IMEI tell nothing apart) */
static const char *holder_of(json_object *present, const char *key)
{
	const char *at = NULL;

	json_object_object_foreach(present, id, k) {
		if (strcmp(json_object_get_string(k), key))
			continue;
		if (at)
			return NULL;
		at = id;
	}
	return at;
}

static void set_op(json_object *ops, const char *sec, const char *opt, const char *value, const char *was)
{
	json_object *op = json_object_new_object();

	json_object_object_add(op, "op", json_object_new_string("set"));
	json_object_object_add(op, "package", json_object_new_string("medvemodem"));
	json_object_object_add(op, "section", json_object_new_string(sec));
	json_object_object_add(op, "option", json_object_new_string(opt));
	json_object_object_add(op, "value", json_object_new_string(value));
	json_object_object_add(op, "expect", was ? json_object_new_string(was) : NULL);
	json_object_array_add(ops, op);
}

json_object *mm_identity_rekey(json_object *img, const char *id, const char *old, const char *key)
{
	json_object *ops = json_object_new_array();
	size_t t;

	if (!img || !id || !old || !key || !strcmp(old, key))
		return ops;
	json_object_object_foreach(img, sec, s) {
		const char *type = ostr(s, ".type"), *opt = NULL;

		for (t = 0; type && t < sizeof(keyed) / sizeof(keyed[0]); t++)
			if (!strcmp(type, keyed[t].type))
				opt = keyed[t].opt;
		if (opt && ostr(s, opt) && !strcmp(ostr(s, opt), id) && ostr(s, "imei_key") && !strcmp(ostr(s, "imei_key"), old))
			set_op(ops, sec, "imei_key", key, old);
	}
	return ops;
}

bool mm_identity_keyed_other(json_object *img, const char *id, const char *key)
{
	size_t t;

	if (!img || !id || !key)
		return false;
	json_object_object_foreach(img, sec, s) {
		const char *type = ostr(s, ".type"), *opt = NULL;

		(void)sec;
		for (t = 0; type && t < sizeof(keyed) / sizeof(keyed[0]); t++)
			if (!strcmp(type, keyed[t].type))
				opt = keyed[t].opt;
		if (opt && ostr(s, opt) && !strcmp(ostr(s, opt), id) && ostr(s, "imei_key") && strcmp(ostr(s, "imei_key"), key))
			return true;
	}
	return false;
}

/* the other section of `type` keyed to `id`, or NULL */
static const char *other_section(json_object *img, const char *type, const char *opt, const char *id, const char *not, json_object **out)
{
	json_object_object_foreach(img, sec, s) {
		if (strcmp(sec, not) && ostr(s, ".type") && !strcmp(ostr(s, ".type"), type) && ostr(s, opt) && !strcmp(ostr(s, opt), id)) {
			*out = s;
			return sec;
		}
	}
	return NULL;
}

/*
 * The modem's section moves to the port it is in now, where the engine may have made a section of its own meanwhile
 * (a tester's MF833V, 2026-11-08: after a reboot on another port the engine's new `device` section had no `adopt`, the
 * old one with `adopt '1'` stayed behind, and the card said «no interface»). Two sections for one modem never: what
 * the new one lacks comes from the old one (the operator's settings), then the old one goes.
 */
static void move_or_merge(json_object *ops, json_object *img, const char *type, const char *sec, json_object *s, const char *opt,
			  const char *to, const char *from)
{
	json_object *t = NULL, *op;
	const char *tsec = other_section(img, type, opt, to, sec, &t), *tkey = tsec ? ostr(t, "imei_key") : NULL;

	/* a section there keyed to another modem is that modem's (two clones swapped): it moves itself, no merge */
	if (!tsec || (tkey && ostr(s, "imei_key") && strcmp(tkey, ostr(s, "imei_key")))) {
		set_op(ops, sec, opt, to, from);
		return;
	}
	json_object_object_foreach(s, k, v) {
		json_object *have = NULL;

		if (k[0] == '.' || !strcmp(k, opt) || json_object_object_get_ex(t, k, &have))
			continue;
		op = json_object_new_object();
		json_object_object_add(op, "op", json_object_new_string("set"));
		json_object_object_add(op, "package", json_object_new_string("medvemodem"));
		json_object_object_add(op, "section", json_object_new_string(tsec));
		json_object_object_add(op, "option", json_object_new_string(k));
		json_object_object_add(op, "value", json_object_get(v));
		json_object_object_add(op, "expect", NULL);
		json_object_array_add(ops, op);
	}
	op = json_object_new_object();
	json_object_object_add(op, "op", json_object_new_string("delete-section"));
	json_object_object_add(op, "package", json_object_new_string("medvemodem"));
	json_object_object_add(op, "section", json_object_new_string(sec));
	/* mm_cfg_check deletes a section only as it was read (review 2026-11-15: without it every merge was skipped) */
	json_object_object_add(op, "expect", json_object_get(s));
	json_object_array_add(ops, op);
}

json_object *mm_identity_follow(json_object *img, json_object *present)
{
	json_object *ops = json_object_new_array();
	const char *any = NULL;
	size_t t;

	json_object_object_foreach(present, pid, pk) {
		(void)pid;
		any = json_object_get_string(pk);
		break;
	}
	if (!img || !any)
		return ops;
	json_object_object_foreach(img, sec, s) {
		const char *type = ostr(s, ".type"), *opt = NULL, *id, *key, *here, *holder;

		for (t = 0; type && t < sizeof(keyed) / sizeof(keyed[0]); t++)
			if (!strcmp(type, keyed[t].type))
				opt = keyed[t].opt;
		if (!opt || !(id = ostr(s, opt)))
			continue;
		key = ostr(s, "imei_key");
		here = ostr(present, id);
		if (here && !holder_of(present, here))
			here = NULL;
		/* a key made with another salt (a new router, a lost salt) says nothing: made again below */
		if (key && strlen(key) == MM_IMEI_KEY_SIZE - 1 && !strncmp(key, any, 5)) {
			holder = holder_of(present, key);
			if (holder && strcmp(holder, id))
				move_or_merge(ops, img, type, sec, s, opt, holder, id);	/* its modem is in another port now */
			else if (!holder && here) {
				char parked[64];

				/* another modem sits where this one was: kept for its own modem, given to no other */
				snprintf(parked, sizeof(parked), "parked:%s", key);
				set_op(ops, sec, opt, parked, id);
			}
		} else if (here) {
			set_op(ops, sec, "imei_key", here, key);
		}
	}
	return ops;
}

bool mm_identity_shared(json_object *present, json_object *shared)
{
	json_object *seen = json_object_new_object(), *drop = json_object_new_array();
	bool grew = false;
	size_t i;

	if (!present || !shared) {
		json_object_put(seen);
		json_object_put(drop);
		return false;
	}
	json_object_object_foreach(present, id, k) {
		const char *key = json_object_get_string(k);

		(void)id;
		if (!key)
			continue;
		if (json_object_object_get(seen, key) && !json_object_object_get(shared, key)) {
			json_object_object_add(shared, key, json_object_new_boolean(1));
			grew = true;
		}
		json_object_object_add(seen, key, json_object_new_boolean(1));
	}
	json_object_object_foreach(present, pid, pk) {
		if (json_object_get_string(pk) && json_object_object_get(shared, json_object_get_string(pk)))
			json_object_array_add(drop, json_object_new_string(pid));
	}
	for (i = 0; i < json_object_array_length(drop); i++)
		json_object_object_del(present, json_object_get_string(json_object_array_get_idx(drop, i)));
	json_object_put(seen);
	json_object_put(drop);
	return grew;
}

/* the section of `type` keyed to `id` already */
static bool has_section(json_object *img, const char *type, const char *opt, const char *id)
{
	json_object_object_foreach(img, sec, s) {
		(void)sec;
		if (ostr(s, ".type") && !strcmp(ostr(s, ".type"), type) && ostr(s, opt) && !strcmp(ostr(s, opt), id))
			return true;
	}
	return false;
}

json_object *mm_identity_follow_sim(json_object *img, json_object *sims, json_object *present_ids)
{
	json_object *ops = json_object_new_array();
	size_t t;

	if (!img || !sims || !present_ids)
		return ops;
	json_object_object_foreach(img, sec, s) {
		const char *type = ostr(s, ".type"), *opt = NULL, *id, *mine, *key, *holder = NULL;
		int n = 0;

		for (t = 0; type && t < sizeof(keyed) / sizeof(keyed[0]); t++)
			if (!strcmp(type, keyed[t].type))
				opt = keyed[t].opt;
		if (!opt || !(id = ostr(s, opt)) || !strncmp(id, "parked:", 7))
			continue;
		key = ostr(s, "sim_key");
		if (json_object_object_get(present_ids, id)) {
			/* its modem is here: the key follows the SIM in it now, or stays when no SIM was read */
			if ((mine = ostr(sims, id)) && (!key || strcmp(key, mine)))
				set_op(ops, sec, "sim_key", mine, key);
			continue;
		}
		if (!key)
			continue;
		json_object_object_foreach(sims, pid, pk) {
			if (json_object_get_string(pk) && !strcmp(json_object_get_string(pk), key)) {
				holder = pid;
				n++;
			}
		}
		if (n == 1 && !has_section(img, type, opt, holder))
			set_op(ops, sec, opt, holder, id);
	}
	return ops;
}
