#include "internal/html_viewer_internal.h"

void html_viewer_navigate(struct html_viewer_app *app, const char *url) {
  html_viewer_request_internal(app, url, HTTP_GET, NULL, 0, 0);
}

void html_viewer_paint(struct html_viewer_app *app) {
  struct gui_surface *s = NULL;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  int32_t y = 28 - app->scroll_offset;
  if (!app || !app->window || !f || !theme) return;
  html_viewer_poll_background(app);
  s = &app->window->surface;

  /* Use body CSS background-color if set, else default theme */
  {
    uint32_t page_bg = theme->window_bg;
    for (int bi = 0; bi < app->doc.node_count; bi++) {
      enum html_node_type t = app->doc.nodes[bi].type;
      if ((t == HTML_NODE_TAG_BODY || t == HTML_NODE_TAG_HTML) &&
          app->doc.nodes[bi].css_bg_color) {
        page_bg = app->doc.nodes[bi].css_bg_color; break;
      }
    }
    for (uint32_t py = 0; py < s->height; py++) {
      uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
      for (uint32_t px = 0; px < s->width; px++) line[px] = page_bg;
    }
  }
  /* Toolbar: 24px bar, back/forward/reload buttons on the left, URL after */
  {
    int can_back = (hv_history_cur > 0);
    int can_fwd  = (hv_history_cur < hv_history_count - 1);
    uint32_t btn_color = theme->accent_alt;
    uint32_t url_bg    = app->url_editing ? theme->terminal_bg : theme->accent_alt;
    for (uint32_t py = 0; py < 24 && py < s->height; py++) {
      uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
      for (uint32_t px = 0; px < 74; px++) row[px] = btn_color;
      for (uint32_t px = 74; px < s->width; px++) row[px] = url_bg;
    }
    /* Draw separator line between buttons and URL area */
    for (uint32_t py = 2; py < 22 && py < s->height; py++) {
      uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
      row[73] = theme->window_border;
    }
    /* Back button: "<" glyph */
    font_draw_char(s, f, 4, 4, '<',
                   can_back ? theme->accent : theme->text_muted);
    /* Forward button: ">" glyph */
    font_draw_char(s, f, 26, 4, '>',
                   can_fwd ? theme->accent : theme->text_muted);
    /* Reload button: "R" glyph (loading shows "*") */
    font_draw_char(s, f, 50, 4, app->loading ? '*' : 'R', theme->accent);
    /* HTTPS/HTTP indicator before URL */
    {
      int is_https = (hv_strncmp(app->url, "https://", 8) == 0);
      int is_http  = (hv_strncmp(app->url, "http://",  7) == 0);
      uint32_t indicator_color = is_https ? 0x00AA00 :
                                  is_http  ? 0xAA8800 : theme->text_muted;
      const char *indicator = is_https ? "S " : is_http ? "! " : "  ";
      font_draw_string(s, f, 78, 4, indicator, indicator_color);
    }
    /* URL text (shifted right to make room for indicator) */
    font_draw_string(s, f, 78 + (int32_t)f->glyph_width * 2, 4, app->url,
                     app->url_editing ? theme->text : theme->text_muted);
    {
      char nav_status[96];
      int32_t status_x = (int32_t)s->width - (int32_t)f->glyph_width * 18;
      nav_status[0] = '\0';
      kstrcpy(nav_status, sizeof(nav_status),
              app->last_stage[0] ? app->last_stage : "idle");
      if (app->safe_mode) {
        kbuf_append(nav_status, sizeof(nav_status), " safe");
      }
      if (status_x > 160) {
        font_draw_string(s, f, status_x, 4, nav_status, theme->text_muted);
      }
    }
    /* Bookmark star on the right edge of URL bar */
    {
      int32_t star_x = (int32_t)s->width - (int32_t)f->glyph_width - 4;
      int is_bm = hv_is_bookmarked(app->url);
      if (star_x > 78) {
        font_draw_char(s, f, star_x, 4, '*',
                       is_bm ? 0xFFD700 : theme->text_muted);
      }
    }
    if (app->url_editing) {
      int32_t url_x0 = 78 + (int32_t)f->glyph_width * 2;
      int32_t cx = url_x0 + app->url_cursor * (int32_t)f->glyph_width;
      for (uint32_t cy = 4; cy < 4 + f->glyph_height && cy < 24; cy++) {
        if ((uint32_t)cx < s->width) {
          uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + cy * s->pitch);
          row[(uint32_t)cx] = theme->accent;
        }
      }
    }
  }

  /* Inline run state for consecutive inline elements (A, SPAN, MARK, TEXT) */
  {
    int32_t il_x = 0;  /* 0 = no active inline run */
    int32_t il_y = 0;
    int32_t il_h = 0;
    int i;
    for (i = 0; i < app->doc.node_count; i++) {
      struct html_node *node = &app->doc.nodes[i];
      if (node->hidden) {
        if (il_x > 0) { y = il_y + il_h; il_x = 0; }
        node->il_x_left = 0; node->il_x_right = 0;
        continue;
      }
      if (hv_node_is_inline(node)) {
        int32_t lm = 12 + (node->indent > 0 && node->indent < 300 ? node->indent : 0);
        int32_t max_w = (int32_t)s->width - lm - 12;
        uint32_t color = html_viewer_node_color(theme, node);
        int underline = (node->type == HTML_NODE_TAG_A && !node->no_underline);
        int32_t end_x;
        int h;
        if (max_w < (int32_t)f->glyph_width) max_w = (int32_t)f->glyph_width;
        if (il_x == 0) {
          /* New inline run: begin at top margin of first node */
          il_y = y + (int32_t)html_viewer_node_margin_top(node);
          il_x = lm;
          il_h = (int32_t)f->glyph_height + 2;
        }
        node->il_x_left = il_x;
        end_x = il_x;
        h = html_viewer_wrap_text_from(s, f, lm, il_x, il_y, max_w,
                                       node->text, color, underline, &end_x);
        node->il_x_right = end_x;
        if (h > il_h) il_h = h;
        il_x = end_x + (int32_t)f->glyph_width; /* inter-element gap */
        if (il_x >= lm + max_w) {
          /* Ran off the end: wrap inline cursor to next line */
          il_y += il_h;
          il_x = lm;
          il_h = (int32_t)f->glyph_height + 2;
        }
      } else {
        /* Block element: flush any pending inline run */
        if (il_x > 0) { y = il_y + il_h; il_x = 0; }
        node->il_x_left = 0; node->il_x_right = 0;
        {
          int32_t y_before = y;
          y = html_viewer_render_node(s, f, theme, node, y, 1);
          if (node->css_border_width > 0 && y > y_before) {
            int32_t bx = 10, bw = (int32_t)s->width - 20;
            uint32_t bcol = node->css_border_color
                            ? node->css_border_color : theme->window_border;
            hv_draw_border_rect(s, bx, y_before, bw, y - y_before,
                                (int)node->css_border_width, bcol);
          }
        }
      }
      if (y > (int32_t)s->height + app->scroll_offset + 32) break;
    }
    /* Flush any trailing inline run */
    if (il_x > 0) y = il_y + il_h;
  }
  app->content_height = y + 8;

  /* Scrollbar: 4px strip on the right edge */
  if (app->content_height > (int32_t)s->height && s->width > 6) {
    int viewport = (int)s->height - 28;
    int total = app->content_height;
    if (total > 0 && viewport > 0) {
      uint32_t sb_x = s->width - 4;
      uint32_t sb_top = 28;
      uint32_t sb_h = (uint32_t)s->height - 28;
      uint32_t thumb_h = (uint32_t)((int64_t)sb_h * viewport / total);
      uint32_t thumb_y = sb_top + (uint32_t)((int64_t)sb_h * app->scroll_offset / total);
      if (thumb_h < 4) thumb_h = 4;
      if (thumb_y + thumb_h > sb_top + sb_h) thumb_y = sb_top + sb_h - thumb_h;
      for (uint32_t sy = sb_top; sy < sb_top + sb_h && sy < s->height; sy++) {
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + sy * s->pitch);
        uint32_t color = (sy >= thumb_y && sy < thumb_y + thumb_h)
                         ? theme->accent : theme->window_border;
        for (uint32_t sx = sb_x; sx < sb_x + 4 && sx < s->width; sx++)
          row[sx] = color;
      }
    }
  }

  if (app->loading) {
    /* Draw a full-width loading bar at the bottom of the viewport. */
    uint32_t bar_h = f->glyph_height + 8;
    uint32_t bar_y = s->height > bar_h ? s->height - bar_h : 0;
    for (uint32_t py = bar_y; py < s->height; py++) {
      uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
      for (uint32_t px = 0; px < s->width; px++) row[px] = theme->accent_alt;
    }
    char status_line[HTTP_MAX_HOST + 24];
    status_line[0] = '\0';
    kstrcpy(status_line, sizeof(status_line), "Carregando: ");
    kbuf_append(status_line, sizeof(status_line), app->url);
    if (app->last_stage[0]) {
      kbuf_append(status_line, sizeof(status_line), " [");
      kbuf_append(status_line, sizeof(status_line), app->last_stage);
      kbuf_append(status_line, sizeof(status_line), "]");
    }
    font_draw_string(s, f, 4, (int32_t)(bar_y + 4), status_line, theme->accent_text);
  }
  /* Find-in-page bar at bottom */
  if (app->url_searching) {
    uint32_t bar_h = f->glyph_height + 8;
    uint32_t bar_y = s->height > bar_h ? s->height - bar_h : 0;
    for (uint32_t py = bar_y; py < s->height; py++) {
      uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
      for (uint32_t px = 0; px < s->width; px++) row[px] = theme->terminal_bg;
    }
    /* Highlight nodes matching query (yellow background tint) */
    if (app->search_query[0]) {
      int32_t hy2 = 28 - app->scroll_offset;
      for (int si2 = 0; si2 < app->doc.node_count; si2++) {
        struct html_node *sn2 = &app->doc.nodes[si2];
        int32_t node_top2 = hy2 + html_viewer_node_margin_top(sn2);
        int32_t node_bot2 = html_viewer_render_node(NULL, f, theme, sn2, hy2, 0);
        hy2 = node_bot2;
        if (!sn2->hidden && hv_contains_ci(sn2->text, app->search_query)) {
          for (int32_t dy2 = node_top2; dy2 < node_bot2 && dy2 >= 0;
               dy2++) {
            if ((uint32_t)dy2 >= s->height || (uint32_t)dy2 >= bar_y) break;
            uint32_t *rp2 = (uint32_t *)((uint8_t *)s->pixels +
                                         (uint32_t)dy2 * s->pitch);
            for (uint32_t px2 = 0; px2 < s->width; px2++) {
              uint32_t c = rp2[px2];
              /* Blend with yellow tint */
              uint32_t r2 = ((c >> 16) & 0xFF) / 2 + 0x7F;
              uint32_t g2 = ((c >>  8) & 0xFF) / 2 + 0x60;
              uint32_t b2 = (c & 0xFF) / 2;
              rp2[px2] = 0xFF000000 | (r2 << 16) | (g2 << 8) | b2;
            }
          }
        }
      }
    }
    /* Draw find bar UI */
    font_draw_string(s, f, 4, (int32_t)(bar_y + 4), "Find: ", theme->accent);
    char fq_display[140];
    kstrcpy(fq_display, sizeof(fq_display), app->search_query);
    kbuf_append(fq_display, sizeof(fq_display), "_");
    font_draw_string(s, f, 4 + (int32_t)f->glyph_width * 6,
                     (int32_t)(bar_y + 4), fq_display, theme->text);
    font_draw_string(s, f, (int32_t)s->width - (int32_t)f->glyph_width * 9,
                     (int32_t)(bar_y + 4), "[Esc=Close]", theme->text_muted);
  }
  if (app->loading && app->window) {
    compositor_invalidate(app->window->id);
  }
}

void html_viewer_scroll(struct html_viewer_app *app, int delta) {
  int max_scroll = 0;
  if (!app) return;
  app->scroll_offset += delta * 20;
  if (app->window) {
    int viewport = (int)app->window->surface.height - 32;
    max_scroll = app->content_height - viewport;
    if (max_scroll < 0) max_scroll = 0;
  }
  if (app->scroll_offset < 0) app->scroll_offset = 0;
  if (app->scroll_offset > max_scroll) app->scroll_offset = max_scroll;
  if (app->window) compositor_invalidate(app->window->id);
}
