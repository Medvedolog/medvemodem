#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pdu.h"
#include "sms.h"

/* GSM 03.38 default alphabet; 0x1B is the escape to the extension table */
static const long gsm7[128] = {
	0x40, 0xa3, 0x24, 0xa5, 0xe8, 0xe9, 0xf9, 0xec, 0xf2, 0xc7, 0x0a, 0xd8, 0xf8, 0x0d, 0xc5, 0xe5,
	0x394, 0x5f, 0x3a6, 0x393, 0x39b, 0x3a9, 0x3a0, 0x3a8, 0x3a3, 0x398, 0x39e, -1, 0xc6, 0xe6, 0xdf, 0xc9,
	0x20, 0x21, 0x22, 0x23, 0xa4, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0xa1, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0xc4, 0xd6, 0xd1, 0xdc, 0xa7,
	0xbf, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0xe4, 0xf6, 0xf1, 0xfc, 0xe0,
};
static const struct { unsigned char code; long cp; } gsm7_ext[] = {
	{ 0x0a, 0x0c }, { 0x14, 0x5e }, { 0x28, 0x7b }, { 0x29, 0x7d }, { 0x2f, 0x5c },
	{ 0x3c, 0x5b }, { 0x3d, 0x7e }, { 0x3e, 0x5d }, { 0x40, 0x7c }, { 0x65, 0x20ac },
};

struct text {
	char *s;
	size_t len, cap;
};

static void put_cp(struct text *t, long cp)
{
	char u[4];
	int n;

	if (cp < 0x80)
		u[0] = (char)cp, n = 1;
	else if (cp < 0x800)
		u[0] = (char)(0xc0 | (cp >> 6)), u[1] = (char)(0x80 | (cp & 0x3f)), n = 2;
	else if (cp < 0x10000)
		u[0] = (char)(0xe0 | (cp >> 12)), u[1] = (char)(0x80 | ((cp >> 6) & 0x3f)), u[2] = (char)(0x80 | (cp & 0x3f)), n = 3;
	else
		u[0] = (char)(0xf0 | (cp >> 18)), u[1] = (char)(0x80 | ((cp >> 12) & 0x3f)),
		u[2] = (char)(0x80 | ((cp >> 6) & 0x3f)), u[3] = (char)(0x80 | (cp & 0x3f)), n = 4;
	if (t->len + (size_t)n + 1 > t->cap) {
		size_t cap = t->cap ? t->cap * 2 : 256;
		char *ns;

		while (cap < t->len + (size_t)n + 1)
			cap *= 2;
		if (!(ns = realloc(t->s, cap)))
			return;
		t->s = ns;
		t->cap = cap;
	}
	memcpy(t->s + t->len, u, (size_t)n);
	t->len += (size_t)n;
	t->s[t->len] = '\0';
}

