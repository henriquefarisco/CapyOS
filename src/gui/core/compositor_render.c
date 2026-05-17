/*
 * src/gui/core/compositor_render.c
 *
 * Scene composition + frame presentation + cursor rendering for the
 * compositor. Split out of `compositor.c` on 2026-05-02 (Etapa 2
 * audit) to keep each TU below the 900-line layout-audit cap.
 *
 * Pipeline:
 *   1. `compositor_render` is the entry point called by the desktop
 *      runtime each tick.
 *   2. If the scene is dirty and a backbuffer exists,
 *      `compose_scene` rasterises wallpaper + desktop callback +
 *      every visible window in z-order into the backbuffer, then
 *      `present_full_frame_from_backbuffer` blits the backbuffer
 *      to the front framebuffer in one shot. No backbuffer means
 *      direct front-buffer composition (initial fallback).
 *   3. `compositor_render_cursor` paints the cursor sprite directly
 *      on the front buffer, optionally restoring the previous
 *      cursor area from the backbuffer to erase the old position.
 *
 * Window decoration (title bar + close button + outline + rounded
 * corners) lives here because it is purely a render-time concern;
 * window creation/lifecycle stays in `compositor.c`.
 */

#include "internal/compositor_internal.h"
#include "gui/font.h"

/* Pixel-perfect 1 px outline around a window (title bar + body)
 * that follows the rounded mask. A pixel is on the perimeter if
 * it is inside the shape and at least one orthogonal neighbour
 * is outside. The check matches `comp_window_pixel_inside`'s
 * mask, so the outline traces the rose-petal curve of the corners.
 *
 * Called AFTER the title bar paint and body blit so the border
 * sits ON TOP of both, which means windows have a visible edge
 * even when their body bg color is close to the wallpaper or a
 * sibling window's body. Fixes the "windows blend into the
 * desktop" complaint reported on the love+capyos themes. */
static void render_window_outline(struct gui_window *win,
                                  uint32_t *buf, uint32_t buf_stride) {
  uint32_t total_w = win->frame.width;
  uint32_t title_h = win->decorated ? comp_window_title_height() : 0u;
  uint32_t total_h = title_h + win->frame.height;
  if (total_w == 0u || total_h == 0u) return;

  int32_t origin_x = win->frame.x;
  int32_t origin_y = win->frame.y - (int32_t)title_h;

  /* Etapa UX W7-ish (2026-05-03): use o corner_radius por-janela.
   * Decorated windows tem default 8 (set em compositor_create_window);
   * overlays podem opt-in setando win->corner_radius. */
  uint32_t corner_r = win->corner_radius;
  if (corner_r != 0u &&
      (total_w < corner_r * 2u || total_h < corner_r * 2u)) {
    corner_r = 0u;
  }

  /* Sem corner_radius e sem decoration nao tem o que delinhar. */
  if (corner_r == 0u && !win->decorated) return;

  uint32_t color = g_theme.window_border;
  /* Border custom da janela (e.g. menu popup pode usar accent). */
  if (win->border_color != 0u) color = win->border_color;
  /* Focus accent: focused windows get the active title color as
   * outline so the user sees which window will receive input. */
  if (win->focused) color = g_theme.title_active;

  for (uint32_t row = 0; row < total_h; row++) {
    int32_t py = origin_y + (int32_t)row;
    if (py < 0 || (uint32_t)py >= comp_height) continue;
    for (uint32_t col = 0; col < total_w; col++) {
      int rounded = (corner_r != 0u);
      int inside = rounded
          ? comp_window_pixel_inside(col, row, total_w, total_h, corner_r)
          : 1;
      if (!inside) continue;
      /* Perimeter test: any orthogonal neighbor outside the shape
       * (or outside the rect bounds) makes this pixel a border
       * pixel. The four-direction check produces a clean 1 px
       * line, including on the curved corners. */
      int n_left   = (col == 0u) ||
          (rounded && !comp_window_pixel_inside(col - 1u, row, total_w, total_h, corner_r));
      int n_right  = (col + 1u >= total_w) ||
          (rounded && !comp_window_pixel_inside(col + 1u, row, total_w, total_h, corner_r));
      int n_top    = (row == 0u) ||
          (rounded && !comp_window_pixel_inside(col, row - 1u, total_w, total_h, corner_r));
      int n_bot    = (row + 1u >= total_h) ||
          (rounded && !comp_window_pixel_inside(col, row + 1u, total_w, total_h, corner_r));
      if (!(n_left || n_right || n_top || n_bot)) continue;

      int32_t px = origin_x + (int32_t)col;
      if (px < 0 || (uint32_t)px >= comp_width) continue;
      buf[py * buf_stride + px] = color;
    }
  }
}

