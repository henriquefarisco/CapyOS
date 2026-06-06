/* Host test: real BearSSL TLS client engine + real in-tree trust anchors
 * (Etapa 5 / Slice 5.3-5.4 engine smoke, host-side, no network).
 *
 * This is the strongest host-side proof that the userland TLS handshake
 * will work once wired into ring-3: it builds a REAL `br_ssl_client`
 * over the SAME trust anchors the kernel uses (capyos_tls_trust_anchors)
 * and drives it far enough to emit a valid TLS ClientHello — exercising
 * br_ssl_client_init_full (which also arms br_x509_minimal with the
 * anchors), entropy injection, reset with SNI, and the record layer.
 *
 * It does NOT change shipped behaviour: the userland libcapy-tls stays
 * fail-closed (`capy_tls_is_supported()==0`) until the engine is plugged
 * into the socket I/O seam (Slice 5.4) and validated by a VMware smoke.
 * The point here is to catch — on the host — any mismatch between our
 * anchors/config and real BearSSL before the x86_64/network work. */

#include "bearssl.h"
#include "security/internal/tls_trust_anchors.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[tls-client-engine] FAIL: %s\n", msg);
  g_failures++;
}

/* Naive byte-substring search (memmem is not portable C99). */
static int contains(const unsigned char *hay, size_t hlen,
                    const unsigned char *needle, size_t nlen) {
  if (nlen == 0 || hlen < nlen) return 0;
  for (size_t i = 0; i + nlen <= hlen; i++) {
    if (memcmp(hay + i, needle, nlen) == 0) return 1;
  }
  return 0;
}

/* BR_SSL_BUFSIZE_BIDI is ~33 KiB; keep it off the stack. */
static unsigned char g_iobuf[BR_SSL_BUFSIZE_BIDI];
static br_ssl_client_context g_cc;
static br_x509_minimal_context g_xc;

int run_tls_client_engine_tests(void) {
  g_failures = 0;

  const br_x509_trust_anchor *tas = capyos_tls_trust_anchors();
  size_t nta = capyos_tls_trust_anchor_count();
  const char *host = "example.com";

  /* Full TLS-1.2 client profile, cert validation armed against the real
   * in-tree anchors (same data the kernel TLS client uses). */
  br_ssl_client_init_full(&g_cc, &g_xc, tas, nta);
  br_ssl_engine_set_buffer(&g_cc.eng, g_iobuf, sizeof g_iobuf, 1);

  /* In ring-3 this entropy comes from SYS_GETRANDOM (Slice 5.1). For a
   * deterministic host test we inject a fixed non-zero seed so the DRBG
   * is satisfied and the ClientHello can be generated. */
  static const unsigned char seed[32] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    0x0F, 0x1E, 0x2D, 0x3C, 0x4B, 0x5A, 0x69, 0x78,
    0x87, 0x96, 0xA5, 0xB4, 0xC3, 0xD2, 0xE1, 0xF0
  };
  br_ssl_engine_inject_entropy(&g_cc.eng, seed, sizeof seed);

  if (!br_ssl_client_reset(&g_cc, host, 0)) {
    fail("br_ssl_client_reset returned 0");
    return g_failures;
  }
  if (br_ssl_engine_last_error(&g_cc.eng) != BR_ERR_OK) {
    fail("engine reports an error right after reset");
    return g_failures;
  }
  if ((br_ssl_engine_current_state(&g_cc.eng) & BR_SSL_SENDREC) == 0) {
    fail("engine is not ready to send records (no ClientHello queued)");
    return g_failures;
  }

  size_t len = 0;
  unsigned char *recs = br_ssl_engine_sendrec_buf(&g_cc.eng, &len);
  if (!recs || len < 9u) {
    fail("no ClientHello records were produced");
    return g_failures;
  }
  /* TLS record header: type(1) | version(2) | length(2), then the
   * handshake message: msg_type(1) ... */
  if (recs[0] != 0x16) fail("first outgoing record is not a handshake (0x16)");
  if (recs[5] != 0x01) fail("first handshake message is not a ClientHello (0x01)");
  if (!contains(recs, len, (const unsigned char *)host, strlen(host)))
    fail("ClientHello does not carry the requested SNI hostname");

  if (g_failures == 0)
    printf("[tests] tls_client_engine OK (real BearSSL ClientHello, SNI=%s, %zu anchors)\n",
           host, nta);
  return g_failures;
}
