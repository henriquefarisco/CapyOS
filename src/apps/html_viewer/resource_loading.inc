void html_viewer_request_sync(struct html_viewer_app *app,
                              const char *url,
                              enum http_method method,
                              const uint8_t *body,
                              size_t body_len,
                              int depth,
                              uint32_t navigation_id) {
  char normalized_url[HTML_URL_MAX];
  char redirect_url[HTML_URL_MAX];
  char meta_refresh_url[HTML_URL_MAX];
  const char *target = url;
  const char *location = NULL;
  int rc = -1;
  static struct http_request req_stack[6];
  static struct http_response resp_stack[6];
  int frame = depth;
  struct http_request *req = NULL;
  struct http_response *resp = NULL;
  if (frame < 0) frame = 0;
  if (frame > 5) frame = 5;
  req = &req_stack[frame];
  resp = &resp_stack[frame];
  if (!app || !url) return;
  if (!html_viewer_navigation_is_current(app, navigation_id)) return;
  if (!url[0]) {
    html_viewer_load_builtin(app, "about:home");
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  /* about:navigate?url=... — navigate to the URL query param */
  if (hv_strncmp(url, "about:navigate", 14) == 0) {
    char nav_target[HTML_URL_MAX];
    nav_target[0] = '\0';
    hv_extract_query_param(url, "url", nav_target, sizeof(nav_target));
    hv_url_decode(nav_target);
    if (nav_target[0]) {
      html_viewer_request_sync(app, nav_target, HTTP_GET, NULL, 0, depth + 1,
                               navigation_id);
    } else {
      html_viewer_load_builtin(app, "about:home");
      if (app->window) compositor_invalidate(app->window->id);
    }
    return;
  }
  if (hv_strncmp(url, "about:home", 10) == 0 ||
      hv_strncmp(url, "about:newtab", 12) == 0 ||
      hv_strncmp(url, "about:version", 13) == 0 ||
      hv_strncmp(url, "about:blank", 11) == 0 ||
      hv_strncmp(url, "about:history", 13) == 0 ||
      hv_strncmp(url, "about:bookmarks", 15) == 0 ||
      hv_strncmp(url, "about:settings", 14) == 0) {
    html_viewer_load_builtin(app, url);
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  if (hv_strncmp(url, "about:", 6) == 0) {
    kstrcpy(app->url, sizeof(app->url), url);
    html_viewer_set_error_context(app, "navigate", "Unknown about page.");
    html_viewer_set_error(app, "Navigation Error", "Unknown about page.");
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  if (hv_has_scheme(url) &&
      hv_strncmp(url, "http://", 7) != 0 &&
      hv_strncmp(url, "https://", 8) != 0) {
    kstrcpy(app->url, sizeof(app->url), url);
    html_viewer_set_error_context(app, "navigate", "Unsupported URL scheme.");
    html_viewer_set_error(app, "Navigation Error", "Unsupported URL scheme.");
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  if (hv_strncmp(url, "http://", 7) != 0 &&
      hv_strncmp(url, "https://", 8) != 0) {
    kstrcpy(normalized_url, sizeof(normalized_url), "https://");
    kbuf_append(normalized_url, sizeof(normalized_url), url);
    target = normalized_url;
  }

  kstrcpy(app->url, sizeof(app->url), target);
  kstrcpy(app->final_url, sizeof(app->final_url), target);
  app->scroll_offset = 0;
  app->focused_node_index = -1;
  html_viewer_set_state(app, HTML_VIEWER_NAV_LOADING, "loading");
  /* Leave both the paint and present to the desktop/compositor frame loop. */
  if (app->window) {
    compositor_invalidate(app->window->id);
  }
  kmemzero(resp, sizeof(*resp));
  rc = html_viewer_issue_request(app, target, method, body, body_len, req, resp);
  if (!html_viewer_navigation_is_current(app, navigation_id)) {
    http_response_free(resp);
    return;
  }
  if (rc != 0) {
    html_viewer_set_transport_error(app);
  } else if (resp->status_code >= 300 && resp->status_code < 400) {
    int redirect_method = method;
    const uint8_t *redirect_body = redirect_method == HTTP_GET ? NULL : body;
    size_t redirect_body_len = redirect_method == HTTP_GET ? 0 : body_len;
    if (resp->status_code == 303) {
      redirect_method = HTTP_GET;
      redirect_body = NULL;
      redirect_body_len = 0;
    } else if ((resp->status_code == 301 || resp->status_code == 302) &&
               method != HTTP_GET && method != HTTP_HEAD) {
      redirect_method = HTTP_GET;
      redirect_body = NULL;
      redirect_body_len = 0;
    }
    html_viewer_capture_cookies(app, req, resp);
    location = html_viewer_find_header(resp, "Location");
    if (location) {
      app->redirect_count = (uint8_t)(depth + 1);
      html_viewer_set_state(app, HTML_VIEWER_NAV_REDIRECTING, "redirect");
    }
    if (location && depth >= HV_NAV_REDIRECT_LIMIT - 1) {
      html_viewer_set_error_context(app, "redirect", "Too many redirects.");
      html_viewer_set_error(app, "Navigation Error", "Too many redirects.");
    } else if (location &&
               hv_resolve_url(target, location, redirect_url, sizeof(redirect_url)) == 0 &&
               !kstreq(redirect_url, target)) {
      http_response_free(resp);
      html_viewer_request_sync(app, redirect_url, redirect_method,
                               redirect_body, redirect_body_len, depth + 1,
                               navigation_id);
      return;
    } else {
  if (app->background_mode) {
      struct html_viewer_apply_options bg_opts = {0, 0, 0, 1};
      html_viewer_apply_response(app, req, resp, &bg_opts);
      } else {
        html_viewer_apply_response(app, req, resp, NULL);
      }
    }
  } else {
    if (app->background_mode) {
      struct html_viewer_apply_options bg_opts = {0, 0, 0, 1};
      html_viewer_apply_response(app, req, resp, &bg_opts);
    } else {
      html_viewer_apply_response(app, req, resp, NULL);
    }
  }

  meta_refresh_url[0] = '\0';
  if (html_viewer_navigation_is_current(app, navigation_id) &&
      app->nav_state != HTML_VIEWER_NAV_FAILED &&
      app->nav_state != HTML_VIEWER_NAV_CANCELLED &&
      app->doc.meta_refresh_url[0] &&
      app->doc.meta_refresh_delay <= HV_META_REFRESH_AUTO_LIMIT_SECONDS) {
    if (depth >= HV_NAV_REDIRECT_LIMIT - 1) {
      html_viewer_set_error_context(app, "redirect", "Too many redirects.");
      html_viewer_set_error(app, "Navigation Error", "Too many redirects.");
    } else if (html_viewer_resolve_document_url(app, app->doc.meta_refresh_url,
                                                meta_refresh_url,
                                                sizeof(meta_refresh_url)) == 0 &&
               !kstreq(meta_refresh_url, target)) {
      app->redirect_count = (uint8_t)(depth + 1);
      html_viewer_set_state(app, HTML_VIEWER_NAV_REDIRECTING, "redirect");
      http_response_free(resp);
      html_viewer_request_sync(app, meta_refresh_url, HTTP_GET, NULL, 0,
                               depth + 1, navigation_id);
      return;
    }
  }

  if (app->doc.title[0]) {
    if (app->window) compositor_set_title(app->window->id, app->doc.title);
  } else if (app->window) {
    compositor_set_title(app->window->id, app->url);
  }
  http_response_free(resp);
  if (!html_viewer_navigation_is_current(app, navigation_id)) {
    return;
  }
  if (app->nav_state != HTML_VIEWER_NAV_FAILED &&
      app->nav_state != HTML_VIEWER_NAV_CANCELLED) {
    html_viewer_set_state(app, HTML_VIEWER_NAV_READY, "ready");
  }
  if (app->window) compositor_invalidate(app->window->id);
}

void html_viewer_request_internal(struct html_viewer_app *app,
                                  const char *url,
                                  enum http_method method,
                                  const uint8_t *body,
                                  size_t body_len,
                                  int depth) {
  if (!app || !url) return;
  if (depth != 0) {
    html_viewer_request_sync(app, url, method, body, body_len, depth,
                             app->active_navigation_id);
    return;
  }
  if (hv_strncmp(url, "about:navigate", 14) == 0) {
    char nav_target[HTML_URL_MAX];
    nav_target[0] = '\0';
    hv_extract_query_param(url, "url", nav_target, sizeof(nav_target));
    hv_url_decode(nav_target);
    if (nav_target[0]) {
      html_viewer_request_internal(app, nav_target, HTTP_GET, NULL, 0, 0);
    } else {
      html_viewer_background_cancel();
      html_viewer_request_sync(app, "about:home", HTTP_GET, NULL, 0, 0,
                               app->active_navigation_id);
    }
    return;
  }
  html_viewer_begin_navigation(app, url);
  if (!url[0] || hv_strncmp(url, "about:", 6) == 0 ||
      (hv_has_scheme(url) &&
       hv_strncmp(url, "http://", 7) != 0 &&
       hv_strncmp(url, "https://", 8) != 0)) {
    html_viewer_background_cancel();
    html_viewer_request_sync(app, url, method, body, body_len, 0,
                             app->active_navigation_id);
    return;
  }
  if (html_viewer_queue_async_request(app, url, method, body, body_len) == 0) {
    return;
  }
  html_viewer_request_sync(app, url, method, body, body_len, 0,
                           app->active_navigation_id);
}

static int hv_doc_has_pending_css(const struct html_document *doc,
                                  const char *url) {
  if (!doc || !url || !url[0]) return 0;
  for (int i = 0; i < doc->css_count; i++) {
    if (kstreq(doc->pending_css[i], url)) return 1;
  }
  return 0;
}

static int hv_doc_queue_pending_css(struct html_document *doc,
                                    const char *url) {
  if (!doc || !url || !url[0]) return 0;
  if (hv_doc_has_pending_css(doc, url)) return 1;
  if (doc->css_count >= HTML_MAX_PENDING_CSS) return 0;
  kstrcpy(doc->pending_css[doc->css_count++], HTML_URL_MAX, url);
  return 1;
}

void hv_queue_css_imports(struct html_viewer_app *app,
                          const char *css_url,
                          const uint8_t *body,
                          size_t body_len) {
  size_t pos = 0;
  if (!app || !css_url || !body || body_len == 0) return;
  while (pos < body_len && app->doc.css_count < HTML_MAX_PENDING_CSS) {
    static char import_ref[HTML_URL_MAX];
    static char resolved[HTML_URL_MAX];
    size_t out = 0;
    if (!hv_body_match_ci_at(body, body_len, pos, "@import")) {
      pos++;
      continue;
    }
    pos += 7;
    while (pos < body_len && hv_is_space((char)body[pos])) pos++;
    import_ref[0] = '\0';
    if (hv_body_match_ci_at(body, body_len, pos, "url(")) {
      pos += 4;
      while (pos < body_len && hv_is_space((char)body[pos])) pos++;
      if (pos < body_len && (body[pos] == '"' || body[pos] == '\'')) {
        uint8_t quote = body[pos++];
        while (pos < body_len && body[pos] != quote &&
               out + 1 < sizeof(import_ref)) {
          import_ref[out++] = (char)body[pos++];
        }
        while (pos < body_len && body[pos] != ')') pos++;
        if (pos < body_len) pos++;
      } else {
        while (pos < body_len && body[pos] != ')' &&
               !hv_is_space((char)body[pos]) &&
               out + 1 < sizeof(import_ref)) {
          import_ref[out++] = (char)body[pos++];
        }
        while (pos < body_len && body[pos] != ')') pos++;
        if (pos < body_len) pos++;
      }
    } else if (pos < body_len && (body[pos] == '"' || body[pos] == '\'')) {
      uint8_t quote = body[pos++];
      while (pos < body_len && body[pos] != quote &&
             out + 1 < sizeof(import_ref)) {
        import_ref[out++] = (char)body[pos++];
      }
      if (pos < body_len) pos++;
    }
    import_ref[out] = '\0';
    hv_trim_text(import_ref);
    if (import_ref[0] &&
        hv_resolve_url(css_url, import_ref, resolved, sizeof(resolved)) == 0 &&
        !hv_doc_queue_pending_css(&app->doc, resolved)) {
      app->safe_mode = 1;
      break;
    }
  }
  if (pos < body_len && app->doc.css_count >= HTML_MAX_PENDING_CSS) {
    app->safe_mode = 1;
  }
}

void hv_fetch_external_css(struct html_viewer_app *app) {
  static char abs_url[HTML_URL_MAX];
  static struct css_stylesheet sheet;
  static struct http_request req;
  static struct http_response resp;
  int i;
  if (!app || !app->doc.css_count) return;
  for (i = 0; i < app->doc.css_count && i < HV_EXTERNAL_CSS_LIMIT; i++) {
    struct hv_http_cache_entry *cached;
    if (html_viewer_resolve_document_url(app, app->doc.pending_css[i], abs_url,
                                         sizeof(abs_url)) != 0)
      kstrcpy(abs_url, sizeof(abs_url), app->doc.pending_css[i]);
    cached = hv_http_cache_find(abs_url);
    if (cached) {
      if (cached->body_len > HV_TEXT_BODY_LIMIT) {
        app->safe_mode = 1;
        continue;
      }
      kmemzero(&sheet, sizeof(sheet));
      hv_queue_css_imports(app, abs_url, cached->body, cached->body_len);
      if (css_parse((const char *)cached->body, cached->body_len, &sheet) == 0) {
        css_apply_to_doc(&sheet, &app->doc);
        app->external_css_loaded++;
        if ((uint32_t)app->external_css_loaded + (uint32_t)app->external_images_loaded >=
            HV_EXTERNAL_FETCH_LIMIT) {
          app->safe_mode = 1;
          break;
        }
      }
    } else {
      kmemzero(&req, sizeof(req));
      kmemzero(&resp, sizeof(resp));
      if (html_viewer_issue_request(app, abs_url, HTTP_GET, NULL, 0, &req, &resp) == 0) {
        if (resp.body && resp.body_len > 0 && resp.body_len <= HV_TEXT_BODY_LIMIT) {
          hv_http_cache_store(abs_url, &resp);
          kmemzero(&sheet, sizeof(sheet));
          hv_queue_css_imports(app, abs_url, resp.body, resp.body_len);
          if (css_parse((const char *)resp.body, resp.body_len, &sheet) == 0) {
            css_apply_to_doc(&sheet, &app->doc);
            app->external_css_loaded++;
            if ((uint32_t)app->external_css_loaded + (uint32_t)app->external_images_loaded >=
                HV_EXTERNAL_FETCH_LIMIT) {
              app->safe_mode = 1;
            }
          }
        } else if (resp.body_len > HV_TEXT_BODY_LIMIT) {
          app->safe_mode = 1;
        }
      }
      http_response_free(&resp);
    }
  }
  if (app->doc.css_count > HV_EXTERNAL_CSS_LIMIT) {
    app->safe_mode = 1;
  }
}

/* Prefetch DNS for unique external hosts found in links and image srcs.
 * Warms the DNS cache before CSS and image fetches to reduce per-request latency. */
void hv_prefetch_dns(struct html_viewer_app *app) {
#define HV_PREFETCH_MAX 12
  static char seen[HV_PREFETCH_MAX][HTTP_MAX_HOST];
  int seen_count = 0;
  int i;
  if (!app || app->safe_mode || app->doc.node_count > (HTML_MAX_NODES * 3) / 4) return;
  for (i = 0; i < app->doc.node_count && seen_count < HV_PREFETCH_MAX; i++) {
    struct html_node *node = &app->doc.nodes[i];
    const char *url = NULL;
    static char host[HTTP_MAX_HOST];
    uint32_t cached_ip = 0;
    int j;
    if (node->href[0]) url = node->href;
    if (!url || !url[0]) continue;
    /* Only prefetch http/https URLs */
    if (hv_strncmp(url, "http://", 7) != 0 && hv_strncmp(url, "https://", 8) != 0)
      continue;
    /* Extract hostname */
    {
      const char *p = url + (url[7] == '/' ? 8 : 7);  /* skip scheme:// */
      size_t hi = 0;
      while (*p && *p != '/' && *p != ':' && *p != '?' && hi < sizeof(host) - 1)
        host[hi++] = *p++;
      host[hi] = '\0';
    }
    if (!host[0]) continue;
    /* Skip if already seen in this prefetch pass */
    for (j = 0; j < seen_count; j++)
      if (hv_streq_ci(seen[j], host)) break;
    if (j < seen_count) continue;
    /* Skip if already in DNS cache */
    if (dns_cache_lookup(host, &cached_ip) == 0) continue;
    /* Prefetch: resolve with short timeout (500ms) */
    {
      uint32_t ip = 0;
      kstrcpy(seen[seen_count], sizeof(seen[seen_count]), host);
      seen_count++;
      net_stack_dns_resolve(host, 500, &ip);
      /* ip may be 0 on failure; the cache stores it regardless */
    }
  }
#undef HV_PREFETCH_MAX
}

void hv_fetch_page_images(struct html_viewer_app *app, int allow_network) {
  static char abs_url[HTML_URL_MAX];
  static struct png_image img;
  static struct jpeg_image jimg;
  static struct http_request req;
  static struct http_response resp;
  int i;
  if (!app) return;
  for (i = 0; i < app->doc.node_count; i++) {
    struct html_node *node = &app->doc.nodes[i];
    const uint8_t *body = NULL;
    size_t body_len = 0;
    struct hv_http_cache_entry *cached;
    if (node->type != HTML_NODE_TAG_IMG || !node->href[0]) continue;
    if (app->external_images_loaded >= HV_EXTERNAL_IMAGE_LIMIT) {
      app->safe_mode = 1;
      break;
    }
    if (node->image_pixels && node->image_width > 0 && node->image_height > 0) continue;
    if (hv_strncmp(node->href, "data:image/", 11) == 0) {
      kstrcpy(abs_url, sizeof(abs_url), node->href);
    } else {
      if (html_viewer_resolve_document_url(app, node->href, abs_url,
                                           sizeof(abs_url)) != 0)
        kstrcpy(abs_url, sizeof(abs_url), node->href);
      kstrcpy(node->href, sizeof(node->href), abs_url);
    }
    if (hv_strncmp(abs_url, "data:image/", 11) == 0) {
      if (hv_decode_data_image(abs_url, &img, &jimg) == 0 &&
          img.pixels && img.width > 0 && img.height > 0) {
        node->image_pixels = img.pixels;
        node->image_width = (uint16_t)img.width;
        node->image_height = (uint16_t)img.height;
      } else if (hv_contains_ci(abs_url, "data:image/webp") ||
                 hv_contains_ci(abs_url, "data:image/avif")) {
        node->image_error = HV_IMAGE_ERROR_UNSUPPORTED;
      } else {
        node->image_error = HV_IMAGE_ERROR_DECODE_FAILED;
      }
      continue;
    }
    if (!allow_network) continue;
    cached = hv_http_cache_find(abs_url);
    if (cached) {
      body = cached->body;
      body_len = cached->body_len;
    } else {
      kmemzero(&req, sizeof(req));
      kmemzero(&resp, sizeof(resp));
      if (html_viewer_issue_request(app, abs_url, HTTP_GET, NULL, 0, &req, &resp) != 0 ||
          !resp.body || resp.body_len < 8) {
        http_response_free(&resp);
        continue;
      }
      if (resp.body_len > HV_IMAGE_BODY_LIMIT) {
        app->safe_mode = 1;
        node->image_error = HV_IMAGE_ERROR_TOO_LARGE;
        http_response_free(&resp);
        continue;
      }
      if (hv_image_type_is_known_unsupported(html_viewer_find_header(&resp, "Content-Type")) ||
          hv_image_body_is_known_unsupported(resp.body, resp.body_len)) {
        node->image_error = HV_IMAGE_ERROR_UNSUPPORTED;
        http_response_free(&resp);
        continue;
      }
      hv_http_cache_store(abs_url, &resp);
      body = resp.body;
      body_len = resp.body_len;
    }
    if (body_len > HV_IMAGE_BODY_LIMIT) {
      app->safe_mode = 1;
      node->image_error = HV_IMAGE_ERROR_TOO_LARGE;
      if (!cached) http_response_free(&resp);
      continue;
    }
    if (hv_image_body_is_known_unsupported(body, body_len)) {
      node->image_error = HV_IMAGE_ERROR_UNSUPPORTED;
      if (!cached) http_response_free(&resp);
      continue;
    }
    kmemzero(&img, sizeof(img));
    if (body_len >= 8) {
      int decoded = 0;
      /* Try PNG first (signature: 0x89 'P' 'N' 'G') */
      if (!decoded && body[0] == 0x89 && body[1] == 0x50) {
        if (png_decode(body, body_len, &img) == 0 &&
            img.pixels && img.width > 0 && img.height > 0) {
          decoded = 1;
        }
      }
      /* Try JPEG (signature: 0xFF 0xD8) */
      if (!decoded && body[0] == 0xFF && body[1] == 0xD8) {
        kmemzero(&jimg, sizeof(jimg));
        if (jpeg_decode(body, body_len, &jimg) == 0 &&
            jimg.pixels && jimg.width > 0 && jimg.height > 0) {
          img.pixels = jimg.pixels;
          img.width  = jimg.width;
          img.height = jimg.height;
          decoded = 1;
        }
      }
      /* Fallback: try PNG regardless of signature */
      if (!decoded) {
        png_decode(body, body_len, &img);
      }
      if (img.pixels && img.width > 0 && img.height > 0) {
        node->image_pixels = img.pixels;
        node->image_width = (uint16_t)img.width;
        node->image_height = (uint16_t)img.height;
        node->image_error = 0;
        app->external_images_loaded++;
        if ((uint32_t)app->external_css_loaded + (uint32_t)app->external_images_loaded >=
            HV_EXTERNAL_FETCH_LIMIT) {
          app->safe_mode = 1;
        }
      } else {
        node->image_error = HV_IMAGE_ERROR_DECODE_FAILED;
      }
    }
    if (!cached) http_response_free(&resp);
  }
}

