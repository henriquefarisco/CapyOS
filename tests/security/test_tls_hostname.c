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

/* === Boundary tests (alpha.260+) =================================
 *
 * Lock the exact RFC 1035 § 2.3.4 boundaries so a future refactor
 * cannot silently tighten or loosen them. The existing
 * `_rejects_length_limits` covers the OVER-limit case (65 and 254);
 * these cover the AT-LIMIT case (63 and 253) which must remain
 * ACCEPTED. */

static void test_tls_hostname_accepts_max_label_length(void) {
  /* Exactly 63 chars in one label is the upper bound of RFC 1035
   * §2.3.4 and must remain accepted. */
  char max_label[64];
  size_t i;
  for (i = 0; i < 63; i++) max_label[i] = 'a';
  max_label[63] = '\0';
  TEST("tls_hostname_valid accepts max 63-char label");
  if (tls_hostname_valid(max_label)) PASS();
  else FAIL("63-char label rejected (RFC 1035 boundary)");
}

static void test_tls_hostname_accepts_max_total_length(void) {
  /* Exactly 253 chars total (one short of the 254 cap because the
   * trailing root dot is counted in some specs but not in the wire
   * form). The existing policy caps total_len at 253 inclusive,
   * so 253 must be accepted and 254 rejected. */
  char max_name[254];
  size_t pos = 0;
  /* Pattern: four 63-char labels = 4*63 + 3*'.' = 255 — too long.
   * Use: label63 . label63 . label63 . label61 = 63+1+63+1+63+1+61 = 253. */
  size_t i;
  for (i = 0; i < 63; i++) max_name[pos++] = 'a';
  max_name[pos++] = '.';
  for (i = 0; i < 63; i++) max_name[pos++] = 'a';
  max_name[pos++] = '.';
  for (i = 0; i < 63; i++) max_name[pos++] = 'a';
  max_name[pos++] = '.';
  for (i = 0; i < 61; i++) max_name[pos++] = 'a';
  max_name[pos] = '\0';
  TEST("tls_hostname_valid accepts max 253-char total length");
  if (tls_hostname_valid(max_name)) PASS();
  else FAIL("253-char total length rejected (RFC 1035 boundary)");
}

static void test_tls_hostname_accepts_single_char_labels(void) {
  /* Minimal valid: single character per label. RFC 1035 does not
   * forbid 1-char labels; "a.b" is a syntactically valid hostname
   * even if no public CA would sign such a cert. The policy must
   * remain permissive at this boundary. */
  TEST("tls_hostname_valid accepts single-character labels");
  if (tls_hostname_valid("a") &&
      tls_hostname_valid("a.b") &&
      tls_hostname_valid("a.b.c")) PASS();
  else FAIL("single-character label rejected");
}

static void test_tls_hostname_rejects_just_dots(void) {
  /* Pathological: hostnames consisting of only dots must be
   * rejected. The "empty label" check handles this but adding an
   * explicit test prevents accidental loosening. */
  TEST("tls_hostname_valid rejects all-dot hostnames");
  if (!tls_hostname_valid(".") &&
      !tls_hostname_valid("..") &&
      !tls_hostname_valid("...")) PASS();
  else FAIL("all-dot hostname accepted");
}

static void test_tls_hostname_rejects_unicode_high_bit(void) {
  /* The policy only allows ASCII letters/digits/dot/hyphen. A
   * non-ASCII byte (high bit set) is implicitly rejected by the
   * `tls_hostname_policy_char_safe` predicate. Lock this in
   * explicitly so a future refactor of the char predicate cannot
   * accept high-bit bytes (which would expose us to UTF-8 IDN
   * homograph attacks that we are NOT prepared to validate). */
  TEST("tls_hostname_valid rejects high-bit bytes (no IDN)");
  if (!tls_hostname_valid("example\xC3\xA9.com") && /* é in UTF-8 */
      !tls_hostname_valid("\xFF" "example.com") &&
      !tls_hostname_valid("\x80" "nonascii.com")) PASS();
  else FAIL("high-bit byte accepted (would enable IDN homograph attack)");
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
  test_tls_hostname_accepts_max_label_length();
  test_tls_hostname_accepts_max_total_length();
  test_tls_hostname_accepts_single_char_labels();
  test_tls_hostname_rejects_just_dots();
  test_tls_hostname_rejects_unicode_high_bit();
  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
