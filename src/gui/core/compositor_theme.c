/*
 * src/gui/core/compositor_theme.c
 *
 * Theme palette and UI scale management for the compositor.
 * Split out of `compositor.c` on 2026-05-02 (Etapa 2 audit) to
 * keep each TU below the 900-line layout-audit cap.
 *
 * Adding a new theme means:
 *   1. Add a `comp_streq(theme, "<name>")` branch to
 *      `compositor_apply_theme` setting all 16 fields of
 *      `g_theme` plus `comp_wallpaper` and ending with
 *      `comp_request_scene_redraw()`.
 *   2. Document any legacy aliases via additional `||
 *      comp_streq(theme, "<alias>")` so historical config.ini
 *      values keep working.
 *   3. Update the shell `config-theme` command list and the
 *      setup wizard UI strings.
 */

#include "internal/compositor_internal.h"

void compositor_apply_theme(const char *theme, uint32_t screen_w,
                            uint32_t screen_h) {
  uint8_t scale = (screen_w >= 1280 || screen_h >= 900) ? 2 : 1;

  if (theme && (comp_streq(theme, "classic-modern") ||
                comp_streq(theme, "classic") ||
                comp_streq(theme, "ubuntu7"))) {
    g_theme.wallpaper = 0x002D123A;
    g_theme.window_bg = 0x001B2535;
    g_theme.window_border = 0x0060A5FA;
    g_theme.title_active = 0x00E95420;
    g_theme.title_inactive = 0x003B4A60;
    g_theme.text = 0x00F8FAFC;
    g_theme.text_muted = 0x00CBD5E1;
    g_theme.accent = 0x00E95420;
    g_theme.accent_alt = 0x003B82F6;
    g_theme.accent_text = 0x00FFFFFF;
    g_theme.taskbar_bg = 0x00101824;
    g_theme.taskbar_fg = 0x00F8FAFC;
    g_theme.taskbar_highlight = 0x003B82F6;
    g_theme.terminal_bg = 0x000F172A;
    g_theme.terminal_fg = 0x00F8FAFC;
    g_theme.ui_scale = scale;
    comp_wallpaper = g_theme.wallpaper;
    comp_request_scene_redraw();
    return;
  }

  if (theme && comp_streq(theme, "ocean")) {
    g_theme.wallpaper = 0x00041024;
    g_theme.window_bg = 0x000C213A;
    g_theme.window_border = 0x0021476A;
    g_theme.title_active = 0x0035B7FF;
    g_theme.title_inactive = 0x0021476A;
    g_theme.text = 0x00DDF6FF;
    g_theme.text_muted = 0x0089AFCF;
    g_theme.accent = 0x005FD5FF;
    g_theme.accent_alt = 0x0021476A;
    g_theme.accent_text = 0x00041024;
    g_theme.taskbar_bg = 0x0008192D;
    g_theme.taskbar_fg = 0x00DDF6FF;
    g_theme.taskbar_highlight = 0x0021476A;
    g_theme.terminal_bg = 0x000A1B3A;
    g_theme.terminal_fg = 0x00DDF6FF;
    g_theme.ui_scale = scale;
    comp_wallpaper = g_theme.wallpaper;
    comp_request_scene_redraw();
    return;
  }

  if (theme && comp_streq(theme, "forest")) {
    g_theme.wallpaper = 0x000A1710;
    g_theme.window_bg = 0x0015231A;
    g_theme.window_border = 0x00284A31;
    g_theme.title_active = 0x002FAE5B;
    g_theme.title_inactive = 0x00284A31;
    g_theme.text = 0x00E9F8E7;
    g_theme.text_muted = 0x0092B7A6;
    g_theme.accent = 0x0048D778;
    g_theme.accent_alt = 0x00284A31;
    g_theme.accent_text = 0x000A1710;
    g_theme.taskbar_bg = 0x000D1A12;
    g_theme.taskbar_fg = 0x00E9F8E7;
    g_theme.taskbar_highlight = 0x00284A31;
    g_theme.terminal_bg = 0x000F2415;
    g_theme.terminal_fg = 0x00E9F8E7;
    g_theme.ui_scale = scale;
    comp_wallpaper = g_theme.wallpaper;
    comp_request_scene_redraw();
    return;
  }

  if (theme && (comp_streq(theme, "love") || comp_streq(theme, "rosa") ||
                comp_streq(theme, "pink"))) {
    /* Tema "love": paleta moderna com magenta/coral. Os apelidos
     * "rosa"/"pink" foram removidos da UI mas continuam aceitos
     * para nao quebrar /system/config.ini de instalacoes antigas. */
    g_theme.wallpaper        = 0x001F0A14; /* rose 950 */
    g_theme.window_bg        = 0x002B1521; /* rose 900 */
    g_theme.window_border    = 0x00F9A8D4; /* pink 300 */
    g_theme.title_active     = 0x00EC4899; /* pink 500 */
    g_theme.title_inactive   = 0x004A1D3A; /* rose 800 */
    g_theme.text             = 0x00FFE4F0; /* rose 50 */
    g_theme.text_muted       = 0x00E8B9D0; /* rose 200 */
    g_theme.accent           = 0x00F472B6; /* pink 400 */
    g_theme.accent_alt       = 0x004A1D3A; /* rose 800 */
    g_theme.accent_text      = 0x00FFFFFF;
    g_theme.taskbar_bg       = 0x00200A14;
    g_theme.taskbar_fg       = 0x00FFE4F0;
    g_theme.taskbar_highlight = 0x00EC4899;
    g_theme.terminal_bg      = 0x001F0A14;
    g_theme.terminal_fg      = 0x00FFE4F0;
    g_theme.ui_scale         = scale;
    comp_wallpaper           = g_theme.wallpaper;
    comp_request_scene_redraw();
    return;
  }

  if (theme && comp_streq(theme, "high-contrast")) {
    g_theme.wallpaper = 0x00000000;
    g_theme.window_bg = 0x00000000;
    g_theme.window_border = 0x00FFFFFF;
    g_theme.title_active = 0x00FFFF00;
    g_theme.title_inactive = 0x00808080;
    g_theme.text = 0x00FFFFFF;
    g_theme.text_muted = 0x00C0C0C0;
    g_theme.accent = 0x00FFFF00;
    g_theme.accent_alt = 0x00404040;
    g_theme.accent_text = 0x00000000;
    g_theme.taskbar_bg = 0x00000000;
    g_theme.taskbar_fg = 0x00FFFFFF;
    g_theme.taskbar_highlight = 0x00404040;
    g_theme.terminal_bg = 0x00000000;
    g_theme.terminal_fg = 0x00FFFFFF;
    g_theme.ui_scale = scale;
    comp_wallpaper = g_theme.wallpaper;
    comp_request_scene_redraw();
    return;
  }

  /* Default `capyos` theme (refresh 2026-05-01 PM): wallpaper indigo
   * profundo, body slate escuro, borda mint claro, accent verde
   * CapyOS. Bordas + corpo + wallpaper formam 3 patamares de luma
   * distintos para legibilidade. */
  g_theme.wallpaper        = 0x00091828; /* indigo 950 */
  g_theme.window_bg        = 0x00162232; /* slate 850 */
  g_theme.window_border    = 0x005EE9B5; /* mint 300 */
  g_theme.title_active     = 0x0026C88A; /* CapyOS green */
  g_theme.title_inactive   = 0x00374559; /* slate 700 */
  g_theme.text             = 0x00F0FBF6;
  g_theme.text_muted       = 0x00B0C4D4; /* slate 300 */
  g_theme.accent           = 0x0026C88A;
  g_theme.accent_alt       = 0x002F4A5C; /* teal-slate */
  g_theme.accent_text      = 0x00091828;
  g_theme.taskbar_bg       = 0x00060F18;
  g_theme.taskbar_fg       = 0x00F0FBF6;
  g_theme.taskbar_highlight = 0x0026C88A;
  g_theme.terminal_bg      = 0x00091828;
  g_theme.terminal_fg      = 0x00F0FBF6;
  g_theme.ui_scale         = scale;
  comp_wallpaper           = g_theme.wallpaper;
  comp_request_scene_redraw();
}

const struct gui_theme_palette *compositor_theme(void) {
  return &g_theme;
}

uint8_t compositor_ui_scale(void) {
  return g_theme.ui_scale;
}
