/*
 * src/kernel/syscall_net_init.c (2026-05-08, F4 seção c)
 *
 * Production wiring that bridges `src/kernel/syscall_net.c`'s
 * injectable backend ABI to the real `socket_*` family in
 * `src/net/services/socket.c`. Kept in a separate translation unit
 * so the host unit tests linking `syscall_net.c` standalone do not
 * accumulate transitive net-stack dependencies (`tcp_open`,
 * `net_stack_send_ipv4`, etc.).
 *
 * The single entry point `syscall_net_install_default_ops` is meant
 * to be called once during kernel boot, AFTER `socket_system_init`
 * has zeroed the socket table. The same call also registers the
 * close hook on the process FD lifecycle (so `process_destroy` /
 * SYS_EXIT / Task Manager kill flush the socket table without
 * hard-linking process.c against socket.c).
 */

#include "kernel/syscall_net.h"
#include "kernel/process.h"
#include "net/socket.h"
#include "net/dns_cache.h"
#include "net/stack.h"

#include <stddef.h>
#include <stdint.h>

/* F4 seção c parte 4/4 (2026-05-08): timeout for the active DNS
 * query when the local cache misses. 3000 ms mirrors the value
 * already used by the kernel's HTTP client (`http_request` in
 * `src/net/services/http/request_response.c`) so behaviour is
 * uniform across syscall and kernel-internal callers. The
 * resolver thread blocks the calling task for at most this
 * duration; non-blocking variants will land alongside the future
 * `flags` slot of `SYS_DNS_RESOLVE`. */
#define SYSCALL_DNS_RESOLVE_TIMEOUT_MS 3000u

/* F4 seção c parte 4/4 follow-up (2026-05-08): TTL clamp window
 * for cache insertion. RFC 1035 leaves TTL as a raw uint32_t
 * (0..2^31-1 per §3.2.1, ~68 years). Real-world recursors emit
 * anywhere from 0 (CDN edge: do-not-cache) to 86400 (root NS).
 *   - Lower bound 60 s: protects against `TTL=0` / `TTL=1`
 *     hot-path attacks where a hostile resolver forces every
 *     resolve to round-trip UDP/53. The resolver itself can
 *     still serve fresh records on each cache miss; a 60 s floor
 *     just rate-limits the syscall traffic.
 *   - Upper bound 86400 s (24 h): caps memory pressure on the
 *     fixed 64-entry cache table. Records older than 24 h almost
 *     certainly need re-resolution (DHCP lease renew, CDN edge
 *     migration, BGP route flap), and `dns_cache_ttl_ticks` is
 *     uint64_t-safe at this magnitude.
 *   - TTL == 0 from the wire is treated as "use default" by
 *     `dns_cache_insert` (which substitutes DNS_CACHE_TTL_DEFAULT
 *     = 300 s). We forward 0 verbatim so the substitution stays
 *     in one place and the upper-bound clamp doesn't silently
 *     override the do-not-cache hint. */
#define SYSCALL_DNS_RESOLVE_TTL_MIN_S 60u
#define SYSCALL_DNS_RESOLVE_TTL_MAX_S 86400u

/* Non-static so the host test (`tests/test_syscall_net_init.c`) can
 * pin the clamp boundaries without going through the full
 * cache+stack wrapper. Kept in the same TU as
 * `syscall_dns_resolve_with_active` to keep the constants and
 * the function in one place. */
uint32_t syscall_dns_resolve_clamp_ttl(uint32_t raw) {
  if (raw == 0u) return 0u;
  if (raw < SYSCALL_DNS_RESOLVE_TTL_MIN_S) return SYSCALL_DNS_RESOLVE_TTL_MIN_S;
  if (raw > SYSCALL_DNS_RESOLVE_TTL_MAX_S) return SYSCALL_DNS_RESOLVE_TTL_MAX_S;
  return raw;
}

/* Sessão 44 (2026-05-08): negative TTL clamp upper bound. RFC 2308
 * §5 caps at min(SOA.MINIMUM, SOA.TTL) but recursors can serve
 * MINIMUM in the day/week range -- way too long for a typo'd
 * URL bar. We cap at DNS_CACHE_NEGATIVE_TTL_MAX (1 h) to bound
 * the time a transient NXDOMAIN can shadow a real fix. The lower
 * bound 30 s rate-limits hostile authoritative servers that emit
 * MINIMUM=1 to force us to re-query every miss (DDoS amplifier). */
#define SYSCALL_DNS_RESOLVE_NEG_TTL_MIN_S 30u

