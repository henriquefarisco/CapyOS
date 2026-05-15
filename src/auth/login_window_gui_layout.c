/* login_window_gui_layout.c — Pure geometry for the real loginwindow panel.
 *
 * Derives the panel position, size, border thickness, title bar height and
 * inner content box from a framebuffer resolution. The function is
 * deterministic, constant time, allocation-free and side-effect free. It
 * never draws pixels, captures input, calls the authentication backend or
 * mutates persistent state, so the fail-closed invariants of the loginwindow
 * credential stack are preserved: the textual login path stays authoritative
 * until real GUI draw/input slices are wired on top of this layout.
 */
#include "auth/login_window_gui_layout.h"

static void login_window_gui_panel_layout_reset(
    struct login_window_gui_panel_layout *out) {
  if (!out) {
    return;
  }
  out->version = LOGIN_WINDOW_GUI_PANEL_LAYOUT_VERSION;
  out->fb_valid = 0;
  out->panel_visible = 0;
  out->fallback_text_required = 1;
  out->fb_width = 0;
  out->fb_height = 0;
  out->min_fb_width = LOGIN_WINDOW_GUI_PANEL_MIN_FB_WIDTH;
  out->min_fb_height = LOGIN_WINDOW_GUI_PANEL_MIN_FB_HEIGHT;
  out->max_panel_width = LOGIN_WINDOW_GUI_PANEL_MAX_WIDTH;
  out->max_panel_height = LOGIN_WINDOW_GUI_PANEL_MAX_HEIGHT;
  out->min_panel_width = LOGIN_WINDOW_GUI_PANEL_MIN_WIDTH;
  out->min_panel_height = LOGIN_WINDOW_GUI_PANEL_MIN_HEIGHT;
  out->panel_x = 0;
  out->panel_y = 0;
  out->panel_width = 0;
  out->panel_height = 0;
  out->border_px = 0;
  out->title_bar_height = 0;
  out->content_x = 0;
  out->content_y = 0;
  out->content_width = 0;
  out->content_height = 0;
  out->min_content_width = LOGIN_WINDOW_GUI_PANEL_MIN_CONTENT_WIDTH;
  out->min_content_height = LOGIN_WINDOW_GUI_PANEL_MIN_CONTENT_HEIGHT;
  out->state = "blocked";
  out->blocked_reason = "framebuffer-unavailable";
}

static uint32_t clamp_u32(uint32_t value, uint32_t low, uint32_t high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

int login_window_gui_panel_layout_compute(uint32_t fb_width, uint32_t fb_height,
                                          struct login_window_gui_panel_layout *out) {
  uint32_t panel_w = 0;
  uint32_t panel_h = 0;
  uint32_t chrome_w = 0;
  uint32_t chrome_h = 0;

  if (!out) {
    return -1;
  }
  login_window_gui_panel_layout_reset(out);

  if (fb_width == 0 || fb_height == 0) {
    out->blocked_reason = "framebuffer-zero";
    return 0;
  }
  out->fb_valid = 1;
  out->fb_width = fb_width;
  out->fb_height = fb_height;

  if (fb_width < LOGIN_WINDOW_GUI_PANEL_MIN_FB_WIDTH ||
      fb_height < LOGIN_WINDOW_GUI_PANEL_MIN_FB_HEIGHT) {
    out->blocked_reason = "framebuffer-too-small";
    return 0;
  }

  panel_w = (fb_width * LOGIN_WINDOW_GUI_PANEL_WIDTH_NUM) /
            LOGIN_WINDOW_GUI_PANEL_WIDTH_DEN;
  panel_h = (fb_height * LOGIN_WINDOW_GUI_PANEL_HEIGHT_NUM) /
            LOGIN_WINDOW_GUI_PANEL_HEIGHT_DEN;

  panel_w = clamp_u32(panel_w, LOGIN_WINDOW_GUI_PANEL_MIN_WIDTH,
                      LOGIN_WINDOW_GUI_PANEL_MAX_WIDTH);
  panel_h = clamp_u32(panel_h, LOGIN_WINDOW_GUI_PANEL_MIN_HEIGHT,
                      LOGIN_WINDOW_GUI_PANEL_MAX_HEIGHT);

  if (panel_w > fb_width || panel_h > fb_height) {
    out->blocked_reason = "panel-exceeds-framebuffer";
    return 0;
  }

  chrome_w = 2u * LOGIN_WINDOW_GUI_PANEL_BORDER_PX;
  chrome_h = LOGIN_WINDOW_GUI_PANEL_TITLE_BAR_HEIGHT +
             2u * LOGIN_WINDOW_GUI_PANEL_BORDER_PX;

  if (panel_w <= chrome_w || panel_h <= chrome_h) {
    out->blocked_reason = "panel-chrome-too-large";
    return 0;
  }

  if ((panel_w - chrome_w) < LOGIN_WINDOW_GUI_PANEL_MIN_CONTENT_WIDTH ||
      (panel_h - chrome_h) < LOGIN_WINDOW_GUI_PANEL_MIN_CONTENT_HEIGHT) {
    out->blocked_reason = "panel-content-too-small";
    return 0;
  }

  out->panel_width = panel_w;
  out->panel_height = panel_h;
  out->panel_x = (fb_width - panel_w) / 2u;
  out->panel_y = (fb_height - panel_h) / 2u;
  out->border_px = LOGIN_WINDOW_GUI_PANEL_BORDER_PX;
  out->title_bar_height = LOGIN_WINDOW_GUI_PANEL_TITLE_BAR_HEIGHT;
  out->content_x = out->panel_x + out->border_px;
  out->content_y = out->panel_y + out->border_px + out->title_bar_height;
  out->content_width = panel_w - chrome_w;
  out->content_height = panel_h - chrome_h;

  out->panel_visible = 1;
  out->fallback_text_required = 0;
  out->state = "panel-layout-ready";
  out->blocked_reason = "ready";
  return 0;
}
