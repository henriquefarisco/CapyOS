/* VGA shim for 64-bit kernel.
 * Maps vga_write() to kcon k_puts() for NoirFS/VFS compatibility.
 * This allows existing FS code to compile without modification.
 */
#ifndef VGA_H
#define VGA_H

#ifdef __x86_64__
/* For 64-bit kernel, redirect VGA output to framebuffer console */
#include "core/kcon.h"

static inline void vga_write(const char *s) { k_puts(s); }

static inline void vga_newline(void) { k_putc('\n'); }

#else
/* For 32-bit kernel, use existing VGA functions */
void vga_init(void);
void vga_write(const char *s);
void vga_newline(void);
void vga_clear(void);
void vga_set_color(unsigned char fg, unsigned char bg);
#endif

#endif /* VGA_H */
