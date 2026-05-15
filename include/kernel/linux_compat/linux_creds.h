#ifndef KERNEL_LINUX_COMPAT_LINUX_CREDS_H
#define KERNEL_LINUX_COMPAT_LINUX_CREDS_H

/* Linux ABI supplementary group credentials.
 *
 *   int getgroups(int size, gid_t list[]);
 *   int setgroups(size_t size, const gid_t *list);
 *
 * Why this matters for the Firefox port:
 *   - musl `initgroups()` (run by the dynamic linker for setuid
 *     programs) calls getgroups+setgroups; -ENOSYS short-circuits
 *     the whole credential-scrubbing path and the linker bails.
 *   - getgroups(0, NULL) is the documented Linux idiom to query
 *     the supplementary group count without copying; userland
 *     issues this dozens of times during startup.
 *
 * Marco M1 runs as root with no supplementary groups, so the
 * kernel's authoritative answer is "zero groups". setgroups
 * succeeds as a no-op for any well-formed list (CAP_SETGID is
 * implicit at root). When a real credential subsystem lands,
 * provider injection lets the boot init swap the storage in. */

#include <stdint.h>
#include <stddef.h>

/* Linux: NGROUPS_MAX = 65536 (sysconf result). Anything larger
 * is rejected at the syscall boundary. */
#define LINUX_NGROUPS_MAX 65536

int64_t linux_getgroups(int size, uint32_t *list);
int64_t linux_setgroups(size_t size, const uint32_t *list);

void linux_creds_register_syscalls(void);
void linux_creds_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_CREDS_H */
