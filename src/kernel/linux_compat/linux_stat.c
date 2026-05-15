#include "kernel/linux_compat/linux_stat.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static void zero_stat(struct linux_stat *s) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < sizeof(*s); i++) p[i] = 0;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void fill_stat(struct linux_stat *out, uint32_t mode, int64_t size) {
    zero_stat(out);
    out->st_uid     = 0;
    out->st_gid     = 0;
    out->st_nlink   = 1;
    out->st_blksize = 4096;
    out->st_mode    = mode;
    out->st_size    = size;
}

static int path_is_dir(const char *path) {
    return str_eq(path, "/") ||
           str_eq(path, "/dev") ||
           str_eq(path, "/dev/") ||
           str_eq(path, "/dev/shm") ||
           str_eq(path, "/dev/shm/") ||
           str_eq(path, "/proc") ||
           str_eq(path, "/proc/") ||
           str_eq(path, "/proc/self") ||
           str_eq(path, "/proc/self/") ||
           str_eq(path, "/tmp") ||
           str_eq(path, "/tmp/");
}

static int path_is_dev_char(const char *path) {
    return str_eq(path, "/dev/null") ||
           str_eq(path, "/dev/zero") ||
           str_eq(path, "/dev/full") ||
           str_eq(path, "/dev/random") ||
           str_eq(path, "/dev/urandom");
}

static int path_is_proc_regular(const char *path) {
    return str_eq(path, "/proc/cpuinfo") ||
           str_eq(path, "/proc/meminfo") ||
           str_eq(path, "/proc/version") ||
           str_eq(path, "/proc/uptime") ||
           str_eq(path, "/proc/loadavg") ||
           str_eq(path, "/proc/self/maps") ||
           str_eq(path, "/proc/self/exe") ||
           str_eq(path, "/proc/self/cmdline") ||
           str_eq(path, "/proc/self/status");
}

int linux_stat_path_is_known(const char *path) {
    if (!path) return 0;
    return path_is_dir(path) ||
           path_is_dev_char(path) ||
           path_is_proc_regular(path);
}

static int64_t stat_known_path(const char *path, struct linux_stat *out,
                               int follow_symlinks) {
    if (path_is_dir(path)) {
        fill_stat(out, LINUX_S_IFDIR | LINUX_STAT_DIR_PERMS, 0);
        return 0;
    }
    if (path_is_dev_char(path)) {
        fill_stat(out, LINUX_S_IFCHR | LINUX_STAT_DEFAULT_PERMS, 0);
        return 0;
    }
    if (!follow_symlinks && str_eq(path, "/proc/self/exe")) {
        fill_stat(out, LINUX_S_IFLNK | LINUX_STAT_DEFAULT_PERMS, 0);
        return 0;
    }
    if (path_is_proc_regular(path)) {
        fill_stat(out, LINUX_S_IFREG | LINUX_STAT_DEFAULT_PERMS, 0);
        return 0;
    }
    return -LINUX_ENOSYS;
}

int64_t linux_fstat(int fd, struct linux_stat *out) {
    if (fd < 0) return -LINUX_EBADF;
    if (!out)   return -LINUX_EFAULT;

    zero_stat(out);

    /* Universal sane defaults. */
    out->st_uid     = 0;
    out->st_gid     = 0;
    out->st_nlink   = 1;
    out->st_blksize = 4096;

    /* fds 0/1/2 are reported as character devices. This matches
     * what Linux does when stdin/out/err are connected to a
     * terminal -- and also what musl tolerates when they are
     * redirected (because we already say -ENOTTY in ioctl, the
     * stdio layer picks block-buffered regardless of S_IFCHR). */
    if (fd == 0 || fd == 1 || fd == 2) {
        out->st_mode = LINUX_S_IFCHR | LINUX_STAT_DEFAULT_PERMS;
        out->st_size = 0;
        out->st_rdev = 0;
        return 0;
    }

    /* Everything else: synthesise as a regular file with unknown
     * size. Userland that needs the real size falls back to
     * read-until-EOF or lseek(SEEK_END). */
    out->st_mode = LINUX_S_IFREG | LINUX_STAT_DEFAULT_PERMS;
    out->st_size = 0;
    return 0;
}

int64_t linux_stat(const char *path, struct linux_stat *out) {
    if (!path) return -LINUX_EFAULT;
    if (!out)  return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    return stat_known_path(path, out, 1);
}

int64_t linux_lstat(const char *path, struct linux_stat *out) {
    if (!path) return -LINUX_EFAULT;
    if (!out)  return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    return stat_known_path(path, out, 0);
}

/* ---- Syscall adapters ---- */

static int64_t sys_fstat(const struct linux_syscall_args *a) {
    return linux_fstat((int)a->a0,
                       (struct linux_stat *)(uintptr_t)a->a1);
}

static int64_t sys_stat(const struct linux_syscall_args *a) {
    return linux_stat((const char *)(uintptr_t)a->a0,
                      (struct linux_stat *)(uintptr_t)a->a1);
}

static int64_t sys_lstat(const struct linux_syscall_args *a) {
    return linux_lstat((const char *)(uintptr_t)a->a0,
                       (struct linux_stat *)(uintptr_t)a->a1);
}

void linux_stat_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_fstat, sys_fstat);
    (void)linux_syscall_register(LINUX_NR_stat,  sys_stat);
    (void)linux_syscall_register(LINUX_NR_lstat, sys_lstat);
}
