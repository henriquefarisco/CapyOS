#ifndef KERNEL_LINUX_COMPAT_LINUX_SETID_H
#define KERNEL_LINUX_COMPAT_LINUX_SETID_H

/* Linux ABI identity-changing syscalls.
 *
 *   int setuid(uid_t uid);
 *   int setgid(gid_t gid);
 *   int setresuid(uid_t ruid, uid_t euid, uid_t suid);
 *   int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
 *   int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);
 *   int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid);
 *
 * Why this matters for the Firefox port:
 *   - musl `initgroups()` (run by the dynamic linker for setuid
 *     binaries) calls getresuid+setresuid as part of its
 *     credential scrubbing dance; -ENOSYS aborts the path.
 *   - Sandbox wrappers (firejail-style) issue setresuid/setresgid
 *     to drop to an unprivileged uid before exec; -ENOSYS makes
 *     the wrapper fail-closed and refuse to launch Firefox.
 *
 * Marco M1 runs as a single root user (uid/gid/euid/egid =
 * suid/sgid = 0). Linux semantics:
 *   - setuid(0) succeeds because we are root (CAP_SETUID
 *     implicit).
 *   - setuid(non-zero) -> -EPERM until per-task credentials land.
 *   - setresuid honours the Linux "(uid_t)-1 = no change" sentinel
 *     for each of the three components; passing all -1 is a
 *     no-op success.
 *   - getresuid stores zeros via the ABI pointer, validating the
 *     buffer for NULL (-> -EFAULT). */

#include <stdint.h>
#include <stddef.h>

#define LINUX_SETID_UID_NOCHANGE ((uint32_t)-1)

int64_t linux_setuid    (uint32_t uid);
int64_t linux_setgid    (uint32_t gid);
int64_t linux_setresuid (uint32_t ruid, uint32_t euid, uint32_t suid);
int64_t linux_setresgid (uint32_t rgid, uint32_t egid, uint32_t sgid);
int64_t linux_getresuid (uint32_t *ruid, uint32_t *euid, uint32_t *suid);
int64_t linux_getresgid (uint32_t *rgid, uint32_t *egid, uint32_t *sgid);

void linux_setid_register_syscalls(void);
void linux_setid_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_SETID_H */
