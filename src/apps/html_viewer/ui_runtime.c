#include "internal/html_viewer_internal.h"

/* Static buffer for dynamically-generated about: pages */
static char hv_about_buf[16384];

static void hv_about_append_kib(char *buf, size_t buf_size, size_t bytes) {
  kbuf_append_u32(buf, buf_size, (uint32_t)(bytes / 1024u));
  kbuf_append(buf, buf_size, " KiB");
}

void html_viewer_load_quick_start(struct html_viewer_app *app) {
  struct html_node *node = NULL;

  if (!app) return;

  hv_doc_reset(&app->doc);
  app->scroll_offset = 0;
  app->url_editing = 0;
  app->url_cursor = 0;
  app->focused_node_index = -1;
  app->background_mode = 0;
  app->safe_mode = 0;
  app->redirect_count = 0;
  app->external_css_loaded = 0;
  app->external_images_loaded = 0;
  hv_resource_budget_reset(app);
  hv_render_budget_reset(app);
  app->last_error_reason[0] = '\0';
  kstrcpy(app->last_stage, sizeof(app->last_stage), "ready");
  kstrcpy(app->url, sizeof(app->url), "about:newtab");
  kstrcpy(app->final_url, sizeof(app->final_url), app->url);
  kstrcpy(app->doc.title, sizeof(app->doc.title), "CapyBrowser");
  html_viewer_set_state(app, HTML_VIEWER_NAV_READY, "ready");

  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_H1;
    node->bold = 1;
    kstrcpy(node->text, sizeof(node->text), "CapyBrowser");
  }

  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_P;
    kstrcpy(node->text, sizeof(node->text),
            "Type a URL in the top bar and press Enter.");
  }

  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_P;
    kstrcpy(node->text, sizeof(node->text),
            "Services and drivers continue loading in the background.");
  }

  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_A;
    kstrcpy(node->text, sizeof(node->text), "History");
    kstrcpy(node->href, sizeof(node->href), "about:history");
  }

  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_A;
    kstrcpy(node->text, sizeof(node->text), "Bookmarks");
    kstrcpy(node->href, sizeof(node->href), "about:bookmarks");
  }

  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_A;
    kstrcpy(node->text, sizeof(node->text), "Settings");
    kstrcpy(node->href, sizeof(node->href), "about:settings");
  }

  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_A;
    kstrcpy(node->text, sizeof(node->text), "About");
    kstrcpy(node->href, sizeof(node->href), "about:version");
  }

  if (app->window) compositor_set_title(app->window->id, app->doc.title);
}

