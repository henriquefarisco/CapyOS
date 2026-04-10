#ifndef NET_HTTP_H
#define NET_HTTP_H

#include <stdint.h>
#include <stddef.h>

#define HTTP_MAX_URL       512
#define HTTP_MAX_HOST      128
#define HTTP_MAX_PATH      256
#define HTTP_MAX_HEADERS   16
#define HTTP_MAX_HEADER_VALUE 256
#define HTTP_RECV_BUF_SIZE 8192

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
  uint8_t recv_buf[HTTP_RECV_BUF_SIZE];
  uint32_t recv_len;
  int error;
};

int http_init(void);
int http_request(const struct http_request *req, struct http_response *resp);
int http_get(const char *url, struct http_response *resp);
int http_download(const char *url, uint8_t *buffer, size_t buffer_size,
                  size_t *out_len);
void http_response_free(struct http_response *resp);
int http_parse_url(const char *url, char *host, size_t host_len,
                   char *path, size_t path_len, uint16_t *port, int *use_tls);

#endif /* NET_HTTP_H */
