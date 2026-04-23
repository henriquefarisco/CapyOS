                                   const struct gui_theme_palette *theme,
                                   const struct html_node *node,
                                   int32_t y, int draw) {
  char display[HTML_TEXT_MAX + 16];
  int32_t margin = 12;
  int32_t max_width = 0;
  int32_t top = y + html_viewer_node_margin_top(node);
  int32_t height = 0;
  uint32_t color = html_viewer_node_color(theme, node);
  int32_t viewport_width = 640;
  if (!f || !theme || !node) return y;
  if (surface) viewport_width = (int32_t)surface->width;
  else if (g_viewer.window) viewport_width = (int32_t)g_viewer.window->surface.width;
  if (viewport_width < (int32_t)f->glyph_width) viewport_width = (int32_t)f->glyph_width;
  if (node->hidden) return y;
  /* Apply CSS line-height for this node */
  g_hv_line_height_px = (int)node->css_line_height;
  /* CSS background color or <mark> highlight: fill the node's row */
  {
    uint32_t bg = node->css_bg_color;
    if (!bg && node->type == HTML_NODE_TAG_MARK) bg = 0x3A3010; /* dim yellow bg */
    if (draw && bg) {
      int32_t row_h = (int32_t)f->glyph_height + 4;
      for (int32_t dy = 0; dy < row_h; dy++) {
        int32_t hy = top + dy;
        if (hy >= 0 && (uint32_t)hy < surface->height) {
          uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                      (uint32_t)hy * surface->pitch);
          for (uint32_t px = 0; px < surface->width; px++) rp[px] = bg;
        }
      }
    }
  }
  if (node->type == HTML_NODE_TAG_BR) return y + html_viewer_node_margin_bottom(node);
  /* Horizontal rule: draw a 2-px line across the viewport */
  if (node->type == HTML_NODE_TAG_HR) {
    if (draw) {
      for (int32_t dy = 0; dy < 2; dy++) {
        int32_t hy = top + dy;
        if (hy >= 0 && (uint32_t)hy < surface->height) {
          uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                       (uint32_t)hy * surface->pitch);
          for (uint32_t px = (uint32_t)margin;
               px < surface->width - (uint32_t)margin; px++) {
            row[px] = theme->text_muted;
          }
        }
      }
    }
    return top + 2 + html_viewer_node_margin_bottom(node);
  }
  /* Table row: separator line + reserve space for side-by-side cells */
  if (node->type == HTML_NODE_TAG_TR) {
    hv_table_row_h = (int32_t)f->glyph_height + 8;
    hv_table_row_y = top + 1;
    if (draw) {
      int32_t hy = top;
      if (hy >= 0 && (uint32_t)hy < surface->height) {
        uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                     (uint32_t)hy * surface->pitch);
        for (uint32_t px = (uint32_t)margin;
             px < surface->width - (uint32_t)margin; px++) {
          row[px] = theme->window_border;
        }
      }
    }
    /* Advance y past separator + full row height so TD nodes don't shift y */
    return top + 1 + hv_table_row_h + html_viewer_node_margin_bottom(node);
  }
  /* Scaled heading rendering: H1=3×, H2=2×, H3=2×, H4/H5/H6=1× bold */
  if (node->type == HTML_NODE_TAG_H1 || node->type == HTML_NODE_TAG_H2 ||
      node->type == HTML_NODE_TAG_H3 || node->type == HTML_NODE_TAG_H4 ||
      node->type == HTML_NODE_TAG_H5 || node->type == HTML_NODE_TAG_H6) {
    int scale;
    int32_t max_w = viewport_width - margin * 2;
    int32_t start_x = margin;
    int32_t h;
    if (node->font_size > 0)
      scale = (int)(node->font_size / f->glyph_height);
    else if (node->type == HTML_NODE_TAG_H1) scale = 3;
    else if (node->type == HTML_NODE_TAG_H2) scale = 2;
    else if (node->type == HTML_NODE_TAG_H3) scale = 2;
    else scale = 1;
    if (scale < 1) scale = 1;
    /* Apply text-align: center */
    if (node->text_align == 1) {
      int32_t tw = (int32_t)(kstrlen(node->text) * f->glyph_width * (uint32_t)scale);
      if (tw < max_w) start_x = margin + (max_w - tw) / 2;
    } else if (node->text_align == 2) {
      int32_t tw = (int32_t)(kstrlen(node->text) * f->glyph_width * (uint32_t)scale);
      if (tw < max_w) start_x = margin + max_w - tw;
    }
    /* Headings are always bold: double-draw shifted 1px */
    if (draw && surface && scale >= 1) {
      hv_wrap_text_scaled(surface, f, start_x + 1, top, max_w, node->text, color, scale);
    }
    h = hv_wrap_text_scaled(draw ? surface : NULL, f, start_x, top, max_w,
                    node->text, color, scale);
    return top + h + html_viewer_node_margin_bottom(node);
  }

  /* IMG node: blit cached pixels or fall back to alt text */
  if (node->type == HTML_NODE_TAG_IMG) {
    if (node->image_pixels && node->image_width > 0 && node->image_height > 0) {
      uint32_t max_w = viewport_width > margin * 2
                       ? (uint32_t)(viewport_width - margin * 2) : 1;
      uint32_t dst_w = node->image_width < max_w ? node->image_width : max_w;
      uint32_t dst_h = (dst_w == node->image_width) ? node->image_height
                     : (uint32_t)((uint64_t)node->image_height * dst_w / node->image_width);
      if (dst_h == 0) dst_h = 1;
      if (draw) {
        for (uint32_t dy = 0; dy < dst_h; dy++) {
          int32_t sy_row = top + (int32_t)dy;
          if (sy_row < 0 || (uint32_t)sy_row >= surface->height) continue;
          uint32_t *dst_row = (uint32_t *)((uint8_t *)surface->pixels +
                                           (uint32_t)sy_row * surface->pitch);
          uint32_t src_y = dy * node->image_height / dst_h;
          const uint32_t *src_row = node->image_pixels + src_y * node->image_width;
          for (uint32_t dx = 0; dx < dst_w; dx++) {
            uint32_t src_x = dx * node->image_width / dst_w;
            uint32_t px = src_row[src_x];
            uint32_t alpha = px >> 24;
            if (alpha == 0xFF) {
              dst_row[(uint32_t)margin + dx] = px;
            } else if (alpha > 0) {
              uint32_t bg = theme->window_bg;
              uint32_t r = ((px >> 16) & 0xFF) * alpha / 255
                         + ((bg >> 16) & 0xFF) * (255 - alpha) / 255;
              uint32_t g = ((px >> 8)  & 0xFF) * alpha / 255
                         + ((bg >> 8)  & 0xFF) * (255 - alpha) / 255;
              uint32_t b = (px & 0xFF) * alpha / 255
                         + (bg & 0xFF) * (255 - alpha) / 255;
              dst_row[(uint32_t)margin + dx] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
          }
        }
      }
      return top + (int32_t)dst_h + html_viewer_node_margin_bottom(node);
    }
    /* fallback */
    {
      char img_display[HTML_TEXT_MAX + 16];
      kstrcpy(img_display, sizeof(img_display), "[image] ");
      if (node->image_error == HV_IMAGE_ERROR_UNSUPPORTED) {
        kstrcpy(img_display, sizeof(img_display), "[unsupported image] ");
      } else if (node->image_error == HV_IMAGE_ERROR_TOO_LARGE) {
        kstrcpy(img_display, sizeof(img_display), "[image too large] ");
      } else if (node->image_error == HV_IMAGE_ERROR_DECODE_FAILED) {
        kstrcpy(img_display, sizeof(img_display), "[image decode failed] ");
      }
      if (node->text[0]) kbuf_append(img_display, sizeof(img_display), node->text);
      int32_t h = html_viewer_wrap_text(draw ? surface : NULL, f, margin, top,
                      viewport_width - margin * 2,
                      img_display, color, 0);
      return top + h + html_viewer_node_margin_bottom(node);
    }
  }
  /* Table cell: render at column x offset, y fixed by prior TR, no y advance */
  if (node->type == HTML_NODE_TAG_TD) {
    int32_t ncols  = node->col_count > 0 ? (int32_t)(uint8_t)node->col_count : 1;
    int32_t cidx   = (int32_t)(uint8_t)node->col_index;
    int32_t total_w = viewport_width - 8;
    int32_t col_w  = total_w / ncols;
    int32_t cell_x = 4 + cidx * col_w;
    int32_t cell_y = hv_table_row_y + 2;
    uint32_t cell_color = node->bold ? theme->accent_text : color;
    if (draw) {
      /* th: accent background */
      if (node->bold) {
        for (int32_t dy = 0; dy < hv_table_row_h; dy++) {
          int32_t hy = hv_table_row_y + dy;
          if (hy < 0 || (uint32_t)hy >= surface->height) continue;
          uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                      (uint32_t)hy * surface->pitch);
          for (int32_t px = cell_x; px < cell_x + col_w &&
               (uint32_t)px < surface->width; px++)
            rp[(uint32_t)px] = theme->accent_alt;
        }
      }
      /* Left vertical border between columns */
      if (cidx > 0 && (uint32_t)cell_x < surface->width) {
        for (int32_t dy = 0; dy < hv_table_row_h; dy++) {
          int32_t hy = hv_table_row_y + dy;
          if (hy < 0 || (uint32_t)hy >= surface->height) continue;
          uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                      (uint32_t)hy * surface->pitch);
          rp[(uint32_t)cell_x] = theme->window_border;
        }
      }
    }
    html_viewer_wrap_text(draw ? surface : NULL, f, cell_x + 2, cell_y,
                          col_w - 4, node->text, cell_color, 0);
    return y; /* y already advanced by TR — do not shift again */
  }
  display[0] = '\0';
  if (node->type == HTML_NODE_TAG_LI && !node->list_style_none)
    kstrcpy(display, sizeof(display), "* ");
  else if (node->type == HTML_NODE_TAG_PRE ||
           node->type == HTML_NODE_TAG_CODE) {
    /* Code block: dark background fill covering full text height */
    kstrcpy(display, sizeof(display), "  ");
    kbuf_append(display, sizeof(display), node->text);
    if (draw && !node->css_bg_color) {
      int32_t avail_w = viewport_width - margin - 12;
      if (avail_w < (int32_t)f->glyph_width) avail_w = (int32_t)f->glyph_width;
      int32_t block_h = html_viewer_wrap_text(NULL, f, margin, top,
                                              avail_w, display, 0, 0) + 4;
      for (int32_t dy = -1; dy < block_h; dy++) {
        int32_t hy = top + dy;
        if (hy < 0 || (uint32_t)hy >= surface->height) continue;
        uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                    (uint32_t)hy * surface->pitch);
        for (uint32_t px = (uint32_t)(margin - 2);
             px < surface->width - 12 && px < surface->width; px++)
          rp[px] = theme->terminal_bg;
      }
    }
  }
  else if (node->type == HTML_NODE_TAG_BLOCKQUOTE) {
    /* Blockquote: accent left border bar spanning full text height */
    kstrcpy(display, sizeof(display), "  ");
    kbuf_append(display, sizeof(display), node->text);
    if (draw) {
      int32_t avail_w = viewport_width - margin - 12;
      if (avail_w < (int32_t)f->glyph_width) avail_w = (int32_t)f->glyph_width;
      int32_t text_h = html_viewer_wrap_text(NULL, f, margin, top,
                                             avail_w, display, 0, 0) + 4;
      for (int32_t dy = -2; dy < text_h; dy++) {
        int32_t hy = top + dy;
        if (hy < 0 || (uint32_t)hy >= surface->height) continue;
        uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                    (uint32_t)hy * surface->pitch);
        int32_t bar_x = margin - 4;
        if (bar_x >= 0 && (uint32_t)bar_x + 3 < surface->width) {
          rp[(uint32_t)bar_x]     = theme->accent;
          rp[(uint32_t)bar_x + 1] = theme->accent;
          rp[(uint32_t)bar_x + 2] = theme->accent;
        }
      }
    }
  }
  else if (node->type == HTML_NODE_TAG_DETAILS) {
    if (node->open) {
      kstrcpy(display, sizeof(display), "[-] ");
      kbuf_append(display, sizeof(display), node->text);
    } else {
      /* Show only first line/sentence when closed */
      char summary[80];
      size_t si = 0;
      while (si < sizeof(summary) - 1 && node->text[si] &&
             node->text[si] != '\n' && node->text[si] != '.') {
        summary[si] = node->text[si]; si++;
      }
      if (node->text[si] == '.') summary[si++] = '.';
      summary[si] = '\0';
      kstrcpy(display, sizeof(display), "[+] ");
      kbuf_append(display, sizeof(display), summary);
      if (node->text[si]) kbuf_append(display, sizeof(display), "...");
    }
  }
  else if (node->type == HTML_NODE_TAG_FIGCAPTION) {
    kstrcpy(display, sizeof(display), "  ");
    kbuf_append(display, sizeof(display), node->text);
  }
  else if (node->type == HTML_NODE_TAG_INPUT) {
    int is_focused = (g_viewer.focused_node_index >= 0 &&
                      node == &g_viewer.doc.nodes[g_viewer.focused_node_index]);
    if (node->input_type == HTML_INPUT_TYPE_TEXTAREA) {
      /* Textarea: multi-row box — render inline and return early */
      int32_t box_w = viewport_width - margin - 12;
      int32_t row_h = (int32_t)f->glyph_height + 2;
      /* Count lines in text (split on \n), min 4 visible rows */
      int nlines = 1;
      for (const char *p = node->text; *p; p++) if (*p == '\n') nlines++;
      if (nlines < 4) nlines = 4;
      int32_t box_h = nlines * row_h + 4;
      if (draw) {
        uint32_t bg = is_focused ? theme->accent_alt : theme->terminal_bg;
        for (int32_t dy = 0; dy < box_h; dy++) {
          int32_t hy = top + dy;
          if (hy < 0 || (uint32_t)hy >= surface->height) continue;
          uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                      (uint32_t)hy * surface->pitch);
          for (int32_t px = margin - 2; px < margin + box_w + 2 && px >= 0 &&
               (uint32_t)px < surface->width; px++)
            rp[(uint32_t)px] = bg;
        }
        /* Render each line of text */
        const char *src = node->text;
        int32_t ty = top + 2;
        for (int ln = 0; ln < nlines; ln++) {
          char line[HTML_TEXT_MAX];
          int li = 0;
          while (*src && *src != '\n' && li < (int)sizeof(line) - 1)
            line[li++] = *src++;
          line[li] = '\0';
          if (*src == '\n') src++;
          if (ln == nlines - 1 && is_focused) {
            if (li + 1 < (int)sizeof(line)) { line[li++] = '_'; line[li] = '\0'; }
          }
          if (line[0]) font_draw_string(surface, f, margin, ty, line, color);
          ty += row_h;
          if (!*src && ln < nlines - 2) break;
        }
        /* Border outline */
        for (int32_t dy = 0; dy < box_h; dy++) {
          int32_t hy = top + dy;
          if (hy < 0 || (uint32_t)hy >= surface->height) continue;
          uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                      (uint32_t)hy * surface->pitch);
          if (margin - 2 >= 0) rp[(uint32_t)(margin - 2)] = theme->window_border;
          if ((uint32_t)(margin + box_w + 1) < surface->width)
            rp[(uint32_t)(margin + box_w + 1)] = theme->window_border;
        }
        for (int32_t px = margin - 2; px < margin + box_w + 2 && px >= 0 &&
             (uint32_t)px < surface->width; px++) {
          if ((uint32_t)top < surface->height)
            ((uint32_t *)((uint8_t *)surface->pixels + (uint32_t)top * surface->pitch))
                [(uint32_t)px] = theme->window_border;
          int32_t bot = top + box_h - 1;
          if (bot >= 0 && (uint32_t)bot < surface->height)
            ((uint32_t *)((uint8_t *)surface->pixels + (uint32_t)bot * surface->pitch))
                [(uint32_t)px] = theme->window_border;
        }
      }
      return top + box_h + html_viewer_node_margin_bottom(node);
    }
    /* Focused single-line input: accent background fill */
    if (draw && is_focused) {
      int32_t bh = (int32_t)f->glyph_height + 4;
      for (int32_t dy = -1; dy < bh; dy++) {
        int32_t hy = top + dy;
        if (hy < 0 || (uint32_t)hy >= surface->height) continue;
        uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                    (uint32_t)hy * surface->pitch);
        for (uint32_t px = (uint32_t)(margin - 2);
             px < surface->width - 12 && px < surface->width; px++)
          rp[px] = theme->accent_alt;
      }
    }
    if (node->input_type == HTML_INPUT_TYPE_CHECKBOX) {
      kbuf_append(display, sizeof(display), node->open ? "[x]" : "[ ]");
    } else if (node->input_type == HTML_INPUT_TYPE_RADIO) {
      kbuf_append(display, sizeof(display), node->open ? "(*)" : "( )");
    } else {
      kbuf_append(display, sizeof(display), "[");
      if (node->text[0]) {
        kbuf_append(display, sizeof(display), node->text);
      } else if (node->placeholder[0] && !is_focused) {
        kbuf_append(display, sizeof(display), node->placeholder);
      }
      if (is_focused) kbuf_append(display, sizeof(display), "_"); /* cursor */
      kbuf_append(display, sizeof(display), "]");
    }
  } else if (node->type == HTML_NODE_TAG_BUTTON) {
    kstrcpy(display, sizeof(display), "[ ");
    kbuf_append(display, sizeof(display), node->text);
    kbuf_append(display, sizeof(display), " ]");
  }
  else kbuf_append(display, sizeof(display), node->text);
  if (!display[0] && node->type == HTML_NODE_TAG_A && node->href[0]) {
    kstrcpy(display, sizeof(display), node->href);
  }
  /* CSS text-transform: uppercase / lowercase / capitalize */
  if (node->css_text_transform && display[0]) {
    for (char *p = display; *p; p++) {
      if (node->css_text_transform == 1) { /* uppercase */
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
      } else if (node->css_text_transform == 2) { /* lowercase */
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
      } else if (node->css_text_transform == 3) { /* capitalize */
        int prev_space = (p == display || *(p-1) == ' ' || *(p-1) == '\t');
        if (prev_space && *p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
      }
    }
  }
  /* CSS margin-left / padding-left → shift the left margin only */
  if (node->indent > 0 && node->indent < 300)
    margin += node->indent;
  max_width = viewport_width - margin - 12; /* right margin fixed at 12 */
  /* CSS width/max-width: clamp content width */
  if (node->css_width > 0 && (int32_t)node->css_width < max_width)
    max_width = (int32_t)node->css_width;
  if (node->css_max_width > 0 && (int32_t)node->css_max_width < max_width)
    max_width = (int32_t)node->css_max_width;
  if (max_width < (int32_t)f->glyph_width) max_width = (int32_t)f->glyph_width;
  /* CSS font-size scaling for non-heading nodes */
  if (node->font_size > 0 && node->font_size != f->glyph_height &&
      node->type != HTML_NODE_TAG_H1 && node->type != HTML_NODE_TAG_H2 &&
      node->type != HTML_NODE_TAG_H3 && node->type != HTML_NODE_TAG_H4 &&
      node->type != HTML_NODE_TAG_H5 && node->type != HTML_NODE_TAG_H6) {
    int scale = (int)(node->font_size / f->glyph_height);
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    if (scale > 1) {
      height = hv_wrap_text_scaled(draw ? surface : NULL, f, margin, top,
                                   max_width, display, color, scale);
      return top + height + html_viewer_node_margin_bottom(node);
    }
  }
  {
    int underline = ((node->type == HTML_NODE_TAG_A ||
                      node->type == HTML_NODE_TAG_INPUT ||
                      node->type == HTML_NODE_TAG_BUTTON) &&
                     !node->no_underline);
    /* Bold: double-draw shifted 1px right for bitmap font simulation */
    if (node->bold && draw && surface) {
      html_viewer_wrap_text(surface, f, margin + 1, top,
                            max_width > 1 ? max_width - 1 : max_width,
                            display, color, 0);
    }
    height = html_viewer_wrap_text(draw ? surface : NULL, f, margin, top, max_width,
                                   display, color, underline);
  }
  return top + height + html_viewer_node_margin_bottom(node);
}

