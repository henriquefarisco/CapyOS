#include "gui/capyui_display_adapter.h"
#include "gui/font.h"

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
#include "capy_display_list.h"
#endif

#include <stddef.h>

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)

struct clip_stack {
  struct gui_rect rects[CAPYUI_DISPLAY_ADAPTER_CLIP_STACK_MAX];
  uint32_t depth;
  uint32_t max_depth;
};

static void stats_zero(struct capyui_display_adapter_stats *stats) {
  if (!stats) return;
  stats->commands_seen = 0u;
  stats->commands_rendered = 0u;
  stats->commands_skipped = 0u;
  stats->unsupported_ops = 0u;
  stats->clip_depth_max = 0u;
}

static void stats_skip(struct capyui_display_adapter_stats *stats) {
  if (stats) stats->commands_skipped++;
}

static void stats_render(struct capyui_display_adapter_stats *stats) {
  if (stats) stats->commands_rendered++;
}

static void stats_unsupported(struct capyui_display_adapter_stats *stats) {
  if (stats) {
    stats->commands_skipped++;
    stats->unsupported_ops++;
  }
}

static int rect_empty(const struct gui_rect *r) {
  return !r || r->width == 0u || r->height == 0u;
}

static int32_t rect_end_clamped(int32_t start, uint32_t extent) {
  int64_t end = (int64_t)start + (int64_t)extent;
  if (end > (int64_t)INT32_MAX) return INT32_MAX;
  if (end < (int64_t)INT32_MIN) return INT32_MIN;
  return (int32_t)end;
}

static int rect_intersect(const struct gui_rect *a, const struct gui_rect *b,
                          struct gui_rect *out) {
  int32_t ax0;
  int32_t ay0;
  int32_t ax1;
  int32_t ay1;
  int32_t bx0;
  int32_t by0;
  int32_t bx1;
  int32_t by1;
  int32_t x0;
  int32_t y0;
  int32_t x1;
  int32_t y1;
  if (rect_empty(a) || rect_empty(b) || !out) return 0;
  ax0 = a->x;
  ay0 = a->y;
  ax1 = rect_end_clamped(a->x, a->width);
  ay1 = rect_end_clamped(a->y, a->height);
  bx0 = b->x;
  by0 = b->y;
  bx1 = rect_end_clamped(b->x, b->width);
  by1 = rect_end_clamped(b->y, b->height);
  x0 = ax0 > bx0 ? ax0 : bx0;
  y0 = ay0 > by0 ? ay0 : by0;
  x1 = ax1 < bx1 ? ax1 : bx1;
  y1 = ay1 < by1 ? ay1 : by1;
  if (x0 >= x1 || y0 >= y1) return 0;
  out->x = x0;
  out->y = y0;
  out->width = (uint32_t)(x1 - x0);
  out->height = (uint32_t)(y1 - y0);
  return 1;
}

static void capy_rect_to_gui(const struct capy_ui_rect *in,
                             struct gui_rect *out) {
  out->x = in->x;
  out->y = in->y;
  out->width = in->width;
  out->height = in->height;
}

static int surface_bounds(const struct gui_surface *surface,
                          struct gui_rect *out) {
  if (!surface || !surface->pixels || surface->width == 0u ||
      surface->height == 0u || surface->pitch / 4u < surface->width) {
    return 0;
  }
  if (surface->width > (uint32_t)INT32_MAX ||
      surface->height > (uint32_t)INT32_MAX) {
    return 0;
  }
  out->x = 0;
  out->y = 0;
  out->width = surface->width;
  out->height = surface->height;
  return 1;
}

static int effective_clip(const struct gui_surface *surface,
                          const struct clip_stack *stack,
                          const struct gui_rect *damage_clip,
                          const struct gui_rect *cmd_rect,
                          struct gui_rect *out) {
  struct gui_rect clipped;
  struct gui_rect bounds;
  if (!surface_bounds(surface, &bounds)) return 0;
  if (!rect_intersect(cmd_rect, &bounds, &clipped)) return 0;
  for (uint32_t i = 0u; i < stack->depth; ++i) {
    if (!rect_intersect(&clipped, &stack->rects[i], &clipped)) return 0;
  }
  if (damage_clip && !rect_intersect(&clipped, damage_clip, &clipped)) return 0;
  *out = clipped;
  return 1;
}

