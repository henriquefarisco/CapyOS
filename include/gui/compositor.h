#ifndef GUI_COMPOSITOR_H
#define GUI_COMPOSITOR_H

#include <stdint.h>
#include <stddef.h>

#define COMPOSITOR_MAX_WINDOWS 32
#define COMPOSITOR_MAX_LAYERS  8

/* Upper bound for a single surface dimension. Generous headroom above
 * any real display (16K-wide panels are ~15360 px) yet far below the
 * point where width*height*4 could wrap size_t. alloc_surface() rejects
 * dimensions beyond this fail-closed, so a caller passing absurd
 * (e.g. attacker-influenced) window dimensions cannot drive an
 * integer-overflow -> undersized allocation -> out-of-bounds surface. */
#define COMPOSITOR_MAX_SURFACE_DIM 32768u

struct gui_rect {
  int32_t x, y;
  uint32_t width, height;
};

struct gui_surface {
  uint32_t *pixels;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
};

struct gui_theme_palette {
  uint32_t wallpaper;
  uint32_t window_bg;
  uint32_t window_border;
  uint32_t title_active;
  uint32_t title_inactive;
  uint32_t text;
  uint32_t text_muted;
  uint32_t accent;
  uint32_t accent_alt;
  uint32_t accent_text;
  uint32_t taskbar_bg;
  uint32_t taskbar_fg;
  uint32_t taskbar_highlight;
  uint32_t terminal_bg;
  uint32_t terminal_fg;
  uint8_t ui_scale;
};

/* Etapa F4 cursors (2026-05-03): tipos de cursor que o compositor
 * pode renderizar. Cada um tem uma bitmap mask em
 * `src/gui/core/compositor_render.c`. O cursor ativo eh selecionado
 * por desktop_run_frame baseado em hit-test contra a janela sob o
 * mouse + callback `on_cursor_hint` da janela. */
enum comp_cursor_kind {
  COMP_CURSOR_ARROW       = 0,  /* Default. */
  COMP_CURSOR_TEXT        = 1,  /* I-beam para text inputs. */
  COMP_CURSOR_RESIZE_H    = 2,  /* <-> para borda left/right. */
  COMP_CURSOR_RESIZE_V    = 3,  /* up-down para borda top/bottom. */
  COMP_CURSOR_RESIZE_DIAG = 4,  /* corner resize. */
  COMP_CURSOR_LOADING     = 5,  /* Hourglass enquanto janela "loading". */
  COMP_CURSOR_KIND_COUNT
};

struct gui_window;

/* Etapa F4 cursors (2026-05-03): callback de "cursor hint". O
 * desktop chama esta funcao quando o mouse entra na janela
 * (com coordenadas locais ao surface) para saber qual cursor
 * desenhar. Retorna COMP_CURSOR_ARROW se nao tem preferencia.
 * Util para apps com text inputs (URL bar do browser, file_manager
 * search, text_editor). */
typedef enum comp_cursor_kind (*comp_cursor_hint_fn)(struct gui_window *win,
                                                      int32_t lx, int32_t ly);

struct gui_window {
  uint32_t id;
  char title[64];
  struct gui_rect frame;
  struct gui_surface surface;
  uint32_t z_order;
  int visible;
  int focused;
  int decorated;
  int resizable;
  int movable;
  /* Etapa F4 minimize/maximize (2026-05-03): estados extras de
   * janela. `minimized` -> escondida (visible = 0) mas ainda no
   * taskbar; clique no item do taskbar restaura. `maximized` ->
   * janela ocupando a tela toda (menos taskbar e title bar).
   * `saved_frame` guarda o frame pre-maximize para restore.
   * Apps nao precisam mexer; usar
   * compositor_minimize_window/compositor_toggle_maximize_window. */
  int minimized;
  int maximized;
  struct gui_rect saved_frame;
  /* Etapa F4 cursors (2026-05-03): apps que esperam operacoes lentas
   * (download, fetch, parse, etc.) podem setar este flag para
   * mostrar o cursor "loading" sobre sua janela. Limpar ao terminar. */
  int loading;
  int capture_mouse;
  /* Etapa UX W7-ish (2026-05-03): raio dos cantos arredondados em px.
   * Default 0 = quadrado. compositor_create_window seta para
   * COMP_WINDOW_CORNER_RADIUS (8) automaticamente quando decorated.
   * Overlays (menu popup, context menu) podem opt-in setando este
   * campo manualmente apos create. */
  uint8_t  corner_radius;
  uint32_t bg_color;
  uint32_t border_color;
  void *user_data;
  void (*on_paint)(struct gui_window *win);
  void (*on_close)(struct gui_window *win);
  void (*on_resize)(struct gui_window *win, uint32_t w, uint32_t h);
  void (*on_focus)(struct gui_window *win);
  void (*on_blur)(struct gui_window *win);
  void (*on_key)(struct gui_window *win, uint32_t keycode, uint8_t mods);
  void (*on_key_up)(struct gui_window *win, uint32_t keycode, uint8_t mods);
  void (*on_mouse)(struct gui_window *win, int32_t x, int32_t y, uint8_t btns);
  void (*on_scroll)(struct gui_window *win, int32_t delta);
  void (*on_timer)(struct gui_window *win, uint32_t timer_id);
  /* Etapa UX W7-ish (2026-05-03): hover (mouse-move sem botao) e
   * context menu (botao direito). Ambos opcionais; quando NULL, o
   * desktop loop ignora. As coordenadas sao locais a janela
   * (x relativa a frame.x, y relativa a frame.y). */
  void (*on_hover)(struct gui_window *win, int32_t lx, int32_t ly);
  void (*on_context_menu)(struct gui_window *win, int32_t lx, int32_t ly);
  /* Etapa F4 cursors (2026-05-03): hint de cursor opcional. Quando
   * NULL, o desktop usa ARROW (ou outro inferido por hit-test em
   * borda). */
  comp_cursor_hint_fn on_cursor_hint;
  /* Etapa 7 / Slice 7.5 (alpha.305): 0 for every window created directly by
   * an in-kernel app (compositor_create_window, the only path that existed
   * before this field) -- these keep using the on_mouse/on_key/on_* callback
   * pointers above. Nonzero (the owning pid) for a window created via the
   * ring-3 graphical syscall ABI (kernel/syscall_gfx_init.c's
   * gfx_backend_win_create) on behalf of a process with NO kernel-side
   * callbacks to call into; the input dispatcher checks this field to route
   * mouse/keyboard for such a window through gui_event_push_* (the
   * SYS_WINDOW_POLL_EVENT queue) instead. Additive field at the tail; does
   * not change any existing window's behavior (defaults to 0). */
  uint32_t gfx_owner_pid;
};

