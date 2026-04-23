void html_viewer_open(void) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();

  if (g_viewer_open && g_viewer.window) {
    compositor_show_window(g_viewer.window->id);
    compositor_focus_window(g_viewer.window->id);
    return;
  }

  html_viewer_cleanup();
  kmemzero(&g_viewer, sizeof(g_viewer));
  g_viewer.focused_node_index = -1;

  g_viewer.window = compositor_create_window("CapyBrowser", 60, 40,
                                             640 + 160 * (scale - 1),
                                             480 + 140 * (scale - 1));
  if (!g_viewer.window) return;
  g_viewer.window->bg_color = theme->window_bg;
  g_viewer.window->border_color = theme->window_border;
  g_viewer.window->user_data = &g_viewer;
  g_viewer.window->on_paint = html_viewer_window_paint;
  g_viewer.window->on_scroll = html_viewer_window_scroll;
  g_viewer.window->on_key = html_viewer_window_key;
  g_viewer.window->on_mouse = html_viewer_window_mouse;
  g_viewer.window->on_close = html_viewer_on_close;
  g_viewer_open = 1;
  html_viewer_load_quick_start(&g_viewer);
  compositor_show_window(g_viewer.window->id);
  compositor_focus_window(g_viewer.window->id);
  compositor_invalidate(g_viewer.window->id);
}
