#include "kernel/linux_compat/linux_vfs_router.h"
#include "kernel/linux_compat/linux_vfs.h"
#include "kernel/linux_compat/linux_devfs.h"
#include "kernel/linux_compat/linux_shm.h"
#include "kernel/linux_compat/linux_procfs.h"
#include "kernel/linux_compat/linux_tmpfs.h"
#include "kernel/linux_compat/linux_eventfd.h"
#include "kernel/linux_compat/linux_memfd.h"
#include "kernel/linux_compat/linux_modern_misc.h"
#include "kernel/linux_compat/linux_inotify.h"
#include "kernel/linux_compat/linux_epoll.h"
#include "kernel/linux_compat/linux_fanotify.h"
#include "kernel/linux_compat/linux_jit_aux.h"
#include "kernel/linux_compat/linux_landlock.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

/* Local prefix-match without pulling string.h. Returns 1 iff
 * `s` begins with `prefix` (case-sensitive, byte-for-byte). */
static int starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

/* Pointer to the suffix that follows `prefix` in `s`, when
 * `s` starts with it. Used by router_open to extract
 * "<name>" from "/dev/shm/<name>". Returns NULL if mismatch. */
static const char *strip_prefix(const char *s, const char *prefix) {
    if (!starts_with(s, prefix)) return NULL;
    while (*prefix) { s++; prefix++; }
    return s;
}

/* fd range tests. Each backend owns a contiguous fd range. */
static int fd_in_devfs(int fd) {
    return fd >= LINUX_DEVFS_FD_BASE &&
           fd <  LINUX_DEVFS_FD_BASE + LINUX_DEVFS_MAX_INSTANCES;
}
static int fd_in_shm(int fd) {
    return fd >= LINUX_SHM_FD_BASE &&
           fd <  LINUX_SHM_FD_BASE + LINUX_SHM_MAX_OBJECTS;
}
static int fd_in_procfs(int fd) {
    return fd >= LINUX_PROCFS_FD_BASE &&
           fd <  LINUX_PROCFS_FD_BASE + LINUX_PROCFS_MAX_INSTANCES;
}
static int fd_in_tmpfs(int fd) {
    return fd >= LINUX_TMPFS_FD_BASE &&
           fd <  LINUX_TMPFS_FD_BASE + LINUX_TMPFS_MAX_HANDLES;
}

/* ---- ops callbacks ---- */

static int router_open(const char *path, uint32_t flags, uint32_t mode) {
    /* `/dev/shm/` must be checked BEFORE `/dev/` because the
     * shm prefix is a strict superset; the wrong order would
     * route shm paths to devfs and surface ENOENT. */
    const char *shm_name = strip_prefix(path, "/dev/shm/");
    if (shm_name) {
        if (*shm_name == '\0') return -LINUX_EINVAL;  /* "/dev/shm/" alone */
        int64_t r = linux_shm_open(shm_name, flags, mode);
        return r < 0 ? (int)r : (int)r;
    }

    if (starts_with(path, "/dev/")) {
        int64_t r = linux_devfs_open(path, flags);
        (void)mode;  /* /dev nodes ignore mode */
        return r < 0 ? (int)r : (int)r;
    }

    if (starts_with(path, "/proc/")) {
        int64_t r = linux_procfs_open(path, flags);
        (void)mode;  /* /proc nodes ignore mode */
        return r < 0 ? (int)r : (int)r;
    }

    if (starts_with(path, "/tmp/")) {
        int64_t r = linux_tmpfs_open(path, flags, mode);
        return r < 0 ? (int)r : (int)r;
    }

    return -LINUX_ENOENT;
}

static int router_close(int fd) {
    if (fd_in_devfs(fd))  return (int)linux_devfs_close(fd);
    if (fd_in_shm(fd))    return (int)linux_shm_close(fd);
    if (fd_in_procfs(fd)) return (int)linux_procfs_close(fd);
    if (fd_in_tmpfs(fd))  return (int)linux_tmpfs_close(fd);
    if (linux_eventfd_family_close(fd) == 0) return 0;
    if (linux_memfd_secret_close(fd) == 0) return 0;
    if (linux_memfd_family_close(fd) == 0) return 0;
    if (linux_inotify_close(fd) == 0) return 0;
    if (linux_epoll_close(fd) == 0) return 0;
    if (linux_fanotify_close(fd) == 0) return 0;
    if (linux_userfaultfd_close(fd) == 0) return 0;
    if (linux_landlock_close(fd) == 0) return 0;
    return -LINUX_EBADF;
}

