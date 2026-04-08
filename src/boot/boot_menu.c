/* boot_menu.c: arrow-key navigable menu widget for boot-time selection.
 *
 * Renders a list of items on the framebuffer; the user navigates with
 * up/down arrow keys (VT100 escape sequences) or number keys and
 * confirms with Enter.  All rendering goes through the I/O callback
 * struct. */
#include "boot/boot_menu.h"

#include <stddef.h>
#include <stdint.h>

/* Font metrics — must match kernel_main.c FONT_* defines. */
#define FONT_W 8u
#define FONT_H 8u
#define FONT_SCALE 2u
#define CELL_W (FONT_W * FONT_SCALE)
#define CELL_H (FONT_H * FONT_SCALE)

/* ---- helpers ------------------------------------------------------------ */

static uint32_t str_len(const char *s) {
  uint32_t n = 0;
  if (s) {
    while (s[n]) {
      ++n;
    }
  }
  return n;
}

static void str_copy(char *dst, uint32_t cap, const char *src) {
  uint32_t i = 0;
  if (!dst || cap == 0) {
    return;
  }
  if (src) {
    while (src[i] && i < cap - 1) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

/* Draw a single menu row.  `highlight` selects colors. */
static void draw_item(const struct boot_menu_io *io, uint32_t x, uint32_t y,
                      const char *prefix, const char *label,
                      int highlight) {
  uint32_t fg = highlight ? io->highlight_fg : io->fg;
  uint32_t bg = highlight ? io->highlight_bg : io->bg;
  uint32_t px = x;
  uint32_t len;

  /* Draw the full row background to the right edge for clean highlight. */
  uint32_t row_w = (io->screen_w > x) ? (io->screen_w - x) : 0;
  if (row_w > 0) {
    io->fill_rect(x, y, row_w, CELL_H, bg);
  }

  /* Prefix (e.g. "> " or "  "). */
  if (prefix) {
    len = str_len(prefix);
    for (uint32_t i = 0; i < len; ++i) {
      io->putch_at(px, y, prefix[i], fg, bg);
      px += CELL_W;
    }
  }

  /* Label text. */
  if (label) {
    len = str_len(label);
    for (uint32_t i = 0; i < len; ++i) {
      io->putch_at(px, y, label[i], fg, bg);
      px += CELL_W;
    }
  }
}

/* ---- public API --------------------------------------------------------- */

void boot_menu_init(struct boot_menu *menu, const char *title) {
  if (!menu) {
    return;
  }
  menu->count = 0;
  menu->selected = 0;
  menu->title = title;
}

void boot_menu_add(struct boot_menu *menu, const char *label, int value) {
  if (!menu || menu->count >= BOOT_MENU_MAX_ITEMS) {
    return;
  }
  str_copy(menu->items[menu->count].label, BOOT_MENU_LABEL_MAX, label);
  menu->items[menu->count].value = value;
  menu->count++;
}

int boot_menu_run(struct boot_menu *menu, const struct boot_menu_io *io,
                  int (*getc)(char *out)) {
  uint32_t base_x, base_y;
  uint32_t title_len;

  if (!menu || !io || !getc || menu->count == 0) {
    return -1;
  }

  /* Compute vertical start so menu is centered. */
  {
    uint32_t total_h = 0;
    if (menu->title) {
      total_h += CELL_H + CELL_H; /* title + gap */
    }
    total_h += menu->count * (CELL_H + 4u); /* items */
    base_y = (io->screen_h > total_h) ? (io->screen_h - total_h) / 2u
                                      : CELL_H * 2u;
  }

  /* Horizontal indent. */
  base_x = CELL_W * 6u;
  if (base_x > io->screen_w / 4u) {
    base_x = io->screen_w / 8u;
  }

  /* Draw title. */
  if (menu->title) {
    title_len = str_len(menu->title);
    uint32_t title_w = title_len * CELL_W;
    uint32_t tx = (io->screen_w > title_w) ? (io->screen_w - title_w) / 2u : 0;
    for (uint32_t i = 0; i < title_len; ++i) {
      io->putch_at(tx + i * CELL_W, base_y, menu->title[i],
                   io->title_fg, io->bg);
    }
    base_y += CELL_H + CELL_H;
  }

  /* Draw all items. */
  for (uint32_t i = 0; i < menu->count; ++i) {
    uint32_t iy = base_y + i * (CELL_H + 4u);
    const char *prefix = (i == menu->selected) ? "> " : "  ";
    draw_item(io, base_x, iy, prefix, menu->items[i].label,
              i == menu->selected);
  }

  /* Input loop. */
  for (;;) {
    char ch = 0;
    uint32_t old_sel = menu->selected;

    if (!getc(&ch)) {
      continue;
    }

    if (ch == '\n' || ch == '\r') {
      return menu->items[menu->selected].value;
    }

    /* VT100 arrow keys: ESC [ A (up), ESC [ B (down). */
    if (ch == 0x1B) {
      char seq1 = 0, seq2 = 0;
      if (!getc(&seq1)) {
        continue;
      }
      if (seq1 != '[') {
        continue;
      }
      if (!getc(&seq2)) {
        continue;
      }
      if (seq2 == 'A') { /* Up */
        if (menu->selected > 0) {
          menu->selected--;
        } else {
          menu->selected = menu->count - 1;
        }
      } else if (seq2 == 'B') { /* Down */
        if (menu->selected + 1 < menu->count) {
          menu->selected++;
        } else {
          menu->selected = 0;
        }
      }
    }

    /* Number keys: '1'..'9' select directly. */
    if (ch >= '1' && ch <= '9') {
      uint32_t idx = (uint32_t)(ch - '1');
      if (idx < menu->count) {
        menu->selected = idx;
      }
    }

    /* Redraw only changed rows. */
    if (menu->selected != old_sel) {
      uint32_t old_y = base_y + old_sel * (CELL_H + 4u);
      uint32_t new_y = base_y + menu->selected * (CELL_H + 4u);
      draw_item(io, base_x, old_y, "  ", menu->items[old_sel].label, 0);
      draw_item(io, base_x, new_y, "> ", menu->items[menu->selected].label, 1);
    }
  }
}
