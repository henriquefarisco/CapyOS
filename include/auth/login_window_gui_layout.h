/* login_window_gui_layout.h — Pure geometry for the real loginwindow panel.
 *
 * Owns: `struct login_window_gui_panel_layout` and the fail-closed
 * `login_window_gui_panel_layout_compute` helper that derives the panel
 * position, size, border thickness, title bar height and content box from a
 * framebuffer resolution. The module is deterministic, allocation-free,
 * side-effect free and does not touch real pixels, real input, real
 * authentication or persistent state. Downstream slices wire the geometry
 * into the framebuffer draw path.
 *
 * Scope is limited to geometry so it remains trivially auditable: security,
 * privacy and cryptography invariants stay unchanged, performance is constant
 * time, reliability is high because every path is exercised in static tests,
 * and the eventual GUI draw layer can rely on a single source of truth for
 * where the loginwindow panel belongs on screen.
 */
#ifndef AUTH_LOGIN_WINDOW_GUI_LAYOUT_H
#define AUTH_LOGIN_WINDOW_GUI_LAYOUT_H

#include <stdint.h>

#define LOGIN_WINDOW_GUI_PANEL_LAYOUT_VERSION 1

/* Minimum framebuffer resolution (in pixels) required to place the panel.
 * Below this the panel becomes invisible and the text login path stays
 * authoritative. */
#define LOGIN_WINDOW_GUI_PANEL_MIN_FB_WIDTH   320u
#define LOGIN_WINDOW_GUI_PANEL_MIN_FB_HEIGHT  240u

/* Maximum allowed panel size. The panel is kept compact even on 4K displays
 * so the login surface is always readable and never consumes the whole
 * screen. */
#define LOGIN_WINDOW_GUI_PANEL_MAX_WIDTH      640u
#define LOGIN_WINDOW_GUI_PANEL_MAX_HEIGHT     480u

/* Minimum panel size to host readable labels, input box and recovery hints. */
#define LOGIN_WINDOW_GUI_PANEL_MIN_WIDTH      260u
#define LOGIN_WINDOW_GUI_PANEL_MIN_HEIGHT     140u

/* Static ratios used to derive a panel that scales with the framebuffer but
 * stays comfortably inside the `MAX` bounds. */
#define LOGIN_WINDOW_GUI_PANEL_WIDTH_NUM      3u
#define LOGIN_WINDOW_GUI_PANEL_WIDTH_DEN      5u
#define LOGIN_WINDOW_GUI_PANEL_HEIGHT_NUM    1u
#define LOGIN_WINDOW_GUI_PANEL_HEIGHT_DEN    2u

/* Fixed chrome measurements. The border is drawn on both sides (top/bottom
 * and left/right), the title bar sits above the content area. */
#define LOGIN_WINDOW_GUI_PANEL_BORDER_PX         2u
#define LOGIN_WINDOW_GUI_PANEL_TITLE_BAR_HEIGHT 18u

/* Minimum content box required for password field + buttons + recovery hint. */
#define LOGIN_WINDOW_GUI_PANEL_MIN_CONTENT_WIDTH   96u
#define LOGIN_WINDOW_GUI_PANEL_MIN_CONTENT_HEIGHT  64u

struct login_window_gui_panel_layout {
    int version;
    int fb_valid;                  /* framebuffer has non-zero dimensions */
    int panel_visible;             /* panel fits and should be drawn */
    int fallback_text_required;    /* text login remains authoritative */
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t min_fb_width;
    uint32_t min_fb_height;
    uint32_t max_panel_width;
    uint32_t max_panel_height;
    uint32_t min_panel_width;
    uint32_t min_panel_height;
    uint32_t panel_x;
    uint32_t panel_y;
    uint32_t panel_width;
    uint32_t panel_height;
    uint32_t border_px;
    uint32_t title_bar_height;
    uint32_t content_x;
    uint32_t content_y;
    uint32_t content_width;
    uint32_t content_height;
    uint32_t min_content_width;
    uint32_t min_content_height;
    const char *state;
    const char *blocked_reason;
};

/* Computes a centered, clamped panel layout for the given framebuffer
 * dimensions. Returns -1 only when `out` is NULL. The function is pure: it
 * never touches real pixels, real input, real authentication or persistent
 * state and preserves the fail-closed invariants of the loginwindow stack.
 */
int login_window_gui_panel_layout_compute(uint32_t fb_width, uint32_t fb_height,
                                          struct login_window_gui_panel_layout *out);

#endif /* AUTH_LOGIN_WINDOW_GUI_LAYOUT_H */
