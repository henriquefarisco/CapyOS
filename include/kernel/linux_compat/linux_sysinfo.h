#ifndef KERNEL_LINUX_COMPAT_LINUX_SYSINFO_H
#define KERNEL_LINUX_COMPAT_LINUX_SYSINFO_H

/* Linux ABI `sysinfo(2)` and `getrusage(2)` -- coarse system
 * and per-task resource statistics.
 *
 *   int sysinfo(struct sysinfo *info);
 *   int getrusage(int who, struct rusage *usage);
 *
 * Both are heavily used: musl/glibc use sysinfo to size internal
 * buffers (notably `pthread_create`'s default stack-size cap),
 * and Firefox's `nsSystemInfo` reads sysinfo at startup to fill
 * crash-report metadata. getrusage is used by `time(1)` and any
 * program that reports peak RSS / user/system CPU.
 *
 * Marco M1 has no real bookkeeping for either, but Linux is
 * tolerant of zero-filled fields. We populate what we can from
 * injected providers (uptime from `linux_proc`, total ram from
 * `linux_proc_format_meminfo`) and zero the rest. */

#include <stdint.h>

/* `struct sysinfo` (Linux x86_64). 112 bytes after natural
 * alignment (uptime=8, loads=24, 6*u64=48, u16+u16=4, pad to
 * 8-align=4, 2*u64=16, u32=4 -> tail aligned to 8 = 112).
 * Layout matches Linux `include/uapi/linux/sysinfo.h` for the
 * 64-bit ABI verbatim. */
struct linux_sysinfo {
    int64_t  uptime;            /* seconds since boot           */
    uint64_t loads[3];          /* 1m/5m/15m load (1<<SI_LOAD_SHIFT) */
    uint64_t totalram;          /* total usable main memory     */
    uint64_t freeram;           /* available memory             */
    uint64_t sharedram;         /* shared memory amount         */
    uint64_t bufferram;         /* memory used by buffers       */
    uint64_t totalswap;         /* total swap                   */
    uint64_t freeswap;          /* available swap               */
    uint16_t procs;             /* number of current processes  */
    uint16_t pad;
    uint64_t totalhigh;         /* high memory total            */
    uint64_t freehigh;          /* available high memory        */
    uint32_t mem_unit;          /* memory unit size in bytes    */
};

/* `getrusage` `who` selectors. */
#define LINUX_RUSAGE_SELF      0
#define LINUX_RUSAGE_CHILDREN (-1)
#define LINUX_RUSAGE_THREAD    1

/* `struct rusage` (Linux x86_64). 144 bytes. */
struct linux_rusage {
    int64_t ru_utime_sec;     int64_t ru_utime_usec;
    int64_t ru_stime_sec;     int64_t ru_stime_usec;
    int64_t ru_maxrss;        int64_t ru_ixrss;
    int64_t ru_idrss;         int64_t ru_isrss;
    int64_t ru_minflt;        int64_t ru_majflt;
    int64_t ru_nswap;         int64_t ru_inblock;
    int64_t ru_oublock;       int64_t ru_msgsnd;
    int64_t ru_msgrcv;        int64_t ru_nsignals;
    int64_t ru_nvcsw;         int64_t ru_nivcsw;
};

struct linux_sysinfo_providers {
    /* Returns total memory in bytes. NULL = report 0. */
    uint64_t (*total_ram_bytes)(void);
    /* Returns free memory in bytes. NULL = report 0. */
    uint64_t (*free_ram_bytes)(void);
    /* Uptime in seconds. NULL = report 0. */
    int64_t  (*uptime_seconds)(void);
    /* Number of running processes. NULL = report 1. */
    uint16_t (*nproc)(void);
};

void linux_sysinfo_install(const struct linux_sysinfo_providers *p);
void linux_sysinfo_reset_for_tests(void);

int64_t linux_sysinfo(struct linux_sysinfo *info);
int64_t linux_getrusage(int who, struct linux_rusage *usage);

void linux_sysinfo_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_SYSINFO_H */
