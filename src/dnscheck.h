/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_DNSCHECK_H
#define MEDVEMODEM_DNSCHECK_H

#include <stddef.h>
#include <json-c/json.h>

/*
 * Does the operator touch DNS (spec 25.9.20, decisions 2026-10-14)? Through the modem's own device
 * (SO_BINDTODEVICE), with the list /usr/share/medvemodem/dnscheck.list (resolver|kind|address,
 * name|<host>, doh|<url>):
 * - each public resolver: who answers (Cloudflare names its site in three capitals; Google's
 *   o-o.myaddr TXT and Quad9's id.server are kept as they come) and the median of three DNS answers
 *   against three TCP handshakes to the same address on 443: one round trip each, so an answer in
 *   under half the handshake (and 10 ms quicker) came from nearer than the server — the operator's
 *   box answering for it;
 * - a random name under .com asked of the operator's server: an address instead of NXDOMAIN;
 * - names a regulator blocks, asked of the operator and each resolver, against DoH (curl, TLS
 *   verified, through the same device): an address DoH does not give that is a bogon, or the same
 *   for two blocked names, or NXDOMAIN where DoH has an answer, is a stub.
 * {"device", "at", "resolvers": [{"kind", "address", "identity", "udp_ms", "tcp_ms", "verdict"}],
 *  "intercept": {"state": "ok"|"intercepted"|"skipped", "detail"},
 *  "nxdomain": {"state": "ok"|"rewritten"|"no-answer"|"error"|"skipped", "server", "ms", "detail"},
 *  "names": [{"name", "doh", "plain": {server: answer}, "verdict": "ok"|"stub"|"skipped"}],
 *  "blocked": {"state": "ok"|"stub"|"skipped", "detail"}}
 *
 * mm_dnsq writes a query; its length, 0 when it does not fit. mm_dns_first reads a reply to id:
 * its RCODE, -1 when it is not one; in out every A of the answer joined by commas, or a TXT's
 * first string. mm_dns_rtt_verdict and mm_dns_bogon are the rules above, for tests.
 */
#include <stdbool.h>

int mm_dnsq(unsigned char *buf, size_t size, const char *name, unsigned int id, int type, int klass);
int mm_dns_first(const unsigned char *buf, size_t len, unsigned int id, int type, char *out, size_t size);
const char *mm_dns_rtt_verdict(int udp_ms, int tcp_ms);
bool mm_dns_bogon(const char *ip);
json_object *mm_dns_check(const char *dev, const char *list, const char *operator_dns, unsigned int seed);

#endif
