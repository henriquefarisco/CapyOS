#ifndef ARCH_X86_64_FRAMEBUFFER_CONSOLE_H
#define ARCH_X86_64_FRAMEBUFFER_CONSOLE_H

#include <stdint.h>

/* Framebuffer console public API.
 * Implementation lives in framebuffer_console.c.
 * These declarations allow other modules to call fbcon functions without
 * relying on bare extern declarations. */

void fbcon_putc(char c);
void fbcon_print(const char *s);
void fbcon_clear_view(void);
void fbcon_print_hex64(uint64_t v);
void fbcon_print_hex(uint64_t v);
void fbcon_print_dec_u32(uint32_t v);
void fbcon_print_hex8(uint8_t v);
void fbcon_print_hex16(uint16_t v);
void fbcon_print_ipv4(uint32_t ip);
void fbcon_print_mac(const uint8_t mac[6]);
void fbcon_putch_at(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fbcon_fill_rect_px(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                        uint32_t color);
void fbcon_set_visual_muted(int muted);

/* Desktop framebuffer queries (set during kernel_main64 init). */
uint32_t *kernel_desktop_get_fb(void);
uint32_t kernel_desktop_get_width(void);
uint32_t kernel_desktop_get_height(void);
uint32_t kernel_desktop_get_pitch(void);

/* Input from serial/keyboard (non-blocking). */
int kernel_input_trygetc(char *out_char);

#endif /* ARCH_X86_64_FRAMEBUFFER_CONSOLE_H */
