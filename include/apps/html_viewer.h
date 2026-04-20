#ifndef APPS_HTML_VIEWER_H
#define APPS_HTML_VIEWER_H

#include "gui/compositor.h"
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
  char id[64];
  char class_list[128];
  int indent;
  uint32_t css_color;
  uint32_t css_bg_color;
};

#define HTML_STYLE_BUF_MAX 8192

#define HTML_MAX_PENDING_CSS 6

struct html_document {
  struct html_node nodes[HTML_MAX_NODES];
  int node_count;
  char title[HTML_TITLE_MAX];
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

struct html_viewer_app {
  struct gui_window *window;
  struct html_document doc;
  char url[HTML_URL_MAX];
  int scroll_offset;
  int loading;
  int url_editing;
  int url_cursor;
  int content_height;
  int focused_node_index;
  struct html_cookie cookies[HTML_MAX_COOKIES];
  uint32_t cookie_count;
};

void html_viewer_open(void);
int html_parse(const char *html, size_t len, struct html_document *doc);
void html_viewer_navigate(struct html_viewer_app *app, const char *url);
void html_viewer_paint(struct html_viewer_app *app);
void html_viewer_scroll(struct html_viewer_app *app, int delta);

#endif
