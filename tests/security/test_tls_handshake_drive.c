/* Host tests for the userland TLS handshake drive (Etapa 5 / Slice 5.4),
 * exercised through a mock transport — no network, no server, no certs.
 *
 * This drives the REAL ring-3 record-layer loop (capy_tls_handshake_run +
 * BearSSL) and pins the security-critical contract that matters before the
 * VMware smoke can run: the client emits a well-formed ClientHello, and
 * every transport/protocol failure is **fail-closed** (never returns OK).
 * The success path needs a live server (validated by the local/VMware
 * smoke), so here we assert the failure surface, which is what guards
 * against a silently-broken handshake. */

#include "capylibc-tls/capy_tls_handshake.h"
#include "security/internal/tls_trust_anchors.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[tls-handshake-drive] FAIL: %s\n", msg);
  g_failures++;
}

static int contains(const unsigned char *hay, size_t hlen,
                    const unsigned char *needle, size_t nlen) {
  if (nlen == 0 || hlen < nlen) return 0;
  for (size_t i = 0; i + nlen <= hlen; i++)
    if (memcmp(hay + i, needle, nlen) == 0) return 1;
  return 0;
}

/* Mock transport. `write` captures bytes (the ClientHello flight); `read`
 * is scripted to EOF or garbage to drive the fail-closed paths. */
struct mock_transport {
  unsigned char wbuf[4096];
  size_t wlen;
  int write_should_fail;
  int read_mode;   /* 0 = EOF, 1 = garbage */
  int read_calls;
};

static int mock_write(void *ctx, const unsigned char *buf, size_t len) {
  struct mock_transport *m = (struct mock_transport *)ctx;
  if (m->write_should_fail) return -1;
  size_t cap = sizeof m->wbuf - m->wlen;
  size_t n = len < cap ? len : cap;
  memcpy(m->wbuf + m->wlen, buf, n);
  m->wlen += n;
  return (int)len; /* pretend the whole record left the wire */
}

static int mock_read(void *ctx, unsigned char *buf, size_t len) {
  struct mock_transport *m = (struct mock_transport *)ctx;
  m->read_calls++;
  if (m->read_mode == 1 && m->read_calls <= 2) {
    for (size_t i = 0; i < len; i++) buf[i] = 0xFF; /* bogus TLS record */
    return (int)len;
  }
  return -1; /* EOF / error */
}

static unsigned char g_iobuf[BR_SSL_BUFSIZE_BIDI];
static br_ssl_client_context g_cc;
static br_x509_minimal_context g_xc;
static const unsigned char g_seed[32] = {
  0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
  0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
  0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x60, 0x71,
  0x82, 0x93, 0xA4, 0xB5, 0xC6, 0xD7, 0xE8, 0xF9
};

static capy_tls_handshake_result_t run_with(struct mock_transport *m) {
  struct capy_tls_transport t;
  t.read = mock_read;
  t.write = mock_write;
  t.ctx = m;
  return capy_tls_handshake_run(
      &g_cc, &g_xc, capyos_tls_trust_anchors(),
      capyos_tls_trust_anchor_count(), "capy.test",
      g_seed, sizeof g_seed, 738000u, 0u,
      g_iobuf, sizeof g_iobuf, &t);
}

static void test_einval(void) {
  struct mock_transport m;
  struct capy_tls_transport t;
  memset(&m, 0, sizeof m);
  t.read = mock_read; t.write = mock_write; t.ctx = &m;
  if (capy_tls_handshake_run(NULL, &g_xc, capyos_tls_trust_anchors(),
        capyos_tls_trust_anchor_count(), "capy.test", g_seed, sizeof g_seed,
        738000u, 0u, g_iobuf, sizeof g_iobuf, &t) != CAPY_TLS_HS_EINVAL)
    fail("NULL client context must be EINVAL");
  if (capy_tls_handshake_run(&g_cc, &g_xc, capyos_tls_trust_anchors(),
        0u, "capy.test", g_seed, sizeof g_seed,
        738000u, 0u, g_iobuf, sizeof g_iobuf, &t) != CAPY_TLS_HS_EINVAL)
    fail("zero anchors must be EINVAL");
  if (capy_tls_handshake_run(&g_cc, &g_xc, capyos_tls_trust_anchors(),
        capyos_tls_trust_anchor_count(), "capy.test", g_seed, sizeof g_seed,
        738000u, 0u, g_iobuf, sizeof g_iobuf, NULL) != CAPY_TLS_HS_EINVAL)
    fail("NULL transport must be EINVAL");
}

static void test_clienthello_then_eof(void) {
  struct mock_transport m;
  memset(&m, 0, sizeof m);
  m.read_mode = 0; /* EOF right after the ClientHello goes out */
  if (run_with(&m) != CAPY_TLS_HS_ETRANSPORT)
    fail("EOF after ClientHello must fail closed (ETRANSPORT)");
  if (m.wlen < 6u) {
    fail("no ClientHello was emitted to the transport");
    return;
  }
  if (m.wbuf[0] != 0x16) fail("emitted record is not a TLS handshake (0x16)");
  if (m.wbuf[5] != 0x01) fail("emitted handshake is not a ClientHello (0x01)");
  if (!contains(m.wbuf, m.wlen, (const unsigned char *)"capy.test", 9u))
    fail("ClientHello did not carry the SNI hostname");
}

static void test_write_failure(void) {
  struct mock_transport m;
  memset(&m, 0, sizeof m);
  m.write_should_fail = 1;
  if (run_with(&m) != CAPY_TLS_HS_ETRANSPORT)
    fail("transport write failure must fail closed (ETRANSPORT)");
}

static void test_garbage_response(void) {
  struct mock_transport m;
  memset(&m, 0, sizeof m);
  m.read_mode = 1; /* bogus server records */
  if (run_with(&m) == CAPY_TLS_HS_OK)
    fail("garbage server response must NOT report success (fail-closed)");
}

static void test_x509_time_conversion(void) {
  uint32_t days = 0, secs = 0;
  /* Unix epoch (1970-01-01 00:00 UTC) == BearSSL day 719528. */
  capy_tls_unix_to_x509_time(0u, &days, &secs);
  if (days != 719528u || secs != 0u) fail("epoch must map to day 719528 / sec 0");
  /* Last second of the epoch day. */
  capy_tls_unix_to_x509_time(86399u, &days, &secs);
  if (days != 719528u || secs != 86399u) fail("end-of-day-0 mapping wrong");
  /* First second of the next day. */
  capy_tls_unix_to_x509_time(86400u, &days, &secs);
  if (days != 719529u || secs != 0u) fail("day rollover mapping wrong");
  capy_tls_unix_to_x509_time(86401u, &days, &secs);
  if (days != 719529u || secs != 1u) fail("day+1 sec+1 mapping wrong");
  /* NULL outputs must be tolerated (no crash). */
  capy_tls_unix_to_x509_time(123456u, NULL, NULL);
}

int run_tls_handshake_drive_tests(void) {
  g_failures = 0;
  test_einval();
  test_clienthello_then_eof();
  test_write_failure();
  test_garbage_response();
  test_x509_time_conversion();
  if (g_failures == 0)
    printf("[tests] tls_handshake_drive OK (real loop: ClientHello + fail-closed paths)\n");
  return g_failures;
}
