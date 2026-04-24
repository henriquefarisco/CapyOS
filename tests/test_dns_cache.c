#include <stdio.h>
#include <string.h>
#include "net/dns_cache.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
  do { tests_run++; printf("  %-40s ", name); } while (0)
#define PASS() \
  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) \
  do { printf("FAIL: %s\n", msg); } while (0)

void test_dns_cache_basic(void) {
  dns_cache_init();

  TEST("dns_cache_lookup miss on empty");
  uint32_t ip = 0;
  if (dns_cache_lookup("example.com", &ip) != 0) { PASS(); }
  else { FAIL("should miss"); }

  TEST("dns_cache_insert + lookup hit");
  dns_cache_insert("example.com", 0x5DB8D822, 300);
  if (dns_cache_lookup("example.com", &ip) == 0 && ip == 0x5DB8D822) { PASS(); }
  else { FAIL("should hit"); }

  TEST("dns_cache_invalidate removes entry");
  dns_cache_invalidate("example.com");
  if (dns_cache_lookup("example.com", &ip) != 0) { PASS(); }
  else { FAIL("should miss after invalidate"); }
}

void test_dns_cache_flush(void) {
  dns_cache_init();
  dns_cache_insert("a.com", 1, 300);
  dns_cache_insert("b.com", 2, 300);

  TEST("dns_cache_flush clears all");
  dns_cache_flush();
  uint32_t ip;
  int miss_a = dns_cache_lookup("a.com", &ip);
  int miss_b = dns_cache_lookup("b.com", &ip);
  if (miss_a != 0 && miss_b != 0) { PASS(); }
  else { FAIL("should miss after flush"); }
}

void test_dns_cache_stats(void) {
  dns_cache_init();

  TEST("dns_cache_stats tracks hits/misses");
  uint32_t ip;
  dns_cache_lookup("x.com", &ip);  /* miss */
  dns_cache_insert("x.com", 42, 300);
  dns_cache_lookup("x.com", &ip);  /* hit */
  struct dns_cache_stats st;
  dns_cache_stats_get(&st);
  if (st.hits >= 1 && st.misses >= 1 && st.entries == 1) { PASS(); }
  else { FAIL("stats wrong"); }
}

void test_dns_cache_update(void) {
  dns_cache_init();

  TEST("dns_cache_insert updates existing");
  dns_cache_insert("y.com", 100, 300);
  dns_cache_insert("y.com", 200, 300);
  uint32_t ip;
  dns_cache_lookup("y.com", &ip);
  if (ip == 200) { PASS(); }
  else { FAIL("should be updated to 200"); }
}

void test_dns_cache_ttl(void) {
  dns_cache_init();
  dns_cache_insert("ttl.com", 1234, 1);
  dns_cache_tick(1);

  TEST("dns_cache_tick preserves entry before TTL");
  uint32_t ip;
  dns_cache_tick(101);
  if (dns_cache_lookup("ttl.com", &ip) == 0 && ip == 1234) { PASS(); }
  else { FAIL("should still be cached before TTL expires"); }

  TEST("dns_cache_tick expires entry after TTL");
  dns_cache_tick(102);
  if (dns_cache_lookup("ttl.com", &ip) != 0) { PASS(); }
  else { FAIL("should miss after TTL expires"); }

  TEST("dns_cache_stats tracks expiration");
  struct dns_cache_stats st;
  dns_cache_stats_get(&st);
  if (st.expired >= 1 && st.entries == 0) { PASS(); }
  else { FAIL("expiration stats wrong"); }
}

int test_dns_cache_run(void) {
  printf("[test_dns_cache]\n");
  tests_run = 0;
  tests_passed = 0;
  test_dns_cache_basic();
  test_dns_cache_flush();
  test_dns_cache_stats();
  test_dns_cache_update();
  test_dns_cache_ttl();
  printf("  %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
