static int test_html_viewer_loads_external_assets(void) {
  struct html_viewer_app *app = NULL;
  const struct html_node *heading = NULL;
  const struct html_node *image = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before asset test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://example.com/assets");

  heading = find_node(HTML_NODE_TAG_H1, &app->doc, "Asset Title");
  image = find_node(HTML_NODE_TAG_IMG, &app->doc, "Pixel");

  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "asset navigation should end in ready state");
  fails += expect_true(app->external_css_loaded > 0,
                       "external stylesheet should be loaded");
  fails += expect_true(app->external_images_loaded > 0,
                       "external image should be loaded");
  fails += expect_true(heading != NULL, "asset page should render the heading");
  fails += expect_true(image != NULL, "asset page should preserve the image node");
  fails += expect_true(image && image->image_pixels != NULL,
                       "external image should decode into pixels");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static void fill_cache_response(struct http_response *resp, uint8_t *body,
                                size_t body_len) {
  memset(resp, 0, sizeof(*resp));
  resp->status_code = 200;
  resp->body = body;
  resp->body_len = body_len;
  strncpy(resp->headers[0].name, "Cache-Control", sizeof(resp->headers[0].name) - 1);
  strncpy(resp->headers[0].value, "max-age=600", sizeof(resp->headers[0].value) - 1);
  strncpy(resp->headers[1].name, "Content-Type", sizeof(resp->headers[1].name) - 1);
  strncpy(resp->headers[1].value, "text/css", sizeof(resp->headers[1].value) - 1);
  resp->header_count = 2;
}

static int test_html_viewer_http_cache_enforces_memory_budget(void) {
  static uint8_t body_a[HV_HTTP_CACHE_BODY_MAX];
  static uint8_t body_b[HV_HTTP_CACHE_BODY_MAX];
  static uint8_t body_c[HV_HTTP_CACHE_BODY_MAX];
  struct http_response resp;
  struct hv_http_cache_stats stats;
  int fails = 0;

  memset(body_a, 'a', sizeof(body_a));
  memset(body_b, 'b', sizeof(body_b));
  memset(body_c, 'c', sizeof(body_c));
  hv_http_cache_clear();

  fill_cache_response(&resp, body_a, sizeof(body_a));
  hv_http_cache_store("https://cache.test/a.css", &resp);
  fill_cache_response(&resp, body_b, sizeof(body_b));
  hv_http_cache_store("https://cache.test/b.css", &resp);

  fails += expect_true(hv_http_cache_find("https://cache.test/a.css") != NULL,
                       "HTTP cache should find stored entry before pressure");
  hv_http_cache_tick();
  hv_http_cache_tick();
  fill_cache_response(&resp, body_c, sizeof(body_c));
  hv_http_cache_store("https://cache.test/c.css", &resp);
  hv_http_cache_stats_get(&stats);

  fails += expect_true(stats.total_bytes <= HV_HTTP_CACHE_TOTAL_MAX,
                       "HTTP cache should stay within total memory budget");
  fails += expect_true(stats.entries <= 2,
                       "HTTP cache should cap resident entries by memory budget");
  fails += expect_true(stats.evictions >= 1,
                       "HTTP cache should evict old entries under memory pressure");
  fails += expect_true(hv_http_cache_find("https://cache.test/c.css") != NULL,
                       "HTTP cache should keep the newly stored entry");

  fill_cache_response(&resp, body_a, sizeof(body_a));
  strncpy(resp.headers[0].value, "no-store", sizeof(resp.headers[0].value) - 1);
  hv_http_cache_store("https://cache.test/no-store.css", &resp);
  fails += expect_true(hv_http_cache_find("https://cache.test/no-store.css") == NULL,
                       "HTTP cache should reject no-store responses");

  hv_http_cache_clear();
  return fails;
}

