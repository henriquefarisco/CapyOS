#ifndef NET_HTTP_HSTS_H
#define NET_HTTP_HSTS_H

#include <stdint.h>
#include <stddef.h>

#include "net/http.h" /* HTTP_MAX_HOST */

/*
 * HTTP Strict-Transport-Security store (RFC 6797 subset) for the CapyOS
 * browser/fetch path (Etapa 7 / Slice 7.6 hardening). A CapyOS-owned net
 * service, sibling of http_cache/http_cookies/http_session/http_fetch_policy.
 *
 * HSTS hardens the HTTPS-first navigation policy: once a host sends a valid
 * `Strict-Transport-Security` header over a secure connection, the client MUST
 * use https for that host (and, with includeSubDomains, its subdomains) until
 * the policy expires -- defeating SSL-stripping / downgrade attacks where an
 * active attacker rewrites https links to http. Where http_fetch_policy turns
 * an http navigation into a *soft* UPGRADE, a matching HSTS entry makes the
 * upgrade *mandatory* (a downgrade to http must be refused).
 *
 * Pure, deterministic, fail-closed, freestanding: clock INJECTED (`now`, epoch
 * seconds), store CALLER-PROVIDED (no kernel .bss until allocated), host-tested.
 *
 * Conservative per RFC 6797:
 *   - the header is honoured ONLY over a secure connection (ignored over http);
 *   - `max-age` is required; a header without it is ignored;
 *   - `max-age=0` REMOVES the host's policy;
 *   - an HSTS host given as an IP literal is rejected (RFC 6797 8.1.1);
 *   - includeSubDomains extends the policy to subdomains.
 */

#define HTTP_HSTS_MAX_ENTRIES 32u
#define HTTP_HSTS_DOMAIN_MAX HTTP_MAX_HOST /* 192 */

struct http_hsts_entry {
  int valid;
  char domain[HTTP_HSTS_DOMAIN_MAX]; /* lower-case, no leading dot */
  long expires;                      /* epoch seconds (now + max-age) */
  int include_subdomains;
  uint64_t lru;
};

struct http_hsts_store {
  struct http_hsts_entry entries[HTTP_HSTS_MAX_ENTRIES];
  uint64_t clock;
  uint32_t set_count;  /* policies stored/updated */
  uint32_t deleted;    /* policies removed (max-age=0) */
  uint32_t evictions;  /* entries evicted at capacity */
};

/* Reset the store to empty. */
void http_hsts_init(struct http_hsts_store *s);

/* Parse a single `Strict-Transport-Security` header `value` and apply it to
 * `host`. `secure` must be 1 (the connection that delivered it was https); a
 * header arriving over http is ignored (returns 0). `now` is the clock.
 * Returns 1 if a policy was stored/updated/removed, 0 if ignored (insecure,
 * missing max-age, IP host, malformed, or no room). */
int http_hsts_process_header(struct http_hsts_store *s, const char *host,
                             const char *value, int secure, long now);

/* 1 if `host` is currently covered by a non-expired HSTS policy (exact match,
 * or a parent policy with includeSubDomains) and so MUST be accessed over
 * https; 0 otherwise. */
int http_hsts_should_upgrade(struct http_hsts_store *s, const char *host,
                             long now);

/* Drop expired entries (expires <= now). */
void http_hsts_gc(struct http_hsts_store *s, long now);

#endif /* NET_HTTP_HSTS_H */
