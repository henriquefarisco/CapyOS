#ifndef DRIVERS_CONSOLE_FBCON_H
#define DRIVERS_CONSOLE_FBCON_H
/* Framebuffer console driver.
 * Manages a pixel-mode text console on the UEFI GOP framebuffer. */
#include <stdint.h>

#define FBCON_FONT_W 8u
#define FBCON_FONT_H 8u
#define FBCON_FONT_SCALE 2u
#define FBCON_CELL_W (FBCON_FONT_W * FBCON_FONT_SCALE)
#define FBCON_CELL_H (FBCON_FONT_H * FBCON_FONT_SCALE)

typedef struct {
  uint32_t *fb;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t origin_y;
  uint32_t cols;
  uint32_t rows;
  uint32_t col;
  uint32_t row;
  uint32_t fg;
  uint32_t bg;
} fbcon_t;

extern fbcon_t g_fbcon;
extern int g_fbcon_serial_mirror;
extern int g_fbcon_serial_ready;

void fbcon_init(uint32_t *fb, uint32_t w, uint32_t h, uint32_t pitch);
void fbcon_putc(char c);
void fbcon_print(const char *s);
void fbcon_clear_view(void);
void fbcon_fill_rect_px(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                        uint32_t color);
void fbcon_putch_at(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fbcon_set_visual_muted(int muted);

void fbcon_print_hex64(uint64_t v);
void fbcon_print_hex(uint64_t v);
void fbcon_print_dec_u32(uint32_t v);
void fbcon_print_hex8(uint8_t v);
void fbcon_print_hex16(uint16_t v);
void fbcon_print_ipv4(uint32_t ip);
void fbcon_print_mac(const uint8_t mac[6]);

#endif /* DRIVERS_CONSOLE_FBCON_H */
