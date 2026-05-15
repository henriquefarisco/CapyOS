#include "capy_tls_internal.h"

int capy_tls_config_resolve(
    const struct capy_tls_config *config,
    struct capy_tls_effective_config *out) {
  if (!out) return 0;
  out->verify_peer = 1;
  out->ca_cert = 0;
  out->ca_cert_len = 0;
  out->timeout_ms = CAPY_TLS_TIMEOUT_DEFAULT_MS;
  if (!config) return 1;
  if (config->verify_peer != 1) return 0;
  if (config->ca_cert && config->ca_cert_len == 0) return 0;
  if (!config->ca_cert && config->ca_cert_len > 0) return 0;
  if (config->timeout_ms > CAPY_TLS_TIMEOUT_MAX_MS) return 0;
  if (config->timeout_ms > 0 &&
      config->timeout_ms < CAPY_TLS_TIMEOUT_MIN_MS) return 0;
  out->verify_peer = config->verify_peer;
  out->ca_cert = config->ca_cert;
  out->ca_cert_len = config->ca_cert_len;
  out->timeout_ms = config->timeout_ms ? config->timeout_ms
                                        : CAPY_TLS_TIMEOUT_DEFAULT_MS;
  return 1;
}