static void fill_rect(struct gui_surface *surface, const struct gui_rect *clip,
                      uint32_t color) {
  for (uint32_t row = 0u; row < clip->height; ++row) {
    uint32_t *line;
    int32_t py = clip->y + (int32_t)row;
    if (py < 0 || (uint32_t)py >= surface->height) continue;
    line = (uint32_t *)((uint8_t *)surface->pixels +
                        (uint32_t)py * surface->pitch);
    for (uint32_t col = 0u; col < clip->width; ++col) {
      int32_t px = clip->x + (int32_t)col;
      if (px < 0 || (uint32_t)px >= surface->width) continue;
      line[px] = color;
    }
  }
}

static void draw_char_clipped(struct gui_surface *surface, const struct font *font,
                              const struct gui_rect *clip, int32_t x,
                              int32_t y, char c, uint32_t color) {
  uint32_t idx;
  uint32_t glyph_offset;
  uint32_t glyph_cols;
  const uint8_t *glyph;
  if (!surface || !font || !font->data || !clip) return;
  idx = (uint32_t)(uint8_t)c;
  if (idx < font->first_char || idx > font->last_char) return;
  glyph_offset = (idx - font->first_char) * font->bytes_per_glyph;
  glyph_cols = font->glyph_width > 8u ? 8u : font->glyph_width;
  glyph = font->data + glyph_offset;
  for (uint32_t row = 0u; row < font->glyph_height; ++row) {
    int32_t py = y + (int32_t)row;
    uint8_t bits;
    uint32_t *line;
    if (py < clip->y || py >= rect_end_clamped(clip->y, clip->height)) continue;
    if (py < 0 || (uint32_t)py >= surface->height) continue;
    bits = glyph[row];
    line = (uint32_t *)((uint8_t *)surface->pixels +
                        (uint32_t)py * surface->pitch);
    for (uint32_t col = 0u; col < glyph_cols; ++col) {
      int32_t px = x + (int32_t)col;
      if (!(bits & (uint8_t)(0x80u >> col))) continue;
      if (px < clip->x || px >= rect_end_clamped(clip->x, clip->width)) continue;
      if (px < 0 || (uint32_t)px >= surface->width) continue;
      line[px] = color;
    }
  }
}

static int draw_text_span(struct gui_surface *surface, const struct font *font,
                          const struct gui_rect *clip, const char *text_pool,
                          uint32_t text_capacity, uint16_t offset,
                          uint16_t len, int32_t x, int32_t y,
                          uint32_t color) {
  if (!surface || !font) return CAPYUI_DISPLAY_ADAPTER_ERR_INVALID;
  if (len == 0u) return CAPYUI_DISPLAY_ADAPTER_OK;
  if (!text_pool) return CAPYUI_DISPLAY_ADAPTER_ERR_INVALID;
  if ((uint32_t)offset > text_capacity ||
      (uint32_t)len > text_capacity - (uint32_t)offset) {
    return CAPYUI_DISPLAY_ADAPTER_ERR_TEXT_RANGE;
  }
  for (uint16_t i = 0u; i < len; ++i) {
    int64_t cx;
    char c = text_pool[(uint32_t)offset + i];
    cx = (int64_t)x + (int64_t)i * (int64_t)font->glyph_width;
    if (cx < (int64_t)INT32_MIN || cx > (int64_t)INT32_MAX) continue;
    draw_char_clipped(surface, font, clip, (int32_t)cx, y, c, color);
  }
  return CAPYUI_DISPLAY_ADAPTER_OK;
}

static int render_rect_cmd(const struct capy_dl_cmd *cmd,
                           struct gui_surface *surface,
                           const struct clip_stack *stack,
                           const struct gui_rect *damage_clip) {
  struct gui_rect rect;
  struct gui_rect clip;
  capy_rect_to_gui(&cmd->rect, &rect);
  if (!effective_clip(surface, stack, damage_clip, &rect, &clip)) return 0;
  fill_rect(surface, &clip, cmd->color);
  return 1;
}

