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

static int test_html_viewer_state_machine_helpers(void) {
  int fails = 0;

  fails += expect_true(strcmp(html_viewer_state_name(HTML_VIEWER_NAV_IDLE),
                              "idle") == 0,
                       "state name idle");
  fails += expect_true(strcmp(html_viewer_state_name(HTML_VIEWER_NAV_LOADING),
                              "loading") == 0,
                       "state name loading");
  fails += expect_true(strcmp(html_viewer_state_name(HTML_VIEWER_NAV_READY),
                              "ready") == 0,
                       "state name ready");
  fails += expect_true(strcmp(html_viewer_state_name(HTML_VIEWER_NAV_FAILED),
                              "failed") == 0,
                       "state name failed");
  fails += expect_true(strcmp(html_viewer_state_name(HTML_VIEWER_NAV_CANCELLED),
                              "cancelled") == 0,
                       "state name cancelled");

  fails += expect_true(html_viewer_state_transition_allowed(
                           HTML_VIEWER_NAV_IDLE, HTML_VIEWER_NAV_LOADING) == 1,
                       "idle -> loading allowed");
  fails += expect_true(html_viewer_state_transition_allowed(
                           HTML_VIEWER_NAV_LOADING,
                           HTML_VIEWER_NAV_RENDERING) == 1,
                       "loading -> rendering allowed");
  fails += expect_true(html_viewer_state_transition_allowed(
                           HTML_VIEWER_NAV_RENDERING,
                           HTML_VIEWER_NAV_READY) == 1,
                       "rendering -> ready allowed");
  fails += expect_true(html_viewer_state_transition_allowed(
                           HTML_VIEWER_NAV_READY,
                           HTML_VIEWER_NAV_LOADING) == 1,
                       "ready -> loading allowed (new navigation)");

  fails += expect_true(html_viewer_state_transition_allowed(
                           HTML_VIEWER_NAV_RENDERING,
                           HTML_VIEWER_NAV_CANCELLED) == 1,
                       "rendering -> cancelled allowed");
  fails += expect_true(html_viewer_state_transition_allowed(
                           HTML_VIEWER_NAV_LOADING,
                           HTML_VIEWER_NAV_FAILED) == 1,
                       "loading -> failed allowed");

  fails += expect_true(html_viewer_state_transition_allowed(
                           HTML_VIEWER_NAV_IDLE,
                           HTML_VIEWER_NAV_RENDERING) == 0,
                       "idle -> rendering NOT allowed");
  fails += expect_true(html_viewer_state_transition_allowed(
                           HTML_VIEWER_NAV_LOADING,
                           HTML_VIEWER_NAV_IDLE) == 0,
                       "loading -> idle NOT allowed");
  fails += expect_true(html_viewer_state_transition_allowed(
                           HTML_VIEWER_NAV_READY,
                           HTML_VIEWER_NAV_RENDERING) == 0,
                       "ready -> rendering NOT allowed");
  return fails;
}

static int g_isolation_heartbeat_count = 0;
static int g_isolation_fatal_count = 0;
static enum html_viewer_nav_state g_isolation_last_state = HTML_VIEWER_NAV_IDLE;

static void test_isolation_heartbeat(uint32_t nav_id,
                                      enum html_viewer_nav_state state) {
  (void)nav_id;
  g_isolation_heartbeat_count++;
  g_isolation_last_state = state;
}

static void test_isolation_on_fatal(uint32_t nav_id, const char *stage,
                                     const char *reason) {
  (void)nav_id;
  (void)stage;
  (void)reason;
  g_isolation_fatal_count++;
}

static int test_html_viewer_strict_transition_escalates(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before strict-mode test");
  if (!app) return fails;

  fails += expect_true(html_viewer_state_strict_mode_enabled() == 0,
                       "strict mode is off by default");

  /* Permissive (default): an invalid transition is accepted with a warn. */
  app->nav_state = HTML_VIEWER_NAV_IDLE;
  html_viewer_set_state(app, HTML_VIEWER_NAV_RENDERING, "skip-loading");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_RENDERING,
                       "permissive mode accepts invalid transition");

  /* Strict mode: same invalid transition is escalated to FAILED. */
  html_viewer_state_strict_mode_set(1);
  fails += expect_true(html_viewer_state_strict_mode_enabled() == 1,
                       "strict mode reports as enabled");
  app->nav_state = HTML_VIEWER_NAV_IDLE;
  app->last_error_reason[0] = '\0';
  html_viewer_set_state(app, HTML_VIEWER_NAV_RENDERING, "violation-stage");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_FAILED,
                       "strict mode forces FAILED on invalid transition");
  fails += expect_true(strstr(app->last_error_reason, "strict mode") != NULL,
                       "strict mode publishes a stable reason");
  fails += expect_true(strcmp(app->last_stage, "violation-stage") == 0,
                       "strict mode preserves the violating stage label");

  /* A valid transition still goes through under strict mode. */
  app->nav_state = HTML_VIEWER_NAV_IDLE;
  html_viewer_set_state(app, HTML_VIEWER_NAV_LOADING, "loading");
  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_LOADING,
                       "strict mode admits valid transitions unchanged");

  html_viewer_state_strict_mode_set(0);
  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_isolation_hooks(void) {
  struct html_viewer_app *app = NULL;
  struct html_viewer_isolation_ops ops;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser open for isolation test");
  if (!app) return fails;

  fails += expect_true(html_viewer_isolation_ops() == NULL,
                       "isolation ops null by default");

  g_isolation_heartbeat_count = 0;
  g_isolation_fatal_count = 0;
  g_isolation_last_state = HTML_VIEWER_NAV_IDLE;

  ops.heartbeat = test_isolation_heartbeat;
  ops.on_fatal = test_isolation_on_fatal;
  html_viewer_set_isolation_ops(&ops);
  fails += expect_true(html_viewer_isolation_ops() == &ops,
                       "isolation ops registered");

  html_viewer_set_state(app, HTML_VIEWER_NAV_READY, "ready");
  fails += expect_true(g_isolation_heartbeat_count == 1,
                       "heartbeat fired once on ready");
  fails += expect_true(g_isolation_last_state == HTML_VIEWER_NAV_READY,
                       "heartbeat got ready state");

  html_viewer_set_state(app, HTML_VIEWER_NAV_LOADING, "loading");
  fails += expect_true(g_isolation_heartbeat_count == 1,
                       "heartbeat NOT fired on loading");

  html_viewer_set_state(app, HTML_VIEWER_NAV_FAILED, "transport");
  fails += expect_true(g_isolation_heartbeat_count == 2,
                       "heartbeat fired on failed");
  fails += expect_true(g_isolation_fatal_count == 1,
                       "on_fatal fired once on failed");

  html_viewer_set_isolation_ops(NULL);
  fails += expect_true(html_viewer_isolation_ops() == NULL,
                       "isolation ops cleared");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

