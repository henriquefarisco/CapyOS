#ifndef CAPYLIBC_NET_CAPY_NET_H
#define CAPYLIBC_NET_CAPY_NET_H

/* libcapy-net (F4 seção c parte 2/2, 2026-05-08)
 *
 * High-level userland networking façade on top of the raw
 * `capy_socket / capy_bind / capy_connect / capy_send / capy_recv`
 * stubs in `userland/lib/capylibc/syscall_stubs.S`. The intent is
 * to give userland binaries (capybrowser, future curl-like tools,
 * the update-agent) a POSIX-flavoured TCP client surface without
 * each one having to re-implement byte-order conversion, dotted
 * IPv4 parsing, full-write / full-read loops and rollback-on-error.
 *
 * Scope today:
 *   - byte-order helpers (host x86_64 is little-endian, network is
 *     big-endian; explicit host_order vs net_order in arg names);
 *   - dotted-decimal IPv4 parser/formatter (NO DNS yet -- the
 *     follow-up will route through the kernel's `dns_cache` via
 *     a dedicated syscall);
 *   - TCP client: `capy_tcp_connect_ip4`, `capy_send_all`,
 *     `capy_recv_all`, `capy_recv_until`.
 *
 * NOT in scope (intentionally deferred to later F4/F5 sessions):
 *   - DNS resolution (needs SYS_DNS or libcapy-net own resolver);
 *   - TLS (F4 seção d -- libcapy-tls + BearSSL userland blob);
 *   - listening servers (`capy_tcp_listen` etc; the kernel-side
 *     `socket_accept` returns -1 today so any wrapper would be
 *     dead code);
 *   - non-blocking I/O / event loop integration.
 *
 * Failure model: all functions return `< 0` on error, and the
 * specific code is recorded in a per-process static so the caller
 * can `capy_net_last_error()` to discriminate. The `< 0` return is
 * the primary signal; errno-style integration is for future libc.
 */

#include <stddef.h>
#include <stdint.h>

/* libcapy-tls types for the network diagnostic below: capy_net_diagnose_stage
 * combines the net error with the TLS state, since TLS handshake/cert failures
 * otherwise collapse into a generic net error code. capy_tls.h is a small,
 * dependency-free sibling header (no reverse include of capy_net.h). */
#include "capylibc-tls/capy_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* === Byte order ============================================== */

/* Host order (LE on x86_64) <-> network order (BE) for 16/32-bit
 * integers. Implemented as pure C so the same code links into the
 * x86_64 userland binary AND the host (arm64 / x86_64) test
 * harness; on every supported host the helpers therefore swap
 * bytes unconditionally. */
uint16_t capy_htons(uint16_t v);
uint32_t capy_htonl(uint32_t v);
uint16_t capy_ntohs(uint16_t v);
uint32_t capy_ntohl(uint32_t v);

/* === IPv4 dotted decimal ===================================== */

/* Parse "1.2.3.4" into a host-order uint32_t (1.2.3.4 -> 0x01020304).
 * Each octet must be a decimal integer 0..255 with no leading '+',
 * '-' or whitespace; an empty string is rejected; there must be
 * exactly four octets separated by '.'. Returns 0 on success and
 * `*out` is populated; returns -1 on any parse failure (and `*out`
 * is left untouched). */
int capy_inet_pton4(const char *str, uint32_t *out_host_order);

/* Format a host-order IPv4 address into the caller's buffer as a
 * dotted-decimal string. The buffer must be at least 16 bytes
 * (`"255.255.255.255"` + NUL). Returns the number of bytes
 * written (excluding the trailing NUL), or -1 if `cap < 16` or
 * `out == NULL`. */
int capy_inet_ntoa4(uint32_t ip_host_order, char *out, size_t cap);

/* === TCP client ============================================== */

/* Open an AF_INET / SOCK_STREAM socket and connect it to
 * `(ip_host_order, port_host_order)`. The function htons-es the
 * port and htonl-s the IP internally before populating the
 * `sockaddr_in` it hands to `capy_connect`.
 *
 * Returns the userland fd (>= 3) on success, -1 on failure. On
 * any failure path the partial state is rolled back (the kernel
 * socket fd, if it was already allocated, is closed via
 * `capy_close` so process FD slots don't leak). */