static int hexval(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* bytes of a hex string; -1 when it is not one */
static int unhex(const char *hex, unsigned char *out, size_t max)
{
	size_t n = hex ? strlen(hex) : 0, i;

	while (n && (hex[n - 1] == '\r' || hex[n - 1] == '\n' || hex[n - 1] == ' '))
		n--;
	if (!n || n % 2 || n / 2 > max)
		return -1;
	for (i = 0; i < n; i += 2) {
		int a = hexval(hex[i]), b = hexval(hex[i + 1]);

		if (a < 0 || b < 0)
			return -1;
		out[i / 2] = (unsigned char)(a << 4 | b);
	}
	return (int)(n / 2);
}

static int septet(const unsigned char *b, int nbytes, int i)
{
	int bit = i * 7, byte = bit / 8, shift = bit % 8, v;

	if (byte >= nbytes)
		return -1;
	v = b[byte] >> shift;
	if (shift > 1) {
		if (byte + 1 >= nbytes)
			return -1;
		v |= b[byte + 1] << (8 - shift);
	}
	return v & 0x7f;
}

/* septets [from, to) of packed GSM 7-bit to text */
static void gsm7_text(struct text *t, const unsigned char *b, int nbytes, int from, int to)
{
	int i;

	for (i = from; i < to; i++) {
		int c = septet(b, nbytes, i);
		size_t k;

		if (c < 0)
			break;
		if (c != 0x1b) {
			put_cp(t, gsm7[c]);
			continue;
		}
		c = septet(b, nbytes, ++i);
		if (c < 0)
			break;
		for (k = 0; k < sizeof(gsm7_ext) / sizeof(gsm7_ext[0]); k++)
			if (gsm7_ext[k].code == c)
				break;
		put_cp(t, k < sizeof(gsm7_ext) / sizeof(gsm7_ext[0]) ? gsm7_ext[k].cp : 0x20);
	}
}

static void utf16_text(struct text *t, const unsigned char *b, int n, bool le)
{
	int i;

	for (i = 0; i + 1 < n; i += 2) {
		long u = le ? b[i] | b[i + 1] << 8 : b[i] << 8 | b[i + 1];

		if (u >= 0xd800 && u < 0xdc00 && i + 3 < n) {
			long l = le ? b[i + 2] | b[i + 3] << 8 : b[i + 2] << 8 | b[i + 3];

			if (l >= 0xdc00 && l < 0xe000) {
				put_cp(t, 0x10000 + ((u - 0xd800) << 10) + (l - 0xdc00));
				i += 2;
				continue;
			}
		}
		put_cp(t, u >= 0xd800 && u < 0xe000 ? 0xfffd : u);
	}
}

/* 7 for GSM 7-bit, 8 for data, 16 for UCS-2 (23.038 section 4); class or -1 */
static int alphabet(int dcs, int *klass)
{
	*klass = -1;
	if ((dcs & 0x80) == 0) {	/* general data coding, with or without automatic deletion */
		if (dcs & 0x10)
			*klass = dcs & 3;
		if (dcs & 0x20)
			return 8;	/* compressed: no text we can show */
		return ((dcs >> 2) & 3) == 1 ? 8 : ((dcs >> 2) & 3) == 2 ? 16 : 7;
	}
	if ((dcs & 0xf0) == 0xf0) {
		*klass = dcs & 3;
		return dcs & 4 ? 8 : 7;
	}
	return (dcs & 0xf0) == 0xe0 ? 16 : 7;
}

static int semi(unsigned char b)
{
	return (b & 0x0f) * 10 + (b >> 4);
}

/* an address field: digits as semi-octets, or GSM 7-bit for an alphanumeric sender */
static void address(struct text *t, const unsigned char *b, int len_digits, int toa)
{
	int i, nbytes = (len_digits + 1) / 2;

	if ((toa & 0x70) == 0x50) {
		size_t at = t->len;

		gsm7_text(t, b, nbytes, 0, len_digits * 4 / 7);
		/*
		 * A length that counts the last octet's fill bits too («MegaFon» in 14 semi-octets, not 13) reads one septet
		 * more, the zero fill: GSM's «@». No sender name ends with it (review 2026-11-15).
		 */
		if (len_digits % 2 == 0 && t->len > at + 1 && t->s[t->len - 1] == '@')
			t->s[--t->len] = '\0';
		return;
	}
	if ((toa & 0x70) == 0x10)
		put_cp(t, '+');
	for (i = 0; i < len_digits; i++) {
		int d = i % 2 ? b[i / 2] >> 4 : b[i / 2] & 0x0f;

		if (d == 0x0f)
			break;
		put_cp(t, "0123456789*#abc"[d]);
	}
}

json_object *mm_pdu_decode(const char *hex)
{
	unsigned char b[400];
	int n = unhex(hex, b, sizeof(b)), p = 0, fo, mti, alen, toa, dcs = 0, udl, klass = -1, alpha, hdr = 0;
	json_object *o = json_object_new_object(), *concat = NULL;
	struct text num = { 0 }, txt = { 0 };
	long long epoch = -1;
	char ts[96] = "";

#define NEED(k) do { if (p + (k) > n) goto bad; } while (0)
	if (n < 2)
		goto bad;
	p = 1 + b[0];		/* the SMSC */
	NEED(2);
	fo = b[p++];
	mti = fo & 3;
	if (mti == 1 || mti == 2) {
		NEED(1);
		p++;		/* message reference */
	}
	NEED(2);
	alen = b[p++];
	toa = b[p++];
	NEED((alen + 1) / 2);
	address(&num, b + p, alen, toa);
	p += (alen + 1) / 2;
	json_object_object_add(o, "type", json_object_new_string(mti == 0 ? "deliver" : mti == 1 ? "submit" : "status"));
	json_object_object_add(o, "number", json_object_new_string(num.s ? num.s : ""));
	if (mti == 0 || mti == 2) {
		int tz, q, off;
		struct tm tm;

		if (mti == 0) {
			NEED(2);
			p++;		/* protocol identifier */
			dcs = b[p++];
		}
		NEED(7);
		memset(&tm, 0, sizeof(tm));
		tm.tm_year = 100 + semi(b[p]);
		tm.tm_mon = semi(b[p + 1]) - 1;
		tm.tm_mday = semi(b[p + 2]);
		tm.tm_hour = semi(b[p + 3]);
		tm.tm_min = semi(b[p + 4]);
		tm.tm_sec = semi(b[p + 5]);
		tz = b[p + 6];
		q = (tz & 0x07) * 10 + (tz >> 4);	/* quarters of an hour; the sign in bit 3 */
		off = (tz & 0x08 ? -1 : 1) * q * 15;
		epoch = (long long)timegm(&tm) - off * 60LL;
		snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1,
			 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, off < 0 ? '-' : '+', abs(off) / 60, abs(off) % 60);
		p += 7;
		if (mti == 2) {
			json_object_object_add(o, "timestamp", json_object_new_string(ts));
			json_object_object_add(o, "epoch", json_object_new_int64(epoch));
			json_object_object_add(o, "text", NULL);
			json_object_object_add(o, "binary", json_object_new_boolean(false));
			free(num.s);
			return o;
		}
	} else if (mti == 1) {
		int vpf = (fo >> 3) & 3;

		NEED(2);
		p++;
		dcs = b[p++];
		NEED(vpf == 2 ? 1 : vpf ? 7 : 0);
		p += vpf == 2 ? 1 : vpf ? 7 : 0;
	} else {
		goto bad;
	}
	NEED(1);
	udl = b[p++];
	alpha = alphabet(dcs, &klass);
	if ((fo & 0x40) && p < n) {	/* user data header: the concatenation of a long message */
		int udhl = b[p], k = 1;

		hdr = udhl + 1;
		while (k + 1 < hdr && p + k + 1 < n) {
			int iei = b[p + k], ielen = b[p + k + 1];
			const unsigned char *v = b + p + k + 2;

			if (p + k + 2 + ielen > n)
				break;
			if ((iei == 0 && ielen == 3) || (iei == 8 && ielen == 4)) {
				json_object_put(concat);
				concat = json_object_new_object();
				json_object_object_add(concat, "ref", json_object_new_int(iei ? v[0] << 8 | v[1] : v[0]));
				json_object_object_add(concat, "total", json_object_new_int(v[iei ? 2 : 1]));
				json_object_object_add(concat, "part", json_object_new_int(v[iei ? 3 : 2]));
			}
			k += 2 + ielen;
		}
	}
	if (alpha == 7) {
		int skip = hdr ? (hdr * 8 + 6) / 7 : 0;

		gsm7_text(&txt, b + p, n - p, skip, udl);
	} else if (alpha == 16) {
		int len = udl < n - p ? udl : n - p;

		if (len > hdr)
			utf16_text(&txt, b + p + hdr, len - hdr, false);
	}
	json_object_object_add(o, "text", alpha == 8 ? NULL : json_object_new_string(txt.s ? txt.s : ""));
	json_object_object_add(o, "binary", json_object_new_boolean(alpha == 8));
	json_object_object_add(o, "timestamp", ts[0] ? json_object_new_string(ts) : NULL);
	json_object_object_add(o, "epoch", epoch >= 0 ? json_object_new_int64(epoch) : NULL);
	if (klass >= 0) {
		char c[8];

		snprintf(c, sizeof(c), "%d", klass);
		json_object_object_add(o, "class", json_object_new_string(c));
	}
	json_object_object_add(o, "concat", concat);
	free(num.s);
	free(txt.s);
	return o;
bad:
#undef NEED
	free(num.s);
	free(txt.s);
	json_object_put(concat);
	json_object_put(o);
	o = json_object_new_object();
	json_object_object_add(o, "error", json_object_new_string("not a readable SMS PDU"));
	return o;
}