static uint32_t render_mix_color(uint32_t color, uint32_t target,
                                 uint8_t amount) {
  uint32_t r = (color >> 16) & 0xFFu;
  uint32_t g = (color >> 8) & 0xFFu;
  uint32_t b = color & 0xFFu;
  uint32_t tr = (target >> 16) & 0xFFu;
  uint32_t tg = (target >> 8) & 0xFFu;
  uint32_t tb = target & 0xFFu;
  r = r + ((tr > r) ? ((tr - r) * amount) / 255u
                    : -((r - tr) * amount) / 255u);
  g = g + ((tg > g) ? ((tg - g) * amount) / 255u
                    : -((g - tg) * amount) / 255u);
  b = b + ((tb > b) ? ((tb - b) * amount) / 255u
                    : -((b - tb) * amount) / 255u);
  return (r << 16) | (g << 8) | b;
}

static uint32_t render_lerp_color(uint32_t a, uint32_t b, uint8_t amount) {
  uint32_t ar = (a >> 16) & 0xFFu;
  uint32_t ag = (a >> 8) & 0xFFu;
  uint32_t ab = a & 0xFFu;
  uint32_t br = (b >> 16) & 0xFFu;
  uint32_t bg = (b >> 8) & 0xFFu;
  uint32_t bb = b & 0xFFu;
  uint32_t r = ar + ((br > ar) ? ((br - ar) * amount) / 255u
                               : -((ar - br) * amount) / 255u);
  uint32_t g = ag + ((bg > ag) ? ((bg - ag) * amount) / 255u
                               : -((ag - bg) * amount) / 255u);
  uint32_t bl = ab + ((bb > ab) ? ((bb - ab) * amount) / 255u
                                : -((ab - bb) * amount) / 255u);
  return (r << 16) | (g << 8) | bl;
}

static void render_fill_rect_clip(uint32_t *buf, uint32_t buf_stride,
                                  int32_t x, int32_t y, uint32_t w,
                                  uint32_t h, uint32_t color) {
  for (uint32_t row = 0; row < h; row++) {
    int32_t py = y + (int32_t)row;
    if (py < 0 || (uint32_t)py >= comp_height) continue;
    for (uint32_t col = 0; col < w; col++) {
      int32_t px = x + (int32_t)col;
      if (px < 0 || (uint32_t)px >= comp_width) continue;
      buf[py * buf_stride + px] = color;
    }
  }
}

static void render_fit_title(const struct font *f, const char *src,
                             uint32_t max_width, char *out,
                             uint32_t out_len) {
  uint32_t len = 0;
  uint32_t max_chars = 0;
  if (!out || out_len == 0u) return;
  out[0] = '\0';
  if (!f || !src || f->glyph_width == 0u || max_width == 0u) return;
  max_chars = max_width / f->glyph_width;
  if (max_chars == 0u) return;
  while (src[len] && len + 1u < out_len) len++;
  if (len <= max_chars) {
    for (uint32_t i = 0; i < len; i++) out[i] = src[i];
    out[len] = '\0';
    return;
  }
  if (max_chars <= 3u) {
    uint32_t n = max_chars;
    if (n >= out_len) n = out_len - 1u;
    for (uint32_t i = 0; i < n; i++) out[i] = '.';
    out[n] = '\0';
    return;
  }
  {
    uint32_t copy = max_chars - 3u;
    if (copy > out_len - 4u) copy = out_len - 4u;
    for (uint32_t i = 0; i < copy; i++) out[i] = src[i];
    out[copy] = '.';
    out[copy + 1u] = '.';
    out[copy + 2u] = '.';
    out[copy + 3u] = '\0';
  }
}

