#include "internal/html_viewer_internal.h"

static void html_viewer_find_next_match(struct html_viewer_app *app) {
  const struct font *font = NULL;
  const struct gui_theme_palette *theme = NULL;
  int32_t ny = 28;
  int found = 0;

  if (!app || !app->search_query[0] || !app->window) return;
  font = font_default();
  theme = compositor_theme();
  if (!font || !theme) return;

  for (int i = 0; i < app->doc.node_count; i++) {
    struct html_node *node = &app->doc.nodes[i];
    int32_t node_top;
    int32_t viewport;
    int new_scroll;
    int max_scroll;

    if (node->hidden) {
      ny = html_viewer_render_node(NULL, font, theme, node, ny, 0);
      continue;
    }
    node_top = ny + html_viewer_node_margin_top(node);
    ny = html_viewer_render_node(NULL, font, theme, node, ny, 0);
    if (!hv_contains_ci(node->text, app->search_query)) continue;

    viewport = (int)app->window->surface.height - 32;
    new_scroll = node_top - 28;
    if (new_scroll < 0) new_scroll = 0;
    max_scroll = app->content_height - viewport;
    if (max_scroll < 0) max_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;
    if (new_scroll > app->scroll_offset + 4 || new_scroll < app->scroll_offset) {
      app->scroll_offset = new_scroll;
      found = 1;
      break;
    }
    found = 1;
  }
  (void)found;
}

static int html_viewer_handle_search_input(struct gui_window *win,
                                           struct html_viewer_app *app,
                                           uint32_t keycode,
                                           char ch) {
  if (!app->url_searching) return 0;

  if (ch == 0x1B || keycode == 0x1B) {
    app->url_searching = 0;
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch == '\b') {
    int qlen = (int)kstrlen(app->search_query);
    if (qlen > 0) {
      app->search_query[qlen - 1] = '\0';
      app->search_cursor--;
    }
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch >= 0x20 && ch < 0x7F) {
    int qlen = (int)kstrlen(app->search_query);
    if (qlen + 1 < (int)sizeof(app->search_query)) {
      app->search_query[qlen] = ch;
      app->search_query[qlen + 1] = '\0';
      app->search_cursor++;
    }
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch == '\n' || ch == '\r' || keycode == KEY_F3) {
    html_viewer_find_next_match(app);
    compositor_invalidate(win->id);
    return 1;
  }
  return 1;
}

static int html_viewer_handle_focused_input(struct gui_window *win,
                                            struct html_viewer_app *app,
                                            char ch) {
  struct html_node *node = NULL;

  if (!app || app->url_editing) return 0;
  if (app->focused_node_index < 0 || app->focused_node_index >= app->doc.node_count) {
    return 0;
  }
  node = &app->doc.nodes[app->focused_node_index];
  if (node->type != HTML_NODE_TAG_INPUT || node->hidden) return 0;

  if (ch == ' ' && (node->input_type == HTML_INPUT_TYPE_CHECKBOX ||
                    node->input_type == HTML_INPUT_TYPE_RADIO)) {
    if (node->input_type == HTML_INPUT_TYPE_CHECKBOX) {
      node->open = node->open ? 0 : 1;
    } else {
      for (int i = 0; i < app->doc.node_count; i++) {
        struct html_node *radio = &app->doc.nodes[i];
        if (radio->type == HTML_NODE_TAG_INPUT &&
            radio->input_type == HTML_INPUT_TYPE_RADIO &&
            kstreq(radio->name, node->name)) {
          radio->open = 0;
        }
      }
      node->open = 1;
    }
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch == '\n' || ch == '\r') {
    if (node->input_type == HTML_INPUT_TYPE_TEXTAREA) {
      size_t len = kstrlen(node->text);
      if (len + 1 < sizeof(node->text)) {
        node->text[len] = '\n';
        node->text[len + 1] = '\0';
      }
      compositor_invalidate(win->id);
      return 1;
    }
    html_viewer_submit_form(app, app->focused_node_index);
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch == 0x1B) {
    app->focused_node_index = -1;
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch == '\b') {
    size_t len = kstrlen(node->text);
    if (len > 0) node->text[len - 1] = '\0';
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch >= 32 && ch < 127) {
    size_t len = kstrlen(node->text);
    if (len + 1 < sizeof(node->text)) {
      node->text[len] = ch;
      node->text[len + 1] = '\0';
    }
    compositor_invalidate(win->id);
    return 1;
  }
  return 0;
}

static void html_viewer_cycle_focus(struct html_viewer_app *app) {
  int start = app->focused_node_index + 1;
  int found = -1;

  for (int i = start; i < app->doc.node_count; i++) {
    if ((app->doc.nodes[i].type == HTML_NODE_TAG_INPUT ||
         app->doc.nodes[i].type == HTML_NODE_TAG_BUTTON) &&
        !app->doc.nodes[i].hidden) {
      found = i;
      break;
    }
  }
  if (found < 0) {
    for (int i = 0; i < app->doc.node_count; i++) {
      if ((app->doc.nodes[i].type == HTML_NODE_TAG_INPUT ||
           app->doc.nodes[i].type == HTML_NODE_TAG_BUTTON) &&
          !app->doc.nodes[i].hidden) {
        found = i;
        break;
      }
    }
  }
  if (found >= 0) app->focused_node_index = found;
}

