#ifndef KERNEL_LINUX_COMPAT_LINUX_PROC_H
#define KERNEL_LINUX_COMPAT_LINUX_PROC_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI `/proc/meminfo` and `/proc/<pid>/status` shims
 * (S2.5 + S2.6).
 *
 * Both are pure string formatters. Callers gather the runtime
 * stats (PMM totals for meminfo, per-task counters for status)
 * and pass them in; the formatter emits Linux 6.x canonical
 * output. The kernel build wires them to /proc dentry handlers
 * later (when S2 lands a real /proc driver).
 *
 * /proc/meminfo: minimal subset that Firefox / xpcom inspect:
 *
 *   MemTotal:       <kb> kB
 *   MemFree:        <kb> kB
 *   MemAvailable:   <kb> kB
 *   Buffers:        0 kB
 *   Cached:         0 kB
 *   SwapTotal:      0 kB
 *   SwapFree:       0 kB
 *
 * (CapyOS has no swap or cached buffers, so we emit zeros for
 *  those fields. Linux always emits them; omitting any field
 *  breaks parsers in the wild.)
 *
 * /proc/<pid>/status: subset that telemetry / glibc / Chromium
 *  IPC inspect:
 *
 *   Name:    <name>
 *   State:   <state-letter> (<state-word>)
 *   Tgid:    <pid>
 *   Pid:     <pid>
 *   PPid:    <ppid>
 *   Uid:     <uid> <uid> <uid> <uid>
 *   Gid:     <gid> <gid> <gid> <gid>
 *   FDSize:  <max_fds>
 *   VmPeak:  <kb> kB
 *   VmSize:  <kb> kB
 *   VmRSS:   <kb> kB
 *
 *  State letters: R=running, S=sleeping, D=disk-sleep,
 *  Z=zombie, T=stopped, X=dead.
 */

/* /proc/meminfo input. Sizes are in bytes; the formatter
 * converts to kB for output (Linux convention). */
struct linux_proc_meminfo {
    uint64_t mem_total_bytes;
    uint64_t mem_free_bytes;
    /* MemAvailable: Linux 3.14+ reports an estimate of memory
     * available for new allocations without swap. For us, with
     * no swap and no reclaimable cache, this equals mem_free. */
    uint64_t mem_available_bytes;
};

/* /proc/<pid>/status input. */
enum linux_proc_state {
    LINUX_PROC_STATE_RUNNING = 0,
    LINUX_PROC_STATE_SLEEPING,
    LINUX_PROC_STATE_DISK_SLEEP,
    LINUX_PROC_STATE_ZOMBIE,
    LINUX_PROC_STATE_STOPPED,
    LINUX_PROC_STATE_DEAD,
};

struct linux_proc_pid_status {
    const char *name;        /* may be NULL -> "unknown" */
    enum linux_proc_state state;
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;            /* real uid; we report the same for
                              * effective/saved/fs to keep parser
                              * consistency. */
    uint32_t gid;
    uint32_t fd_size;        /* size of the fd table */
    uint64_t vm_size_bytes;  /* virtual size */
    uint64_t vm_rss_bytes;   /* resident set */
    uint64_t vm_peak_bytes;  /* peak virtual size */
};

/* Format /proc/meminfo into `buf` (cap `buf_size`). Returns the
 * number of bytes that would have been written if the buffer
 * were large enough (snprintf semantics). NUL-terminates `buf`
 * if `buf_size > 0`. NULL `buf` (and 0 size) -> size-query. */
size_t linux_proc_format_meminfo(const struct linux_proc_meminfo *m,
                                 char *buf, size_t buf_size);

/* Format /proc/<pid>/status into `buf`. Same return contract. */
size_t linux_proc_format_pid_status(const struct linux_proc_pid_status *s,
                                    char *buf, size_t buf_size);