enum window_button_kind {
  WINDOW_BUTTON_CLOSE = 0,
  WINDOW_BUTTON_MAXIMIZE = 1,
  WINDOW_BUTTON_RESTORE = 2,
  WINDOW_BUTTON_MINIMIZE = 3
};

static void render_window_button_stroke(uint32_t *buf, uint32_t buf_stride,
                                        int32_t x, int32_t y, int32_t w,
                                        int32_t h, uint32_t color) {
  if (w <= 0 || h <= 0) return;
  render_fill_rect_clip(buf, buf_stride, x, y, (uint32_t)w, 1u, color);
  render_fill_rect_clip(buf, buf_stride, x, y + h - 1, (uint32_t)w, 1u, color);
  render_fill_rect_clip(buf, buf_stride, x, y, 1u, (uint32_t)h, color);
  render_fill_rect_clip(buf, buf_stride, x + w - 1, y, 1u, (uint32_t)h, color);
}

static void render_window_button_icon(uint32_t *buf, uint32_t buf_stride,
                                      int32_t x, int32_t y, int32_t size,
                                      uint32_t color,
                                      enum window_button_kind kind) {
  int32_t cx = x + size / 2;
  int32_t cy = y + size / 2;
  int32_t r = size >= 18 ? 5 : 4;
  if (kind == WINDOW_BUTTON_CLOSE) {
    for (int32_t i = -r; i <= r; i++) {
      render_fill_rect_clip(buf, buf_stride, cx + i, cy + i, 1u, 1u, color);
      render_fill_rect_clip(buf, buf_stride, cx + i, cy - i, 1u, 1u, color);
    }
    return;
  }
  if (kind == WINDOW_BUTTON_MINIMIZE) {
    render_fill_rect_clip(buf, buf_stride, cx - r, cy + r - 1,
                          (uint32_t)(r * 2 + 1), 2u, color);
    return;
  }
  if (kind == WINDOW_BUTTON_RESTORE) {
    render_window_button_stroke(buf, buf_stride, cx - 3, cy - 5, 8, 8, color);
    render_window_button_stroke(buf, buf_stride, cx - 6, cy - 2, 8, 8, color);
    return;
  }
  render_window_button_stroke(buf, buf_stride, cx - r, cy - r,
                              r * 2 + 1, r * 2 + 1, color);
}

static void render_window_button(uint32_t *buf, uint32_t buf_stride,
                                 int32_t x, int32_t y, int32_t size,
                                 uint32_t bg, uint32_t fg,
                                 enum window_button_kind kind) {
  if (size <= 0) return;
  render_fill_rect_clip(buf, buf_stride, x, y, (uint32_t)size,
                        (uint32_t)size, render_mix_color(bg, 0x00000000, 80u));
  if (size > 2) {
    render_fill_rect_clip(buf, buf_stride, x + 1, y + 1,
                          (uint32_t)(size - 2), (uint32_t)(size - 2), bg);
  }
  if (size > 4) {
    render_fill_rect_clip(buf, buf_stride, x + 2, y + 2,
                          (uint32_t)(size - 4), 1u,
                          render_mix_color(bg, 0x00FFFFFF, 70u));
  }
  render_window_button_icon(buf, buf_stride, x, y, size, fg, kind);
}

