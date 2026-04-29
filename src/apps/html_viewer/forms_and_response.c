#include "internal/html_viewer_internal.h"

int html_viewer_issue_request(struct html_viewer_app *app, const char *url,
                              enum http_method method,
                              const uint8_t *body, size_t body_len,
                              struct http_request *req,
                              struct http_response *resp) {
  static char cookie_header[1024];
  if (!app || !url || !req || !resp) return -1;
  kmemzero(req, sizeof(*req));
  req->method = method;
  req->body = body;
  req->body_len = body_len;
  req->timeout_ms = 8000;
  if (http_parse_url(url, req->host, sizeof(req->host), req->path,
                     sizeof(req->path), &req->port, &req->use_tls) != 0) {
    return -1;
  }
  if (hv_build_cookie_header(app, req->host, req->path, req->use_tls,
                             cookie_header, sizeof(cookie_header)) &&
      req->header_count < HTTP_MAX_HEADERS) {
    kstrcpy(req->headers[req->header_count].name,
            sizeof(req->headers[req->header_count].name), "Cookie");
    kstrcpy(req->headers[req->header_count].value,
            sizeof(req->headers[req->header_count].value), cookie_header);
    req->header_count++;
  }
  if (body && body_len > 0 && req->header_count < HTTP_MAX_HEADERS) {
    kstrcpy(req->headers[req->header_count].name,
            sizeof(req->headers[req->header_count].name), "Content-Type");
    kstrcpy(req->headers[req->header_count].value,
            sizeof(req->headers[req->header_count].value),
            "application/x-www-form-urlencoded");
    req->header_count++;
  }
  return http_request(req, resp);
}

static void hv_form_append_pair(char *out, size_t out_len,
                                const char *name, const char *value,
                                int *wrote_any) {
  if (!out || out_len == 0 || !name || !name[0] || !wrote_any) return;
  if (*wrote_any) kbuf_append(out, out_len, "&");
  hv_urlencode_append(out, out_len, name);
  kbuf_append(out, out_len, "=");
  hv_urlencode_append(out, out_len, value ? value : "");
  *wrote_any = 1;
}

int hv_extract_query_param(const char *url, const char *param,
                           char *out, size_t out_len) {
  const char *q;
  size_t plen;
  if (!url || !param || !out || out_len == 0) return 0;
  out[0] = '\0';
  plen = kstrlen(param);
  q = url;
  while (*q && *q != '?') q++;
  if (!*q) return 0;
  q++;
  while (*q) {
    size_t i = 0;
    while (i < plen && q[i] && q[i] == param[i]) i++;
    if (i == plen && q[i] == '=') {
      const char *v = q + i + 1;
      size_t n = 0;
      while (v[n] && v[n] != '&' && n + 1 < out_len) n++;
      kmemcpy(out, v, n);
      out[n] = '\0';
      return 1;
    }
    while (*q && *q != '&') q++;
    if (*q == '&') q++;
  }
  return 0;
}

void hv_url_decode(char *s) {
  char *r = s;
  char *w = s;
  if (!s) return;
  while (*r) {
    if (*r == '%' && r[1] && r[2]) {
      char hi = r[1];
      char lo = r[2];
      int h = (hi >= '0' && hi <= '9') ? hi - '0'
              : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10
              : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10
                                         : -1;
      int l = (lo >= '0' && lo <= '9') ? lo - '0'
              : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10
              : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10
                                         : -1;
      if (h >= 0 && l >= 0) {
        *w++ = (char)(h * 16 + l);
        r += 3;
        continue;
      }
    } else if (*r == '+') {
      *w++ = ' ';
      r++;
      continue;
    }
    *w++ = *r++;
  }
  *w = '\0';
}

static int html_viewer_build_form_payload(struct html_viewer_app *app,
                                          const struct html_node *submit_node,
                                          char *payload,
                                          size_t payload_len) {
  int wrote_any = 0;
  if (!app || !submit_node || !payload || payload_len == 0) return 0;
  payload[0] = '\0';
  for (int i = 0; i < app->doc.node_count; i++) {
    const struct html_node *node = &app->doc.nodes[i];
    if (!hv_form_action_matches(node, submit_node) ||
        node->form_method != submit_node->form_method) {
      continue;
    }
    if (node->type == HTML_NODE_TAG_INPUT) {
      if (node->input_type == HTML_INPUT_TYPE_CHECKBOX ||
          node->input_type == HTML_INPUT_TYPE_RADIO) {
        if (node->open) {
          hv_form_append_pair(payload, payload_len, node->name, node->text,
                              &wrote_any);
        }
      } else if (node->input_type != HTML_INPUT_TYPE_HIDDEN || node->name[0]) {
        hv_form_append_pair(payload, payload_len, node->name, node->text,
                            &wrote_any);
      }
    } else if (node->type == HTML_NODE_TAG_BUTTON && node == submit_node) {
      hv_form_append_pair(payload, payload_len, node->name, node->text,
                          &wrote_any);
    }
  }
  return wrote_any;
}