static int html_viewer_handle_url_editing(struct gui_window *win,
                                          struct html_viewer_app *app,
                                          uint32_t keycode,
                                          char ch) {
  if (!app->url_editing) return 0;

  if (keycode == KEY_LEFT) {
    if (app->url_cursor > 0) app->url_cursor--;
    compositor_invalidate(win->id);
    return 1;
  }
  if (keycode == KEY_RIGHT) {
    int len = (int)kstrlen(app->url);
    if (app->url_cursor < len) app->url_cursor++;
    compositor_invalidate(win->id);
    return 1;
  }
  if (keycode == KEY_HOME) {
    app->url_cursor = 0;
    compositor_invalidate(win->id);
    return 1;
  }
  if (keycode == KEY_END) {
    app->url_cursor = (int)kstrlen(app->url);
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch == '\n' || ch == '\r') {
    app->url_editing = 0;
    html_viewer_navigate(app, app->url);
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch == 0x1B) {
    app->url_editing = 0;
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch == '\b') {
    if (app->url_cursor > 0) {
      int len = (int)kstrlen(app->url);
      for (int i = app->url_cursor - 1; i < len; i++) {
        app->url[i] = app->url[i + 1];
      }
      app->url_cursor--;
    }
    compositor_invalidate(win->id);
    return 1;
  }
  if (ch >= 32 && ch < 127) {
    int len = (int)kstrlen(app->url);
    if (len < HTML_URL_MAX - 1) {
      for (int i = len; i >= app->url_cursor; i--) {
        app->url[i + 1] = app->url[i];
      }
      app->url[app->url_cursor] = ch;
      app->url_cursor++;
    }
    compositor_invalidate(win->id);
    return 1;
  }
  return 0;
}

void html_viewer_on_close(struct gui_window *win) {
  (void)win;
  html_viewer_cleanup();
}

void html_viewer_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  html_viewer_paint((struct html_viewer_app *)win->user_data);
}

void html_viewer_window_scroll(struct gui_window *win, int32_t delta) {
  if (!win || !win->user_data) return;
  html_viewer_scroll((struct html_viewer_app *)win->user_data, -delta);
}

void html_viewer_window_key(struct gui_window *win, uint32_t keycode,
                            uint8_t mods) {
  struct html_viewer_app *app = NULL;
  char ch;

  (void)mods;
  if (!win || !win->user_data) return;
  app = (struct html_viewer_app *)win->user_data;
  html_viewer_poll_background(app);
  ch = (keycode < 0x80) ? (char)keycode : 0;

  if (keycode == 4 && !app->url_editing) {
    hv_bookmark_add(app->url, app->doc.title);
    compositor_invalidate(win->id);
    return;
  }
  if (keycode == 2 && !app->url_editing) {
    html_viewer_navigate(app, "about:bookmarks");
    return;
  }
  if (keycode == 12) {
    app->url_editing = 1;
    app->url_searching = 0;
    app->url[0] = '\0';
    app->url_cursor = 0;
    compositor_invalidate(win->id);
    return;
  }
  if (keycode == 18 && !app->url_editing) {
    html_viewer_navigate(app, app->url);
    return;
  }
  if ((ch == 0x1B || keycode == 0x1B) &&
      !app->url_editing &&
      !app->url_searching &&
      app->focused_node_index < 0 &&
      app->loading) {
    html_viewer_cancel_navigation(app, "Navigation cancelled by user.");
    return;
  }
  if (keycode == 6 && !app->url_editing) {
    app->url_searching = 1;
    app->search_query[0] = '\0';
    app->search_cursor = 0;
    compositor_invalidate(win->id);
    return;
  }
  if (html_viewer_handle_search_input(win, app, keycode, ch)) return;

  if (!app->url_editing && app->focused_node_index < 0) {
    if (keycode == KEY_LEFT) {
      if (hv_history_cur > 0) {
        hv_history_cur--;
        hv_navigating_history = 1;
        html_viewer_navigate(app, hv_history[hv_history_cur]);
        hv_navigating_history = 0;
      }
      compositor_invalidate(win->id);
      return;
    }
    if (keycode == KEY_RIGHT) {
      if (hv_history_cur < hv_history_count - 1) {
        hv_history_cur++;
        hv_navigating_history = 1;
        html_viewer_navigate(app, hv_history[hv_history_cur]);
        hv_navigating_history = 0;
      }
      compositor_invalidate(win->id);
      return;
    }
  }

  if (html_viewer_handle_focused_input(win, app, ch)) return;
  if (html_viewer_handle_url_editing(win, app, keycode, ch)) return;

  if (!app->url_editing) {
    if (keycode == KEY_UP) {
      html_viewer_scroll(app, -1);
      return;
    }
    if (keycode == KEY_DOWN) {
      html_viewer_scroll(app, 1);
      return;
    }
    if (keycode == KEY_PGUP) {
      int page = app->window ? (int)app->window->surface.height / 20 : 10;
      html_viewer_scroll(app, -page);
      return;
    }
    if (keycode == KEY_PGDN) {
      int page = app->window ? (int)app->window->surface.height / 20 : 10;
      html_viewer_scroll(app, page);
      return;
    }
    if (keycode == KEY_F5) {
      html_viewer_navigate(app, app->url);
      return;
    }
    if (ch == '\t') {
      html_viewer_cycle_focus(app);
      compositor_invalidate(win->id);
      return;
    }
    if (ch >= 32 && ch < 127) {
      app->url_editing = 1;
      app->url[0] = '\0';
      app->url_cursor = 0;
    } else {
      return;
    }
  }

  (void)html_viewer_handle_url_editing(win, app, keycode, ch);
}
