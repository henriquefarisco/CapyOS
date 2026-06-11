/*
 * tests/userland/test_capylibc_net_url.c
 *
 * URL parser + HTTP request builder + HTTP status line parser +
 * HTTP header parser coverage for libcapy-net. Carved out of
 * `tests/test_capylibc_net.c` at the 2026-05-15 monolith refactor so
 * each host-test translation unit stays under the 900-line layout
 * limit. The sockets/DNS coverage, the fake-stub definitions and the
 * top-level entry (`test_capylibc_net_run`) live in
 * `tests/userland/test_capylibc_net.c`. Shared TEST/PASS/FAIL macros,
 * the recording fake-state structure and run/pass counters come from
 * `tests/userland/test_capylibc_net_internal.h`.
 */
#include <string.h>

#include "test_capylibc_net_internal.h"

/* === URL parser (F4 seção c parte 5) ========================= */

static void test_url_parse_http_minimal(void) {
  fake_reset();
  TEST("url_parse: http://example.com");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://example.com", &u);
  if (rc == 0 && u.is_https == 0 && u.port == 80 &&
      strcmp(u.host, "example.com") == 0 &&
      strcmp(u.path, "/") == 0) PASS();
  else FAIL("minimal parse wrong");
}

static void test_url_parse_https_with_port_and_path(void) {
  fake_reset();
  TEST("url_parse: https://host.tld:8443/foo/bar");
  struct capy_url_parts u;
  int rc = capy_url_parse("https://host.tld:8443/foo/bar", &u);
  if (rc == 0 && u.is_https == 1 && u.port == 8443 &&
      strcmp(u.host, "host.tld") == 0 &&
      strcmp(u.path, "/foo/bar") == 0) PASS();
  else FAIL("full parse wrong");
}

static void test_url_parse_http_with_query(void) {
  fake_reset();
  TEST("url_parse: query string preserved in path");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://x.example/api?key=1&x=y", &u);
  if (rc == 0 && strcmp(u.path, "/api?key=1&x=y") == 0) PASS();
  else FAIL("query lost");
}

static void test_url_parse_http_no_path_with_query(void) {
  fake_reset();
  TEST("url_parse: synthesises leading '/' when only query");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://x.example?z=1", &u);
  if (rc == 0 && strcmp(u.path, "/?z=1") == 0) PASS();
  else FAIL("synthesised path wrong");
}

static void test_url_parse_strips_fragment_from_path(void) {
  fake_reset();
  TEST("url_parse strips fragment from path");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://x.example/docs/page#private", &u);
  if (rc == 0 && strcmp(u.path, "/docs/page") == 0) PASS();
  else FAIL("fragment leaked into path");
}

static void test_url_parse_strips_fragment_after_query(void) {
  fake_reset();
  TEST("url_parse preserves query but strips fragment");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://x.example/search?q=capy#secret", &u);
  if (rc == 0 && strcmp(u.path, "/search?q=capy") == 0) PASS();
  else FAIL("query/fragment split wrong");
}

static void test_url_parse_https_default_port(void) {
  fake_reset();
  TEST("url_parse: https://x.example defaults to port 443");
  struct capy_url_parts u;
  int rc = capy_url_parse("https://x.example", &u);
  if (rc == 0 && u.is_https == 1 && u.port == 443) PASS();
  else FAIL("default https port wrong");
}

