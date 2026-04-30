#ifndef APPS_HTML_VIEWER_H
#define APPS_HTML_VIEWER_H

#include "gui/compositor.h"
#include "util/op_budget.h"
#include <stdint.h>

#define HTML_MAX_NODES 512
#define HTML_TEXT_MAX  512
#define HTML_TITLE_MAX 192
#define HTML_URL_MAX   768
#define HTML_MAX_COOKIES 24
#define HTML_COOKIE_NAME_MAX 64
#define HTML_COOKIE_VALUE_MAX 256
#define HTML_COOKIE_DOMAIN_MAX 160
#define HTML_COOKIE_PATH_MAX 256

enum html_node_type {
  HTML_NODE_TEXT = 0,
  HTML_NODE_TAG_P,
  HTML_NODE_TAG_H1,
  HTML_NODE_TAG_H2,
  HTML_NODE_TAG_H3,
  HTML_NODE_TAG_A,
  HTML_NODE_TAG_DIV,
  HTML_NODE_TAG_SPAN,
  HTML_NODE_TAG_BR,
  HTML_NODE_TAG_IMG,
  HTML_NODE_TAG_UL,
  HTML_NODE_TAG_LI,
  HTML_NODE_TAG_BODY,
  HTML_NODE_TAG_HTML,
  HTML_NODE_TAG_HEAD,
  HTML_NODE_TAG_TITLE,
  HTML_NODE_TAG_INPUT,
  HTML_NODE_TAG_BUTTON,
  HTML_NODE_TAG_H4,
  HTML_NODE_TAG_H5,
  HTML_NODE_TAG_H6,
  HTML_NODE_TAG_PRE,
  HTML_NODE_TAG_CODE,
  HTML_NODE_TAG_BLOCKQUOTE,
  HTML_NODE_TAG_HR,
  HTML_NODE_TAG_MARK,
  HTML_NODE_TAG_TD,
  HTML_NODE_TAG_TR,
  HTML_NODE_TAG_MEDIA,
  HTML_NODE_TAG_FIGCAPTION,
  HTML_NODE_TAG_DETAILS
};

struct html_node {
  enum html_node_type type;
  char text[HTML_TEXT_MAX];
  char href[HTML_URL_MAX];
  char name[HTML_COOKIE_NAME_MAX];
  uint32_t color;
  uint32_t font_size;
  int bold;
  uint8_t form_method;
  uint8_t input_type;
  uint8_t hidden;
  uint8_t text_align; /* 0=left, 1=center, 2=right */
  uint8_t col_index;  /* column index within a table row (0-based) */
  uint8_t col_count;  /* total columns in the row (0 = not in a table) */
  uint8_t no_underline; /* CSS text-decoration: none */
  uint8_t list_style_none; /* CSS list-style-type: none */
  uint8_t open; /* <details open> or toggled open by click */
  char placeholder[64]; /* <input placeholder="..."> hint text */
  uint16_t css_max_width; /* max-width in px; 0 = unset */
  uint16_t css_width;     /* width in px; 0 = unset */
  char id[64];
  char class_list[128];
  int indent;
  uint32_t *image_pixels;
  uint16_t image_width;
  uint16_t image_height;
  uint8_t image_error; /* 0=none, 1=unsupported, 2=decode failed, 3=too large */
  uint32_t css_color;
  uint32_t css_bg_color;
  uint8_t css_margin_top;    /* margin-top in px; 0 = use default */
  uint8_t css_margin_bottom; /* margin-bottom in px; 0 = use default */
  uint8_t css_border_width;  /* border width in px; 0 = none */
  uint32_t css_border_color; /* border color (0x00RRGGBB); 0 = theme default */
  uint8_t css_text_transform; /* 0=none, 1=uppercase, 2=lowercase, 3=capitalize */
  uint8_t css_line_height; /* line-height in px (0 = use font default ~18px) */
  uint8_t css_display;     /* 0=block(default), 1=inline, 2=inline-block, 3=flex */
  /* Inline layout bounds set during paint; used by click hit-test (0 = block node) */
  int32_t il_x_left;
  int32_t il_x_right;
};

#define HTML_STYLE_BUF_MAX 8192

#define HTML_MAX_PENDING_CSS 6

struct html_document {
  struct html_node nodes[HTML_MAX_NODES];
  int node_count;
  char title[HTML_TITLE_MAX];
  char base_url[HTML_URL_MAX];
  char meta_refresh_url[HTML_URL_MAX];
  uint32_t meta_refresh_delay;
  char style_text[HTML_STYLE_BUF_MAX];
  char pending_css[HTML_MAX_PENDING_CSS][HTML_URL_MAX];
  int css_count;
};

