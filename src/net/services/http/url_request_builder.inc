int http_init(void) {
  http_set_ok();
  return 0;
}

int http_parse_url(const char *url, char *host, size_t host_len,
                   char *path, size_t path_len, uint16_t *port, int *use_tls) {
  http_set_ok();
  if (!url || !host || host_len == 0 || !path || path_len == 0 || !port || !use_tls) {
    return http_fail(HTTP_ERR_INVALID_ARGUMENT);
  }
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
  if (hi == 0) return http_fail(HTTP_ERR_INVALID_URL);
  url += hi;

  if (*url == ':') {
    int saw_digit = 0;
    url++;
    uint16_t p = 0;
    while (*url >= '0' && *url <= '9') {
      saw_digit = 1;
      p = p * 10 + (uint16_t)(*url - '0');
      url++;
    }
    if (!saw_digit) return http_fail(HTTP_ERR_INVALID_URL);
    if (p > 0) *port = p;
  }

  if (*url == '/') http_strcpy(path, url, path_len);
  else http_strcpy(path, "/", path_len);

  return 0;
}

static int http_build_request(const struct http_request *req, char *buf, size_t buf_size) {
  const char *method_str = "GET";
  size_t pos = 0;

  if (!req || !buf || buf_size < 8) {
    return -1;
  }

  switch (req->method) {
    case HTTP_POST: method_str = "POST"; break;
    case HTTP_PUT: method_str = "PUT"; break;
    case HTTP_DELETE: method_str = "DELETE"; break;
    case HTTP_HEAD: method_str = "HEAD"; break;
    default: break;
  }

  if (http_buf_append_str(buf, buf_size, &pos, method_str) != 0 ||
      http_buf_append_char(buf, buf_size, &pos, ' ') != 0 ||
      http_buf_append_str(buf, buf_size, &pos, req->path[0] ? req->path : "/") != 0 ||
      http_buf_append_str(buf, buf_size, &pos, " HTTP/1.1\r\nHost: ") != 0 ||
      http_buf_append_str(buf, buf_size, &pos, req->host) != 0) {
    return -1;
  }

  if (!http_is_default_port(req)) {
    if (http_buf_append_char(buf, buf_size, &pos, ':') != 0 ||
        http_buf_append_u32(buf, buf_size, &pos, req->port) != 0) {
      return -1;
    }
  }

  if (!http_request_has_header(req, "Connection") &&
      http_buf_append_str(buf, buf_size, &pos, "\r\nConnection: keep-alive") != 0) {
    return -1;
  }
  if (!http_request_has_header(req, "User-Agent") &&
      http_buf_append_str(buf, buf_size, &pos,
                          "\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) CapyBrowser/0.9 Chrome/124.0.0.0 Safari/537.36") != 0) {
    return -1;
  }
  if (!http_request_has_header(req, "Accept") &&
      http_buf_append_str(buf, buf_size, &pos,
                          "\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8") != 0) {
    return -1;
  }
  if (!http_request_has_header(req, "Accept-Language") &&
      http_buf_append_str(buf, buf_size, &pos, "\r\nAccept-Language: en-US,en;q=0.7") != 0) {
    return -1;
  }
  if (!http_request_has_header(req, "Accept-Encoding") &&
      http_buf_append_str(buf, buf_size, &pos,
                          "\r\nAccept-Encoding: gzip, deflate, identity") != 0) {
    return -1;
  }
  if (req->method == HTTP_GET &&
      !http_request_has_header(req, "Upgrade-Insecure-Requests") &&
      http_buf_append_str(buf, buf_size, &pos,
                          "\r\nUpgrade-Insecure-Requests: 1") != 0) {
    return -1;
  }
  if (req->body && req->body_len > 0 && !http_request_has_header(req, "Content-Length")) {
    if (http_buf_append_str(buf, buf_size, &pos, "\r\nContent-Length: ") != 0 ||
        http_buf_append_u32(buf, buf_size, &pos, (uint32_t)req->body_len) != 0) {
      return -1;
    }
  }

  for (uint32_t i = 0; i < req->header_count; i++) {
    if (http_buf_append_str(buf, buf_size, &pos, "\r\n") != 0 ||
        http_buf_append_str(buf, buf_size, &pos, req->headers[i].name) != 0 ||
        http_buf_append_str(buf, buf_size, &pos, ": ") != 0 ||
        http_buf_append_str(buf, buf_size, &pos, req->headers[i].value) != 0) {
      return -1;
    }
  }

  if (http_buf_append_str(buf, buf_size, &pos, "\r\n\r\n") != 0) {
    return -1;
  }
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

