/*
 * tests/net/test_http_hsts.c
 *
 * Host-side coverage for the HSTS store (RFC 6797 subset,
 * src/net/services/http/http_hsts.c). Pure policy + caller-owned store with an
 * injected clock. Locks the secure-only intake, max-age (incl. =0 removal +
 * required), includeSubDomains matching, IP-host rejection, expiry/gc, eviction
 * and fail-closed paths.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "net/http_hsts.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      printf("[FAIL] http_hsts: %s\n", (msg));                                \
    }                                                                          \
  } while (0)

static void test_basic(void) {
  static struct http_hsts_store s;
  http_hsts_init(&s);
  CHECK(http_hsts_process_header(&s, "example.com", "max-age=1000", 1, 1000) == 1,
        "store over https");
  CHECK(http_hsts_should_upgrade(&s, "example.com", 1500) == 1,
        "exact host upgraded");
  CHECK(http_hsts_should_upgrade(&s, "other.com", 1500) == 0,
        "unrelated host not upgraded");
  /* no includeSubDomains -> a subdomain is NOT covered. */
  CHECK(http_hsts_should_upgrade(&s, "www.example.com", 1500) == 0,
        "subdomain not covered without includeSubDomains");
}

static void test_insecure_ignored(void) {
  static struct http_hsts_store s;
  http_hsts_init(&s);
  CHECK(http_hsts_process_header(&s, "example.com", "max-age=1000", 0, 1000) == 0,
        "HSTS header over http ignored");
  CHECK(http_hsts_should_upgrade(&s, "example.com", 1500) == 0,
        "nothing stored from insecure header");
}

static void test_missing_max_age(void) {
  static struct http_hsts_store s;
  http_hsts_init(&s);
  CHECK(http_hsts_process_header(&s, "example.com", "includeSubDomains", 1,
                                 1000) == 0,
        "header without max-age ignored");
  CHECK(http_hsts_should_upgrade(&s, "example.com", 1500) == 0, "not stored");
}

static void test_max_age_zero_removes(void) {
  static struct http_hsts_store s;
  http_hsts_init(&s);
  http_hsts_process_header(&s, "example.com", "max-age=1000", 1, 1000);
  CHECK(http_hsts_should_upgrade(&s, "example.com", 1500) == 1, "stored first");
  CHECK(http_hsts_process_header(&s, "example.com", "max-age=0", 1, 1100) == 1,
        "max-age=0 removes");
  CHECK(http_hsts_should_upgrade(&s, "example.com", 1500) == 0, "removed");
  CHECK(s.deleted >= 1, "delete stat");
  /* max-age=0 for an unknown host is a no-op. */
  CHECK(http_hsts_process_header(&s, "ghost.com", "max-age=0", 1, 1100) == 0,
        "max-age=0 unknown host no-op");
}

static void test_include_subdomains(void) {
  static struct http_hsts_store s;
  http_hsts_init(&s);
  http_hsts_process_header(&s, "example.com", "max-age=1000; includeSubDomains",
                           1, 1000);
  CHECK(http_hsts_should_upgrade(&s, "example.com", 1500) == 1, "apex upgraded");
  CHECK(http_hsts_should_upgrade(&s, "www.example.com", 1500) == 1,
        "subdomain upgraded");
  CHECK(http_hsts_should_upgrade(&s, "a.b.example.com", 1500) == 1,
        "deep subdomain upgraded");
  CHECK(http_hsts_should_upgrade(&s, "notexample.com", 1500) == 0,
        "suffix-without-dot not upgraded");
  CHECK(http_hsts_should_upgrade(&s, "example.com.evil.com", 1500) == 0,
        "prefix host not upgraded");
}

static void test_ip_rejected(void) {
  static struct http_hsts_store s;
  http_hsts_init(&s);
  CHECK(http_hsts_process_header(&s, "1.2.3.4", "max-age=1000", 1, 1000) == 0,
        "IP-literal host rejected");
  CHECK(http_hsts_should_upgrade(&s, "1.2.3.4", 1500) == 0, "IP not stored");
}

static void test_expiry_and_gc(void) {
  static struct http_hsts_store s;
  http_hsts_init(&s);
  http_hsts_process_header(&s, "example.com", "max-age=100", 1, 1000);
  CHECK(http_hsts_should_upgrade(&s, "example.com", 1050) == 1, "fresh");
  CHECK(http_hsts_should_upgrade(&s, "example.com", 2000) == 0, "expired");
  http_hsts_gc(&s, 2000);
  CHECK(http_hsts_should_upgrade(&s, "example.com", 1050) == 0, "gc removed");
}

static void test_quoted_and_whitespace(void) {
  static struct http_hsts_store s;
  http_hsts_init(&s);
  CHECK(http_hsts_process_header(&s, "q.com", "max-age=\"500\" ; includeSubDomains",
                                 1, 1000) == 1,
        "quoted max-age + spaced directives");
  CHECK(http_hsts_should_upgrade(&s, "sub.q.com", 1100) == 1,
        "quoted/spaced applied with subdomains");
}

static void test_eviction(void) {
  static struct http_hsts_store s;
  char host[32];
  unsigned k;
  http_hsts_init(&s);
  for (k = 0; k < HTTP_HSTS_MAX_ENTRIES + 3; k++) {
    snprintf(host, sizeof(host), "h%u.example.net", k);
    http_hsts_process_header(&s, host, "max-age=1000", 1, 1000 + (long)k);
  }
  CHECK(s.evictions == 3, "exactly 3 evictions at capacity+3");
}

static void test_fail_closed(void) {
  static struct http_hsts_store s;
  http_hsts_init(&s);
  CHECK(http_hsts_process_header(NULL, "h", "max-age=1", 1, 1) == 0,
        "NULL store fails");
  CHECK(http_hsts_process_header(&s, NULL, "max-age=1", 1, 1) == 0,
        "NULL host fails");
  CHECK(http_hsts_process_header(&s, "h", NULL, 1, 1) == 0, "NULL value fails");
  CHECK(http_hsts_should_upgrade(NULL, "h", 1) == 0, "NULL store upgrade 0");
  CHECK(http_hsts_should_upgrade(&s, NULL, 1) == 0, "NULL host upgrade 0");
}

int run_http_hsts_tests(void) {
  g_failures = 0;
  test_basic();
  test_insecure_ignored();
  test_missing_max_age();
  test_max_age_zero_removes();
  test_include_subdomains();
  test_ip_rejected();
  test_expiry_and_gc();
  test_quoted_and_whitespace();
  test_eviction();
  test_fail_closed();
  if (g_failures == 0) printf("[http_hsts] all HSTS tests passed\n");
  return g_failures;
}