struct html_cookie {
  char name[HTML_COOKIE_NAME_MAX];
  char value[HTML_COOKIE_VALUE_MAX];
  char domain[HTML_COOKIE_DOMAIN_MAX];
  char path[HTML_COOKIE_PATH_MAX];
  uint8_t secure;
  uint8_t host_only;
};

enum html_viewer_nav_state {
  HTML_VIEWER_NAV_IDLE = 0,
  HTML_VIEWER_NAV_LOADING,
  HTML_VIEWER_NAV_REDIRECTING,
  HTML_VIEWER_NAV_RENDERING,
  HTML_VIEWER_NAV_READY,
  HTML_VIEWER_NAV_FAILED,
  HTML_VIEWER_NAV_CANCELLED
};

/* Stable string label for a navigation state. Used by the audit trail
 * and the about:status-like diagnostic surfaces. */
const char *html_viewer_state_name(enum html_viewer_nav_state state);

/* Returns 1 if the given transition is allowed by the browser state
 * machine. Today this is permissive (every state accepts CANCELLED and
 * FAILED, plus the natural progression) so we can record violations
 * without breaking existing callers; a future tightening can flip it. */
int html_viewer_state_transition_allowed(enum html_viewer_nav_state from,
                                         enum html_viewer_nav_state to);

/* Strict transition mode. When enabled, an invalid transition (as judged by
 * html_viewer_state_transition_allowed) is escalated to a hard navigation
 * failure: the navigation is forced into HTML_VIEWER_NAV_FAILED with a
 * descriptive reason instead of being silently accepted. The default is
 * permissive (warn-only) so existing call sites do not break; tests and
 * release builds can opt into strict mode after every call site has been
 * audited. */
void html_viewer_state_strict_mode_set(int enabled);
int  html_viewer_state_strict_mode_enabled(void);

/* Isolation hook callbacks. The browser does not own a separate process
 * yet, but every concrete app already touches process-scoped state
 * (cookies, in-flight requests, render budget). Once M4 lands a real
 * process, a supervisor registers these to coordinate kill/restart and
 * heartbeats. For now they are no-ops; the wiring stays in place so that
 * future supervision does not need to refactor every call site. */
struct html_viewer_isolation_ops {
    /* Called whenever the browser enters a stable state (READY, FAILED,
     * CANCELLED, or IDLE). The supervisor uses this as a heartbeat. */
    void (*heartbeat)(uint32_t navigation_id,
                      enum html_viewer_nav_state state);
    /* Called when the browser detects an unrecoverable error (budget
     * exhaustion, repeated transport failure, parse OOM). The supervisor
     * may decide to kill/restart the isolated process. */
    void (*on_fatal)(uint32_t navigation_id, const char *stage,
                     const char *reason);
};

void html_viewer_set_isolation_ops(const struct html_viewer_isolation_ops *ops);
const struct html_viewer_isolation_ops *html_viewer_isolation_ops(void);

struct html_viewer_app {
  struct gui_window *window;
  struct html_document doc;
  char url[HTML_URL_MAX];
  char final_url[HTML_URL_MAX];
  int scroll_offset;
  int loading;
  int url_editing;
  int url_cursor;
  int content_height;
  int focused_node_index;
  struct html_cookie cookies[HTML_MAX_COOKIES];
  uint32_t cookie_count;
  int url_searching;       /* 1 = find-in-page bar active */
  char search_query[128];  /* current find query */
  int search_cursor;       /* cursor position in search query */
  uint8_t background_mode;
  uint8_t safe_mode;
  uint8_t redirect_count;
  uint8_t external_css_loaded;
  uint8_t external_images_loaded;
  uint8_t external_fetch_attempts;
  uint8_t resource_budget_exhausted;
  uint16_t render_nodes_visited;
  uint8_t render_budget_exhausted;
  uint16_t parse_nodes_visited;
  uint8_t parse_budget_exhausted;
  uint32_t navigation_id;
  uint32_t active_navigation_id;
  enum html_viewer_nav_state nav_state;
  char last_stage[32];
  char last_error_reason[192];
  /* Navigation-level cooperative budget. All per-phase budgets (parse,
   * render, external resource) consult this and forward exhaustion via
   * op_budget_exhaust(). External actors (Esc-cancel, supervisor) can
   * call op_budget_cancel() on it to abort the navigation cleanly: inner
   * loops that consult op_budget_is_blocked() drop out without forcing a
   * crash. This is the single source of truth for "is the current
   * navigation still alive?". */
  struct op_budget nav_op_budget;
};

void html_viewer_open(void);
int html_parse(const char *html, size_t len, struct html_document *doc);
void html_viewer_navigate(struct html_viewer_app *app, const char *url);
void html_viewer_paint(struct html_viewer_app *app);
void html_viewer_scroll(struct html_viewer_app *app, int delta);

#endif
