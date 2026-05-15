#ifndef KERNEL_LINUX_COMPAT_LINUX_DUP_H
#define KERNEL_LINUX_COMPAT_LINUX_DUP_H

/* Linux ABI `dup(2)` and `dup2(2)` -- file descriptor duplication.
 *
 *   int dup(int oldfd);
 *   int dup2(int oldfd, int newfd);
 *
 * dup() returns the lowest unused fd that refers to the same
 * open file description as `oldfd`. dup2() forces the duplicate
 * to be `newfd`, atomically closing `newfd` first if it was
 * open. dup3() (already wired in linux_fd) adds a flags arg.
 *
 * Marco M1 doesn't have a unified per-process fd table -- file
 * descriptors are encoded by backend (devfs 0x8000, procfs 0x8800,
 * shm 0x9000, tmpfs 0xA000, etc.) and the kernel resolves them
 * at each syscall. There's no shared "open file description"
 * object to refcount, so a true dup that would share a position
 * cursor between two fds isn't representable yet.
 *
 * Strategy:
 *   - Validate arguments fully (Linux semantics for EBADF/EINVAL).
 *   - Treat `dup2(fd, fd)` specially: Linux returns `newfd`
 *     unchanged when oldfd == newfd (and oldfd is valid).
 *   - Otherwise return -ENOSYS via an injectable callback. The
 *     boot init can later install a real dup that allocates a
 *     fresh entry in a unified fd table when one exists.
 *
 * Userland that needs dup primarily for stdio redirection (`<`,
 * `>`, `2>&1` in shells) will see -ENOSYS until the fd table
 * lands; userland that uses dup as a no-op for "is this fd
 * open?" via dup2(fd, fd) gets a faithful answer. */

#include <stdint.h>

struct linux_dup_ops {
    /* Allocate a fresh fd that refers to the same backing
     * object as oldfd. Return -errno on failure, or the new
     * fd on success. */
    int64_t (*dup)(int oldfd);
    /* Duplicate oldfd to newfd, closing newfd first if open.
     * Return newfd on success, -errno on failure. Caller has
     * already validated oldfd >= 0, newfd >= 0,
     * oldfd != newfd. */
    int64_t (*dup2)(int oldfd, int newfd);
};

void linux_dup_install_ops(const struct linux_dup_ops *ops);

int64_t linux_dup(int oldfd);
int64_t linux_dup2(int oldfd, int newfd);

void linux_dup_register_syscalls(void);
void linux_dup_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_DUP_H */
