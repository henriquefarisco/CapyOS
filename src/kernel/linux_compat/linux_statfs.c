#include "kernel/linux_compat/linux_statfs.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_statfs_providers g_providers;
static int                           g_providers_installed;

void linux_statfs_install_providers(const struct linux_statfs_providers *p) {
    if (!p) {
        g_providers = (struct linux_statfs_providers){0};
        g_providers_installed = 0;
        return;
    }
    g_providers = *p;
    g_providers_installed = 1;
}

void linux_statfs_reset_for_tests(void) {
    g_providers = (struct linux_statfs_providers){0};
    g_providers_installed = 0;
}

static void fill_buf(struct linux_statfs *buf) {
    /* Zero the whole struct first; this also covers f_spare and
     * the half of f_fsid we don't write below. */
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < sizeof(*buf); i++) p[i] = 0;

    uint64_t blocks =
        (g_providers_installed && g_providers.total_blocks) ?
        g_providers.total_blocks() :
        16384u;  /* 64 MiB / 4 KiB default */
    uint64_t files =
        (g_providers_installed && g_providers.total_files) ?
        g_providers.total_files() :
        1024u;   /* Marco M1 tmpfs handle table cap */

    buf->f_type    = (int64_t)LINUX_STATFS_TMPFS_MAGIC;
    buf->f_bsize   = (int64_t)LINUX_STATFS_DEFAULT_BSIZE;
    buf->f_blocks  = blocks;
    buf->f_bfree   = blocks;
    buf->f_bavail  = blocks;
    buf->f_files   = files;
    buf->f_ffree   = files;
    buf->f_namelen = (int64_t)LINUX_STATFS_DEFAULT_NAMELEN;
    buf->f_frsize  = (int64_t)LINUX_STATFS_DEFAULT_BSIZE;
    /* f_flags, f_fsid, f_spare left zero. */
}

int64_t linux_statfs(const char *path, struct linux_statfs *buf) {
    if (!path) return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    if (!buf)  return -LINUX_EFAULT;
    fill_buf(buf);
    return 0;
}

int64_t linux_fstatfs(int fd, struct linux_statfs *buf) {
    if (fd < 0) return -LINUX_EBADF;
    if (!buf)   return -LINUX_EFAULT;
    fill_buf(buf);
    return 0;
}

static int64_t sys_statfs(const struct linux_syscall_args *a) {
    return linux_statfs((const char *)(uintptr_t)a->a0,
                        (struct linux_statfs *)(uintptr_t)a->a1);
}
static int64_t sys_fstatfs(const struct linux_syscall_args *a) {
    return linux_fstatfs((int)a->a0,
                         (struct linux_statfs *)(uintptr_t)a->a1);
}

void linux_statfs_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_statfs,  sys_statfs);
    (void)linux_syscall_register(LINUX_NR_fstatfs, sys_fstatfs);
}
