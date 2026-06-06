#ifndef CAPYLIBC_TLS_CAPY_TLS_HANDSHAKE_H
#define CAPYLIBC_TLS_CAPY_TLS_HANDSHAKE_H

/* Etapa 5 / Slice 5.4 — userland TLS handshake drive (BearSSL engine <->
 * transport seam).
 *
 * This is the real client-side record-layer loop the userland TLS backend
 * will use once it is wired into ring-3 (and the public
 * `capy_tls_is_supported()` gate is flipped after the VMware smoke). It is
 * deliberately self-contained and transport-agnostic so it is fully
 * host-testable: ring-3 plugs `capy_recv`/`capy_send` into the transport,
 * while host tests inject a mock. The shipped backend still returns
 * fail-closed (`CAPY_TLS_EUNSUPPORTED`) until this is wired in. */

#include "bearssl.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Transport seam for encrypted TLS records. `read`/`write` move record
 * bytes to/from the peer and MUST return the number of bytes transferred
 * (> 0), or <= 0 on EOF/error (the handshake then fails closed). In ring-3
 * these map to `capy_recv`/`capy_send` on the socket fd; host tests inject
 * a mock. */
typedef int (*capy_tls_transport_read_fn)(void *ctx, unsigned char *buf, size_t len);
typedef int (*capy_tls_transport_write_fn)(void *ctx, const unsigned char *buf, size_t len);

struct capy_tls_transport {
  capy_tls_transport_read_fn read;
  capy_tls_transport_write_fn write;
  void *ctx;
};

typedef enum {
  CAPY_TLS_HS_OK = 0,          /* handshake completed (ready for app data) */
  CAPY_TLS_HS_EINVAL = -1,     /* bad arguments */
  CAPY_TLS_HS_ETRANSPORT = -2, /* transport read/write failed (fail-closed) */
  CAPY_TLS_HS_EPROTOCOL = -3   /* BearSSL handshake/cert error (fail-closed) */
} capy_tls_handshake_result_t;

/* Drive a full BearSSL TLS client handshake to completion over
 * `transport`, validating the peer certificate chain against `anchors`
 * (br_x509_minimal). The caller owns `cc`/`xc`/`iobuf` so it controls
 * their lifetime and zeroization. `entropy` seeds the DRBG (ring-3:
 * SYS_GETRANDOM; tests: a fixed seed). `now_days`/`now_seconds` set the
 * X.509 validation time (BearSSL day-count + seconds-in-day).
 *
 * Returns CAPY_TLS_HS_OK only when the handshake completed against a
 * trusted, reachable peer; every other path is fail-closed and never
 * reports success. */
capy_tls_handshake_result_t capy_tls_handshake_run(
    br_ssl_client_context *cc, br_x509_minimal_context *xc,
    const br_x509_trust_anchor *anchors, size_t anchor_count,
    const char *server_name,
    const unsigned char *entropy, size_t entropy_len,
    uint32_t now_days, uint32_t now_seconds,
    void *iobuf, size_t iobuf_len,
    const struct capy_tls_transport *transport);

/* Convert Unix epoch seconds (e.g. from capy_clock_realtime()) to the
 * BearSSL X.509 validation-time representation: days since 0 AD and
 * seconds-in-day (the Unix epoch is day 719528). Used to feed the
 * `now_days`/`now_seconds` arguments above from real calendar time. */
void capy_tls_unix_to_x509_time(uint64_t unix_seconds,
                                uint32_t *out_days, uint32_t *out_seconds);

#ifdef __cplusplus
}
#endif

#endif /* CAPYLIBC_TLS_CAPY_TLS_HANDSHAKE_H */
