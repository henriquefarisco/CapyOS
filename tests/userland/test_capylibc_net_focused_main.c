/* Small runner for the libcapy-net suite, avoiding the full host aggregate. */

#include "capylibc-tls/capy_tls.h"

int test_capylibc_net_run(void);

/* The focused HTTP tests exercise the fail-closed HTTPS gate, not a real TLS
 * handshake. These stubs keep this runner small; the complete suite links and
 * tests the real capylibc-tls implementation separately. */
int capy_tls_is_supported(void) { return 0; }

struct capy_tls_context *capy_tls_connect_tcp(
    int tcp_fd, const char *hostname, const struct capy_tls_config *config) {
  (void)tcp_fd;
  (void)hostname;
  (void)config;
  return 0;
}

int capy_tls_send(struct capy_tls_context *ctx, const void *data, size_t len) {
  (void)ctx;
  (void)data;
  (void)len;
  return -1;
}

int capy_tls_recv(struct capy_tls_context *ctx, void *buf, size_t len) {
  (void)ctx;
  (void)buf;
  (void)len;
  return -1;
}

int capy_tls_close(struct capy_tls_context *ctx) {
  (void)ctx;
  return -1;
}

void capy_tls_free(struct capy_tls_context *ctx) { (void)ctx; }

capy_tls_err_t capy_tls_last_error(void) { return CAPY_TLS_EUNSUPPORTED; }

capy_tls_state_t capy_tls_last_state(void) {
  return CAPY_TLS_STATE_UNSUPPORTED;
}

int main(void) { return test_capylibc_net_run(); }
