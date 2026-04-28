#include "internal/html_viewer_internal.h"

#ifndef UNIT_TEST
void html_viewer_load_loading_stub(struct html_viewer_app *app,
                                   const char *target_url) {
  struct html_node *node = NULL;

  if (!app) return;

  hv_doc_reset(&app->doc);
  app->scroll_offset = 0;
  app->focused_node_index = -1;
  html_viewer_set_state(app, HTML_VIEWER_NAV_LOADING, "loading");
  if (target_url && target_url[0]) {
    kstrcpy(app->url, sizeof(app->url), target_url);
    kstrcpy(app->final_url, sizeof(app->final_url), target_url);
  }
  kstrcpy(app->doc.title, sizeof(app->doc.title), "Loading");

  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_H1;
    node->bold = 1;
    kstrcpy(node->text, sizeof(node->text), "Loading");
  }

  node = html_push_node(&app->doc);
  if (node) {
    node->type = HTML_NODE_TAG_P;
    kstrcpy(node->text, sizeof(node->text),
            "The page is being fetched in the background.");
  }

  if (target_url && target_url[0]) {
    node = html_push_node(&app->doc);
    if (node) {
      node->type = HTML_NODE_TAG_P;
      kstrcpy(node->text, sizeof(node->text), target_url);
    }
  }

  if (app->window) compositor_set_title(app->window->id, app->doc.title);
}
#endif