static void render_window_decoration(struct gui_window *win, uint32_t *buf,
                                     uint32_t buf_stride) {
  if (!win->decorated) return;
  int32_t x = win->frame.x;
  int32_t y = win->frame.y;
  uint32_t w = win->frame.width;
  uint32_t title_base = win->focused ? g_theme.title_active
                                     : g_theme.title_inactive;
  uint32_t title_top = render_mix_color(title_base, 0x00FFFFFF,
                                        win->focused ? 38u : 18u);
  uint32_t title_bottom = render_mix_color(title_base, 0x00000000,
                                           win->focused ? 32u : 44u);
  uint32_t title_h = comp_window_title_height();
  uint32_t total_h = title_h + win->frame.height;
  uint32_t corner_r = win->corner_radius;
  if (corner_r != 0u && (w < corner_r * 2u || total_h < corner_r * 2u)) {
    corner_r = 0u;
  }

  for (uint32_t row = 0; row < title_h; row++) {
    uint8_t amount = (title_h > 1u)
        ? (uint8_t)((row * 255u) / (title_h - 1u))
        : 0u;
    uint32_t row_color = render_lerp_color(title_top, title_bottom, amount);
    int32_t py = y - (int32_t)title_h + (int32_t)row;
    if (py < 0 || (uint32_t)py >= comp_height) continue;
    for (uint32_t col = 0; col < w; col++) {
      int32_t px = x + (int32_t)col;
      if (px < 0 || (uint32_t)px >= comp_width) continue;
      if (corner_r != 0u &&
          !comp_window_pixel_inside(col, row, w, total_h, corner_r)) {
        continue;
      }
      buf[py * buf_stride + px] = row_color;
    }
  }

  if (title_h > 2u) {
    const uint32_t rows[2] = { 0u, title_h - 1u };
    const uint32_t colors[2] = {
      render_mix_color(title_base, 0x00FFFFFF, win->focused ? 82u : 42u),
      render_mix_color(title_base, 0x00000000, win->focused ? 85u : 120u)
    };
    for (uint32_t edge = 0; edge < 2u; edge++) {
      uint32_t row = rows[edge];
      int32_t py = y - (int32_t)title_h + (int32_t)row;
      if (py < 0 || (uint32_t)py >= comp_height) continue;
      for (uint32_t col = 0; col < w; col++) {
        int32_t px = x + (int32_t)col;
        if (px < 0 || (uint32_t)px >= comp_width) continue;
        if (corner_r != 0u &&
            !comp_window_pixel_inside(col, row, w, total_h, corner_r)) {
          continue;
        }
        buf[py * buf_stride + px] = colors[edge];
      }
    }
  }

  {
    const struct font *f = font_default();
    if (f) {
      struct gui_surface title_surf = { buf, comp_width, comp_height,
                                        buf_stride * 4 };
      int32_t btn_size = (int32_t)title_h - 6;
      int32_t btn_y = y - (int32_t)title_h + 3;
      uint32_t control_w = (btn_size > 0) ? (uint32_t)(3 * btn_size + 16) : 0u;
      uint32_t text_max = (w > control_w + 12u) ? (w - control_w - 12u) : 0u;
      char title_fit[64];
      render_fit_title(f, win->title, text_max, title_fit, sizeof(title_fit));
      if (title_fit[0]) {
        font_draw_string(&title_surf, f, x + 6,
                         y - (int32_t)title_h + 4,
                         title_fit,
                         win->focused ? g_theme.accent_text
                                      : g_theme.text_muted);
      }

      /* Etapa F4 minimize/maximize (2026-05-03): 3 botoes na direita
       * do title bar (R->L): Close, Maximize/Restore, Minimize.
       * Cada botao btn_size x btn_size, separados por 4 px.
       * Labels:
       *   Close     -> "X"
       *   Maximize  -> "[]" (ou "][" quando ja maximizado = restore)
       *   Minimize  -> "_" */
      const struct {
        int32_t offset;
        uint32_t bg;
        enum window_button_kind kind;
      } btns[3] = {
        { 1, win->focused ? 0x00B91C1C
                          : render_mix_color(g_theme.title_inactive,
                                             0x00000000, 44u),
          WINDOW_BUTTON_CLOSE },
        { 2, win->focused ? g_theme.accent_alt
                          : render_mix_color(g_theme.title_inactive,
                                             g_theme.window_bg, 80u),
          win->maximized ? WINDOW_BUTTON_RESTORE : WINDOW_BUTTON_MAXIMIZE },
        { 3, win->focused ? g_theme.accent_alt
                          : render_mix_color(g_theme.title_inactive,
                                             g_theme.window_bg, 80u),
          WINDOW_BUTTON_MINIMIZE }
      };
      for (int b = 0; b < 3; b++) {
        int32_t btn_x = x + (int32_t)w
                         - btns[b].offset * btn_size
                         - btns[b].offset * 4;
        render_window_button(buf, buf_stride, btn_x, btn_y, btn_size,
                             btns[b].bg,
                             win->focused ? g_theme.accent_text
                                          : g_theme.text_muted,
                             btns[b].kind);
      }
    }
  }
}

