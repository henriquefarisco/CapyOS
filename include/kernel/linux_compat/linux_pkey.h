#ifndef KERNEL_LINUX_COMPAT_LINUX_PKEY_H
#define KERNEL_LINUX_COMPAT_LINUX_PKEY_H

/* Linux ABI memory protection keys (MPK / x86 PKU) syscalls.
 *
 *   int pkey_alloc   (unsigned int flags, unsigned int access_rights);
 *   int pkey_free    (int pkey);
 *   int pkey_mprotect(void *addr, size_t len, int prot, int pkey);
 *
 * Why this matters for the Firefox port:
 *   - SpiderMonkey's W^X JIT uses pkey_mprotect to flip the
 *     code buffer from RW to RX without burning a TLB shootdown
 *     -- the CPU's PKRU register lets the same pages be either
 *     readable+writable or readable+executable depending on the
 *     thread-local key state. -ENOSYS makes the JIT fall back
 *     to mprotect-based dual mappings (slower).
 *   - Mozilla's libsecret uses pkey_alloc to mark the
 *     credentials buffer as needing explicit access enable
 *     before a memcpy can read from it.
 *
 * Linux semantics:
 *   - pkey_alloc: 16 keys per task on x86 PKU; flags must be 0;
 *     access_rights mask whitelist. -ENOSPC when exhausted.
 *   - pkey_free: invalid pkey -> -EINVAL.
 *   - pkey_mprotect: like mprotect but binds the range to the
 *     given key.
 *
 * Marco M1: 16-slot per-task key table; the actual hardware
 * PKRU isn't programmed (cooperative single-thread). Userland
 * that uses pkey_mprotect just gets a no-op binding. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_PKEY_DISABLE_ACCESS  0x1
#define LINUX_PKEY_DISABLE_WRITE   0x2
#define LINUX_PKEY_ACCESS_KNOWN \
    (LINUX_PKEY_DISABLE_ACCESS | LINUX_PKEY_DISABLE_WRITE)

#define LINUX_PKEY_MAX             16

/* prot bits (subset; mirror linux/mman.h). */
#define LINUX_PROT_NONE   0
#define LINUX_PROT_READ   1
#define LINUX_PROT_WRITE  2
#define LINUX_PROT_EXEC   4
#define LINUX_PROT_KNOWN_BITS \
    (LINUX_PROT_READ | LINUX_PROT_WRITE | LINUX_PROT_EXEC)

int64_t linux_pkey_alloc   (uint32_t flags, uint32_t access_rights);
int64_t linux_pkey_free    (int pkey);
int64_t linux_pkey_mprotect(uintptr_t addr, size_t len, int prot, int pkey);

void linux_pkey_register_syscalls(void);
void linux_pkey_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_PKEY_H */
