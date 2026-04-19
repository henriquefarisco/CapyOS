#include "apps/text_editor.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "drivers/input/keyboard_layout.h"
#include "fs/vfs.h"
#include "util/kstring.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct text_editor_app g_editor;
static int g_editor_open = 0;

static void text_editor_cleanup(void) {
  g_editor.window = NULL;
  g_editor_open = 0;
}

static void text_editor_on_close(struct gui_window *win) {
  (void)win;
  text_editor_cleanup();
}

static void text_editor_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  text_editor_paint((struct text_editor_app *)win->user_data);
}

static void text_editor_window_key(struct gui_window *win, uint32_t keycode,
                                   uint8_t mods) {
  if (!win || !win->user_data) return;
  struct text_editor_app *app = (struct text_editor_app *)win->user_data;

  /* Ctrl+S → save */
  if ((mods & 0x01) && (keycode == 's' || keycode == 'S' ||
                        keycode == 0x13 /* raw Ctrl+S */)) {
    text_editor_save(app);
    return;
  }

  char ch = (keycode < 0x80) ? (char)keycode : 0;
  text_editor_handle_key(app, keycode, ch);
}

static void text_editor_window_scroll(struct gui_window *win, int32_t delta) {
  if (!win || !win->user_data) return;
  struct text_editor_app *app = (struct text_editor_app *)win->user_data;
  if (delta > 0 && app->scroll_offset > 0)
    app->scroll_offset -= (delta > app->scroll_offset) ? app->scroll_offset : delta;
  else if (delta < 0 && app->scroll_offset < app->line_count - 1)
    app->scroll_offset += (-delta);
  if (app->scroll_offset >= app->line_count && app->line_count > 0)
    app->scroll_offset = app->line_count - 1;
}

void text_editor_open(const char *path) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();

  /* If already open, just focus the existing window */
  if (g_editor_open && g_editor.window) {
    compositor_show_window(g_editor.window->id);
    compositor_focus_window(g_editor.window->id);
    return;
  }

  /* Clean up stale state */
  text_editor_cleanup();
  kmemzero(&g_editor, sizeof(g_editor));

  g_editor.window = compositor_create_window("Text Editor", 100, 80,
                                             600 + 160 * (scale - 1),
                                             400 + 140 * (scale - 1));
  if (!g_editor.window) return;
  g_editor.window->bg_color = theme->window_bg;
  g_editor.window->border_color = theme->window_border;
  g_editor.window->user_data = &g_editor;
  g_editor.window->on_paint = text_editor_window_paint;
  g_editor.window->on_key = text_editor_window_key;
  g_editor.window->on_scroll = text_editor_window_scroll;
  g_editor.window->on_close = text_editor_on_close;
  compositor_show_window(g_editor.window->id);
  compositor_focus_window(g_editor.window->id);
  g_editor_open = 1;

  g_editor.line_count = 1;
  g_editor.lines[0][0] = '\0';

  if (path) {
    kstrcpy(g_editor.path, EDITOR_PATH_MAX, path);
    struct file *f = vfs_open(path, 0x1);
    if (f) {
      char buf[4096];
      long rd = vfs_read(f, buf, sizeof(buf) - 1);
      vfs_close(f);
      if (rd > 0) {
        buf[rd] = '\0';
        g_editor.line_count = 0;
        int col = 0;
        for (long i = 0; i < rd && g_editor.line_count < EDITOR_MAX_LINES; i++) {
          if (buf[i] == '\n' || col >= EDITOR_LINE_MAX - 1) {
            g_editor.lines[g_editor.line_count][col] = '\0';
            g_editor.line_count++;
            col = 0;
          } else {
            g_editor.lines[g_editor.line_count][col++] = buf[i];
          }
        }
        if (col > 0 || g_editor.line_count == 0) {
          g_editor.lines[g_editor.line_count][col] = '\0';
          g_editor.line_count++;
        }
      }
    }
  } else {
    kstrcpy(g_editor.path, EDITOR_PATH_MAX, "untitled.txt");
  }
}

void text_editor_save(struct text_editor_app *app) {
  if (!app || !app->path[0]) return;
  struct vfs_metadata meta = {0, 0, 0x1FF};
  vfs_create(app->path, 0x1, &meta);
  struct file *f = vfs_open(app->path, 0x2);
  if (!f) return;
  for (int i = 0; i < app->line_count; i++) {
    vfs_write(f, app->lines[i], kstrlen(app->lines[i]));
    vfs_write(f, "\n", 1);
  }
  vfs_close(f);
  app->modified = 0;
}