uint32_t syscall_dns_resolve_clamp_neg_ttl(uint32_t raw) {
  if (raw == 0u) raw = DNS_CACHE_TTL_DEFAULT;
  if (raw < SYSCALL_DNS_RESOLVE_NEG_TTL_MIN_S) return SYSCALL_DNS_RESOLVE_NEG_TTL_MIN_S;
  if (raw > DNS_CACHE_NEGATIVE_TTL_MAX) return DNS_CACHE_NEGATIVE_TTL_MAX;
  return raw;
}

/* F4 seção c parte 4/4 (2026-05-08): wraps the cache + active
 * resolver as one syscall-visible operation. The contract is:
 *   1. positive cache hit       → return 0 immediately, no UDP;
 *   2. negative cache hit       → return -1 immediately, no UDP
 *      (RFC 2308 negative caching: the upstream said the name does
 *      not exist; honour that for the cached duration);
 *   3. cache miss + active hit  → seed positive cache with clamped
 *      per-record TTL [60s, 24h], return 0;
 *   4. cache miss + active negative (NXDOMAIN/NODATA with SOA) →
 *      seed negative cache with clamped SOA-derived TTL [30s, 1h],
 *      return -1;
 *   5. cache miss + transport failure (timeout, no DNS server,
 *      malformed response, no SOA) → return -1, do NOT cache.
 *
 * The wrapper deliberately does NOT poison the cache on transport
 * failure -- a transient DNS outage shouldn't masquerade as a
 * permanent NXDOMAIN. We only cache definitive negatives, which
 * the parser distinguishes via `out_neg_ttl > 0` after rc=-1.
 *
 * Sessão 44 (2026-05-08): added negative cache lookup before
 * active query and negative insert after parseable NXDOMAIN/NODATA.
 * `dns_cache_lookup` and `dns_cache_lookup_negative` are disjoint
 * (negative entries are invisible to positive lookup), so the
 * order checked here is purely a perf optimisation: positive lookup
 * first because the common case is "name exists and was just
 * queried". */
static int syscall_dns_resolve_with_active(const char *name, uint32_t *out_ip) {
  if (!name || !out_ip) return -1;
  if (dns_cache_lookup(name, out_ip) == 0) return 0;
  if (dns_cache_lookup_negative(name) == 0) return -1;
  uint32_t ttl = 0u;
  uint32_t neg_ttl = 0u;
  int rc = net_stack_dns_resolve(name, SYSCALL_DNS_RESOLVE_TIMEOUT_MS,
                                 out_ip, &ttl, &neg_ttl);
  if (rc == 0) {
    dns_cache_insert(name, *out_ip, syscall_dns_resolve_clamp_ttl(ttl));
    return 0;
  }
  /* rc == -1: distinguish definitive negative (cache it) from
   * transport failure (don't). The parser populates neg_ttl only
   * when it found a parseable SOA in the authority section. */
  if (neg_ttl > 0u) {
    dns_cache_insert_negative(name, syscall_dns_resolve_clamp_neg_ttl(neg_ttl));
  }
  return -1;
}

static const struct syscall_net_ops g_default_ops = {
    .sock_create  = socket_create,
    .sock_bind    = socket_bind,
    .sock_listen  = socket_listen,
    .sock_accept  = socket_accept,
    .sock_connect = socket_connect,
    .sock_send    = socket_send,
    .sock_recv    = socket_recv,
    .sock_close   = socket_close,
    /* F4 seção c parte 3/3 (2026-05-08): cache-only resolver wired
     * here -- userland was responsible for active queries.
     * F4 seção c parte 4/4 (2026-05-08): swap to the wrapper that
     * falls through cache miss into `net_stack_dns_resolve` and
     * seeds the cache on success, so apps can resolve hostnames
     * never seen before (browser bar, update-agent CLI flag) without
     * each one re-implementing UDP/53 in userland. */
    .dns_resolve  = syscall_dns_resolve_with_active,
};

void syscall_net_install_default_ops(void) {
  /* F4 seção c parte 2/2 (2026-05-08): socket_system_init was
   * never called in production prior to this fix. The static
   * socket_table was zero-initialized and `socket_initialized`
   * defaults to 1 in the .bss, so the bug was latent (no observable
   * symptom on cold boot), but a future re-init path or a hot
   * code-reload would observe stale state. Calling it here pins
   * the contract: by the time the first SYS_SOCKET dispatches,
   * the socket table is freshly zeroed and `sock_stats` is reset.
   * Idempotent against multiple invocations of this function
   * (EBS + post-EBS init paths in kernel_main.c). */
  socket_system_init();
  /* F4 seção c parte 3/3 (2026-05-08): seed an empty DNS cache
   * table here too, mirroring the socket_system_init invariant.
   * `dns_cache_init` is idempotent (re-zeroes a table); calling
   * it twice across the EBS / post-EBS boot paths is harmless. */
  dns_cache_init();
  syscall_net_install_ops(&g_default_ops);
  process_fd_register_socket_close(socket_close);
}
