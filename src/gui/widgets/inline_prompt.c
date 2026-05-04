/* src/gui/widgets/inline_prompt.c
 *
 * Etapa UX W7-ish (2026-05-03): popup modal de single-line text input.
 * Implementacao do contrato em include/gui/inline_prompt.h.
 *
 * Estilo: window undecorada com corner_radius 6 px, fundo window_bg,
 * border accent. Layout vertical:
 *   [4 px padding]
 *   [titulo (font_default)]
 *   [campo: 1 px border + 18 px alto, texto + caret]
 *   [hint "Enter / Esc"]
 */
#include "gui/inline_prompt.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "lang/app_language.h"
#include <stddef.h>

static struct {
  struct gui_window *win;
  char  title[INLINE_PROMPT_TITLE_MAX];
  char  text[INLINE_PROMPT_TEXT_MAX];
  uint32_t cursor;
  inline_prompt_submit_fn on_submit;
  void *ctx;
} g_p = {0};

static uint32_t ip_strlen(const char *s) {
  uint32_t n = 0;
  while (s && s[n]) n++;
  return n;
}

static void ip_strcpy(char *d, const char *s, uint32_t max) {
  uint32_t i = 0;
  if (!d || max == 0u) return;
  if (s) while (i + 1u < max && s[i]) { d[i] = s[i]; i++; }
  d[i] = '\0';
}

static void ip_fill_rect(struct gui_surface *s, int32_t x, int32_t y,
                          uint32_t w, uint32_t h, uint32_t color) {
  if (!s) return;
  for (uint32_t r = 0; r < h; r++) {
    int32_t py = y + (int32_t)r;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels +
                                  (uint32_t)py * s->pitch);
    for (uint32_t c = 0; c < w; c++) {
      int32_t px = x + (int32_t)c;
      if (px >= 0 && (uint32_t)px < s->width) line[px] = color;
    }
  }
}

static void ip_paint(struct gui_window *win) {
  if (!win || win != g_p.win) return;
  const struct gui_theme_palette *theme = compositor_theme();
  const struct font *f = font_default();
  struct gui_surface *s = &win->surface;
  if (!f) return;

  /* Fundo. Compositor mascara cantos arredondados. */
  ip_fill_rect(s, 0, 0, s->width, s->height, theme->window_bg);
  /* Faixa accent no topo (4 px) — afirma destaque visual. */
  ip_fill_rect(s, 0, 0, s->width, 4, theme->accent);

  /* Title em accent_text. */
  font_draw_string(s, f, 8, 8, g_p.title, theme->text);

  /* Caixa do input: 1 px border accent_alt + interior accent_alt
   * lighter. Fica 8 .. width-8 horizontal, 26 .. 44 vertical. */
  int32_t bx = 8, by = 24;
  int32_t bw = (int32_t)s->width - 16;
  int32_t bh = 18;
  ip_fill_rect(s, bx, by, bw, bh, theme->terminal_bg);
  ip_fill_rect(s, bx, by, bw, 1, theme->accent_alt);
  ip_fill_rect(s, bx, by + bh - 1, bw, 1, theme->accent_alt);
  ip_fill_rect(s, bx, by, 1, bh, theme->accent_alt);
  ip_fill_rect(s, bx + bw - 1, by, 1, bh, theme->accent_alt);

  /* Texto digitado + caret. */
  font_draw_string(s, f, bx + 4, by + 4, g_p.text, theme->terminal_fg);
  /* Caret 1 px wide na coluna g_p.cursor. */
  uint32_t gw = 8u; /* glyph width default font */
  int32_t cx = bx + 4 + (int32_t)(g_p.cursor * gw);
  if (cx < bx + bw - 2)
    ip_fill_rect(s, cx, by + 3, 1, (uint32_t)bh - 6, theme->accent);

  /* Hint Enter / Esc na base.
   * Etapa F4 i18n (2026-05-03): localizado por sessao. */
  if (s->height > 50u) {
    font_draw_string(s, f, 8, 46,
                     APP_T("Enter: ok   Esc: cancelar",
                            "Enter: ok   Esc: cancel",
                            "Enter: ok   Esc: cancelar"),
                     theme->text_muted);
  }
}

