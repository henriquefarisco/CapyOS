/* Focused runner for browser_fetch's pure/session and wire-mapping tests. */

#include "capylibc-net/capy_net.h"

int run_browser_fetch_tests(void);

/* The suite injects its transport and tests wire mapping directly. Keep the
 * real syscall transport unreachable in this small host binary. */
int capy_http_get_with_headers(const char *url,
                               const struct capy_http_header *req_headers,
                               int req_header_count, uint8_t *body_buf,
                               size_t body_buf_cap,
                               struct capy_http_response *out) {
  (void)url;
  (void)req_headers;
  (void)req_header_count;
  (void)body_buf;
  (void)body_buf_cap;
  (void)out;
  return -1;
}

int main(void) { return run_browser_fetch_tests(); }
