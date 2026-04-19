#ifndef SECURITY_TLS_H
#define SECURITY_TLS_H

#include <stdint.h>
#include <stddef.h>

#define TLS_VERSION_10 0x0301
#define TLS_VERSION_11 0x0302
#define TLS_VERSION_12 0x0303
#define TLS_VERSION_13 0x0304

#define TLS_ALPN_MAX_LEN 16

#define TLS_RECORD_CHANGE_CIPHER 20
#define TLS_RECORD_ALERT         21
#define TLS_RECORD_HANDSHAKE     22
#define TLS_RECORD_APPLICATION   23

#define TLS_HS_CLIENT_HELLO    1
#define TLS_HS_SERVER_HELLO    2
#define TLS_HS_CERTIFICATE     11
#define TLS_HS_SERVER_KEY_EX   12
#define TLS_HS_SERVER_DONE     14
#define TLS_HS_CLIENT_KEY_EX   16
#define TLS_HS_FINISHED        20

#define TLS_ALERT_CLOSE_NOTIFY  0
#define TLS_ALERT_UNEXPECTED    10
#define TLS_ALERT_BAD_RECORD    20
#define TLS_ALERT_HANDSHAKE_FAIL 40
#define TLS_ALERT_BAD_CERT      42
#define TLS_ALERT_UNKNOWN_CA    48
#define TLS_ALERT_INTERNAL      80

#define TLS_MAX_RECORD_SIZE 16384
#define TLS_MAX_CERT_SIZE   4096

enum tls_state {
  TLS_STATE_INIT = 0,
  TLS_STATE_CLIENT_HELLO_SENT,
  TLS_STATE_SERVER_HELLO_RECEIVED,
  TLS_STATE_CERTIFICATE_RECEIVED,
  TLS_STATE_KEY_EXCHANGE_DONE,
  TLS_STATE_HANDSHAKE_COMPLETE,
  TLS_STATE_APPLICATION,
  TLS_STATE_CLOSING,
  TLS_STATE_CLOSED,
  TLS_STATE_ERROR
};

struct tls_context;

struct tls_config {
  int verify_peer;
  const uint8_t *ca_cert;
  size_t ca_cert_len;
  uint32_t timeout_ms;
};

struct tls_security_info {
  uint16_t protocol_version;
  uint16_t cipher_suite;
  uint32_t trust_anchor_count;
  int peer_verified;
  int hostname_validated;
  int custom_anchor_loaded;
  char alpn[TLS_ALPN_MAX_LEN];
};

int tls_init(void);
struct tls_context *tls_connect(int socket_fd, const char *hostname,
                                 const struct tls_config *config);
int tls_send(struct tls_context *ctx, const void *data, size_t len);
int tls_recv(struct tls_context *ctx, void *buf, size_t len);
int tls_close(struct tls_context *ctx);
void tls_free(struct tls_context *ctx);
int tls_handshake(struct tls_context *ctx);
const char *tls_state_name(enum tls_state state);
enum tls_state tls_last_state(void);
int tls_last_error(void);
const char *tls_alert_name(int alert);
int tls_error(struct tls_context *ctx);
int tls_get_security_info(struct tls_context *ctx, struct tls_security_info *info);
int tls_get_last_security_info(struct tls_security_info *info);
const char *tls_version_name(uint16_t version);
const char *tls_cipher_suite_name(uint16_t suite);

#endif /* SECURITY_TLS_H */