static int render_border_cmd(const struct capy_dl_cmd *cmd,
                             struct gui_surface *surface,
                             const struct clip_stack *stack,
                             const struct gui_rect *damage_clip) {
  struct gui_rect rect;
  uint32_t width;
  uint32_t rendered = 0u;
  capy_rect_to_gui(&cmd->rect, &rect);
  width = cmd->border_width ? (uint32_t)cmd->border_width : 1u;
  if (rect.width == 0u || rect.height == 0u) return 0;
  if (width > rect.width) width = rect.width;
  if (width > rect.height) width = rect.height;
  if (width >= (rect.width + 1u) / 2u || width >= (rect.height + 1u) / 2u) {
    struct gui_rect clip;
    if (!effective_clip(surface, stack, damage_clip, &rect, &clip)) return 0;
    fill_rect(surface, &clip, cmd->color);
    return 1;
  }
  for (uint32_t i = 0u; i < width; ++i) {
    struct gui_rect top = { rect.x + (int32_t)i, rect.y + (int32_t)i,
                            rect.width - i * 2u, 1u };
    struct gui_rect bottom = { rect.x + (int32_t)i,
                               rect.y + (int32_t)rect.height - 1 - (int32_t)i,
                               rect.width - i * 2u, 1u };
    struct gui_rect left = { rect.x + (int32_t)i, rect.y + (int32_t)i,
                             1u, rect.height - i * 2u };
    struct gui_rect right = { rect.x + (int32_t)rect.width - 1 - (int32_t)i,
                              rect.y + (int32_t)i, 1u,
                              rect.height - i * 2u };
    struct gui_rect clip;
    if (top.width > 0u && effective_clip(surface, stack, damage_clip, &top, &clip)) {
      fill_rect(surface, &clip, cmd->color);
      rendered = 1u;
    }
    if (bottom.width > 0u && effective_clip(surface, stack, damage_clip, &bottom, &clip)) {
      fill_rect(surface, &clip, cmd->color);
      rendered = 1u;
    }
    if (left.height > 0u && effective_clip(surface, stack, damage_clip, &left, &clip)) {
      fill_rect(surface, &clip, cmd->color);
      rendered = 1u;
    }
    if (right.height > 0u && effective_clip(surface, stack, damage_clip, &right, &clip)) {
      fill_rect(surface, &clip, cmd->color);
      rendered = 1u;
    }
  }
  return rendered ? 1 : 0;
}

static int render_text_cmd(const struct capy_display_list *dl,
                           const struct capy_dl_cmd *cmd,
                           struct gui_surface *surface,
                           const struct clip_stack *stack,
                           const struct gui_rect *damage_clip) {
  struct gui_rect rect;
  struct gui_rect clip;
  const struct font *font = font_default();
  int rc;
  if (!font) return CAPYUI_DISPLAY_ADAPTER_ERR_INVALID;
  capy_rect_to_gui(&cmd->rect, &rect);
  if (!effective_clip(surface, stack, damage_clip, &rect, &clip)) return 0;
  rc = draw_text_span(surface, font, &clip, dl->text_pool, dl->text_capacity,
                      cmd->text_offset, cmd->text_len, rect.x, rect.y,
                      cmd->color);
  if (rc != CAPYUI_DISPLAY_ADAPTER_OK) return rc;
  return 1;
}

static int clip_push(const struct capy_dl_cmd *cmd, struct clip_stack *stack,
                     struct capyui_display_adapter_stats *stats) {
  if (stack->depth >= CAPYUI_DISPLAY_ADAPTER_CLIP_STACK_MAX) {
    return CAPYUI_DISPLAY_ADAPTER_ERR_CLIP_STACK;
  }
  capy_rect_to_gui(&cmd->rect, &stack->rects[stack->depth]);
  stack->depth++;
  if (stack->depth > stack->max_depth) stack->max_depth = stack->depth;
  if (stats && stack->max_depth > stats->clip_depth_max) {
    stats->clip_depth_max = stack->max_depth;
  }
  return CAPYUI_DISPLAY_ADAPTER_OK;
}

static int clip_pop(struct clip_stack *stack) {
  if (stack->depth == 0u) return CAPYUI_DISPLAY_ADAPTER_ERR_CLIP_STACK;
  stack->depth--;
  return CAPYUI_DISPLAY_ADAPTER_OK;
}

int capyui_display_adapter_available(void) {
  return 1;
}

uint32_t capyui_display_adapter_schema_version(void) {
  return CAPY_DISPLAY_LIST_SCHEMA_VERSION;
}

