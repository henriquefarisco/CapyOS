/*
 * src/gui/core/internal/compositor_internal.h
 *
 * 2026-05-02 (Etapa 2 audit, A1+layout-audit fix): private header
 * shared between the three compositor TUs created when
 * `compositor.c` was split for the layout-audit cap (limit 900
 * lines). Moves shared mutable state and helper signatures out of
 * static-file scope so each TU can stay below the cap without
 * duplicating implementation.
 *
 * TU layout:
 *   - `compositor.c`         : window lifecycle + frame buffer
 *                              ownership + miscellaneous queries.
 *   - `compositor_theme.c`   : theme palette (`compositor_apply_theme`,
 *                              `compositor_theme`, `compositor_ui_scale`).
 *   - `compositor_render.c`  : scene composition + present + cursor
 *                              + window decoration + outline.
 *
 * Symbols here are NOT part of the public compositor ABI; the
 * `internal/` directory keeps them out of the global include path
 * per the layout-audit `internal-header` rule.
 */
#ifndef GUI_CORE_INTERNAL_COMPOSITOR_INTERNAL_H
#define GUI_CORE_INTERNAL_COMPOSITOR_INTERNAL_H

#include "gui/compositor.h"
#include <stddef.h>
#include <stdint.h>

/* Cursor sprite metrics. Used by `compositor_render.c::draw_cursor_on_front`
 * and by `compositor.c::reset_window_slot` to size the cursor backup
 * region. */
#define COMP_CURSOR_WIDTH  16
#define COMP_CURSOR_HEIGHT 16
#define COMP_DIRTY_RECT_MAX 32u

/* Window corner radius (px). The title bar's TOP corners and the
 * body's BOTTOM corners are masked to this radius in
 * `compositor_render.c`. Decoration-less overlays skip the mask. */
#define COMP_WINDOW_CORNER_RADIUS 8u

/* === Shared mutable state ============================================== */
/* Owned by `compositor.c`; read/written by `compositor_theme.c` and
 * `compositor_render.c`. Kept in alphabetical-ish order for diff
 * stability. */
extern uint32_t *comp_fb;
extern uint32_t *comp_backbuffer;
extern uint32_t  comp_width;
extern uint32_t  comp_height;
extern uint32_t  comp_pitch;
extern uint32_t  comp_backbuffer_stride;
extern uint32_t  comp_wallpaper;
extern struct gui_window comp_windows[COMPOSITOR_MAX_WINDOWS];
extern struct compositor_stats comp_stats;
extern struct gui_theme_palette g_theme;
extern void (*comp_desktop_paint_cb)(struct gui_surface *);
extern int      comp_scene_dirty;
extern int      comp_full_presented;
extern int      comp_cursor_valid;
extern int32_t  comp_cursor_x;
extern int32_t  comp_cursor_y;
extern struct gui_rect comp_dirty_rects[COMP_DIRTY_RECT_MAX];
extern uint32_t comp_dirty_rect_count;
extern int      comp_full_redraw_pending;
/* Etapa F4 cursors (2026-05-03): kind do cursor atual; default
 * COMP_CURSOR_ARROW. Setado via compositor_set_cursor() pelo
 * desktop loop conforme hit-test sobre janelas. Lido por
 * draw_cursor_on_front em compositor_render.c para escolher a
 * bitmap apropriada. */
extern uint8_t  comp_cursor_kind_active;
extern uint8_t  comp_cursor_kind_rendered;

/* === Shared helpers =================================================== */
/* Implemented by the compositor core TUs; callers only use this header. */

void     comp_memset32(uint32_t *dst, uint32_t val, size_t count);
void     comp_memcpy(void *dst, const void *src, size_t len);
int      comp_streq(const char *a, const char *b);
int      comp_dirty_append_rect(const struct gui_rect *rect);
void     comp_dirty_mark_full_redraw(void);

uint32_t comp_window_title_height(void);
int      comp_window_rect_to_screen(struct gui_window *win,
                                    const struct gui_rect *rect,
                                    struct gui_rect *out);

/* Returns 1 if (col,row) lies inside the rounded-rect mask of
 * size (w, total_h) with corner radius `r`. See compositor.c for
 * full geometry rationale. */
int      comp_window_pixel_inside(uint32_t col, uint32_t row,
                                  uint32_t w, uint32_t total_h, uint32_t r);

/* Find a window by ID; NULL if no slot matches. */
struct gui_window *comp_find_window(uint32_t id);

/* Mark the scene dirty so the next compositor_render() repaints. */
void     comp_request_scene_redraw(void);

#endif /* GUI_CORE_INTERNAL_COMPOSITOR_INTERNAL_H */
