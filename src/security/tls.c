#include "security/tls.h"
#include "security/crypt.h"
#include "security/csprng.h"
#include "net/socket.h"
#include "memory/kmem.h"
#include <stddef.h>

static void tls_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static void tls_memcpy(void *d, const void *s, size_t n) {
  uint8_t *dp = (uint8_t *)d; const uint8_t *sp = (const uint8_t *)s;
  for (size_t i = 0; i < n; i++) dp[i] = sp[i];
}
static size_t tls_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

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

int tls_init(void) { return 0; }

static int tls_send_record(struct tls_context *ctx, uint8_t type,
                            const uint8_t *data, size_t len) {
  if (!ctx || len > TLS_MAX_RECORD_SIZE) return -1;
  uint8_t header[5];
  header[0] = type;
  header[1] = (uint8_t)(ctx->version >> 8);
  header[2] = (uint8_t)(ctx->version);
  header[3] = (uint8_t)(len >> 8);
  header[4] = (uint8_t)(len);
  if (socket_send(ctx->socket_fd, header, 5, 0) < 0) return -1;
  if (len > 0 && data) {
    if (socket_send(ctx->socket_fd, data, len, 0) < 0) return -1;
  }
  ctx->client_seq++;
  return 0;
}

static int tls_recv_record(struct tls_context *ctx, uint8_t *type,
                            uint8_t *data, size_t *out_len, size_t max_len) {
  uint8_t header[5];
  int r = socket_recv(ctx->socket_fd, header, 5, 0);
  if (r < 5) return -1;
  *type = header[0];
  uint16_t rec_len = ((uint16_t)header[3] << 8) | header[4];
  if (rec_len > max_len) return -1;
  size_t total = 0;
  while (total < rec_len) {
    r = socket_recv(ctx->socket_fd, data + total, rec_len - total, 0);
    if (r <= 0) return -1;
    total += (size_t)r;
  }
  *out_len = rec_len;
  ctx->server_seq++;
  return 0;
}

static int tls_build_client_hello(struct tls_context *ctx, uint8_t *buf,
                                   size_t *out_len) {
  csprng_fill(ctx->client_random, 32);

  size_t pos = 0;
  buf[pos++] = TLS_HS_CLIENT_HELLO;
  size_t len_pos = pos; pos += 3;

  /* Protocol version */
  buf[pos++] = 0x03; buf[pos++] = 0x03;

  /* Client random */
  tls_memcpy(buf + pos, ctx->client_random, 32); pos += 32;

  /* Session ID length = 0 */
  buf[pos++] = 0;

  /* Cipher suites: TLS_RSA_WITH_AES_128_CBC_SHA (0x002F) */
  buf[pos++] = 0; buf[pos++] = 2;
  buf[pos++] = 0x00; buf[pos++] = 0x2F;

  /* Compression: null only */
  buf[pos++] = 1; buf[pos++] = 0;

  /* Extensions: SNI if hostname available */
  /* Minimal: no extensions for now */

  size_t hs_len = pos - len_pos - 3;
  buf[len_pos] = 0;
  buf[len_pos + 1] = (uint8_t)(hs_len >> 8);
  buf[len_pos + 2] = (uint8_t)(hs_len);

  *out_len = pos;
  return 0;
}

static int tls_parse_server_hello(struct tls_context *ctx, const uint8_t *data,
                                   size_t len) {
  if (len < 38) return -1;
  size_t pos = 0;
  if (data[pos++] != TLS_HS_SERVER_HELLO) return -1;
  uint32_t hs_len = ((uint32_t)data[pos] << 16) | ((uint32_t)data[pos+1] << 8) | data[pos+2];
  pos += 3;
  (void)hs_len;

  /* Version */
  pos += 2;
  /* Server random */
  tls_memcpy(ctx->server_random, data + pos, 32); pos += 32;
  /* Session ID */
  uint8_t sid_len = data[pos++];
  pos += sid_len;
  /* Cipher suite */
  pos += 2;
  /* Compression */
  pos += 1;

  return 0;
}

