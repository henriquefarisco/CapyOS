/*
 * src/gui/desktop/internal/taskbar_internal.h
 *
 * Internal helpers shared inside the `taskbar` translation-unit
 * group:
 *
 *   - taskbar.c             : main bar lifecycle, paint, click,
 *                             clock, tray, window list.
 *   - taskbar_menu.c        : start-menu data model, popup
 *                             rendering and recent/session helpers.
 *   - taskbar_menu_input.c  : event handlers (toggle, click, hover,
 *                             scroll, keyboard) + entry activation.
 *
 * Not part of the public API. External callers must keep using
 * `include/gui/taskbar.h`.
 *
 * The symbols below used to be `static` inside the pre-split
 * monolith. Exposing them as extern (with their original names) lets
 * the three sister translation units share state without duplicating
 * logic, and is the only behavioural change in the 2026-05-15
 * refactor.
 */
#ifndef GUI_DESKTOP_INTERNAL_TASKBAR_INTERNAL_H
#define GUI_DESKTOP_INTERNAL_TASKBAR_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/taskbar.h"

/* ── menu row sentinels (taskbar.c) ──────────────────────────────────── */

#define TASKBAR_MENU_ROW_RECENT_TOGGLE 900
#define TASKBAR_MENU_ROW_RECENT_BASE 1000

/* ── string + drawing utilities (taskbar.c) ──────────────────────────── */

void tb_strcpy(char *d, const char *s, size_t max);
uint32_t tb_strlen(const char *s);
int tb_streq(const char *a, const char *b);
void tb_append(char *dst, size_t dst_len, const char *src);
int tb_contains_ci(const char *text, const char *needle);
void tb_fill_rect(struct gui_surface *s, int32_t x, int32_t y, uint32_t w,
                  uint32_t h, uint32_t color);
void tb_fit_text(const struct font *f, const char *src, uint32_t max_width,
                 char *out, size_t out_len);
void tb_draw_fit(struct gui_surface *s, const struct font *f, int32_t x,
                 int32_t y, uint32_t max_width, const char *text,
                 uint32_t color);

/* ── menu data-model helpers (taskbar_menu.c) ────────────────────────── */

const char *taskbar_menu_group_label(struct taskbar *tb, uint32_t index);
int taskbar_menu_entry_is_session(struct taskbar *tb, uint32_t index);
uint32_t taskbar_session_count(struct taskbar *tb);
int taskbar_session_entry_by_slot(struct taskbar *tb, uint32_t slot);
int taskbar_menu_entry_matches(struct taskbar *tb, uint32_t index);
int taskbar_recent_row(uint32_t index);
int taskbar_row_is_recent(int row);
uint32_t taskbar_row_recent_index(int row);
int taskbar_recent_entry_matches(struct taskbar *tb, uint32_t index);
int taskbar_recent_matches(struct taskbar *tb, uint32_t index);
uint32_t taskbar_recent_available_count(struct taskbar *tb);
void taskbar_note_recent(struct taskbar *tb, struct taskbar_menu_entry *entry);

int taskbar_first_match(struct taskbar *tb);
int taskbar_last_match(struct taskbar *tb);
int taskbar_next_match(struct taskbar *tb, int current);
int taskbar_prev_match(struct taskbar *tb, int current);

uint32_t taskbar_menu_visible_height(struct taskbar *tb);
uint32_t taskbar_menu_footer_height(struct taskbar *tb);
uint32_t taskbar_recent_popup_height(struct taskbar *tb);
uint32_t taskbar_menu_current_height(struct taskbar *tb);
uint32_t taskbar_menu_scroll_max(struct taskbar *tb);
void taskbar_clamp_menu_scroll(struct taskbar *tb);

void taskbar_menu_popup_position(struct taskbar *tb, uint32_t popup_w,
                                 uint32_t popup_h, int32_t *out_x,
                                 int32_t *out_y);
int taskbar_menu_entry_at(struct taskbar *tb, int32_t local_x,
                          int32_t local_y);
void taskbar_refresh_menu_popup(struct taskbar *tb);

int taskbar_recent_popup_entry_at(struct taskbar *tb, int32_t local_y);
void taskbar_hide_recent_popup(struct taskbar *tb);
void taskbar_refresh_recent_popup(struct taskbar *tb);

void menu_popup_paint(struct gui_window *win);

#endif /* GUI_DESKTOP_INTERNAL_TASKBAR_INTERNAL_H */
