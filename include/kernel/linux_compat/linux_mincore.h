#ifndef KERNEL_LINUX_COMPAT_LINUX_MINCORE_H
#define KERNEL_LINUX_COMPAT_LINUX_MINCORE_H

/* Linux ABI memory-residency query syscall.
 *
 *   int mincore(void *addr, size_t length, unsigned char *vec);
 *
 * Why this matters for the Firefox port:
 *   - Firefox's JIT (SpiderMonkey IonMonkey) uses mincore on
 *     the trampoline buffer page right before flipping it
 *     RX. -ENOSYS forces a fallback that touches every byte
 *     to ensure pre-faulting (slow on cold start).
 *   - PGO/profile data loaders use mincore to skip already-
 *     resident pages; -ENOSYS makes them re-read everything.
 *   - glibc's `posix_spawn`-style probes call mincore on the
 *     stack to decide whether COW is safe.
 *
 * Linux semantics:
 *   - addr must be page-aligned -> -EINVAL otherwise.
 *   - length is rounded up to the next page.
 *   - vec must point to (length + PAGE_SIZE - 1) / PAGE_SIZE
 *     bytes; each byte's LSB is set if the page is resident.
 *   - Bits 1..6 are reserved (0); bit 7 indicates that the
 *     page is locked.
 *
 * Marco M1 has no swap and no page-aging logic, so all RAM-
 * backed pages are "always resident". We:
 *   - validate addr alignment + length > 0,
 *   - write 1 (resident) for every byte of vec,
 *   - return 0.
 *
 * If the address range is invalid (NULL addr without
 * length 0, or addr+length overflow), Linux returns -ENOMEM. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_MINCORE_PAGE_SIZE   4096

int64_t linux_mincore(uintptr_t addr, size_t length, uint8_t *vec);

void linux_mincore_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_MINCORE_H */
