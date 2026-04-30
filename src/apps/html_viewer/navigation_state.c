#include "internal/html_viewer_internal.h"

#include "kernel/log/klog.h"

const char *html_viewer_state_name(enum html_viewer_nav_state state) {
  switch (state) {
  case HTML_VIEWER_NAV_IDLE:
    return "idle";
  case HTML_VIEWER_NAV_LOADING:
    return "loading";
  case HTML_VIEWER_NAV_REDIRECTING:
    return "redirecting";
  case HTML_VIEWER_NAV_RENDERING:
    return "rendering";
  case HTML_VIEWER_NAV_READY:
    return "ready";
  case HTML_VIEWER_NAV_FAILED:
    return "failed";
  case HTML_VIEWER_NAV_CANCELLED:
    return "cancelled";
  default:
    return "unknown";
  }
}

int html_viewer_state_transition_allowed(enum html_viewer_nav_state from,
                                         enum html_viewer_nav_state to) {
  /* Cancelled or failed transitions are always allowed (escape hatch). */
  if (to == HTML_VIEWER_NAV_CANCELLED || to == HTML_VIEWER_NAV_FAILED) {
    return 1;
  }
  switch (from) {
  case HTML_VIEWER_NAV_IDLE:
  case HTML_VIEWER_NAV_READY:
  case HTML_VIEWER_NAV_FAILED:
  case HTML_VIEWER_NAV_CANCELLED:
    /* Stable states may start a new navigation. */
    return (to == HTML_VIEWER_NAV_LOADING || to == HTML_VIEWER_NAV_IDLE);
  case HTML_VIEWER_NAV_LOADING:
    return (to == HTML_VIEWER_NAV_REDIRECTING ||
            to == HTML_VIEWER_NAV_RENDERING ||
            to == HTML_VIEWER_NAV_READY);
  case HTML_VIEWER_NAV_REDIRECTING:
    return (to == HTML_VIEWER_NAV_LOADING ||
            to == HTML_VIEWER_NAV_RENDERING ||
            to == HTML_VIEWER_NAV_READY);
  case HTML_VIEWER_NAV_RENDERING:
    return (to == HTML_VIEWER_NAV_READY);
  default:
    return 0;
  }
}

static const struct html_viewer_isolation_ops *g_isolation_ops = NULL;

void html_viewer_set_isolation_ops(const struct html_viewer_isolation_ops *ops) {
  g_isolation_ops = ops;
}

const struct html_viewer_isolation_ops *html_viewer_isolation_ops(void) {
  return g_isolation_ops;
}

static int html_viewer_state_is_terminal(enum html_viewer_nav_state state) {
  return state == HTML_VIEWER_NAV_READY ||
         state == HTML_VIEWER_NAV_FAILED ||
         state == HTML_VIEWER_NAV_CANCELLED ||
         state == HTML_VIEWER_NAV_IDLE;
}

int hv_text_looks_codeish(const char *text) {
  size_t len = 0;
  size_t spaces = 0;
  size_t punct = 0;
  if (!text || !text[0]) return 0;
  len = kstrlen(text);
  if (len < 80) return 0;
  if (hv_contains_ci(text, "function(") || hv_contains_ci(text, "function ") ||
      hv_contains_ci(text, "window.") || hv_contains_ci(text, "document.") ||
      hv_contains_ci(text, "AF_initDataCallback") ||
      hv_contains_ci(text, "google.") || hv_contains_ci(text, "gws-") ||
      hv_contains_ci(text, "src=") || hv_contains_ci(text, "href=") ||
      hv_contains_ci(text, "{\"") || hv_contains_ci(text, "\"}") ||
      hv_contains_ci(text, "__NEXT_DATA__") ||
      hv_contains_ci(text, "self.__next_f.push") ||
      hv_contains_ci(text, "JSON.parse") ||
      hv_contains_ci(text, "hydrateRoot") || hv_contains_ci(text, "__NUXT__") ||
      hv_contains_ci(text, "__APOLLO_STATE__") ||
      hv_contains_ci(text, "dataLayer") || hv_contains_ci(text, "gtag(") ||
      hv_contains_ci(text, "webpack") ||
      hv_contains_ci(text, "modulepreload") || hv_contains_ci(text, "import(") ||
      hv_contains_ci(text, "export default") ||
      hv_contains_ci(text, "require(") || hv_contains_ci(text, "<script") ||
      hv_contains_ci(text, "</script")) {
    return 1;
  }
  for (size_t i = 0; text[i]; i++) {
    char ch = text[i];
    if (ch == ' ' || ch == '\t' || ch == '\n') spaces++;
    if (ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == ';' ||
        ch == '=' || ch == '<' || ch == '>' || ch == '\\' || ch == '|') {
      punct++;
    }
  }
  return punct * 100u >= len * 10u && spaces * 100u <= len * 18u;
}