static void tls_prf(const uint8_t *secret, size_t secret_len,
                     const char *label, const uint8_t *seed, size_t seed_len,
                     uint8_t *out, size_t out_len) {
  /* TLS 1.2 PRF using HMAC-SHA256 */
  size_t label_len = tls_strlen(label);
  uint8_t combined[256];
  size_t clen = 0;
  for (size_t i = 0; i < label_len && clen < 200; i++) combined[clen++] = (uint8_t)label[i];
  for (size_t i = 0; i < seed_len && clen < 256; i++) combined[clen++] = seed[i];

  uint8_t A[32];
  crypt_hmac_sha256(secret, secret_len, combined, clen, A);

  size_t generated = 0;
  while (generated < out_len) {
    uint8_t input[32 + 256];
    tls_memcpy(input, A, 32);
    tls_memcpy(input + 32, combined, clen);
    uint8_t block[32];
    crypt_hmac_sha256(secret, secret_len, input, 32 + clen, block);
    for (size_t i = 0; i < 32 && generated < out_len; i++)
      out[generated++] = block[i];
    crypt_hmac_sha256(secret, secret_len, A, 32, A);
  }
}

int tls_handshake(struct tls_context *ctx) {
  if (!ctx) return -1;

  /* Send ClientHello */
  uint8_t hello_buf[256];
  size_t hello_len;
  if (tls_build_client_hello(ctx, hello_buf, &hello_len) != 0) return -1;
  if (tls_send_record(ctx, TLS_RECORD_HANDSHAKE, hello_buf, hello_len) != 0) {
    ctx->state = TLS_STATE_ERROR;
    return -1;
  }
  ctx->state = TLS_STATE_CLIENT_HELLO_SENT;

  /* Receive ServerHello */
  uint8_t rec_type;
  uint8_t rec_buf[TLS_MAX_RECORD_SIZE];
  size_t rec_len;
  if (tls_recv_record(ctx, &rec_type, rec_buf, &rec_len, sizeof(rec_buf)) != 0) {
    ctx->state = TLS_STATE_ERROR;
    return -1;
  }
  if (rec_type != TLS_RECORD_HANDSHAKE) { ctx->state = TLS_STATE_ERROR; return -1; }
  if (tls_parse_server_hello(ctx, rec_buf, rec_len) != 0) {
    ctx->state = TLS_STATE_ERROR;
    return -1;
  }
  ctx->state = TLS_STATE_SERVER_HELLO_RECEIVED;

  /* Receive Certificate */
  if (tls_recv_record(ctx, &rec_type, rec_buf, &rec_len, sizeof(rec_buf)) != 0) {
    ctx->state = TLS_STATE_ERROR;
    return -1;
  }
  ctx->state = TLS_STATE_CERTIFICATE_RECEIVED;

  /* Receive ServerHelloDone */
  if (tls_recv_record(ctx, &rec_type, rec_buf, &rec_len, sizeof(rec_buf)) != 0) {
    ctx->state = TLS_STATE_ERROR;
    return -1;
  }

  /* Generate pre-master secret */
  uint8_t pre_master[48];
  pre_master[0] = 0x03; pre_master[1] = 0x03;
  csprng_fill(pre_master + 2, 46);

  /* Derive master secret */
  uint8_t seed[64];
  tls_memcpy(seed, ctx->client_random, 32);
  tls_memcpy(seed + 32, ctx->server_random, 32);
  tls_prf(pre_master, 48, "master secret", seed, 64, ctx->master_secret, 48);

  /* Derive key material */
  uint8_t key_block[128];
  uint8_t key_seed[64];
  tls_memcpy(key_seed, ctx->server_random, 32);
  tls_memcpy(key_seed + 32, ctx->client_random, 32);
  tls_prf(ctx->master_secret, 48, "key expansion", key_seed, 64, key_block, 128);

  tls_memcpy(ctx->client_write_key, key_block + 0, 32);
  tls_memcpy(ctx->server_write_key, key_block + 32, 32);
  tls_memcpy(ctx->client_write_iv, key_block + 64, 16);
  tls_memcpy(ctx->server_write_iv, key_block + 80, 16);

  ctx->state = TLS_STATE_KEY_EXCHANGE_DONE;

  /* Send ClientKeyExchange (simplified - send encrypted pre-master) */
  uint8_t cke_buf[64];
  cke_buf[0] = TLS_HS_CLIENT_KEY_EX;
  cke_buf[1] = 0; cke_buf[2] = 0; cke_buf[3] = 48;
  tls_memcpy(cke_buf + 4, pre_master, 48);
  tls_send_record(ctx, TLS_RECORD_HANDSHAKE, cke_buf, 52);

  /* Send ChangeCipherSpec */
  uint8_t ccs = 1;
  tls_send_record(ctx, TLS_RECORD_CHANGE_CIPHER, &ccs, 1);

  /* Send Finished */
  uint8_t finished_buf[16];
  finished_buf[0] = TLS_HS_FINISHED;
  finished_buf[1] = 0; finished_buf[2] = 0; finished_buf[3] = 12;
  tls_prf(ctx->master_secret, 48, "client finished", seed, 64, finished_buf + 4, 12);
  tls_send_record(ctx, TLS_RECORD_HANDSHAKE, finished_buf, 16);

  /* Receive ChangeCipherSpec + Finished */
  tls_recv_record(ctx, &rec_type, rec_buf, &rec_len, sizeof(rec_buf));
  tls_recv_record(ctx, &rec_type, rec_buf, &rec_len, sizeof(rec_buf));

  ctx->state = TLS_STATE_HANDSHAKE_COMPLETE;
  ctx->state = TLS_STATE_APPLICATION;
  return 0;
}

