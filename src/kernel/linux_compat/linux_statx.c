#include "kernel/linux_compat/linux_statx.h"
#include "kernel/linux_compat/linux_stat.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static void zero_statx(struct linux_statx *s) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < sizeof(*s); i++) p[i] = 0;
}

static int path_is_empty(const char *p) {
    return p == NULL || p[0] == '\0';
}

static void project_stat_to_statx(const struct linux_stat *src, uint32_t mask,
                                  struct linux_statx *buf) {
    zero_statx(buf);
    buf->stx_mask    = mask & LINUX_STATX_SUPPORTED;
    buf->stx_blksize = (uint32_t)src->st_blksize;
    buf->stx_nlink   = (uint32_t)src->st_nlink;
    buf->stx_uid     = src->st_uid;
    buf->stx_gid     = src->st_gid;
    buf->stx_mode    = (uint16_t)src->st_mode;
    buf->stx_ino     = src->st_ino;
    buf->stx_size    = (uint64_t)src->st_size;
    buf->stx_blocks  = (uint64_t)src->st_blocks;
    buf->stx_attributes_mask = 0;
}

int64_t linux_statx(int dirfd, const char *pathname, int flags,
                    uint32_t mask, struct linux_statx *buf) {
    if (!buf) return -LINUX_EFAULT;

    /* Mode 1: AT_EMPTY_PATH with dirfd as the actual fd. This is
     * how userland issues "fstat via statx" without a path. */
    int fstat_mode = (path_is_empty(pathname) &&
                      (flags & LINUX_STATX_AT_EMPTY_PATH)) ||
                     /* Some musl paths pass empty path without
                      * AT_EMPTY_PATH; tolerate by treating empty
                      * path as fstat-on-dirfd anyway when dirfd
                      * is non-negative and valid. */
                     (path_is_empty(pathname) && dirfd >= 0);

    if (fstat_mode) {
        if (dirfd < 0) return -LINUX_EBADF;

        /* Reuse linux_fstat to get synthetic metadata, then
         * project it onto struct statx fields. */
        struct linux_stat src;
        int64_t rc = linux_fstat(dirfd, &src);
        if (rc < 0) return rc;

        project_stat_to_statx(&src, mask, buf);
        return 0;
    }

    if (dirfd != LINUX_STATX_AT_FDCWD && dirfd < 0) {
        return -LINUX_EBADF;
    }
    if (dirfd != LINUX_STATX_AT_FDCWD && dirfd >= 0) {
        return -LINUX_ENOTDIR;
    }

    struct linux_stat src;
    int64_t rc;
    if (flags & LINUX_STATX_AT_SYMLINK_NOFOLLOW) {
        rc = linux_lstat(pathname, &src);
    } else {
        rc = linux_stat(pathname, &src);
    }
    if (rc < 0) return rc;
    project_stat_to_statx(&src, mask, buf);
    return 0;
}

static int64_t sys_statx(const struct linux_syscall_args *a) {
    return linux_statx((int)a->a0,
                       (const char *)(uintptr_t)a->a1,
                       (int)a->a2,
                       (uint32_t)a->a3,
                       (struct linux_statx *)(uintptr_t)a->a4);
}

void linux_statx_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_statx, sys_statx);
}