static int64_t router_read(int fd, void *buf, size_t len) {
    if (fd_in_devfs(fd))  return linux_devfs_read_fd(fd, buf, len);
    if (fd_in_procfs(fd)) return linux_procfs_read_fd(fd, buf, len);
    if (fd_in_tmpfs(fd))  return linux_tmpfs_read_fd(fd, buf, len);
    int64_t ev = linux_eventfd_family_read(fd, buf, len);
    if (ev != -LINUX_EBADF) return ev;
    int64_t mm = linux_memfd_secret_read(fd, buf, len);
    if (mm != -LINUX_EBADF) return mm;
    int64_t mf = linux_memfd_family_read(fd, buf, len);
    if (mf != -LINUX_EBADF) return mf;
    int64_t in = linux_inotify_read(fd, buf, len);
    if (in != -LINUX_EBADF) return in;
    int64_t ep = linux_epoll_read(fd, buf, len);
    if (ep != -LINUX_EBADF) return ep;
    int64_t fan = linux_fanotify_read(fd, buf, len);
    if (fan != -LINUX_EBADF) return fan;
    int64_t uffd = linux_userfaultfd_read(fd, buf, len);
    if (uffd != -LINUX_EBADF) return uffd;
    int64_t ll = linux_landlock_read(fd, buf, len);
    if (ll != -LINUX_EBADF) return ll;
    if (fd_in_shm(fd))    {
        /* shm objects don't support stream read in this milestone;
         * userland must mmap the fd. mmap support for shm is gated
         * on backing-pages infrastructure (a future expansion of
         * tmpfs would back shm too). */
        (void)buf; (void)len;
        return -LINUX_ENOSYS;
    }
    return -LINUX_EBADF;
}

static int64_t router_write(int fd, const void *buf, size_t len) {
    if (fd_in_devfs(fd))  return linux_devfs_write_fd(fd, buf, len);
    if (fd_in_procfs(fd)) return linux_procfs_write_fd(fd, buf, len);
    if (fd_in_tmpfs(fd))  return linux_tmpfs_write_fd(fd, buf, len);
    int64_t ev = linux_eventfd_family_write(fd, buf, len);
    if (ev != -LINUX_EBADF) return ev;
    int64_t mm = linux_memfd_secret_write(fd, buf, len);
    if (mm != -LINUX_EBADF) return mm;
    int64_t mf = linux_memfd_family_write(fd, buf, len);
    if (mf != -LINUX_EBADF) return mf;
    int64_t in = linux_inotify_write(fd, buf, len);
    if (in != -LINUX_EBADF) return in;
    int64_t ep = linux_epoll_write(fd, buf, len);
    if (ep != -LINUX_EBADF) return ep;
    int64_t fan = linux_fanotify_write(fd, buf, len);
    if (fan != -LINUX_EBADF) return fan;
    int64_t uffd = linux_userfaultfd_write(fd, buf, len);
    if (uffd != -LINUX_EBADF) return uffd;
    int64_t ll = linux_landlock_write(fd, buf, len);
    if (ll != -LINUX_EBADF) return ll;
    if (fd_in_shm(fd))    {
        (void)buf; (void)len;
        return -LINUX_ENOSYS;  /* see router_read for rationale */
    }
    return -LINUX_EBADF;
}

static int64_t router_lseek(int fd, int64_t offset, int whence) {
    if (fd_in_devfs(fd))  return linux_devfs_lseek_fd(fd, offset, whence);
    if (fd_in_procfs(fd)) return linux_procfs_lseek_fd(fd, offset, whence);
    if (fd_in_tmpfs(fd))  return linux_tmpfs_lseek_fd(fd, offset, whence);
    int64_t ev = linux_eventfd_family_lseek(fd, offset, whence);
    if (ev != -LINUX_EBADF) return ev;
    int64_t mm = linux_memfd_secret_lseek(fd, offset, whence);
    if (mm != -LINUX_EBADF) return mm;
    int64_t mf = linux_memfd_family_lseek(fd, offset, whence);
    if (mf != -LINUX_EBADF) return mf;
    int64_t in = linux_inotify_lseek(fd, offset, whence);
    if (in != -LINUX_EBADF) return in;
    int64_t ep = linux_epoll_lseek(fd, offset, whence);
    if (ep != -LINUX_EBADF) return ep;
    int64_t fan = linux_fanotify_lseek(fd, offset, whence);
    if (fan != -LINUX_EBADF) return fan;
    int64_t uffd = linux_userfaultfd_lseek(fd, offset, whence);
    if (uffd != -LINUX_EBADF) return uffd;
    int64_t ll = linux_landlock_lseek(fd, offset, whence);
    if (ll != -LINUX_EBADF) return ll;
    if (fd_in_shm(fd))    {
        /* shm: lseek is meaningful (object has a size) but we
         * have no per-fd file position table. Treat as "always
         * 0" until shm gets a tmpfs-style backing layer. */
        (void)offset; (void)whence;
        return 0;
    }
    return -LINUX_EBADF;
}

void linux_vfs_router_install(void) {
    static const struct linux_vfs_ops ops = {
        .open  = router_open,
        .close = router_close,
        .read  = router_read,
        .write = router_write,
        .lseek = router_lseek,
    };
    linux_vfs_install_ops(&ops);
}
