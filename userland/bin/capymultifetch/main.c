/*
 * userland/bin/capymultifetch/main.c — ring-3 multi-fetch browser runtime smoke
 * (Etapa 7 / Slice 7.5: persistent http_session over the real transport).
 *
 * The first ring-3 consumer of the multi-fetch runtime (browser_fetch.c): it
 * holds a PERSISTENT session (cache + cookie jar + HSTS) across navigations and
 * proves the Etapa 7 "cache accelerates the 2nd visit" criterion at runtime:
 *
 *   1. fetch CAPYMULTIFETCH_URL twice over the real Etapa 5 TLS/socket path
 *      through the SAME browser_fetch_ctx;
 *   2. assert that only ONE real network transport call happened for the two
 *      visits of a cacheable page (the 2nd was served from the in-process cache).
 *
 * This app deliberately fetches CAPYMULTIFETCH_URL as configured, WITHOUT
 * running it through browser_fetch_plan (HSTS + HTTPS-first): that policy
 * decision is pure logic already host-tested exhaustively
 * (tests/net/test_browser_fetch.c: test_navigation_plan / test_hsts_forces_https
 * / test_subresource_mixed_content) and needs no network to verify. The
 * hermetic QEMU smoke server is plain HTTP on the SLIRP gateway (mirroring
 * capybrowse-text's smoke setup, which avoids TLS trust-anchor complexity for
 * a local test double); forcing HTTPS-first here would make the smoke
 * depend on a local TLS server instead of exercising the thing this smoke
 * actually proves — the cache short-circuit.
 *
 * On success emits the COM1 marker `[smoke] browser-multifetch ready` and exits
 * 0; any transport failure or a cache that failed to short-circuit exits non-0
 * (fail-closed). The endpoint is compile-time configurable (like tls_smoke /
 * capybrowse) for the controlled smoke server. NO CapyBrowser core dependency
 * (uses capylibc-net's capy_url_parse directly), so no symbol-rename dance.
 */

#include <capylibc/capylibc.h>

#include "browser_fetch.h"
#include "page_budget.h"

#ifndef CAPYMULTIFETCH_URL
#define CAPYMULTIFETCH_URL "https://example.com/"
#endif

/* Bounded retry to absorb the async DHCP lease window (mirrors tls_smoke). */
#ifndef CAPYMULTIFETCH_MAX_ATTEMPTS
#define CAPYMULTIFETCH_MAX_ATTEMPTS 600u
#endif
#define CAPYMULTIFETCH_SLEEP_TICKS 10u

/* Per-page resource caps (Slice 7.6): cap total admitted bytes + wall-time so a
 * hostile/huge page cannot exhaust memory or hang the runtime. */
#define CAPYMULTIFETCH_MAX_BYTES (2u * 1024u * 1024u)
#define CAPYMULTIFETCH_MAX_TICKS 6000

/* Bulk state in .bss (zeroed by the ELF loader); browser_fetch_ctx is ~0.5 MiB
 * (the cache), far too large for the stack. */
static struct browser_fetch_ctx g_ctx;
static struct http_response g_resp;

static size_t mf_cstr_len(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}
static void mf_out(const char *s) { capy_write(1, s, mf_cstr_len(s)); }

int main(int rank) {
  struct page_budget budget;
  unsigned attempt;
  int got = 0;
  uint32_t calls_after_first_visit;

  (void)rank;
  browser_fetch_init(&g_ctx);
  page_budget_init(&budget, CAPYMULTIFETCH_MAX_BYTES, CAPYMULTIFETCH_MAX_TICKS, 0);

  /* 1st visit over the real transport (retry transport-level failures across
   * the DHCP window; a delivered response of any status counts). Each retry
   * attempt (including failed ones, before the network is up) issues its own
   * real transport call, so `transport_calls` after this loop may be > 1 even
   * though only ONE of them actually reached the server. Snapshot it here
   * rather than assuming it equals 1. */
  for (attempt = 0u; attempt < CAPYMULTIFETCH_MAX_ATTEMPTS; ++attempt) {
    if (browser_fetch_get(&g_ctx, CAPYMULTIFETCH_URL, &g_resp, 1) >= 0) {
      got = 1;
      break;
    }
    capy_yield();
    capy_sleep(CAPYMULTIFETCH_SLEEP_TICKS);
  }
  if (!got) {
    mf_out("multifetch: 1st fetch failed (transport)\n");
    capy_exit(1);
  }
  (void)page_budget_add_bytes(&budget, g_resp.body_len);
  calls_after_first_visit = g_ctx.transport_calls;

  /* 2nd visit of the same cacheable URL: should be served from the cache with
   * NO additional network transport call, i.e. transport_calls must not grow
   * past the snapshot taken right after the (successful) 1st visit. */
  (void)browser_fetch_get(&g_ctx, CAPYMULTIFETCH_URL, &g_resp, 1);

  if (g_ctx.transport_calls == calls_after_first_visit && page_budget_ok(&budget)) {
    mf_out("[smoke] browser-multifetch ready\n");
    capy_exit(0);
  }
  mf_out("multifetch: cache did not short-circuit the 2nd visit\n");
  capy_exit(2);
}
