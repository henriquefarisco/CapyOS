/* Host tests for TLS server-certificate validation (Etapa 5 / Slice 5.5
 * "cert inválido falha fechado"), via BearSSL's br_x509_minimal — the same
 * validation engine br_ssl_client_init_full arms during the handshake.
 *
 * This is the security-critical half of TLS: deciding whether to TRUST a
 * server's certificate chain for a given hostname. Using a throwaway test
 * PKI (CA + leaf for "capy.test", public certs only — no private keys in
 * the tree), it pins the accept path AND the three fail-closed rejections
 * the handshake relies on: wrong hostname, expired, and untrusted issuer.
 *
 * Validation time convention (BearSSL): days since 0 AD; the Unix epoch is
 * day 719528. The test PKI is valid ~2026..2126, so day 741000 (~2028) is
 * inside the window and day 900000 (~2435) is past notAfter. */

#include "bearssl.h"
#include "test_tls_fixture_certs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[tls-cert-validation] FAIL: %s\n", msg);
  g_failures++;
}

/* Collects the trust-anchor DN as the CA cert is decoded. */
struct dn_sink {
  unsigned char buf[512];
  size_t len;
};

static void capture_dn(void *ctx, const void *buf, size_t len) {
  struct dn_sink *s = (struct dn_sink *)ctx;
  if (s->len + len <= sizeof s->buf) {
    memcpy(s->buf + s->len, buf, len);
    s->len += len;
  }
}

/* Build a trust anchor from a CA certificate DER. The decoder and DN sink
 * must outlive the anchor: the anchor's public-key buffers point into the
 * decoder context. */
static int build_anchor(br_x509_decoder_context *dc, struct dn_sink *sink,
                        const unsigned char *der, size_t len,
                        br_x509_trust_anchor *ta) {
  br_x509_pkey *pk;
  sink->len = 0;
  br_x509_decoder_init(dc, capture_dn, sink);
  br_x509_decoder_push(dc, der, len);
  pk = br_x509_decoder_get_pkey(dc);
  if (!pk) return 0;
  ta->dn.data = sink->buf;
  ta->dn.len = sink->len;
  ta->flags = BR_X509_TA_CA;
  ta->pkey = *pk;
  return 1;
}

/* Validate the test server cert chain against `anchor` for `host` at the
 * given validation day; returns the BearSSL chain error (0 == trusted). */
static unsigned validate(const br_x509_trust_anchor *anchor,
                         const char *host, uint32_t days) {
  br_x509_minimal_context ctx;
  const br_x509_class **xc;
  br_x509_minimal_init_full(&ctx, anchor, 1);
  br_x509_minimal_set_time(&ctx, days, 0u);
  xc = &ctx.vtable;
  (*xc)->start_chain(xc, host);
  (*xc)->start_cert(xc, (uint32_t)sizeof capy_test_server_der);
  (*xc)->append(xc, capy_test_server_der, sizeof capy_test_server_der);
  (*xc)->end_cert(xc);
  return (*xc)->end_chain(xc);
}

/* Separate, long-lived decoders/sinks so both anchors stay valid. */
static br_x509_decoder_context g_ca_dc;
static struct dn_sink g_ca_dn;
static br_x509_decoder_context g_bogus_dc;
static struct dn_sink g_bogus_dn;

int run_tls_cert_validation_tests(void) {
  br_x509_trust_anchor ca;
  br_x509_trust_anchor bogus;
  g_failures = 0;

  if (!build_anchor(&g_ca_dc, &g_ca_dn, capy_test_ca_der,
                    sizeof capy_test_ca_der, &ca)) {
    fail("could not decode the test CA certificate");
    return g_failures;
  }

  /* Accept: valid leaf for the right host, inside validity, trusted CA. */
  if (validate(&ca, "capy.test", 741000u) != BR_ERR_OK)
    fail("valid chain for capy.test must be trusted (BR_ERR_OK)");

  /* Reject: hostname mismatch. */
  if (validate(&ca, "evil.example", 741000u) != BR_ERR_X509_BAD_SERVER_NAME)
    fail("hostname mismatch must fail closed (BAD_SERVER_NAME)");

  /* Reject: validation time past notAfter. */
  if (validate(&ca, "capy.test", 900000u) != BR_ERR_X509_EXPIRED)
    fail("expired validity must fail closed (EXPIRED)");

  /* Reject: issuer not among the trust anchors. Use the leaf cert itself
   * as a bogus anchor — the chain's issuer (the CA) is then untrusted. */
  if (!build_anchor(&g_bogus_dc, &g_bogus_dn, capy_test_server_der,
                    sizeof capy_test_server_der, &bogus)) {
    fail("could not decode the leaf certificate for the bogus anchor");
    return g_failures;
  }
  if (validate(&bogus, "capy.test", 741000u) != BR_ERR_X509_NOT_TRUSTED)
    fail("untrusted issuer must fail closed (NOT_TRUSTED)");

  if (g_failures == 0)
    printf("[tests] tls_cert_validation OK (accept valid; reject host/expired/untrusted)\n");
  return g_failures;
}