struct compositor_stats {
  uint32_t window_count;
  uint32_t visible_count;
  uint64_t frames_rendered;
  uint64_t dirty_rects;
  uint64_t full_frames_presented;
  uint64_t partial_frames_presented;
  uint64_t dirty_rects_presented;
  /* Etapa 4 Fase D follow-up (2026-05-25): counts how many partial
   * frames actually needed to erase the cursor area before the
   * desktop loop redrew it. A low value means the cursor is rarely
   * in the damaged region; a non-zero value still means each erase
   * was justified by a real intersection between the cursor and a
   * dirty rect. Always 0 in full-present mode because the
   * full-frame copy already overwrote the cursor area. */
  uint64_t cursor_erases_partial;
};

void compositor_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
                     uint32_t pitch);
void compositor_shutdown(void);
struct gui_window *compositor_create_window(const char *title, int32_t x,
                                            int32_t y, uint32_t w, uint32_t h);
void compositor_destroy_window(uint32_t window_id);
void compositor_show_window(uint32_t window_id);
void compositor_hide_window(uint32_t window_id);
void compositor_focus_window(uint32_t window_id);
void compositor_move_window(uint32_t window_id, int32_t x, int32_t y);
void compositor_resize_window(uint32_t window_id, uint32_t w, uint32_t h);
void compositor_set_title(uint32_t window_id, const char *title);
void compositor_invalidate(uint32_t window_id);
void compositor_invalidate_rect(uint32_t window_id, struct gui_rect *rect);
void compositor_invalidate_desktop_rect(const struct gui_rect *rect);
/* Etapa UX W7-ish (2026-05-03): forca redraw global (incluindo o
 * wallpaper + desktop icons) sem precisar de window id. Usado pelo
 * desktop_icons quando muda selection / refresh de listagem. */
void compositor_invalidate_all(void);
void compositor_render(void);
int compositor_current_render_clip(struct gui_rect *out);
int compositor_needs_render(void);
int compositor_cursor_needs_render(int32_t x, int32_t y);
void compositor_render_cursor(int32_t x, int32_t y);
struct gui_window *compositor_window_at(int32_t x, int32_t y);
struct gui_window *compositor_focused_window(void);
struct gui_window *compositor_get_window(uint32_t window_id);
int compositor_window_exists(uint32_t window_id);
void compositor_stats_get(struct compositor_stats *out);
void compositor_screen_size(uint32_t *out_w, uint32_t *out_h);
void compositor_set_wallpaper(uint32_t color);
void compositor_apply_theme(const char *theme, uint32_t screen_w, uint32_t screen_h);
const struct gui_theme_palette *compositor_theme(void);
uint8_t compositor_ui_scale(void);
void compositor_set_desktop_callback(void (*callback)(struct gui_surface *));
int compositor_hit_close_button(struct gui_window *win, int32_t x, int32_t y);

/* Etapa F4 minimize/maximize (2026-05-03): hit-tests dos botoes
 * adicionais e setters de estado da janela. Os 3 botoes ficam
 * alinhados a direita do title bar nesta ordem (R->L):
 *   [Close] [Maximize/Restore] [Minimize]
 * Cada botao tem largura `title_h - 6` px com 4 px de gap. */
int compositor_hit_minimize_button(struct gui_window *win,
                                    int32_t x, int32_t y);
int compositor_hit_maximize_button(struct gui_window *win,
                                    int32_t x, int32_t y);

/* Esconde a janela e marca minimized=1. O taskbar continua
 * mostrando o item; clique nele restaura via show_window+focus. */
void compositor_minimize_window(uint32_t window_id);

/* Alterna entre maximize (full screen menos title+taskbar) e
 * restore (volta ao saved_frame). screen_h_avail eh a altura
 * disponivel (= screen_h - taskbar_h); o caller eh responsavel
 * por descontar o taskbar. */
void compositor_toggle_maximize_window(uint32_t window_id,
                                        uint32_t screen_w,
                                        uint32_t screen_h_avail);

/* Etapa F4 cursors (2026-05-03): seletor global de cursor. Mudar
 * o kind invalida o cache de cursor para forcar redraw. */
void compositor_set_cursor(enum comp_cursor_kind kind);
enum comp_cursor_kind compositor_cursor_kind(void);

#endif /* GUI_COMPOSITOR_H */