int capy_tcp_connect_ip4(uint32_t ip_host_order, uint16_t port_host_order);

/* Same as above, but the IP comes in as a dotted-decimal string.
 * Returns -1 if the string is not a well-formed IPv4 address; no
 * DNS resolution is attempted. */
int capy_tcp_connect_str(const char *ip_dotted, uint16_t port_host_order);

/* F4 seção c parte 3/3 (2026-05-08) -- resolve `host` to a
 * host-order IPv4 address. The function tries `capy_inet_pton4`
 * first so a literal address ("1.2.3.4") short-circuits without
 * touching the DNS cache. On parse failure it falls back to
 * `capy_dns_resolve` (kernel-side DNS cache lookup; no active
 * resolver yet -- the cache must be seeded). Returns 0 on
 * success and writes the IP into `*out_ip`; -1 on any failure
 * (NULL args, parse fail AND cache miss, etc.). */
int capy_resolve_host_ip4(const char *host, uint32_t *out_ip);

/* F4 seção c parte 3/3 (2026-05-08) -- one-shot TCP client that
 * accepts either a dotted-decimal IPv4 OR a hostname. Equivalent
 * to `capy_resolve_host_ip4(host, &ip)` followed by
 * `capy_tcp_connect_ip4(ip, port)`. Returns the userland fd on
 * success or -1 on any failure (resolution OR connection); the
 * specific cause is recorded in `capy_net_last_error()`. */
int capy_tcp_connect_host(const char *host, uint16_t port_host_order);

/* Send EXACTLY `len` bytes through `fd`, looping until either all
 * bytes are accepted by the kernel or a send returns 0 / -1. On
 * success returns `len` (cast to long). On a 0/-1 send before
 * completion returns the number of bytes that DID make it through
 * (which may be 0). The function never spins forever: a kernel
 * send returning 0 is treated as "buffer full and no progress"
 * and bubbles up as "no further bytes accepted". */
long capy_send_all(int fd, const void *buf, size_t len);

/* Receive up to `cap` bytes into `buf` via a single underlying
 * `capy_recv` call. Returns the byte count (>= 0); 0 means the
 * peer closed the socket cleanly; -1 on a kernel error. This is
 * a thin pass-through with NULL/0 sanitisation so the caller
 * doesn't have to repeat the same checks. */
long capy_recv_all(int fd, void *buf, size_t cap);

/* Receive bytes one at a time until either:
 *   - `terminator` is read (it IS included in the byte count);
 *   - `cap` bytes have been buffered without seeing the terminator;
 *   - the peer closes the connection (returns the partial count,
 *     0 if nothing was read);
 *   - a kernel error occurs (returns -1).
 *
 * The buffer is NOT NUL-terminated; the caller can do that via
 * `((char *)buf)[ret] = 0` if `ret < cap`. Useful for HTTP-style
 * line-oriented protocols where each line ends in '\n'. */
long capy_recv_until(int fd, void *buf, size_t cap, uint8_t terminator);

/* === URL parsing (F4 seção c parte 5, 2026-05-08) =========== */

#define CAPY_URL_MAX_HOST   192
#define CAPY_URL_MAX_PATH   768

/* Split a URL into scheme/host/port/path. The struct is filled
 * top-down; on parse failure no field is partially-valid. The
 * input must start with `http://` or `https://`. The host can be
 * a DNS-label hostname OR a dotted-decimal IPv4; bytes outside
 * `[A-Za-z0-9.-]`, empty labels, labels >63 bytes, and labels starting
 * or ending with `-` are rejected. The optional port is decimal between
 * `host` and the path (`host:8080/path`). The
 * path defaults to `/` if absent. Query strings are preserved in `path`,
 * while fragments (`#...`) are stripped before request construction.
 *
 * IPv6 literals (`[::1]`) are deliberately NOT supported in this
 * iteration; libcapy-net is IPv4-only until the kernel net stack
 * grows IPv6. Userinfo (`user:pass@host`) is also rejected --
 * security-sensitive footgun and apps that need it can pre-strip. */