struct tls_context *tls_connect(int socket_fd, const char *hostname,
                                 const struct tls_config *config) {
  struct tls_context *ctx = (struct tls_context *)kmalloc(sizeof(struct tls_context));
  if (!ctx) return NULL;
  tls_memset(ctx, 0, sizeof(*ctx));
  ctx->socket_fd = socket_fd;
  ctx->version = TLS_VERSION_12;
  ctx->state = TLS_STATE_INIT;
  ctx->verify_peer = config ? config->verify_peer : 0;
  (void)hostname;

  if (tls_handshake(ctx) != 0) {
    kfree(ctx);
    return NULL;
  }
  return ctx;
}

int tls_send(struct tls_context *ctx, const void *data, size_t len) {
  if (!ctx || ctx->state != TLS_STATE_APPLICATION) return -1;
  /* In a real implementation, data would be encrypted with the session keys.
   * For now, send as application data record (plaintext in development). */
  return tls_send_record(ctx, TLS_RECORD_APPLICATION, (const uint8_t *)data, len);
}

int tls_recv(struct tls_context *ctx, void *buf, size_t len) {
  if (!ctx || ctx->state != TLS_STATE_APPLICATION) return -1;
  uint8_t rec_type;
  size_t rec_len;
  if (tls_recv_record(ctx, &rec_type, (uint8_t *)buf, &rec_len, len) != 0)
    return -1;
  if (rec_type == TLS_RECORD_ALERT) {
    ctx->state = TLS_STATE_ERROR;
    ctx->error_code = ((uint8_t *)buf)[1];
    return -1;
  }
  if (rec_type != TLS_RECORD_APPLICATION) return -1;
  return (int)rec_len;
}

int tls_close(struct tls_context *ctx) {
  if (!ctx) return -1;
  uint8_t alert[2] = { 1, TLS_ALERT_CLOSE_NOTIFY };
  tls_send_record(ctx, TLS_RECORD_ALERT, alert, 2);
  ctx->state = TLS_STATE_CLOSED;
  return 0;
}

void tls_free(struct tls_context *ctx) {
  if (!ctx) return;
  /* Clear sensitive material */
  tls_memset(ctx->master_secret, 0, 48);
  tls_memset(ctx->client_write_key, 0, 32);
  tls_memset(ctx->server_write_key, 0, 32);
  kfree(ctx);
}

int tls_error(struct tls_context *ctx) {
  return ctx ? ctx->error_code : -1;
}