int capyui_display_adapter_render(const struct capy_display_list *dl,
                                  struct gui_surface *surface,
                                  const struct gui_rect *damage_clip,
                                  struct capyui_display_adapter_stats *stats) {
  struct clip_stack stack;
  stats_zero(stats);
  if (!dl || !surface || !surface->pixels) return CAPYUI_DISPLAY_ADAPTER_ERR_INVALID;
  if (!dl->cmds && dl->count != 0u) return CAPYUI_DISPLAY_ADAPTER_ERR_INVALID;
  if (dl->version > CAPY_DISPLAY_LIST_SCHEMA_VERSION) {
    return CAPYUI_DISPLAY_ADAPTER_ERR_UNSUPPORTED_SCHEMA;
  }
  stack.depth = 0u;
  stack.max_depth = 0u;
  for (uint32_t i = 0u; i < dl->count; ++i) {
    const struct capy_dl_cmd *cmd = &dl->cmds[i];
    int rendered = 0;
    int rc = CAPYUI_DISPLAY_ADAPTER_OK;
    if (stats) stats->commands_seen++;
    switch (cmd->op) {
      case CAPY_DL_NONE:
        stats_skip(stats);
        break;
      case CAPY_DL_RECT:
        rendered = render_rect_cmd(cmd, surface, &stack, damage_clip);
        if (rendered) stats_render(stats);
        else stats_skip(stats);
        break;
      case CAPY_DL_IMAGE_REF:
        stats_unsupported(stats);
        break;
      case CAPY_DL_DIRTY_HINT:
        stats_skip(stats);
        break;
      case CAPY_DL_BORDER:
      case CAPY_DL_FOCUS_RING:
        rendered = render_border_cmd(cmd, surface, &stack, damage_clip);
        if (rendered) stats_render(stats);
        else stats_skip(stats);
        break;
      case CAPY_DL_TEXT:
        rc = render_text_cmd(dl, cmd, surface, &stack, damage_clip);
        if (rc < 0) return rc;
        if (rc > 0) stats_render(stats);
        else stats_skip(stats);
        break;
      case CAPY_DL_CLIP_PUSH:
        rc = clip_push(cmd, &stack, stats);
        if (rc != CAPYUI_DISPLAY_ADAPTER_OK) return rc;
        stats_skip(stats);
        break;
      case CAPY_DL_CLIP_POP:
        rc = clip_pop(&stack);
        if (rc != CAPYUI_DISPLAY_ADAPTER_OK) return rc;
        stats_skip(stats);
        break;
      case CAPY_DL_DPI_SCOPE:
      case CAPY_DL_TRANSFORM_PUSH:
      case CAPY_DL_TRANSFORM_POP:
      case CAPY_DL_PLUGIN_OP:
        stats_unsupported(stats);
        break;
      default:
        stats_unsupported(stats);
        break;
    }
  }
  if (stack.depth != 0u) return CAPYUI_DISPLAY_ADAPTER_ERR_CLIP_STACK;
  return CAPYUI_DISPLAY_ADAPTER_OK;
}

static int cmd_equal(const struct capy_dl_cmd *a, const struct capy_dl_cmd *b) {
  return a->op == b->op &&
         a->rect.x == b->rect.x &&
         a->rect.y == b->rect.y &&
         a->rect.width == b->rect.width &&
         a->rect.height == b->rect.height &&
         a->color == b->color &&
         a->text_offset == b->text_offset &&
         a->text_len == b->text_len &&
         a->border_width == b->border_width &&
         a->font_size == b->font_size &&
         a->font_id == b->font_id &&
         a->image_id == b->image_id &&
         a->transform.m00 == b->transform.m00 &&
         a->transform.m01 == b->transform.m01 &&
         a->transform.m02 == b->transform.m02 &&
         a->transform.m10 == b->transform.m10 &&
         a->transform.m11 == b->transform.m11 &&
         a->transform.m12 == b->transform.m12;
}

static int text_span_equal(const struct capy_display_list *a_dl,
                           const struct capy_dl_cmd *a,
                           const struct capy_display_list *b_dl,
                           const struct capy_dl_cmd *b) {
  if (a->op != CAPY_DL_TEXT) return 1;
  if (!a_dl->text_pool || !b_dl->text_pool) return a->text_len == 0u;
  if ((uint32_t)a->text_offset + (uint32_t)a->text_len >
          a_dl->text_capacity ||
      (uint32_t)b->text_offset + (uint32_t)b->text_len >
          b_dl->text_capacity) {
    return 0;
  }
  for (uint16_t i = 0u; i < a->text_len; ++i) {
    if (a_dl->text_pool[(uint32_t)a->text_offset + i] !=
        b_dl->text_pool[(uint32_t)b->text_offset + i]) {
      return 0;
    }
  }
  return 1;
}

