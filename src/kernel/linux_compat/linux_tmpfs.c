#include "kernel/linux_compat/linux_tmpfs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

/* Local string helpers (no <string.h> in freestanding). */
static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static size_t str_len_capped(const char *s, size_t cap) {
    if (!s) return 0;
    size_t i = 0;
    while (i < cap && s[i]) i++;
    return i;
}

/* Open(2) flags we care about. Mirrors linux_vfs_router's
 * accepted set so flags survive the round trip cleanly. */
#define TMPFS_O_RDONLY    0x0000u
#define TMPFS_O_WRONLY    0x0001u
#define TMPFS_O_RDWR      0x0002u
#define TMPFS_O_ACCMODE   0x0003u
#define TMPFS_O_CREAT     0x0040u
#define TMPFS_O_EXCL      0x0080u
#define TMPFS_O_TRUNC     0x0200u
#define TMPFS_O_APPEND    0x0400u
#define TMPFS_O_NONBLOCK  0x0800u
#define TMPFS_O_CLOEXEC   0x80000u

#define TMPFS_KNOWN_FLAGS \
    (TMPFS_O_ACCMODE | TMPFS_O_CREAT | TMPFS_O_EXCL | \
     TMPFS_O_TRUNC | TMPFS_O_APPEND | TMPFS_O_NONBLOCK | \
     TMPFS_O_CLOEXEC)

#define TMPFS_PREFIX     "/tmp/"
#define TMPFS_PREFIX_LEN 5u

/* ---- Storage ---- */

struct tmpfs_file {
    uint8_t  in_use;
    uint8_t  unlinked;       /* name removed; reaped when refcount hits 0 */
    uint32_t refcount;       /* number of handles open against this file */
    size_t   size;           /* current size in bytes (<= MAX_FILE_SIZE) */
    char     name[LINUX_TMPFS_MAX_NAME];
    uint8_t  content[LINUX_TMPFS_MAX_FILE_SIZE];
};

struct tmpfs_handle {
    uint8_t  in_use;
    uint16_t file_idx;
    size_t   pos;
    uint32_t flags;          /* O_APPEND/O_RDONLY/etc preserved */
};

static struct tmpfs_file   g_files  [LINUX_TMPFS_MAX_FILES];
static struct tmpfs_handle g_handles[LINUX_TMPFS_MAX_HANDLES];

void linux_tmpfs_reset_for_tests(void) {
    for (int i = 0; i < LINUX_TMPFS_MAX_FILES; i++) {
        g_files[i].in_use = 0;
        g_files[i].unlinked = 0;
        g_files[i].refcount = 0;
        g_files[i].size = 0;
        g_files[i].name[0] = '\0';
        /* Keep content garbage; size=0 means it's unreadable. */
    }
    for (int i = 0; i < LINUX_TMPFS_MAX_HANDLES; i++) {
        g_handles[i].in_use = 0;
        g_handles[i].file_idx = 0;
        g_handles[i].pos = 0;
        g_handles[i].flags = 0;
    }
}

/* ---- Helpers ---- */

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

/* Validate `path` and return the suffix after `/tmp/`. NULL if the
 * path is not under /tmp/, has no name, or contains a sub-slash. */
static const char *validate_path(const char *path) {
    if (!path || !starts_with(path, TMPFS_PREFIX)) return NULL;
    const char *name = path + TMPFS_PREFIX_LEN;
    if (*name == '\0') return NULL;  /* "/tmp/" alone */
    /* No subdirectories: reject if there is a '/' inside the name. */
    for (const char *p = name; *p; p++) {
        if (*p == '/') return NULL;
    }
    return name;
}