static void compose_scene(uint32_t *buf, uint32_t buf_stride) {
  uint32_t max_z = 0;

  if (!buf || buf_stride == 0) return;

  for (uint32_t y = 0; y < comp_height; y++) {
    comp_memset32(buf + y * buf_stride, comp_wallpaper, comp_width);
  }

  if (comp_desktop_paint_cb) {
    struct gui_surface desktop = { buf, comp_width, comp_height, buf_stride * 4 };
    comp_desktop_paint_cb(&desktop);
  }

  for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
    if (comp_windows[i].id && comp_windows[i].visible &&
        comp_windows[i].z_order > max_z) {
      max_z = comp_windows[i].z_order;
    }
  }

  for (uint32_t z = 0; z <= max_z; z++) {
    for (int i = 0; i < COMPOSITOR_MAX_WINDOWS; i++) {
      struct gui_window *win = &comp_windows[i];
      if (!win->id || !win->visible || win->z_order != z) continue;
      if (!win->surface.pixels) continue;

      if (win->on_paint) win->on_paint(win);
      render_window_decoration(win, buf, buf_stride);

      {
        int32_t wx = win->frame.x;
        int32_t wy = win->frame.y;
        uint32_t sw = win->surface.width;
        /* Etapa UX W7-ish (2026-05-03): use o per-window
         * corner_radius. Decorated tem 8 (default); overlays podem
         * setar para arredondar (e.g., menu popup com 6 px). */
        uint32_t title_h = win->decorated ? comp_window_title_height() : 0u;
        uint32_t total_h = title_h + win->frame.height;
        uint32_t corner_r = win->corner_radius;
        if (corner_r != 0u && (win->frame.width < corner_r * 2u ||
                                total_h < corner_r * 2u)) {
          corner_r = 0u;
        }
        /* Para overlays (sem title bar) com corner_radius, o body
         * comeca em row=0 do mask total. Para decorated, body comeca
         * abaixo do title bar. */
        for (uint32_t row = 0; row < win->frame.height; row++) {
          int32_t py = wy + (int32_t)row;
          int32_t px_start = 0;
          int32_t px_end = 0;
          uint32_t col_start = 0;
          uint32_t copy_len = 0;
          if (py < 0 || (uint32_t)py >= comp_height) continue;
          px_start = wx < 0 ? 0 : wx;
          px_end = wx + (int32_t)win->frame.width;
          if (px_end > (int32_t)comp_width) px_end = (int32_t)comp_width;
          if (px_start >= px_end) continue;
          col_start = (uint32_t)(px_start - wx);
          copy_len = (uint32_t)(px_end - px_start);
          if (corner_r == 0u) {
            comp_memcpy(&buf[py * buf_stride + px_start],
                        &win->surface.pixels[row * sw + col_start],
                        copy_len * 4);
          } else {
            uint32_t win_row = row + title_h;
            /* Mascara nas bordas: o row pode estar dentro de qualquer
             * dos 4 cantos. Para overlay (title_h=0) o top tambem
             * arredonda. Otimizacao: se win_row e win_col estao longe
             * dos cantos, comp_window_pixel_inside e trivialmente 1. */
            int row_in_top    = (win_row < corner_r);
            int row_in_bot    = (win_row + corner_r >= total_h);
            if (!row_in_top && !row_in_bot) {
              comp_memcpy(&buf[py * buf_stride + px_start],
                          &win->surface.pixels[row * sw + col_start],
                          copy_len * 4);
            } else {
              for (uint32_t c = 0; c < copy_len; c++) {
                uint32_t win_col = col_start + c;
                if (!comp_window_pixel_inside(win_col, win_row,
                                              win->frame.width, total_h,
                                              corner_r)) {
                  continue;
                }
                buf[py * buf_stride + px_start + (int32_t)c] =
                    win->surface.pixels[row * sw + col_start + c];
              }
            }
          }
        }
      }

      /* Outline draws for decorated windows or any rounded overlay. */
      if (win->decorated || win->corner_radius != 0u) {
        render_window_outline(win, buf, buf_stride);
      }
    }
  }
}