static int display_cmd_equal(const struct capy_display_list *prev,
                             const struct capy_dl_cmd *pa,
                             const struct capy_display_list *next,
                             const struct capy_dl_cmd *pb) {
  if (!cmd_equal(pa, pb)) return 0;
  return text_span_equal(prev, pa, next, pb);
}

static int capy_rect_equal(const struct capy_ui_rect *a,
                           const struct capy_ui_rect *b) {
  return a->x == b->x && a->y == b->y && a->width == b->width &&
         a->height == b->height;
}

static int append_dirty_rect(struct gui_rect *out_dirty, uint32_t out_cap,
                             uint32_t *written,
                             const struct capy_ui_rect *rect) {
  struct gui_rect gui_rect;
  if (*written > 0u) {
    struct gui_rect *last = &out_dirty[*written - 1u];
    if (last->x == rect->x && last->y == rect->y &&
        last->width == rect->width && last->height == rect->height) {
      return 0;
    }
  }
  if (*written >= out_cap) return CAPYUI_DISPLAY_ADAPTER_ERR_DIRTY_OVERFLOW;
  capy_rect_to_gui(rect, &gui_rect);
  out_dirty[*written] = gui_rect;
  ++(*written);
  return 0;
}

int capyui_display_adapter_diff_damage(const struct capy_display_list *prev,
                                       const struct capy_display_list *next,
                                       struct gui_rect *out_dirty,
                                       uint32_t out_cap) {
  uint32_t prev_count = prev ? prev->count : 0u;
  uint32_t next_count = next ? next->count : 0u;
  uint32_t common;
  uint32_t written = 0u;
  if (out_cap > 0u && !out_dirty) return CAPYUI_DISPLAY_ADAPTER_ERR_INVALID;
  if ((prev_count > 0u && (!prev || !prev->cmds)) ||
      (next_count > 0u && (!next || !next->cmds))) {
    return CAPYUI_DISPLAY_ADAPTER_ERR_INVALID;
  }
  if (prev_count == 0u && next_count == 0u) return 0;
  common = (prev_count < next_count) ? prev_count : next_count;
  for (uint32_t i = 0u; i < common; ++i) {
    const struct capy_dl_cmd *pa = &prev->cmds[i];
    const struct capy_dl_cmd *pb = &next->cmds[i];
    if (display_cmd_equal(prev, pa, next, pb)) continue;
    if (append_dirty_rect(out_dirty, out_cap, &written, &pb->rect) != 0) {
      return CAPYUI_DISPLAY_ADAPTER_ERR_DIRTY_OVERFLOW;
    }
    if (!capy_rect_equal(&pa->rect, &pb->rect)) {
      if (append_dirty_rect(out_dirty, out_cap, &written, &pa->rect) != 0) {
        return CAPYUI_DISPLAY_ADAPTER_ERR_DIRTY_OVERFLOW;
      }
    }
  }
  for (uint32_t i = common; i < next_count; ++i) {
    if (append_dirty_rect(out_dirty, out_cap, &written,
                          &next->cmds[i].rect) != 0) {
      return CAPYUI_DISPLAY_ADAPTER_ERR_DIRTY_OVERFLOW;
    }
  }
  for (uint32_t i = common; i < prev_count; ++i) {
    if (append_dirty_rect(out_dirty, out_cap, &written,
                          &prev->cmds[i].rect) != 0) {
      return CAPYUI_DISPLAY_ADAPTER_ERR_DIRTY_OVERFLOW;
    }
  }
  return (int)written;
}

