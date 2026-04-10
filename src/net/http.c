#include "net/http.h"
#include "net/socket.h"
#include "net/dns_cache.h"
#include "net/stack.h"
#include <stddef.h>

static void http_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static void http_memcpy(void *d, const void *s, size_t n) {
  uint8_t *dp = (uint8_t *)d; const uint8_t *sp = (const uint8_t *)s;
  for (size_t i = 0; i < n; i++) dp[i] = sp[i];
}
static size_t http_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void http_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = '\0';
}
static int http_strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) return a[i] - b[i];
    if (a[i] == 0) return 0;
  }
  return 0;
}

int http_init(void) { return 0; }

int http_parse_url(const char *url, char *host, size_t host_len,
                   char *path, size_t path_len, uint16_t *port, int *use_tls) {
  if (!url) return -1;
  *use_tls = 0;
  *port = 80;

  if (http_strncmp(url, "https://", 8) == 0) {
    *use_tls = 1; *port = 443; url += 8;
  } else if (http_strncmp(url, "http://", 7) == 0) {
    url += 7;
  }

  size_t hi = 0;
  while (url[hi] && url[hi] != '/' && url[hi] != ':' && hi < host_len - 1) {
    host[hi] = url[hi]; hi++;
  }
  host[hi] = '\0';
  url += hi;

  if (*url == ':') {
    url++;
    uint16_t p = 0;
    while (*url >= '0' && *url <= '9') { p = p * 10 + (uint16_t)(*url - '0'); url++; }
    if (p > 0) *port = p;
  }

  if (*url == '/') http_strcpy(path, url, path_len);
  else http_strcpy(path, "/", path_len);

  return 0;
}

static int http_build_request(const struct http_request *req, char *buf, size_t buf_size) {
  const char *method_str = "GET";
  switch (req->method) {
    case HTTP_POST: method_str = "POST"; break;
    case HTTP_PUT: method_str = "PUT"; break;
    case HTTP_DELETE: method_str = "DELETE"; break;
    case HTTP_HEAD: method_str = "HEAD"; break;
    default: break;
  }

  size_t pos = 0;
  const char *s;

  s = method_str;
  while (*s && pos < buf_size - 1) buf[pos++] = *s++;
  buf[pos++] = ' ';
  s = req->path;
  while (*s && pos < buf_size - 1) buf[pos++] = *s++;
  s = " HTTP/1.1\r\nHost: ";
  while (*s && pos < buf_size - 1) buf[pos++] = *s++;
  s = req->host;
  while (*s && pos < buf_size - 1) buf[pos++] = *s++;
  s = "\r\nConnection: close\r\nUser-Agent: CapyOS/0.8\r\n";
  while (*s && pos < buf_size - 1) buf[pos++] = *s++;

  for (uint32_t i = 0; i < req->header_count; i++) {
    s = req->headers[i].name;
    while (*s && pos < buf_size - 1) buf[pos++] = *s++;
    s = ": ";
    while (*s && pos < buf_size - 1) buf[pos++] = *s++;
    s = req->headers[i].value;
    while (*s && pos < buf_size - 1) buf[pos++] = *s++;
    s = "\r\n";
    while (*s && pos < buf_size - 1) buf[pos++] = *s++;
  }

  s = "\r\n";
  while (*s && pos < buf_size - 1) buf[pos++] = *s++;
  buf[pos] = '\0';
  return (int)pos;
}

static int http_parse_status_line(const char *line, int *status_code) {
  /* HTTP/1.x NNN ... */
  if (http_strncmp(line, "HTTP/1.", 7) != 0) return -1;
  const char *p = line + 7;
  while (*p && *p != ' ') p++;
  if (*p == ' ') p++;
  *status_code = 0;
  while (*p >= '0' && *p <= '9') { *status_code = *status_code * 10 + (*p - '0'); p++; }
  return 0;
}

static int http_find_header(const char *headers, const char *name, char *value, size_t vlen) {
  size_t nlen = http_strlen(name);
  const char *p = headers;
  while (*p) {
    if (http_strncmp(p, name, nlen) == 0 && p[nlen] == ':') {
      p += nlen + 1;
      while (*p == ' ') p++;
      size_t i = 0;
      while (p[i] && p[i] != '\r' && p[i] != '\n' && i < vlen - 1) {
        value[i] = p[i]; i++;
      }
      value[i] = '\0';
      return 0;
    }
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
  }
  return -1;
}

