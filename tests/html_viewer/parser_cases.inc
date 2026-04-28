#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/apps/html_viewer/internal/html_viewer_internal.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "kernel/log/klog.h"
#include "net/http.h"
#include "security/tls.h"

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[FAIL] html_viewer: %s\n", msg);
    return 1;
  }
  return 0;
}

static int test_html_viewer_open_quick_start(void);
static int test_html_viewer_http_navigation_finishes_loading(void);
static int test_html_viewer_redirects_update_final_url(void);
static int test_html_viewer_all_common_redirect_statuses(void);
static int test_html_viewer_redirect_limit_fails_controlled(void);
static int test_html_viewer_escape_cancels_loading_navigation(void);
static int test_html_viewer_loads_external_assets(void);
static int test_html_viewer_discards_stale_response(void);
static int test_html_viewer_safe_mode_on_large_document(void);
static int test_html_viewer_rejects_script_document(void);
static int test_html_viewer_extracts_html_after_bootstrap_noise(void);
static int test_html_viewer_degrades_script_only_html_shell(void);
static int test_html_viewer_follows_meta_refresh_redirect(void);
static int test_html_viewer_uses_base_href_for_assets(void);
static int test_html_viewer_marks_unsupported_image_format(void);
static int test_html_viewer_loads_stylesheet_rel_tokens_and_imports(void);
static int test_html_viewer_loads_style_block_imports_once(void);
static int test_html_viewer_http_cache_enforces_memory_budget(void);
static int test_html_viewer_about_metrics_pages(void);
static int test_html_viewer_external_resource_budget_logs_failure(void);
static int test_html_viewer_render_budget_logs_failure(void);
static char g_last_http_host[192];
static char g_last_http_path[768];
static uint16_t g_last_http_port = 0;
static int g_last_http_tls = 0;
static struct html_viewer_app *g_reentrant_app = NULL;
static int g_reentrant_triggered = 0;

static const struct html_node *find_node(enum html_node_type type,
                                         const struct html_document *doc,
                                         const char *text) {
  if (!doc) return NULL;
  for (int i = 0; i < doc->node_count; i++) {
    if (doc->nodes[i].type != type) continue;
    if (!text || strcmp(doc->nodes[i].text, text) == 0) return &doc->nodes[i];
  }
  return NULL;
}

static int document_contains_text(const struct html_document *doc,
                                  const char *fragment) {
  if (!doc || !fragment) return 0;
  for (int i = 0; i < doc->node_count; i++) {
    if (strstr(doc->nodes[i].text, fragment) != NULL) return 1;
  }
  return 0;
}

static int test_html_entities_and_links(void) {
  struct html_document doc;
  const struct html_node *link = NULL;
  const char *html =
      "<html><head><title>Alpha &amp; Beta</title></head>"
      "<body><h1>Alpha &amp; Beta</h1>"
      "<p>One&nbsp;Two &#33;</p>"
      "<a href=\"/search?q=capy\"><span>Search&nbsp;Now</span></a>"
      "</body></html>";
  int fails = 0;

  html_parse(html, strlen(html), &doc);
  fails += expect_true(strcmp(doc.title, "Alpha & Beta") == 0,
                       "title should decode entities");
  fails += expect_true(find_node(HTML_NODE_TAG_H1, &doc, "Alpha & Beta") != NULL,
                       "h1 should decode entities");
  fails += expect_true(find_node(HTML_NODE_TAG_P, &doc, "One Two !") != NULL,
                       "paragraph should decode nbsp and numeric entities");
  link = find_node(HTML_NODE_TAG_A, &doc, "Search Now");
  fails += expect_true(link != NULL, "nested link text should be extracted");
  fails += expect_true(link && strcmp(link->href, "/search?q=capy") == 0,
                       "href should be preserved");
  return fails;
}

static int test_html_skips_script_and_keeps_noscript(void) {
  struct html_document doc;
  int fails = 0;
  const char *html =
      "<body><script>bad()</script>"
      "<noscript>JS disabled</noscript>"
      "<ul><li>First</li><li>Second</li></ul>"
      "<img alt=\"Capy logo\" src=\"/capy.png\"></body>";

  html_parse(html, strlen(html), &doc);
  fails += expect_true(find_node(HTML_NODE_TAG_DIV, &doc, "JS disabled") != NULL,
                       "noscript content should remain visible");
  fails += expect_true(find_node(HTML_NODE_TAG_LI, &doc, "First") != NULL,
                       "first list item should be parsed");
  fails += expect_true(find_node(HTML_NODE_TAG_LI, &doc, "Second") != NULL,
                       "second list item should be parsed");
  fails += expect_true(find_node(HTML_NODE_TAG_IMG, &doc, "Capy logo") != NULL,
                       "image alt text should be exposed");
  for (int i = 0; i < doc.node_count; i++) {
    fails += expect_true(strstr(doc.nodes[i].text, "bad()") == NULL,
                         "script content should not leak into the document");
  }
  return fails;
}

