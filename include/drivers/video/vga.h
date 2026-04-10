/* VGA shim for 64-bit kernel.
 * Maps vga_write() to kcon k_puts() for CAPYFS/VFS compatibility.
 * This allows existing FS code to compile without modification.
 */
#ifndef VGA_H
#define VGA_H

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#ifdef __x86_64__
/* For 64-bit kernel, these symbols are provided by arch/x86_64/stubs.c. */
void vga_init(void);
void vga_write(const char *s);
void vga_newline(void);
void vga_clear(void);
void vga_set_color(unsigned char fg, unsigned char bg);
void vga_putc(char c);
void vga_backspace(void);
void vga_backspace_multiple(int n);
void vga_update_hw_cursor(void);
void vga_get_cursor(int *row, int *col);
void vga_set_cursor(int row, int col);

#else
/* For 32-bit kernel, use existing VGA functions */
void vga_init(void);
void vga_write(const char *s);
void vga_newline(void);
void vga_clear(void);
void vga_set_color(unsigned char fg, unsigned char bg);
void vga_putc(char c);
void vga_backspace(void);
void vga_backspace_multiple(int n);
void vga_update_hw_cursor(void);
void vga_get_cursor(int *row, int *col);
void vga_set_cursor(int row, int col);
#endif

#endif /* VGA_H */
