/*
 * userland/lib/capylibc-net/capy_net_resolve.c (F4 seção c parte 3/3)
 *
 * Hostname → IPv4 resolver + one-shot TCP client wrappers.
 *
 *   capy_resolve_host_ip4(host, &ip):
 *     1. Try `capy_inet_pton4(host, &ip)` first. A literal IPv4
 *        address ("8.8.8.8") short-circuits the DNS path entirely
 *        and never reaches the kernel resolver. This matches what
 *        glibc / musl do (`getaddrinfo` first checks AI_NUMERICHOST).
 *     2. On parse failure, fall back to `capy_dns_resolve(host,
 *        &ip, 0)` which queries the kernel-side DNS cache via
 *        SYS_DNS_RESOLVE. A miss propagates as -1 with
 *        CAPY_NET_EDNS in `capy_net_last_error()`.
 *
 *   capy_tcp_connect_host(host, port):
 *     Resolve via the helper above, then delegate to
 *     `capy_tcp_connect_ip4`. The `_str` variant from part 2/2
 *     stays available for callers that explicitly want NO DNS
 *     fallback (a stricter-by-default API for security-sensitive
 *     contexts; e.g. an updater that only ever connects to a
 *     hard-coded IP).
 *
 * Note: the kernel does NOT auto-promote a cache miss into an
 * active DNS query in this iteration. The cache is seeded by
 * DHCP discovery + (future) libcapy-net DNS client. Callers
 * that need a guaranteed lookup should populate the cache out of
 * band -- the active resolver is a follow-up.
 */

#include "capylibc-net/capy_net.h"
#include "capylibc/capylibc.h"

#include <stddef.h>
#include <stdint.h>

extern void capy_net_internal_set_error(capy_net_err_t err);
extern void capy_net_internal_reset_error(void);

int capy_resolve_host_ip4(const char *host, uint32_t *out_ip) {
  capy_net_internal_reset_error();
  if (!host || !out_ip) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  /* Step 1: literal-IPv4 fast path. capy_inet_pton4 itself sets
   * CAPY_NET_EPARSE on failure -- we deliberately reset that
   * BEFORE the DNS fallback so a successful DNS lookup doesn't
   * leave the parse-error visible to the caller. */
  if (capy_inet_pton4(host, out_ip) == 0) {
    return 0;
  }
  capy_net_internal_reset_error();

  /* Step 2: kernel DNS cache. A miss is the common failure mode
   * today (cache is sparsely seeded). flags == 0. */
  if (capy_dns_resolve(host, out_ip, 0) != 0) {
    capy_net_internal_set_error(CAPY_NET_EDNS);
    return -1;
  }
  return 0;
}

int capy_tcp_connect_host(const char *host, uint16_t port_host_order) {
  capy_net_internal_reset_error();
  if (!host) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  uint32_t ip = 0;
  if (capy_resolve_host_ip4(host, &ip) != 0) {
    /* capy_resolve_host_ip4 already set the right error. */
    return -1;
  }
  return capy_tcp_connect_ip4(ip, port_host_order);
}
