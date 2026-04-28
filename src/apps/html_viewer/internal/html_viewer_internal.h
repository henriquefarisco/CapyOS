#ifndef HTML_VIEWER_INTERNAL_H
#define HTML_VIEWER_INTERNAL_H

#include "apps/html_viewer.h"
#include "apps/css_parser.h"
#include "drivers/input/keyboard_layout.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/jpeg_loader.h"
#include "gui/png_loader.h"
#ifndef UNIT_TEST
#include "kernel/scheduler.h"
#include "kernel/worker.h"
#endif
#include "memory/kmem.h"
#include "net/dns_cache.h"
#include "net/http.h"
#include "net/stack.h"
#include "security/tls.h"
#include "util/kstring.h"

#include <stddef.h>

#define HV_NAV_REDIRECT_LIMIT 5
#define HV_HTML_BODY_LIMIT (256 * 1024)
#define HV_TEXT_BODY_LIMIT (128 * 1024)
#define HV_IMAGE_BODY_LIMIT (512 * 1024)
#define HV_EXTERNAL_FETCH_LIMIT 6
#define HV_EXTERNAL_IMAGE_LIMIT 4
#define HV_EXTERNAL_CSS_LIMIT 4
#define HV_ERROR_REASON_MAX 191
#define HV_HTML_SNIFF_LIMIT 8192
#define HV_META_REFRESH_AUTO_LIMIT_SECONDS 5

#define HV_IMAGE_ERROR_UNSUPPORTED 1
#define HV_IMAGE_ERROR_DECODE_FAILED 2
#define HV_IMAGE_ERROR_TOO_LARGE 3

#define HV_HTTP_CACHE_MAX 8
#define HV_HTTP_CACHE_BODY_MAX (256 * 1024)
#define HV_HTTP_CACHE_TOTAL_MAX (512 * 1024)
#define HV_HISTORY_MAX 16
#define HV_BOOKMARK_MAX 32

enum { HTML_VIEWER_HTTP_ERR_TLS = 6 };
enum { HTML_FORM_METHOD_GET = 0, HTML_FORM_METHOD_POST = 1 };
enum {
  HTML_INPUT_TYPE_TEXT = 0,
  HTML_INPUT_TYPE_SEARCH,
  HTML_INPUT_TYPE_TEXTAREA,
  HTML_INPUT_TYPE_HIDDEN,
  HTML_INPUT_TYPE_SUBMIT,
  HTML_INPUT_TYPE_BUTTON,
  HTML_INPUT_TYPE_CHECKBOX,
  HTML_INPUT_TYPE_RADIO
};

#ifndef UNIT_TEST
enum {
  HV_BROWSER_IDLE = 0,
  HV_BROWSER_QUEUED,
  HV_BROWSER_RUNNING,
  HV_BROWSER_READY
};

struct hv_browser_job {
  volatile uint32_t state;
  uint32_t ticket;
  enum http_method method;
  char url[HTML_URL_MAX];
  uint8_t body[HTML_URL_MAX];
  size_t body_len;
  struct html_viewer_app *staged_app;
};
#endif

struct hv_http_cache_entry {
  char url[HTTP_MAX_URL];
  uint8_t *body;
  size_t body_len;
  char content_type[128];
  uint32_t max_age;
  uint32_t age;
};

struct hv_http_cache_stats {
  uint32_t entries;
  size_t total_bytes;
  uint32_t hits;
  uint32_t misses;
  uint32_t stores;
  uint32_t evictions;
  uint32_t expired;
  uint32_t rejected;
};

struct html_viewer_apply_options {
  uint8_t update_window_title;
  uint8_t push_history;
  uint8_t fetch_external_assets;
  uint8_t decode_inline_images;
};

extern struct html_viewer_app g_viewer;
extern int g_viewer_open;
extern volatile uint32_t g_hv_parse_lock;
extern struct hv_http_cache_entry hv_http_cache[HV_HTTP_CACHE_MAX];
extern struct hv_http_cache_stats hv_http_cache_stats;
extern char hv_history[HV_HISTORY_MAX][HTML_URL_MAX];
extern int hv_history_count;
extern int hv_history_cur;
extern int hv_navigating_history;
extern char hv_bookmark_url[HV_BOOKMARK_MAX][HTML_URL_MAX];
extern char hv_bookmark_title[HV_BOOKMARK_MAX][HTML_TITLE_MAX];
extern int hv_bookmark_count;
extern int32_t hv_table_row_y;
extern int32_t hv_table_row_h;
#ifndef UNIT_TEST
extern struct hv_browser_job g_hv_browser_job;
extern int g_hv_browser_worker_bootstrapped;
extern int g_hv_browser_worker_pool;
extern uint32_t g_hv_browser_ticket;
extern uint32_t g_hv_browser_active_ticket;
extern int g_hv_browser_followup_pending;
extern enum http_method g_hv_browser_followup_method;
extern char g_hv_browser_followup_url[HTML_URL_MAX];
extern uint8_t g_hv_browser_followup_body[HTML_URL_MAX];
extern size_t g_hv_browser_followup_body_len;
#endif