void html_viewer_submit_form(struct html_viewer_app *app, int node_index) {
  char action[HTML_URL_MAX];
  char resolved_action[HTML_URL_MAX];
  char payload[HTML_URL_MAX];
  char target[HTML_URL_MAX];
  struct html_node *node;
  int has_payload;
  if (!app || node_index < 0 || node_index >= app->doc.node_count) return;
  node = &app->doc.nodes[node_index];
  if (node->type != HTML_NODE_TAG_INPUT && node->type != HTML_NODE_TAG_BUTTON) {
    return;
  }
  if (node->type == HTML_NODE_TAG_BUTTON &&
      node->input_type == HTML_INPUT_TYPE_BUTTON) {
    return;
  }

  if (node->href[0]) {
    kstrcpy(action, sizeof(action), node->href);
  } else {
    kstrcpy(action, sizeof(action), app->url);
  }

  if (html_viewer_resolve_document_url(app, action, resolved_action,
                                       sizeof(resolved_action)) != 0) {
    kstrcpy(resolved_action, sizeof(resolved_action), action);
  }

  has_payload =
      html_viewer_build_form_payload(app, node, payload, sizeof(payload));
  if (node->form_method == HTML_FORM_METHOD_POST) {
    html_viewer_request_internal(
        app, resolved_action, HTTP_POST,
        has_payload ? (const uint8_t *)payload : NULL,
        has_payload ? kstrlen(payload) : 0, 0);
    return;
  }

  kstrcpy(target, sizeof(target), resolved_action);
  if (has_payload) {
    kbuf_append(target, sizeof(target),
                hv_contains_ci(resolved_action, "?") ? "&" : "?");
    kbuf_append(target, sizeof(target), payload);
  }
  html_viewer_request_internal(app, target, HTTP_GET, NULL, 0, 0);
}

void html_viewer_capture_cookies(struct html_viewer_app *app,
                                 const struct http_request *req,
                                 const struct http_response *resp) {
  if (!app || !req || !resp) return;
  for (uint32_t i = 0; i < resp->header_count; i++) {
    if (hv_streq_ci(resp->headers[i].name, "Set-Cookie")) {
      hv_store_cookie_from_header(app, req->host, req->path, req->use_tls,
                                  resp->headers[i].value);
    }
  }
}

void html_viewer_load_text_document(struct html_viewer_app *app,
                                    const char *title,
                                    const char *text,
                                    size_t len,
                                    uint32_t color) {
  size_t pos = 0;
  if (!app) return;
  hv_doc_reset(&app->doc);
  kstrcpy(app->doc.title, sizeof(app->doc.title),
          title && title[0] ? title : "CapyBrowser");
  while (pos < len && app->doc.node_count < HTML_MAX_NODES) {
    struct html_node *node = html_push_node(&app->doc);
    size_t out_pos = 0;
    int last_space = 1;
    if (!node) break;
    node->type = HTML_NODE_TEXT;
    node->color = color;
    while (pos < len && text[pos] != '\n') {
      hv_text_append_char(node->text, sizeof(node->text), &out_pos, text[pos],
                          &last_space);
      pos++;
    }
    hv_trim_text(node->text);
    if (!node->text[0]) app->doc.node_count--;
    if (text[pos] == '\n' && app->doc.node_count < HTML_MAX_NODES) {
      struct html_node *br = html_push_node(&app->doc);
      if (br) br->type = HTML_NODE_TAG_BR;
      pos++;
    }
  }
  if (app->doc.node_count == 0) {
    html_viewer_set_error_context(app, "render", "Empty response.");
    html_viewer_set_error(app, title, "Empty response.");
  }
}

static int hv_doc_visible_text_count(const struct html_document *doc) {
  int visible = 0;
  if (!doc) return 0;
  for (int i = 0; i < doc->node_count; i++) {
    const struct html_node *node = &doc->nodes[i];
    if (!node->hidden && node->text[0]) visible++;
  }
  return visible;
}

static void html_viewer_load_degraded_document(struct html_viewer_app *app,
                                               const char *title,
                                               const char *message) {
  struct html_node *node = NULL;
  if (!app) return;
  hv_doc_reset(&app->doc);
  kstrcpy(app->doc.title, sizeof(app->doc.title),
          title && title[0] ? title : "Limited Page");
  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_H1;
    node->bold = 1;
    node->font_size = 24;
    node->css_color = 0xF9E2AF;
    kstrcpy(node->text, sizeof(node->text), app->doc.title);
  }
  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_P;
    node->color = 0xCDD6F4;
    kstrcpy(node->text, sizeof(node->text),
            message && message[0]
                ? message
                : "This page did not expose enough static HTML for the "
                  "built-in browser.");
  }
  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_P;
    node->color = 0xA6ADC8;
    kstrcpy(node->text, sizeof(node->text),
            "CapyBrowser kept the desktop responsive and avoided rendering "
            "script/data as page text.");
  }
}