void hv_simplify_degraded_doc(struct html_document *doc) {
  if (!doc) return;
  for (int i = 0; i < doc->node_count; i++) {
    struct html_node *node = &doc->nodes[i];
    if ((node->type == HTML_NODE_TEXT || node->type == HTML_NODE_TAG_DIV ||
         node->type == HTML_NODE_TAG_SPAN || node->type == HTML_NODE_TAG_P) &&
        !node->href[0] && hv_text_looks_codeish(node->text)) {
      node->hidden = 1;
      continue;
    }
  }
}

void hv_cookie_trim(char *text) {
  size_t len = kstrlen(text);
  size_t start = 0;
  while (text[start] == ' ') start++;
  while (len > start && text[len - 1] == ' ') len--;
  if (start > 0 && len > start) kmemmove(text, text + start, len - start);
  else if (start >= len) {
    text[0] = '\0';
    return;
  }
  text[len - start] = '\0';
}

int hv_cookie_domain_matches(const char *cookie_domain, const char *host,
                             int host_only) {
  size_t host_len = kstrlen(host);
  size_t domain_len = kstrlen(cookie_domain);
  if (!cookie_domain[0] || !host[0]) return 0;
  if (host_only) return hv_streq_ci(cookie_domain, host);
  if (hv_streq_ci(cookie_domain, host)) return 1;
  if (host_len <= domain_len) return 0;
  return hv_streq_ci(host + (host_len - domain_len), cookie_domain) &&
         host[host_len - domain_len - 1] == '.';
}

int hv_cookie_path_matches(const char *cookie_path, const char *path) {
  size_t i = 0;
  if (!cookie_path[0]) return 1;
  while (cookie_path[i]) {
    if (path[i] != cookie_path[i]) return 0;
    i++;
  }
  return 1;
}

int hv_cookie_find_slot(struct html_viewer_app *app, const char *name,
                        const char *domain, const char *path) {
  if (!app) return -1;
  for (uint32_t i = 0; i < app->cookie_count; i++) {
    if (hv_streq_ci(app->cookies[i].name, name) &&
        hv_streq_ci(app->cookies[i].domain, domain) &&
        hv_streq_ci(app->cookies[i].path, path)) {
      return (int)i;
    }
  }
  return -1;
}

void hv_cookie_remove_index(struct html_viewer_app *app, uint32_t index) {
  if (!app || index >= app->cookie_count) return;
  for (uint32_t i = index; i + 1 < app->cookie_count; i++) {
    app->cookies[i] = app->cookies[i + 1];
  }
  if (app->cookie_count > 0) app->cookie_count--;
}

void hv_cookie_default_path(const char *request_path, char *out,
                            size_t out_len) {
  size_t len = hv_path_directory_length(request_path);
  if (!out || out_len == 0) return;
  if (!request_path || !request_path[0] || request_path[0] != '/') {
    kstrcpy(out, out_len, "/");
    return;
  }
  hv_copy_prefix(out, out_len, request_path, len);
  if (!out[0]) kstrcpy(out, out_len, "/");
}

