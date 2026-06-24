#ifndef NET_HTTP_FETCH_POLICY_H
#define NET_HTTP_FETCH_POLICY_H

/*
 * Fetch security policy (scheme allow-list + HTTPS-first navigation +
 * mixed-content blocking) for the CapyOS browser/fetch path (Etapa 7 — Slice
 * 7.6 hardening, foundational for safe sub-resource loading by 7.4/7.5). A
 * CapyOS-owned net-service policy (the browser core defers HTTPS-first /
 * mixed-content to the host adapter), sibling of http_cache/http_cookies.
 *
 * Pure, deterministic, fail-closed, freestanding: every decision is a function
 * of the (classified) scheme(s) alone, so it is fully host-testable and carries
 * no state. The gate a fetcher applies BEFORE opening a connection:
 *   - navigation:   where the user/redirect points the top-level document;
 *   - sub-resource: an image/style/script/etc. requested by a loaded document.
 *
 * Conservative by design (deny by default): an unknown scheme is BLOCK; an
 * insecure (http) sub-resource of a secure (https) page is BLOCK (mixed
 * content; passive resources are blocked too); `file:`/`ftp:`/`javascript:` are
 * never fetchable by web content.
 */

enum http_url_scheme {
  HTTP_SCHEME_HTTPS = 0,
  HTTP_SCHEME_HTTP,
  HTTP_SCHEME_WSS,
  HTTP_SCHEME_WS,
  HTTP_SCHEME_DATA,
  HTTP_SCHEME_BLOB,
  HTTP_SCHEME_ABOUT,
  HTTP_SCHEME_FILE,
  HTTP_SCHEME_FTP,
  HTTP_SCHEME_JAVASCRIPT,
  HTTP_SCHEME_OTHER /* unknown / unsupported -> treated as untrusted */
};

enum http_fetch_decision {
  HTTP_FETCH_BLOCK = 0,   /* refuse */
  HTTP_FETCH_ALLOW = 1,   /* proceed as-is */
  HTTP_FETCH_UPGRADE = 2  /* retry over https first (HTTPS-first) */
};

/* Classify a scheme token (case-insensitive, WITHOUT the trailing ':'); a
 * NULL/empty/unknown scheme is HTTP_SCHEME_OTHER. */
enum http_url_scheme http_fetch_classify_scheme(const char *scheme);

/* 1 if the scheme is a secure transport (https / wss), else 0. data:/blob:/
 * about: are not network transports and are NOT "secure" for this purpose. */
int http_fetch_scheme_is_secure(enum http_url_scheme s);

/* Top-level navigation policy (HTTPS-first): https -> ALLOW; http -> UPGRADE
 * (attempt https, the adapter may fall back per its own policy); data/about/
 * blob -> ALLOW (inline/internal); file/ftp/ws/wss/javascript/other -> BLOCK. */
enum http_fetch_decision http_fetch_policy_navigation(enum http_url_scheme target);

/* Sub-resource policy (mixed-content). `page_secure` is 1 when the embedding
 * document was loaded over a secure transport. A secure page blocks every
 * insecure (http/ws) sub-resource; both kinds block file/ftp/javascript. */
enum http_fetch_decision http_fetch_policy_subresource(int page_secure,
                                                       enum http_url_scheme sub);

const char *http_fetch_decision_name(enum http_fetch_decision d);
const char *http_url_scheme_name(enum http_url_scheme s);

#endif /* NET_HTTP_FETCH_POLICY_H */
