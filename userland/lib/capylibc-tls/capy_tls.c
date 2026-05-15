#include "capy_tls_internal.h"
#include "security/tls_hostname_policy.h"

static capy_tls_err_t g_capy_tls_last_error = CAPY_TLS_OK;
static capy_tls_state_t g_capy_tls_last_state = CAPY_TLS_STATE_INIT;
static struct capy_tls_security_info g_capy_tls_last_info;

static void capy_tls_zero_info(struct capy_tls_security_info *info) {
  if (!info) return;
  uint8_t *p = (uint8_t *)info;
  for (size_t i = 0; i < sizeof(*info); i++) p[i] = 0;
}

static void capy_tls_record(capy_tls_state_t state, capy_tls_err_t err) {
  g_capy_tls_last_state = state;
  g_capy_tls_last_error = err;
  if (err != CAPY_TLS_OK) capy_tls_zero_info(&g_capy_tls_last_info);
}

static int capy_tls_hostname_valid(const char *hostname) {
  return tls_hostname_policy_valid(hostname);
}

int capy_tls_init(void) {
  capy_tls_zero_info(&g_capy_tls_last_info);
  capy_tls_record(CAPY_TLS_STATE_INIT, CAPY_TLS_OK);
  return 0;
}

int capy_tls_is_supported(void) {
  return 0;
}

struct capy_tls_context *capy_tls_connect_tcp(
    int socket_fd,
    const char *hostname,
    const struct capy_tls_config *config) {
  struct capy_tls_effective_config effective;
  struct capy_tls_context *prepared;
  capy_tls_err_t backend_err;
  capy_tls_zero_info(&g_capy_tls_last_info);
  if (socket_fd < 0 || !capy_tls_hostname_valid(hostname) ||
      !capy_tls_config_resolve(config, &effective)) {
    capy_tls_record(CAPY_TLS_STATE_ERROR, CAPY_TLS_EINVAL);
    return 0;
  }
  prepared = capy_tls_context_acquire();
  if (!prepared) {
    capy_tls_record(CAPY_TLS_STATE_ERROR, CAPY_TLS_ESTATE);
    return 0;
  }
  if (!capy_tls_context_prepare(prepared, socket_fd, hostname,
                                &effective)) {
    capy_tls_context_release(prepared);
    capy_tls_record(CAPY_TLS_STATE_ERROR, CAPY_TLS_EINVAL);
    return 0;
  }
  backend_err = capy_tls_backend_connect(prepared);
  capy_tls_context_release(prepared);
  if (backend_err != CAPY_TLS_EUNSUPPORTED) {
    capy_tls_record(CAPY_TLS_STATE_ERROR, backend_err);
    return 0;
  }
  capy_tls_record(CAPY_TLS_STATE_UNSUPPORTED, CAPY_TLS_EUNSUPPORTED);
  return 0;
}

int capy_tls_send(struct capy_tls_context *ctx, const void *data, size_t len) {
  if (!ctx || (!data && len > 0)) {
    capy_tls_record(CAPY_TLS_STATE_ERROR, CAPY_TLS_EINVAL);
    return -1;
  }
  capy_tls_record(CAPY_TLS_STATE_UNSUPPORTED, CAPY_TLS_EUNSUPPORTED);
  return -1;
}

int capy_tls_recv(struct capy_tls_context *ctx, void *buf, size_t len) {
  if (!ctx || !buf || len == 0) {
    capy_tls_record(CAPY_TLS_STATE_ERROR, CAPY_TLS_EINVAL);
    return -1;
  }
  capy_tls_record(CAPY_TLS_STATE_UNSUPPORTED, CAPY_TLS_EUNSUPPORTED);
  return -1;
}

int capy_tls_close(struct capy_tls_context *ctx) {
  if (!ctx) {
    capy_tls_record(CAPY_TLS_STATE_ERROR, CAPY_TLS_EINVAL);
    return -1;
  }
  capy_tls_record(CAPY_TLS_STATE_UNSUPPORTED, CAPY_TLS_EUNSUPPORTED);
  return -1;
}

void capy_tls_free(struct capy_tls_context *ctx) {
  capy_tls_context_release(ctx);
}

capy_tls_err_t capy_tls_last_error(void) {
  return g_capy_tls_last_error;
}

capy_tls_state_t capy_tls_last_state(void) {
  return g_capy_tls_last_state;
}

const char *capy_tls_error_name(capy_tls_err_t err) {
  switch (err) {
    case CAPY_TLS_OK: return "ok";
    case CAPY_TLS_EINVAL: return "invalid-argument";
    case CAPY_TLS_EUNSUPPORTED: return "unsupported";
    case CAPY_TLS_ESTATE: return "bad-state";
    default: return "unknown";
  }
}

const char *capy_tls_state_name(capy_tls_state_t state) {
  switch (state) {
    case CAPY_TLS_STATE_INIT: return "init";
    case CAPY_TLS_STATE_UNSUPPORTED: return "unsupported";
    case CAPY_TLS_STATE_CLOSED: return "closed";
    case CAPY_TLS_STATE_ERROR: return "error";
    default: return "unknown";
  }
}

int capy_tls_get_security_info(
    struct capy_tls_context *ctx,
    struct capy_tls_security_info *info) {
  if (!ctx || !info) {
    capy_tls_record(CAPY_TLS_STATE_ERROR, CAPY_TLS_EINVAL);
    return -1;
  }
  capy_tls_zero_info(info);
  capy_tls_record(CAPY_TLS_STATE_UNSUPPORTED, CAPY_TLS_EUNSUPPORTED);
  return -1;
}

int capy_tls_get_last_security_info(struct capy_tls_security_info *info) {
  if (!info) {
    capy_tls_record(CAPY_TLS_STATE_ERROR, CAPY_TLS_EINVAL);
    return -1;
  }
  *info = g_capy_tls_last_info;
  return 0;
}
