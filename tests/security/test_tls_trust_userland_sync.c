/* Host test: lock the userland libcapy-tls trust METADATA catalog to the
 * real in-tree BearSSL anchor bundle (Etapa 5 / Slice 5.3, gap G3).
 *
 * Two trust surfaces live in the tree:
 *
 *   - the real kernel bundle (src/security/tls_trust_anchors.c), exposed by
 *     capyos_tls_trust_anchors()/_count() — the actual br_x509_trust_anchor[]
 *     the kernel TLS client uses in production and that the gated userland
 *     handshake (capy_tls_backend_connect under CAPYOS_TLS_USERLAND_HANDSHAKE)
 *     reuses verbatim via br_ssl_client_init_full();
 *
 *   - the userland metadata catalog (userland/lib/capylibc-tls/capy_tls_trust.c),
 *     a hand-maintained, cert-bytes-absent projection of that bundle (anchor
 *     count, RSA/EC split, per-slot key type) whose fingerprints the
 *     fail-closed preflight in capy_tls_backend.c trusts.
 *
 * test_tls_trust_anchors.c independently asserts the real bundle is 146 with
 * a 106/40 RSA/EC split, and tests/userland/test_capylibc_tls_trust.c asserts
 * the metadata claims the same numbers — but, until now, nothing proved the
 * metadata actually tracks the real bundle. A kernel re-bundle (different
 * count, a shifted RSA/EC split, or a single reordered/replaced anchor) would
 * silently desync the userland metadata, which the gated handshake's preflight
 * gates depend on. This test ties the two together so such drift fails on the
 * host instead of only at handshake time; a deliberate bundle change must
 * update both sides in the same commit.
 *
 * Both translation units (the kernel anchor data and the userland metadata)
 * are already linked into the single host unit_tests binary, so this test
 * needs no extra object — it just references symbols from each side.
 */

#include "security/internal/tls_trust_anchors.h" /* real bundle + BearSSL types */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Userland trust-metadata accessors, defined in
 * userland/lib/capylibc-tls/capy_tls_trust.c (declared in that library's
 * internal header). Forward-declared here so this security-side test stays
 * self-contained instead of pulling the userland include tree into the host
 * build — the same pattern tests/net/test_http_url.c uses for the http
 * internals. */
uint32_t capy_tls_default_trust_anchor_count(void);
uint32_t capy_tls_default_trust_anchor_rsa_count(void);
uint32_t capy_tls_default_trust_anchor_ec_count(void);
uint32_t capy_tls_default_trust_anchor_slot_count(void);
uint32_t capy_tls_default_trust_anchor_slot_key_type(uint32_t index);
int capy_tls_default_trust_anchors_available(void);

/* libcapy-tls per-slot key-type masks
 * (CAPY_TLS_TRUST_ANCHOR_KEYTYPE_{RSA,EC}_MASK in capy_tls_internal.h),
 * mirrored locally for the same self-containment reason. These are distinct
 * from the BearSSL BR_KEYTYPE_* values, so the per-index comparison maps the
 * real anchor's key_type to the expected mask explicitly. */
#define CAPY_TLS_SLOT_KEYTYPE_RSA_MASK 0x1u
#define CAPY_TLS_SLOT_KEYTYPE_EC_MASK  0x2u

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[tls-trust-userland-sync] FAIL: %s\n", msg);
  g_failures++;
}

/* The anchor count the userland metadata catalog advertises must equal the
 * real bundle size, and the slot table must be the same length. */
static void test_count_matches_real_bundle(void) {
  size_t real = capyos_tls_trust_anchor_count();
  if (!capy_tls_default_trust_anchors_available())
    fail("userland default trust store reports unavailable");
  if ((size_t)capy_tls_default_trust_anchor_count() != real)
    fail("userland metadata anchor count drifted from the real bundle");
  if ((size_t)capy_tls_default_trust_anchor_slot_count() != real)
    fail("userland slot count drifted from the real bundle");
}

/* The RSA/EC split the userland metadata advertises must match the split
 * computed directly from the real bundle's per-anchor key types. */
static void test_key_split_matches_real_bundle(void) {
  const br_x509_trust_anchor *tas = capyos_tls_trust_anchors();
  size_t n = capyos_tls_trust_anchor_count();
  size_t rsa = 0u;
  size_t ec = 0u;
  size_t i;
  if (!tas) {
    fail("null real anchor table");
    return;
  }
  for (i = 0; i < n; i++) {
    if (tas[i].pkey.key_type == BR_KEYTYPE_RSA) rsa++;
    else if (tas[i].pkey.key_type == BR_KEYTYPE_EC) ec++;
  }
  if ((size_t)capy_tls_default_trust_anchor_rsa_count() != rsa)
    fail("userland RSA anchor count drifted from the real bundle");
  if ((size_t)capy_tls_default_trust_anchor_ec_count() != ec)
    fail("userland EC anchor count drifted from the real bundle");
}

/* Strongest lock: every userland slot's key type must match the real anchor
 * at the SAME index. The metadata is a positional projection of the bundle,
 * so a reordered or replaced anchor (even one that preserves the 106/40
 * totals) is caught here, not just an aggregate count change. */
static void test_per_index_keytype_matches_real_bundle(void) {
  const br_x509_trust_anchor *tas = capyos_tls_trust_anchors();
  size_t n = capyos_tls_trust_anchor_count();
  size_t i;
  if (!tas) {
    fail("null real anchor table");
    return;
  }
  if ((size_t)capy_tls_default_trust_anchor_slot_count() != n) {
    fail("slot count mismatch; per-index comparison skipped");
    return;
  }
  for (i = 0; i < n; i++) {
    uint32_t slot = capy_tls_default_trust_anchor_slot_key_type((uint32_t)i);
    uint32_t want = 0u;
    if (tas[i].pkey.key_type == BR_KEYTYPE_RSA) {
      want = CAPY_TLS_SLOT_KEYTYPE_RSA_MASK;
    } else if (tas[i].pkey.key_type == BR_KEYTYPE_EC) {
      want = CAPY_TLS_SLOT_KEYTYPE_EC_MASK;
    } else {
      fail("real anchor key type is neither RSA nor EC");
      return;
    }
    if (slot != want) {
      fail("userland slot key type drifted from the real bundle at an index");
      return;
    }
  }
}

int run_tls_trust_userland_sync_tests(void) {
  g_failures = 0;
  test_count_matches_real_bundle();
  test_key_split_matches_real_bundle();
  test_per_index_keytype_matches_real_bundle();
  if (g_failures == 0)
    printf("[tests] tls_trust_userland_sync OK (%zu anchors)\n",
           capyos_tls_trust_anchor_count());
  return g_failures;
}