void hv_store_cookie_from_header(struct html_viewer_app *app, const char *host,
                                 const char *path, int use_tls,
                                 const char *header_value) {
  char segment[384];
  char name[HTML_COOKIE_NAME_MAX];
  char value[HTML_COOKIE_VALUE_MAX];
  char domain[HTML_COOKIE_DOMAIN_MAX];
  char cookie_path[HTML_COOKIE_PATH_MAX];
  size_t pos = 0;
  uint8_t secure = 0;
  uint8_t host_only = 1;
  int delete_cookie = 0;
  int existing = -1;
  if (!app || !host || !path || !header_value || !header_value[0]) return;

  kstrcpy(domain, sizeof(domain), host);
  hv_cookie_default_path(path, cookie_path, sizeof(cookie_path));
  name[0] = '\0';
  value[0] = '\0';

  while (header_value[pos]) {
    size_t seg_len = 0;
    while (header_value[pos + seg_len] && header_value[pos + seg_len] != ';') {
      seg_len++;
    }
    hv_copy_prefix(segment, sizeof(segment), header_value + pos, seg_len);
    hv_cookie_trim(segment);
    if (!name[0]) {
      size_t i = 0;
      while (segment[i] && segment[i] != '=' && i + 1 < sizeof(name)) {
        name[i] = segment[i];
        i++;
      }
      name[i] = '\0';
      if (segment[i] == '=') kstrcpy(value, sizeof(value), segment + i + 1);
      hv_cookie_trim(name);
      hv_cookie_trim(value);
      if (!name[0]) return;
    } else if (hv_contains_ci(segment, "=")) {
      char attr_name[32];
      char attr_value[HTML_COOKIE_PATH_MAX];
      size_t i = 0;
      size_t j = 0;
      while (segment[i] && segment[i] != '=' && i + 1 < sizeof(attr_name)) {
        attr_name[i] = segment[i];
        i++;
      }
      attr_name[i] = '\0';
      if (segment[i] == '=') {
        kstrcpy(attr_value, sizeof(attr_value), segment + i + 1);
      } else {
        attr_value[0] = '\0';
      }
      hv_cookie_trim(attr_name);
      hv_cookie_trim(attr_value);
      if (hv_streq_ci(attr_name, "Domain")) {
        if (attr_value[0] == '.') {
          while (attr_value[j + 1]) {
            attr_value[j] = attr_value[j + 1];
            j++;
          }
          attr_value[j] = '\0';
        }
        if (!hv_cookie_domain_matches(attr_value, host, 0)) return;
        kstrcpy(domain, sizeof(domain), attr_value);
        host_only = 0;
      } else if (hv_streq_ci(attr_name, "Path")) {
        kstrcpy(cookie_path, sizeof(cookie_path), attr_value[0] ? attr_value : "/");
      } else if (hv_streq_ci(attr_name, "Max-Age") &&
                 attr_value[0] == '0' && attr_value[1] == '\0') {
        delete_cookie = 1;
      }
    } else if (hv_streq_ci(segment, "Secure")) {
      secure = 1;
    }
    pos += seg_len;
    if (header_value[pos] == ';') pos++;
    while (header_value[pos] == ' ') pos++;
  }

  if (secure && !use_tls) return;
  existing = hv_cookie_find_slot(app, name, domain, cookie_path);
  if (delete_cookie) {
    if (existing >= 0) hv_cookie_remove_index(app, (uint32_t)existing);
    return;
  }
  if (existing < 0) {
    if (app->cookie_count >= HTML_MAX_COOKIES) hv_cookie_remove_index(app, 0);
    existing = (int)app->cookie_count++;
  }
  kmemzero(&app->cookies[existing], sizeof(app->cookies[existing]));
  kstrcpy(app->cookies[existing].name, sizeof(app->cookies[existing].name),
          name);
  kstrcpy(app->cookies[existing].value, sizeof(app->cookies[existing].value),
          value);
  kstrcpy(app->cookies[existing].domain, sizeof(app->cookies[existing].domain),
          domain);
  kstrcpy(app->cookies[existing].path, sizeof(app->cookies[existing].path),
          cookie_path);
  app->cookies[existing].secure = secure;
  app->cookies[existing].host_only = host_only;
}

