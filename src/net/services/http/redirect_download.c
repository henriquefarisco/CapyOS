#include "internal/http_internal.h"

int http_resolve_location(const struct http_request *base,
                          const char *location,
                          char *out, size_t out_size) {
  size_t pos = 0;
  if (!base || !location || !out || out_size < 16) return -1;

  if (http_strncmp(location, "http://", 7) == 0 ||
      http_strncmp(location, "https://", 8) == 0) {
    http_strcpy(out, location, out_size);
    return out[0] ? 0 : -1;
  }

  if (location[0] == '/' && location[1] == '/') {
    if (http_buf_append_str(out, out_size, &pos,
                            base->use_tls ? "https:" : "http:") != 0) return -1;
    if (http_buf_append_str(out, out_size, &pos, location) != 0) return -1;
    out[pos] = '\0';
    return 0;
  }

  if (http_buf_append_str(out, out_size, &pos,
                          base->use_tls ? "https://" : "http://") != 0) return -1;
  if (http_buf_append_str(out, out_size, &pos, base->host) != 0) return -1;
  if (!http_is_default_port(base)) {
    if (http_buf_append_char(out, out_size, &pos, ':') != 0) return -1;
    if (http_buf_append_u32(out, out_size, &pos, base->port) != 0) return -1;
  }

  if (location[0] == '/') {
    if (http_buf_append_str(out, out_size, &pos, location) != 0) return -1;
  } else {
    const char *base_path = base->path[0] ? base->path : "/";
    size_t base_len = http_strlen(base_path);
    size_t cut = base_len;
    while (cut > 0 && base_path[cut - 1] != '/') cut--;
    if (cut == 0) {
      if (http_buf_append_char(out, out_size, &pos, '/') != 0) return -1;
    } else {
      for (size_t i = 0; i < cut; i++) {
        if (http_buf_append_char(out, out_size, &pos, base_path[i]) != 0) return -1;
      }
    }
    if (http_buf_append_str(out, out_size, &pos, location) != 0) return -1;
  }

  out[pos] = '\0';
  return 0;
}

int http_status_is_redirect(int status) {
  return status == 301 || status == 302 || status == 303 ||
         status == 307 || status == 308;
}

int http_get(const char *url, struct http_response *resp) {
  if (!url || !resp) return http_fail(HTTP_ERR_INVALID_ARGUMENT);

  struct http_request req;
  char next_url[HTTP_MAX_URL];
  const int max_hops = 5;

  http_memset(resp, 0, sizeof(*resp));
  http_strcpy(next_url, url, sizeof(next_url));

  for (int hop = 0; hop <= max_hops; hop++) {
    http_memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    req.timeout_ms = 10000;

    if (http_parse_url(next_url, req.host, HTTP_MAX_HOST, req.path, HTTP_MAX_PATH,
                       &req.port, &req.use_tls) != 0) {
      return -g_http_last_error;
    }

    int rc = http_request(&req, resp);
    if (rc != 0) return rc;

    if (!http_status_is_redirect(resp->status_code)) {
      return 0;
    }
    if (hop == max_hops) {
      http_response_free(resp);
      return http_fail(HTTP_ERR_REDIRECT_LIMIT);
    }

    const char *location = http_find_header(resp, "Location");
    if (!location || !location[0]) {
      return 0;
    }

    char loc_copy[HTTP_MAX_HEADER_VALUE];
    http_strcpy(loc_copy, location, sizeof(loc_copy));

    char resolved[HTTP_MAX_URL];
    if (http_resolve_location(&req, loc_copy, resolved, sizeof(resolved)) != 0) {
      http_response_free(resp);
      return http_fail(HTTP_ERR_BAD_REDIRECT);
    }

    http_response_free(resp);
    http_strcpy(next_url, resolved, sizeof(next_url));
  }

  return http_fail(HTTP_ERR_REDIRECT_LIMIT);
}

int http_download(const char *url, uint8_t *buffer, size_t buffer_size,
                  size_t *out_len) {
  struct http_response resp;
  if (http_get(url, &resp) != 0) return -1;
  if (resp.status_code != 200) {
    http_response_free(&resp);
    return -1;
  }

  size_t copy = resp.body_len;
  if (copy > buffer_size) copy = buffer_size;
  if (resp.body && copy > 0) http_memcpy(buffer, resp.body, copy);
  if (out_len) *out_len = copy;
  http_response_free(&resp);
  return 0;
}

void http_response_free(struct http_response *resp) {
  if (resp) {
    if (resp->body) {
      kfree(resp->body);
      resp->body = NULL;
    }
    resp->body_len = 0;
    resp->content_length = 0;
    resp->header_count = 0;
    resp->chunked = 0;
  }
}
