#ifndef KERNEL_LINUX_COMPAT_LINUX_EXEC_EXT_H
#define KERNEL_LINUX_COMPAT_LINUX_EXEC_EXT_H

/* Linux ABI exec + fd-cleanup extensions.
 *
 *   int execveat   (int dirfd, const char *pathname,
 *                    char *const argv[], char *const envp[],
 *                    int flags);
 *   int close_range(unsigned int first, unsigned int last,
 *                    unsigned int flags);
 *
 * Why this matters for the Firefox port:
 *   - musl `posix_spawn` and bash `exec` use execveat with
 *     dirfd-based path resolution to avoid TOCTOU races when
 *     execing helpers found via `which`. -ENOSYS forces a
 *     fallback that re-opens the path (small race window).
 *   - close_range(0, ~0, 0) is the canonical way to scrub all
 *     inherited fds before exec. Firefox content sandbox uses
 *     it before exec'ing the renderer to avoid leaking parent
 *     fds (security-critical). The pre-Linux-5.9 fallback
 *     iterates close(fd) for every fd up to RLIMIT_NOFILE,
 *     which is slow.
 *
 * Linux semantics:
 *   - execveat dirfd: AT_FDCWD or fd >= 0; AT_EMPTY_PATH valid.
 *   - close_range(first, last, flags) closes [first, last]
 *     inclusive. Special values: last == ~0u closes through
 *     end of fd table.
 *   - close_range flags: CLOEXEC sets close-on-exec instead of
 *     closing; UNSHARE shares the parent's fd table on first
 *     close.
 *
 * Marco M1: execveat is a stub returning -ENOSYS until exec
 * itself lands (currently musl + capybrowser stack uses spawned
 * processes, not exec); userland code probes -ENOSYS deterministically.
 * close_range delegates to the existing close() syscall via
 * a callback; we accept any well-formed range. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_AT_FDCWD            (-100)
#define LINUX_AT_EMPTY_PATH       0x1000
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100

#define LINUX_EXECVEAT_KNOWN_FLAGS \
    (LINUX_AT_EMPTY_PATH | LINUX_AT_SYMLINK_NOFOLLOW)

#define LINUX_CLOSE_RANGE_UNSHARE  (1u << 1)
#define LINUX_CLOSE_RANGE_CLOEXEC  (1u << 2)
#define LINUX_CLOSE_RANGE_KNOWN_FLAGS \
    (LINUX_CLOSE_RANGE_UNSHARE | LINUX_CLOSE_RANGE_CLOEXEC)

struct linux_exec_ext_ops {
    /* Optional callback for close_range. Receives a single fd
     * to either close or mark CLOEXEC. NULL = caller falls back
     * to per-fd close which we don't have generically yet. */
    int64_t (*close_one)(int fd);
    int64_t (*set_cloexec_one)(int fd);
};

void linux_exec_ext_install_ops(const struct linux_exec_ext_ops *ops);
void linux_exec_ext_reset_for_tests(void);

int64_t linux_execveat(int dirfd, const char *pathname,
                       char *const argv[], char *const envp[],
                       int flags);
int64_t linux_close_range(uint32_t first, uint32_t last, uint32_t flags);

void linux_exec_ext_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_EXEC_EXT_H */
