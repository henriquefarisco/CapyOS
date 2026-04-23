#ifndef CORE_KLOG_H
#define CORE_KLOG_H

#include <stddef.h>
#include <stdint.h>

/* CapyOS Kernel Log (klog)
 *
 * Ring buffer em memoria que acumula mensagens de diagnostico desde o early
 * boot. Quando o filesystem estiver montado, klog_flush() persiste apenas as
 * entradas novas em /var/log/capyos_klog.txt. Ate la, klog_dump() pode
 * serializar para qualquer callback de I/O.
 *
 * Cada entrada ocupa ate KLOG_LINE_MAX bytes.
 * O ring buffer tem KLOG_RING_LINES slots; ao estourar, as entradas mais
 * antigas sao sobrescritas.
 */

#define KLOG_LINE_MAX 128u
#define KLOG_RING_LINES 256u
#define KLOG_RING_BYTES (KLOG_LINE_MAX * KLOG_RING_LINES)

#define KLOG_DEBUG 0
#define KLOG_INFO 1
#define KLOG_WARN 2
#define KLOG_ERROR 3

void klog(int level, const char *msg);
void klog_hex(int level, const char *prefix, uint64_t value);
void klog_dec(int level, const char *prefix, uint32_t value);
void klog_dump(void (*print)(const char *s));
int klog_flush(int (*write_fn)(const char *path, const char *text));
uint32_t klog_count(void);
void klog_reset(void);
const char *klog_serialize(void);

#endif /* CORE_KLOG_H */
