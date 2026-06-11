/*
 * tests/net/test_http_url.c
 *
 * Host-side coverage for the url_request_builder TU (src/net/services/
 * http/url_request_builder.c): http_parse_url, http_build_request and
 * http_parse_status_line. All three are pure string logic — no
 * transport, DNS or allocation — so this test links only that TU and
 * supplies the single external symbol it references (g_http_last_error,
 * normally defined in prelude_headers_encoding.c, which is deliberately
 * not linked into this focused unit).
 *
 * The security cases lock the request-smuggling / CRLF-injection
 * defense: any byte <= 0x20 or == 0x7F anywhere in the URL must be
 * rejected before it can reach the wire through http_build_request's
 * Host header. http_parse_url returns 0 on success and a negative code
 * on failure, so these tests assert success/failure by sign without
 * depending on the internal HTTP_ERR_* enum values.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "net/http.h"

/* http_parse_url records its last error in this global via the
 * header-inline http_set_ok/http_fail. The production definition lives
 * in prelude_headers_encoding.c (not linked here), so provide it. */
int g_http_last_error = 0;

/* http_build_request / http_parse_status_line are declared in the
 * module-internal http_internal.h (not the public net/http.h). Forward-
 * declare them here so this focused unit can exercise them without
 * pulling the internal header's transport/TLS types into the host build;
 * they resolve at link time from the already-linked url_request_builder. */
int http_build_request(const struct http_request *req, char *buf,
                       size_t buf_size);
int http_parse_status_line(const char *line, int *status_code);
size_t http_parse_content_length(const char *value);

static int g_failures = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            printf("[FAIL] http_url: %s\n", (msg));                        \
        }                                                                  \
    } while (0)

static void test_parse_https_with_path(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    int rc = http_parse_url("https://example.com/path/page.html",
                            host, sizeof(host), path, sizeof(path),
                            &port, &tls);
    CHECK(rc == 0, "https url must parse");
    CHECK(tls == 1, "https sets use_tls");
    CHECK(port == 443, "https defaults to port 443");
    CHECK(strcmp(host, "example.com") == 0, "host extracted");
    CHECK(strcmp(path, "/path/page.html") == 0, "path extracted");
}

static void test_parse_http_default_port_and_root_path(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    int rc = http_parse_url("http://example.com", host, sizeof(host),
                            path, sizeof(path), &port, &tls);
    CHECK(rc == 0, "http url must parse");
    CHECK(tls == 0, "http clears use_tls");
    CHECK(port == 80, "http defaults to port 80");
    CHECK(strcmp(host, "example.com") == 0, "host extracted");
    CHECK(strcmp(path, "/") == 0, "missing path defaults to /");
}

static void test_parse_explicit_port(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    int rc = http_parse_url("https://example.com:8443/x", host, sizeof(host),
                            path, sizeof(path), &port, &tls);
    CHECK(rc == 0, "explicit-port url must parse");
    CHECK(port == 8443, "explicit port honored");
    CHECK(tls == 1, "scheme tls preserved with explicit port");
    CHECK(strcmp(host, "example.com") == 0, "host stops at colon");
    CHECK(strcmp(path, "/x") == 0, "path after port");
}

static void test_parse_no_scheme_treated_as_host(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    int rc = http_parse_url("example.com/p", host, sizeof(host),
                            path, sizeof(path), &port, &tls);
    CHECK(rc == 0, "scheme-less url parses as host");
    CHECK(tls == 0 && port == 80, "scheme-less defaults to http/80");
    CHECK(strcmp(host, "example.com") == 0, "host extracted");
    CHECK(strcmp(path, "/p") == 0, "path extracted");
}

/* ---- security: request-smuggling / CRLF-injection rejection ---- */

static void test_parse_rejects_crlf_injection(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    /* Embedded CRLF that would otherwise smuggle a second request into
     * the Host header line emitted by http_build_request. */
    int rc = http_parse_url("https://example.com\r\nGET /evil HTTP/1.1",
                            host, sizeof(host), path, sizeof(path),
                            &port, &tls);
    CHECK(rc != 0, "CR/LF in url must be rejected (smuggling defense)");
}

static void test_parse_rejects_space(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    int rc = http_parse_url("https://exa mple.com/", host, sizeof(host),
                            path, sizeof(path), &port, &tls);
    CHECK(rc != 0, "space in url must be rejected");
}

static void test_parse_rejects_control_and_del(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    int rc1 = http_parse_url("https://example.com/\x01", host, sizeof(host),
                             path, sizeof(path), &port, &tls);
    CHECK(rc1 != 0, "0x01 control byte must be rejected");
    int rc2 = http_parse_url("https://example.com/\x7f", host, sizeof(host),
                             path, sizeof(path), &port, &tls);
    CHECK(rc2 != 0, "0x7F DEL byte must be rejected");
}