/* /proc/self/maps (S2.1).
 *
 * Linux format (one line per region):
 *   <start>-<end> <perms> <offset> <dev> <inode>  <pathname>
 *
 * Where:
 *   <start>-<end>   hex without 0x prefix, lowercase, page-aligned
 *   <perms>         4 chars: r/- w/- x/- p/s (private/shared)
 *   <offset>        hex (00000000 for anon)
 *   <dev>           hex major:minor (00:00 for anon)
 *   <inode>         decimal (0 for anon)
 *   <pathname>      file path, or [stack]/[heap]/[anon]/empty
 *
 * Real maps require iterating page tables AND the anon-region
 * registry. For Marco M1 we accept an array of pre-collected
 * entries; the kernel side iterates `vmm_anon_region` + page
 * walk and fills the array. */
struct linux_proc_maps_entry {
    uint64_t start;
    uint64_t end;
    int      perm_read;
    int      perm_write;
    int      perm_exec;
    int      perm_shared;   /* 0 == private */
    uint64_t offset;
    uint32_t dev_major;
    uint32_t dev_minor;
    uint64_t inode;
    const char *pathname;   /* may be NULL -> emits "" (anon) */
};

size_t linux_proc_format_maps(const struct linux_proc_maps_entry *entries,
                              size_t n,
                              char *buf, size_t buf_size);

/* /proc/self/cmdline (S2.3).
 *
 * Linux format: argv[0] '\0' argv[1] '\0' ... argv[N-1] '\0'.
 * No trailing terminator beyond the final NUL of the last arg.
 * Empty cmdline (no args) -> 0 bytes (truly empty file).
 *
 * Caller passes the argv array. If any argv[i] is NULL it is
 * treated as the end of the array. */
size_t linux_proc_format_cmdline(const char *const *argv,
                                 char *buf, size_t buf_size);

/* /proc/self/exe (S2.2).
 *
 * Linux: `readlink("/proc/self/exe")` returns the absolute path
 * to the binary. CapyOS does not have file paths today, but the
 * caller (xpcom XRE_GetBinaryPath) just needs something stable
 * that round-trips.
 *
 * The formatter copies `path` to `buf`, NUL-terminates, returns
 * the number of bytes (excluding NUL) that would have been
 * written -- snprintf semantics. NULL `path` -> emits "/unknown"
 * so userland readlink result is never empty. */
size_t linux_proc_format_self_exe(const char *path,
                                  char *buf, size_t buf_size);

/* /proc/version.
 *
 * Linux format (single line, ends with '\n'):
 *   "Linux version <release> (<builder>) (<compiler>) <build_info>\n"
 *
 * Userland (musl __libc_get_version_string, JS shell init,
 * Chromium DEV_NAME) usually only checks for the "Linux version"
 * prefix and ignores the rest. We emit the prefix verbatim so
 * those checks pass.
 *
 * `release` may be NULL -> emits a sensible default. */
size_t linux_proc_format_version(const char *release,
                                 char *buf, size_t buf_size);

/* /proc/uptime.
 *
 * Linux format (single line):
 *   "<uptime_seconds>.<hundredths> <idle_seconds>.<hundredths>\n"
 *
 * Both fields use 2 digits of fractional precision. We accept
 * nanoseconds for both and convert internally so callers can
 * pass clock_gettime(MONOTONIC) values directly. */
struct linux_proc_uptime {
    uint64_t uptime_ns;
    uint64_t idle_ns;
};
size_t linux_proc_format_uptime(const struct linux_proc_uptime *u,
                                char *buf, size_t buf_size);

/* /proc/loadavg.
 *
 * Linux format (single line):
 *   "<l1> <l5> <l15> <running>/<total> <last_pid>\n"
 *
 * Where l1, l5, l15 are decimal load averages with 2 fractional
 * digits. We accept thousandths (e.g. 100 = 0.10) so the caller
 * doesn't need floating-point arithmetic. CapyOS without real
 * load metrics passes zeros, which is parseable by glibc/libcap. */
struct linux_proc_loadavg {
    uint32_t load1_thousandths;
    uint32_t load5_thousandths;
    uint32_t load15_thousandths;
    uint32_t running_tasks;
    uint32_t total_tasks;
    uint32_t last_pid;
};
size_t linux_proc_format_loadavg(const struct linux_proc_loadavg *l,
                                 char *buf, size_t buf_size);

void linux_proc_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_PROC_H */
