int net_stack_dns_resolve(const char *hostname, uint32_t timeout_ms,
                          uint32_t *out_ip) {
  (void)hostname; (void)timeout_ms;
  if (out_ip) *out_ip = 0;
  return -1;
}

int tls_init(void) { return 0; }
struct tls_context *tls_connect(int socket_fd, const char *hostname,
                                const struct tls_config *config) {
  (void)socket_fd;
  (void)hostname;
  (void)config;
  return NULL;
}
int tls_send(struct tls_context *ctx, const void *data, size_t len) {
  (void)ctx;
  (void)data;
  (void)len;
  return -1;
}
int tls_recv(struct tls_context *ctx, void *buf, size_t len) {
  (void)ctx;
  (void)buf;
  (void)len;
  return -1;
}
int tls_close(struct tls_context *ctx) {
  (void)ctx;
  return 0;
}
void tls_free(struct tls_context *ctx) { (void)ctx; }
int tls_handshake(struct tls_context *ctx) {
  (void)ctx;
  return -1;
}
const char *tls_state_name(enum tls_state state) {
  (void)state;
  return "init";
}
enum tls_state tls_last_state(void) { return TLS_STATE_INIT; }
int tls_last_error(void) { return 0; }
const char *tls_alert_name(int alert) {
  (void)alert;
  return "ok";
}
int tls_error(struct tls_context *ctx) {
  (void)ctx;
  return 0;
}
int tls_get_security_info(struct tls_context *ctx, struct tls_security_info *info) {
  (void)ctx;
  (void)info;
  return -1;
}
int tls_get_last_security_info(struct tls_security_info *info) {
  (void)info;
  return -1;
}
const char *tls_version_name(uint16_t version) {
  (void)version;
  return "TLS";
}
const char *tls_cipher_suite_name(uint16_t suite) {
  (void)suite;
  return "cipher";
}

