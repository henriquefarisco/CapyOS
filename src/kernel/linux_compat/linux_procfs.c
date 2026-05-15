#include "kernel/linux_compat/linux_procfs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

/* Local string-equality without pulling string.h. */
static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

/* The only flags that matter for procfs paths are O_ACCMODE
 * (read access; we accept any), O_CLOEXEC (silently honoured by
 * userland fd table, no-op here), and O_NONBLOCK (no effect --
 * reads are synchronous against the rendered buffer). Anything
 * else fails fast so userland sees ENOSYS-style determinism. */
#define PROCFS_OPEN_KNOWN_FLAGS \
    (0x0003u  /* O_ACCMODE */ |  \
     0x0800u  /* O_NONBLOCK */ | \
     0x80000u /* O_CLOEXEC  */)

struct procfs_slot {
    uint8_t in_use;
    size_t  size;                       /* total bytes valid in buf */
    size_t  pos;                        /* read cursor */
    char    buf[LINUX_PROCFS_MAX_BUFFER];
};

static struct procfs_slot          g_pfs[LINUX_PROCFS_MAX_INSTANCES];
static struct linux_procfs_providers g_providers;

void linux_procfs_install_providers(const struct linux_procfs_providers *p) {
    if (p) g_providers = *p;
    else   g_providers = (struct linux_procfs_providers){0};
}

void linux_procfs_reset_for_tests(void) {
    for (int i = 0; i < LINUX_PROCFS_MAX_INSTANCES; i++) {
        g_pfs[i].in_use = 0;
        g_pfs[i].size   = 0;
        g_pfs[i].pos    = 0;
    }
    g_providers = (struct linux_procfs_providers){0};
}

static int pfs_fd_to_slot(int fd) {
    int slot = fd - LINUX_PROCFS_FD_BASE;
    if (slot < 0 || slot >= LINUX_PROCFS_MAX_INSTANCES) return -1;
    if (!g_pfs[slot].in_use) return -1;
    return slot;
}

static int alloc_slot(void) {
    for (int i = 0; i < LINUX_PROCFS_MAX_INSTANCES; i++) {
        if (!g_pfs[i].in_use) {
            g_pfs[i].in_use = 1;
            g_pfs[i].size = 0;
            g_pfs[i].pos = 0;
            return i;
        }
    }
    return -1;
}

/* ---- Renderers, one per supported path ----
 *
 * Each renderer fills `slot->buf` (cap LINUX_PROCFS_MAX_BUFFER)
 * and sets `slot->size` to the number of bytes actually written
 * (clamped to MAX_BUFFER for truncation). */

static void render_meminfo(struct procfs_slot *slot) {
    struct linux_proc_meminfo m = {0};
    if (g_providers.meminfo) {
        int rc = g_providers.meminfo(&m);
        if (rc < 0) { slot->size = 0; return; }
    }
    size_t need = linux_proc_format_meminfo(
        &m, slot->buf, LINUX_PROCFS_MAX_BUFFER);
    slot->size = need < LINUX_PROCFS_MAX_BUFFER
                 ? need : LINUX_PROCFS_MAX_BUFFER - 1;
}

static void render_cpuinfo(struct procfs_slot *slot) {
    /* Allow up to 8 CPUs in the on-stack scratch. */
    struct linux_cpuinfo_entry entries[8] = {{0}};
    size_t n = 0;
    if (g_providers.cpuinfo) {
        n = g_providers.cpuinfo(entries, 8);
        if (n > 8) n = 8;
    }
    size_t need = linux_cpuinfo_format(
        entries, n, slot->buf, LINUX_PROCFS_MAX_BUFFER);
    slot->size = need < LINUX_PROCFS_MAX_BUFFER
                 ? need : LINUX_PROCFS_MAX_BUFFER - 1;
}

static void render_maps(struct procfs_slot *slot) {
    struct linux_proc_maps_entry entries[16] = {{0}};
    size_t n = 0;
    if (g_providers.maps) {
        n = g_providers.maps(entries, 16);
        if (n > 16) n = 16;
    }
    size_t need = linux_proc_format_maps(
        entries, n, slot->buf, LINUX_PROCFS_MAX_BUFFER);
    slot->size = need < LINUX_PROCFS_MAX_BUFFER
                 ? need : LINUX_PROCFS_MAX_BUFFER - 1;
}

static void render_cmdline(struct procfs_slot *slot) {
    const char *const *argv = NULL;
    if (g_providers.cmdline) argv = g_providers.cmdline();
    size_t need = linux_proc_format_cmdline(
        argv, slot->buf, LINUX_PROCFS_MAX_BUFFER);
    slot->size = need < LINUX_PROCFS_MAX_BUFFER
                 ? need : LINUX_PROCFS_MAX_BUFFER - 1;
}

static void render_self_exe(struct procfs_slot *slot) {
    const char *path = NULL;
    if (g_providers.self_exe) path = g_providers.self_exe();
    size_t need = linux_proc_format_self_exe(
        path, slot->buf, LINUX_PROCFS_MAX_BUFFER);
    slot->size = need < LINUX_PROCFS_MAX_BUFFER
                 ? need : LINUX_PROCFS_MAX_BUFFER - 1;
}

static void render_self_status(struct procfs_slot *slot) {
    struct linux_proc_pid_status s = {0};
    if (g_providers.self_status) {
        int rc = g_providers.self_status(&s);
        if (rc < 0) { slot->size = 0; return; }
    }
    size_t need = linux_proc_format_pid_status(
        &s, slot->buf, LINUX_PROCFS_MAX_BUFFER);
    slot->size = need < LINUX_PROCFS_MAX_BUFFER
                 ? need : LINUX_PROCFS_MAX_BUFFER - 1;
}