int http_last_error(void);
const char *http_error_string(int error);

void hv_doc_release_assets(struct html_document *doc);
void hv_doc_reset(struct html_document *doc);
void hv_http_cache_clear(void);
struct hv_http_cache_entry *hv_http_cache_find(const char *url);
void hv_http_cache_tick(void);
struct hv_http_cache_entry *hv_http_cache_slot(size_t body_len);
void hv_http_cache_stats_get(struct hv_http_cache_stats *out);
uint32_t hv_parse_cache_control_max_age(const char *cc);
void hv_http_cache_store(const char *url, const struct http_response *resp);
void hv_bookmark_add(const char *url, const char *title);
int hv_is_bookmarked(const char *url);
void hv_history_push(const char *url);

int hv_strncmp(const char *a, const char *b, size_t n);
int hv_is_space(char ch);
int hv_streq_ci(const char *a, const char *b);
int hv_contains_ci(const char *text, const char *needle);
int hv_parse_locked(const char *html, size_t len, struct html_document *doc);
void hv_copy_prefix(char *dst, size_t dst_len, const char *src, size_t src_len);
size_t hv_path_directory_length(const char *path);
void hv_trim_text(char *text);
uint8_t hv_parse_form_method(const char *method);
int hv_form_action_matches(const struct html_node *a,
                           const struct html_node *b);
int hv_has_scheme(const char *url);
int hv_resolve_url(const char *base_url, const char *ref,
                   char *out, size_t out_len);
void hv_urlencode_append(char *out, size_t out_len, const char *text);
const char *html_viewer_find_header(const struct http_response *resp,
                                    const char *name);
void hv_text_append_char(char *dst, size_t dst_len, size_t *dst_pos,
                         char ch, int *last_space);
int hv_decode_entity_value(const char *html, size_t len,
                           size_t *consumed, char *out_char);
void hv_append_decoded_text(char *dst, size_t dst_len, size_t *dst_pos,
                            const char *src, size_t src_len,
                            int *last_space);
void hv_read_tag_name(const char *html, size_t len,
                      size_t *pos, char *tag, size_t tag_len);
size_t hv_scan_tag_end(const char *html, size_t len, size_t pos,
                       size_t *tag_end, int *self_closing);
size_t hv_skip_special_tag(const char *html, size_t len, size_t pos);
int hv_extract_attr_value(const char *attrs, size_t len, const char *name,
                          char *out, size_t out_len);
int html_viewer_resolve_document_url(const struct html_viewer_app *app,
                                     const char *ref, char *out,
                                     size_t out_len);
int hv_has_boolean_attr(const char *attrs, size_t len, const char *name);
int hv_token_list_contains_ci(const char *list, const char *needle);
int hv_extract_srcset_first_url(const char *srcset, char *out,
                                size_t out_len);
int hv_image_type_supported_by_decoder(const char *type);
int hv_image_type_is_known_unsupported(const char *type);
int hv_image_body_is_known_unsupported(const uint8_t *body, size_t body_len);

struct html_node *html_push_node(struct html_document *doc);
uint8_t hv_form_input_type(const char *type);
int hv_body_match_ci_at(const uint8_t *body, size_t len, size_t pos,
                        const char *needle);
size_t hv_body_find_ci(const uint8_t *body, size_t len,
                       const char *needle, size_t max_scan);
size_t hv_find_html_start(const uint8_t *body, size_t len);
int hv_body_looks_html(const uint8_t *body, size_t len);
int hv_body_looks_textual(const uint8_t *body, size_t len);
int hv_content_type_is_html(const char *content_type);
int hv_content_type_is_textual(const char *content_type);
int hv_content_type_is_script_or_data(const char *content_type);
int hv_body_looks_script_or_data(const uint8_t *body, size_t len);
int hv_parse_meta_refresh_content(const char *content, uint32_t *out_delay,
                                  char *out_url, size_t out_url_len);
int hv_base64_value(char ch);
uint8_t *hv_base64_decode_alloc(const char *text, size_t len, size_t *out_len);
int hv_decode_data_image(const char *url, struct png_image *img,
                         struct jpeg_image *jimg);

int hv_text_looks_codeish(const char *text);
void hv_simplify_degraded_doc(struct html_document *doc);
void hv_cookie_trim(char *text);
int hv_cookie_domain_matches(const char *cookie_domain, const char *host,
                             int host_only);
int hv_cookie_path_matches(const char *cookie_path, const char *path);
int hv_cookie_find_slot(struct html_viewer_app *app, const char *name,
                        const char *domain, const char *path);
void hv_cookie_remove_index(struct html_viewer_app *app, uint32_t index);
void hv_cookie_default_path(const char *request_path, char *out, size_t out_len);
void hv_store_cookie_from_header(struct html_viewer_app *app, const char *host,
                                 const char *path, int use_tls,
                                 const char *header_value);
int hv_build_cookie_header(struct html_viewer_app *app, const char *host,
                           const char *path, int use_tls,
                           char *out, size_t out_len);
void html_viewer_set_state(struct html_viewer_app *app,
                           enum html_viewer_nav_state state,
                           const char *stage);
