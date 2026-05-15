#ifndef KERNEL_LINUX_COMPAT_LINUX_SANDBOX_H
#define KERNEL_LINUX_COMPAT_LINUX_SANDBOX_H

/* Linux ABI sandbox-related syscalls.
 *
 *   int chroot     (const char *path);
 *   int personality(unsigned long persona);
 *   int setfsuid   (uid_t fsuid);
 *   int setfsgid   (gid_t fsgid);
 *
 * Why this matters for the Firefox port:
 *   - Firefox content sandbox calls `chroot("/")` after seccomp
 *     filter installation to remove path-based attack surface
 *     from the renderer. -ENOSYS makes the sandbox refuse to
 *     start.
 *   - glibc/musl probe `personality(PER_LINUX | ADDR_NO_RANDOMIZE)`
 *     to disable ASLR for valgrind-style debugging; -ENOSYS makes
 *     the probe think the kernel is too old.
 *   - `setfsuid`/`setfsgid` are used by NFS clients and a few
 *     setuid helpers to swap the filesystem-access uid; Linux
 *     returns the previous fsuid/fsgid (Linux semantics).
 *   - Some C++ runtimes call personality to identify themselves
 *     via PER_LINUX_32BIT for COMPAT_BINPRM-style probes.
 *
 * Marco M1 runs as root in a single-process world. We accept
 * the syscalls and:
 *   - chroot: validates the path argument and returns 0 (Marco
 *     M1 has only one root view; no actual change). When namei
 *     walker lands, this stores per-task root override.
 *   - personality: stores the value in module-local state and
 *     returns the previous value (Linux semantics: returning
 *     0xFFFFFFFF would be the error path). Querying with
 *     persona == 0xFFFFFFFF reads without writing.
 *   - setfsuid/setfsgid: stores in module-local state and
 *     returns the previous value. */

#include <stdint.h>
#include <stddef.h>

/* personality() persona constants. The full set is large; we
 * accept any persona value (including unknown bits) per Linux
 * convention -- the kernel never rejects on unknown bits, it
 * just ignores them. */
#define LINUX_PER_LINUX           0x0000
#define LINUX_PER_LINUX_32BIT     0x0008
#define LINUX_PER_LINUX_FDPIC     0x0080
#define LINUX_ADDR_NO_RANDOMIZE   0x0040000
#define LINUX_ADDR_LIMIT_3GB      0x8000000

/* personality query sentinel: passing 0xFFFFFFFF returns the
 * current persona without modifying it. */
#define LINUX_PERSONALITY_QUERY   0xFFFFFFFFu

struct linux_sandbox_ops {
    /* Optional callback for chroot. NULL = caller falls back to
     * accepting any well-formed path as a no-op (Marco M1 single
     * root). The callback returns 0 on success, negative errno
     * otherwise. */
    int64_t (*chroot)(const char *path);
};

void linux_sandbox_install_ops(const struct linux_sandbox_ops *ops);
void linux_sandbox_reset_for_tests(void);

int64_t linux_chroot     (const char *path);
int64_t linux_personality(uint32_t persona);
int64_t linux_setfsuid   (int fsuid);
int64_t linux_setfsgid   (int fsgid);

void linux_sandbox_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_SANDBOX_H */
