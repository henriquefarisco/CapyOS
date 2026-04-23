#include "internal/html_viewer_internal.h"

static int html_viewer_node_hit_test(struct html_viewer_app *app,
                                     const struct font *f,
                                     const struct html_node *node,
                                     int32_t start_y,
                                     int32_t *top_out,
                                     int32_t *bottom_out) {
  struct gui_surface *surface = NULL;
  const struct gui_theme_palette *theme = compositor_theme();
  int32_t end_y = 0;
  if (!app || !app->window || !f || !node || !theme) return start_y;
  surface = &app->window->surface;
  if (top_out) *top_out = start_y + html_viewer_node_margin_top(node);
  end_y = html_viewer_render_node(surface, f, theme, node, start_y, 0);
  if (bottom_out) *bottom_out = end_y;
  return end_y;
}

static void hv_scroll_to_anchor(struct html_viewer_app *app, const char *id) {
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  int32_t y = 28;
  int viewport = 0;
  int max_scroll = 0;
  if (!app || !id || !id[0] || !f || !theme || !app->window) return;
  for (int i = 0; i < app->doc.node_count; i++) {
    struct html_node *node = &app->doc.nodes[i];
    if (node->id[0] && kstreq(node->id, id)) {
      app->scroll_offset = y - 28;
      if (app->scroll_offset < 0) app->scroll_offset = 0;
      viewport = (int)app->window->surface.height - 32;
      max_scroll = app->content_height - viewport;
      if (max_scroll < 0) max_scroll = 0;
      if (app->scroll_offset > max_scroll) app->scroll_offset = max_scroll;
      compositor_invalidate(app->window->id);
      return;
    }
    y = html_viewer_render_node(&app->window->surface, f, theme, node, y, 0);
  }
}

void html_viewer_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                              uint8_t buttons) {
  struct html_viewer_app *app = NULL;
  const struct font *f = font_default();

  if (!win || !win->user_data || !(buttons & 1)) return;
  app = (struct html_viewer_app *)win->user_data;
  html_viewer_poll_background(app);

  if (y < 24) {
    if (x < 24) {
      if (hv_history_cur > 0) {
        hv_history_cur--;
        hv_navigating_history = 1;
        html_viewer_navigate(app, hv_history[hv_history_cur]);
        hv_navigating_history = 0;
      }
    } else if (x < 50) {
      if (hv_history_cur < hv_history_count - 1) {
        hv_history_cur++;
        hv_navigating_history = 1;
        html_viewer_navigate(app, hv_history[hv_history_cur]);
        hv_navigating_history = 0;
      }
    } else if (x < 74) {
      html_viewer_navigate(app, app->url);
    } else {
      app->url_editing = 1;
      {
        int32_t url_x0 = 78 + (int32_t)font_default()->glyph_width * 2;
        int click_pos = (x - url_x0) / (int32_t)font_default()->glyph_width;
        int url_len = (int)kstrlen(app->url);
        app->url_cursor = click_pos < 0 ? 0 : click_pos > url_len ? url_len : click_pos;
      }
    }
    compositor_invalidate(win->id);
    return;
  }
  if (!f) return;

  {
    int32_t node_y = 28 - app->scroll_offset;
    int32_t il_x = 0;
    int32_t il_y = 0;
    int32_t il_h = 0;

    for (int i = 0; i < app->doc.node_count; i++) {
      int32_t top, bottom;
      struct html_node *node = &app->doc.nodes[i];

      if (node->hidden) {
        if (il_x > 0) {
          node_y = il_y + il_h;
          il_x = 0;
        }
        continue;
      }

      if (hv_node_is_inline(node)) {
        int32_t lm = 12 + (node->indent > 0 && node->indent < 300 ? node->indent : 0);
        int32_t max_w = (int32_t)win->surface.width - lm - 12;
        int32_t end_x;
        int h;
        if (max_w < (int32_t)f->glyph_width) max_w = (int32_t)f->glyph_width;
        if (il_x == 0) {
          il_y = node_y + (int32_t)html_viewer_node_margin_top(node);
          il_x = lm;
          il_h = (int32_t)f->glyph_height + 2;
        }
        top = il_y;
        end_x = il_x;
        h = html_viewer_wrap_text_from(NULL, f, lm, il_x, il_y, max_w,
                                       node->text, 0, 0, &end_x);
        bottom = il_y + (h > il_h ? h : il_h);
        if (h > il_h) il_h = h;
        il_x = end_x + (int32_t)f->glyph_width;
        if (il_x >= lm + max_w) {
          il_y += il_h;
          il_x = lm;
          il_h = (int32_t)f->glyph_height + 2;
        }

        if (node->type == HTML_NODE_TAG_A && node->href[0] &&
            y >= top && y < bottom &&
            x >= node->il_x_left && x <= node->il_x_right + 4) {
          char resolved[HTML_URL_MAX];
          if (node->href[0] == '#') {
            hv_scroll_to_anchor(app, node->href + 1);
            compositor_invalidate(win->id);
            return;
          }
          if (html_viewer_resolve_document_url(app, node->href, resolved,
                                               sizeof(resolved)) == 0) {
            html_viewer_navigate(app, resolved);
          } else {
            html_viewer_navigate(app, node->href);
          }
          compositor_invalidate(win->id);
          return;
        }
        continue;
      }

      if (il_x > 0) {
        node_y = il_y + il_h;
        il_x = 0;
      }
      top = node_y;
      bottom = node_y;
      node_y = html_viewer_node_hit_test(app, f, node, node_y, &top, &bottom);

      if (node->type == HTML_NODE_TAG_INPUT && y >= top && y < bottom && x >= 12) {
        app->focused_node_index = i;
        app->url_editing = 0;
        if (node->input_type == HTML_INPUT_TYPE_CHECKBOX) {
          node->open = node->open ? 0 : 1;
        } else if (node->input_type == HTML_INPUT_TYPE_RADIO) {
          for (int ri = 0; ri < app->doc.node_count; ri++) {
            struct html_node *rn = &app->doc.nodes[ri];
            if (rn->type == HTML_NODE_TAG_INPUT &&
                rn->input_type == HTML_INPUT_TYPE_RADIO &&
                kstreq(rn->name, node->name)) {
              rn->open = 0;
            }
          }
          node->open = 1;
        }
        compositor_invalidate(win->id);
        return;
      }
      if (node->type == HTML_NODE_TAG_BUTTON && y >= top && y < bottom && x >= 12) {
        app->focused_node_index = i;
        app->url_editing = 0;
        html_viewer_submit_form(app, i);
        compositor_invalidate(win->id);
        return;
      }
      if (node->type == HTML_NODE_TAG_DETAILS && y >= top && y < bottom && x >= 12) {
        node->open = node->open ? 0 : 1;
        compositor_invalidate(win->id);
        return;
      }
      if ((node->type == HTML_NODE_TAG_A || node->type == HTML_NODE_TAG_LI) &&
          node->href[0] && y >= top && y < bottom && x >= 12) {
        char resolved[HTML_URL_MAX];
        if (node->href[0] == '#') {
          hv_scroll_to_anchor(app, node->href + 1);
          compositor_invalidate(win->id);
          return;
        }
        if (html_viewer_resolve_document_url(app, node->href, resolved,
                                             sizeof(resolved)) == 0) {
          html_viewer_navigate(app, resolved);
        } else {
          html_viewer_navigate(app, node->href);
        }
        compositor_invalidate(win->id);
        return;
      }
    }
  }

  app->focused_node_index = -1;
  compositor_invalidate(win->id);
}
