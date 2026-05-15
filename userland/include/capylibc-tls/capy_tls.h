#ifndef CAPYLIBC_TLS_CAPY_TLS_H
#define CAPYLIBC_TLS_CAPY_TLS_H

#include <stddef.h>
#include <stdint.h>

#define CAPY_TLS_ALPN_MAX_LEN 16
#define CAPY_TLS_TIMEOUT_DEFAULT_MS 10000u
#define CAPY_TLS_TIMEOUT_MIN_MS 100u
#define CAPY_TLS_TIMEOUT_MAX_MS 120000u

typedef enum {
  CAPY_TLS_OK           = 0,
  CAPY_TLS_EINVAL       = -1,
  CAPY_TLS_EUNSUPPORTED = -2,
  CAPY_TLS_ESTATE       = -3
} capy_tls_err_t;

typedef enum {
  CAPY_TLS_STATE_INIT = 0,
  CAPY_TLS_STATE_UNSUPPORTED,
  CAPY_TLS_STATE_CLOSED,
  CAPY_TLS_STATE_ERROR
} capy_tls_state_t;

struct capy_tls_context;

struct capy_tls_config {
  int verify_peer;
  const uint8_t *ca_cert;
  size_t ca_cert_len;
  uint32_t timeout_ms;
};

struct capy_tls_security_info {
  uint16_t protocol_version;
  uint16_t cipher_suite;
  uint32_t trust_anchor_count;
  int peer_verified;
  int hostname_validated;
  int custom_anchor_loaded;
  char alpn[CAPY_TLS_ALPN_MAX_LEN];
};

int capy_tls_init(void);
int capy_tls_is_supported(void);
struct capy_tls_context *capy_tls_connect_tcp(
    int socket_fd,
    const char *hostname,
    const struct capy_tls_config *config);
int capy_tls_send(struct capy_tls_context *ctx, const void *data, size_t len);
int capy_tls_recv(struct capy_tls_context *ctx, void *buf, size_t len);
int capy_tls_close(struct capy_tls_context *ctx);
void capy_tls_free(struct capy_tls_context *ctx);
capy_tls_err_t capy_tls_last_error(void);
capy_tls_state_t capy_tls_last_state(void);
const char *capy_tls_error_name(capy_tls_err_t err);
const char *capy_tls_state_name(capy_tls_state_t state);
int capy_tls_get_security_info(
    struct capy_tls_context *ctx,
    struct capy_tls_security_info *info);
int capy_tls_get_last_security_info(struct capy_tls_security_info *info);

#endif
