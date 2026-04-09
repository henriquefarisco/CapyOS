#include "apps/text_editor.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "fs/vfs.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct text_editor_app g_editor;

static void ed_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static void ed_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = '\0';
}
static size_t ed_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

void text_editor_open(const char *path) {
  ed_memset(&g_editor, 0, sizeof(g_editor));
  g_editor.window = compositor_create_window("Text Editor", 100, 80, 600, 400);
  if (!g_editor.window) return;
  g_editor.window->bg_color = 0x1E1E2E;
  compositor_show_window(g_editor.window->id);
  compositor_focus_window(g_editor.window->id);

  g_editor.line_count = 1;
  g_editor.lines[0][0] = '\0';

  if (path) {
    ed_strcpy(g_editor.path, path, EDITOR_PATH_MAX);
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
    ed_strcpy(g_editor.path, "untitled.txt", EDITOR_PATH_MAX);
  }
}

void text_editor_save(struct text_editor_app *app) {
  if (!app || !app->path[0]) return;
  struct vfs_metadata meta = {0, 0, 0x1FF};
  vfs_create(app->path, 0x1, &meta);
  struct file *f = vfs_open(app->path, 0x2);
  if (!f) return;
  for (int i = 0; i < app->line_count; i++) {
    vfs_write(f, app->lines[i], ed_strlen(app->lines[i]));
    vfs_write(f, "\n", 1);
  }
  vfs_close(f);
  app->modified = 0;
}

void text_editor_handle_key(struct text_editor_app *app, uint32_t keycode, char ch) {
  if (!app) return;
  (void)keycode;

  if (ch == '\b') {
    if (app->cursor_col > 0) {
      char *line = app->lines[app->cursor_line];
      int len = (int)ed_strlen(line);
      for (int i = app->cursor_col - 1; i < len; i++) line[i] = line[i + 1];
      app->cursor_col--;
      app->modified = 1;
    } else if (app->cursor_line > 0) {
      /* Merge with previous line */
      int prev_len = (int)ed_strlen(app->lines[app->cursor_line - 1]);
      char *prev = app->lines[app->cursor_line - 1];
      char *cur = app->lines[app->cursor_line];
      int cur_len = (int)ed_strlen(cur);
      if (prev_len + cur_len < EDITOR_LINE_MAX - 1) {
        for (int i = 0; i < cur_len; i++) prev[prev_len + i] = cur[i];
        prev[prev_len + cur_len] = '\0';
      }
      for (int i = app->cursor_line; i < app->line_count - 1; i++)
        ed_strcpy(app->lines[i], app->lines[i + 1], EDITOR_LINE_MAX);
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
        ed_strcpy(app->lines[i], app->lines[i - 1], EDITOR_LINE_MAX);
      app->line_count++;
      char *cur = app->lines[app->cursor_line];
      int col = app->cursor_col;
      ed_strcpy(app->lines[app->cursor_line + 1], cur + col, EDITOR_LINE_MAX);
      cur[col] = '\0';
      app->cursor_line++;
      app->cursor_col = 0;
      app->modified = 1;
    }
    return;
  }

  if (ch >= 32 && ch < 127) {
    char *line = app->lines[app->cursor_line];
    int len = (int)ed_strlen(line);
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
  if (!f) return;

  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = 0x1E1E2E;
  }

  /* Title bar info */
  font_draw_string(s, f, 4, 2, app->path, 0x89B4FA);
  if (app->modified) font_draw_string(s, f, (int32_t)(s->width - 80), 2, "[modified]", 0xF38BA8);

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
    font_draw_string(s, f, 4, y, lnum, 0x6C7086);

    /* Line content */
    font_draw_string(s, f, 40, y, app->lines[i], 0xCDD6F4);

    /* Cursor */
    if (i == app->cursor_line) {
      int32_t cx = 40 + app->cursor_col * (int32_t)f->glyph_width;
      for (uint32_t cy = 0; cy < gh; cy++) {
        int32_t py = y + (int32_t)cy;
        if (py >= 0 && (uint32_t)py < s->height && cx >= 0 && (uint32_t)cx < s->width) {
          uint32_t *pline = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)py * s->pitch);
          pline[cx] = 0xF5C2E7;
        }
      }
    }
    y += (int32_t)gh;
  }
}
