int http_init(void) { return 0; }
int http_request(const struct http_request *req, struct http_response *resp) {
  static const char body[] =
      "<html><head><title>Modern Example</title></head>"
      "<body>"
      "<h1>Modern Example</h1>"
      "<p>Primary content loaded.</p>"
      "<noscript>JS disabled fallback works.</noscript>"
      "<a href=\"https://example.com/docs\">Docs</a>"
      "</body></html>";
  static const char landing_body[] =
      "<html><head><title>Redirect Landing</title></head>"
      "<body><h1>Redirect Landing</h1><p>Redirect complete.</p></body></html>";
  static const char asset_body[] =
      "<html><head><title>Asset Page</title>"
      "<link rel=\"stylesheet\" href=\"https://assets.test/site.css\"></head>"
      "<body><h1>Asset Title</h1><img alt=\"Pixel\" src=\"https://assets.test/pixel.png\"></body></html>";
  static const char imported_css_body[] =
      "<html><head><title>Imported CSS Page</title>"
      "<link rel=\"preload\" as=\"style\" href=\"https://assets.test/import-root.css\"></head>"
      "<body><h1>Imported CSS Title</h1><p>Imported theme paragraph.</p></body></html>";
  static const char style_import_body[] =
      "<html><head><title>Style Import Page</title>"
      "<style>@import \"https://assets.test/import-theme.css\";"
      "@import url(\"https://assets.test/import-theme.css\");</style></head>"
      "<body><h1>Style Import Title</h1><p>Inline style import paragraph.</p></body></html>";
  static const char base_body[] =
      "<html><head><title>Base Asset Page</title>"
      "<base href=\"https://cdnbase.test/assets/\">"
      "<link rel=\"stylesheet\" href=\"site.css\"></head>"
      "<body><h1>Base Asset Title</h1><img alt=\"Base Pixel\" src=\"pixel.png\"></body></html>";
  static const char meta_body[] =
      "<html><head><title>Meta Redirect</title>"
      "<meta http-equiv=\"refresh\" content=\"0; url=/landing\"></head>"
      "<body><p>Redirecting...</p></body></html>";
  static const char meta_landing_body[] =
      "<html><head><title>Meta Landing</title></head>"
      "<body><h1>Meta Landing</h1><p>Meta refresh complete.</p></body></html>";
  static const char webp_page_body[] =
      "<html><head><title>WebP Only</title></head>"
      "<body><h1>WebP Only</h1><img alt=\"Modern Image\" src=\"https://assets.test/modern.webp\"></body></html>";
  static const char fresh_body[] =
      "<html><head><title>Fresh Example</title></head>"
      "<body><h1>Fresh Example</h1><p>Newest navigation wins.</p></body></html>";
  static const char script_body[] =
      "window.__BOOTSTRAP__={\"chunks\":[1,2],\"buildId\":\"dev\"};"
      "function hydrate(){return document.body;} self.__next_f.push([1]);";
  static const char noisy_body[] =
      "window.__BOOTSTRAP__={\"chunks\":[1]};\n"
      "self.__next_f.push([\"ignored bootstrap\"]);\n"
      "<html><head><title>Noisy Page</title></head>"
      "<body><h1>Noisy Page</h1><p>Readable content.</p></body></html>";
  static const char shell_body[] =
      "<html><head><title>App Shell</title></head><body>"
      "<div>window.__BOOTSTRAP__={\"chunks\":[1,2,3],\"buildId\":\"dev\","
      "\"props\":{\"pageProps\":{\"items\":[\"a\",\"b\",\"c\"]}}};"
      "self.__next_f.push([\"long script bootstrap that should stay hidden\"]);"
      "function hydrate(){return document.body;}</div>"
      "</body></html>";
  static const char css_body[] = "h1{color:#112233;} body{background-color:#010203;}";
  static const char import_root_css_body[] =
      "@import url(\"import-theme.css\"); h1{color:#112233;}";
  static const char import_theme_css_body[] = "p{color:#445566;}";
  static const uint8_t png_body[] = {
      0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
      0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1F,0x15,0xC4,
      0x89,0x00,0x00,0x00,0x0D,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0x60,0x60,0x60,0xF8,
      0x0F,0x00,0x01,0x04,0x01,0x00,0x70,0x20,0x65,0x0B,0x00,0x00,0x00,0x00,0x49,0x45,
      0x4E,0x44,0xAE,0x42,0x60,0x82
  };
  static const uint8_t webp_body[] = {
      'R','I','F','F',0x08,0x00,0x00,0x00,'W','E','B','P','V','P','8',' ',
      0x00,0x00,0x00,0x00
  };
  size_t len = 0;
  const char *text_body = NULL;
  if (!req || !resp) return -1;
  memset(resp, 0, sizeof(*resp));
  strncpy(g_last_http_host, req->host, sizeof(g_last_http_host) - 1);
  strncpy(g_last_http_path, req->path, sizeof(g_last_http_path) - 1);
  g_last_http_port = req->port;
  g_last_http_tls = req->use_tls;

  if (strcmp(req->host, "redirect.test") == 0) {
    int status = 302;
    if (strcmp(req->path, "/301") == 0) status = 301;
    else if (strcmp(req->path, "/302") == 0) status = 302;
    else if (strcmp(req->path, "/303") == 0) status = 303;
    else if (strcmp(req->path, "/307") == 0) status = 307;
    else if (strcmp(req->path, "/308") == 0) status = 308;
    resp->status_code = status;
    strcpy(resp->headers[0].name, "Location");
    strcpy(resp->headers[0].value, "https://example.com/landing");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "loop.test") == 0) {
    resp->status_code = 302;
    strcpy(resp->headers[0].name, "Location");
    if (strcmp(req->path, "/1") == 0)
      strcpy(resp->headers[0].value, "https://loop.test/2");
    else if (strcmp(req->path, "/2") == 0)
      strcpy(resp->headers[0].value, "https://loop.test/3");
    else if (strcmp(req->path, "/3") == 0)
      strcpy(resp->headers[0].value, "https://loop.test/4");
    else if (strcmp(req->path, "/4") == 0)
      strcpy(resp->headers[0].value, "https://loop.test/5");
    else
      strcpy(resp->headers[0].value, "https://loop.test/6");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "assets.test") == 0 && strcmp(req->path, "/site.css") == 0) {
    len = strlen(css_body);
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len + 1);
    if (!resp->body) return -1;
    memcpy(resp->body, css_body, len + 1);
    resp->body_len = len;
    resp->content_length = len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "text/css");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "assets.test") == 0 && strcmp(req->path, "/import-root.css") == 0) {
    len = strlen(import_root_css_body);
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len + 1);
    if (!resp->body) return -1;
    memcpy(resp->body, import_root_css_body, len + 1);
    resp->body_len = len;
    resp->content_length = len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "text/css");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "assets.test") == 0 && strcmp(req->path, "/import-theme.css") == 0) {
    len = strlen(import_theme_css_body);
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len + 1);
    if (!resp->body) return -1;
    memcpy(resp->body, import_theme_css_body, len + 1);
    resp->body_len = len;
    resp->content_length = len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "text/css");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "cdnbase.test") == 0 && strcmp(req->path, "/assets/site.css") == 0) {
    len = strlen(css_body);
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len + 1);
    if (!resp->body) return -1;
    memcpy(resp->body, css_body, len + 1);
    resp->body_len = len;
    resp->content_length = len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "text/css");
    resp->header_count = 1;
    return 0;
  }

  if ((strcmp(req->host, "assets.test") == 0 && strcmp(req->path, "/pixel.png") == 0) ||
      (strcmp(req->host, "cdnbase.test") == 0 && strcmp(req->path, "/assets/pixel.png") == 0)) {
    len = sizeof(png_body);
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len);
    if (!resp->body) return -1;
    memcpy(resp->body, png_body, len);
    resp->body_len = len;
    resp->content_length = len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "image/png");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "assets.test") == 0 && strcmp(req->path, "/modern.webp") == 0) {
    len = sizeof(webp_body);
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len);
    if (!resp->body) return -1;
    memcpy(resp->body, webp_body, len);
    resp->body_len = len;
    resp->content_length = len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "image/webp");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "heavy.test") == 0) {
    len = 300000;
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len + 32);
    if (!resp->body) return -1;
    memcpy(resp->body, "<html><body>", 12);
    memset(resp->body + 12, 'A', len);
    memcpy(resp->body + 12 + len, "</body></html>", 15);
    resp->body_len = len + 27;
    resp->content_length = resp->body_len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "text/html; charset=utf-8");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "script.test") == 0) {
    len = strlen(script_body);
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len + 1);
    if (!resp->body) return -1;
    memcpy(resp->body, script_body, len + 1);
    resp->body_len = len;
    resp->content_length = len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "application/javascript");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "noisy.test") == 0) {
    len = strlen(noisy_body);
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len + 1);
    if (!resp->body) return -1;
    memcpy(resp->body, noisy_body, len + 1);
    resp->body_len = len;
    resp->content_length = len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "text/plain; charset=utf-8");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "shell.test") == 0) {
    len = strlen(shell_body);
    resp->status_code = 200;
    resp->body = (uint8_t *)malloc(len + 1);
    if (!resp->body) return -1;
    memcpy(resp->body, shell_body, len + 1);
    resp->body_len = len;
    resp->content_length = len;
    strcpy(resp->headers[0].name, "Content-Type");
    strcpy(resp->headers[0].value, "text/html; charset=utf-8");
    resp->header_count = 1;
    return 0;
  }

  if (strcmp(req->host, "stale.test") == 0 && strcmp(req->path, "/old") == 0) {
    if (g_reentrant_app && !g_reentrant_triggered) {
      g_reentrant_triggered = 1;
      html_viewer_navigate(g_reentrant_app, "https://example.com/fresh");
    }
    text_body = "<html><head><title>Old Example</title></head>"
                "<body><h1>Old Example</h1></body></html>";
  } else if (strcmp(req->host, "example.com") == 0 && strcmp(req->path, "/landing") == 0) {
    text_body = landing_body;
  } else if (strcmp(req->host, "example.com") == 0 && strcmp(req->path, "/assets") == 0) {
    text_body = asset_body;
  } else if (strcmp(req->host, "example.com") == 0 && strcmp(req->path, "/imported-css") == 0) {
    text_body = imported_css_body;
  } else if (strcmp(req->host, "example.com") == 0 && strcmp(req->path, "/style-import") == 0) {
    text_body = style_import_body;
  } else if (strcmp(req->host, "example.com") == 0 && strcmp(req->path, "/base") == 0) {
    text_body = base_body;
  } else if (strcmp(req->host, "example.com") == 0 && strcmp(req->path, "/fresh") == 0) {
    text_body = fresh_body;
  } else if (strcmp(req->host, "example.com") == 0 && strcmp(req->path, "/webp") == 0) {
    text_body = webp_page_body;
  } else if (strcmp(req->host, "meta.test") == 0 && strcmp(req->path, "/start") == 0) {
    text_body = meta_body;
  } else if (strcmp(req->host, "meta.test") == 0 && strcmp(req->path, "/landing") == 0) {
    text_body = meta_landing_body;
  } else if (strcmp(req->host, "example.com") == 0) {
    text_body = body;
  } else {
    return -1;
  }

  len = strlen(text_body);
  resp->status_code = 200;
  resp->body = (uint8_t *)malloc(len + 1);
  if (!resp->body) return -1;
  memcpy(resp->body, text_body, len + 1);
  resp->body_len = len;
  resp->content_length = len;
  strcpy(resp->headers[0].name, "Content-Type");
  strcpy(resp->headers[0].value, "text/html; charset=utf-8");
  resp->header_count = 1;
  return 0;
}
int http_get(const char *url, struct http_response *resp) {
  (void)url;
  (void)resp;
  return -1;
}
int http_download(const char *url, uint8_t *buffer, size_t buffer_size,
                  size_t *out_len) {
  (void)url;
  (void)buffer;
  (void)buffer_size;
  (void)out_len;
  return -1;
}
int http_parse_url(const char *url, char *host, size_t host_len,
                   char *path, size_t path_len, uint16_t *port, int *use_tls) {
  const char *cursor = url;
  size_t host_i = 0;
  size_t path_i = 0;
  if (!url || !host || !path || !port || !use_tls || host_len == 0 || path_len == 0) {
    return -1;
  }
  if (strncmp(cursor, "https://", 8) == 0) {
    *use_tls = 1;
    *port = 443;
    cursor += 8;
  } else if (strncmp(cursor, "http://", 7) == 0) {
    *use_tls = 0;
    *port = 80;
    cursor += 7;
  } else {
    return -1;
  }
  while (*cursor && *cursor != '/' && *cursor != ':' && host_i + 1 < host_len) {
    host[host_i++] = *cursor++;
  }
  host[host_i] = '\0';
  if (*cursor == ':') {
    uint16_t parsed_port = 0;
    cursor++;
    while (*cursor >= '0' && *cursor <= '9') {
      parsed_port = (uint16_t)(parsed_port * 10 + (uint16_t)(*cursor - '0'));
      cursor++;
    }
    if (parsed_port != 0) *port = parsed_port;
  }
  if (*cursor == '\0') {
    if (path_len < 2) return -1;
    path[0] = '/';
    path[1] = '\0';
    return 0;
  }
  while (cursor[path_i] && path_i + 1 < path_len) {
    path[path_i] = cursor[path_i];
    path_i++;
  }
  path[path_i] = '\0';
  return host[0] ? 0 : -1;
}
void http_response_free(struct http_response *resp) {
  if (!resp) return;
  free(resp->body);
  resp->body = NULL;
  resp->body_len = 0;
}
int http_last_error(void) { return 0; }
const char *http_error_string(int error) {
  (void)error;
  return "ok";
}