int hv_build_cookie_header(struct html_viewer_app *app, const char *host,
                           const char *path, int use_tls,
                           char *out, size_t out_len) {
  int wrote_any = 0;
  if (!out || out_len == 0) return 0;
  out[0] = '\0';
  if (!app || !host || !path) return 0;
  for (uint32_t i = 0; i < app->cookie_count; i++) {
    const struct html_cookie *cookie = &app->cookies[i];
    if (cookie->secure && !use_tls) continue;
    if (!hv_cookie_domain_matches(cookie->domain, host, cookie->host_only)) {
      continue;
    }
    if (!hv_cookie_path_matches(cookie->path, path)) continue;
    if (wrote_any) kbuf_append(out, out_len, "; ");
    kbuf_append(out, out_len, cookie->name);
    kbuf_append(out, out_len, "=");
    kbuf_append(out, out_len, cookie->value);
    wrote_any = 1;
  }
  return wrote_any;
}

static int g_html_viewer_strict_mode = 0;

void html_viewer_state_strict_mode_set(int enabled) {
  g_html_viewer_strict_mode = enabled ? 1 : 0;
}

int html_viewer_state_strict_mode_enabled(void) {
  return g_html_viewer_strict_mode;
}

void html_viewer_set_state(struct html_viewer_app *app,
                           enum html_viewer_nav_state state,
                           const char *stage) {
  if (!app) return;
  enum html_viewer_nav_state from = app->nav_state;
  int suspicious = (from != state &&
                    !html_viewer_state_transition_allowed(from, state));
  if (suspicious) {
    klog(KLOG_WARN, "[browser] suspicious state transition");
    if (g_html_viewer_strict_mode) {
      /* Hard escalation: route the transition to FAILED so the rest of the
       * pipeline observes a coherent navigation outcome instead of a state
       * pair that violates the state machine. */
      kstrcpy(app->last_stage, sizeof(app->last_stage),
              stage && stage[0] ? stage : "state-machine");
      kstrcpy(app->last_error_reason, sizeof(app->last_error_reason),
              "Suspicious browser state transition (strict mode).");
      app->nav_state = HTML_VIEWER_NAV_FAILED;
      app->loading = 0;
      /* Audit event: strict mode just escalated a violation. Logged with
       * a stable [audit] prefix so log-mining tools can grep for security-
       * relevant browser events alongside [auth], [priv], [update]. */
      klog(KLOG_WARN,
           "[audit] [browser] strict-mode violation -> nav=FAILED");
      if (g_isolation_ops && g_isolation_ops->heartbeat) {
        g_isolation_ops->heartbeat(app->active_navigation_id,
                                   HTML_VIEWER_NAV_FAILED);
      }
      if (g_isolation_ops && g_isolation_ops->on_fatal) {
        g_isolation_ops->on_fatal(app->active_navigation_id, app->last_stage,
                                  app->last_error_reason);
      }
      return;
    }
  }
  app->nav_state = state;
  app->loading = (state == HTML_VIEWER_NAV_LOADING ||
                  state == HTML_VIEWER_NAV_REDIRECTING ||
                  state == HTML_VIEWER_NAV_RENDERING);
  if (stage && stage[0]) {
    kstrcpy(app->last_stage, sizeof(app->last_stage), stage);
  }
  if (g_isolation_ops && g_isolation_ops->heartbeat &&
      html_viewer_state_is_terminal(state)) {
    g_isolation_ops->heartbeat(app->active_navigation_id, state);
  }
  if (state == HTML_VIEWER_NAV_FAILED && g_isolation_ops &&
      g_isolation_ops->on_fatal) {
    g_isolation_ops->on_fatal(app->active_navigation_id, app->last_stage,
                              app->last_error_reason);
  }
}

