#include "gui/window_manager.h"
#include "gui/compositor.h"
#include <stddef.h>

void wm_init(struct window_manager *wm, uint32_t screen_w, uint32_t screen_h) {
  if (!wm) return;
  wm->drag_mode = WM_DRAG_NONE;
  wm->drag_window_id = 0;
  wm->screen_w = screen_w;
  wm->screen_h = screen_h;
}

/* Detect which resize handle (if any) the cursor is over for a
 * resizable, decorated window. Returns WM_DRAG_NONE if the point
 * is in the title bar / interior / outside the window. The corner
 * grip wins over the right/bottom edges so the user can drag
 * diagonally without having to land on the exact 1 px overlap. */
static enum wm_drag_mode hit_test_resize_zone(struct gui_window *win,
                                               int32_t x, int32_t y) {
  if (!win || !win->resizable) return WM_DRAG_NONE;
  int32_t right  = win->frame.x + (int32_t)win->frame.width;
  int32_t bottom = win->frame.y + (int32_t)win->frame.height;
  int32_t grip   = (int32_t)WM_RESIZE_GRIP_WIDTH;

  /* Outside the window's resize-active rectangle entirely. We allow
   * a few pixels of overshoot past the edge so a rapid drag does
   * not lose the grip the moment the cursor crosses the border. */
  if (x < win->frame.x - grip || x > right + grip) return WM_DRAG_NONE;
  if (y < win->frame.y - grip || y > bottom + grip) return WM_DRAG_NONE;

  int near_right  = (x >= right - grip);
  int near_bottom = (y >= bottom - grip);

  if (near_right && near_bottom) return WM_DRAG_RESIZE_CORNER;
  /* Right edge: must be within the vertical span of the window
   * (not the title bar above) so the click doesn't conflict with
   * the close button or the title-bar drag region. */
  if (near_right && y >= win->frame.y && y < bottom) {
    return WM_DRAG_RESIZE_RIGHT;
  }
  /* Bottom edge: must be within the horizontal span. */
  if (near_bottom && x >= win->frame.x && x < right) {
    return WM_DRAG_RESIZE_BOTTOM;
  }
  return WM_DRAG_NONE;
}

void wm_handle_mouse_down(struct window_manager *wm, int32_t x, int32_t y,
                           uint8_t buttons) {
  if (!wm || !(buttons & 1)) return;
  struct gui_window *win = compositor_window_at(x, y);
  if (!win || !win->movable) return;
  compositor_focus_window(win->id);

  /* Resize takes priority over move so a click on the bottom-right
   * grip doesn't accidentally start a move drag (the cursor at
   * `right - grip` is technically still inside the window for
   * `compositor_window_at`). */
  enum wm_drag_mode resize_zone = hit_test_resize_zone(win, x, y);
  if (resize_zone != WM_DRAG_NONE) {
    wm->drag_mode = resize_zone;
    wm->drag_window_id = win->id;
    wm->drag_start_x = x;
    wm->drag_start_y = y;
    wm->drag_win_w = win->frame.width;
    wm->drag_win_h = win->frame.height;
    return;
  }

  if (win->decorated && y >= win->frame.y - (int32_t)WM_TITLE_BAR_HEIGHT &&
      y < win->frame.y) {
    wm->drag_mode = WM_DRAG_MOVE;
    wm->drag_window_id = win->id;
    wm->drag_start_x = x;
    wm->drag_start_y = y;
    wm->drag_win_x = win->frame.x;
    wm->drag_win_y = win->frame.y;
    return;
  }
}

void wm_handle_mouse_move(struct window_manager *wm, int32_t x, int32_t y) {
  if (!wm || wm->drag_mode == WM_DRAG_NONE) return;
  int32_t dx = x - wm->drag_start_x;
  int32_t dy = y - wm->drag_start_y;
  if (wm->drag_mode == WM_DRAG_MOVE) {
    int32_t nx = wm->drag_win_x + dx;
    int32_t ny = wm->drag_win_y + dy;
    if (nx < WM_SNAP_THRESHOLD && nx > -WM_SNAP_THRESHOLD) nx = 0;
    if (ny < WM_SNAP_THRESHOLD && ny > -WM_SNAP_THRESHOLD) ny = 0;
    compositor_move_window(wm->drag_window_id, nx, ny);
    return;
  }

  /* Compute prospective new dimensions. The CORNER mode tracks
   * both axes; RIGHT only width, BOTTOM only height. We always
   * read from the original drag-start size (`drag_win_w/h`) so the
   * resize is absolute against the click anchor -- accumulating
   * deltas across mouse moves would make the window drift if the
   * compositor clamps a step. */
  int32_t nw_signed = (int32_t)wm->drag_win_w;
  int32_t nh_signed = (int32_t)wm->drag_win_h;
  if (wm->drag_mode == WM_DRAG_RESIZE_RIGHT
      || wm->drag_mode == WM_DRAG_RESIZE_CORNER) {
    nw_signed = (int32_t)wm->drag_win_w + dx;
  }
  if (wm->drag_mode == WM_DRAG_RESIZE_BOTTOM
      || wm->drag_mode == WM_DRAG_RESIZE_CORNER) {
    nh_signed = (int32_t)wm->drag_win_h + dy;
  }
  if (nw_signed < (int32_t)WM_MIN_WINDOW_W) nw_signed = (int32_t)WM_MIN_WINDOW_W;
  if (nh_signed < (int32_t)WM_MIN_WINDOW_H) nh_signed = (int32_t)WM_MIN_WINDOW_H;
  compositor_resize_window(wm->drag_window_id,
                           (uint32_t)nw_signed, (uint32_t)nh_signed);
}

void wm_handle_mouse_up(struct window_manager *wm) {
  if (!wm) return;
  wm->drag_mode = WM_DRAG_NONE;
  wm->drag_window_id = 0;
}

void wm_snap_window(struct window_manager *wm, uint32_t window_id, int32_t x) {
  if (!wm) return;
  if (x <= WM_SNAP_THRESHOLD) {
    compositor_move_window(window_id, 0, 0);
    compositor_resize_window(window_id, wm->screen_w / 2, wm->screen_h - 32);
  } else if (x >= (int32_t)wm->screen_w - WM_SNAP_THRESHOLD) {
    compositor_move_window(window_id, (int32_t)(wm->screen_w / 2), 0);
    compositor_resize_window(window_id, wm->screen_w / 2, wm->screen_h - 32);
  }
}