struct capy_url_parts {
  int      is_https;          /* 0 = http, 1 = https */
  char     host[CAPY_URL_MAX_HOST];
  uint16_t port;              /* host-order; defaults: http=80, https=443 */
  char     path[CAPY_URL_MAX_PATH]; /* always non-empty; '/' if not given */
};

int capy_url_parse(const char *url, struct capy_url_parts *out);

/* === HTTP client (F4 seção c parte 5, 2026-05-08) ============ */

#define CAPY_HTTP_MAX_HEADERS         16
#define CAPY_HTTP_MAX_HEADER_NAME     48
#define CAPY_HTTP_MAX_HEADER_VALUE    192

struct capy_http_header {
  char name[CAPY_HTTP_MAX_HEADER_NAME];
  char value[CAPY_HTTP_MAX_HEADER_VALUE];
};

/* Response metadata. The body itself is written into the
 * caller-provided buffer (see `capy_http_get`). `body_len` is
 * the number of bytes actually written; if `truncated` is set
 * the on-wire body was longer than `body_buf_cap` and the tail
 * was dropped. `content_length` is the Content-Length header as
 * an integer (0 if absent or explicitly zero; malformed present values fail
 * `capy_http_get`). */
struct capy_http_response {
  int                     status_code;
  int                     header_count;
  struct capy_http_header headers[CAPY_HTTP_MAX_HEADERS];
  size_t                  body_len;
  size_t                  content_length;
  int                     truncated;
};

/* Look up a header by name, case-insensitive. Returns the value
 * pointer (NUL-terminated) if found, NULL otherwise. The pointer
 * is into the `response` struct and is valid as long as the
 * struct is. */
const char *capy_http_response_find_header(
    const struct capy_http_response *resp, const char *name);

/* One-shot HTTP/1.1 GET. Resolves `host`, opens a TCP socket,
 * sends the request, and parses the status line + headers + body. Response
 * heads may terminate with CRLFCRLF or LF-only empty lines.
 * (Content-Length only -- any Transfer-Encoding or non-identity
 * Content-Encoding is rejected as `CAPY_NET_EUNSUPPORTED` in this iteration).
 * The body is written
 * into `body_buf` up to `body_buf_cap`; if the on-wire body is
 * larger, `out->truncated = 1` and the tail bytes are silently
 * dropped (the connection is still drained so the kernel buffer
 * doesn't stall). Informational 1xx responses are rejected in this
 * iteration because multiple response heads are not yet supported. Statuses
 * 204 and 304 define an empty body. A present Content-Length, including zero,
 * defines
 * the body size and must be a strict decimal value that fits in size_t;
 * duplicate Content-Length values across the raw header block must agree;
 * non-zero Content-Length on a no-body status is rejected.
 * Malformed/truncated response header syntax/names/values and folded/continuation response
 * headers are rejected, including beyond the stored-header cap, and early EOF before that length is an
 * HTTP error. Returns 0 on success,
 * -1 on any failure;
 * inspect `capy_net_last_error()` for the cause.
 *
 * NOT supported in this iteration:
 *   - HTTPS (returns CAPY_NET_EUNSUPPORTED via libcapy-tls fail-closed adapter)
 *   - transfer-encoding response bodies
 *   - content-encoding response bodies except identity
 *   - informational 1xx response continuation to a final response
 *   - automatic redirect follow (caller must inspect Location
 *     and call again)
 *   - request body (POST/PUT) -- GET only
 *   - non-default ports rely on the URL parser; default 80
 *
 * `body_buf` may be NULL with `body_buf_cap == 0` if the caller
 * only cares about status_code and headers (a bare HEAD-style
 * probe; the body is then read and discarded byte-by-byte to
 * keep the socket clean). */
int capy_http_get(const char *url,
                  uint8_t *body_buf, size_t body_buf_cap,
                  struct capy_http_response *out);