void html_viewer_set_error_context(struct html_viewer_app *app,
                                   const char *stage,
                                   const char *reason) {
  if (!app) return;
  if (stage && stage[0]) {
    kstrcpy(app->last_stage, sizeof(app->last_stage), stage);
  }
  if (reason && reason[0]) {
    kstrcpy(app->last_error_reason, sizeof(app->last_error_reason), reason);
  } else {
    app->last_error_reason[0] = '\0';
  }
}

void html_viewer_begin_navigation(struct html_viewer_app *app, const char *url) {
  if (!app) return;
  app->navigation_id++;
  app->active_navigation_id = app->navigation_id;
  app->redirect_count = 0;
  app->safe_mode = 0;
  app->external_css_loaded = 0;
  app->external_images_loaded = 0;
  hv_resource_budget_reset(app);
  hv_render_budget_reset(app);
  hv_parse_budget_reset(app);
  hv_nav_budget_reset(app);
  app->last_error_reason[0] = '\0';
  kstrcpy(app->last_stage, sizeof(app->last_stage), "loading");
  if (url && url[0]) {
    kstrcpy(app->final_url, sizeof(app->final_url), url);
  } else {
    app->final_url[0] = '\0';
  }
  html_viewer_set_state(app, HTML_VIEWER_NAV_LOADING, "loading");
}

int html_viewer_navigation_is_current(const struct html_viewer_app *app,
                                      uint32_t navigation_id) {
  return app && navigation_id != 0 &&
         app->active_navigation_id == navigation_id;
}

void html_viewer_cancel_navigation(struct html_viewer_app *app,
                                   const char *reason) {
  if (!app) return;
  /* Trip the navigation-level cooperative budget BEFORE bumping
   * active_navigation_id. Any inner loop still running for the old
   * navigation observes op_budget_is_blocked() == 1 and bails out
   * cleanly on its next take(). */
  hv_nav_budget_cancel(app, reason);
  html_viewer_background_cancel();
  app->active_navigation_id = ++app->navigation_id;
  html_viewer_set_error_context(app, "cancelled",
                                reason && reason[0] ? reason
                                                    : "Navigation cancelled.");
  html_viewer_set_state(app, HTML_VIEWER_NAV_CANCELLED, "cancelled");
  if (reason && reason[0]) {
    html_viewer_load_text_document(app, "Navigation Cancelled", reason,
                                   kstrlen(reason), 0xCDD6F4);
  } else {
    html_viewer_load_text_document(app, "Navigation Cancelled",
                                   "Navigation cancelled.", 21, 0xCDD6F4);
  }
  if (app->window) {
    compositor_set_title(app->window->id, "Navigation Cancelled");
    compositor_invalidate(app->window->id);
  }
}

void html_viewer_set_transport_error(struct html_viewer_app *app) {
  char message[160];
  int err = http_last_error();
  message[0] = '\0';
  kstrcpy(message, sizeof(message), http_error_string(err));
  if (err == HTML_VIEWER_HTTP_ERR_TLS) {
    struct tls_security_info info;
    kbuf_append(message, sizeof(message), " (");
    kbuf_append(message, sizeof(message), tls_state_name(tls_last_state()));
    if (tls_last_error() != 0) {
      kbuf_append(message, sizeof(message), ", ");
      kbuf_append(message, sizeof(message), tls_alert_name(tls_last_error()));
    }
    if (tls_get_last_security_info(&info) == 0 && info.protocol_version != 0) {
      kbuf_append(message, sizeof(message), ", ");
      kbuf_append(message, sizeof(message),
                  tls_version_name(info.protocol_version));
      if (info.hostname_validated) {
        kbuf_append(message, sizeof(message), ", host-ok");
      }
      if (info.alpn[0]) {
        kbuf_append(message, sizeof(message), ", ");
        kbuf_append(message, sizeof(message), info.alpn);
      }
    }
    kbuf_append(message, sizeof(message), ")");
  }
  html_viewer_set_error_context(app, "transport", message);
  html_viewer_set_error(app, "Navigation Error", message);
}

