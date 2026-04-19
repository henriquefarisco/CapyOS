/* Kernel console abstraction for 64-bit kernel.
 * Provides k_printf/k_log functions that work with framebuffer console.
 */
#ifndef KCON_H
#define KCON_H

#include <stdint.h>

/* Log severity levels */
#define KLOG_DEBUG 0
#define KLOG_INFO 1
#define KLOG_WARN 2
#define KLOG_ERROR 3

/* Initialize kernel console (called by kernel_main64) */
void kcon_init(void);

/* Print string to console */
void k_puts(const char *s);

/* Print character to console */
void k_putc(char c);

/* Print hex value */
void k_hex32(uint32_t val);
void k_hex64(uint64_t val);

/* Print decimal value */
void k_dec32(uint32_t val);

/* Log message with prefix */
void k_log(int level, const char *prefix, const char *msg);

/* Formatted log (simplified, no format strings - just prefix + message) */
#define K_DEBUG(prefix, msg) k_log(KLOG_DEBUG, prefix, msg)
#define K_INFO(prefix, msg) k_log(KLOG_INFO, prefix, msg)
#define K_WARN(prefix, msg) k_log(KLOG_WARN, prefix, msg)
#define K_ERROR(prefix, msg) k_log(KLOG_ERROR, prefix, msg)

#endif /* KCON_H */