int inline_prompt_show(const char *title, const char *default_text,
                       int32_t screen_x, int32_t screen_y,
                       inline_prompt_submit_fn on_submit, void *ctx) {
  inline_prompt_close();
  ip_strcpy(g_p.title, title ? title : "", INLINE_PROMPT_TITLE_MAX);
  ip_strcpy(g_p.text, default_text ? default_text : "",
            INLINE_PROMPT_TEXT_MAX);
  g_p.cursor = ip_strlen(g_p.text);
  g_p.on_submit = on_submit;
  g_p.ctx = ctx;
  if (screen_x < 0) screen_x = 0;
  if (screen_y < 0) screen_y = 0;
  g_p.win = compositor_create_window("Prompt", screen_x, screen_y,
                                      INLINE_PROMPT_WIDTH,
                                      INLINE_PROMPT_HEIGHT);
  if (!g_p.win) {
    g_p.on_submit = NULL;
    g_p.ctx = NULL;
    return -1;
  }
  g_p.win->decorated = 0;
  g_p.win->movable = 0;
  g_p.win->resizable = 0;
  g_p.win->corner_radius = 6;
  g_p.win->border_color = compositor_theme()->accent;
  g_p.win->bg_color = compositor_theme()->window_bg;
  g_p.win->z_order = COMPOSITOR_MAX_WINDOWS + 7;
  g_p.win->user_data = NULL;
  g_p.win->on_paint = ip_paint;
  compositor_show_window(g_p.win->id);
  compositor_focus_window(g_p.win->id);
  return 0;
}

void inline_prompt_close(void) {
  if (g_p.win) {
    compositor_destroy_window(g_p.win->id);
    g_p.win = NULL;
  }
  g_p.title[0] = '\0';
  g_p.text[0] = '\0';
  g_p.cursor = 0;
  g_p.on_submit = NULL;
  g_p.ctx = NULL;
}

int inline_prompt_is_open(void) {
  return g_p.win != NULL;
}

int inline_prompt_handle_key(uint32_t keycode, char ch) {
  if (!g_p.win) return 0;

  /* Esc -> cancela sem callback. */
  if (ch == 0x1B || keycode == 0x1B) {
    inline_prompt_close();
    return 1;
  }
  /* Enter -> submit. */
  if (ch == '\n' || ch == '\r') {
    inline_prompt_submit_fn cb = g_p.on_submit;
    void *cb_ctx = g_p.ctx;
    char snap[INLINE_PROMPT_TEXT_MAX];
    ip_strcpy(snap, g_p.text, INLINE_PROMPT_TEXT_MAX);
    inline_prompt_close();
    if (cb) cb(snap, cb_ctx);
    return 1;
  }
  /* Backspace. */
  if (ch == '\b' || keycode == '\b') {
    if (g_p.cursor > 0) {
      uint32_t len = ip_strlen(g_p.text);
      if (g_p.cursor <= len) {
        for (uint32_t i = g_p.cursor - 1u; i < len; i++) {
          g_p.text[i] = g_p.text[i + 1u];
        }
        g_p.cursor--;
      }
    }
    if (g_p.win) compositor_invalidate(g_p.win->id);
    return 1;
  }
  /* Printable ASCII -> insert at cursor. */
  if (ch >= 32 && ch < 127) {
    uint32_t len = ip_strlen(g_p.text);
    if (len + 1u < INLINE_PROMPT_TEXT_MAX) {
      for (uint32_t i = len; i > g_p.cursor; i--) {
        g_p.text[i] = g_p.text[i - 1u];
      }
      g_p.text[g_p.cursor] = ch;
      g_p.cursor++;
      g_p.text[len + 1u] = '\0';
    }
    if (g_p.win) compositor_invalidate(g_p.win->id);
    return 1;
  }
  /* Outras teclas (arrows etc.) absorvidas pelo prompt sem efeito. */
  return 1;
}

int inline_prompt_handle_click(int32_t screen_x, int32_t screen_y) {
  if (!g_p.win) return 0;
  int32_t px = g_p.win->frame.x;
  int32_t py = g_p.win->frame.y;
  uint32_t pw = g_p.win->frame.width;
  uint32_t ph = g_p.win->frame.height;
  if (screen_x < px || screen_x >= px + (int32_t)pw ||
      screen_y < py || screen_y >= py + (int32_t)ph) {
    inline_prompt_close();
    return 1;
  }
  /* Click dentro do popup: por enquanto mantem foco; futuramente
   * poderia mover o caret pra coluna do click. */
  return 1;
}
