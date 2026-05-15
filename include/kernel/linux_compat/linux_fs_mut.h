#ifndef KERNEL_LINUX_COMPAT_LINUX_FS_MUT_H
#define KERNEL_LINUX_COMPAT_LINUX_FS_MUT_H

/* Linux ABI filesystem mutation syscalls -- mkdir/rmdir/unlink/
 * rename (path-based) plus their *at-family variants that take a
 * directory fd.
 *
 *   int   mkdir(const char *path, mode_t mode);
 *   int   mkdirat(int dirfd, const char *path, mode_t mode);
 *   int   rmdir(const char *path);
 *   int   unlink(const char *path);
 *   int   unlinkat(int dirfd, const char *path, int flags);
 *   int   rename(const char *old, const char *new);
 *   int   renameat(int oldfd, const char *old, int newfd, const char *new);
 *   int   renameat2(int oldfd, const char *old, int newfd, const char *new,
 *                   unsigned int flags);
 *
 * Why this matters for the Firefox port:
 *   - Firefox profile setup creates `~/.mozilla/firefox/<id>` via
 *     mkdir, and Firefox cache writes go through atomic
 *     rename(tmpfile, finalfile) every time the cache index
 *     advances. Without these, the very first profile load
 *     aborts.
 *   - Shell, ./configure scripts and dynamic linker probe
 *     existence and clean up via unlink/rmdir as part of every
 *     temp-file dance.
 *
 * Marco M1 has no namei walker yet, so path-based forms return
 * -ENOSYS unless a provider is installed (tmpfs can plug in once
 * its mutation hooks land). Validation of arguments is performed
 * up front so userland sees deterministic Linux-faithful errors
 * for the obviously-bad cases (NULL/empty paths, unknown flags,
 * unrecognised dirfds). */

#include <stdint.h>
#include <stddef.h>

#define LINUX_FS_MUT_AT_FDCWD       (-100)

/* unlinkat flags. Linux defines a single bit; everything else
 * is rejected at validation. */
#define LINUX_AT_REMOVEDIR          0x200

/* renameat2 flag bits (linux/fs.h). */
#define LINUX_RENAME_NOREPLACE      (1u << 0)
#define LINUX_RENAME_EXCHANGE       (1u << 1)
#define LINUX_RENAME_WHITEOUT       (1u << 2)
#define LINUX_RENAME_KNOWN_FLAGS \
    (LINUX_RENAME_NOREPLACE | LINUX_RENAME_EXCHANGE | LINUX_RENAME_WHITEOUT)

struct linux_fs_mut_ops {
    /* All callbacks take canonical paths after the AT_FDCWD
     * shim has been resolved by the dispatcher. Return value
     * mirrors Linux: 0 on success, negative errno on failure.
     * Any callback may be NULL -- the caller falls back to
     * -ENOSYS in that case. */
    int64_t (*mkdir) (const char *path, uint32_t mode);
    int64_t (*rmdir) (const char *path);
    int64_t (*unlink)(const char *path);
    int64_t (*rename)(const char *oldpath, const char *newpath,
                      uint32_t renameat2_flags);
};

void linux_fs_mut_install_ops(const struct linux_fs_mut_ops *ops);
void linux_fs_mut_reset_for_tests(void);

int64_t linux_mkdir   (const char *path, uint32_t mode);
int64_t linux_mkdirat (int dirfd, const char *path, uint32_t mode);
int64_t linux_rmdir   (const char *path);
int64_t linux_unlink  (const char *path);
int64_t linux_unlinkat(int dirfd, const char *path, int flags);
int64_t linux_rename  (const char *oldpath, const char *newpath);
int64_t linux_renameat(int oldfd, const char *old, int newfd, const char *new);
int64_t linux_renameat2(int oldfd, const char *old, int newfd, const char *new,
                        uint32_t flags);

void linux_fs_mut_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_FS_MUT_H */