/* Same as `capy_http_get` but emits `req_header_count` caller-supplied request
 * headers (e.g. `Cookie`, `If-None-Match`, `If-Modified-Since` from the browser
 * fetch/cache/cookie layer) after the standard Host/User-Agent/Accept block and
 * before the framing `Connection: close` terminator. This is the transport seam
 * a multi-fetch browser runtime drives: the runtime's cache/cookie session
 * decides which conditional/Cookie headers a request carries, then hands them
 * here verbatim.
 *
 * Each header is validated fail-closed BEFORE any socket is opened: the name
 * must be a valid HTTP token and the value must contain no raw CTL bytes (so a
 * `\r`/`\n` in either can never inject a second header line). Framing- and
 * routing-critical names the builder owns -- `Host`, `Connection`,
 * `Content-Length`, `Transfer-Encoding` -- are rejected (anti request-smuggling
 * / anti Host-routing confusion) with `CAPY_NET_EPARSE`. `req_headers` may be
 * NULL only when `req_header_count == 0`; otherwise it is `CAPY_NET_EINVAL`.
 * `capy_http_get(url, ...)` is exactly this with `(NULL, 0)` headers. */
int capy_http_get_with_headers(const char *url,
                               const struct capy_http_header *req_headers,
                               int req_header_count,
                               uint8_t *body_buf, size_t body_buf_cap,
                               struct capy_http_response *out);

/* Internal helpers exposed for unit tests and potential reuse by
 * a hand-rolled state machine (e.g. capybrowser when it adopts
 * libcapy-net). Stable contract so tests can pin the wire format
 * without reaching into anonymous statics. */

/* Format a GET request line + Host: header + final blank line into
 * `buf` (NUL-terminator NOT counted). Returns the byte count on
 * success, -1 if `buf` is too small or host/path/port are malformed. Port 0 is
 * rejected. Paths must be absolute request targets without fragments. Hosts follow the same
 * DNS-label byte/boundary rules as `capy_url_parse`. The caller picks the host
 * format (literal IP or hostname) -- libcapy-net does not
 * rewrite `host`. */
int capy_http_build_get_request(const char *host, uint16_t port,
                                 const char *path,
                                 char *buf, size_t buf_cap);

/* Same as `capy_http_build_get_request` but appends `req_header_count`
 * caller-supplied headers between the standard Host/User-Agent/Accept block and
 * the trailing `Connection: close` line. Names/values are validated as in
 * `capy_http_get_with_headers` (token name, CTL-free value, reserved framing
 * names rejected) -> -1 with `CAPY_NET_EPARSE` on a bad/reserved header. Returns
 * the byte count on success, -1 (and `CAPY_NET_EBUF`) if `buf` is too small.
 * `req_headers` may be NULL only when `req_header_count == 0`. */
int capy_http_build_get_request_ex(const char *host, uint16_t port,
                                    const char *path,
                                    const struct capy_http_header *req_headers,
                                    int req_header_count,
                                    char *buf, size_t buf_cap);

/* Parse "HTTP/1.1 200 OK\r\n" (or `\n`-only) into `*out_status`.
 * Status code must be decimal and in the HTTP 100..599 range; the code must be
 * followed by SP and the reason phrase must not contain raw control bytes.
 * Returns the index of the byte AFTER the status-line terminator
 * (so headers parsing can start there). Returns -1 on malformed
 * status line. */
int capy_http_parse_status_line(const char *buf, size_t len,
                                 int *out_status);

/* Walk header lines from a response header block through the trailing empty
 * line (`\r\n` or LF-only); truncated blocks without the empty terminator
 * are rejected. Stores up to `CAPY_HTTP_MAX_HEADERS`
 * entries into `out`; subsequent well-formed headers are ignored by this
 * parser output, but all non-empty header lines must contain ':'; header
 * syntax is still validated and `capy_http_get`
 * still scans the raw block for framing-critical headers. Folded/continuation
 * lines are rejected. Returns the number of headers stored. */
int capy_http_parse_headers(const char *buf, size_t header_block_len,
                             struct capy_http_response *out);

/* === Errors ================================================== */