void text_editor_handle_key(struct text_editor_app *app, uint32_t keycode, char ch) {
  if (!app) return;

  /* Handle arrow keys */
  if (keycode == KEY_UP) {
    if (app->cursor_line > 0) {
      app->cursor_line--;
      int len = (int)kstrlen(app->lines[app->cursor_line]);
      if (app->cursor_col > len) app->cursor_col = len;
    }
    return;
  }
  if (keycode == KEY_DOWN) {
    if (app->cursor_line < app->line_count - 1) {
      app->cursor_line++;
      int len = (int)kstrlen(app->lines[app->cursor_line]);
      if (app->cursor_col > len) app->cursor_col = len;
    }
    return;
  }
  if (keycode == KEY_LEFT) {
    if (app->cursor_col > 0) app->cursor_col--;
    return;
  }
  if (keycode == KEY_RIGHT) {
    int len = (int)kstrlen(app->lines[app->cursor_line]);
    if (app->cursor_col < len) app->cursor_col++;
    return;
  }

  if (ch == '\b') {
    if (app->cursor_col > 0) {
      char *line = app->lines[app->cursor_line];
      int len = (int)kstrlen(line);
      for (int i = app->cursor_col - 1; i < len; i++) line[i] = line[i + 1];
      app->cursor_col--;
      app->modified = 1;
    } else if (app->cursor_line > 0) {
      /* Merge with previous line */
      int prev_len = (int)kstrlen(app->lines[app->cursor_line - 1]);
      char *prev = app->lines[app->cursor_line - 1];
      char *cur = app->lines[app->cursor_line];
      int cur_len = (int)kstrlen(cur);
      if (prev_len + cur_len < EDITOR_LINE_MAX - 1) {
        for (int i = 0; i < cur_len; i++) prev[prev_len + i] = cur[i];
        prev[prev_len + cur_len] = '\0';
      }
      for (int i = app->cursor_line; i < app->line_count - 1; i++)
        kstrcpy(app->lines[i], EDITOR_LINE_MAX, app->lines[i + 1]);
      app->line_count--;
      app->cursor_line--;
      app->cursor_col = prev_len;
      app->modified = 1;
    }
    return;
  }

  if (ch == '\n' || ch == '\r') {
    if (app->line_count < EDITOR_MAX_LINES) {
      for (int i = app->line_count; i > app->cursor_line + 1; i--)
        kstrcpy(app->lines[i], EDITOR_LINE_MAX, app->lines[i - 1]);
      app->line_count++;
      char *cur = app->lines[app->cursor_line];
      int col = app->cursor_col;
      kstrcpy(app->lines[app->cursor_line + 1], EDITOR_LINE_MAX, cur + col);
      cur[col] = '\0';
      app->cursor_line++;
      app->cursor_col = 0;
      app->modified = 1;
    }
    return;
  }

  if (ch >= 32 && ch < 127) {
    char *line = app->lines[app->cursor_line];
    int len = (int)kstrlen(line);
    if (len < EDITOR_LINE_MAX - 1) {
      for (int i = len; i >= app->cursor_col; i--) line[i + 1] = line[i];
      line[app->cursor_col] = ch;
      app->cursor_col++;
      app->modified = 1;
    }
  }
}

void text_editor_paint(struct text_editor_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  if (!f) return;

  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = theme->window_bg;
  }

  /* Title bar info */
  font_draw_string(s, f, 4, 2, app->path, theme->accent);
  if (app->modified)
    font_draw_string(s, f, (int32_t)(s->width - 80), 2, "[modified]",
                     theme->accent_alt);

  int32_t y = 20;
  uint32_t gh = f->glyph_height;
  for (int i = app->scroll_offset; i < app->line_count && y + (int32_t)gh < (int32_t)s->height; i++) {
    /* Line number */
    char lnum[8];
    int ln = i + 1, lp = 0;
    if (ln < 10) { lnum[lp++] = ' '; lnum[lp++] = ' '; }
    else if (ln < 100) { lnum[lp++] = ' '; }
    char tmp[6]; int tp = 0;
    while (ln > 0) { tmp[tp++] = '0' + (ln % 10); ln /= 10; }
    for (int j = tp - 1; j >= 0; j--) lnum[lp++] = tmp[j];
    lnum[lp] = '\0';
    font_draw_string(s, f, 4, y, lnum, theme->text_muted);

    /* Line content */
    font_draw_string(s, f, 40, y, app->lines[i], theme->text);

    /* Cursor */
    if (i == app->cursor_line) {
      int32_t cx = 40 + app->cursor_col * (int32_t)f->glyph_width;
      for (uint32_t cy = 0; cy < gh; cy++) {
        int32_t py = y + (int32_t)cy;
        if (py >= 0 && (uint32_t)py < s->height && cx >= 0 && (uint32_t)cx < s->width) {
          uint32_t *pline = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)py * s->pitch);
          pline[cx] = theme->accent;
        }
      }
    }
    y += (int32_t)gh;
  }

  /* Status bar: Ln X, Col Y */
  {
    int32_t bar_y = (int32_t)s->height - (int32_t)gh - 4;
    /* Background strip */
    for (uint32_t sx = 0; sx < s->width; sx++) {
      uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)bar_y * s->pitch);
      line[sx] = theme->terminal_bg;
    }
    char status[64];
    int sp = 0;
    status[sp++] = 'L'; status[sp++] = 'n'; status[sp++] = ' ';
    { int v = app->cursor_line + 1; char t[8]; int tp = 0;
      if (v == 0) t[tp++] = '0';
      else { while (v > 0) { t[tp++] = '0' + (v % 10); v /= 10; } }
      for (int i = tp - 1; i >= 0; i--) status[sp++] = t[i]; }
    status[sp++] = ','; status[sp++] = ' ';
    status[sp++] = 'C'; status[sp++] = 'o'; status[sp++] = 'l'; status[sp++] = ' ';
    { int v = app->cursor_col + 1; char t[8]; int tp = 0;
      if (v == 0) t[tp++] = '0';
      else { while (v > 0) { t[tp++] = '0' + (v % 10); v /= 10; } }
      for (int i = tp - 1; i >= 0; i--) status[sp++] = t[i]; }
    status[sp] = '\0';
    font_draw_string(s, f, 4, bar_y, status, theme->text_muted);

    if (app->modified)
      font_draw_string(s, f, (int32_t)(s->width - 72), bar_y, "[Ctrl+S]",
                       theme->accent_alt);
  }
}
