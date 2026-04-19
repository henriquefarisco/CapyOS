#include <stdio.h>
#include <string.h>

#include "apps/html_viewer.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "net/http.h"
#include "security/tls.h"

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[FAIL] html_viewer: %s\n", msg);
    return 1;
  }
  return 0;
}

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
  const struct html_node *hidden = NULL;
  const struct html_node *button = NULL;
  int fails = 0;
  const char *html =
      "<body><form action=\"/search\" method=\"get\">"
      "<input type=\"hidden\" name=\"source\" value=\"capy\">"
      "<input type=\"search\" name=\"q\" value=\"hello\">"
      "<button type=\"submit\">Go</button>"
      "</form></body>";

  html_parse(html, strlen(html), &doc);
  search = find_node(HTML_NODE_TAG_INPUT, &doc, "hello");
  hidden = find_node(HTML_NODE_TAG_INPUT, &doc, "capy");
  button = find_node(HTML_NODE_TAG_BUTTON, &doc, "Go");

  fails += expect_true(search != NULL, "search input should be parsed");
  fails += expect_true(search && strcmp(search->name, "q") == 0,
                       "search input name should be preserved");
  fails += expect_true(search && strcmp(search->href, "/search") == 0,
                       "form action should be copied to input");
  fails += expect_true(hidden != NULL && hidden->hidden,
                       "hidden input should be preserved for submission");
  fails += expect_true(button != NULL, "submit button should be parsed");
  fails += expect_true(button && strcmp(button->href, "/search") == 0,
                       "submit button should use form action");
  return fails;
}

int run_html_viewer_tests(void) {
  int fails = 0;
  fails += test_html_entities_and_links();
  fails += test_html_skips_script_and_keeps_noscript();
  fails += test_html_parses_basic_forms();
  if (fails == 0) printf("[PASS] html_viewer\n");
  return fails;
}

static struct gui_window g_test_window;
static struct gui_theme_palette g_test_theme = {
    0, 0, 0, 0, 0, 0xFFFFFF, 0xAAAAAA, 0x33AAFF, 0x6699FF, 0xFFFFFF,
    0, 0, 0, 0, 0, 1};
static struct font g_test_font = {NULL, 8, 16, 32, 255, 16};

void compositor_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
                     uint32_t pitch) {
  (void)framebuffer;
  (void)width;
  (void)height;
  (void)pitch;
}
void compositor_shutdown(void) {}
struct gui_window *compositor_create_window(const char *title, int32_t x,
                                            int32_t y, uint32_t w, uint32_t h) {
  (void)title;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  memset(&g_test_window, 0, sizeof(g_test_window));
  return &g_test_window;
}
void compositor_destroy_window(uint32_t window_id) { (void)window_id; }
void compositor_show_window(uint32_t window_id) { (void)window_id; }
void compositor_hide_window(uint32_t window_id) { (void)window_id; }
void compositor_focus_window(uint32_t window_id) { (void)window_id; }
void compositor_move_window(uint32_t window_id, int32_t x, int32_t y) {
  (void)window_id;
  (void)x;
  (void)y;
}
void compositor_resize_window(uint32_t window_id, uint32_t w, uint32_t h) {
  (void)window_id;
  (void)w;
  (void)h;
}
void compositor_set_title(uint32_t window_id, const char *title) {
  (void)window_id;
  (void)title;
}
void compositor_invalidate(uint32_t window_id) { (void)window_id; }
void compositor_invalidate_rect(uint32_t window_id, struct gui_rect *rect) {
  (void)window_id;
  (void)rect;
}
void compositor_render(void) {}
void compositor_render_cursor(int32_t x, int32_t y) {
  (void)x;
  (void)y;
}
struct gui_window *compositor_window_at(int32_t x, int32_t y) {
  (void)x;
  (void)y;
  return NULL;
}
struct gui_window *compositor_focused_window(void) { return NULL; }
void compositor_stats_get(struct compositor_stats *out) { (void)out; }
void compositor_set_wallpaper(uint32_t color) { (void)color; }
void compositor_apply_theme(const char *theme, uint32_t screen_w, uint32_t screen_h) {
  (void)theme;
  (void)screen_w;
  (void)screen_h;
}
const struct gui_theme_palette *compositor_theme(void) { return &g_test_theme; }
uint8_t compositor_ui_scale(void) { return 1; }
void compositor_set_desktop_callback(void (*callback)(struct gui_surface *)) {
  (void)callback;
}
int compositor_hit_close_button(struct gui_window *win, int32_t x, int32_t y) {
  (void)win;
  (void)x;
  (void)y;
  return 0;
}

