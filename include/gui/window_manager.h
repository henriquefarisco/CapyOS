#ifndef GUI_WINDOW_MANAGER_H
#define GUI_WINDOW_MANAGER_H

#include <stdint.h>
#include "gui/compositor.h"

#define WM_TITLE_BAR_HEIGHT 24
#define WM_BORDER_WIDTH 2
#define WM_SNAP_THRESHOLD 8

/* 2026-05-02: hit area for resize grips. The visible border is
 * still WM_BORDER_WIDTH (2 px) but we accept clicks within
 * WM_RESIZE_GRIP_WIDTH (6 px) of the right/bottom edges so the
 * user does not have to land precisely on a 2 px line. The corner
 * grip uses the same width on both axes. Keep this >= the visible
 * border width and small enough that it does not overlap typical
 * window content padding. */
#define WM_RESIZE_GRIP_WIDTH 6

/* Minimum size enforced when a resize drag tries to shrink a window.
 * Smaller values would push title-bar buttons + content off-screen.
 * Apps that need a stricter floor can clamp again in their on_resize
 * handler. */
#define WM_MIN_WINDOW_W 120u
#define WM_MIN_WINDOW_H 80u

enum wm_drag_mode {
  WM_DRAG_NONE = 0,
  WM_DRAG_MOVE,
  WM_DRAG_RESIZE_RIGHT,
  WM_DRAG_RESIZE_BOTTOM,
  WM_DRAG_RESIZE_CORNER
};

struct window_manager {
  enum wm_drag_mode drag_mode;
  uint32_t drag_window_id;
  int32_t drag_start_x;
  int32_t drag_start_y;
  int32_t drag_win_x;
  int32_t drag_win_y;
  uint32_t drag_win_w;
  uint32_t drag_win_h;
  uint32_t screen_w;
  uint32_t screen_h;
};

void wm_init(struct window_manager *wm, uint32_t screen_w, uint32_t screen_h);
void wm_handle_mouse_down(struct window_manager *wm, int32_t x, int32_t y, uint8_t buttons);
void wm_handle_mouse_move(struct window_manager *wm, int32_t x, int32_t y);
void wm_handle_mouse_up(struct window_manager *wm);
void wm_snap_window(struct window_manager *wm, uint32_t window_id, int32_t x);

#endif