void html_viewer_apply_response(
    struct html_viewer_app *app, const struct http_request *req,
    const struct http_response *resp,
    const struct html_viewer_apply_options *opts) {
  const char *content_type = html_viewer_find_header(resp, "Content-Type");
  struct html_viewer_apply_options local_opts = {1, 1, 1, 1};
  if (opts) local_opts = *opts;
  if (!app || !req || !resp) return;
  html_viewer_capture_cookies(app, req, resp);
  if (resp->body && resp->body_len > 0 &&
      (hv_content_type_is_html(content_type) ||
       hv_body_looks_html(resp->body, resp->body_len))) {
    size_t html_start = hv_find_html_start(resp->body, resp->body_len);
    const char *html_body = (const char *)resp->body;
    size_t html_len = resp->body_len;
    if (html_start < resp->body_len) {
      html_body += html_start;
      html_len -= html_start;
      if (html_start > 0) app->safe_mode = 1;
    }
    if (html_len > HV_HTML_BODY_LIMIT) {
      app->safe_mode = 1;
      html_viewer_set_error_context(app, "parse",
                                    "HTML document exceeds safe browser limit.");
      html_viewer_set_error(app, "Navigation Error",
                            "HTML document exceeds the safe browser limit.");
      return;
    }
    html_viewer_set_state(app, HTML_VIEWER_NAV_RENDERING, "rendering");
    if (local_opts.fetch_external_assets) {
      hv_http_cache_tick();
    }
    hv_parse_locked_with_app(app, html_body, html_len, &app->doc);
    hv_simplify_degraded_doc(&app->doc);
    if (app->doc.node_count == 0 || hv_doc_visible_text_count(&app->doc) == 0) {
      char detected_title[HTML_TITLE_MAX];
      detected_title[0] = '\0';
      if (app->doc.title[0]) {
        kstrcpy(detected_title, sizeof(detected_title), app->doc.title);
      }
      app->safe_mode = 1;
      html_viewer_set_error_context(
          app, "render",
          "Document has no visible static HTML after script/data filtering.");
      html_viewer_load_degraded_document(
          app, detected_title[0] ? detected_title : "Limited Page",
          "This page appears to rely on JavaScript or hidden bootstrap data. "
          "The current browser can show static HTML fallbacks, but it will "
          "not render raw script.");
    }
    if (local_opts.update_window_title && app->doc.title[0] && app->window) {
      compositor_set_title(app->window->id, app->doc.title);
    }
    if (local_opts.push_history) {
      hv_history_push(app->url);
    }
    if (local_opts.fetch_external_assets) {
      hv_prefetch_dns(app);
      if (app->doc.style_text[0]) {
        hv_queue_css_imports(app, app->url, (const uint8_t *)app->doc.style_text,
                             kstrlen(app->doc.style_text));
      }
      hv_fetch_external_css(app);
    }
    if (local_opts.decode_inline_images || local_opts.fetch_external_assets) {
      hv_fetch_page_images(app, local_opts.fetch_external_assets ? 1 : 0);
    }
    return;
  }
  if (resp->body && resp->body_len > 0 &&
      (hv_content_type_is_script_or_data(content_type) ||
       hv_body_looks_script_or_data(resp->body, resp->body_len))) {
    app->safe_mode = 1;
    html_viewer_set_error_context(
        app, "render",
        "Script/data response cannot be rendered as a document.");
    html_viewer_set_error(app, "Navigation Error",
                          "Script/data response cannot be rendered as a "
                          "document.");
    return;
  }
  if (resp->body && resp->body_len > 0 &&
      (hv_content_type_is_textual(content_type) ||
       hv_body_looks_textual(resp->body, resp->body_len))) {
    if (resp->body_len > HV_TEXT_BODY_LIMIT) {
      app->safe_mode = 1;
      html_viewer_set_error_context(app, "render",
                                    "Text document exceeds safe browser limit.");
      html_viewer_set_error(app, "Navigation Error",
                            "Text document exceeds the safe browser limit.");
      return;
    }
    html_viewer_load_text_document(
        app, content_type ? content_type : "Document",
        (const char *)resp->body, resp->body_len, 0xCDD6F4);
    html_viewer_set_state(app, HTML_VIEWER_NAV_RENDERING, "rendering");
    return;
  }
  if (resp->body && resp->body_len > 0) {
    html_viewer_set_error_context(
        app, "render",
        "Unsupported content type for the built-in renderer.");
    html_viewer_set_error(app, "Navigation Error",
                          "Unsupported content type for the built-in renderer.");
    return;
  }
  if (resp->status_code != 200) {
    html_viewer_set_status_error(app, resp->status_code);
    return;
  }
  html_viewer_set_transport_error(app);
}