static int test_html_viewer_external_resource_budget_logs_failure(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  klog_reset();
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before resource-budget test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://example.com/budget");

  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "resource-budget page should degrade without failing navigation");
  fails += expect_true(app->safe_mode == 1,
                       "resource-budget page should enable safe mode");
  fails += expect_true(app->resource_budget_exhausted == 1,
                       "resource-budget page should record exhausted budget");
  fails += expect_true(app->external_fetch_attempts == HV_EXTERNAL_FETCH_LIMIT,
                       "resource-budget page should stop at the fetch budget");
  fails += expect_true(app->external_css_loaded == HV_EXTERNAL_CSS_LIMIT,
                       "resource-budget page should spend CSS stage budget first");
  fails += expect_true(app->external_images_loaded == 2,
                       "resource-budget page should stop remaining image fetches");
  fails += expect_true(strstr(app->last_error_reason,
                              "External resource budget exhausted") != NULL,
                       "resource-budget page should expose a controlled reason");
  fails += expect_true(strstr(klog_serialize(),
                              "[browser] external resource budget exhausted") != NULL,
                       "resource-budget exhaustion should be persisted through klog");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_about_metrics_pages(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before metrics page test");
  if (!app) return fails;

  html_viewer_navigate(app, "about:network");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "about:network should render as a builtin page");
  fails += expect_true(strcmp(app->url, "about:network") == 0,
                       "about:network should update current URL");
  fails += expect_true(document_contains_text(&app->doc, "Network Metrics"),
                       "about:network should expose network metrics");
  fails += expect_true(document_contains_text(&app->doc, "HTTP Cache"),
                       "about:network should expose HTTP cache metrics");
  fails += expect_true(document_contains_text(&app->doc, "DNS Cache"),
                       "about:network should expose DNS cache metrics");

  html_viewer_navigate(app, "about:memory");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "about:memory should render as a builtin page");
  fails += expect_true(strcmp(app->url, "about:memory") == 0,
                       "about:memory should update current URL");
  fails += expect_true(document_contains_text(&app->doc, "Memory Metrics"),
                       "about:memory should expose memory metrics");
  fails += expect_true(document_contains_text(&app->doc, "Kernel heap used"),
                       "about:memory should expose heap usage");
  fails += expect_true(document_contains_text(&app->doc, "HTTP cache memory"),
                       "about:memory should expose cache memory usage");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_discards_stale_response(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  g_reentrant_app = NULL;
  g_reentrant_triggered = 0;
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before stale-response test");
  if (!app) return fails;

  g_reentrant_app = app;
  html_viewer_navigate(app, "https://stale.test/old");
  g_reentrant_app = NULL;

  fails += expect_true(g_reentrant_triggered == 1,
                       "stale-response test should trigger a newer navigation");
  fails += expect_true(strcmp(app->doc.title, "Fresh Example") == 0,
                       "older response should not overwrite the newer navigation");
  fails += expect_true(strcmp(app->url, "https://example.com/fresh") == 0,
                       "current URL should belong to the newer navigation");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "newer navigation should end in ready state");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_safe_mode_on_large_document(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before large-document test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://heavy.test/oversized");

  fails += expect_true(app->loading == 0,
                       "oversized document should exit loading state");
  fails += expect_true(app->safe_mode == 1,
                       "oversized document should enable safe mode");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_FAILED,
                       "oversized document should fail in a controlled way");
  fails += expect_true(strstr(app->last_error_reason, "safe browser limit") != NULL,
                       "oversized document should expose a useful failure reason");
  fails += expect_true(strcmp(app->last_stage, "parse") == 0,
                       "oversized document should fail during parse stage");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_rejects_script_document(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before script-response test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://script.test/app.js");

  fails += expect_true(app->loading == 0,
                       "script response should exit loading state");
  fails += expect_true(app->safe_mode == 1,
                       "script response should enable safe mode");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_FAILED,
                       "script response should fail as unsupported document");
  fails += expect_true(strstr(app->last_error_reason, "Script/data response") != NULL,
                       "script response should expose a controlled error reason");
  fails += expect_true(!document_contains_text(&app->doc, "window.__BOOTSTRAP__"),
                       "script response should not be rendered as raw page text");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_extracts_html_after_bootstrap_noise(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before noisy-html test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://noisy.test/");

  fails += expect_true(app->loading == 0,
                       "noisy HTML response should exit loading state");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "noisy HTML response should render after HTML sniffing");
  fails += expect_true(app->safe_mode == 1,
                       "noisy HTML response should mark safe mode after skipping bootstrap");
  fails += expect_true(strcmp(app->doc.title, "Noisy Page") == 0,
                       "noisy HTML response should parse title from embedded HTML");
  fails += expect_true(find_node(HTML_NODE_TAG_H1, &app->doc, "Noisy Page") != NULL,
                       "noisy HTML response should render visible heading");
  fails += expect_true(!document_contains_text(&app->doc, "self.__next_f.push"),
                       "bootstrap script should not leak into rendered document");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_degrades_script_only_html_shell(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before app-shell test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://shell.test/");

  fails += expect_true(app->loading == 0,
                       "script-only HTML shell should exit loading state");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "script-only HTML shell should produce a controlled degraded page");
  fails += expect_true(app->safe_mode == 1,
                       "script-only HTML shell should enable safe mode");
  fails += expect_true(strcmp(app->doc.title, "App Shell") == 0,
                       "degraded page should preserve the original title");
  fails += expect_true(document_contains_text(&app->doc, "rely on JavaScript"),
                       "degraded page should explain the JS-heavy limitation");
  fails += expect_true(!document_contains_text(&app->doc, "window.__BOOTSTRAP__"),
                       "degraded page should not unhide bootstrap script text");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_follows_meta_refresh_redirect(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before meta-refresh test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://meta.test/start");

  fails += expect_true(app->loading == 0,
                       "meta refresh should exit loading state");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "meta refresh landing should end in ready state");
  fails += expect_true(strcmp(app->url, "https://meta.test/landing") == 0,
                       "meta refresh should update current URL to landing page");
  fails += expect_true(strcmp(app->final_url, "https://meta.test/landing") == 0,
                       "meta refresh should record final URL");
  fails += expect_true(app->redirect_count == 1,
                       "meta refresh should count as a redirect hop");
  fails += expect_true(strcmp(app->doc.title, "Meta Landing") == 0,
                       "meta refresh landing document should be rendered");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}
