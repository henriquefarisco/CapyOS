#ifndef BOOT_MENU_H
#define BOOT_MENU_H

#include <stdint.h>

#define BOOT_MENU_MAX_ITEMS 16
#define BOOT_MENU_LABEL_MAX 64

struct boot_menu_item {
  char label[BOOT_MENU_LABEL_MAX];
  int value;
};

struct boot_menu {
  struct boot_menu_item items[BOOT_MENU_MAX_ITEMS];
  uint32_t count;
  uint32_t selected;
  const char *title;
};

/* I/O callbacks for rendering and input. */
struct boot_menu_io {
  uint32_t screen_w;
  uint32_t screen_h;
  uint32_t bg;
  uint32_t fg;
  uint32_t highlight_bg;
  uint32_t highlight_fg;
  uint32_t title_fg;
  void (*fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint32_t color);
  void (*putch_at)(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
};

void boot_menu_init(struct boot_menu *menu, const char *title);
void boot_menu_add(struct boot_menu *menu, const char *label, int value);

/* Run the menu interactively. Returns the value of the selected item.
 * getc blocks until a character is available; arrow keys are expected
 * as VT100 escape sequences (ESC [ A/B for up/down). */
int boot_menu_run(struct boot_menu *menu, const struct boot_menu_io *io,
                  int (*getc)(char *out));

#endif /* BOOT_MENU_H */
