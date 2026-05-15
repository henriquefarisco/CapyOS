#include "capy_tls_internal.h"
#include "security/tls_hostname_policy.h"

static struct capy_tls_context g_capy_tls_context_slots[CAPY_TLS_CONTEXT_SLOT_COUNT];
static uint8_t g_capy_tls_context_slot_used[CAPY_TLS_CONTEXT_SLOT_COUNT];

static void capy_tls_memzero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) return;
  while (len-- > 0) *p++ = 0;
}

void capy_tls_context_reset(struct capy_tls_context *ctx) {
  if (!ctx) return;
  capy_tls_memzero(ctx, sizeof(*ctx));
  ctx->socket_fd = -1;
  ctx->config.verify_peer = 1;
  ctx->config.timeout_ms = CAPY_TLS_TIMEOUT_DEFAULT_MS;
}

void capy_tls_context_clear(struct capy_tls_context *ctx) {
  capy_tls_context_reset(ctx);
}

struct capy_tls_context *capy_tls_context_acquire(void) {
  size_t i;
  for (i = 0; i < CAPY_TLS_CONTEXT_SLOT_COUNT; i++) {
    if (!g_capy_tls_context_slot_used[i]) {
      g_capy_tls_context_slot_used[i] = 1;
      capy_tls_context_reset(&g_capy_tls_context_slots[i]);
      return &g_capy_tls_context_slots[i];
    }
  }
  return 0;
}

void capy_tls_context_release(struct capy_tls_context *ctx) {
  size_t i;
  if (!ctx) return;
  for (i = 0; i < CAPY_TLS_CONTEXT_SLOT_COUNT; i++) {
    if (ctx == &g_capy_tls_context_slots[i]) {
      capy_tls_context_clear(&g_capy_tls_context_slots[i]);
      g_capy_tls_context_slot_used[i] = 0;
      return;
    }
  }
}

static int capy_tls_hostname_copy(char *dst, const char *src) {
  size_t i = 0;
  if (!dst || !src) return 0;
  while (src[i]) {
    if (i >= CAPY_TLS_HOSTNAME_MAX_LEN) return 0;
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
  return 1;
}

int capy_tls_context_prepare(
    struct capy_tls_context *ctx,
    int socket_fd,
    const char *hostname,
    const struct capy_tls_effective_config *config) {
  if (!ctx) return 0;
  capy_tls_context_reset(ctx);
  if (socket_fd < 0 || !config ||
      !tls_hostname_policy_valid(hostname)) return 0;
  ctx->socket_fd = socket_fd;
  if (!capy_tls_hostname_copy(ctx->hostname, hostname)) {
    capy_tls_context_reset(ctx);
    return 0;
  }
  ctx->config = *config;
  return 1;
}
