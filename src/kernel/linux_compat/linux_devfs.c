#include "kernel/linux_compat/linux_devfs.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_random.h"

#include <stdint.h>
#include <stddef.h>

/* Local string-equality without pulling string.h. The kernel build
 * does not link <string.h> in freestanding mode and the host build
 * lifts our own implementation. */
static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

enum linux_devfs_id linux_devfs_lookup(const char *path) {
    if (!path) return LINUX_DEV_NONE;
    if (str_eq(path, "/dev/null"))     return LINUX_DEV_NULL;
    if (str_eq(path, "/dev/zero"))     return LINUX_DEV_ZERO;
    if (str_eq(path, "/dev/full"))     return LINUX_DEV_FULL;
    if (str_eq(path, "/dev/urandom"))  return LINUX_DEV_URANDOM;
    if (str_eq(path, "/dev/random"))   return LINUX_DEV_RANDOM;
    return LINUX_DEV_NONE;
}

int64_t linux_devfs_read(enum linux_devfs_id id, void *buf, size_t len) {
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;

    uint8_t *out = (uint8_t *)buf;

    switch (id) {
        case LINUX_DEV_NULL:
            /* /dev/null reads always EOF. */
            return 0;

        case LINUX_DEV_ZERO:
        case LINUX_DEV_FULL:
            /* /dev/zero and /dev/full read pattern is identical
             * (fill with 0x00). They differ only on write. */
            for (size_t i = 0; i < len; i++) out[i] = 0x00;
            return (int64_t)len;

        case LINUX_DEV_URANDOM:
        case LINUX_DEV_RANDOM:
            /* Delegate to the same getrandom path so the CSPRNG
             * source is the single source of truth. flags=0 means
             * default semantics (blocking is a no-op since the pool
             * is always ready). */
            return linux_getrandom(out, len, 0);

        case LINUX_DEV_NONE:
        default:
            return -LINUX_ENODEV;
    }
}

int64_t linux_devfs_write(enum linux_devfs_id id, const void *buf, size_t len) {
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    (void)buf;  /* contents discarded */

    switch (id) {
        case LINUX_DEV_NULL:
        case LINUX_DEV_ZERO:
        case LINUX_DEV_URANDOM:
        case LINUX_DEV_RANDOM:
            /* Sink semantics: ack the bytes and forget. */
            return (int64_t)len;

        case LINUX_DEV_FULL:
            /* /dev/full always reports the disk is full on write. */
            return -LINUX_ENOSPC;

        case LINUX_DEV_NONE:
        default:
            return -LINUX_ENODEV;
    }
}

/* ---- fd API used by the linux_vfs router ----
 *
 * Each open() allocates a slot in this small table. The slot
 * stores the resolved devfs id; the returned fd is
 * LINUX_DEVFS_FD_BASE + slot. close() releases the slot.
 *
 * read/write/lseek wrappers resolve fd -> slot -> id and
 * delegate to the existing id-based functions so semantics
 * stay in one place. */

struct devfs_slot {
    uint8_t in_use;
    uint8_t id;  /* enum linux_devfs_id; uint8_t to keep slot small */
};

static struct devfs_slot g_dev_fds[LINUX_DEVFS_MAX_INSTANCES];

/* Mask of open(2) flags we accept for /dev nodes. We honour the
 * access-mode bits but ignore the file-creation bits because
 * /dev nodes are not regular files. NONBLOCK is silently
 * accepted because reads on /dev/<x> never block in this shim
 * (CSPRNG always has bytes; null/zero/full are synchronous). */
#define DEVFS_OPEN_KNOWN_FLAGS \
    (0x0003u  /* O_ACCMODE: RDONLY|WRONLY|RDWR */ |  \
     0x0040u  /* O_CREAT     -- ignored */ |          \
     0x0080u  /* O_EXCL      -- ignored */ |          \
     0x0200u  /* O_TRUNC     -- ignored */ |          \
     0x0800u  /* O_NONBLOCK  -- ignored, never blocks */ | \
     0x80000u /* O_CLOEXEC   -- ignored */)

static int dev_fd_to_slot(int fd) {
    int slot = fd - LINUX_DEVFS_FD_BASE;
    if (slot < 0 || slot >= LINUX_DEVFS_MAX_INSTANCES) return -1;
    if (!g_dev_fds[slot].in_use) return -1;
    return slot;
}

int64_t linux_devfs_open(const char *path, uint32_t flags) {
    if (!path) return -LINUX_EFAULT;
    if (flags & ~DEVFS_OPEN_KNOWN_FLAGS) return -LINUX_EINVAL;

    /* Linux returns EINVAL when O_ACCMODE is the all-bits pattern
     * (3); same check we apply in linux_vfs_open(). */
    if ((flags & 0x3u) == 0x3u) return -LINUX_EINVAL;

    enum linux_devfs_id id = linux_devfs_lookup(path);
    if (id == LINUX_DEV_NONE) return -LINUX_ENOENT;

    for (int slot = 0; slot < LINUX_DEVFS_MAX_INSTANCES; slot++) {
        if (!g_dev_fds[slot].in_use) {
            g_dev_fds[slot].in_use = 1;
            g_dev_fds[slot].id = (uint8_t)id;
            return LINUX_DEVFS_FD_BASE + slot;
        }
    }
    return -LINUX_EMFILE;
}

int64_t linux_devfs_close(int fd) {
    int slot = dev_fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    g_dev_fds[slot].in_use = 0;
    g_dev_fds[slot].id = 0;
    return 0;
}

int64_t linux_devfs_read_fd(int fd, void *buf, size_t len) {
    int slot = dev_fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    return linux_devfs_read((enum linux_devfs_id)g_dev_fds[slot].id,
                            buf, len);
}

int64_t linux_devfs_write_fd(int fd, const void *buf, size_t len) {
    int slot = dev_fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    return linux_devfs_write((enum linux_devfs_id)g_dev_fds[slot].id,
                             buf, len);
}

int64_t linux_devfs_lseek_fd(int fd, int64_t offset, int whence) {
    (void)offset;
    int slot = dev_fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    /* Validate whence for parity with linux_vfs_lseek so the
     * router can safely forward without re-checking. */
    if (whence != 0 && whence != 1 && whence != 2 &&
        whence != 3 && whence != 4) return -LINUX_EINVAL;
    /* Character devices in Linux 6.x always report position 0. */
    return 0;
}

void linux_devfs_reset_for_tests(void) {
    for (int i = 0; i < LINUX_DEVFS_MAX_INSTANCES; i++) {
        g_dev_fds[i].in_use = 0;
        g_dev_fds[i].id = 0;
    }
}
