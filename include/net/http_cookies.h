#ifndef NET_HTTP_COOKIES_H
#define NET_HTTP_COOKIES_H

#include <stdint.h>
#include <stddef.h>

#include "net/http.h" /* struct http_response, HTTP_MAX_HOST */

/*
 * Bounded per-domain cookie jar (RFC 6265 subset) for the CapyOS browser/fetch
 * path (Etapa 7 / Slice 7.5). Sibling of http_cache: a CapyOS-owned network
 * service (cache/cookies are CapyOS adapters, not the decoupled CapyBrowser
 * core).
 *
 * Pure, deterministic, fail-closed and freestanding (host-testable): the clock
 * is INJECTED (`now`, epoch seconds) and the jar is CALLER-PROVIDED
 * (`struct http_cookie_jar`) -- no kernel .bss until a caller allocates one, and
 * reproducible under `make test`. Operates on the kernel HTTP model
 * (struct http_response Set-Cookie headers + request host/path).
 *
 * Scope (conservative -- never sends a cookie to the wrong site):
 *   - parses `name=value` + Domain / Path / Max-Age / Expires / Secure /
 *     HttpOnly attributes (SameSite is parsed-and-ignored for now);
 *   - Max-Age wins over Expires; an unparseable/absent expiry -> session cookie;
 *   - a Set-Cookie whose Domain does NOT domain-match the request host is
 *     REJECTED (anti-injection, RFC 6265 5.3); no Domain attribute -> host-only;
 *   - default-path derived from the request path (RFC 6265 5.1.4);
 *   - a cookie with an expiry in the past (or Max-Age <= 0) DELETES a match;
 *   - request matching: domain-match + path-match + (Secure -> https only) +
 *     not-expired; output sorted by descending path length (RFC 6265 5.4);
 *   - bounded jar: fixed entries, oldest-eviction; Secure cookies require a
 *     secure request to be sent.
 *
 * Cookie dates reuse the shared HTTP IMF-fixdate parser (http_cache_parse_date);
 * the legacy dash-separated Netscape date is treated as unparseable -> session.
 */

#define HTTP_COOKIE_MAX 16u
#define HTTP_COOKIE_NAME_MAX 64u
#define HTTP_COOKIE_VALUE_MAX 256u
#define HTTP_COOKIE_DOMAIN_MAX HTTP_MAX_HOST /* 192 */
#define HTTP_COOKIE_PATH_MAX 256u
/* Cap for the assembled `Cookie:` request header value. */
#define HTTP_COOKIE_HEADER_MAX 1024u

struct http_cookie {
  int valid;
  char name[HTTP_COOKIE_NAME_MAX];
  char value[HTTP_COOKIE_VALUE_MAX];
  char domain[HTTP_COOKIE_DOMAIN_MAX]; /* canonical, lower-case, no leading dot */
  char path[HTTP_COOKIE_PATH_MAX];
  long expires;   /* epoch seconds; 0 = session cookie (no persistent expiry) */
  int host_only;  /* 1 when no Domain attribute -> exact host match only */
  int secure;     /* only sent over https */
  int http_only;  /* not exposed to scripts (advisory; JS is blocked anyway) */
  uint64_t lru;   /* set/refresh order, for eviction */
};

struct http_cookie_jar {
  struct http_cookie cookies[HTTP_COOKIE_MAX];
  uint64_t clock;
  uint32_t set_count;  /* cookies accepted (stored or updated) */
  uint32_t deleted;    /* cookies removed by an expiring Set-Cookie */
  uint32_t rejected;   /* Set-Cookie rejected (domain mismatch / malformed) */
  uint32_t evictions;  /* entries evicted at capacity */
};

/* Reset the jar to empty. */
void http_cookie_jar_init(struct http_cookie_jar *j);

/* Parse a single Set-Cookie value into *out, resolving default domain/path from
 * the request (req_host / req_path) and the expiry against `now`. Returns 1 if
 * the cookie is well-formed AND its Domain domain-matches req_host; 0 otherwise
 * (rejected/malformed). Exposed for testing. */
int http_cookie_parse_set_cookie(const char *set_cookie, const char *req_host,
                                 const char *req_path, long now,
                                 struct http_cookie *out);

/* RFC 6265 5.1.3 domain match: 1 if `host` == `domain` or `host` is a subdomain
 * of `domain` (and `host` is not an IP literal). Both compared case-insensitively. */
int http_cookie_domain_match(const char *host, const char *domain);

/* RFC 6265 5.1.4 path match. */
int http_cookie_path_match(const char *req_path, const char *cookie_path);

/* Apply every Set-Cookie header in `resp` to the jar (store/update, or delete on
 * an expired cookie). req_host/req_path resolve defaults + the domain-match
 * gate; `now` is the clock. Returns the number of cookies accepted. */
int http_cookie_jar_set_from_response(struct http_cookie_jar *j,
                                      const char *req_host,
                                      const char *req_path,
                                      const struct http_response *resp, long now);

/* Build the `Cookie:` request-header value for (req_host, req_path, secure) at
 * `now` into `out` (cap bytes): every non-expired cookie that domain-matches,
 * path-matches and (if Secure) is on a secure request, joined as
 * "n1=v1; n2=v2" sorted by descending path length. Returns the written length
 * (0 if none match or out is too small). NUL-terminates when cap > 0. */
size_t http_cookie_jar_header(struct http_cookie_jar *j, const char *req_host,
                              const char *req_path, int secure, long now,
                              char *out, size_t cap);

/* Drop expired cookies (expires != 0 && expires <= now). */
void http_cookie_jar_gc(struct http_cookie_jar *j, long now);

#endif /* NET_HTTP_COOKIES_H */
