static int test_html_viewer_open_quick_start(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;

  fails += expect_true(app != NULL, "browser open should attach app state to the window");
  fails += expect_true(app && app->window == &g_test_window,
                       "browser app should keep the created window");
  fails += expect_true(app && strcmp(app->url, "about:newtab") == 0,
                       "browser quick start should seed about:newtab");
  fails += expect_true(app && strcmp(app->doc.title, "CapyBrowser") == 0,
                       "browser quick start should define the window title");
  fails += expect_true(app && find_node(HTML_NODE_TAG_H1, &app->doc, "CapyBrowser") != NULL,
                       "browser quick start should include the H1 heading");
  fails += expect_true(app &&
                           find_node(HTML_NODE_TAG_A, &app->doc, "History") != NULL,
                       "browser quick start should expose navigation links");
  fails += expect_true(g_test_window.on_close != NULL,
                       "browser window should register a close handler");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);

  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should reopen cleanly after close");
  fails += expect_true(app && strcmp(app->url, "about:newtab") == 0,
                       "browser reopen should reset quick start state");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_http_navigation_finishes_loading(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  memset(g_last_http_host, 0, sizeof(g_last_http_host));
  memset(g_last_http_path, 0, sizeof(g_last_http_path));
  g_last_http_port = 0;
  g_last_http_tls = 0;

  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should expose app state before navigation");
  if (!app) return fails;

  html_viewer_navigate(app, "example.com/news");

  fails += expect_true(app->loading == 0,
                       "http navigation should not stay stuck in loading");
  fails += expect_true(strcmp(app->url, "https://example.com/news") == 0,
                       "navigation should normalize host-only URLs to https");
  fails += expect_true(strcmp(app->doc.title, "Modern Example") == 0,
                       "http navigation should parse the returned HTML title");
  fails += expect_true(find_node(HTML_NODE_TAG_H1, &app->doc, "Modern Example") != NULL,
                       "http navigation should render the main heading");
  fails += expect_true(find_node(HTML_NODE_TAG_DIV, &app->doc,
                                 "JS disabled fallback works.") != NULL,
                       "http navigation should preserve noscript fallback text");
  fails += expect_true(strcmp(g_last_http_host, "example.com") == 0,
                       "http request should target the parsed host");
  fails += expect_true(strcmp(g_last_http_path, "/news") == 0,
                       "http request should target the parsed path");
  fails += expect_true(g_last_http_port == 443,
                       "https normalization should use port 443");
  fails += expect_true(g_last_http_tls == 1,
                       "https normalization should enable TLS");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_redirects_update_final_url(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before redirect test");
  if (!app) return fails;

  html_viewer_navigate(app, "http://redirect.test/start");

  fails += expect_true(app->loading == 0, "redirected navigation should finish loading");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "redirected navigation should end in ready state");
  fails += expect_true(strcmp(app->url, "https://example.com/landing") == 0,
                       "redirect should update current URL to the landing page");
  fails += expect_true(strcmp(app->final_url, "https://example.com/landing") == 0,
                       "redirect should record the final URL");
  fails += expect_true(app->redirect_count == 1,
                       "redirect count should track the redirect chain");
  fails += expect_true(strcmp(app->doc.title, "Redirect Landing") == 0,
                       "redirect landing page should be rendered");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_all_common_redirect_statuses(void) {
  static const int statuses[] = {301, 302, 303, 307, 308};
  int fails = 0;

  for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
    struct html_viewer_app *app = NULL;
    char url[64];

    memset(&g_test_window, 0, sizeof(g_test_window));
    html_viewer_open();
    app = (struct html_viewer_app *)g_test_window.user_data;
    fails += expect_true(app != NULL, "browser should open before redirect-status test");
    if (!app) return fails;

    snprintf(url, sizeof(url), "http://redirect.test/%d", statuses[i]);
    html_viewer_navigate(app, url);

    fails += expect_true(app->loading == 0,
                         "redirect status should exit loading state");
    fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                         "redirect status should end in ready state");
    fails += expect_true(strcmp(app->url, "https://example.com/landing") == 0,
                         "redirect status should update current URL");
    fails += expect_true(strcmp(app->final_url, "https://example.com/landing") == 0,
                         "redirect status should record final URL");
    fails += expect_true(app->redirect_count == 1,
                         "redirect status should count one hop");

    if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  }

  return fails;
}

static int test_html_viewer_redirect_limit_fails_controlled(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before redirect-limit test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://loop.test/1");

  fails += expect_true(app->loading == 0,
                       "redirect limit should exit loading state");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_FAILED,
                       "redirect limit should fail in a controlled way");
  fails += expect_true(strcmp(app->last_stage, "redirect") == 0,
                       "redirect limit should expose redirect stage");
  fails += expect_true(strstr(app->last_error_reason, "Too many redirects") != NULL,
                       "redirect limit should expose useful error reason");
  fails += expect_true(app->redirect_count >= 4,
                       "redirect limit should preserve observed redirect count");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_escape_cancels_loading_navigation(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before cancel test");
  fails += expect_true(g_test_window.on_key != NULL,
                       "browser window should register key handler");
  if (!app || !g_test_window.on_key) return fails;

  app->loading = 1;
  app->url_editing = 0;
  app->url_searching = 0;
  app->focused_node_index = -1;
  app->nav_state = HTML_VIEWER_NAV_LOADING;

  g_test_window.on_key(&g_test_window, 0x1B, 0);

  fails += expect_true(app->loading == 0,
                       "Escape should clear loading state");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_CANCELLED,
                       "Escape should move navigation to cancelled state");
  fails += expect_true(strcmp(app->last_stage, "cancelled") == 0,
                       "cancel should expose cancelled stage");
  fails += expect_true(strstr(app->last_error_reason, "cancelled") != NULL,
                       "cancel should expose cancellation reason");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