static int test_html_parses_basic_forms(void) {
  struct html_document doc;
  const struct html_node *search = NULL;
  const struct html_node *notes = NULL;
  const struct html_node *hidden = NULL;
  const struct html_node *button = NULL;
  int fails = 0;
  const char *html =
      "<body><form action=\"/search\" method=\"get\">"
      "<input type=\"hidden\" name=\"source\" value=\"capy\">"
      "<input type=\"search\" name=\"q\" value=\"hello\">"
      "<textarea name=\"notes\">caps lock off</textarea>"
      "<button type=\"submit\">Go</button>"
      "</form></body>";

  html_parse(html, strlen(html), &doc);
  search = find_node(HTML_NODE_TAG_INPUT, &doc, "hello");
  notes = find_node(HTML_NODE_TAG_INPUT, &doc, "caps lock off");
  hidden = find_node(HTML_NODE_TAG_INPUT, &doc, "capy");
  button = find_node(HTML_NODE_TAG_BUTTON, &doc, "Go");

  fails += expect_true(search != NULL, "search input should be parsed");
  fails += expect_true(search && strcmp(search->name, "q") == 0,
                       "search input name should be preserved");
  fails += expect_true(search && strcmp(search->href, "/search") == 0,
                       "form action should be copied to input");
  fails += expect_true(notes != NULL, "textarea should be parsed as an editable control");
  fails += expect_true(notes && strcmp(notes->name, "notes") == 0,
                       "textarea name should be preserved");
  fails += expect_true(notes && strcmp(notes->href, "/search") == 0,
                       "textarea should use the form action");
  fails += expect_true(hidden != NULL && hidden->hidden,
                       "hidden input should be preserved for submission");
  fails += expect_true(button != NULL, "submit button should be parsed");
  fails += expect_true(button && strcmp(button->href, "/search") == 0,
                       "submit button should use form action");
  return fails;
}

static int test_html_handles_boolean_attributes(void) {
  struct html_document doc;
  const struct html_node *details = NULL;
  const struct html_node *checkbox = NULL;
  int fails = 0;
  const char *html =
      "<body>"
      "<details open><summary>Status</summary>Ready</details>"
      "<form action=\"/toggle\">"
      "<input type=\"checkbox\" name=\"enabled\" checked value=\"yes\">"
      "</form>"
      "</body>";

  html_parse(html, strlen(html), &doc);
  details = find_node(HTML_NODE_TAG_DETAILS, &doc, NULL);
  checkbox = find_node(HTML_NODE_TAG_INPUT, &doc, "yes");

  fails += expect_true(details != NULL, "details node should be parsed");
  fails += expect_true(details && strstr(details->text, "Status") != NULL,
                       "details summary text should be preserved");
  fails += expect_true(details && details->open,
                       "boolean open attribute should be preserved");
  fails += expect_true(checkbox != NULL, "checkbox input should be parsed");
  fails += expect_true(checkbox && checkbox->open,
                       "boolean checked attribute should be preserved");
  return fails;
}

static int test_html_parses_lazy_image_sources(void) {
  struct html_document doc;
  const struct html_node *data_src = NULL;
  const struct html_node *srcset = NULL;
  const struct html_node *picture = NULL;
  int fails = 0;
  const char *html =
      "<body>"
      "<img alt=\"Lazy\" data-src=\"/lazy.png\">"
      "<img alt=\"Responsive\" srcset=\"/small.jpg 1x, /large.jpg 2x\">"
      "<picture>"
      "<source type=\"image/avif\" srcset=\"/modern.avif\">"
      "<source type=\"image/png\" srcset=\"/fallback.png 1x\">"
      "<img alt=\"Picture fallback\">"
      "</picture>"
      "</body>";

  html_parse(html, strlen(html), &doc);
  data_src = find_node(HTML_NODE_TAG_IMG, &doc, "Lazy");
  srcset = find_node(HTML_NODE_TAG_IMG, &doc, "Responsive");
  picture = find_node(HTML_NODE_TAG_IMG, &doc, "Picture fallback");

  fails += expect_true(data_src != NULL, "data-src image should be parsed");
  fails += expect_true(data_src && strcmp(data_src->href, "/lazy.png") == 0,
                       "data-src should become the image fetch URL");
  fails += expect_true(srcset != NULL, "srcset image should be parsed");
  fails += expect_true(srcset && strcmp(srcset->href, "/small.jpg") == 0,
                       "first srcset candidate should become the fallback image URL");
  fails += expect_true(picture != NULL, "picture fallback image should be parsed");
  fails += expect_true(picture && strcmp(picture->href, "/fallback.png") == 0,
                       "picture should choose the first supported source fallback");
  return fails;
}