/* ---- sending ---- */

static long next_utf8(const unsigned char **s)
{
	const unsigned char *p = *s;
	long cp;
	int n, i;

	if (p[0] < 0x80)
		n = 0, cp = p[0];
	else if ((p[0] & 0xe0) == 0xc0)
		n = 1, cp = p[0] & 0x1f;
	else if ((p[0] & 0xf0) == 0xe0)
		n = 2, cp = p[0] & 0x0f;
	else if ((p[0] & 0xf8) == 0xf0)
		n = 3, cp = p[0] & 0x07;
	else
		return -1;
	for (i = 1; i <= n; i++) {
		if ((p[i] & 0xc0) != 0x80)
			return -1;
		cp = (cp << 6) | (p[i] & 0x3f);
	}
	*s = p + n + 1;
	return cp;
}

/* text to septets (an extension character takes two); -1 when a character is not in the alphabet */
static int to_gsm7(const char *text, unsigned char *out, int max)
{
	const unsigned char *p = (const unsigned char *)text;
	int n = 0;

	while (*p) {
		long cp = next_utf8(&p);
		int c, k;

		if (cp < 0)
			return -1;
		for (c = 0; c < 128 && gsm7[c] != cp; c++)
			;
		if (c < 128) {
			if (n >= max)
				return -1;
			out[n++] = (unsigned char)c;
			continue;
		}
		for (k = 0; k < (int)(sizeof(gsm7_ext) / sizeof(gsm7_ext[0])) && gsm7_ext[k].cp != cp; k++)
			;
		if (k == (int)(sizeof(gsm7_ext) / sizeof(gsm7_ext[0])) || n + 2 > max)
			return -1;
		out[n++] = 0x1b;
		out[n++] = gsm7_ext[k].code;
	}
	return n;
}

