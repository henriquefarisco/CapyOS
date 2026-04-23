const char *http_find_header(const struct http_response *resp, const char *name) {
  if (!resp || !name) return NULL;
  for (uint32_t i = 0; i < resp->header_count; i++) {
    if (http_streq_ci(resp->headers[i].name, name)) {
      return resp->headers[i].value;
    }
  }
  return NULL;
}

static void http_set_header_value(struct http_response *resp, const char *name,
                                  const char *value) {
  if (!resp || !name || !value) return;
  for (uint32_t i = 0; i < resp->header_count; i++) {
    if (!http_streq_ci(resp->headers[i].name, name)) continue;
    http_strcpy(resp->headers[i].value, value, sizeof(resp->headers[i].value));
    return;
  }
}

int http_request(const struct http_request *req, struct http_response *resp) {
  uint32_t ip = 0;
  struct http_transport transport;
  char request_buf[4096];
  char *recv_buf = NULL;
  size_t recv_capacity = HTTP_RECV_BUF_SIZE;
  size_t total = 0;
  int header_done = 0;
  char *body_start = NULL;
  size_t header_end_offset = 0;
  size_t body_received = 0;
  int req_len;
  int result = 0;
  int buffer_exhausted = 0;

  http_set_ok();
  if (!req || !resp) return http_fail(HTTP_ERR_INVALID_ARGUMENT);
  http_memset(resp, 0, sizeof(*resp));

  /* Resolve hostname */
  if (dns_cache_lookup(req->host, &ip) != 0) {
    if (net_stack_dns_resolve(req->host, 3000, &ip) != 0) return http_fail(HTTP_ERR_DNS);
    dns_cache_insert(req->host, ip, 300);
  }

  http_transport_reset(&transport);
  if (http_transport_connect(&transport, req, ip) != 0) {
    return -g_http_last_error;
  }

  /* Build and send request */
  req_len = http_build_request(req, request_buf, sizeof(request_buf));
  if (req_len <= 0) {
    result = http_fail(HTTP_ERR_SEND);
    goto cleanup;
  }

  if (http_transport_send_all(&transport, request_buf, (size_t)req_len) != 0) {
    result = -g_http_last_error;
    goto cleanup;
  }

  if (req->body && req->body_len > 0) {
    if (http_transport_send_all(&transport, req->body, req->body_len) != 0) {
      result = -g_http_last_error;
      goto cleanup;
    }
  }

  /* Receive response */
  recv_buf = (char *)kmalloc(recv_capacity);
  if (!recv_buf) {
    result = http_fail(HTTP_ERR_NO_MEMORY);
    goto cleanup;
  }

  while (1) {
    int r;
    if (total + 1 >= recv_capacity) {
      size_t new_capacity = recv_capacity;
      char *grown = http_grow_recv_buffer(recv_buf, recv_capacity, total + 2,
                                          &new_capacity);
      if (!grown) {
        result = http_fail(recv_capacity >= HTTP_MAX_RESPONSE_SIZE
                               ? HTTP_ERR_RESPONSE_TOO_LARGE
                               : HTTP_ERR_NO_MEMORY);
        goto cleanup;
      }
      recv_buf = grown;
      recv_capacity = new_capacity;
    }

    r = http_transport_recv(&transport, recv_buf + total, recv_capacity - 1 - total);
    if (r < 0) {
      result = http_fail(HTTP_ERR_RECV);
      goto cleanup;
    }
    if (r == 0) break;
    total += (size_t)r;
    recv_buf[total] = '\0';

    if (!header_done) {
      for (size_t i = 0; i + 3 < total; i++) {
        if (recv_buf[i] == '\r' && recv_buf[i+1] == '\n' &&
            recv_buf[i+2] == '\r' && recv_buf[i+3] == '\n') {
          header_done = 1;
          header_end_offset = i + 4;
          body_start = recv_buf + header_end_offset;
          if (http_parse_status_line(recv_buf, &resp->status_code) != 0) {
            result = http_fail(HTTP_ERR_RESPONSE_PARSE);
            goto cleanup;
          }
          http_store_headers(recv_buf, header_end_offset, resp);
          resp->content_length = 0;
          for (uint32_t hi = 0; hi < resp->header_count; hi++) {
            if (http_streq_ci(resp->headers[hi].name, "Content-Length")) {
              const char *p = resp->headers[hi].value;
              while (*p >= '0' && *p <= '9') {
                resp->content_length = resp->content_length * 10 + (size_t)(*p - '0');
                p++;
              }
              break;
            }
          }
          break;
        }
      }
    }

    if (header_done) {
      size_t body_received = total - header_end_offset;
      if (resp->content_length > 0 && body_received >= resp->content_length) break;
      if (resp->chunked && body_start &&
          http_chunked_complete((const uint8_t *)body_start, body_received)) break;
    }
  }

  if (!buffer_exhausted) {
    buffer_exhausted = (total + 1 >= recv_capacity &&
                        recv_capacity >= HTTP_MAX_RESPONSE_SIZE);
  }

  if (!header_done) {
    result = http_fail(buffer_exhausted ? HTTP_ERR_RESPONSE_TOO_LARGE
                                        : HTTP_ERR_RESPONSE_PARSE);
    goto cleanup;
  }

  body_received = total - header_end_offset;
  if (buffer_exhausted &&
      ((resp->content_length > 0 && body_received < resp->content_length) ||
       (resp->chunked && body_start &&
        !http_chunked_complete((const uint8_t *)body_start, body_received)) ||
       (!resp->chunked && resp->content_length == 0))) {
    result = http_fail(HTTP_ERR_RESPONSE_TOO_LARGE);
    goto cleanup;
  }

  if (body_received > 0 && body_start) {
    const char *content_encoding;
    resp->body = (uint8_t *)kmalloc(body_received + 1);
    if (!resp->body) {
      result = http_fail(HTTP_ERR_NO_MEMORY);
      goto cleanup;
    }
    http_memcpy(resp->body, body_start, body_received);
    resp->body[body_received] = '\0';
    resp->body_len = body_received;
    if (resp->chunked) {
      size_t decoded_len = resp->body_len;
      if (http_decode_chunked_body(resp->body, &decoded_len) != 0) {
        http_response_free(resp);
        result = http_fail(HTTP_ERR_RESPONSE_PARSE);
        goto cleanup;
      }
      resp->body_len = decoded_len;
    }

    content_encoding = http_find_header(resp, "Content-Encoding");
    if (http_encoding_requires_decode(content_encoding)) {
      uint8_t *decoded_body = NULL;
      size_t decoded_len = 0;
      int decode_rc = http_encoding_decode_body(content_encoding,
                                                resp->body,
                                                resp->body_len,
                                                HTTP_MAX_RESPONSE_SIZE,
                                                &decoded_body,
                                                &decoded_len);
      if (decode_rc == HTTP_ENCODING_ERR_NO_MEMORY) {
        http_response_free(resp);
        result = http_fail(HTTP_ERR_NO_MEMORY);
        goto cleanup;
      }
      if (decode_rc == HTTP_ENCODING_ERR_TOO_LARGE) {
        http_response_free(resp);
        result = http_fail(HTTP_ERR_RESPONSE_TOO_LARGE);
        goto cleanup;
      }
      if (decode_rc != HTTP_ENCODING_OK) {
        http_response_free(resp);
        result = http_fail(HTTP_ERR_RESPONSE_ENCODING);
        goto cleanup;
      }
      kfree(resp->body);
      resp->body = decoded_body;
      resp->body_len = decoded_len;
      resp->content_length = decoded_len;
      http_set_header_value(resp, "Content-Encoding", "identity");
    }
  }

  http_set_ok();
cleanup:
  if (recv_buf) {
    kfree(recv_buf);
  }
  /* Return connection to pool if keep-alive was negotiated and request succeeded */
  if (result == 0 && transport.socket_fd >= 0) {
    const char *conn_hdr = http_find_header(resp, "Connection");
    int do_keepalive = (conn_hdr && http_contains_ci(conn_hdr, "keep-alive"));
    /* HTTP/1.1 defaults to keep-alive unless Connection: close is specified */
    if (!conn_hdr && resp->status_code >= 200 && resp->status_code < 400) {
      int close_explicit = 0;
      for (uint32_t hi = 0; hi < resp->header_count; hi++) {
        if (http_streq_ci(resp->headers[hi].name, "Connection") &&
            http_contains_ci(resp->headers[hi].value, "close")) {
          close_explicit = 1; break;
        }
      }
      if (!close_explicit) do_keepalive = 1;
    }
    if (do_keepalive) {
      http_pool_store(req->host, req->port, req->use_tls,
                      transport.socket_fd, transport.tls);
      /* Don't let http_transport_close close the socket */
      transport.socket_fd = -1;
      transport.tls = NULL;
    }
  }
  http_transport_close(&transport);
  return result;
}

/* Compose an absolute URL from a Location header value.
 * Handles three Location forms:
 *   1) "https://host/path"    absolute (scheme + authority)
 *   2) "//host/path"          protocol-relative (inherit scheme)
 *   3) "/path" / "path"       path-relative (inherit scheme + authority)
 * Returns 0 on success, -1 if the result would not fit `out_size`.
 */