int capyui_display_adapter_render_window(struct gui_window *window,
                                         const struct capy_display_list *prev,
                                         const struct capy_display_list *next,
                                         struct capyui_display_adapter_stats *stats) {
  struct gui_rect dirty[CAPYUI_DISPLAY_ADAPTER_DIRTY_RECT_MAX];
  int dirty_count;
  int rc;
  stats_zero(stats);
  if (!window || !window->id || !next) return CAPYUI_DISPLAY_ADAPTER_ERR_INVALID;
  dirty_count = capyui_display_adapter_diff_damage(
      prev, next, dirty, CAPYUI_DISPLAY_ADAPTER_DIRTY_RECT_MAX);
  if (dirty_count == CAPYUI_DISPLAY_ADAPTER_ERR_DIRTY_OVERFLOW) {
    rc = capyui_display_adapter_render(next, &window->surface, 0, stats);
    if (rc != CAPYUI_DISPLAY_ADAPTER_OK) return rc;
    compositor_invalidate(window->id);
    return CAPYUI_DISPLAY_ADAPTER_OK;
  }
  if (dirty_count < 0) return dirty_count;
  if (dirty_count == 0) return CAPYUI_DISPLAY_ADAPTER_OK;
  for (int i = 0; i < dirty_count; ++i) {
    rc = capyui_display_adapter_render(next, &window->surface, &dirty[i],
                                       i == 0 ? stats : 0);
    if (rc != CAPYUI_DISPLAY_ADAPTER_OK) return rc;
    compositor_invalidate_rect(window->id, &dirty[i]);
  }
  return CAPYUI_DISPLAY_ADAPTER_OK;
}

int capyui_display_adapter_render_producer_window(
    struct gui_window *window,
    const struct capy_display_list *prev,
    struct capy_display_list *next,
    capyui_display_adapter_emit_fn emit,
    void *producer,
    struct capyui_display_adapter_stats *stats) {
  int rc;
  stats_zero(stats);
  if (!window || !window->id || !next || !emit) {
    return CAPYUI_DISPLAY_ADAPTER_ERR_INVALID;
  }
  rc = emit(producer, next);
  if (rc != 0) return CAPYUI_DISPLAY_ADAPTER_ERR_EMIT;
  return capyui_display_adapter_render_window(window, prev, next, stats);
}

#else

int capyui_display_adapter_available(void) {
  return 0;
}

uint32_t capyui_display_adapter_schema_version(void) {
  return 0u;
}

int capyui_display_adapter_render(const struct capy_display_list *dl,
                                  struct gui_surface *surface,
                                  const struct gui_rect *damage_clip,
                                  struct capyui_display_adapter_stats *stats) {
  (void)dl;
  (void)surface;
  (void)damage_clip;
  if (stats) {
    stats->commands_seen = 0u;
    stats->commands_rendered = 0u;
    stats->commands_skipped = 0u;
    stats->unsupported_ops = 0u;
    stats->clip_depth_max = 0u;
  }
  return CAPYUI_DISPLAY_ADAPTER_ERR_UNAVAILABLE;
}

int capyui_display_adapter_diff_damage(const struct capy_display_list *prev,
                                       const struct capy_display_list *next,
                                       struct gui_rect *out_dirty,
                                       uint32_t out_cap) {
  (void)prev;
  (void)next;
  (void)out_dirty;
  (void)out_cap;
  return CAPYUI_DISPLAY_ADAPTER_ERR_UNAVAILABLE;
}

int capyui_display_adapter_render_window(struct gui_window *window,
                                         const struct capy_display_list *prev,
                                         const struct capy_display_list *next,
                                         struct capyui_display_adapter_stats *stats) {
  (void)window;
  (void)prev;
  (void)next;
  if (stats) {
    stats->commands_seen = 0u;
    stats->commands_rendered = 0u;
    stats->commands_skipped = 0u;
    stats->unsupported_ops = 0u;
    stats->clip_depth_max = 0u;
  }
  return CAPYUI_DISPLAY_ADAPTER_ERR_UNAVAILABLE;
}

int capyui_display_adapter_render_producer_window(
    struct gui_window *window,
    const struct capy_display_list *prev,
    struct capy_display_list *next,
    capyui_display_adapter_emit_fn emit,
    void *producer,
    struct capyui_display_adapter_stats *stats) {
  (void)window;
  (void)prev;
  (void)next;
  (void)emit;
  (void)producer;
  if (stats) {
    stats->commands_seen = 0u;
    stats->commands_rendered = 0u;
    stats->commands_skipped = 0u;
    stats->unsupported_ops = 0u;
    stats->clip_depth_max = 0u;
  }
  return CAPYUI_DISPLAY_ADAPTER_ERR_UNAVAILABLE;
}

#endif