static int find_file_by_name(const char *name) {
    for (int i = 0; i < LINUX_TMPFS_MAX_FILES; i++) {
        if (g_files[i].in_use && !g_files[i].unlinked &&
            str_eq(g_files[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int alloc_file_slot(void) {
    for (int i = 0; i < LINUX_TMPFS_MAX_FILES; i++) {
        if (!g_files[i].in_use) return i;
    }
    return -1;
}

static int alloc_handle_slot(void) {
    for (int i = 0; i < LINUX_TMPFS_MAX_HANDLES; i++) {
        if (!g_handles[i].in_use) return i;
    }
    return -1;
}

static int handle_from_fd(int fd) {
    int slot = fd - LINUX_TMPFS_FD_BASE;
    if (slot < 0 || slot >= LINUX_TMPFS_MAX_HANDLES) return -1;
    if (!g_handles[slot].in_use) return -1;
    return slot;
}

/* If a file is unlinked AND its refcount has hit zero, free its
 * slot so it can be re-used. */
static void maybe_reap_file(int file_idx) {
    if (file_idx < 0 || file_idx >= LINUX_TMPFS_MAX_FILES) return;
    struct tmpfs_file *f = &g_files[file_idx];
    if (f->unlinked && f->refcount == 0) {
        f->in_use = 0;
        f->unlinked = 0;
        f->size = 0;
        f->name[0] = '\0';
    }
}

/* ---- Public surface ---- */

int64_t linux_tmpfs_open(const char *path, uint32_t flags, uint32_t mode) {
    (void)mode;
    if (!path) return -LINUX_EFAULT;
    if (flags & ~TMPFS_KNOWN_FLAGS) return -LINUX_EINVAL;
    if ((flags & TMPFS_O_ACCMODE) == TMPFS_O_ACCMODE) return -LINUX_EINVAL;
    /* O_EXCL without O_CREAT is invalid (Linux behaviour). */
    if ((flags & TMPFS_O_EXCL) && !(flags & TMPFS_O_CREAT))
        return -LINUX_EINVAL;

    const char *name = validate_path(path);
    if (!name) return -LINUX_ENOENT;
    if (str_len_capped(name, LINUX_TMPFS_MAX_NAME) >= LINUX_TMPFS_MAX_NAME)
        return -LINUX_ENAMETOOLONG;

    int file_idx = find_file_by_name(name);
    if (file_idx < 0) {
        if (!(flags & TMPFS_O_CREAT)) return -LINUX_ENOENT;
        file_idx = alloc_file_slot();
        if (file_idx < 0) return -LINUX_ENOSPC;
        struct tmpfs_file *f = &g_files[file_idx];
        f->in_use = 1;
        f->unlinked = 0;
        f->refcount = 0;
        f->size = 0;
        size_t i = 0;
        while (name[i] && i < LINUX_TMPFS_MAX_NAME - 1) {
            f->name[i] = name[i]; i++;
        }
        f->name[i] = '\0';
    } else {
        if ((flags & TMPFS_O_CREAT) && (flags & TMPFS_O_EXCL))
            return -LINUX_EEXIST;
        if (flags & TMPFS_O_TRUNC) g_files[file_idx].size = 0;
    }

    int hidx = alloc_handle_slot();
    if (hidx < 0) {
        /* Roll back the file allocation only if we just created it. */
        if (g_files[file_idx].refcount == 0 &&
            (flags & TMPFS_O_CREAT) &&
            find_file_by_name(name) == file_idx) {
            /* The file we just created has no handle yet; safe to
             * release the slot to keep things tidy. */
            g_files[file_idx].in_use = 0;
            g_files[file_idx].name[0] = '\0';
        }
        return -LINUX_EMFILE;
    }
    g_files[file_idx].refcount++;

    g_handles[hidx].in_use = 1;
    g_handles[hidx].file_idx = (uint16_t)file_idx;
    g_handles[hidx].pos = (flags & TMPFS_O_APPEND)
                          ? g_files[file_idx].size : 0;
    g_handles[hidx].flags = flags;

    return LINUX_TMPFS_FD_BASE + hidx;
}

int64_t linux_tmpfs_close(int fd) {
    int hidx = handle_from_fd(fd);
    if (hidx < 0) return -LINUX_EBADF;
    int file_idx = g_handles[hidx].file_idx;
    g_handles[hidx].in_use = 0;
    g_handles[hidx].file_idx = 0;
    g_handles[hidx].pos = 0;
    g_handles[hidx].flags = 0;
    if (g_files[file_idx].refcount > 0) g_files[file_idx].refcount--;
    maybe_reap_file(file_idx);
    return 0;
}

int64_t linux_tmpfs_read_fd(int fd, void *buf, size_t len) {
    int hidx = handle_from_fd(fd);
    if (hidx < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;

    struct tmpfs_handle *h = &g_handles[hidx];
    /* Reject reads on files opened write-only. */
    if ((h->flags & TMPFS_O_ACCMODE) == TMPFS_O_WRONLY) return -LINUX_EBADF;

    struct tmpfs_file *f = &g_files[h->file_idx];
    if (h->pos >= f->size) return 0;
    size_t avail = f->size - h->pos;
    size_t n = avail < len ? avail : len;
    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < n; i++) out[i] = f->content[h->pos + i];
    h->pos += n;
    return (int64_t)n;
}

int64_t linux_tmpfs_write_fd(int fd, const void *buf, size_t len) {
    int hidx = handle_from_fd(fd);
    if (hidx < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;

    struct tmpfs_handle *h = &g_handles[hidx];
    if ((h->flags & TMPFS_O_ACCMODE) == TMPFS_O_RDONLY) return -LINUX_EBADF;
    /* O_APPEND: writes always go to end of file. */
    if (h->flags & TMPFS_O_APPEND) h->pos = g_files[h->file_idx].size;

    struct tmpfs_file *f = &g_files[h->file_idx];
    if (h->pos >= LINUX_TMPFS_MAX_FILE_SIZE) return -LINUX_ENOSPC;
    size_t room = LINUX_TMPFS_MAX_FILE_SIZE - h->pos;
    size_t n = len < room ? len : room;
    const uint8_t *in = (const uint8_t *)buf;
    for (size_t i = 0; i < n; i++) f->content[h->pos + i] = in[i];
    h->pos += n;
    if (h->pos > f->size) f->size = h->pos;
    /* Linux returns short write rather than ENOSPC when at least
     * one byte fit; we follow that. If nothing fit, return ENOSPC. */
    if (n == 0) return -LINUX_ENOSPC;
    return (int64_t)n;
}

int64_t linux_tmpfs_lseek_fd(int fd, int64_t offset, int whence) {
    int hidx = handle_from_fd(fd);
    if (hidx < 0) return -LINUX_EBADF;
    struct tmpfs_handle *h = &g_handles[hidx];
    struct tmpfs_file   *f = &g_files[h->file_idx];

    int64_t new_pos;
    switch (whence) {
        case 0: new_pos = offset; break;                  /* SEEK_SET */
        case 1: new_pos = (int64_t)h->pos + offset; break;/* SEEK_CUR */
        case 2: new_pos = (int64_t)f->size + offset; break;/* SEEK_END */
        case 3:                                           /* SEEK_DATA */
        case 4:                                           /* SEEK_HOLE */
            /* tmpfs files have no holes; treat as SEEK_SET. */
            new_pos = offset;
            break;
        default: return -LINUX_EINVAL;
    }
    if (new_pos < 0) return -LINUX_EINVAL;
    /* Linux allows seeking past EOF; subsequent writes auto-extend
     * with zero-fill. We don't pre-fill, but writes will populate
     * the gap as the cursor moves forward. */
    h->pos = (size_t)new_pos;
    return new_pos;
}

int64_t linux_tmpfs_unlink(const char *path) {
    if (!path) return -LINUX_EFAULT;
    const char *name = validate_path(path);
    if (!name) return -LINUX_ENOENT;
    int file_idx = find_file_by_name(name);
    if (file_idx < 0) return -LINUX_ENOENT;
    g_files[file_idx].unlinked = 1;
    /* Remove the name so future opens find a different slot.
     * Existing handles still reference file_idx and operate
     * normally until they close. */
    g_files[file_idx].name[0] = '\0';
    maybe_reap_file(file_idx);
    return 0;
}
