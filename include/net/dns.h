#ifndef NET_DNS_H
#define NET_DNS_H

#include <stddef.h>
#include <stdint.h>

int net_dns_encode_name(const char *host, uint8_t *out, size_t out_cap,
                        size_t *out_len);
int net_dns_parse_first_a(const uint8_t *msg, size_t len, uint16_t expected_id,
                          uint32_t *out_ip);

#endif /* NET_DNS_H */