static void render_version(struct procfs_slot *slot) {
    const char *release = NULL;
    if (g_providers.version_release) release = g_providers.version_release();
    size_t need = linux_proc_format_version(
        release, slot->buf, LINUX_PROCFS_MAX_BUFFER);
    slot->size = need < LINUX_PROCFS_MAX_BUFFER
                 ? need : LINUX_PROCFS_MAX_BUFFER - 1;
}

static void render_uptime(struct procfs_slot *slot) {
    struct linux_proc_uptime u = {0};
    if (g_providers.uptime) {
        int rc = g_providers.uptime(&u);
        if (rc < 0) { slot->size = 0; return; }
    }
    size_t need = linux_proc_format_uptime(
        &u, slot->buf, LINUX_PROCFS_MAX_BUFFER);
    slot->size = need < LINUX_PROCFS_MAX_BUFFER
                 ? need : LINUX_PROCFS_MAX_BUFFER - 1;
}

static void render_loadavg(struct procfs_slot *slot) {
    struct linux_proc_loadavg l = {0};
    if (g_providers.loadavg) {
        int rc = g_providers.loadavg(&l);
        if (rc < 0) { slot->size = 0; return; }
    }
    size_t need = linux_proc_format_loadavg(
        &l, slot->buf, LINUX_PROCFS_MAX_BUFFER);
    slot->size = need < LINUX_PROCFS_MAX_BUFFER
                 ? need : LINUX_PROCFS_MAX_BUFFER - 1;
}

/* Path -> renderer dispatch. Returns NULL if path is not in the
 * supported set. */
typedef void (*renderer_fn)(struct procfs_slot *slot);

static renderer_fn match_path(const char *path) {
    if (str_eq(path, "/proc/cpuinfo"))      return render_cpuinfo;
    if (str_eq(path, "/proc/meminfo"))      return render_meminfo;
    if (str_eq(path, "/proc/self/maps"))    return render_maps;
    if (str_eq(path, "/proc/self/exe"))     return render_self_exe;
    if (str_eq(path, "/proc/self/cmdline")) return render_cmdline;
    if (str_eq(path, "/proc/self/status"))  return render_self_status;
    if (str_eq(path, "/proc/version"))      return render_version;
    if (str_eq(path, "/proc/uptime"))       return render_uptime;
    if (str_eq(path, "/proc/loadavg"))      return render_loadavg;
    return NULL;
}

/* ---- Public surface ---- */

int64_t linux_procfs_open(const char *path, uint32_t flags) {
    if (!path) return -LINUX_EFAULT;
    if (flags & ~PROCFS_OPEN_KNOWN_FLAGS) return -LINUX_EINVAL;
    /* Reject the all-bits ACCMODE pattern (Linux EINVAL). */
    if ((flags & 0x3u) == 0x3u) return -LINUX_EINVAL;

    renderer_fn render = match_path(path);
    if (!render) return -LINUX_ENOENT;

    int slot = alloc_slot();
    if (slot < 0) return -LINUX_EMFILE;

    render(&g_pfs[slot]);
    return LINUX_PROCFS_FD_BASE + slot;
}

int64_t linux_procfs_close(int fd) {
    int slot = pfs_fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    g_pfs[slot].in_use = 0;
    g_pfs[slot].size = 0;
    g_pfs[slot].pos = 0;
    return 0;
}

int64_t linux_procfs_read_fd(int fd, void *buf, size_t len) {
    int slot = pfs_fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;

    struct procfs_slot *s = &g_pfs[slot];
    if (s->pos >= s->size) return 0;  /* EOF */

    size_t avail = s->size - s->pos;
    size_t n = avail < len ? avail : len;

    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t)s->buf[s->pos + i];
    s->pos += n;
    return (int64_t)n;
}

int64_t linux_procfs_write_fd(int fd, const void *buf, size_t len) {
    (void)buf; (void)len;
    int slot = pfs_fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    /* /proc files in this milestone are read-only. Linux returns
     * EROFS when userland tries to write a read-only sysfs/procfs
     * entry; we surface the same so musl's perror prints the
     * correct message. */
    return -LINUX_EROFS;
}

int64_t linux_procfs_lseek_fd(int fd, int64_t offset, int whence) {
    int slot = pfs_fd_to_slot(fd);
    if (slot < 0) return -LINUX_EBADF;

    struct procfs_slot *s = &g_pfs[slot];
    int64_t new_pos;
    switch (whence) {
        case 0: new_pos = offset; break;                 /* SEEK_SET */
        case 1: new_pos = (int64_t)s->pos + offset; break;/* SEEK_CUR */
        case 2: new_pos = (int64_t)s->size + offset; break;/* SEEK_END */
        case 3:                                          /* SEEK_DATA */
        case 4:                                          /* SEEK_HOLE */
            /* Procfs files have no holes; SEEK_DATA at any pos
             * past size returns ENXIO in Linux, otherwise stays.
             * We approximate by treating both as SEEK_SET. */
            new_pos = offset;
            break;
        default: return -LINUX_EINVAL;
    }
    if (new_pos < 0) return -LINUX_EINVAL;
    /* Linux allows seeking past EOF for regular files; for procfs
     * the result of a read past EOF is just 0 bytes. We clamp the
     * cursor to size to keep the read path simple. */
    if ((size_t)new_pos > s->size) new_pos = (int64_t)s->size;
    s->pos = (size_t)new_pos;
    return new_pos;
}
