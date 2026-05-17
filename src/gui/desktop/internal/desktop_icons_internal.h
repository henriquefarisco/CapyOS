#ifndef GUI_DESKTOP_DESKTOP_ICONS_INTERNAL_H
#define GUI_DESKTOP_DESKTOP_ICONS_INTERNAL_H

/*
 * src/gui/desktop/internal/desktop_icons_internal.h
 *
 * Internal header for desktop_icons.c and its sibling
 * desktop_icons_context.c. Carries the shared static state
 * (`g_di`), the `di_entry` element type, the DI_CTX_* action
 * constants, and the declarations of `di_*` helpers that the
 * context-menu sibling needs to consume.
 *
 * Introduced in the 2026-05-16 preventive refactor when
 * desktop_icons.c reached 823/900 LOC. The context-menu code
 * (~140 LOC of action dispatch + inline-prompt callbacks) was
 * extracted into a sibling TU; this header is the linkage glue.
 *
 * NOT a public header. Consumers outside src/gui/desktop/ must
 * keep using include/gui/desktop_icons.h.
 */

#include <stdint.h>
#include <stddef.h>

#include "gui/desktop_icons.h"
#include "fs/vfs.h"

struct di_entry {
  char     name[DESKTOP_ICONS_NAME_MAX];
  uint16_t mode;
  uint8_t  reserved[2];
};

struct di_state {
  struct di_entry entries[DESKTOP_ICONS_MAX];
  uint32_t        count;
  char            path[256];
  uint32_t        taskbar_h;
  int             selected;          /* idx ou -1 */
  uint32_t        screen_w;
  uint32_t        screen_h;
  /* Contexto da operacao (rename/new) em curso. */
  int             pending_action;    /* DI_CTX_* */
  int             pending_target;    /* idx do icone alvo, ou -1 */
  int             drag_active;
  int             drag_source;
  int             drag_over;
  int             drag_open_on_release;
  int             drag_moved;
  int32_t         drag_start_x;
  int32_t         drag_start_y;
};

extern struct di_state g_di;

#define DI_CTX_OPEN     1u
#define DI_CTX_DELETE   2u
#define DI_CTX_RENAME   3u
#define DI_CTX_NEW_FILE 4u
#define DI_CTX_NEW_DIR  5u
#define DI_CTX_REFRESH  6u

/* Helpers shared with desktop_icons_context.c.
 * Definitions live in desktop_icons.c. */
void di_strcpy(char *d, const char *s, uint32_t max);
void di_join(const char *dir, const char *name, char *out, uint32_t max);
void di_icon_position(uint32_t idx, int32_t *out_x, int32_t *out_y);
int  di_is_text(const char *name);
void di_request_delete_selected(void);

#endif /* GUI_DESKTOP_DESKTOP_ICONS_INTERNAL_H */
