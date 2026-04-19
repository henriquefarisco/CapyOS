#include "security/tls.h"

#include "security/csprng.h"
#include "drivers/rtc/rtc.h"
#include "memory/kmem.h"
#include "net/socket.h"
#include "tls_trust_anchors.h"

#include "bearssl.h"
#include <stddef.h>

struct tls_context {
  int socket_fd;
  enum tls_state state;
  int error_code;
  int verify_peer;
  uint32_t timeout_ms;
  uint16_t negotiated_version;
  uint16_t cipher_suite;
  uint32_t trust_anchor_count;
  int peer_verified;
  char selected_alpn[TLS_ALPN_MAX_LEN];
  br_ssl_client_context client;
  br_x509_minimal_context x509;
  br_sslio_context io;
  unsigned char *iobuf;
};

static enum tls_state g_tls_last_state = TLS_STATE_INIT;
static int g_tls_last_error = 0;
static const char *g_tls_alpn_protocols[] = { "http/1.1" };

static void tls_memzero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  while (len-- > 0) {
    *p++ = 0;
  }
}

static void tls_copy_string(char *dst, size_t dst_len, const char *src) {
  size_t i = 0;
  if (!dst || dst_len == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  while (src[i] && i + 1 < dst_len) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static void tls_reset_security_info(struct tls_context *ctx) {
  if (!ctx) {
    return;
  }
  ctx->negotiated_version = 0;
  ctx->cipher_suite = 0;
  ctx->trust_anchor_count = 0;
  ctx->peer_verified = 0;
  ctx->selected_alpn[0] = '\0';
}

static void tls_record_success(struct tls_context *ctx, enum tls_state state) {
  g_tls_last_state = state;
  g_tls_last_error = 0;
  if (ctx) {
    ctx->state = state;
    ctx->error_code = 0;
  }
}

static void tls_record_failure(struct tls_context *ctx, int error) {
  g_tls_last_state = TLS_STATE_ERROR;
  g_tls_last_error = error;
  if (ctx) {
    ctx->state = TLS_STATE_ERROR;
    ctx->error_code = error;
  }
}

static int tls_engine_error(struct tls_context *ctx, int fallback) {
  int err;
  if (!ctx) {
    return fallback;
  }
  err = br_ssl_engine_last_error(&ctx->client.eng);
  return err != 0 ? err : fallback;
}

static void tls_seed_engine(struct tls_context *ctx) {
  uint8_t seed[48];
  csprng_fill(seed, sizeof(seed));
  br_ssl_engine_inject_entropy(&ctx->client.eng, seed, sizeof(seed));
  tls_memzero(seed, sizeof(seed));
}

static void tls_set_validation_time(struct tls_context *ctx) {
  uint64_t unix_time;
  uint32_t days;
  uint32_t seconds;

  if (!ctx) {
    return;
  }
  unix_time = rtc_unix_timestamp();
  days = 719528u + (uint32_t)(unix_time / 86400ULL);
  seconds = (uint32_t)(unix_time % 86400ULL);
  br_x509_minimal_set_time(&ctx->x509, days, seconds);
}

static void tls_capture_session(struct tls_context *ctx) {
  br_ssl_session_parameters session;
  const char *selected_alpn;

  if (!ctx) {
    return;
  }

  tls_memzero(&session, sizeof(session));
  br_ssl_engine_get_session_parameters(&ctx->client.eng, &session);
  ctx->negotiated_version = session.version;
  ctx->cipher_suite = session.cipher_suite;
  ctx->trust_anchor_count = (uint32_t)capyos_tls_trust_anchor_count();
  ctx->peer_verified = 1;

  selected_alpn = br_ssl_engine_get_selected_protocol(&ctx->client.eng);
  tls_copy_string(ctx->selected_alpn, sizeof(ctx->selected_alpn), selected_alpn);
  tls_memzero(session.master_secret, sizeof(session.master_secret));
}

static int tls_socket_read(void *read_context, unsigned char *data, size_t len) {
  struct tls_context *ctx = (struct tls_context *)read_context;
  int r;
  if (!ctx || !data || len == 0) {
    return -1;
  }
  r = socket_recv(ctx->socket_fd, data, len, 0);
  return r > 0 ? r : -1;
}

static int tls_socket_write(void *write_context, const unsigned char *data,
                            size_t len) {
  struct tls_context *ctx = (struct tls_context *)write_context;
  int r;
  if (!ctx || !data || len == 0) {
    return -1;
  }
  r = socket_send(ctx->socket_fd, data, len, 0);
  return r > 0 ? r : -1;
}

const char *tls_state_name(enum tls_state state) {
  switch (state) {
    case TLS_STATE_INIT: return "init";
    case TLS_STATE_CLIENT_HELLO_SENT: return "client-hello-sent";
    case TLS_STATE_SERVER_HELLO_RECEIVED: return "server-hello-received";
    case TLS_STATE_CERTIFICATE_RECEIVED: return "certificate-received";
    case TLS_STATE_KEY_EXCHANGE_DONE: return "key-exchange-done";
    case TLS_STATE_HANDSHAKE_COMPLETE: return "handshake-complete";
    case TLS_STATE_APPLICATION: return "application";
    case TLS_STATE_CLOSING: return "closing";
    case TLS_STATE_CLOSED: return "closed";
    case TLS_STATE_ERROR: return "error";
    default: return "unknown";
  }
}

enum tls_state tls_last_state(void) { return g_tls_last_state; }

int tls_last_error(void) { return g_tls_last_error; }

const char *tls_version_name(uint16_t version) {
  switch (version) {
    case TLS_VERSION_10: return "TLS 1.0";
    case TLS_VERSION_11: return "TLS 1.1";
    case TLS_VERSION_12: return "TLS 1.2";
    case TLS_VERSION_13: return "TLS 1.3";
    default: return "unknown";
  }
}

const char *tls_cipher_suite_name(uint16_t suite) {
  switch (suite) {
    case BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
      return "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256";
    case BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
      return "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384";
    case BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256:
      return "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
    case BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384:
      return "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
    case BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256:
      return "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256";
    case BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256:
      return "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256";
    case BR_TLS_RSA_WITH_AES_128_GCM_SHA256:
      return "TLS_RSA_WITH_AES_128_GCM_SHA256";
    case BR_TLS_RSA_WITH_AES_256_GCM_SHA384:
      return "TLS_RSA_WITH_AES_256_GCM_SHA384";
    default: return "unknown";
  }
}

const char *tls_alert_name(int alert) {
  if (alert >= BR_ERR_RECV_FATAL_ALERT &&
      alert < (BR_ERR_RECV_FATAL_ALERT + 256)) {
    switch (alert - BR_ERR_RECV_FATAL_ALERT) {
      case TLS_ALERT_CLOSE_NOTIFY: return "recv-close-notify";
      case TLS_ALERT_UNEXPECTED: return "recv-unexpected-message";
      case TLS_ALERT_BAD_RECORD: return "recv-bad-record";
      case TLS_ALERT_HANDSHAKE_FAIL: return "recv-handshake-failure";
      case TLS_ALERT_BAD_CERT: return "recv-bad-certificate";
      case TLS_ALERT_UNKNOWN_CA: return "recv-unknown-ca";
      case TLS_ALERT_INTERNAL: return "recv-internal-error";
      default: return "recv-fatal-alert";
    }
  }
  if (alert >= BR_ERR_SEND_FATAL_ALERT &&
      alert < (BR_ERR_SEND_FATAL_ALERT + 256)) {
    switch (alert - BR_ERR_SEND_FATAL_ALERT) {
      case TLS_ALERT_CLOSE_NOTIFY: return "send-close-notify";
      default: return "send-fatal-alert";
    }
  }

  switch (alert) {
    case BR_ERR_OK: return "ok";
    case BR_ERR_BAD_PARAM: return "bad-parameter";
    case BR_ERR_BAD_STATE: return "bad-state";
    case BR_ERR_UNSUPPORTED_VERSION: return "unsupported-version";
    case BR_ERR_BAD_VERSION: return "bad-version";
    case BR_ERR_BAD_LENGTH: return "bad-length";
    case BR_ERR_TOO_LARGE: return "record-too-large";
    case BR_ERR_BAD_MAC: return "bad-mac";
    case BR_ERR_NO_RANDOM: return "no-random";
    case BR_ERR_UNKNOWN_TYPE: return "unknown-record-type";
    case BR_ERR_UNEXPECTED: return "unexpected-message";
    case BR_ERR_BAD_CCS: return "bad-change-cipher-spec";
    case BR_ERR_BAD_ALERT: return "bad-alert";
    case BR_ERR_BAD_HANDSHAKE: return "bad-handshake";
    case BR_ERR_OVERSIZED_ID: return "oversized-session-id";
    case BR_ERR_BAD_CIPHER_SUITE: return "bad-cipher-suite";
    case BR_ERR_BAD_COMPRESSION: return "bad-compression";
    case BR_ERR_BAD_FRAGLEN: return "bad-fragment-length";
    case BR_ERR_BAD_SECRENEG: return "bad-secure-renegotiation";
    case BR_ERR_EXTRA_EXTENSION: return "unexpected-extension";
    case BR_ERR_BAD_SNI: return "bad-sni";
    case BR_ERR_BAD_HELLO_DONE: return "bad-server-hello-done";
    case BR_ERR_LIMIT_EXCEEDED: return "limit-exceeded";
    case BR_ERR_BAD_FINISHED: return "bad-finished";
    case BR_ERR_RESUME_MISMATCH: return "resume-mismatch";
    case BR_ERR_INVALID_ALGORITHM: return "invalid-algorithm";
    case BR_ERR_BAD_SIGNATURE: return "bad-signature";
    case BR_ERR_WRONG_KEY_USAGE: return "wrong-key-usage";
    case BR_ERR_NO_CLIENT_AUTH: return "client-auth-required";
    case BR_ERR_IO: return "transport-io";
    case BR_ERR_X509_INVALID_VALUE: return "x509-invalid-value";
    case BR_ERR_X509_TRUNCATED: return "x509-truncated";
    case BR_ERR_X509_EMPTY_CHAIN: return "x509-empty-chain";
    case BR_ERR_X509_INNER_TRUNC: return "x509-inner-truncated";
    case BR_ERR_X509_BAD_TAG_CLASS: return "x509-bad-tag-class";
    case BR_ERR_X509_BAD_TAG_VALUE: return "x509-bad-tag-value";
    case BR_ERR_X509_INDEFINITE_LENGTH: return "x509-indefinite-length";
    case BR_ERR_X509_EXTRA_ELEMENT: return "x509-extra-element";
    case BR_ERR_X509_UNEXPECTED: return "x509-unexpected";
    case BR_ERR_X509_NOT_CONSTRUCTED: return "x509-not-constructed";
    case BR_ERR_X509_NOT_PRIMITIVE: return "x509-not-primitive";
    case BR_ERR_X509_PARTIAL_BYTE: return "x509-partial-byte";
    case BR_ERR_X509_BAD_BOOLEAN: return "x509-bad-boolean";
    case BR_ERR_X509_OVERFLOW: return "x509-overflow";
    case BR_ERR_X509_BAD_DN: return "x509-bad-dn";
    case BR_ERR_X509_BAD_TIME: return "x509-bad-time";
    case BR_ERR_X509_UNSUPPORTED: return "x509-unsupported";
    case BR_ERR_X509_LIMIT_EXCEEDED: return "x509-limit-exceeded";
    case BR_ERR_X509_WRONG_KEY_TYPE: return "x509-wrong-key-type";
    case BR_ERR_X509_BAD_SIGNATURE: return "x509-bad-signature";
    case BR_ERR_X509_TIME_UNKNOWN: return "x509-time-unknown";
    case BR_ERR_X509_EXPIRED: return "x509-expired";
    case BR_ERR_X509_DN_MISMATCH: return "x509-dn-mismatch";
    case BR_ERR_X509_BAD_SERVER_NAME: return "x509-bad-server-name";
    case BR_ERR_X509_CRITICAL_EXTENSION: return "x509-critical-extension";
    case BR_ERR_X509_NOT_CA: return "x509-not-ca";
    case BR_ERR_X509_FORBIDDEN_KEY_USAGE: return "x509-forbidden-key-usage";
    case BR_ERR_X509_WEAK_PUBLIC_KEY: return "x509-weak-public-key";
    case BR_ERR_X509_NOT_TRUSTED: return "x509-not-trusted";
    default: return "unknown-tls-error";
  }
}

int tls_init(void) {
  g_tls_last_state = TLS_STATE_INIT;
  g_tls_last_error = 0;
  return 0;
}

int tls_handshake(struct tls_context *ctx) {
  unsigned state;

  if (!ctx) {
    tls_record_failure(NULL, BR_ERR_BAD_PARAM);
    return -1;
  }

  tls_record_success(ctx, TLS_STATE_CLIENT_HELLO_SENT);
  if (br_sslio_flush(&ctx->io) < 0) {
    tls_record_failure(ctx, tls_engine_error(ctx, BR_ERR_IO));
    return -1;
  }

  state = br_ssl_engine_current_state(&ctx->client.eng);
  if ((state & (BR_SSL_SENDAPP | BR_SSL_RECVAPP)) == 0) {
    tls_record_failure(ctx, tls_engine_error(ctx, BR_ERR_BAD_STATE));
    return -1;
  }

  tls_capture_session(ctx);
  tls_record_success(ctx, TLS_STATE_HANDSHAKE_COMPLETE);
  return 0;
}

struct tls_context *tls_connect(int socket_fd, const char *hostname,
                                const struct tls_config *config) {
  struct tls_context *ctx;

  if (socket_fd < 0) {
    tls_record_failure(NULL, BR_ERR_BAD_PARAM);
    return NULL;
  }

  ctx = (struct tls_context *)kmalloc(sizeof(*ctx));
  if (!ctx) {
    tls_record_failure(NULL, BR_ERR_IO);
    return NULL;
  }
  tls_memzero(ctx, sizeof(*ctx));

  ctx->iobuf = (unsigned char *)kmalloc(BR_SSL_BUFSIZE_BIDI);
  if (!ctx->iobuf) {
    kfree(ctx);
    tls_record_failure(NULL, BR_ERR_IO);
    return NULL;
  }

  ctx->socket_fd = socket_fd;
  ctx->state = TLS_STATE_INIT;
  ctx->verify_peer = config ? config->verify_peer : 1;
  ctx->timeout_ms = (config && config->timeout_ms) ? config->timeout_ms : 10000u;
  tls_reset_security_info(ctx);
  (void)config;

  br_ssl_client_init_full(&ctx->client, &ctx->x509,
                          capyos_tls_trust_anchors(),
                          capyos_tls_trust_anchor_count());
  tls_set_validation_time(ctx);
  br_ssl_engine_set_buffer(&ctx->client.eng, ctx->iobuf, BR_SSL_BUFSIZE_BIDI, 1);
  br_ssl_engine_set_versions(&ctx->client.eng, BR_TLS12, BR_TLS12);
  br_ssl_engine_set_protocol_names(&ctx->client.eng,
                                   g_tls_alpn_protocols,
                                   sizeof(g_tls_alpn_protocols) / sizeof(g_tls_alpn_protocols[0]));
  br_ssl_engine_add_flags(&ctx->client.eng, BR_OPT_FAIL_ON_ALPN_MISMATCH);
  tls_seed_engine(ctx);
  if (!br_ssl_client_reset(&ctx->client, hostname, 0)) {
    tls_record_failure(ctx, tls_engine_error(ctx, BR_ERR_NO_RANDOM));
    tls_free(ctx);
    return NULL;
  }

  br_sslio_init(&ctx->io, &ctx->client.eng, tls_socket_read, ctx,
                tls_socket_write, ctx);
  if (tls_handshake(ctx) != 0) {
    tls_free(ctx);
    return NULL;
  }
  return ctx;
}

int tls_send(struct tls_context *ctx, const void *data, size_t len) {
  if (!ctx || (!data && len > 0)) {
    tls_record_failure(ctx, BR_ERR_BAD_PARAM);
    return -1;
  }
  if (len == 0) {
    return 0;
  }
  if (br_sslio_write_all(&ctx->io, data, len) < 0 ||
      br_sslio_flush(&ctx->io) < 0) {
    tls_record_failure(ctx, tls_engine_error(ctx, BR_ERR_IO));
    return -1;
  }
  tls_record_success(ctx, TLS_STATE_APPLICATION);
  return (int)len;
}

int tls_recv(struct tls_context *ctx, void *buf, size_t len) {
  int r;
  int err;
  unsigned state;

  if (!ctx || !buf || len == 0) {
    tls_record_failure(ctx, BR_ERR_BAD_PARAM);
    return -1;
  }

  r = br_sslio_read(&ctx->io, buf, len);
  if (r >= 0) {
    tls_record_success(ctx, TLS_STATE_APPLICATION);
    return r;
  }

  err = br_ssl_engine_last_error(&ctx->client.eng);
  state = br_ssl_engine_current_state(&ctx->client.eng);
  if ((state & BR_SSL_CLOSED) != 0 && err == BR_ERR_OK) {
    tls_record_success(ctx, TLS_STATE_CLOSED);
    return 0;
  }

  tls_record_failure(ctx, err != 0 ? err : BR_ERR_IO);
  return -1;
}

int tls_close(struct tls_context *ctx) {
  if (!ctx) {
    return -1;
  }
  ctx->state = TLS_STATE_CLOSING;
  if (br_sslio_close(&ctx->io) != 0 ||
      br_ssl_engine_last_error(&ctx->client.eng) == BR_ERR_OK) {
    tls_record_success(ctx, TLS_STATE_CLOSED);
    return 0;
  }
  tls_record_failure(ctx, tls_engine_error(ctx, BR_ERR_IO));
  return -1;
}

void tls_free(struct tls_context *ctx) {
  if (!ctx) {
    return;
  }
  if (ctx->iobuf) {
    kfree(ctx->iobuf);
    ctx->iobuf = NULL;
  }
  kfree(ctx);
}

int tls_error(struct tls_context *ctx) {
  return ctx ? ctx->error_code : g_tls_last_error;
}

int tls_get_security_info(struct tls_context *ctx, struct tls_security_info *info) {
  if (!ctx || !info) {
    return -1;
  }
  info->protocol_version = ctx->negotiated_version;
  info->cipher_suite = ctx->cipher_suite;
  info->trust_anchor_count = ctx->trust_anchor_count;
  info->peer_verified = ctx->peer_verified;
  tls_copy_string(info->alpn, sizeof(info->alpn), ctx->selected_alpn);
  return 0;
}
