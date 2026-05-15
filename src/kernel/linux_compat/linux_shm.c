#include "kernel/linux_compat/linux_shm.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

struct shm_object {
    int       in_use;       /* slot is allocated */
    int       named;        /* name still bound */
    int       refcount;     /* fds referring to this object */
    char      name[LINUX_SHM_MAX_NAME + 1];
    uint64_t  size;
    /* For Marco M1 we do not actually allocate frames; size is
     * tracked. When mmap routes here, it asks the VMM to back
     * the region with `size / PAGE_SIZE` zero-filled pages. */
};

static struct shm_object g_shms[LINUX_SHM_MAX_OBJECTS];

void linux_shm_reset_for_tests(void) {
    for (size_t i = 0; i < LINUX_SHM_MAX_OBJECTS; i++) {
        g_shms[i].in_use   = 0;
        g_shms[i].named    = 0;
        g_shms[i].refcount = 0;
        g_shms[i].size     = 0;
        g_shms[i].name[0]  = '\0';
    }
}

static size_t name_len(const char *s, size_t cap) {
    size_t i = 0;
    while (i < cap && s[i] != '\0') i++;
    return i;
}

static int name_eq(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int find_named(const char *name) {
    for (int i = 0; i < LINUX_SHM_MAX_OBJECTS; i++) {
        if (g_shms[i].in_use && g_shms[i].named &&
            name_eq(g_shms[i].name, name)) return i;
    }
    return -1;
}

static int alloc_slot(void) {
    for (int i = 0; i < LINUX_SHM_MAX_OBJECTS; i++) {
        if (!g_shms[i].in_use) return i;
    }
    return -1;
}

static int fd_to_slot(int fd) {
    int slot = fd - LINUX_SHM_FD_BASE;
    if (slot < 0 || slot >= LINUX_SHM_MAX_OBJECTS) return -1;
    if (!g_shms[slot].in_use) return -1;
    return slot;
}

static void release_slot_if_orphaned(int slot) {
    struct shm_object *o = &g_shms[slot];
    if (!o->named && o->refcount == 0) {
        o->in_use = 0;
        o->size   = 0;
        o->name[0] = '\0';
    }
}

/* ---------- shm_open ---------- */

int64_t linux_shm_open(const char *name, uint32_t oflag, uint32_t mode) {
    (void)mode;
    if (oflag & ~LINUX_SHM_OPEN_KNOWN_FLAGS) return -LINUX_EINVAL;
    if (!name || name[0] == '\0') return -LINUX_EINVAL;

    size_t len = name_len(name, LINUX_SHM_MAX_NAME + 1);
    if (len > LINUX_SHM_MAX_NAME) return -LINUX_ENAMETOOLONG;

    int existing = find_named(name);
    int want_create = (oflag & LINUX_O_CREAT) != 0;
    int want_excl   = (oflag & LINUX_O_EXCL) != 0;
    int want_trunc  = (oflag & LINUX_O_TRUNC) != 0;

    if (existing >= 0) {
        if (want_create && want_excl) return -LINUX_EEXIST;
        if (want_trunc) g_shms[existing].size = 0;
        g_shms[existing].refcount++;
        return (int64_t)(LINUX_SHM_FD_BASE + existing);
    }

    if (!want_create) return -LINUX_ENOENT;

    int slot = alloc_slot();
    if (slot < 0) return -LINUX_EMFILE;

    g_shms[slot].in_use   = 1;
    g_shms[slot].named    = 1;
    g_shms[slot].refcount = 1;
    g_shms[slot].size     = 0;
    for (size_t i = 0; i < len; i++) g_shms[slot].name[i] = name[i];
    g_shms[slot].name[len] = '\0';
    return (int64_t)(LINUX_SHM_FD_BASE + slot);
}

/* ---------- shm_unlink ---------- */

int64_t linux_shm_unlink(const char *name) {
    if (!name) return -LINUX_EINVAL;
    int slot = find_named(name);
    if (slot < 0) return -LINUX_ENOENT;
    g_shms[slot].named = 0;
    /* POSIX: existing fds remain valid; backing freed on last close. */
    release_slot_if_orphaned(slot);
    return 0;
}

/* ---------- shm_truncate ---------- */

int64_t linux_shm_truncate(int fd, uint64_t size) {
    if (size > LINUX_SHM_MAX_SIZE) return -LINUX_EFBIG;
    int slot = fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    g_shms[slot].size = size;
    return 0;
}

int64_t linux_shm_size(int fd) {
    int slot = fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    return (int64_t)g_shms[slot].size;
}

int64_t linux_shm_close(int fd) {
    int slot = fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    if (g_shms[slot].refcount > 0) g_shms[slot].refcount--;
    release_slot_if_orphaned(slot);
    return 0;
}

size_t linux_shm_test_named_count(void) {
    size_t n = 0;
    for (size_t i = 0; i < LINUX_SHM_MAX_OBJECTS; i++) {
        if (g_shms[i].in_use && g_shms[i].named) n++;
    }
    return n;
}