void font_init(void) {}
const struct font *font_default(void) { return &g_test_font; }
void font_draw_char(struct gui_surface *surface, const struct font *f,
                    int32_t x, int32_t y, char c, uint32_t color) {
  (void)surface;
  (void)f;
  (void)x;
  (void)y;
  (void)c;
  (void)color;
}
void font_draw_string(struct gui_surface *surface, const struct font *f,
                      int32_t x, int32_t y, const char *text, uint32_t color) {
  (void)surface;
  (void)f;
  (void)x;
  (void)y;
  (void)text;
  (void)color;
}
uint32_t font_string_width(const struct font *f, const char *text) {
  return (uint32_t)((text ? strlen(text) : 0u) * (f ? f->glyph_width : 8u));
}
uint32_t font_string_height(const struct font *f, const char *text) {
  uint32_t lines = 1;
  if (text) {
    for (const char *p = text; *p; ++p) {
      if (*p == '\n') lines++;
    }
  }
  return lines * (f ? f->glyph_height : 16u);
}
void font_metrics_get(const struct font *f, struct font_metrics *out) {
  if (!out) return;
  out->ascent = f ? f->glyph_height - 2u : 14u;
  out->descent = 2u;
  out->line_height = f ? f->glyph_height : 16u;
  out->avg_width = f ? f->glyph_width : 8u;
}

int http_init(void) { return 0; }
int http_request(const struct http_request *req, struct http_response *resp) {
  (void)req;
  (void)resp;
  return -1;
}
int http_get(const char *url, struct http_response *resp) {
  (void)url;
  (void)resp;
  return -1;
}
int http_download(const char *url, uint8_t *buffer, size_t buffer_size,
                  size_t *out_len) {
  (void)url;
  (void)buffer;
  (void)buffer_size;
  (void)out_len;
  return -1;
}
int http_parse_url(const char *url, char *host, size_t host_len,
                   char *path, size_t path_len, uint16_t *port, int *use_tls) {
  (void)url;
  if (host && host_len) host[0] = '\0';
  if (path && path_len) path[0] = '\0';
  if (port) *port = 0;
  if (use_tls) *use_tls = 0;
  return -1;
}
void http_response_free(struct http_response *resp) { (void)resp; }
int http_last_error(void) { return 0; }
const char *http_error_string(int error) {
  (void)error;
  return "ok";
}

int tls_init(void) { return 0; }
struct tls_context *tls_connect(int socket_fd, const char *hostname,
                                const struct tls_config *config) {
  (void)socket_fd;
  (void)hostname;
  (void)config;
  return NULL;
}
int tls_send(struct tls_context *ctx, const void *data, size_t len) {
  (void)ctx;
  (void)data;
  (void)len;
  return -1;
}
int tls_recv(struct tls_context *ctx, void *buf, size_t len) {
  (void)ctx;
  (void)buf;
  (void)len;
  return -1;
}
int tls_close(struct tls_context *ctx) {
  (void)ctx;
  return 0;
}
void tls_free(struct tls_context *ctx) { (void)ctx; }
int tls_handshake(struct tls_context *ctx) {
  (void)ctx;
  return -1;
}
const char *tls_state_name(enum tls_state state) {
  (void)state;
  return "init";
}
enum tls_state tls_last_state(void) { return TLS_STATE_INIT; }
int tls_last_error(void) { return 0; }
const char *tls_alert_name(int alert) {
  (void)alert;
  return "ok";
}
int tls_error(struct tls_context *ctx) {
  (void)ctx;
  return 0;
}
int tls_get_security_info(struct tls_context *ctx, struct tls_security_info *info) {
  (void)ctx;
  (void)info;
  return -1;
}
int tls_get_last_security_info(struct tls_security_info *info) {
  (void)info;
  return -1;
}
const char *tls_version_name(uint16_t version) {
  (void)version;
  return "TLS";
}
const char *tls_cipher_suite_name(uint16_t suite) {
  (void)suite;
  return "cipher";
}