/* ---- malformed authority / arguments ---- */

static void test_parse_rejects_empty_host(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    int rc1 = http_parse_url("https:///path", host, sizeof(host),
                             path, sizeof(path), &port, &tls);
    CHECK(rc1 != 0, "empty authority must be rejected");
    int rc2 = http_parse_url("https://:8080/", host, sizeof(host),
                             path, sizeof(path), &port, &tls);
    CHECK(rc2 != 0, "missing host before port must be rejected");
}

static void test_parse_rejects_colon_without_digits(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    int rc = http_parse_url("https://example.com:/path", host, sizeof(host),
                            path, sizeof(path), &port, &tls);
    CHECK(rc != 0, "colon with no port digits must be rejected");
}

static void test_parse_rejects_null_args(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    CHECK(http_parse_url(NULL, host, sizeof(host), path, sizeof(path),
                         &port, &tls) != 0, "NULL url rejected");
    CHECK(http_parse_url("https://example.com/", NULL, 0, path, sizeof(path),
                         &port, &tls) != 0, "NULL/zero host buffer rejected");
}

/* ---- http_build_request: the bytes that go on the wire ---- */

static void test_build_request_get_basic(void) {
    struct http_request req;
    char buf[1024];
    int n;
    const char *want = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n";
    size_t len;
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    strcpy(req.host, "example.com");
    strcpy(req.path, "/index.html");
    req.port = 443;
    req.use_tls = 1;
    n = http_build_request(&req, buf, sizeof(buf));
    CHECK(n > 0, "GET build returns positive length");
    CHECK((size_t)n == strlen(buf), "returned length matches written bytes");
    CHECK(strncmp(buf, want, strlen(want)) == 0,
          "request line + Host header (default 443 port omitted)");
    CHECK(strstr(buf, "\r\nConnection: close") != NULL,
          "Connection: close default header");
    CHECK(strstr(buf, "\r\nUser-Agent: CapyOS/") != NULL,
          "User-Agent default header");
    len = strlen(buf);
    CHECK(len >= 4u && strcmp(buf + len - 4, "\r\n\r\n") == 0,
          "request terminates with a blank line");
}

static void test_build_request_post_sets_content_length(void) {
    struct http_request req;
    char buf[1024];
    static const uint8_t body[] = "k=v";
    int n;
    memset(&req, 0, sizeof(req));
    req.method = HTTP_POST;
    strcpy(req.host, "api.example.com");
    strcpy(req.path, "/submit");
    req.port = 80;
    req.use_tls = 0;
    req.body = body;
    req.body_len = 3u;
    n = http_build_request(&req, buf, sizeof(buf));
    CHECK(n > 0, "POST build ok");
    CHECK(strncmp(buf, "POST /submit HTTP/1.1\r\n", 23) == 0,
          "POST request line");
    CHECK(strstr(buf, "\r\nContent-Length: 3") != NULL,
          "Content-Length derived from body_len");
}

static void test_build_request_includes_nondefault_port(void) {
    struct http_request req;
    char buf[1024];
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    strcpy(req.host, "example.com");
    strcpy(req.path, "/");
    req.port = 8080;
    req.use_tls = 0;
    CHECK(http_build_request(&req, buf, sizeof(buf)) > 0, "build ok");
    CHECK(strstr(buf, "Host: example.com:8080\r\n") != NULL,
          "non-default port appended to Host header");
}

static void test_build_request_appends_custom_header(void) {
    struct http_request req;
    char buf[1024];
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    strcpy(req.host, "example.com");
    strcpy(req.path, "/");
    req.port = 443;
    req.use_tls = 1;
    strcpy(req.headers[0].name, "X-Test");
    strcpy(req.headers[0].value, "yes");
    req.header_count = 1u;
    CHECK(http_build_request(&req, buf, sizeof(buf)) > 0, "build ok");
    CHECK(strstr(buf, "\r\nX-Test: yes") != NULL, "custom header appended");
}

static void test_build_request_rejects_tiny_buffer(void) {
    struct http_request req;
    char tiny[8];
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    strcpy(req.host, "example.com");
    strcpy(req.path, "/");
    req.port = 443;
    req.use_tls = 1;
    CHECK(http_build_request(&req, tiny, sizeof(tiny)) < 0,
          "tiny buffer must fail closed, never overflow");
}

/* ---- http_parse_status_line ---- */

