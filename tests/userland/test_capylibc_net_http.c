/*
 * tests/userland/test_capylibc_net_http.c
 *
 * `capy_http_get` end-to-end coverage for libcapy-net using the
 * recording fake stubs declared in
 * `tests/userland/test_capylibc_net_internal.h`. The tests exercise:
 *
 *   - happy-path GET + Content-Length body draining
 *   - LF-only response head terminator (single-recv + split-recv)
 *   - Body buffer truncation
 *   - Short Content-Length body rejection
 *   - HTTPS unsupported gate (fail-closed before any I/O)
 *   - TLS->net error mapping + invalid HTTPS adapter state
 *   - Status-code edge cases (404 empty body, 100/103 informational,
 *     204 no-content body tail, 304 with non-zero Content-Length)
 *   - Content-Encoding gating (identity accepted, gzip/empty rejected)
 *   - Header parser robustness (no separator, obs-fold, chunked,
 *     identity transfer encoding, bad Content-Length, overflow,
 *     duplicates, validations beyond stored header cap)
 *   - Request format on the wire (request target + Host + Connection
 *     close, fragment stripping)
 *
 * Carved out of `tests/test_capylibc_net.c` at the 2026-05-15
 * monolith refactor so each host-test translation unit stays under
 * the 900-line layout limit. The sockets/DNS coverage and the fake
 * stub definitions live in `tests/userland/test_capylibc_net.c`.
 */
#include <string.h>

#include "test_capylibc_net_internal.h"

static const char k_canned_response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_happy_path(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x01020304u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get happy path: parse status + headers + body");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 200 && r.header_count == 2 &&
      r.content_length == 13 && r.body_len == 13 && r.truncated == 0 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("end-to-end parse failed");
}

static const char k_canned_lf_only_response[] =
    "HTTP/1.1 200 OK\n"
    "Content-Length: 13\n"
    "Content-Type: text/plain\n"
    "\n"
    "Hello, world!";

static void test_http_get_lf_only_head(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_lf_only_response;
  g_fake.recv_canned_len = sizeof(k_canned_lf_only_response) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get accepts LF-only response head terminator");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/lf-only", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 200 && r.header_count == 2 &&
      r.content_length == 13 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("LF-only head terminator rejected");
}

static void test_http_get_lf_only_head_split_recv(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_lf_only_response;
  g_fake.recv_canned_len = sizeof(k_canned_lf_only_response) - 1;
  g_fake.recv_chunk_size = 7;

  TEST("http_get reassembles LF-only head split across recv calls");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/lf-only-split", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 200 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("split LF-only head terminator rejected");
}

static void test_http_get_chunked_recv(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;
  g_fake.recv_chunk_size = 7;

  TEST("http_get reassembles head split across multiple recv calls");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 200 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("chunked recv didn't reassemble");
}

static void test_http_get_body_truncated(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;

  TEST("http_get marks truncated when body_buf too small");
  uint8_t body[5];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  if (rc == 0 && r.body_len == 5 && r.truncated == 1 &&
      memcmp(body, "Hello", 5) == 0) PASS();
  else FAIL("truncation not flagged");
}

static void test_http_get_truncated_known_length_stops_without_eof_recv(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get stops at known Content-Length after truncation");
  uint8_t body[5];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  if (rc == 0 && r.body_len == 5 && r.truncated == 1 &&
      g_fake.recv_calls == 1 && memcmp(body, "Hello", 5) == 0) PASS();
  else FAIL("known Content-Length waited for EOF after truncation");
}

