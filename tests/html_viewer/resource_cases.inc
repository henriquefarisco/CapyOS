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

