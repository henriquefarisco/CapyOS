/*
 * tests/net/test_http_fetch_policy.c
 *
 * Host-side coverage for the fetch security policy (scheme classification +
 * HTTPS-first navigation + mixed-content sub-resource blocking,
 * src/net/services/http/http_fetch_policy.c). Pure, stateless decisions, so the
 * test just exercises the full scheme x context matrix and the security-critical
 * deny-by-default + mixed-content cases.
 */
#include <stdio.h>
#include <string.h>

#include "net/http_fetch_policy.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      printf("[FAIL] http_fetch_policy: %s\n", (msg));                        \
    }                                                                          \
  } while (0)

static void test_classify(void) {
  CHECK(http_fetch_classify_scheme("https") == HTTP_SCHEME_HTTPS, "https");
  CHECK(http_fetch_classify_scheme("HTTPS") == HTTP_SCHEME_HTTPS, "HTTPS ci");
  CHECK(http_fetch_classify_scheme("http") == HTTP_SCHEME_HTTP, "http");
  CHECK(http_fetch_classify_scheme("ws") == HTTP_SCHEME_WS, "ws");
  CHECK(http_fetch_classify_scheme("wss") == HTTP_SCHEME_WSS, "wss");
  CHECK(http_fetch_classify_scheme("data") == HTTP_SCHEME_DATA, "data");
  CHECK(http_fetch_classify_scheme("blob") == HTTP_SCHEME_BLOB, "blob");
  CHECK(http_fetch_classify_scheme("about") == HTTP_SCHEME_ABOUT, "about");
  CHECK(http_fetch_classify_scheme("file") == HTTP_SCHEME_FILE, "file");
  CHECK(http_fetch_classify_scheme("ftp") == HTTP_SCHEME_FTP, "ftp");
  CHECK(http_fetch_classify_scheme("javascript") == HTTP_SCHEME_JAVASCRIPT,
        "javascript");
  CHECK(http_fetch_classify_scheme("gopher") == HTTP_SCHEME_OTHER, "unknown");
  CHECK(http_fetch_classify_scheme("") == HTTP_SCHEME_OTHER, "empty");
  CHECK(http_fetch_classify_scheme(NULL) == HTTP_SCHEME_OTHER, "NULL");
  /* near-misses must not partial-match. */
  CHECK(http_fetch_classify_scheme("https-x") == HTTP_SCHEME_OTHER,
        "no prefix match");
  CHECK(http_fetch_classify_scheme("htt") == HTTP_SCHEME_OTHER, "no short match");
}

static void test_is_secure(void) {
  CHECK(http_fetch_scheme_is_secure(HTTP_SCHEME_HTTPS) == 1, "https secure");
  CHECK(http_fetch_scheme_is_secure(HTTP_SCHEME_WSS) == 1, "wss secure");
  CHECK(http_fetch_scheme_is_secure(HTTP_SCHEME_HTTP) == 0, "http insecure");
  CHECK(http_fetch_scheme_is_secure(HTTP_SCHEME_WS) == 0, "ws insecure");
  CHECK(http_fetch_scheme_is_secure(HTTP_SCHEME_DATA) == 0,
        "data not a secure transport");
}

static void test_navigation(void) {
  CHECK(http_fetch_policy_navigation(HTTP_SCHEME_HTTPS) == HTTP_FETCH_ALLOW,
        "nav https allow");
  CHECK(http_fetch_policy_navigation(HTTP_SCHEME_HTTP) == HTTP_FETCH_UPGRADE,
        "nav http upgrade (HTTPS-first)");
  CHECK(http_fetch_policy_navigation(HTTP_SCHEME_DATA) == HTTP_FETCH_ALLOW,
        "nav data allow");
  CHECK(http_fetch_policy_navigation(HTTP_SCHEME_ABOUT) == HTTP_FETCH_ALLOW,
        "nav about allow");
  CHECK(http_fetch_policy_navigation(HTTP_SCHEME_FILE) == HTTP_FETCH_BLOCK,
        "nav file block");
  CHECK(http_fetch_policy_navigation(HTTP_SCHEME_FTP) == HTTP_FETCH_BLOCK,
        "nav ftp block");
  CHECK(http_fetch_policy_navigation(HTTP_SCHEME_JAVASCRIPT) == HTTP_FETCH_BLOCK,
        "nav javascript block");
  CHECK(http_fetch_policy_navigation(HTTP_SCHEME_WS) == HTTP_FETCH_BLOCK,
        "nav ws block");
  CHECK(http_fetch_policy_navigation(HTTP_SCHEME_OTHER) == HTTP_FETCH_BLOCK,
        "nav unknown block");
}