int http_request(const struct http_request *req, struct http_response *resp) {
  if (!req || !resp) return -1;
  http_memset(resp, 0, sizeof(*resp));

  /* Resolve hostname */
  uint32_t ip = 0;
  if (dns_cache_lookup(req->host, &ip) != 0) {
    if (net_stack_dns_resolve(req->host, 3000, &ip) != 0) return -1;
    dns_cache_insert(req->host, ip, 300);
  }

  /* Create socket and connect */
  int fd = socket_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return -1;

  struct sockaddr_in addr;
  http_memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = ((req->port >> 8) & 0xFF) | ((req->port << 8) & 0xFF00);
  addr.sin_addr = ip;

  if (socket_connect(fd, &addr) != 0) {
    socket_close(fd);
    return -1;
  }

  /* Build and send request */
  char request_buf[2048];
  int req_len = http_build_request(req, request_buf, sizeof(request_buf));
  if (req_len <= 0) { socket_close(fd); return -1; }

  if (req->use_tls) {
    /* TLS would wrap the socket here */
    /* For now, send plaintext */
  }

  if (socket_send(fd, request_buf, (size_t)req_len, 0) < 0) {
    socket_close(fd);
    return -1;
  }

  if (req->body && req->body_len > 0) {
    socket_send(fd, req->body, req->body_len, 0);
  }

  /* Receive response */
  char recv_buf[HTTP_RECV_BUF_SIZE];
  size_t total = 0;
  int header_done = 0;
  char *body_start = NULL;
  size_t header_end_offset = 0;

  while (total < sizeof(recv_buf) - 1) {
    int r = socket_recv(fd, recv_buf + total, sizeof(recv_buf) - 1 - total, 0);
    if (r <= 0) break;
    total += (size_t)r;
    recv_buf[total] = '\0';

    if (!header_done) {
      for (size_t i = 0; i + 3 < total; i++) {
        if (recv_buf[i] == '\r' && recv_buf[i+1] == '\n' &&
            recv_buf[i+2] == '\r' && recv_buf[i+3] == '\n') {
          header_done = 1;
          header_end_offset = i + 4;
          body_start = recv_buf + header_end_offset;
          break;
        }
      }
    }

    if (header_done) break;
  }

  if (!header_done) { socket_close(fd); return -1; }

  /* Parse status line */
  http_parse_status_line(recv_buf, &resp->status_code);

  /* Parse content-length */
  char cl_val[32];
  resp->content_length = 0;
  if (http_find_header(recv_buf, "Content-Length", cl_val, sizeof(cl_val)) == 0) {
    const char *p = cl_val;
    while (*p >= '0' && *p <= '9') {
      resp->content_length = resp->content_length * 10 + (size_t)(*p - '0');
      p++;
    }
  }

  /* Copy body */
  size_t body_received = total - header_end_offset;
  if (body_received > 0 && body_start) {
    resp->body = (uint8_t *)body_start;
    resp->body_len = body_received;

    /* Read remaining body */
    while (resp->body_len < resp->content_length && total < sizeof(recv_buf) - 1) {
      int r = socket_recv(fd, recv_buf + total, sizeof(recv_buf) - 1 - total, 0);
      if (r <= 0) break;
      total += (size_t)r;
      resp->body_len += (size_t)r;
    }
  }

  socket_close(fd);
  return 0;
}

int http_get(const char *url, struct http_response *resp) {
  if (!url || !resp) return -1;

  struct http_request req;
  http_memset(&req, 0, sizeof(req));
  req.method = HTTP_GET;
  req.timeout_ms = 10000;

  if (http_parse_url(url, req.host, HTTP_MAX_HOST, req.path, HTTP_MAX_PATH,
                     &req.port, &req.use_tls) != 0)
    return -1;

  return http_request(&req, resp);
}

int http_download(const char *url, uint8_t *buffer, size_t buffer_size,
                  size_t *out_len) {
  struct http_response resp;
  if (http_get(url, &resp) != 0) return -1;
  if (resp.status_code != 200) return -1;

  size_t copy = resp.body_len;
  if (copy > buffer_size) copy = buffer_size;
  if (resp.body && copy > 0) http_memcpy(buffer, resp.body, copy);
  if (out_len) *out_len = copy;
  return 0;
}

void http_response_free(struct http_response *resp) {
  if (resp) {
    resp->body = NULL;
    resp->body_len = 0;
  }
}
