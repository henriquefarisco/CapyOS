static int test_html_viewer_uses_base_href_for_assets(void) {
  struct html_viewer_app *app = NULL;
  const struct html_node *image = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before base-href test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://example.com/base");

  image = find_node(HTML_NODE_TAG_IMG, &app->doc, "Base Pixel");

  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "base-href page should end in ready state");
  fails += expect_true(strcmp(app->doc.base_url, "https://cdnbase.test/assets/") == 0,
                       "base href should be recorded in the parsed document");
  fails += expect_true(app->external_css_loaded > 0,
                       "relative stylesheet should resolve through base href");
  fails += expect_true(app->external_images_loaded > 0,
                       "relative image should resolve through base href");
  fails += expect_true(image != NULL, "base-href page should keep image node");
  fails += expect_true(image && strcmp(image->href, "https://cdnbase.test/assets/pixel.png") == 0,
                       "image URL should be normalized through base href");
  fails += expect_true(image && image->image_pixels != NULL,
                       "base-href image should decode into pixels");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_marks_unsupported_image_format(void) {
  struct html_viewer_app *app = NULL;
  const struct html_node *image = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before unsupported-image test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://example.com/webp");

  image = find_node(HTML_NODE_TAG_IMG, &app->doc, "Modern Image");

  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "unsupported image page should still render document");
  fails += expect_true(image != NULL, "unsupported image should keep image node");
  fails += expect_true(image && image->image_pixels == NULL,
                       "unsupported image should not expose decoded pixels");
  fails += expect_true(image && image->image_error == 1,
                       "unsupported WebP image should be marked explicitly");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_loads_stylesheet_rel_tokens_and_imports(void) {
  struct html_viewer_app *app = NULL;
  const struct html_node *paragraph = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before imported-css test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://example.com/imported-css");

  paragraph = find_node(HTML_NODE_TAG_P, &app->doc, "Imported theme paragraph.");

  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "imported CSS page should render successfully");
  fails += expect_true(app->doc.css_count >= 2,
                       "preload-as-style link and @import should both be queued");
  fails += expect_true(app->external_css_loaded >= 2,
                       "root stylesheet and imported stylesheet should both load");
  fails += expect_true(paragraph != NULL,
                       "imported CSS page should keep paragraph node");
  fails += expect_true(app->doc.css_count >= 2 &&
                       strcmp(app->doc.pending_css[1],
                              "https://assets.test/import-theme.css") == 0,
                       "relative @import should resolve against the CSS file URL");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}

static int test_html_viewer_loads_style_block_imports_once(void) {
  struct html_viewer_app *app = NULL;
  int fails = 0;

  memset(&g_test_window, 0, sizeof(g_test_window));
  html_viewer_open();
  app = (struct html_viewer_app *)g_test_window.user_data;
  fails += expect_true(app != NULL, "browser should open before style-import test");
  if (!app) return fails;

  html_viewer_navigate(app, "https://example.com/style-import");

  fails += expect_true(app->nav_state == HTML_VIEWER_NAV_READY,
                       "style import page should render successfully");
  fails += expect_true(app->doc.css_count == 1,
                       "duplicate style-block imports should be deduplicated");
  fails += expect_true(strcmp(app->doc.pending_css[0],
                              "https://assets.test/import-theme.css") == 0,
                       "style-block import should resolve as a pending CSS URL");
  fails += expect_true(app->external_css_loaded == 1,
                       "style-block import should be fetched once");

  if (g_test_window.on_close) g_test_window.on_close(&g_test_window);
  return fails;
}