static void present_full_frame_from_backbuffer(void) {
  uint32_t front_stride = comp_pitch / 4;
  if (!comp_fb || !comp_backbuffer || front_stride == 0) return;
  for (uint32_t y = 0; y < comp_height; y++) {
    comp_memcpy(&comp_fb[y * front_stride],
                &comp_backbuffer[y * comp_backbuffer_stride],
                comp_width * sizeof(uint32_t));
  }
}

static void copy_backbuffer_rect_to_front(int32_t x, int32_t y,
                                          uint32_t w, uint32_t h) {
  int32_t x0 = x;
  int32_t y0 = y;
  int32_t x1 = x + (int32_t)w;
  int32_t y1 = y + (int32_t)h;
  uint32_t front_stride = comp_pitch / 4;

  if (!comp_fb || !comp_backbuffer || front_stride == 0
      || comp_backbuffer_stride == 0) {
    return;
  }

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > (int32_t)comp_width) x1 = (int32_t)comp_width;
  if (y1 > (int32_t)comp_height) y1 = (int32_t)comp_height;
  if (x0 >= x1 || y0 >= y1) return;

  for (int32_t py = y0; py < y1; py++) {
    comp_memcpy(&comp_fb[(uint32_t)py * front_stride + (uint32_t)x0],
                &comp_backbuffer[(uint32_t)py * comp_backbuffer_stride + (uint32_t)x0],
                (size_t)(x1 - x0) * sizeof(uint32_t));
  }
}

/* Etapa F4 cursors (2026-05-03): tabela de bitmasks 16x16 por kind.
 * Cada cursor tem contorno e preenchimento separados, inspirado no
 * tema X.Org Whiteglass, para ficar legivel sobre fundos claros e escuros.
 *
 * Layout dos bitmaps (1 = pixel; bit mais significativo no col 0):
 *   ARROW  : seta padrao
 *   TEXT   : I-beam (linhas verticais + topo/base)
 *   RES H  : <-> setas horizontais
 *   RES V  : ^v setas verticais
 *   RES_DG : diagonal (top-left <-> bottom-right)
 *   LOAD   : ampulheta minimalista */
struct cursor_mask {
  uint16_t outline[COMP_CURSOR_HEIGHT];
  uint16_t fill[COMP_CURSOR_HEIGHT];
};