static void test_url_parse_rejects_unknown_scheme(void) {
  fake_reset();
  TEST("url_parse rejects ftp://...");
  struct capy_url_parts u;
  if (capy_url_parse("ftp://example.com", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("ftp accepted");
}

static void test_url_parse_rejects_userinfo(void) {
  fake_reset();
  TEST("url_parse rejects userinfo (CVE-class footgun)");
  struct capy_url_parts u;
  if (capy_url_parse("http://user:pass@example.com/", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("userinfo accepted");
}

static void test_url_parse_rejects_backslash_in_host(void) {
  fake_reset();
  TEST("url_parse rejects backslash in host");
  struct capy_url_parts u;
  if (capy_url_parse("http://good.example\\evil/path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("backslash host accepted");
}

static void test_url_parse_rejects_percent_in_host(void) {
  fake_reset();
  TEST("url_parse rejects percent-encoded authority in host");
  struct capy_url_parts u;
  if (capy_url_parse("http://good.example%2fevil/path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("percent host accepted");
}

static void test_url_parse_rejects_empty_host_label(void) {
  fake_reset();
  TEST("url_parse rejects empty host label");
  struct capy_url_parts u;
  if (capy_url_parse("http://good..example/path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("empty host label accepted");
}

static void test_url_parse_rejects_hyphen_edge_host_label(void) {
  fake_reset();
  TEST("url_parse rejects hyphen-edge host label");
  struct capy_url_parts u;
  if (capy_url_parse("http://-bad.example/path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("hyphen-edge host label accepted");
}

static void test_url_parse_rejects_ipv6(void) {
  fake_reset();
  TEST("url_parse rejects IPv6 literal [::1]");
  struct capy_url_parts u;
  if (capy_url_parse("http://[::1]/", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("ipv6 accepted");
}

static void test_url_parse_rejects_empty_host(void) {
  fake_reset();
  TEST("url_parse rejects empty host (http:///path)");
  struct capy_url_parts u;
  if (capy_url_parse("http:///path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("empty host accepted");
}

static void test_url_parse_rejects_zero_port(void) {
  fake_reset();
  TEST("url_parse rejects port 0");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example:0/", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("port 0 accepted");
}

static void test_url_parse_rejects_overflow_port(void) {
  fake_reset();
  TEST("url_parse rejects port > 65535");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example:99999/", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("overflow port accepted");
}

static void test_url_parse_rejects_null(void) {
  fake_reset();
  TEST("url_parse rejects NULL url");
  struct capy_url_parts u;
  if (capy_url_parse(NULL, &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("NULL url accepted");
}

static void test_url_parse_rejects_raw_crlf_in_path(void) {
  fake_reset();
  TEST("url_parse rejects raw CRLF in path");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/a\r\nHost:evil", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw CRLF accepted");
}

static void test_url_parse_rejects_raw_tab_in_query(void) {
  fake_reset();
  TEST("url_parse rejects raw tab in query");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/search?q=ok\tbad", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw tab accepted");
}

static void test_url_parse_rejects_backslash_in_path(void) {
  fake_reset();
  TEST("url_parse rejects backslash in request target");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/a\\b", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("backslash path accepted");
}

static void test_url_parse_rejects_percent_nul_in_path(void) {
  fake_reset();
  TEST("url_parse rejects %00 in path");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/a%00b", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("percent NUL path accepted");
}

static void test_url_parse_rejects_percent_nul_in_query(void) {
  fake_reset();
  TEST("url_parse rejects %00 in query");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/search?q=%00", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("percent NUL query accepted");
}

/* === HTTP request builder ==================================== */

static void test_http_build_get_default_port(void) {
  fake_reset();
  TEST("http_build_get default port 80 -> no :port in Host");
  char buf[512];
  int n = capy_http_build_get_request("example.com", 80, "/", buf, sizeof(buf));
  if (n > 0 && strstr(buf, "GET / HTTP/1.1\r\n") != NULL &&
      strstr(buf, "Host: example.com\r\n") != NULL &&
      strstr(buf, "Host: example.com:80\r\n") == NULL &&
      strstr(buf, "Connection: close\r\n\r\n") != NULL) PASS();
  else FAIL("request format wrong");
}

static void test_http_build_get_https_default_port(void) {
  fake_reset();
  TEST("http_build_get default https port 443 -> no :port in Host");
  char buf[512];
  /* Slice 5.5: real userland HTTPS resolves to port 443. The Host header
   * must omit the scheme-default port (bare authority), not "host:443". */
  int n = capy_http_build_get_request("example.com", 443, "/", buf, sizeof(buf));
  if (n > 0 && strstr(buf, "GET / HTTP/1.1\r\n") != NULL &&
      strstr(buf, "Host: example.com\r\n") != NULL &&
      strstr(buf, "Host: example.com:443\r\n") == NULL &&
      strstr(buf, "Connection: close\r\n\r\n") != NULL) PASS();
  else FAIL("https default port should be omitted from Host");
}

static void test_http_build_get_custom_port(void) {
  fake_reset();
  TEST("http_build_get custom port 8080 -> Host: host:8080");
  char buf[512];
  int n = capy_http_build_get_request("x.example", 8080, "/api", buf, sizeof(buf));
  if (n > 0 && strstr(buf, "GET /api HTTP/1.1\r\n") != NULL &&
      strstr(buf, "Host: x.example:8080\r\n") != NULL) PASS();
  else FAIL("custom port not in Host");
}

static void test_http_build_get_rejects_zero_port(void) {
  fake_reset();
  TEST("http_build_get rejects port 0");
  char buf[512];
  if (capy_http_build_get_request("x.example", 0, "/", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("port 0 accepted");
}

static void test_http_build_get_buffer_too_small(void) {
  fake_reset();
  TEST("http_build_get returns -1 when buf overflows during build");
  char buf[40];
  if (capy_http_build_get_request("x.example", 80, "/some/path", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EBUF) PASS();
  else FAIL("did not detect overflow");
}

static void test_http_build_get_rejects_raw_crlf_host(void) {
  fake_reset();
  TEST("http_build_get rejects raw CRLF in host");
  char buf[512];
  if (capy_http_build_get_request("x.example\r\nX:evil", 80, "/", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw CRLF host accepted");
}

static void test_http_build_get_rejects_ambiguous_host_char(void) {
  fake_reset();
  TEST("http_build_get rejects ambiguous host char");
  char buf[512];
  if (capy_http_build_get_request("x.example\\evil", 80, "/", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("ambiguous host accepted");
}

static void test_http_build_get_rejects_bad_host_label_boundary(void) {
  fake_reset();
  TEST("http_build_get rejects bad host label boundary");
  char buf[512];
  if (capy_http_build_get_request("bad-.example", 80, "/", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("bad host label boundary accepted");
}

static void test_http_build_get_rejects_raw_tab_path(void) {
  fake_reset();
  TEST("http_build_get rejects raw tab in path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "/ok\tbad", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw tab path accepted");
}

static void test_http_build_get_rejects_fragment_in_path(void) {
  fake_reset();
  TEST("http_build_get rejects fragment in path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "/path#secret", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("fragment path accepted");
}

static void test_http_build_get_rejects_backslash_path(void) {
  fake_reset();
  TEST("http_build_get rejects backslash in path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "/a\\b", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("backslash path accepted");
}

static void test_http_build_get_rejects_percent_nul_path(void) {
  fake_reset();
  TEST("http_build_get rejects %00 in path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "/a%00b", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("percent NUL path accepted");
}

static void test_http_build_get_rejects_relative_path(void) {
  fake_reset();
  TEST("http_build_get rejects non-empty relative path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "relative", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("relative path accepted");
}

/* === HTTP status line parser ================================= */

static void test_http_parse_status_200(void) {
  fake_reset();
  TEST("parse_status_line: HTTP/1.1 200 OK");
  const char *s = "HTTP/1.1 200 OK\r\nrest";
  int code = 0;
  int after = capy_http_parse_status_line(s, strlen(s), &code);
  if (after == 17 && code == 200) PASS();
  else FAIL("status 200 parse wrong");
}

static void test_http_parse_status_404(void) {
  fake_reset();
  TEST("parse_status_line: HTTP/1.0 404 Not Found");
  const char *s = "HTTP/1.0 404 Not Found\nbody";
  int code = 0;
  int after = capy_http_parse_status_line(s, strlen(s), &code);
  if (after == 23 && code == 404) PASS();
  else FAIL("status 404 parse wrong");
}

static void test_http_parse_status_rejects_bad_prefix(void) {
  fake_reset();
  TEST("parse_status_line rejects HTTP/2.0 prefix");
  int code = 0;
  if (capy_http_parse_status_line("HTTP/2.0 200 OK\r\n", 17, &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("HTTP/2.0 accepted");
}

static void test_http_parse_status_rejects_non_digit_code(void) {
  fake_reset();
  TEST("parse_status_line rejects non-digit status code");
  int code = 0;
  if (capy_http_parse_status_line("HTTP/1.1 X00 Bad\r\n", 18, &code) == -1) PASS();
  else FAIL("non-digit code accepted");
}

static void test_http_parse_status_rejects_below_range(void) {
  fake_reset();
  TEST("parse_status_line rejects status below HTTP range");
  const char *s = "HTTP/1.1 099 Weird\r\n";
  int code = 0;
  if (capy_http_parse_status_line(s, strlen(s), &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("status 099 accepted");
}

static void test_http_parse_status_rejects_above_range(void) {
  fake_reset();
  TEST("parse_status_line rejects status above HTTP range");
  const char *s = "HTTP/1.1 600 Weird\r\n";
  int code = 0;
  if (capy_http_parse_status_line(s, strlen(s), &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("status 600 accepted");
}

static void test_http_parse_status_rejects_missing_reason_separator(void) {
  fake_reset();
  TEST("parse_status_line rejects missing reason separator");
  int code = 0;
  if (capy_http_parse_status_line("HTTP/1.1 200OK\r\n", 16, &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("status line without separator accepted");
}

static void test_http_parse_status_rejects_ctl_reason(void) {
  fake_reset();
  TEST("parse_status_line rejects raw control in reason phrase");
  const char *s = "HTTP/1.1 200 O" "\x01" "K\r\n";
  int code = 0;
  if (capy_http_parse_status_line(s, strlen(s), &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw control in reason phrase accepted");
}

/* === HTTP header parser ====================================== */

static void test_http_parse_headers_basic(void) {
  fake_reset();
  TEST("parse_headers: 2 headers + empty terminator");
  const char *block =
      "Content-Length: 42\r\n"
      "Server: nginx\r\n"
      "\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  int n = capy_http_parse_headers(block, strlen(block), &r);
  if (n == 2 &&
      strcmp(r.headers[0].name, "Content-Length") == 0 &&
      strcmp(r.headers[0].value, "42") == 0 &&
      strcmp(r.headers[1].name, "Server") == 0 &&
      strcmp(r.headers[1].value, "nginx") == 0) PASS();
  else FAIL("header values wrong");
}

static void test_http_parse_headers_strips_ows(void) {
  fake_reset();
  TEST("parse_headers strips leading/trailing OWS");
  const char *block = "X-Custom:    spaced value   \r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  capy_http_parse_headers(block, strlen(block), &r);
  if (r.header_count == 1 &&
      strcmp(r.headers[0].value, "spaced value") == 0) PASS();
  else FAIL("OWS not stripped");
}

static void test_http_parse_headers_rejects_unterminated_line(void) {
  fake_reset();
  TEST("parse_headers rejects unterminated header line");
  const char *block = "X-Test: ok";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("unterminated header line accepted");
}

static void test_http_parse_headers_rejects_missing_empty_terminator(void) {
  fake_reset();
  TEST("parse_headers rejects missing empty terminator");
  const char *block = "X-Test: ok\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("header block without empty terminator accepted");
}

static void test_http_parse_headers_rejects_missing_separator(void) {
  fake_reset();
  TEST("parse_headers rejects header without separator");
  const char *block = "X-Broken-Header\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("header without separator accepted");
}

static void test_http_parse_headers_rejects_bad_name_space(void) {
  fake_reset();
  TEST("parse_headers rejects space in header name");
  const char *block = "Bad Name: value\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("bad header name accepted");
}

static void test_http_parse_headers_rejects_ctl_value(void) {
  fake_reset();
  TEST("parse_headers rejects raw control in header value");
  const char *block = "X-Test: ok\x01" "bad\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("bad header value accepted");
}

static void test_http_parse_headers_rejects_obs_fold(void) {
  fake_reset();
  TEST("parse_headers rejects folded continuation lines");
  const char *block = "X-Test: ok\r\n folded\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("folded header continuation accepted");
}

static const char k_header_block_bad_name_after_cap[] =
    "X-Fill-00: a\r\n"
    "X-Fill-01: b\r\n"
    "X-Fill-02: c\r\n"
    "X-Fill-03: d\r\n"
    "X-Fill-04: e\r\n"
    "X-Fill-05: f\r\n"
    "X-Fill-06: g\r\n"
    "X-Fill-07: h\r\n"
    "X-Fill-08: i\r\n"
    "X-Fill-09: j\r\n"
    "X-Fill-10: k\r\n"
    "X-Fill-11: l\r\n"
    "X-Fill-12: m\r\n"
    "X-Fill-13: n\r\n"
    "X-Fill-14: o\r\n"
    "X-Fill-15: p\r\n"
    "Bad Name: value\r\n"
    "\r\n";

static void test_http_parse_headers_rejects_bad_name_after_header_cap(void) {
  fake_reset();
  TEST("parse_headers validates names beyond stored header cap");
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(k_header_block_bad_name_after_cap,
                              strlen(k_header_block_bad_name_after_cap), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("bad header name beyond cap accepted");
}

static void test_http_parse_headers_keeps_internal_tab_value(void) {
  fake_reset();
  TEST("parse_headers allows HTAB inside header value");
  const char *block = "X-Test: ok\tvalue\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  int n = capy_http_parse_headers(block, strlen(block), &r);
  if (n == 1 && strcmp(r.headers[0].value, "ok\tvalue") == 0) PASS();
  else FAIL("HTAB value rejected");
}

static void test_http_response_find_header_ci(void) {
  fake_reset();
  TEST("find_header is case-insensitive");
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  r.header_count = 1;
  strcpy(r.headers[0].name, "Content-Type");
  strcpy(r.headers[0].value, "text/html");
  const char *v1 = capy_http_response_find_header(&r, "content-type");
  const char *v2 = capy_http_response_find_header(&r, "CONTENT-TYPE");
  const char *v3 = capy_http_response_find_header(&r, "Other");
  if (v1 && strcmp(v1, "text/html") == 0 &&
      v2 && strcmp(v2, "text/html") == 0 &&
      v3 == NULL) PASS();
  else FAIL("CI lookup broken");
}

void test_capylibc_net_url_cases(void) {
  test_url_parse_http_minimal();
  test_url_parse_https_with_port_and_path();
  test_url_parse_http_with_query();
  test_url_parse_http_no_path_with_query();
  test_url_parse_strips_fragment_from_path();
  test_url_parse_strips_fragment_after_query();
  test_url_parse_https_default_port();
  test_url_parse_rejects_unknown_scheme();
  test_url_parse_rejects_userinfo();
  test_url_parse_rejects_backslash_in_host();
  test_url_parse_rejects_percent_in_host();
  test_url_parse_rejects_empty_host_label();
  test_url_parse_rejects_hyphen_edge_host_label();
  test_url_parse_rejects_ipv6();
  test_url_parse_rejects_empty_host();
  test_url_parse_rejects_zero_port();
  test_url_parse_rejects_overflow_port();
  test_url_parse_rejects_null();
  test_url_parse_rejects_raw_crlf_in_path();
  test_url_parse_rejects_raw_tab_in_query();
  test_url_parse_rejects_backslash_in_path();
  test_url_parse_rejects_percent_nul_in_path();
  test_url_parse_rejects_percent_nul_in_query();
  test_http_build_get_default_port();
  test_http_build_get_https_default_port();
  test_http_build_get_custom_port();
  test_http_build_get_rejects_zero_port();
  test_http_build_get_buffer_too_small();
  test_http_build_get_rejects_raw_crlf_host();
  test_http_build_get_rejects_ambiguous_host_char();
  test_http_build_get_rejects_bad_host_label_boundary();
  test_http_build_get_rejects_raw_tab_path();
  test_http_build_get_rejects_fragment_in_path();
  test_http_build_get_rejects_backslash_path();
  test_http_build_get_rejects_percent_nul_path();
  test_http_build_get_rejects_relative_path();
  test_http_parse_status_200();
  test_http_parse_status_404();
  test_http_parse_status_rejects_bad_prefix();
  test_http_parse_status_rejects_non_digit_code();
  test_http_parse_status_rejects_below_range();
  test_http_parse_status_rejects_above_range();
  test_http_parse_status_rejects_missing_reason_separator();
  test_http_parse_status_rejects_ctl_reason();
  test_http_parse_headers_basic();
  test_http_parse_headers_strips_ows();
  test_http_parse_headers_rejects_unterminated_line();
  test_http_parse_headers_rejects_missing_empty_terminator();
  test_http_parse_headers_rejects_missing_separator();
  test_http_parse_headers_rejects_bad_name_space();
  test_http_parse_headers_rejects_ctl_value();
  test_http_parse_headers_rejects_obs_fold();
  test_http_parse_headers_rejects_bad_name_after_header_cap();
  test_http_parse_headers_keeps_internal_tab_value();
  test_http_response_find_header_ci();
}