static void test_subresource_secure_page(void) {
  /* mixed-content: a secure page only loads secure sub-resources. */
  CHECK(http_fetch_policy_subresource(1, HTTP_SCHEME_HTTPS) == HTTP_FETCH_ALLOW,
        "secure page + https sub");
  CHECK(http_fetch_policy_subresource(1, HTTP_SCHEME_WSS) == HTTP_FETCH_ALLOW,
        "secure page + wss sub");
  CHECK(http_fetch_policy_subresource(1, HTTP_SCHEME_HTTP) == HTTP_FETCH_BLOCK,
        "secure page + http sub BLOCKED (mixed content)");
  CHECK(http_fetch_policy_subresource(1, HTTP_SCHEME_WS) == HTTP_FETCH_BLOCK,
        "secure page + ws sub blocked");
  CHECK(http_fetch_policy_subresource(1, HTTP_SCHEME_DATA) == HTTP_FETCH_ALLOW,
        "secure page + data sub allow");
  CHECK(http_fetch_policy_subresource(1, HTTP_SCHEME_FILE) == HTTP_FETCH_BLOCK,
        "secure page + file sub block");
  CHECK(http_fetch_policy_subresource(1, HTTP_SCHEME_JAVASCRIPT) ==
            HTTP_FETCH_BLOCK,
        "secure page + javascript sub block");
  CHECK(http_fetch_policy_subresource(1, HTTP_SCHEME_OTHER) == HTTP_FETCH_BLOCK,
        "secure page + unknown sub block");
}

static void test_subresource_insecure_page(void) {
  CHECK(http_fetch_policy_subresource(0, HTTP_SCHEME_HTTP) == HTTP_FETCH_ALLOW,
        "insecure page + http sub allow");
  CHECK(http_fetch_policy_subresource(0, HTTP_SCHEME_HTTPS) == HTTP_FETCH_ALLOW,
        "insecure page + https sub allow");
  CHECK(http_fetch_policy_subresource(0, HTTP_SCHEME_WS) == HTTP_FETCH_BLOCK,
        "insecure page + ws sub block");
  CHECK(http_fetch_policy_subresource(0, HTTP_SCHEME_DATA) == HTTP_FETCH_ALLOW,
        "insecure page + data sub allow");
  CHECK(http_fetch_policy_subresource(0, HTTP_SCHEME_FILE) == HTTP_FETCH_BLOCK,
        "insecure page + file sub block");
  CHECK(http_fetch_policy_subresource(0, HTTP_SCHEME_JAVASCRIPT) ==
            HTTP_FETCH_BLOCK,
        "insecure page + javascript sub block");
}

static void test_real_world(void) {
  /* the security-critical case: an https page must not load an http image. */
  enum http_url_scheme img = http_fetch_classify_scheme("http");
  CHECK(http_fetch_policy_subresource(1, img) == HTTP_FETCH_BLOCK,
        "https page blocks an http image (mixed content)");
  /* names round-trip for diagnostics. */
  CHECK(strcmp(http_fetch_decision_name(HTTP_FETCH_BLOCK), "BLOCK") == 0,
        "decision name BLOCK");
  CHECK(strcmp(http_url_scheme_name(HTTP_SCHEME_HTTPS), "https") == 0,
        "scheme name https");
}

int run_http_fetch_policy_tests(void) {
  g_failures = 0;
  test_classify();
  test_is_secure();
  test_navigation();
  test_subresource_secure_page();
  test_subresource_insecure_page();
  test_real_world();
  if (g_failures == 0) printf("[http_fetch_policy] all fetch-policy tests passed\n");
  return g_failures;
}