static int to_utf16(const char *text, unsigned short *out, int max)
{
	const unsigned char *p = (const unsigned char *)text;
	int n = 0;

	while (*p) {
		long cp = next_utf8(&p);

		if (cp < 0)
			return -1;
		if (cp >= 0x10000) {
			if (n + 2 > max)
				return -1;
			cp -= 0x10000;
			out[n++] = (unsigned short)(0xd800 + (cp >> 10));
			out[n++] = (unsigned short)(0xdc00 + (cp & 0x3ff));
		} else {
			if (n + 1 > max)
				return -1;
			out[n++] = (unsigned short)cp;
		}
	}
	return n;
}

static void hex_out(char *dst, const unsigned char *b, int n)
{
	static const char x[] = "0123456789ABCDEF";
	int i;

	for (i = 0; i < n; i++) {
		dst[2 * i] = x[b[i] >> 4];
		dst[2 * i + 1] = x[b[i] & 15];
	}
	dst[2 * n] = '\0';
}

int mm_pdu_submit(const char *number, const char *smsc, const char *text, int ref, char (*pdus)[400], int *lens, int max)
{
	unsigned char sept[1700], pdu[220], da[12], sc[12];
	unsigned short u16[1700];
	int ns, nu = 0, parts, per, one, i, start, dlen = 0, digits = 0, toa = 0x81, sclen = 0, sctoa = 0x81;
	bool seven;
	const char *d;

	if (!mm_sms_number_ok(number) || !mm_sms_text_ok(text) || (smsc && *smsc && !mm_sms_number_ok(smsc)))
		return -1;
	/* the SMS centre of this message, or none (00: the one on the SIM) */
	if (smsc && *smsc) {
		int n = 0;

		d = smsc;
		if (*d == '+') {
			sctoa = 0x91;
			d++;
		}
		memset(sc, 0xff, sizeof(sc));
		for (; *d; d++, n++)
			sc[n / 2] = n % 2 ? (unsigned char)((sc[n / 2] & 0x0f) | (*d - '0') << 4) : (unsigned char)(0xf0 | (*d - '0'));
		sclen = (n + 1) / 2;
	}
	d = number;
	if (*d == '+') {
		toa = 0x91;
		d++;
	}
	memset(da, 0xff, sizeof(da));
	for (; *d; d++, digits++)
		da[digits / 2] = digits % 2 ? (unsigned char)((da[digits / 2] & 0x0f) | (*d - '0') << 4)
					    : (unsigned char)(0xf0 | (*d - '0'));
	dlen = (digits + 1) / 2;
	ns = to_gsm7(text, sept, (int)sizeof(sept));
	seven = ns >= 0;
	if (!seven && (nu = to_utf16(text, u16, (int)(sizeof(u16) / sizeof(u16[0])))) < 0)
		return -1;
	one = seven ? 160 : 70;
	per = seven ? 153 : 67;
	/* parts: split without cutting an escape pair or a surrogate pair */
	{
		int total = seven ? ns : nu, at = 0, bounds[256];

		parts = 0;
		bounds[0] = 0;
		if (total <= one) {
			bounds[++parts] = total;
		} else {
			while (at < total) {
				int end = at + per < total ? at + per : total;

				if (end < total && ((seven && sept[end - 1] == 0x1b) ||
						    (!seven && u16[end - 1] >= 0xd800 && u16[end - 1] < 0xdc00)))
					end--;
				if (parts + 1 >= 256)
					return -1;
				bounds[++parts] = end;
				at = end;
			}
		}
		if (parts > max || parts > 255)
			return -1;
		for (i = 0; i < parts; i++) {
			int p = 0, from = bounds[i], to = bounds[i + 1], k, udl;
			bool udh = parts > 1;

			memset(pdu, 0, sizeof(pdu));
			if (sclen) {
				pdu[p++] = (unsigned char)(sclen + 1);	/* the SMSC: its length, type, digits */
				pdu[p++] = (unsigned char)sctoa;
				memcpy(pdu + p, sc, (size_t)sclen);
				p += sclen;
			} else {
				pdu[p++] = 0x00;			/* the SIM's SMSC */
			}
			pdu[p++] = (unsigned char)(0x11 | (udh ? 0x40 : 0));	/* SUBMIT, relative validity */
			pdu[p++] = 0x00;				/* message reference: the modem's */
			pdu[p++] = (unsigned char)digits;
			pdu[p++] = (unsigned char)toa;
			memcpy(pdu + p, da, (size_t)dlen);
			p += dlen;
			pdu[p++] = 0x00;				/* protocol identifier */
			pdu[p++] = seven ? 0x00 : 0x08;
			pdu[p++] = 0xaa;				/* 4 days */
			{
				int udl_at = p++;
				unsigned char *ud = pdu + p;

				if (udh) {
					ud[0] = 5;
					ud[1] = 0;
					ud[2] = 3;
					ud[3] = (unsigned char)ref;
					ud[4] = (unsigned char)parts;
					ud[5] = (unsigned char)(i + 1);
				}
				if (seven) {
					start = udh ? 7 : 0;	/* 6 header octets and a fill bit */
					for (k = from; k < to; k++) {
						int bit = (start + k - from) * 7, byte = bit / 8, shift = bit % 8;

						ud[byte] |= (unsigned char)(sept[k] << shift);
						if (shift > 1)
							ud[byte + 1] |= (unsigned char)(sept[k] >> (8 - shift));
					}
					udl = start + to - from;
					p += (udl * 7 + 7) / 8;
				} else {
					int o = udh ? 6 : 0;

					for (k = from; k < to; k++) {
						ud[o++] = (unsigned char)(u16[k] >> 8);
						ud[o++] = (unsigned char)(u16[k] & 0xff);
					}
					udl = o;
					p += o;
				}
				pdu[udl_at] = (unsigned char)udl;
			}
			hex_out(pdus[i], pdu, p);
			lens[i] = p - 1 - sclen - (sclen ? 1 : 0);	/* AT+CMGS counts the TPDU only */
		}
	}
	return parts;
}

