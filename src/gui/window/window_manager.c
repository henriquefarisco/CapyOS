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

void wm_handle_mouse_down(struct window_manager *wm, int32_t x, int32_t y,
                           uint8_t buttons) {
  if (!wm || !(buttons & 1)) return;
  struct gui_window *win = compositor_window_at(x, y);
  if (!win || !win->movable) return;
  compositor_focus_window(win->id);
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
  if (win->resizable) {
    int32_t right = win->frame.x + (int32_t)win->frame.width;
    int32_t bottom = win->frame.y + (int32_t)win->frame.height;
    if (x >= right - WM_BORDER_WIDTH && y >= bottom - WM_BORDER_WIDTH) {
      wm->drag_mode = WM_DRAG_RESIZE_CORNER;
      wm->drag_window_id = win->id;
      wm->drag_start_x = x;
      wm->drag_start_y = y;
      wm->drag_win_w = win->frame.width;
      wm->drag_win_h = win->frame.height;
    }
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
  } else if (wm->drag_mode == WM_DRAG_RESIZE_CORNER) {
    uint32_t nw = (uint32_t)((int32_t)wm->drag_win_w + dx);
    uint32_t nh = (uint32_t)((int32_t)wm->drag_win_h + dy);
    if (nw < 100) nw = 100;
    if (nh < 60) nh = 60;
    compositor_resize_window(wm->drag_window_id, nw, nh);
  }
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
