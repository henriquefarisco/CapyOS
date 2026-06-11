#ifndef NET_DHCP_OPTIONS_H
#define NET_DHCP_OPTIONS_H

#include <stddef.h>
#include <stdint.h>

/*
 * DHCP OFFER/ACK option-block parser (RFC 2132).
 *
 * This is untrusted network input: the bytes come straight from whatever
 * answered the broadcast on UDP/67. The parser is bounded and fail-safe —
 * it never reads past `len`, stops on a malformed (over-long) option, and
 * skips options whose length is too short for the field they encode. It
 * cannot loop forever (the cursor advances by at least one byte per
 * iteration).
 *
 * Out-params are written only when a well-formed option of the expected
 * length is present; otherwise they are left untouched, so the caller must
 * pre-initialise them. Any out-param may be NULL (that field is then
 * ignored). `options` may be NULL (treated as an empty block).
 *
 * Extracted from src/net/core/stack_services.c so the parser is
 * host-testable in isolation, mirroring the self-contained DNS parser in
 * net/dns.h. The behaviour is byte-for-byte identical to the previous
 * in-file static implementation.
 */
void dhcp_parse_options(const uint8_t *options, size_t len,
                        uint8_t *msg_type, uint32_t *server_id,
                        uint32_t *subnet_mask, uint32_t *router,
                        uint32_t *dns);

#endif /* NET_DHCP_OPTIONS_H */