void html_viewer_set_error_context(struct html_viewer_app *app,
                                   const char *stage,
                                   const char *reason);
void html_viewer_begin_navigation(struct html_viewer_app *app, const char *url);
int html_viewer_navigation_is_current(const struct html_viewer_app *app,
                                      uint32_t navigation_id);
void html_viewer_cancel_navigation(struct html_viewer_app *app,
                                   const char *reason);
void html_viewer_set_transport_error(struct html_viewer_app *app);
void html_viewer_set_status_error(struct html_viewer_app *app, int status_code);
void html_viewer_set_error(struct html_viewer_app *app, const char *title,
                           const char *message);

void html_viewer_load_text_document(struct html_viewer_app *app,
                                    const char *title, const char *text,
                                    size_t len, uint32_t color);
void html_viewer_load_quick_start(struct html_viewer_app *app);
void html_viewer_load_loading_stub(struct html_viewer_app *app,
                                   const char *target_url);
void html_viewer_load_builtin(struct html_viewer_app *app, const char *url);
void html_viewer_cleanup(void);
void html_viewer_on_close(struct gui_window *win);
void html_viewer_window_paint(struct gui_window *win);
void html_viewer_window_scroll(struct gui_window *win, int32_t delta);
void html_viewer_window_key(struct gui_window *win, uint32_t keycode,
                            uint8_t mods);
void html_viewer_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                              uint8_t buttons);
int32_t html_viewer_render_node(struct gui_surface *surface,
                                const struct font *f,
                                const struct gui_theme_palette *theme,
                                const struct html_node *node,
                                int32_t y, int draw);
int html_viewer_node_margin_top(const struct html_node *node);
void html_viewer_submit_form(struct html_viewer_app *app, int node_index);
int hv_node_is_inline(const struct html_node *node);
int html_viewer_wrap_text_from(struct gui_surface *surface,
                               const struct font *f,
                               int32_t x_left, int32_t x_start,
                               int32_t y, int32_t max_width,
                               const char *text, uint32_t color,
                               int underline, int32_t *out_end_x);
void html_viewer_request_sync(struct html_viewer_app *app,
                              const char *url,
                              enum http_method method,
                              const uint8_t *body,
                              size_t body_len,
                              int depth,
                              uint32_t navigation_id);
int html_viewer_issue_request(struct html_viewer_app *app, const char *url,
                              enum http_method method,
                              const uint8_t *body, size_t body_len,
                              struct http_request *req,
                              struct http_response *resp);
int html_viewer_queue_async_request(struct html_viewer_app *app,
                                    const char *url,
                                    enum http_method method,
                                    const uint8_t *body,
                                    size_t body_len);
void html_viewer_poll_background(struct html_viewer_app *app);
void html_viewer_background_cancel(void);
void html_viewer_capture_cookies(struct html_viewer_app *app,
                                 const struct http_request *req,
                                 const struct http_response *resp);
void html_viewer_apply_response(
    struct html_viewer_app *app, const struct http_request *req,
    const struct http_response *resp,
    const struct html_viewer_apply_options *opts);
int hv_extract_query_param(const char *url, const char *param,
                           char *out, size_t out_len);
void hv_url_decode(char *s);
void html_viewer_request_internal(struct html_viewer_app *app,
                                  const char *url,
                                  enum http_method method,
                                  const uint8_t *body,
                                  size_t body_len,
                                  int depth);
void hv_queue_css_imports(struct html_viewer_app *app,
                          const char *css_url,
                          const uint8_t *body,
                          size_t body_len);
void hv_fetch_external_css(struct html_viewer_app *app);
void hv_prefetch_dns(struct html_viewer_app *app);
void hv_fetch_page_images(struct html_viewer_app *app,
                          int allow_network);

int hv_tag_is_void(const char *tag);
size_t hv_skip_block(const char *html, size_t len, size_t pos, const char *tag);
size_t hv_collect_text_until_tag(const char *html, size_t len, size_t pos,
                                  const char *tag, char *out, size_t out_len);
size_t hv_parse_inline_content(const char *html, size_t len, size_t pos,
                                const char *close_tag, struct html_document *doc,
                                const struct html_node *tmpl);
void hv_apply_node_attrs(struct html_node *node, const char *attrs, size_t len);
int hv_doc_queue_pending_css(struct html_document *doc, const char *url);
extern int g_hv_line_height_px;
int html_viewer_wrap_text(struct gui_surface *surface, const struct font *f,
                          int32_t x, int32_t y, int32_t max_width,
                          const char *text, uint32_t color, int underline);
int hv_wrap_text_scaled(struct gui_surface *surface, const struct font *f,
                        int32_t x, int32_t y, int32_t max_width,
                        const char *text, uint32_t color, int scale);
void hv_draw_border_rect(struct gui_surface *s, int32_t x, int32_t y,
                          int32_t w, int32_t h, int bw, uint32_t color);
uint32_t html_viewer_node_color(const struct gui_theme_palette *theme,
                                const struct html_node *node);
int html_viewer_node_margin_bottom(const struct html_node *node);

#endif