/* ---- USSD ---- */

static int score(const char *s, bool *clean)
{
	const unsigned char *p = (const unsigned char *)s;
	int good = 0, letters = 0;

	*clean = true;
	while (*p) {
		long cp = next_utf8(&p);

		if (cp < 0 || cp == 0xfffd || (cp < 0x20 && cp != '\n' && cp != '\r') || (cp >= 0x7f && cp < 0xa0) ||
		    (cp > 0x52f && cp < 0x2000) || (cp > 0x206f && cp < 0x20a0) || (cp > 0x20cf && cp != 0x2116)) {
			*clean = false;
			return 0;
		}
		good++;
		if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0x400 && cp <= 0x4ff))
			letters++;
	}
	return good + 4 * letters;
}

void mm_ussd_text(const char *str, int dcs, char *out, size_t size)
{
	unsigned char b[400];
	int klass, alpha = dcs == 0x11 ? 16 : alphabet(dcs, &klass), n = unhex(str, b, sizeof(b));
	struct text cand[3] = { { 0 } };
	int best = -1, best_score = 0, i;

	snprintf(out, size, "%s", str ? str : "");
	if (n < 2)
		return;
	if (alpha == 16) {
		int skip = dcs == 0x11 ? 2 : 0;	/* a language code before the text */

		utf16_text(&cand[0], b + skip, n - skip, false);
		utf16_text(&cand[1], b + skip, n - skip, true);	/* some firmwares swap the bytes */
	} else if (alpha == 7 && n >= 4) {
		gsm7_text(&cand[0], b, n, 0, n * 8 / 7);
		for (i = 0; i < n; i++)		/* one septet per octet */
			put_cp(&cand[1], b[i] < 128 && gsm7[b[i]] >= 0 ? gsm7[b[i]] : 0xfffd);
	} else if (alpha == 8) {
		for (i = 0; i < n; i++)
			put_cp(&cand[0], b[i]);
	}
	for (i = 0; i < 3; i++) {
		bool clean;
		int s;

		if (!cand[i].s)
			continue;
		while (cand[i].len && (cand[i].s[cand[i].len - 1] == '\r' || cand[i].s[cand[i].len - 1] == '@'))
			cand[i].s[--cand[i].len] = '\0';	/* the padding of packed 7-bit */
		s = score(cand[i].s, &clean);
		if (clean && s > best_score) {
			best_score = s;
			best = i;
		}
	}
	/* a GSM 7-bit answer the modem gave as text of digits ("1234") is not hex: packed text has letters */
	if (best >= 0 && (alpha != 7 || strpbrk(str, "ABCDEFabcdef")))
		snprintf(out, size, "%s", cand[best].s);
	for (i = 0; i < 3; i++)
		free(cand[i].s);
}

