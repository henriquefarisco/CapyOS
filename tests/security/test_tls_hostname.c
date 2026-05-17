#include "security/tls_hostname.h"
#include "security/tls_hostname_policy.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(label) do { tests_run++; printf("    %-68s", label); } while (0)
#define PASS() do { tests_passed++; printf(" OK\n"); } while (0)
#define FAIL(why) do { printf(" FAIL: %s\n", why); } while (0)

static void test_tls_hostname_accepts_valid_names(void) {
  TEST("tls_hostname_valid accepts DNS names and IPv4 literals");
  if (tls_hostname_valid("example.com") &&
      tls_hostname_valid("www.example-1.com") &&
      tls_hostname_valid("localhost") &&
      tls_hostname_valid("192.0.2.1")) PASS();
  else FAIL("valid hostname rejected");
}

static void test_tls_hostname_rejects_empty_and_controls(void) {
  TEST("tls_hostname_valid rejects empty/control hostnames");
  if (!tls_hostname_valid(0) &&
      !tls_hostname_valid("") &&
      !tls_hostname_valid("bad host") &&
      !tls_hostname_valid("bad\tname") &&
      !tls_hostname_valid("bad\nname")) PASS();
  else FAIL("empty/control hostname accepted");
}

static void test_tls_hostname_rejects_label_boundaries(void) {
  TEST("tls_hostname_valid rejects unsafe label boundaries");
  if (!tls_hostname_valid("-example.com") &&
      !tls_hostname_valid("example-.com") &&
      !tls_hostname_valid("example..com") &&
      !tls_hostname_valid("example.com.") &&
      !tls_hostname_valid(".example.com")) PASS();
  else FAIL("unsafe label boundary accepted");
}

static void test_tls_hostname_rejects_ambiguous_syntax(void) {
  TEST("tls_hostname_valid rejects ambiguous TLS names");
  if (!tls_hostname_valid("exa_mple.com") &&
      !tls_hostname_valid("exa%mple.com") &&
      !tls_hostname_valid("exa\\mple.com") &&
      !tls_hostname_valid("[::1]") &&
      !tls_hostname_valid("*.example.com")) PASS();
  else FAIL("ambiguous hostname accepted");
}

static void test_tls_hostname_wrapper_matches_shared_policy(void) {
  const char *samples[] = {
      "example.com", "localhost", "192.0.2.1", "bad host",
      "example..com", "exa_mple.com", "[::1]", "*.example.com"
  };
  TEST("tls_hostname_valid wraps shared hostname policy");
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
    if (tls_hostname_valid(samples[i]) !=
        tls_hostname_policy_valid(samples[i])) {
      FAIL("wrapper diverged from shared policy");
      return;
    }
  }
  PASS();
}

static void test_tls_hostname_rejects_length_limits(void) {
  char long_label[65];
  char long_name[255];
  memset(long_label, 'a', sizeof(long_label));
  long_label[64] = '\0';
  memset(long_name, 'a', sizeof(long_name));
  long_name[63] = '.';
  long_name[127] = '.';
  long_name[191] = '.';
  long_name[254] = '\0';
  TEST("tls_hostname_valid rejects length limits");
  if (!tls_hostname_valid(long_label) &&
      !tls_hostname_valid(long_name)) PASS();
  else FAIL("overlong hostname accepted");
}

int test_tls_hostname_run(void) {
  printf("[test_tls_hostname]\n");
  tests_run = 0;
  tests_passed = 0;
  test_tls_hostname_accepts_valid_names();
  test_tls_hostname_rejects_empty_and_controls();
  test_tls_hostname_rejects_label_boundaries();
  test_tls_hostname_rejects_ambiguous_syntax();
  test_tls_hostname_wrapper_matches_shared_policy();
  test_tls_hostname_rejects_length_limits();
  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
