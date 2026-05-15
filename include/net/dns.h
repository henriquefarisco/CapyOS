#ifndef NET_DNS_H
#define NET_DNS_H

#include <stddef.h>
#include <stdint.h>

int net_dns_encode_name(const char *host, uint8_t *out, size_t out_cap,
                        size_t *out_len);
int net_dns_parse_first_a(const uint8_t *msg, size_t len, uint16_t expected_id,
                          uint32_t *out_ip, uint32_t *out_ttl);

/* Sessão 44 (2026-05-08) — RFC 2308 negative caching.
 * Returns 0 if the response is a definitive negative (NXDOMAIN with
 * RCODE=3, or NODATA with RCODE=0+ANCOUNT=0+at least one SOA in the
 * authority section) AND a parseable SOA record provides MINIMUM.
 * On success, *out_neg_ttl is populated with min(SOA.TTL, SOA.MINIMUM)
 * per RFC 2308 §5 ("the SOA record from the authority section ...
 * the TTL ... is set to the lesser of either the MINIMUM field of
 * the SOA record or the TTL of the SOA itself").
 *
 * Returns -1 if the response is positive (A record present), the
 * RCODE is some other failure (SERVFAIL=2, REFUSED=5, ...), or the
 * authority section lacks a parseable SOA. Callers that already
 * tried `net_dns_parse_first_a` and got -1 use this function to
 * disambiguate "definitive NXDOMAIN" from "transport failure". */
int net_dns_parse_negative_ttl(const uint8_t *msg, size_t len,
                               uint16_t expected_id, uint32_t *out_neg_ttl);

#endif /* NET_DNS_H */
