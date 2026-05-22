#ifndef NET_HTTP_H
#define NET_HTTP_H

#include <stdint.h>
#include <stddef.h>

/* HTTP buffer sizing.
 *
 * Real-world redirect chains (notably GitHub Release downloads served
 * via release-assets.githubusercontent.com with SAS query strings or
 * JWT-wrapped tokens) routinely produce 1500-1800 byte URLs and
 * 1500-1800 byte `Location:` headers. HTTP_MAX_URL and HTTP_MAX_PATH
 * therefore sit at 2048 to accommodate two doublings of the median
 * SAS URL we observe in production. `Location:` is parsed into a
 * dedicated, untruncated `http_response.location` buffer (see below),
 * so HTTP_MAX_HEADER_VALUE only governs *other* headers and can stay
 * small (Content-Type, ETag, ...) and HTTP_MAX_HEADERS is reduced
 * from 24 to 16 to compensate for the larger path/url buffers in
 * `struct http_request` (kept on the kernel stack inside http_get).
 */
#define HTTP_MAX_URL       2048
#define HTTP_MAX_HOST      192
#define HTTP_MAX_PATH      2048
#define HTTP_MAX_HEADERS   16
#define HTTP_MAX_HEADER_VALUE 256
#define HTTP_RECV_BUF_SIZE 131072
/* Must cover CAPYPKG_PAYLOAD_MAX so GitHub Release assets that are valid
 * package payloads do not fail inside the HTTP layer before capypkg can
 * verify and install them. */
#define HTTP_MAX_RESPONSE_SIZE (8 * 1024 * 1024)

enum http_method {
  HTTP_GET = 0,
  HTTP_POST,
  HTTP_PUT,
  HTTP_DELETE,
  HTTP_HEAD
};

enum http_state {
  HTTP_STATE_IDLE = 0,
  HTTP_STATE_CONNECTING,
  HTTP_STATE_SENDING,
  HTTP_STATE_RECEIVING_HEADERS,
  HTTP_STATE_RECEIVING_BODY,
  HTTP_STATE_COMPLETE,
  HTTP_STATE_ERROR
};

struct http_header {
  char name[64];
  char value[HTTP_MAX_HEADER_VALUE];
};

struct http_response {
  int status_code;
  struct http_header headers[HTTP_MAX_HEADERS];
  uint32_t header_count;
  /* Dedicated, untruncated copy of the `Location:` response header
   * (empty string if absent). Populated by http_store_headers in
   * parallel with the generic headers[] array, so the redirect
   * handler in http_get can consume the full URL even when the
   * GitHub Release / Azure SAS chain emits a 1500+ byte target. */
  char location[HTTP_MAX_URL];
  uint8_t *body;
  size_t body_len;
  size_t content_length;
  int chunked;
};

struct http_request {
  enum http_method method;
  char host[HTTP_MAX_HOST];
  char path[HTTP_MAX_PATH];
  uint16_t port;
  int use_tls;
  struct http_header headers[HTTP_MAX_HEADERS];
  uint32_t header_count;
  const uint8_t *body;
  size_t body_len;
  uint32_t timeout_ms;
};

struct http_client {
  int socket_fd;
  void *tls_ctx;
  enum http_state state;
  struct http_response response;
  uint8_t *recv_buf;
  uint32_t recv_capacity;
  uint32_t recv_len;
  int error;
};

int http_init(void);
int http_request(const struct http_request *req, struct http_response *resp);
int http_get(const char *url, struct http_response *resp);
int http_download(const char *url, uint8_t *buffer, size_t buffer_size,
                  size_t *out_len);
int http_last_error(void);
const char *http_error_string(int error);
void http_response_free(struct http_response *resp);
int http_parse_url(const char *url, char *host, size_t host_len,
                   char *path, size_t path_len, uint16_t *port, int *use_tls);
const char *http_find_header(const struct http_response *resp, const char *name);

#endif /* NET_HTTP_H */