static const char k_canned_short_content_length_body[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello";

static void test_http_get_rejects_short_content_length_body(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_short_content_length_body;
  g_fake.recv_canned_len = sizeof(k_canned_short_content_length_body) - 1;

  TEST("http_get rejects EOF before Content-Length body is complete");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("short Content-Length body accepted");
}

static void test_http_get_https_unsupported(void) {
  fake_reset();
  TEST("http_get rejects https:// before DNS/socket I/O");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("https://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED &&
      g_fake.dns_calls == 0 && g_fake.socket_calls == 0 &&
      g_fake.connect_calls == 0 && g_fake.send_calls == 0 &&
      g_fake.recv_calls == 0 && g_fake.close_calls == 0) PASS();
  else FAIL("https touched network or was accepted");
}

static void test_https_tls_error_mapping(void) {
  TEST("https adapter maps libcapy-tls errors to libcapy-net errors");
  if (capy_net_internal_tls_error_to_net(CAPY_TLS_OK) == CAPY_NET_OK &&
      capy_net_internal_tls_error_to_net(CAPY_TLS_EINVAL) == CAPY_NET_EINVAL &&
      capy_net_internal_tls_error_to_net(CAPY_TLS_EUNSUPPORTED) ==
          CAPY_NET_EUNSUPPORTED &&
      capy_net_internal_tls_error_to_net(CAPY_TLS_ESTATE) ==
          CAPY_NET_EUNSUPPORTED &&
      capy_net_internal_tls_error_to_net((capy_tls_err_t)-99) ==
          CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("unexpected TLS error mapping");
}

static void test_https_adapter_rejects_invalid_state(void) {
  fake_reset();
  TEST("https adapter rejects invalid URL state");
  if (capy_net_internal_https_fail_closed(0) == -1 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("invalid https adapter state accepted");
}

static const char k_canned_404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_404_no_body(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_404;
  g_fake.recv_canned_len = sizeof(k_canned_404) - 1;

  TEST("http_get parses 404 with empty body");
  uint8_t body[8];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/missing", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 404 && r.body_len == 0 &&
      r.content_length == 0) PASS();
  else FAIL("404 not parsed");
}

static const char k_canned_100_then_200[] =
    "HTTP/1.1 100 Continue\r\n"
    "\r\n"
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 5\r\n"
    "\r\n"
    "Hello";

static void test_http_get_rejects_100_continue(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_100_then_200;
  g_fake.recv_canned_len = sizeof(k_canned_100_then_200) - 1;

  TEST("http_get rejects informational 100 response");
  uint8_t body[16];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/continue", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("100 Continue accepted as final response");
}

static const char k_canned_103_then_200[] =
    "HTTP/1.1 103 Early Hints\r\n"
    "Link: </style.css>\r\n"
    "\r\n"
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 5\r\n"
    "\r\n"
    "Hello";

static void test_http_get_rejects_103_early_hints(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_103_then_200;
  g_fake.recv_canned_len = sizeof(k_canned_103_then_200) - 1;

  TEST("http_get rejects informational 103 response");
  uint8_t body[16];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/early-hints", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("103 Early Hints accepted as final response");
}

static const char k_canned_204_with_tail[] =
    "HTTP/1.1 204 No Content\r\n"
    "\r\n"
    "Ignored";

static void test_http_get_204_ignores_body_tail(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_204_with_tail;
  g_fake.recv_canned_len = sizeof(k_canned_204_with_tail) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get treats 204 as known empty body");
  uint8_t body[16];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/no-content", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 204 && r.content_length == 0 &&
      r.body_len == 0 && r.truncated == 0 && g_fake.recv_calls == 1) PASS();
  else FAIL("204 response exposed body tail");
}

static const char k_canned_304_with_nonzero_content_length[] =
    "HTTP/1.1 304 Not Modified\r\n"
    "Content-Length: 1\r\n"
    "\r\n"
    "X";

static void test_http_get_rejects_304_nonzero_content_length(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_304_with_nonzero_content_length;
  g_fake.recv_canned_len = sizeof(k_canned_304_with_nonzero_content_length) - 1;

  TEST("http_get rejects non-zero Content-Length on 304");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/not-modified", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("304 with non-zero Content-Length accepted");
}

static const char k_canned_content_length_zero_with_tail[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 0\r\n"
    "\r\n"
    "Ignored";

static void test_http_get_content_length_zero_ignores_tail(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_content_length_zero_with_tail;
  g_fake.recv_canned_len = sizeof(k_canned_content_length_zero_with_tail) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get treats Content-Length zero as known empty body");
  uint8_t body[16];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/empty", body, sizeof(body), &r);
  if (rc == 0 && r.content_length == 0 && r.body_len == 0 &&
      r.truncated == 0 && g_fake.recv_calls == 1) PASS();
  else FAIL("Content-Length zero consumed tail bytes as body");
}

static const char k_canned_identity_content_encoding[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Encoding: identity\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_accepts_identity_content_encoding(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_identity_content_encoding;
  g_fake.recv_canned_len = sizeof(k_canned_identity_content_encoding) - 1;

  TEST("http_get accepts identity Content-Encoding");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/identity", body, sizeof(body), &r);
  if (rc == 0 && r.content_length == 13 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("identity Content-Encoding rejected");
}

static const char k_canned_gzip_content_encoding[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Encoding: gzip\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_gzip_content_encoding(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_gzip_content_encoding;
  g_fake.recv_canned_len = sizeof(k_canned_gzip_content_encoding) - 1;

  TEST("http_get rejects unsupported Content-Encoding");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/gzip", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("gzip Content-Encoding accepted without decoder");
}

static const char k_canned_empty_content_encoding[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Encoding:   \r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_rejects_empty_content_encoding(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_empty_content_encoding;
  g_fake.recv_canned_len = sizeof(k_canned_empty_content_encoding) - 1;

  TEST("http_get rejects empty Content-Encoding");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/empty-encoding", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("empty Content-Encoding accepted");
}

static const char k_canned_header_without_separator[] =
    "HTTP/1.1 200 OK\r\n"
    "X-Broken-Header\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_rejects_header_without_separator(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_header_without_separator;
  g_fake.recv_canned_len = sizeof(k_canned_header_without_separator) - 1;

  TEST("http_get rejects header without separator");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/no-header-separator", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("header without separator accepted by http_get");
}

static const char k_canned_obs_fold_header[] =
    "HTTP/1.1 200 OK\r\n"
    "X-Test: ok\r\n"
    " folded\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_rejects_obs_fold_header(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_obs_fold_header;
  g_fake.recv_canned_len = sizeof(k_canned_obs_fold_header) - 1;

  TEST("http_get rejects folded response headers");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/folded", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("folded response header accepted");
}

static const char k_canned_chunked[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "5\r\nHello\r\n0\r\n\r\n";

static void test_http_get_rejects_chunked(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_chunked;
  g_fake.recv_canned_len = sizeof(k_canned_chunked) - 1;

  TEST("http_get rejects chunked transfer-encoding");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("chunked accepted");
}

static const char k_canned_identity_transfer_encoding[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: identity\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_identity_transfer_encoding(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_identity_transfer_encoding;
  g_fake.recv_canned_len = sizeof(k_canned_identity_transfer_encoding) - 1;

  TEST("http_get rejects non-chunked Transfer-Encoding");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("non-chunked Transfer-Encoding accepted");
}

static const char k_canned_bad_content_length_suffix[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13x\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_bad_content_length_suffix(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_bad_content_length_suffix;
  g_fake.recv_canned_len = sizeof(k_canned_bad_content_length_suffix) - 1;

  TEST("http_get rejects Content-Length with suffix");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("bad Content-Length suffix accepted");
}

static const char k_canned_bad_content_length_overflow[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 184467440737095516160\r\n"
    "\r\n";

static void test_http_get_rejects_content_length_overflow(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_bad_content_length_overflow;
  g_fake.recv_canned_len = sizeof(k_canned_bad_content_length_overflow) - 1;

  TEST("http_get rejects Content-Length overflow");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("Content-Length overflow accepted");
}

static const char k_canned_duplicate_content_length_same[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_accepts_duplicate_content_length_same(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_duplicate_content_length_same;
  g_fake.recv_canned_len = sizeof(k_canned_duplicate_content_length_same) - 1;

  TEST("http_get accepts duplicate matching Content-Length");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  if (rc == 0 && r.content_length == 13 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("duplicate matching Content-Length rejected");
}

static const char k_canned_duplicate_content_length_conflict[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13\r\n"
    "Content-Length: 12\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_duplicate_content_length_conflict(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_duplicate_content_length_conflict;
  g_fake.recv_canned_len = sizeof(k_canned_duplicate_content_length_conflict) - 1;

  TEST("http_get rejects conflicting duplicate Content-Length");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("conflicting duplicate Content-Length accepted");
}

static const char k_canned_content_length_after_header_cap[] =
    "HTTP/1.1 200 OK\r\n"
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
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_uses_content_length_after_header_cap(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_content_length_after_header_cap;
  g_fake.recv_canned_len = sizeof(k_canned_content_length_after_header_cap) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get resolves Content-Length beyond stored header cap");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/late-cl", body, sizeof(body), &r);
  if (rc == 0 && r.header_count == CAPY_HTTP_MAX_HEADERS &&
      r.content_length == 13 && r.body_len == 13 && g_fake.recv_calls == 1 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("Content-Length beyond stored cap ignored");
}

static const char k_canned_content_length_conflict_after_header_cap[] =
    "HTTP/1.1 200 OK\r\n"
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
    "Content-Length: 13\r\n"
    "Content-Length: 12\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_content_length_conflict_after_header_cap(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_content_length_conflict_after_header_cap;
  g_fake.recv_canned_len = sizeof(k_canned_content_length_conflict_after_header_cap) - 1;

  TEST("http_get rejects Content-Length conflict beyond stored header cap");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/conflict-late-cl", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("late conflicting Content-Length accepted");
}

static const char k_canned_bad_header_value_after_cap[] =
    "HTTP/1.1 200 OK\r\n"
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
    "X-Late: ok\x01" "bad\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_rejects_bad_header_value_after_header_cap(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_bad_header_value_after_cap;
  g_fake.recv_canned_len = sizeof(k_canned_bad_header_value_after_cap) - 1;

  TEST("http_get validates header values beyond stored header cap");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/bad-late-header", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("bad header value beyond cap accepted");
}

static void test_http_get_strips_fragment_from_request_target(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;

  TEST("http_get strips URL fragment from request target");
  uint8_t body[64];
  struct capy_http_response r;
  capy_http_get("http://example.com/path?x=1#secret", body, sizeof(body), &r);
  g_fake.send_log[g_fake.send_log_len] = '\0';
  if (strstr((char *)g_fake.send_log, "GET /path?x=1 HTTP/1.1\r\n") != NULL &&
      strstr((char *)g_fake.send_log, "#secret") == NULL) PASS();
  else FAIL("fragment leaked into request target");
}

static void test_http_get_request_format(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;

  TEST("http_get sends well-formed GET request on the wire");
  uint8_t body[64];
  struct capy_http_response r;
  capy_http_get("http://example.com:8080/path?x=1", body, sizeof(body), &r);
  g_fake.send_log[g_fake.send_log_len] = '\0';
  if (strstr((char *)g_fake.send_log, "GET /path?x=1 HTTP/1.1\r\n") != NULL &&
      strstr((char *)g_fake.send_log, "Host: example.com:8080\r\n") != NULL &&
      strstr((char *)g_fake.send_log, "Connection: close\r\n\r\n") != NULL) PASS();
  else FAIL("request line / Host header malformed");
}

void test_capylibc_net_http_cases(void) {
  test_http_get_happy_path();
  test_http_get_lf_only_head();
  test_http_get_lf_only_head_split_recv();
  test_http_get_chunked_recv();
  test_http_get_body_truncated();
  test_http_get_truncated_known_length_stops_without_eof_recv();
  test_http_get_rejects_short_content_length_body();
  test_http_get_https_unsupported();
  test_https_tls_error_mapping();
  test_https_adapter_rejects_invalid_state();
  test_http_get_404_no_body();
  test_http_get_rejects_100_continue();
  test_http_get_rejects_103_early_hints();
  test_http_get_204_ignores_body_tail();
  test_http_get_rejects_304_nonzero_content_length();
  test_http_get_content_length_zero_ignores_tail();
  test_http_get_accepts_identity_content_encoding();
  test_http_get_rejects_gzip_content_encoding();
  test_http_get_rejects_empty_content_encoding();
  test_http_get_rejects_header_without_separator();
  test_http_get_rejects_obs_fold_header();
  test_http_get_rejects_chunked();
  test_http_get_rejects_identity_transfer_encoding();
  test_http_get_rejects_bad_content_length_suffix();
  test_http_get_rejects_content_length_overflow();
  test_http_get_accepts_duplicate_content_length_same();
  test_http_get_rejects_duplicate_content_length_conflict();
  test_http_get_uses_content_length_after_header_cap();
  test_http_get_rejects_content_length_conflict_after_header_cap();
  test_http_get_rejects_bad_header_value_after_header_cap();
  test_http_get_strips_fragment_from_request_target();
  test_http_get_request_format();
}
