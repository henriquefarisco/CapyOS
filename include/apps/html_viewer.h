#ifndef APPS_HTML_VIEWER_H
#define APPS_HTML_VIEWER_H

#include "gui/compositor.h"
#include <stdint.h>

#define HTML_MAX_NODES 256
#define HTML_TEXT_MAX  512
#define HTML_URL_MAX   512

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
  HTML_NODE_TAG_TITLE
};

struct html_node {
  enum html_node_type type;
  char text[128];
  char href[HTML_URL_MAX];
  uint32_t color;
  uint32_t font_size;
  int bold;
};

struct html_document {
  struct html_node nodes[HTML_MAX_NODES];
  int node_count;
  char title[128];
};

struct html_viewer_app {
  struct gui_window *window;
  struct html_document doc;
  char url[HTML_URL_MAX];
  int scroll_offset;
  int loading;
};

void html_viewer_open(void);
int html_parse(const char *html, size_t len, struct html_document *doc);
void html_viewer_navigate(struct html_viewer_app *app, const char *url);
void html_viewer_paint(struct html_viewer_app *app);
void html_viewer_scroll(struct html_viewer_app *app, int delta);

#endif
