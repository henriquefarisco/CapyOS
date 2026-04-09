#ifndef BOOT_UI_H
#define BOOT_UI_H

#include <stdint.h>

/* Maximum hardware compatibility warnings collected during boot. */
#define BOOT_WARNING_MAX 8
#define BOOT_WARNING_TEXT_MAX 96

struct boot_warnings {
  uint32_t count;
  char messages[BOOT_WARNING_MAX][BOOT_WARNING_TEXT_MAX];
};

void boot_warnings_init(struct boot_warnings *w);
void boot_warnings_add(struct boot_warnings *w, const char *msg);

/* I/O callbacks that the boot UI uses to draw on the framebuffer.
 * Populated from kernel_main.c statics so boot_ui.c stays decoupled. */
struct boot_ui_io {
  uint32_t screen_w;
  uint32_t screen_h;
  uint32_t splash_bg;
  uint32_t splash_icon;
  uint32_t splash_bar_border;
  uint32_t splash_bar_bg;
  uint32_t splash_bar_fill;
  uint32_t text_fg;
  uint32_t text_muted_fg;
  uint32_t console_bg;
  void (*fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint32_t color);
  void (*putch_at)(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
  void (*draw_icon)(uint32_t x, uint32_t y, uint32_t scale, uint32_t color);
};

/* Lifecycle: call begin, then advance/set_status per stage, then end. */
void boot_ui_init(const struct boot_ui_io *io);
void boot_ui_splash_begin(void);
void boot_ui_splash_advance(uint32_t stage, uint32_t total);
void boot_ui_splash_set_status(const char *text);
void boot_ui_splash_end(void);

/* Show compatibility warnings. Returns 0=continue, 1=halt. */
int boot_ui_show_warnings(const struct boot_warnings *w,
                          int (*getc)(char *out));

#endif /* BOOT_UI_H */