static void test_status_line_parses_code(void) {
    int code = -1;
    CHECK(http_parse_status_line("HTTP/1.1 200 OK", &code) == 0 && code == 200,
          "200 OK");
    code = -1;
    CHECK(http_parse_status_line("HTTP/1.0 404 Not Found", &code) == 0 &&
              code == 404,
          "404 Not Found");
    code = -1;
    CHECK(http_parse_status_line("HTTP/1.1 301 Moved Permanently", &code) == 0 &&
              code == 301,
          "301 Moved Permanently");
}

static void test_status_line_rejects_non_http(void) {
    int code = -1;
    CHECK(http_parse_status_line("ICY 200 OK", &code) != 0,
          "non-HTTP status line rejected");
    CHECK(http_parse_status_line("HTTP/2 200", &code) != 0,
          "non-1.x version rejected");
}

static void test_status_line_rejects_overlong_code(void) {
    int code = -1;
    /* A status code with more than three digits is malformed and must be
     * rejected, not accumulated into an int until it overflows (UB). */
    CHECK(http_parse_status_line("HTTP/1.1 9999 OK", &code) != 0,
          "overlong status code rejected");
    code = -1;
    CHECK(http_parse_status_line("HTTP/1.1 200000000000 OK", &code) != 0,
          "absurdly long status code rejected");
}

/* ---- Content-Length parsing + overflow saturation ---- */

static void test_content_length_parses_value(void) {
    CHECK(http_parse_content_length("0") == 0u, "zero length");
    CHECK(http_parse_content_length("1024") == 1024u, "decimal length");
    /* The kernel header store strips only leading OWS, so a trailing space
     * can remain; the parser stops at the first non-digit, as before. */
    CHECK(http_parse_content_length("123 ") == 123u, "stops at trailing space");
    CHECK(http_parse_content_length("") == 0u, "empty value is zero");
    CHECK(http_parse_content_length(NULL) == 0u, "null value is zero");
}

static void test_content_length_saturates_on_overflow(void) {
    /* A Content-Length far beyond size_t must saturate to SIZE_MAX, never
     * wrap to a small value that would let the receive loop stop early and
     * mis-frame a reused keep-alive connection (response smuggling). */
    CHECK(http_parse_content_length("999999999999999999999999999999") == SIZE_MAX,
          "overflowing Content-Length saturates, not wraps");
}

/* ---- security: port out-of-range / integer-wrap rejection ---- */

static void test_parse_rejects_oversized_port(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    /* 65536 is one past the 16-bit port space; it must be rejected, not
     * wrapped to 0 (which would silently fall back to the scheme default). */
    CHECK(http_parse_url("https://example.com:65536/", host, sizeof(host),
                         path, sizeof(path), &port, &tls) != 0,
          "port 65536 must be rejected (no uint16 wrap)");
    /* 65590 would wrap to 54 without the range check, connecting to a
     * different port than the URL named. */
    port = 0;
    tls = -1;
    CHECK(http_parse_url("https://example.com:65590/x", host, sizeof(host),
                         path, sizeof(path), &port, &tls) != 0,
          "out-of-range port must be rejected, not wrapped");
    /* A digit run longer than any valid port must also fail closed. */
    port = 0;
    tls = -1;
    CHECK(http_parse_url("https://example.com:99999999999/", host, sizeof(host),
                         path, sizeof(path), &port, &tls) != 0,
          "absurdly long port must be rejected");
}

static void test_parse_accepts_max_port(void) {
    char host[256];
    char path[256];
    uint16_t port = 0;
    int tls = -1;
    /* The boundary value 65535 stays valid and is unaffected by the
     * out-of-range guard. */
    int rc = http_parse_url("https://example.com:65535/", host, sizeof(host),
                            path, sizeof(path), &port, &tls);
    CHECK(rc == 0 && port == 65535, "max valid port 65535 accepted");
}

int run_http_url_tests(void) {
    g_failures = 0;
    test_parse_https_with_path();
    test_parse_http_default_port_and_root_path();
    test_parse_explicit_port();
    test_parse_rejects_oversized_port();
    test_parse_accepts_max_port();
    test_parse_no_scheme_treated_as_host();
    test_parse_rejects_crlf_injection();
    test_parse_rejects_space();
    test_parse_rejects_control_and_del();
    test_parse_rejects_empty_host();
    test_parse_rejects_colon_without_digits();
    test_parse_rejects_null_args();
    test_build_request_get_basic();
    test_build_request_post_sets_content_length();
    test_build_request_includes_nondefault_port();
    test_build_request_appends_custom_header();
    test_build_request_rejects_tiny_buffer();
    test_status_line_parses_code();
    test_status_line_rejects_non_http();
    test_status_line_rejects_overlong_code();
    test_content_length_parses_value();
    test_content_length_saturates_on_overflow();
    if (g_failures == 0) printf("[PASS] http_url\n");
    return g_failures;
}
