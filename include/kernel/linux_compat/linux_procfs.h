#ifndef KERNEL_LINUX_COMPAT_LINUX_PROCFS_H
#define KERNEL_LINUX_COMPAT_LINUX_PROCFS_H

#include <stdint.h>
#include <stddef.h>

#include "kernel/linux_compat/linux_proc.h"
#include "kernel/linux_compat/linux_cpuinfo.h"

/* Linux-ABI `/proc` backend.
 *
 * Connects the existing read-only formatters in `linux_proc` and
 * `linux_cpuinfo` to the VFS front door. Each `open(2)` call:
 *   1. Matches the path against the supported set.
 *   2. Calls the appropriate formatter, with data from injected
 *      providers.
 *   3. Stores the rendered bytes in a per-slot buffer.
 *   4. Returns an fd that backs `read`/`lseek` against the buffer.
 *
 * Why a separate module rather than putting routing logic inside
 * `linux_proc`:
 *   - `linux_proc` is a pure formatter library (no fd table, no
 *     state). Mixing it with the procfs frontend would couple
 *     test surfaces unnecessarily.
 *   - Future expansion of `/proc` (e.g. `/proc/<pid>/...` for
 *     cross-process introspection, `/proc/sys/...` writable
 *     entries) plugs in here without touching the formatters.
 *
 * Supported paths (Marco M1 read-only set):
 *
 *   /proc/cpuinfo            -> linux_cpuinfo_format
 *   /proc/meminfo            -> linux_proc_format_meminfo
 *   /proc/self/maps          -> linux_proc_format_maps
 *   /proc/self/exe           -> linux_proc_format_self_exe
 *   /proc/self/cmdline       -> linux_proc_format_cmdline
 *   /proc/self/status        -> linux_proc_format_pid_status
 *   /proc/version            -> linux_proc_format_version
 *   /proc/uptime             -> linux_proc_format_uptime
 *   /proc/loadavg            -> linux_proc_format_loadavg
 *
 * Anything else under `/proc/` returns -ENOENT.
 *
 * Buffers are sized at `LINUX_PROCFS_MAX_BUFFER` per slot. Render
 * larger than that is truncated (snprintf semantics) -- userland
 * gets a partial but well-formed prefix, never garbage.
 */

/* fd encoding: 0x8800 between devfs (0x8000) and shm (0x9000),
 * disjoint from every other backend. */
#define LINUX_PROCFS_FD_BASE        0x8800
#define LINUX_PROCFS_MAX_INSTANCES  16
#define LINUX_PROCFS_MAX_BUFFER     4096u

/* Provider bundle. Caller injects callbacks that source kernel
 * state. NULL provider for a path means "render emits an empty
 * file" -- the open succeeds, read returns 0 bytes. This lets
 * userland tools that probe `/proc/...` decide for themselves
 * how to handle missing data without hitting -ENOSYS. */
struct linux_procfs_providers {
    /* /proc/meminfo: fill struct, return 0 on success or -errno. */
    int (*meminfo)(struct linux_proc_meminfo *out);

    /* /proc/cpuinfo: fill up to `cap` entries, return how many
     * were filled (0..cap). Returning 0 emits an empty file. */
    size_t (*cpuinfo)(struct linux_cpuinfo_entry *out, size_t cap);

    /* /proc/self/maps: same convention as cpuinfo. */
    size_t (*maps)(struct linux_proc_maps_entry *out, size_t cap);

    /* /proc/self/cmdline. Returning NULL emits an empty file. */
    const char *const *(*cmdline)(void);

    /* /proc/self/exe. Returning NULL emits "/unknown". */
    const char *(*self_exe)(void);

    /* /proc/self/status: fill struct, return 0 or -errno. */
    int (*self_status)(struct linux_proc_pid_status *out);

    /* /proc/version. Returning NULL emits the default release
     * string (linux_proc_format_version uses "6.5.0-capyos"). */
    const char *(*version_release)(void);

    /* /proc/uptime: fill struct, return 0 or -errno. NULL provider
     * emits "0.00 0.00\n". */
    int (*uptime)(struct linux_proc_uptime *out);

    /* /proc/loadavg: fill struct, return 0 or -errno. NULL provider
     * emits "0.00 0.00 0.00 0/0 0\n". */
    int (*loadavg)(struct linux_proc_loadavg *out);
};

void linux_procfs_install_providers(const struct linux_procfs_providers *p);
void linux_procfs_reset_for_tests(void);

/* Public surface. All return -LINUX_E* on failure. */
int64_t linux_procfs_open (const char *path, uint32_t flags);
int64_t linux_procfs_close(int fd);
int64_t linux_procfs_read_fd (int fd, void *buf, size_t len);
int64_t linux_procfs_write_fd(int fd, const void *buf, size_t len);
int64_t linux_procfs_lseek_fd(int fd, int64_t offset, int whence);

#endif /* KERNEL_LINUX_COMPAT_LINUX_PROCFS_H */