void html_viewer_set_status_error(struct html_viewer_app *app, int status_code) {
  char message[128];
  message[0] = '\0';
  kstrcpy(message, sizeof(message), "HTTP status ");
  kbuf_append_u32(message, sizeof(message), (uint32_t)status_code);
  html_viewer_set_error_context(app, "status", message);
  html_viewer_set_error(app, "Navigation Error", message);
}

void html_viewer_set_error(struct html_viewer_app *app, const char *title,
                           const char *message) {
  static char err_buf[4096];
  size_t elen = 0;
  if (!app) return;
  err_buf[0] = '\0';
  kbuf_append(err_buf, sizeof(err_buf),
              "<html><head><title>Navigation Error</title></head><body>");
  kbuf_append(err_buf, sizeof(err_buf), "<h1>");
  kbuf_append(err_buf, sizeof(err_buf),
              title && title[0] ? title : "Navigation Error");
  kbuf_append(err_buf, sizeof(err_buf), "</h1>");
  if (message && message[0]) {
    kbuf_append(err_buf, sizeof(err_buf), "<p>");
    kbuf_append(err_buf, sizeof(err_buf), message);
    kbuf_append(err_buf, sizeof(err_buf), "</p>");
  }
  if (app->url[0]) {
    kbuf_append(err_buf, sizeof(err_buf), "<p><strong>URL:</strong> ");
    kbuf_append(err_buf, sizeof(err_buf), app->url);
    kbuf_append(err_buf, sizeof(err_buf), "</p>");
  }
  if (app->final_url[0]) {
    kbuf_append(err_buf, sizeof(err_buf), "<p><strong>Final URL:</strong> ");
    kbuf_append(err_buf, sizeof(err_buf), app->final_url);
    kbuf_append(err_buf, sizeof(err_buf), "</p>");
  }
  if (app->last_stage[0]) {
    kbuf_append(err_buf, sizeof(err_buf), "<p><strong>Stage:</strong> ");
    kbuf_append(err_buf, sizeof(err_buf), app->last_stage);
    kbuf_append(err_buf, sizeof(err_buf), "</p>");
  }
  if (app->redirect_count > 0) {
    kbuf_append(err_buf, sizeof(err_buf), "<p><strong>Redirects:</strong> ");
    kbuf_append_u32(err_buf, sizeof(err_buf), app->redirect_count);
    kbuf_append(err_buf, sizeof(err_buf), "</p>");
  }
  if (app->safe_mode) {
    kbuf_append(err_buf, sizeof(err_buf),
                "<p><strong>Safe mode:</strong> enabled for this navigation.</p>");
  }
  if (app->url[0]) {
    kbuf_append(err_buf, sizeof(err_buf), "<a href=\"");
    kbuf_append(err_buf, sizeof(err_buf), app->url);
    kbuf_append(err_buf, sizeof(err_buf), "\">Try Again</a>  ");
  }
  kbuf_append(err_buf, sizeof(err_buf),
              "<a href=\"about:home\">New Tab</a></body></html>");
  while (err_buf[elen]) elen++;
  hv_doc_reset(&app->doc);
  hv_parse_locked(err_buf, elen, &app->doc);
  for (int ei = 0; ei < app->doc.node_count; ei++) {
    if (app->doc.nodes[ei].type == HTML_NODE_TAG_H1) {
      app->doc.nodes[ei].css_color = 0xF38BA8;
    }
  }
  kstrcpy(app->doc.title, sizeof(app->doc.title),
          title && title[0] ? title : "Navigation Error");
  if (app->final_url[0] == '\0' && app->url[0]) {
    kstrcpy(app->final_url, sizeof(app->final_url), app->url);
  }
  html_viewer_set_state(app, HTML_VIEWER_NAV_FAILED,
                        app->last_stage[0] ? app->last_stage : "failed");
  if (app->window) compositor_set_title(app->window->id, app->doc.title);
}
