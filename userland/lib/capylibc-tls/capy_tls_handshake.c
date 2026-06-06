/* Etapa 5 / Slice 5.4 — userland TLS handshake drive (implementation).
 *
 * Canonical BearSSL client record-layer loop: pump the engine's
 * send/recv record buffers through the transport until the handshake is
 * established (engine reaches the application-data state) or fails. Every
 * failure path returns a non-OK, fail-closed result — a transport error
 * or any BearSSL/cert error never yields CAPY_TLS_HS_OK. */

#include "capylibc-tls/capy_tls_handshake.h"

capy_tls_handshake_result_t capy_tls_handshake_run(
    br_ssl_client_context *cc, br_x509_minimal_context *xc,
    const br_x509_trust_anchor *anchors, size_t anchor_count,
    const char *server_name,
    const unsigned char *entropy, size_t entropy_len,
    uint32_t now_days, uint32_t now_seconds,
    void *iobuf, size_t iobuf_len,
    const struct capy_tls_transport *transport) {

  if (!cc || !xc || !anchors || anchor_count == 0u || !server_name ||
      !entropy || entropy_len == 0u || !iobuf || iobuf_len == 0u ||
      !transport || !transport->read || !transport->write) {
    return CAPY_TLS_HS_EINVAL;
  }

  /* Full TLS-1.2 client profile with certificate validation armed against
   * the supplied trust anchors. */
  br_ssl_client_init_full(cc, xc, anchors, anchor_count);
  br_x509_minimal_set_time(xc, now_days, now_seconds);
  br_ssl_engine_set_buffer(&cc->eng, iobuf, iobuf_len, 1);
  br_ssl_engine_inject_entropy(&cc->eng, entropy, entropy_len);

  if (!br_ssl_client_reset(cc, server_name, 0)) {
    return CAPY_TLS_HS_EPROTOCOL;
  }

  for (;;) {
    unsigned st = br_ssl_engine_current_state(&cc->eng);

    if (st & BR_SSL_CLOSED) {
      return (br_ssl_engine_last_error(&cc->eng) == BR_ERR_OK)
                 ? CAPY_TLS_HS_OK
                 : CAPY_TLS_HS_EPROTOCOL;
    }

    /* Flush any pending outbound records first. */
    if (st & BR_SSL_SENDREC) {
      size_t len = 0;
      unsigned char *buf = br_ssl_engine_sendrec_buf(&cc->eng, &len);
      int n = transport->write(transport->ctx, buf, len);
      if (n <= 0) return CAPY_TLS_HS_ETRANSPORT;
      br_ssl_engine_sendrec_ack(&cc->eng, (size_t)n);
      continue;
    }

    /* Then feed inbound records the engine is waiting for. */
    if (st & BR_SSL_RECVREC) {
      size_t len = 0;
      unsigned char *buf = br_ssl_engine_recvrec_buf(&cc->eng, &len);
      int n = transport->read(transport->ctx, buf, len);
      if (n <= 0) return CAPY_TLS_HS_ETRANSPORT;
      br_ssl_engine_recvrec_ack(&cc->eng, (size_t)n);
      continue;
    }

    /* Nothing pending on the record layer and the engine is ready to move
     * application data: the handshake is established. */
    if (st & (BR_SSL_SENDAPP | BR_SSL_RECVAPP)) {
      return CAPY_TLS_HS_OK;
    }

    /* No actionable state before app-data: treat as a protocol failure. */
    return CAPY_TLS_HS_EPROTOCOL;
  }
}

void capy_tls_unix_to_x509_time(uint64_t unix_seconds,
                                uint32_t *out_days, uint32_t *out_seconds) {
  /* BearSSL counts days since 0 AD; the Unix epoch (1970-01-01) is day
   * 719528. seconds is the count within the day [0, 86400). */
  if (out_days) {
    *out_days = (uint32_t)(719528ull + unix_seconds / 86400ull);
  }
  if (out_seconds) {
    *out_seconds = (uint32_t)(unix_seconds % 86400ull);
  }
}