static const struct cursor_mask k_cursor_masks[COMP_CURSOR_KIND_COUNT] = {
  /* ARROW */ {
    {
      0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
      0xFFC0, 0xFEE0, 0xF860, 0xD830, 0x8C30, 0x0630, 0x0630, 0x0000
    },
    {
      0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7C00,
      0x7800, 0x6800, 0x0800, 0x0C00, 0x0400, 0x0000, 0x0000, 0x0000
    }
  },
  /* TEXT (I-beam) */ {
    {
      0x3FFC, 0x0660, 0x0660, 0x0660, 0x0660, 0x0660, 0x0660, 0x0660,
      0x0660, 0x0660, 0x0660, 0x0660, 0x0660, 0x0660, 0x3FFC, 0x0000
    },
    {
      0x1FF8, 0x0240, 0x0240, 0x0240, 0x0240, 0x0240, 0x0240, 0x0240,
      0x0240, 0x0240, 0x0240, 0x0240, 0x0240, 0x0240, 0x1FF8, 0x0000
    }
  },
  /* RESIZE_H (<->) */ {
    {
      0x0000, 0x0000, 0x0000, 0x0810, 0x1818, 0x381C, 0x7FFE, 0xFFFF,
      0x7FFE, 0x381C, 0x1818, 0x0810, 0x0000, 0x0000, 0x0000, 0x0000
    },
    {
      0x0000, 0x0000, 0x0000, 0x0000, 0x0810, 0x1818, 0x3FFC, 0x7FFE,
      0x3FFC, 0x1818, 0x0810, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    }
  },
  /* RESIZE_V (^v) */ {
    {
      0x0180, 0x03C0, 0x07E0, 0x0FF0, 0x0180, 0x0180, 0x0180, 0x0180,
      0x0180, 0x0180, 0x0180, 0x0180, 0x0FF0, 0x07E0, 0x03C0, 0x0180
    },
    {
      0x0000, 0x0180, 0x03C0, 0x07E0, 0x0000, 0x0180, 0x0180, 0x0180,
      0x0180, 0x0180, 0x0180, 0x0000, 0x07E0, 0x03C0, 0x0180, 0x0000
    }
  },
  /* RESIZE_DIAG (\) */ {
    {
      0xE000, 0xF000, 0xF800, 0x3C00, 0x1E00, 0x0F00, 0x0780, 0x03C0,
      0x01E0, 0x00F0, 0x0078, 0x003C, 0x001F, 0x000F, 0x0007, 0x0000
    },
    {
      0x4000, 0x6000, 0x7000, 0x1800, 0x0C00, 0x0600, 0x0300, 0x0180,
      0x00C0, 0x0060, 0x0030, 0x0018, 0x000E, 0x0006, 0x0002, 0x0000
    }
  },
  /* LOADING (ampulheta) */ {
    {
      0x7FFE, 0x7FFE, 0x300C, 0x1818, 0x0C30, 0x0660, 0x03C0, 0x0180,
      0x0180, 0x03C0, 0x0660, 0x0C30, 0x1818, 0x300C, 0x7FFE, 0x7FFE
    },
    {
      0x3FFC, 0x0000, 0x1008, 0x0810, 0x0420, 0x0240, 0x0180, 0x0000,
      0x0000, 0x0180, 0x0240, 0x0420, 0x0810, 0x1008, 0x0000, 0x3FFC
    }
  }
};

static void draw_cursor_on_front(int32_t x, int32_t y) {
  if (!comp_fb) return;
  uint8_t kind = comp_cursor_kind_active;
  if (kind >= (uint8_t)COMP_CURSOR_KIND_COUNT) {
    kind = (uint8_t)COMP_CURSOR_ARROW;
  }
  const struct cursor_mask *mask = &k_cursor_masks[kind];

  for (int row = 0; row < COMP_CURSOR_HEIGHT; row++) {
    int32_t py = y + row;
    if (py < 0 || (uint32_t)py >= comp_height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)comp_fb + py * comp_pitch);
    for (int col = 0; col < COMP_CURSOR_WIDTH; col++) {
      int32_t px = x + col;
      if (px < 0 || (uint32_t)px >= comp_width) continue;
      uint16_t bit = (uint16_t)(0x8000u >> col);
      if (mask->outline[row] & bit) line[px] = 0x000000u;
      if (mask->fill[row] & bit) line[px] = 0x00FFFFFFu;
    }
  }
}

void compositor_render(void) {
  uint32_t front_stride = comp_pitch / 4;
  if (!comp_fb || front_stride == 0) return;

  comp_full_presented = 0;
  if (!comp_scene_dirty) return;

  if (comp_backbuffer) {
    compose_scene(comp_backbuffer, comp_backbuffer_stride);
    present_full_frame_from_backbuffer();
  } else {
    compose_scene(comp_fb, front_stride);
  }

  comp_scene_dirty = 0;
  comp_full_presented = 1;
  comp_stats.frames_rendered++;
}

void compositor_render_cursor(int32_t x, int32_t y) {
  if (!comp_fb) return;

  if (!comp_full_presented && comp_cursor_valid &&
      comp_cursor_x == x && comp_cursor_y == y) {
    return;
  }

  if (comp_backbuffer && !comp_full_presented && comp_cursor_valid) {
    copy_backbuffer_rect_to_front(comp_cursor_x, comp_cursor_y,
                                  COMP_CURSOR_WIDTH, COMP_CURSOR_HEIGHT);
  }

  draw_cursor_on_front(x, y);
  comp_cursor_x = x;
  comp_cursor_y = y;
  comp_cursor_valid = 1;
  comp_cursor_kind_rendered = comp_cursor_kind_active;
  comp_full_presented = 0;
}
