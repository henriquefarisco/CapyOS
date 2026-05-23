#include "gui/core/internal/compositor_internal.h"

#include <stdint.h>

struct gui_rect comp_dirty_rects[COMP_DIRTY_RECT_MAX];
uint32_t comp_dirty_rect_count = 0;
int comp_full_redraw_pending = 1;

static int rect_empty(const struct gui_rect *rect) {
  return !rect || rect->width == 0u || rect->height == 0u;
}

static int rect_end(int32_t start, uint32_t extent) {
  int64_t end = (int64_t)start + (int64_t)extent;
  if (end > (int64_t)INT32_MAX) return INT32_MAX;
  if (end < (int64_t)INT32_MIN) return INT32_MIN;
  return (int)end;
}

static int rects_touch_or_overlap(const struct gui_rect *a,
                                  const struct gui_rect *b) {
  int32_t ax1;
  int32_t ay1;
  int32_t bx1;
  int32_t by1;
  if (rect_empty(a) || rect_empty(b)) return 0;
  ax1 = rect_end(a->x, a->width);
  ay1 = rect_end(a->y, a->height);
  bx1 = rect_end(b->x, b->width);
  by1 = rect_end(b->y, b->height);
  return a->x <= bx1 && b->x <= ax1 && a->y <= by1 && b->y <= ay1;
}

static int rect_union_changed(struct gui_rect *dst,
                              const struct gui_rect *src) {
  int32_t x0;
  int32_t y0;
  int32_t x1;
  int32_t y1;
  struct gui_rect merged;
  if (rect_empty(dst) || rect_empty(src)) return 0;
  x0 = dst->x < src->x ? dst->x : src->x;
  y0 = dst->y < src->y ? dst->y : src->y;
  x1 = rect_end(dst->x, dst->width);
  y1 = rect_end(dst->y, dst->height);
  {
    int32_t sx1 = rect_end(src->x, src->width);
    int32_t sy1 = rect_end(src->y, src->height);
    if (sx1 > x1) x1 = sx1;
    if (sy1 > y1) y1 = sy1;
  }
  merged.x = x0;
  merged.y = y0;
  merged.width = (uint32_t)(x1 - x0);
  merged.height = (uint32_t)(y1 - y0);
  if (dst->x == merged.x && dst->y == merged.y &&
      dst->width == merged.width && dst->height == merged.height) {
    return 0;
  }
  *dst = merged;
  return 1;
}

static void merge_dirty_rect_neighbors(uint32_t index) {
  uint32_t i = 0u;
  if (index >= comp_dirty_rect_count) return;
  while (i < comp_dirty_rect_count) {
    if (i == index) {
      ++i;
      continue;
    }
    if (rects_touch_or_overlap(&comp_dirty_rects[index], &comp_dirty_rects[i])) {
      rect_union_changed(&comp_dirty_rects[index], &comp_dirty_rects[i]);
      for (uint32_t j = i; j + 1u < comp_dirty_rect_count; ++j) {
        comp_dirty_rects[j] = comp_dirty_rects[j + 1u];
      }
      --comp_dirty_rect_count;
      if (i < index) --index;
      i = 0u;
      continue;
    }
    ++i;
  }
}

void comp_dirty_mark_full_redraw(void) {
  comp_dirty_rect_count = 0;
  comp_full_redraw_pending = 1;
}

int comp_dirty_append_rect(const struct gui_rect *rect) {
  if (rect_empty(rect)) return 0;
  for (uint32_t i = 0u; i < comp_dirty_rect_count; ++i) {
    if (rects_touch_or_overlap(&comp_dirty_rects[i], rect)) {
      if (rect_union_changed(&comp_dirty_rects[i], rect)) {
        comp_scene_dirty = 1;
        comp_full_presented = 0;
        comp_stats.dirty_rects++;
        merge_dirty_rect_neighbors(i);
      }
      return 1;
    }
  }
  if (comp_dirty_rect_count >= COMP_DIRTY_RECT_MAX) {
    comp_request_scene_redraw();
    return 0;
  }
  comp_dirty_rects[comp_dirty_rect_count++] = *rect;
  comp_scene_dirty = 1;
  comp_full_presented = 0;
  comp_stats.dirty_rects++;
  return 1;
}

int comp_window_rect_to_screen(struct gui_window *win,
                               const struct gui_rect *rect,
                               struct gui_rect *out) {
  int32_t local_x0;
  int32_t local_y0;
  int32_t local_x1;
  int32_t local_y1;
  int32_t screen_x0;
  int32_t screen_y0;
  int32_t screen_x1;
  int32_t screen_y1;
  if (!win || rect_empty(rect) || !out) return 0;
  local_x0 = rect->x;
  local_y0 = rect->y;
  local_x1 = rect_end(rect->x, rect->width);
  local_y1 = rect_end(rect->y, rect->height);
  if (local_x0 < 0) local_x0 = 0;
  if (local_y0 < 0) local_y0 = 0;
  if (local_x1 > (int32_t)win->surface.width) local_x1 = (int32_t)win->surface.width;
  if (local_y1 > (int32_t)win->surface.height) local_y1 = (int32_t)win->surface.height;
  if (local_x0 >= local_x1 || local_y0 >= local_y1) return 0;
  screen_x0 = win->frame.x + local_x0;
  screen_y0 = win->frame.y + local_y0;
  screen_x1 = win->frame.x + local_x1;
  screen_y1 = win->frame.y + local_y1;
  if (screen_x0 < 0) screen_x0 = 0;
  if (screen_y0 < 0) screen_y0 = 0;
  if (screen_x1 > (int32_t)comp_width) screen_x1 = (int32_t)comp_width;
  if (screen_y1 > (int32_t)comp_height) screen_y1 = (int32_t)comp_height;
  if (screen_x0 >= screen_x1 || screen_y0 >= screen_y1) return 0;
  out->x = screen_x0;
  out->y = screen_y0;
  out->width = (uint32_t)(screen_x1 - screen_x0);
  out->height = (uint32_t)(screen_y1 - screen_y0);
  return 1;
}
