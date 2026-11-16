// SPDX-License-Identifier: GPL-3.0-only
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "subnet.h"

static bool parse(const char *s, unsigned *addr, int *len)
{
	char buf[64], *slash;
	struct in_addr a;

	if (!s || snprintf(buf, sizeof(buf), "%s", s) >= (int)sizeof(buf))
		return false;
	slash = strchr(buf, '/');
	*len = 32;
	if (slash) {
		*slash = '\0';
		if (sscanf(slash + 1, "%d", len) != 1 || *len < 0 || *len > 32)
			return false;
	}
	if (inet_pton(AF_INET, buf, &a) != 1)
		return false;
	*addr = ntohl(a.s_addr);
	return true;
}

static unsigned mask_of(int len)
{
	return len ? ~0u << (32 - len) : 0;
}

static bool overlap(unsigned a, int alen, unsigned b, int blen)
{
	unsigned m = mask_of(alen < blen ? alen : blen);

	return (a & m) == (b & m);
}

json_object *mm_subnet_clash(json_object *addrs, const char *gw, const char *name, json_object *status)
{
	json_object *list = NULL;
	size_t i, j;

	if (!name || !json_object_object_get_ex(status, "ipv4-address", &list) || !json_object_is_type(list, json_type_array) ||
	    !json_object_is_type(addrs, json_type_array))
		return NULL;
	for (i = 0; i < json_object_array_length(list); i++) {
		json_object *e = json_object_array_get_idx(list, i), *ad = NULL, *m = NULL;
		unsigned r, a, g;
		int rlen, alen, glen;
		char router[64];

		if (!json_object_object_get_ex(e, "address", &ad) || !parse(json_object_get_string(ad), &r, &rlen))
			continue;
		rlen = json_object_object_get_ex(e, "mask", &m) ? json_object_get_int(m) : 32;
		if (rlen <= 0 || rlen > 32)
			continue;	/* a /0 is not a LAN */
		snprintf(router, sizeof(router), "%s/%d", json_object_get_string(ad), rlen);
		for (j = 0; j < json_object_array_length(addrs); j++) {
			const char *s = json_object_get_string(json_object_array_get_idx(addrs, j));
			json_object *o;

			if (!parse(s, &a, &alen) || !alen || !overlap(a, alen, r, rlen))
				continue;
			o = json_object_new_object();
			json_object_object_add(o, "interface", json_object_new_string(name));
			json_object_object_add(o, "router", json_object_new_string(router));
			json_object_object_add(o, "address", json_object_new_string(s));
			if (gw && parse(gw, &g, &glen))
				json_object_object_add(o, "gateway", json_object_new_string(gw));
			return o;
		}
	}
	return NULL;
}

bool mm_subnet_overlap(const char *a, const char *b)
{
	unsigned x, y;
	int xl, yl;

	return parse(a, &x, &xl) && parse(b, &y, &yl) && xl && yl && overlap(x, xl, y, yl);
}

const char *mm_subnet_pick(json_object *taken, char *out, size_t size)
{
	int i;

	/* the stick's factory 192.168.100.1 first, then 192.168.101.1 to 192.168.120.1 */
	for (i = 100; i <= 120; i++) {
		char net[24];
		bool free = true;

		snprintf(net, sizeof(net), "192.168.%d.1/24", i);
		for (size_t j = 0; free && json_object_is_type(taken, json_type_array) && j < json_object_array_length(taken); j++)
			free = !mm_subnet_overlap(net, json_object_get_string(json_object_array_get_idx(taken, j)));
		if (free) {
			snprintf(out, size, "192.168.%d.1", i);
			return out;
		}
	}
	return NULL;
}

const char *mm_subnet_first_host(const char *cidr, char *out, size_t size)
{
	unsigned a, net;
	int len;

	if (!parse(cidr, &a, &len) || len < 8 || len > 30)
		return NULL;
	net = (a & mask_of(len)) + 1;
	snprintf(out, size, "%u.%u.%u.%u", net >> 24, (net >> 16) & 0xff, (net >> 8) & 0xff, net & 0xff);
	return out;
}
