#ifndef CAPYOS_CAPYGFX_TOOLBAR_H
#define CAPYOS_CAPYGFX_TOOLBAR_H

/* Pure pixel toolbar + interaction model for the ring-3 graphical browser.
 * The module owns no window or network resources: callers feed key/mouse input,
 * draw into their own ARGB32 buffer and execute returned actions. */

#include <stddef.h>
#include <stdint.h>

#include "browser_navigation.h"
#include "display_list.h"

#define CAPYGFX_TOOLBAR_HEIGHT 40u
#define CAPYGFX_TOOLBAR_GLYPH_W 8u
#define CAPYGFX_TOOLBAR_GLYPH_H 8u
#define CAPYGFX_TOOLBAR_DETAIL_MAX 96u

struct capygfx_toolbar_rect {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

struct capygfx_toolbar_layout {
  struct capygfx_toolbar_rect back;
  struct capygfx_toolbar_rect forward;
  struct capygfx_toolbar_rect reload;
  struct capygfx_toolbar_rect address;
  struct capygfx_toolbar_rect go;
  struct capygfx_toolbar_rect status;
  uint32_t toolbar_height;
  uint32_t page_y;
  uint32_t page_height;
};

enum capygfx_toolbar_action {
  CAPYGFX_TOOLBAR_ACTION_NONE = 0,
  CAPYGFX_TOOLBAR_ACTION_BACK,
  CAPYGFX_TOOLBAR_ACTION_FORWARD,
  CAPYGFX_TOOLBAR_ACTION_RELOAD,
  CAPYGFX_TOOLBAR_ACTION_GO,
  CAPYGFX_TOOLBAR_ACTION_FOCUS_ADDRESS
};

struct capygfx_toolbar {
  char address[BROWSER_NAVIGATION_URL_MAX];
  size_t address_len;
  size_t cursor;
  int address_focused;
  int select_all;
  int can_back;
  int can_forward;
  enum browser_navigation_state navigation_state;
  int http_status;
  char detail[CAPYGFX_TOOLBAR_DETAIL_MAX];
};

void capygfx_toolbar_init(struct capygfx_toolbar *toolbar);

/* Synchronize buttons/status and, when a document is READY, its canonical URL.
 * A failed navigation deliberately does not overwrite an address the user may
 * still want to correct and resubmit. */
void capygfx_toolbar_sync_navigation(
    struct capygfx_toolbar *toolbar,
    const struct browser_navigation *navigation);

/* Atomic bounded address replacement. Returns 1 on success, 0 on NULL/overflow
 * and preserves the previous value on failure. */
int capygfx_toolbar_set_address(struct capygfx_toolbar *toolbar,
                               const char *address);

/* Set a bounded user-facing status/error detail. NULL or empty clears it. */
void capygfx_toolbar_set_detail(struct capygfx_toolbar *toolbar,
                               const char *detail);

/* Deterministic responsive layout. Every rectangle remains inside width/height;
 * on very narrow surfaces the address field collapses before buttons overlap. */
void capygfx_toolbar_compute_layout(uint32_t width, uint32_t height,
                                    struct capygfx_toolbar_layout *out);

enum capygfx_toolbar_action capygfx_toolbar_hit_test(
    const struct capygfx_toolbar *toolbar,
    const struct capygfx_toolbar_layout *layout, int32_t x, int32_t y);

/* Mouse-down focuses/positions the address caret or returns a button action. */
enum capygfx_toolbar_action capygfx_toolbar_mouse_down(
    struct capygfx_toolbar *toolbar,
    const struct capygfx_toolbar_layout *layout, int32_t x, int32_t y);

/* Handle one CapyOS keycode. Printable ASCII inserts at the caret; Backspace,
 * Delete, Left/Right/Home/End edit boundedly; Enter returns GO; F5 returns
 * RELOAD and F6 focuses/selects the address. */
enum capygfx_toolbar_action capygfx_toolbar_key(
    struct capygfx_toolbar *toolbar, uint32_t keycode);

/* Draw toolbar/status into caller-owned ARGB32 pixels. Fully clipped; returns 0
 * on success or -1 for invalid args. */
int capygfx_toolbar_draw(const struct capygfx_toolbar *toolbar, uint32_t *pixels,
                         uint32_t width, uint32_t height);

/* Map a window-local click into display-list cell coordinates, accounting for
 * the toolbar/page origin and pixel scroll. On a link, atomically copies its URL
 * and returns 1. Returns 0 for no link/invalid geometry/overflow, with out_url
 * empty. Reverse traversal selects the visually topmost matching link. */
int capygfx_toolbar_link_hit_test(const struct capy_dl *display_list,
                                  int32_t window_x, int32_t window_y,
                                  uint32_t page_y, int32_t scroll_x,
                                  int32_t scroll_y, uint32_t cell_width,
                                  uint32_t cell_height, char *out_url,
                                  size_t out_url_cap);

#endif /* CAPYOS_CAPYGFX_TOOLBAR_H */
