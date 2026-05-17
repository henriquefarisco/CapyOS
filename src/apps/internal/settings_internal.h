/*
 * src/apps/internal/settings_internal.h
 *
 * Internal helpers shared inside the `settings` translation-unit
 * group:
 *
 *   - settings.c          : lifecycle, window event dispatch, public
 *                           open/switch_tab, click router.
 *   - settings_view.c     : tab layout helpers and `settings_paint`.
 *   - settings_actions.c  : inline-prompt action callbacks for theme,
 *                           keyboard, language, user creation and
 *                           browser homepage.
 *
 * Not part of the public API. External callers must keep using
 * `include/apps/settings.h`.
 *
 * The symbols below used to be `static` inside the pre-split
 * monolith. Exposing them as extern (with their original names) lets
 * the three sister translation units share state without duplicating
 * logic, and is the only behavioural change in the 2026-05-15
 * refactor.
 */
#ifndef APPS_INTERNAL_SETTINGS_INTERNAL_H
#define APPS_INTERNAL_SETTINGS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "apps/settings.h"
#include "auth/user.h"
#include "gui/compositor.h"

/* ── click-row infrastructure ────────────────────────────────────────── */

#define SETTINGS_MAX_ROWS       24
#define SETTINGS_ROW_THEME      1u
#define SETTINGS_ROW_KEYBOARD   2u
#define SETTINGS_ROW_LANGUAGE   3u
#define SETTINGS_ROW_NEWUSER    4u
#define SETTINGS_ROW_HOMEPAGE   5u

struct settings_click_row {
  int32_t  x0, y0, x1, y1;
  uint16_t kind;
  char     arg[40];
};

/* ── globals defined in settings.c ───────────────────────────────────── */

extern struct settings_app g_settings;
extern int g_settings_open;
extern struct settings_click_row g_rows[SETTINGS_MAX_ROWS];
extern uint32_t g_row_count;

/* Username pipeline buffer used to chain on_username_submit ->
 * on_password_submit, defined in settings_actions.c. */
extern char g_pending_username[USER_NAME_MAX];

/* ── tab labels (settings.c) ─────────────────────────────────────────── */

const char *tab_label(enum settings_tab t);

/* ── status helper (settings.c) ──────────────────────────────────────── */

void settings_set_status(struct settings_app *app, const char *text,
                         uint32_t color);

/* ── click row helpers (settings.c) ──────────────────────────────────── */

void rows_reset(void);
void rows_add(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t kind,
              const char *arg);
const struct settings_click_row *rows_hit(int32_t x, int32_t y);

/* ── layout helpers (settings_view.c) ────────────────────────────────── */

int32_t settings_sidebar_width(uint32_t surface_w, uint8_t scale);
void settings_layout_tabs(struct settings_app *app);

/* ── username + admin gates (settings_actions.c) ─────────────────────── */

int settings_validate_username(const char *name);
int settings_require_admin(struct settings_app *app);

/* ── action entry points (settings_actions.c) ────────────────────────── */

void apply_theme_choice(const char *theme);
void apply_keyboard_choice(const char *layout);
void apply_language_choice(const char *lang);
void start_user_creation(void);
void start_homepage_edit(void);

/* ── small numeric formatters reused by view/actions ─────────────────── */

void settings_u32_str(uint32_t v, char *buf, int len);
void ipv4_str(uint32_t ip, char *out, int len);

#endif /* APPS_INTERNAL_SETTINGS_INTERNAL_H */
