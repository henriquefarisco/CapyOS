#ifndef KERNEL_LINUX_COMPAT_LINUX_LINK_H
#define KERNEL_LINUX_COMPAT_LINUX_LINK_H

/* Linux ABI hard- and soft-link syscalls.
 *
 *   int link    (const char *oldpath, const char *newpath);
 *   int linkat  (int olddirfd, const char *oldpath,
 *                int newdirfd, const char *newpath, int flags);
 *   int symlink (const char *target, const char *linkpath);
 *   int symlinkat(const char *target, int newdirfd,
 *                 const char *linkpath);
 *
 * Why this matters for the Firefox port:
 *   - Firefox's atomic-update path uses link(tmpfile, finalfile)
 *     followed by unlink(tmpfile) so a crash mid-write never
 *     leaves a half-written cache shard. Without link, the
 *     fallback is rename() which we already wired in sessao 28,
 *     but Firefox also uses link for content-deduplicated
 *     download targets.
 *   - musl `realpath()` walks symlink chains, so once Firefox
 *     enables remote profile sync it issues lots of symlinkat
 *     calls inside the cache.
 *
 * Marco M1 has no namei walker yet, so all forms return -ENOSYS
 * after up-front argument validation. Provider injection lets
 * tmpfs (or a future real fs) plug in the real ops. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_LINK_AT_FDCWD          (-100)
#define LINUX_LINK_AT_SYMLINK_FOLLOW 0x400
#define LINUX_LINK_AT_EMPTY_PATH     0x1000

#define LINUX_LINK_AT_KNOWN_FLAGS \
    (LINUX_LINK_AT_SYMLINK_FOLLOW | LINUX_LINK_AT_EMPTY_PATH)

struct linux_link_ops {
    int64_t (*hard_link)(const char *oldpath, const char *newpath,
                         int follow_symlink);
    int64_t (*sym_link)(const char *target, const char *linkpath);
};

void linux_link_install_ops(const struct linux_link_ops *ops);
void linux_link_reset_for_tests(void);

int64_t linux_link    (const char *oldpath, const char *newpath);
int64_t linux_linkat  (int olddirfd, const char *oldpath,
                       int newdirfd, const char *newpath, int flags);
int64_t linux_symlink (const char *target, const char *linkpath);
int64_t linux_symlinkat(const char *target, int newdirfd,
                        const char *linkpath);

void linux_link_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_LINK_H */