void html_viewer_load_builtin(struct html_viewer_app *app, const char *url) {
  const char *html = NULL;
  size_t len = 0;
  if (!app || !url) return;
  if (hv_strncmp(url, "about:blank", 11) == 0) {
    html = "<html><head><title>New Tab</title></head><body></body></html>";
    url = "about:blank";
  } else if (hv_strncmp(url, "about:history", 13) == 0) {
    hv_about_buf[0] = '\0';
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "<html><head><title>History</title></head><body>"
                "<h1>Browsing History</h1>");
    if (hv_history_count == 0) {
      kbuf_append(hv_about_buf, sizeof(hv_about_buf), "<p>No history yet.</p>");
    } else {
      for (int i = hv_history_count - 1; i >= 0; i--) {
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), "<a href=\"");
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), hv_history[i]);
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), "\">");
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), hv_history[i]);
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</a><br>");
      }
    }
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</body></html>");
    html = hv_about_buf;
    url = "about:history";
  } else if (hv_strncmp(url, "about:bookmarks", 15) == 0) {
    hv_about_buf[0] = '\0';
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "<html><head><title>Bookmarks</title></head><body>"
                "<h1>Bookmarks</h1>");
    if (hv_bookmark_count == 0) {
      kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                  "<p>No bookmarks yet. Press Ctrl+D on a page to bookmark it.</p>");
    } else {
      for (int i = 0; i < hv_bookmark_count; i++) {
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), "<a href=\"");
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), hv_bookmark_url[i]);
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), "\">");
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), hv_bookmark_title[i]);
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</a><br>");
      }
    }
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</body></html>");
    html = hv_about_buf;
    url = "about:bookmarks";
  } else if (hv_strncmp(url, "about:settings", 14) == 0) {
    html =
        "<html><head><title>Settings</title></head><body>"
        "<h1>CapyBrowser Settings</h1>"
        "<h2>Keyboard Shortcuts</h2>"
        "<ul>"
        "<li><strong>Enter</strong> - Navigate to URL in address bar</li>"
        "<li><strong>Backspace</strong> - Go back in history</li>"
        "<li><strong>F5</strong> - Reload page</li>"
        "<li><strong>F3</strong> - Find next match</li>"
        "<li><strong>Ctrl+F</strong> - Find in page</li>"
        "<li><strong>Ctrl+L</strong> - Focus address bar</li>"
        "<li><strong>Ctrl+R</strong> - Reload page</li>"
        "<li><strong>Esc</strong> - Cancel current navigation when loading</li>"
        "<li><strong>Ctrl+D</strong> - Bookmark current page</li>"
        "<li><strong>Ctrl+B</strong> - Open bookmarks</li>"
        "<li><strong>Arrow keys</strong> - Scroll page</li>"
        "<li><strong>Page Up/Down</strong> - Scroll by page</li>"
        "<li><strong>Tab</strong> - Focus next form field</li>"
        "</ul>"
        "<h2>Navigation</h2>"
        "<ul>"
        "<li>Click the &lt; button or press Backspace to go back</li>"
        "<li>Click the &gt; button to go forward</li>"
        "<li>Click R button, press F5, or Ctrl+R to reload</li>"
        "<li>Press Esc while loading to cancel the current navigation</li>"
        "<li>Click the * star to bookmark this page</li>"
        "</ul>"
        "<h2>Features</h2>"
        "<ul>"
        "<li>HTML5: block/inline layout, tables, forms, details, semantic tags</li>"
        "<li>CSS: color, background, font-size, font-weight, text-align, margin/padding, border, display:none, line-height, max-width, text-transform, text-decoration, opacity, list-style, @media queries</li>"
        "<li>Images: PNG and JPEG (baseline DCT) via HTTP fetch with LRU cache</li>"
        "<li>HTTP: keep-alive connection pool, gzip/deflate, chunked encoding, redirects, cookies</li>"
        "<li>HTTPS: TLS 1.2 via BearSSL with 146 trust anchors</li>"
        "<li>DNS: A-record cache + prefetch for page resources</li>"
        "<li>Navigation: back/forward history, bookmarks (Ctrl+D), find-in-page (Ctrl+F)</li>"
        "<li>Safety: safe-mode fallback, navigation state tracking, capped external resources</li>"
        "</ul>"
        "<a href=\"about:home\">Home</a> | <a href=\"about:version\">Version</a> | "
        "<a href=\"about:network\">Network</a> | <a href=\"about:memory\">Memory</a>"
        "</body></html>";
    url = "about:settings";
  } else if (hv_strncmp(url, "about:network", 13) == 0) {
    struct dns_cache_stats dns_stats;
    struct hv_http_cache_stats http_stats;
    dns_cache_stats_get(&dns_stats);
    hv_http_cache_stats_get(&http_stats);
    hv_about_buf[0] = '\0';
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "<html><head><title>Network Metrics</title></head><body>"
                "<h1>Network Metrics</h1><h2>DNS Cache</h2><p>Entries: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), dns_stats.entries);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>Hits: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), (uint32_t)dns_stats.hits);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>Misses: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), (uint32_t)dns_stats.misses);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>Expired: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), (uint32_t)dns_stats.expired);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "</p><h2>HTTP Cache</h2><p>Entries: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), http_stats.entries);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>Memory: ");
    hv_about_append_kib(hv_about_buf, sizeof(hv_about_buf), http_stats.total_bytes);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), " / ");
    hv_about_append_kib(hv_about_buf, sizeof(hv_about_buf), HV_HTTP_CACHE_TOTAL_MAX);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>Hits: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), http_stats.hits);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>Misses: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), http_stats.misses);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>Evictions: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), http_stats.evictions);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>Rejected: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), http_stats.rejected);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "</p><a href=\"about:memory\">Memory</a> | "
                "<a href=\"about:settings\">Settings</a> | "
                "<a href=\"about:home\">Home</a></body></html>");
    html = hv_about_buf;
    url = "about:network";
  } else if (hv_strncmp(url, "about:memory", 12) == 0) {
    struct hv_http_cache_stats http_stats;
    hv_http_cache_stats_get(&http_stats);
    hv_about_buf[0] = '\0';
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "<html><head><title>Memory Metrics</title></head><body>"
                "<h1>Memory Metrics</h1><p>Kernel heap used: ");
    hv_about_append_kib(hv_about_buf, sizeof(hv_about_buf), kheap_used());
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>Kernel heap size: ");
    hv_about_append_kib(hv_about_buf, sizeof(hv_about_buf), kheap_size());
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>HTTP cache memory: ");
    hv_about_append_kib(hv_about_buf, sizeof(hv_about_buf), http_stats.total_bytes);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), " / ");
    hv_about_append_kib(hv_about_buf, sizeof(hv_about_buf), HV_HTTP_CACHE_TOTAL_MAX);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</p><p>HTTP cache entries: ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), http_stats.entries);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "</p><a href=\"about:network\">Network</a> | "
                "<a href=\"about:settings\">Settings</a> | "
                "<a href=\"about:home\">Home</a></body></html>");
    html = hv_about_buf;
    url = "about:memory";
  } else if (hv_strncmp(url, "about:version", 13) == 0) {
    hv_about_buf[0] = '\0';
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "<html><head><title>About CapyBrowser</title></head><body>"
                "<h1>CapyBrowser</h1>"
                "<p>HTTP/1.1 client with verified HTTPS (TLS 1.2) transport.</p>"
                "<p>Features: cookies, redirects, gzip/deflate, CSS engine (custom properties, @media, HSL), PNG images, forms, history, bookmarks, find-in-page.</p>"
                "<h2>Current Navigation Status</h2>"
                "<p><strong>URL:</strong> ");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                app->url[0] ? app->url : "(none)");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "</p><p><strong>Final URL:</strong> ");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                app->final_url[0] ? app->final_url : "(none)");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "</p><p><strong>Stage:</strong> ");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                app->last_stage[0] ? app->last_stage : "idle");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "</p><p><strong>Redirects:</strong> ");
    kbuf_append_u32(hv_about_buf, sizeof(hv_about_buf), app->redirect_count);
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "</p><p><strong>Safe mode:</strong> ");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                app->safe_mode ? "enabled" : "disabled");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "</p><p><strong>Last error:</strong> ");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                app->last_error_reason[0] ? app->last_error_reason : "(none)");
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "</p><a href=\"about:settings\">Settings</a> | "
                "<a href=\"about:home\">Home</a>"
                "</body></html>");
    html = hv_about_buf;
  } else {
    hv_about_buf[0] = '\0';
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "<html><head><title>New Tab</title></head><body>"
                "<h1>CapyBrowser</h1>"
                "<p>Type a URL and press Enter. Press Ctrl+D to bookmark current page.</p>"
                "<form action=\"about:navigate\" method=\"get\">"
                "<input type=\"text\" name=\"url\" placeholder=\"Enter URL or search...\"> "
                "<input type=\"submit\" value=\"Go\">"
                "</form>"
                "<h2>Bookmarks</h2>");
    if (hv_bookmark_count == 0) {
      kbuf_append(hv_about_buf, sizeof(hv_about_buf), "<p>No bookmarks yet.</p>");
    } else {
      for (int i = 0; i < hv_bookmark_count; i++) {
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), "<a href=\"");
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), hv_bookmark_url[i]);
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), "\">");
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), hv_bookmark_title[i]);
        kbuf_append(hv_about_buf, sizeof(hv_about_buf), "</a><br>");
      }
    }
    kbuf_append(hv_about_buf, sizeof(hv_about_buf),
                "<h2>Quick Links</h2>"
                "<a href=\"https://www.google.com\">Google</a><br>"
                "<a href=\"https://example.com\">example.com</a><br>"
                "<a href=\"about:history\">History</a><br>"
                "<a href=\"about:bookmarks\">All Bookmarks</a><br>"
                "<a href=\"about:settings\">Settings</a><br>"
                "<a href=\"about:network\">Network Metrics</a><br>"
                "<a href=\"about:memory\">Memory Metrics</a><br>"
                "<a href=\"about:version\">About</a>"
                "</body></html>");
    html = hv_about_buf;
    url = (hv_strncmp(url, "about:newtab", 12) == 0) ? "about:newtab"
                                                      : "about:home";
  }
  while (html[len]) len++;
  hv_parse_locked(html, len, &app->doc);
  kstrcpy(app->url, sizeof(app->url), url);
  kstrcpy(app->final_url, sizeof(app->final_url), url);
  app->last_error_reason[0] = '\0';
  html_viewer_set_state(app, HTML_VIEWER_NAV_READY, "ready");
  if (app->doc.title[0] && app->window) {
    compositor_set_title(app->window->id, app->doc.title);
  }
}

void html_viewer_cleanup(void) {
  html_viewer_background_cancel();
  hv_doc_reset(&g_viewer.doc);
#ifndef UNIT_TEST
  if (g_hv_browser_job.staged_app &&
      g_hv_browser_job.state != HV_BROWSER_QUEUED &&
      g_hv_browser_job.state != HV_BROWSER_RUNNING) {
    hv_doc_reset(&g_hv_browser_job.staged_app->doc);
  }
  if (g_hv_browser_job.state != HV_BROWSER_QUEUED &&
      g_hv_browser_job.state != HV_BROWSER_RUNNING) {
    hv_http_cache_clear();
  }
#else
  hv_http_cache_clear();
#endif
  g_viewer.window = NULL;
  g_viewer_open = 0;
}