typedef enum {
  CAPY_NET_OK            = 0,
  CAPY_NET_EINVAL        = -1,  /* NULL pointer, zero length, etc. */
  CAPY_NET_EPARSE        = -2,  /* malformed string (inet_pton4 / URL / status line) */
  CAPY_NET_ESOCK         = -3,  /* underlying capy_socket failed */
  CAPY_NET_ECONNECT      = -4,  /* underlying capy_connect failed */
  CAPY_NET_ESEND         = -5,  /* underlying capy_send failed */
  CAPY_NET_ERECV         = -6,  /* underlying capy_recv failed */
  CAPY_NET_EBUF          = -7,  /* caller buffer too small (e.g. ntoa cap < 16) */
  CAPY_NET_EDNS          = -8,  /* DNS lookup failed (cache miss, etc.) */
  CAPY_NET_EHTTP         = -9,  /* HTTP-level error (malformed response) */
  CAPY_NET_EUNSUPPORTED  = -10  /* feature not implemented (HTTPS, chunked, etc.) */
} capy_net_err_t;

/* The most recent error code logged by a libcapy-net call. The
 * value is reset to `CAPY_NET_OK` at the start of every call,
 * so reading it AFTER a failed call gives the cause; reading it
 * after success returns `CAPY_NET_OK`. The slot is a single
 * static int (libcapy-net is single-threaded today; pthread-style
 * TLS will land alongside the threading work). */
capy_net_err_t capy_net_last_error(void);

/* Stable, human-readable English description of a libcapy-net error
 * code (POSIX-strerror style). Always returns a non-NULL static string
 * — including for unknown/out-of-range codes — so callers can print it
 * unconditionally. This is the EN base; the CapyBrowse Text app layers
 * a localized, stage-aware diagnostic (DNS/TCP/TLS/HTTP) on top. */
const char *capy_net_strerror(capy_net_err_t err);

/* === Diagnostic stage (Etapa 6 / Slice 6.3) ================== */

/* User-facing stage of a failed network fetch. CapyBrowse Text uses
 * this to show a clear DNS/TCP/TLS/HTTP error. The base system owns
 * this error UX (browser-core-integration-contract.md §"CapyOS base
 * fornece ... UX de erro"); the HTML-to-text core stays in CapyBrowser. */
typedef enum {
  CAPY_NET_STAGE_OK    = 0,  /* no error */
  CAPY_NET_STAGE_INPUT,      /* URL/argument/unsupported feature */
  CAPY_NET_STAGE_DNS,        /* name resolution */
  CAPY_NET_STAGE_TCP,        /* socket / connect / send / recv */
  CAPY_NET_STAGE_TLS,        /* TLS handshake / certificate */
  CAPY_NET_STAGE_HTTP        /* HTTP response framing */
} capy_net_stage_t;

/* Classify a failed fetch into a stage. `tls_state` is the libcapy-tls
 * state for THIS request (`capy_tls_last_state()`, or CAPY_TLS_STATE_INIT
 * for a plain-HTTP request): a CAPY_TLS_STATE_ERROR pins the stage to TLS
 * even though the net layer collapses handshake/cert failures into a
 * generic code. Pure function — no global state, never fails. */
capy_net_stage_t capy_net_diagnose_stage(capy_net_err_t net_err,
                                         capy_tls_state_t tls_state);

/* Stable EN label for a stage (e.g. "TLS"). Always non-NULL. */
const char *capy_net_stage_name(capy_net_stage_t stage);

/* Friendly, localized one-line message for a diagnostic stage, for the
 * CapyBrowse Text error UX. `lang` is a language tag ("pt-BR", "en", "es",
 * NULL); EN is the mandatory fallback base (matches the system
 * localization_select rule). Strings are ASCII-only (same convention as the
 * kernel localization catalog, e.g. "Portugues"/"Espanol") so they render on
 * the framebuffer font without accent handling. libcapy-net is ring-3 and
 * cannot link the kernel localization catalog, so it carries its own scoped
 * net-diagnostic vocabulary here. Always non-NULL. */
const char *capy_net_stage_message(capy_net_stage_t stage, const char *lang);

/* Actionable localized hint to pair with capy_net_stage_message: the message
 * states what failed, the hint tells the user what to do next (check the
 * network, the address, or note that the certificate is untrusted). Same lang
 * rule and ASCII-only convention; CAPY_NET_STAGE_OK returns "" (no hint). The
 * CapyBrowse Text app prints it as a second line. Always non-NULL. */
const char *capy_net_stage_hint(capy_net_stage_t stage, const char *lang);

#ifdef __cplusplus
}
#endif

#endif /* CAPYLIBC_NET_CAPY_NET_H */
