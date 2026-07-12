/* Focused end-to-end tests for libcapy-net's strict streaming chunk decoder. */

#include <string.h>

#include "../../userland/lib/capylibc-net/capy_net_http_internal.h"
#include "test_capylibc_net_internal.h"

static int chunked_get(const uint8_t *response, size_t response_len,
                       size_t recv_chunk, uint8_t *body, size_t body_cap,
                       struct capy_http_response *out) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = response;
  g_fake.recv_canned_len = response_len;
  g_fake.recv_chunk_size = recv_chunk;
  return capy_http_get("http://example.com/chunked", body, body_cap, out);
}

static const char k_basic[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "4\r\nWiki\r\n"
    "5\r\npedia\r\n"
    "0\r\n\r\n";

static void test_chunked_basic_split_every_byte(void) {
  uint8_t body[16];
  struct capy_http_response r;
  TEST("http_get decodes chunked across every recv boundary");
  if (chunked_get((const uint8_t *)k_basic, sizeof(k_basic) - 1u, 1u,
                  body, sizeof(body), &r) == 0 &&
      r.status_code == 200 && r.content_length == 0u && r.body_len == 9u &&
      r.truncated == 0 && memcmp(body, "Wikipedia", 9u) == 0)
    PASS();
  else
    FAIL("valid split chunked response rejected or decoded incorrectly");
}

static const char k_extensions_and_trailers[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: ChUnKeD\r\n"
    "\r\n"
    "4;foo=bar;quoted=\"a b\"\r\nWiki\r\n"
    "0;done=yes\r\n"
    "ETag: \"v1\"\r\n"
    "X-Checksum: ok\r\n"
    "\r\n";

static void test_chunked_extensions_and_trailers(void) {
  uint8_t body[8];
  struct capy_http_response r;
  TEST("chunk extensions and bounded safe trailers are accepted");
  if (chunked_get((const uint8_t *)k_extensions_and_trailers,
                  sizeof(k_extensions_and_trailers) - 1u, 7u,
                  body, sizeof(body), &r) == 0 &&
      r.body_len == 4u && r.truncated == 0 && memcmp(body, "Wiki", 4u) == 0)
    PASS();
  else
    FAIL("valid extensions/trailers rejected");
}

static const char k_te_and_cl[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "Content-Length: 5\r\n"
    "\r\n"
    "5\r\nHello\r\n0\r\n\r\n";

static void test_chunked_rejects_te_plus_cl(void) {
  uint8_t body[16];
  struct capy_http_response r;
  TEST("http_get rejects Transfer-Encoding plus Content-Length");
  if (chunked_get((const uint8_t *)k_te_and_cl, sizeof(k_te_and_cl) - 1u,
                  0u, body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP)
    PASS();
  else
    FAIL("ambiguous TE+CL framing accepted");
}

static const char k_unsupported_te[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: gzip, chunked\r\n"
    "\r\n"
    "0\r\n\r\n";

static const char k_duplicate_te[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "0\r\n\r\n";

static void test_chunked_rejects_unsupported_or_duplicate_te(void) {
  uint8_t body[8];
  struct capy_http_response r;
  TEST("http_get rejects transfer-coding chains");
  if (chunked_get((const uint8_t *)k_unsupported_te,
                  sizeof(k_unsupported_te) - 1u, 0u, body, sizeof(body), &r) ==
          -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED)
    PASS();
  else
    FAIL("unsupported transfer-coding chain accepted");

  TEST("http_get rejects duplicate Transfer-Encoding fields");
  if (chunked_get((const uint8_t *)k_duplicate_te,
                  sizeof(k_duplicate_te) - 1u, 0u, body, sizeof(body), &r) ==
          -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED)
    PASS();
  else
    FAIL("duplicate Transfer-Encoding accepted");
}

static void test_chunked_truncates_but_drains(void) {
  uint8_t body[5];
  struct capy_http_response r;
  TEST("chunked body truncation keeps prefix and drains framing");
  if (chunked_get((const uint8_t *)k_basic, sizeof(k_basic) - 1u, 2u,
                  body, sizeof(body), &r) == 0 &&
      r.body_len == sizeof(body) && r.truncated == 1 &&
      memcmp(body, "Wikip", sizeof(body)) == 0 &&
      g_fake.recv_canned_pos == sizeof(k_basic) - 1u)
    PASS();
  else
    FAIL("truncated chunked body was not fully drained/validated");

  TEST("zero-capacity chunked body is drained and marked truncated");
  if (chunked_get((const uint8_t *)k_basic, sizeof(k_basic) - 1u, 3u,
                  NULL, 0u, &r) == 0 &&
      r.body_len == 0u && r.truncated == 1 &&
      g_fake.recv_canned_pos == sizeof(k_basic) - 1u)
    PASS();
  else
    FAIL("zero-capacity chunked response did not drain safely");
}

static const char k_incomplete_after_truncation[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "A\r\nabc";

static const char k_bad_size[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "Z\r\nx\r\n0\r\n\r\n";

static const char k_extra_after_end[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "0\r\n\r\nX";

static void test_chunked_rejects_malformed_or_incomplete(void) {
  uint8_t body[2];
  struct capy_http_response r;
  TEST("truncated buffer does not hide incomplete chunk framing");
  if (chunked_get((const uint8_t *)k_incomplete_after_truncation,
                  sizeof(k_incomplete_after_truncation) - 1u, 2u,
                  body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP)
    PASS();
  else
    FAIL("incomplete chunk accepted after output truncation");

  TEST("non-hex chunk size fails closed");
  if (chunked_get((const uint8_t *)k_bad_size, sizeof(k_bad_size) - 1u,
                  0u, body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP)
    PASS();
  else
    FAIL("invalid chunk size accepted");

  TEST("bytes after terminal trailer block fail closed");
  if (chunked_get((const uint8_t *)k_extra_after_end,
                  sizeof(k_extra_after_end) - 1u, 0u,
                  body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP)
    PASS();
  else
    FAIL("bytes after chunked message accepted");
}

static const char k_forbidden_trailer[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "0\r\nContent-Length: 0\r\n\r\n";

static const char k_folded_trailer[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "0\r\n folded\r\n\r\n";

static void test_chunked_rejects_dangerous_trailers(void) {
  uint8_t body[1];
  struct capy_http_response r;
  TEST("framing fields are forbidden in chunk trailers");
  if (chunked_get((const uint8_t *)k_forbidden_trailer,
                  sizeof(k_forbidden_trailer) - 1u, 4u,
                  body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP)
    PASS();
  else
    FAIL("Content-Length trailer accepted");

  TEST("obs-fold chunk trailer fails closed");
  if (chunked_get((const uint8_t *)k_folded_trailer,
                  sizeof(k_folded_trailer) - 1u, 4u,
                  body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP)
    PASS();
  else
    FAIL("folded trailer accepted");
}

static void test_chunked_metadata_budgets(void) {
  char response[7000];
  static const char prefix[] =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
  static const char suffix[] = "\r\nx\r\n0\r\n\r\n";
  size_t pos;
  size_t i;
  uint8_t body[1];
  struct capy_http_response r;

  memcpy(response, prefix, sizeof(prefix) - 1u);
  pos = sizeof(prefix) - 1u;
  response[pos++] = '1';
  response[pos++] = ';';
  for (i = 0u; i < CAPY_HTTP_CHUNK_LINE_MAX + 1u; ++i) response[pos++] = 'a';
  memcpy(response + pos, suffix, sizeof(suffix) - 1u);
  pos += sizeof(suffix) - 1u;
  TEST("chunk extension line budget is enforced");
  if (chunked_get((const uint8_t *)response, pos, 17u,
                  body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP)
    PASS();
  else
    FAIL("oversized chunk extension accepted");

  memcpy(response, prefix, sizeof(prefix) - 1u);
  pos = sizeof(prefix) - 1u;
  memcpy(response + pos, "0\r\n", 3u);
  pos += 3u;
  for (i = 0u; i < CAPY_HTTP_TRAILER_MAX_FIELDS + 1u; ++i) {
    memcpy(response + pos, "X: a\r\n", 6u);
    pos += 6u;
  }
  memcpy(response + pos, "\r\n", 2u);
  pos += 2u;
  TEST("chunk trailer field-count budget is enforced");
  if (chunked_get((const uint8_t *)response, pos, 23u,
                  body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP)
    PASS();
  else
    FAIL("too many trailer fields accepted");

  memcpy(response, prefix, sizeof(prefix) - 1u);
  pos = sizeof(prefix) - 1u;
  memcpy(response + pos, "0\r\n", 3u);
  pos += 3u;
  for (i = 0u; i < 6u; ++i) {
    size_t j;
    memcpy(response + pos, "X: ", 3u);
    pos += 3u;
    for (j = 0u; j < 700u; ++j) response[pos++] = 'a';
    memcpy(response + pos, "\r\n", 2u);
    pos += 2u;
  }
  memcpy(response + pos, "\r\n", 2u);
  pos += 2u;
  TEST("aggregate chunk trailer byte budget is enforced");
  if (chunked_get((const uint8_t *)response, pos, 31u,
                  body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP)
    PASS();
  else
    FAIL("oversized trailer block accepted");
}

void test_capylibc_net_chunked_cases(void) {
  test_chunked_basic_split_every_byte();
  test_chunked_extensions_and_trailers();
  test_chunked_rejects_te_plus_cl();
  test_chunked_rejects_unsupported_or_duplicate_te();
  test_chunked_truncates_but_drains();
  test_chunked_rejects_malformed_or_incomplete();
  test_chunked_rejects_dangerous_trailers();
  test_chunked_metadata_budgets();
}
