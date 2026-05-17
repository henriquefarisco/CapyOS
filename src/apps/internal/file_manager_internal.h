/*
 * src/apps/internal/file_manager_internal.h
 *
 * Internal helpers shared inside the `file_manager` translation-unit
 * group:
 *
 *   - file_manager.c       : lifecycle, navigation, event dispatch
 *   - file_manager_view.c  : drawing primitives + file_manager_paint
 *   - file_manager_dnd.c   : drag-and-drop handlers + context menu
 *
 * Not part of the public API: external callers must keep using
 * `include/apps/file_manager.h`.
 *
 * The symbols below used to be `static` inside the pre-split monolith.
 * Exposing them as extern (with `fm_` and `file_manager_` prefixes that
 * already existed in the monolith) lets the three sister translation
 * units share state without duplicating logic, and is the only
 * behavioural change in the 2026-05-15 refactor.
 */
#ifndef APPS_INTERNAL_FILE_MANAGER_INTERNAL_H
#define APPS_INTERNAL_FILE_MANAGER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "apps/file_manager.h"
#include "gui/compositor.h"

/* Toolbar height in pixels — referenced from view + dnd. */
#define FM_PATHBAR_H 24
#define FM_TOOLBAR_H 58
#define FM_BUTTON_Y 30
#define FM_BUTTON_H 22

/* ── globals defined in file_manager.c ───────────────────────────────── */

extern struct file_manager_app g_fm;
extern int g_fm_open;
extern char g_fm_delete_path[FM_PATH_MAX];
extern uint16_t g_fm_delete_mode;

/* ── status helpers (file_manager.c) ─────────────────────────────────── */

void fm_set_status(struct file_manager_app *app, const char *text,
                   uint32_t color);
void fm_set_vfs_status(struct file_manager_app *app, const char *prefix,
                       int rc);
void fm_set_ok_status(struct file_manager_app *app, const char *text);

/* ── view-layer helpers (file_manager_view.c) ────────────────────────── */

struct font;
struct gui_surface;

int32_t fm_row_height(const struct font *f);
void fm_fit_text(const struct font *f, const char *src, uint32_t max_width,
                 char *out, size_t out_len);
void fm_draw_fit(struct gui_surface *s, const struct font *f, int32_t x,
                 int32_t y, uint32_t max_width, const char *text,
                 uint32_t color);
void fm_fill_rect(struct gui_surface *s, int32_t x, int32_t y, uint32_t w,
                  uint32_t h, uint32_t color);
void fm_toolbar_layout(uint32_t width, int32_t *out_x, int32_t *out_w,
                       int32_t *out_gap);
void fm_paint_button(struct gui_surface *s, const struct font *f, int32_t x,
                     int32_t y, int32_t w, const char *label, uint32_t bg,
                     uint32_t fg, uint32_t border);
int fm_row_at(struct file_manager_app *app, int32_t x, int32_t y);

/* ── filesystem operation helpers (file_manager.c) ───────────────────── */

void fm_join_path(const char *dir, const char *name, char *out,
                  size_t out_len);
const char *fm_basename(const char *path);
int fm_path_equal(const char *a, const char *b);
int fm_path_inside_or_same(const char *dir, const char *path);
int fm_path_exists(const char *path);
int fm_unique_path(struct file_manager_app *app, const char *base,
                   const char *suffix, char *out, size_t out_len);
const char *fm_initial_path(void);

void fm_open_entry(struct file_manager_app *app, int idx);
int fm_move_entry_to_dir(struct file_manager_app *app, int src_idx,
                         int dst_idx);
int fm_move_path_to_dir(struct file_manager_app *app, const char *src_path,
                        const char *dst_dir);
int fm_delete_path_now(struct file_manager_app *app, const char *path,
                       uint16_t mode);
void fm_request_delete_selected(struct file_manager_app *app);
int fm_drop_target_dir_at(struct file_manager_app *app, int32_t screen_x,
                          int32_t screen_y, char *out, size_t out_len,
                          int *out_row);

void file_manager_load_path(struct file_manager_app *app, const char *path,
                            int preserve_status);
void file_manager_navigate_back(struct file_manager_app *app);
void file_manager_navigate_up(struct file_manager_app *app);
void file_manager_cleanup(void);

/* ── window event handlers shared between TUs ────────────────────────── */

/* Defined in file_manager_dnd.c, registered as `on_context_menu` by
 * file_manager.c::file_manager_open. */
void file_manager_window_context_menu(struct gui_window *win, int32_t lx,
                                      int32_t ly);

#endif /* APPS_INTERNAL_FILE_MANAGER_INTERNAL_H */
