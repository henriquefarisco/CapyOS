#ifndef KERNEL_LINUX_COMPAT_LINUX_FS_META_H
#define KERNEL_LINUX_COMPAT_LINUX_FS_META_H

/* Linux ABI filesystem metadata mutation syscalls -- chmod and
 * chown families.
 *
 *   int chmod  (const char *path, mode_t mode);
 *   int fchmod (int fd, mode_t mode);
 *   int fchmodat(int dirfd, const char *path, mode_t mode, int flags);
 *   int chown  (const char *path, uid_t uid, gid_t gid);
 *   int fchown (int fd, uid_t uid, gid_t gid);
 *   int lchown (const char *path, uid_t uid, gid_t gid);
 *   int fchownat(int dirfd, const char *path,
 *                uid_t uid, gid_t gid, int flags);
 *
 * Why this matters for the Firefox port:
 *   - Firefox cache directories are created with `chmod 0700` after
 *     mkdir to lock down access; without chmod the profile loader
 *     fails its sanity check and aborts.
 *   - musl's `mkstemp()` follows up the open with `fchmod(fd, 0600)`
 *     to harden the temp file; on -ENOSYS the helper aborts.
 *   - Bash and ./configure scripts touch chown extensively when
 *     installing files; -ENOSYS short-circuits configure.
 *
 * Marco M1 has a single root user with no real ownership tracking.
 * The kernel state for a path's mode/uid/gid lives in tmpfs once
 * its metadata API lands. For now we provide:
 *
 *   - Path-based forms validate args (NULL/empty path, unknown
 *     fchmodat/fchownat flags) and delegate to a provider injected
 *     via `linux_fs_meta_install_ops`. Without a provider they
 *     return -ENOSYS.
 *   - fd-based forms validate fd >= 0 and delegate to the provider
 *     keyed by fd. Without a provider they return -ENOSYS.
 *   - chown family with uid == (uid_t)-1 means "don't change uid"
 *     in Linux semantics and is forwarded verbatim.
 *
 * AT-family flags supported: AT_SYMLINK_NOFOLLOW (so lchown can be
 * expressed as fchownat with that flag) and AT_EMPTY_PATH. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_FS_META_AT_FDCWD          (-100)
#define LINUX_FS_META_AT_SYMLINK_NOFOLLOW 0x100
#define LINUX_FS_META_AT_EMPTY_PATH     0x1000

/* Combined mask used to validate fchmodat/fchownat flags. */
#define LINUX_FS_META_AT_KNOWN_FLAGS \
    (LINUX_FS_META_AT_SYMLINK_NOFOLLOW | LINUX_FS_META_AT_EMPTY_PATH)

struct linux_fs_meta_ops {
    /* Path-based callbacks. NULL = -ENOSYS fallback. */
    int64_t (*chmod_path)(const char *path, uint32_t mode);
    int64_t (*chown_path)(const char *path, uint32_t uid, uint32_t gid,
                          int follow_symlink);
    /* Fd-based callbacks. NULL = -ENOSYS fallback. */
    int64_t (*chmod_fd)(int fd, uint32_t mode);
    int64_t (*chown_fd)(int fd, uint32_t uid, uint32_t gid);
};

void linux_fs_meta_install_ops(const struct linux_fs_meta_ops *ops);
void linux_fs_meta_reset_for_tests(void);

int64_t linux_chmod   (const char *path, uint32_t mode);
int64_t linux_fchmod  (int fd, uint32_t mode);
int64_t linux_fchmodat(int dirfd, const char *path, uint32_t mode, int flags);
int64_t linux_chown   (const char *path, uint32_t uid, uint32_t gid);
int64_t linux_fchown  (int fd, uint32_t uid, uint32_t gid);
int64_t linux_lchown  (const char *path, uint32_t uid, uint32_t gid);
int64_t linux_fchownat(int dirfd, const char *path,
                       uint32_t uid, uint32_t gid, int flags);

void linux_fs_meta_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_FS_META_H */