json_object *mm_ussd_urc(const char *line)
{
	const char *c = line ? strstr(line, "+CUSD:") : NULL, *q1, *q2;
	json_object *o;
	int m;

	if (!c || sscanf(c + 6, " %d", &m) != 1)
		return NULL;
	o = json_object_new_object();
	json_object_object_add(o, "m", json_object_new_int(m));
	q1 = strchr(c, '"');
	q2 = q1 ? strrchr(q1 + 1, '"') : NULL;
	if (q1 && q2) {
		char *str = strndup(q1 + 1, (size_t)(q2 - q1 - 1)), out[2048];
		int dcs = 15;

		sscanf(q2 + 1, " ,%d", &dcs);
		mm_ussd_text(str, dcs, out, sizeof(out));
		json_object_object_add(o, "reply", json_object_new_string(out));
		free(str);
	}
	return o;
}

/* ---- the AT storage listing ---- */

static int by_id(const void *a, const void *b)
{
	int x = json_object_get_int(json_object_object_get(*(json_object *const *)a, "id"));
	int y = json_object_get_int(json_object_object_get(*(json_object *const *)b, "id"));

	return (x > y) - (x < y);
}

json_object *mm_sms_at_list(const char *cpms_raw, const char *cmgl_raw)
{
	json_object *out = json_object_new_object(), *raw = json_object_new_array(), *msgs = json_object_new_array();
	const char *p = cmgl_raw ? cmgl_raw : "", *c = cpms_raw ? strstr(cpms_raw, "+CPMS:") : NULL;
	char storage[8] = "";
	int used = -1, total = -1;
	size_t i, k;

	if (c && sscanf(c, "+CPMS: \"%7[^\"]\",%d,%d", storage, &used, &total) >= 1) {
		for (i = 0; storage[i]; i++)
			storage[i] = (char)tolower((unsigned char)storage[i]);
		json_object_object_add(out, "storage", json_object_new_string(storage));
		if (used >= 0)
			json_object_object_add(out, "used", json_object_new_int(used));
		if (total >= 0)
			json_object_object_add(out, "total", json_object_new_int(total));
	}
	/* "+CMGL: <index>,<stat>,[<alpha>],<length>" and the PDU on the next line */
	while (*p) {
		const char *h = strstr(p, "+CMGL:"), *nl, *pdu_end;
		int idx, stat;
		char *hex;
		json_object *m;

		if (!h)
			break;
		if (sscanf(h, "+CMGL: %d,%d", &idx, &stat) != 2 || !(nl = strchr(h, '\n'))) {
			p = h + 6;
			continue;
		}
		nl++;
		while (*nl == '\r' || *nl == '\n')
			nl++;
		pdu_end = nl + strspn(nl, "0123456789ABCDEFabcdef");
		hex = strndup(nl, (size_t)(pdu_end - nl));
		m = mm_pdu_decode(hex);
		free(hex);
		p = pdu_end;
		if (json_object_object_get(m, "error")) {
			json_object_put(m);
			continue;
		}
		json_object_object_add(m, "id", json_object_new_int(idx));
		json_object_object_add(m, "state", json_object_new_string(stat <= 1 ? "received" : stat == 3 ? "sent" : "stored"));
		json_object_array_add(raw, m);
	}
	/* join the parts of one message: same type, number, reference and count, each part number once */
	for (i = 0; i < json_object_array_length(raw); i++) {
		json_object *m = json_object_array_get_idx(raw, i), *cc = json_object_object_get(m, "concat"), *parts, *slot[256] = { 0 };
		const char *num = json_object_get_string(json_object_object_get(m, "number"));
		const char *type = json_object_get_string(json_object_object_get(m, "type"));
		int ref, tot, have = 0, lowest, s;
		struct text t = { 0 };
		json_object *joined;
		bool binary = false;

		if (json_object_object_get(m, "taken"))
			continue;
		if (!cc) {
			json_object *one = json_object_get(m);

			json_object_object_del(one, "concat");
			json_object_object_add(one, "parts", json_object_new_array());
			json_object_array_add(json_object_object_get(one, "parts"), json_object_get(json_object_object_get(one, "id")));
			json_object_array_add(msgs, one);
			continue;
		}
		ref = json_object_get_int(json_object_object_get(cc, "ref"));
		tot = json_object_get_int(json_object_object_get(cc, "total"));
		parts = json_object_new_array();
		lowest = json_object_get_int(json_object_object_get(m, "id"));
		for (k = i; k < json_object_array_length(raw); k++) {
			json_object *o = json_object_array_get_idx(raw, k), *oc = json_object_object_get(o, "concat");
			int part;

			if (!oc || json_object_object_get(o, "taken") || json_object_get_int(json_object_object_get(oc, "ref")) != ref ||
			    json_object_get_int(json_object_object_get(oc, "total")) != tot ||
			    strcmp(json_object_get_string(json_object_object_get(o, "number")), num) ||
			    strcmp(json_object_get_string(json_object_object_get(o, "type")), type))
				continue;
			part = json_object_get_int(json_object_object_get(oc, "part"));
			/* a part number already filled is another message of the same sender under a reused reference (it is
			 * 8 bits, it wraps at 256; a field report, 2026-11-14): it starts a message of its own */
			if (part >= 1 && part <= 255 && slot[part])
				continue;
			json_object_object_add(o, "taken", json_object_new_boolean(true));
			json_object_array_add(parts, json_object_get(json_object_object_get(o, "id")));
			if (json_object_get_int(json_object_object_get(o, "id")) < lowest)
				lowest = json_object_get_int(json_object_object_get(o, "id"));
			if (part >= 1 && part <= 255 && !slot[part])
				slot[part] = o;
		}
		joined = json_object_new_object();
		json_object_object_foreach(m, key, val)
			if (strcmp(key, "concat") && strcmp(key, "taken") && strcmp(key, "text"))
				json_object_object_add(joined, key, json_object_get(val));
		for (s = 1; s <= tot && s <= 255; s++) {
			if (!slot[s])
				continue;
			have++;
			if (json_object_get_boolean(json_object_object_get(slot[s], "binary")))
				binary = true;
			if (json_object_get_string(json_object_object_get(slot[s], "text"))) {
				const char *x = json_object_get_string(json_object_object_get(slot[s], "text"));
				size_t xl = strlen(x);

				if (t.len + xl + 1 > t.cap) {
					size_t cap = t.cap ? t.cap : 256;
					char *ns;

					while (cap < t.len + xl + 1)
						cap *= 2;
					if ((ns = realloc(t.s, cap))) {
						t.s = ns;
						t.cap = cap;
					}
				}
				if (t.len + xl + 1 <= t.cap) {
					memcpy(t.s + t.len, x, xl + 1);
					t.len += xl;
				}
			}
		}
		/* the first part's time: the time the message was sent */
		for (s = 1; s <= tot && s <= 255 && !slot[s]; s++)
			;
		if (s <= tot && s <= 255) {
			json_object_object_add(joined, "timestamp", json_object_get(json_object_object_get(slot[s], "timestamp")));
			json_object_object_add(joined, "epoch", json_object_get(json_object_object_get(slot[s], "epoch")));
		}
		json_object_object_add(joined, "id", json_object_new_int(lowest));
		json_object_object_add(joined, "text", binary ? NULL : json_object_new_string(t.s ? t.s : ""));
		json_object_object_add(joined, "binary", json_object_new_boolean(binary));
		json_object_object_add(joined, "parts", parts);
		if (have < tot) {
			json_object_object_add(joined, "have", json_object_new_int(have));
			json_object_object_add(joined, "total", json_object_new_int(tot));
		}
		free(t.s);
		json_object_array_add(msgs, joined);
	}
	json_object_put(raw);
	json_object_array_sort(msgs, by_id);
	{
		size_t n = json_object_array_length(msgs);

		for (i = 0; i < n; i++) {
			json_object *m = json_object_array_get_idx(msgs, i);

			if (storage[0])
				json_object_object_add(m, "storage", json_object_new_string(storage));
			json_object_object_add(m, "path", json_object_new_string("at"));
		}
	}
	json_object_object_add(out, "messages", msgs);
	return out;
}
