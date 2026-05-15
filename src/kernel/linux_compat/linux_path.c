#include "kernel/linux_compat/linux_path.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_path_providers g_providers;

void linux_path_install(const struct linux_path_providers *p) {
    if (p) g_providers = *p;
    else g_providers = (struct linux_path_providers){0};
}

void linux_path_reset_for_tests(void) {
    g_providers = (struct linux_path_providers){0};
}

static int path_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

int64_t linux_getcwd(char *buf, size_t size) {
    if (!buf)        return -LINUX_EFAULT;
    if (size == 0)   return -LINUX_EINVAL;
    /* "/" + '\0' = 2 bytes. */
    if (size < 2)    return -LINUX_ERANGE;
    buf[0] = '/';
    buf[1] = '\0';
    return 2;
}

int64_t linux_readlink(const char *path, char *buf, size_t bufsize) {
    if (!path || !buf) return -LINUX_EFAULT;
    if (bufsize == 0)  return -LINUX_EINVAL;

    if (path_eq(path, "/proc/self/exe")) {
        if (!g_providers.resolve_proc_self_exe) return -LINUX_ENOSYS;
        return g_providers.resolve_proc_self_exe(buf, bufsize);
    }

    /* Linux: -EINVAL for "not a symlink"; we apply this to every
     * other path. /etc/hosts, /dev/stdin etc. tolerate this:
     * userland that needs the canonical name falls back to
     * realpath which uses lstat, and lstat returning -ENOSYS
     * lets it terminate gracefully. */
    return -LINUX_EINVAL;
}

int64_t linux_readlinkat(int dirfd, const char *path, char *buf, size_t bufsize) {
    if (dirfd == LINUX_PATH_AT_FDCWD) {
        return linux_readlink(path, buf, bufsize);
    }
    /* Real dirfd support requires a directory fd table, which
     * doesn't exist yet. Linux returns -ENOTDIR for fds that
     * don't reference directories; that maps cleanly here. */
    return -LINUX_ENOTDIR;
}

/* ---- Syscall adapters ---- */

static int64_t sys_getcwd(const struct linux_syscall_args *a) {
    return linux_getcwd((char *)(uintptr_t)a->a0, (size_t)a->a1);
}

static int64_t sys_readlink(const struct linux_syscall_args *a) {
    return linux_readlink((const char *)(uintptr_t)a->a0,
                          (char *)(uintptr_t)a->a1,
                          (size_t)a->a2);
}

static int64_t sys_readlinkat(const struct linux_syscall_args *a) {
    return linux_readlinkat((int)a->a0,
                            (const char *)(uintptr_t)a->a1,
                            (char *)(uintptr_t)a->a2,
                            (size_t)a->a3);
}

void linux_path_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_getcwd,     sys_getcwd);
    (void)linux_syscall_register(LINUX_NR_readlink,   sys_readlink);
    (void)linux_syscall_register(LINUX_NR_readlinkat, sys_readlinkat);
}
