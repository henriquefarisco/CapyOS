/* Host tests for the in-tree BearSSL trust anchors (Etapa 5 / Slice 5.2).
 *
 * These are the `br_x509_trust_anchor[]` the kernel TLS client already
 * uses in production (src/security/tls.c) and the foundation the
 * userland libcapy-tls handshake (later Etapa 5 slices) will reuse.
 *
 * This is the first host test to compile against real BearSSL types
 * (via -Ithird_party/bearssl/inc, wired in Slice 5.2), so it doubles as
 * proof that the BearSSL include path is correct in the host build. It
 * links only src/security/tls_trust_anchors.c (pure data — no BearSSL
 * function objects needed) and validates every anchor is well-formed:
 * non-empty DN, a CA flag, and RSA or EC key material present. A
 * malformed/empty anchor table would otherwise only surface during the
 * x86_64 handshake; this catches it on the host. */

#include "security/internal/tls_trust_anchors.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[tls-trust-anchors] FAIL: %s\n", msg);
  g_failures++;
}

static void test_count_is_known(void) {
  /* Locks the pinned bundle size; a change here is a deliberate bundle
   * update and must be reviewed (and propagated to the userland copy). */
  if (capyos_tls_trust_anchor_count() != 146u)
    fail("trust anchor count drifted from 146");
}

static void test_table_non_null(void) {
  if (capyos_tls_trust_anchors() == NULL)
    fail("trust anchor table pointer must be non-NULL");
}

static void test_every_anchor_well_formed(void) {
  const br_x509_trust_anchor *tas = capyos_tls_trust_anchors();
  size_t n = capyos_tls_trust_anchor_count();
  if (!tas) {
    fail("null anchor table");
    return;
  }
  for (size_t i = 0; i < n; i++) {
    const br_x509_trust_anchor *ta = &tas[i];
    if (ta->dn.data == NULL || ta->dn.len == 0u) {
      fail("anchor DN must be non-empty");
      return;
    }
    if ((ta->flags & BR_X509_TA_CA) == 0u) {
      fail("trust anchor must carry the CA flag");
      return;
    }
    if (ta->pkey.key_type == BR_KEYTYPE_RSA) {
      if (!ta->pkey.key.rsa.n || ta->pkey.key.rsa.nlen == 0u ||
          !ta->pkey.key.rsa.e || ta->pkey.key.rsa.elen == 0u) {
        fail("RSA anchor must have modulus + exponent");
        return;
      }
    } else if (ta->pkey.key_type == BR_KEYTYPE_EC) {
      if (!ta->pkey.key.ec.q || ta->pkey.key.ec.qlen == 0u) {
        fail("EC anchor must have a public point");
        return;
      }
    } else {
      fail("anchor key type must be RSA or EC");
      return;
    }
  }
}

int run_tls_trust_anchors_tests(void) {
  g_failures = 0;
  test_count_is_known();
  test_table_non_null();
  test_every_anchor_well_formed();
  if (g_failures == 0)
    printf("[tests] tls_trust_anchors OK (%zu anchors)\n",
           capyos_tls_trust_anchor_count());
  return g_failures;
}
