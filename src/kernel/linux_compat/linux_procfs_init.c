#include "kernel/linux_compat/linux_procfs.h"

/* Boot wiring for `linux_procfs`. Excluded from host tests.
 *
 * Marco M1 strategy: install minimal-but-truthful providers so
 * `cat /proc/cpuinfo` / `cat /proc/meminfo` produce parseable
 * output even before the real kernel state is hooked up.
 *
 * Each provider lives in its own translation unit so it can be
 * replaced individually as the kernel grows real iterators
 * (vmm_anon_region for maps, pmm_stats for meminfo, real cpuid
 * harvest for cpuinfo).
 */

#if !defined(UNIT_TEST)

#include "kernel/linux_compat/linux_proc.h"
#include "kernel/linux_compat/linux_cpuinfo.h"
#include "kernel/linux_compat/linux_clock.h"
#include "kernel/linux_compat/linux_types.h"

#include <stdint.h>
#include <stddef.h>

/* ---- Placeholder providers ---- */

static int placeholder_meminfo(struct linux_proc_meminfo *out) {
    /* Zero-initialised: MemTotal/MemFree/MemAvailable all 0.
     * Real PMM stats hookup lands when pmm exposes a getter. */
    *out = (struct linux_proc_meminfo){0};
    return 0;
}

static size_t placeholder_cpuinfo(struct linux_cpuinfo_entry *out, size_t cap) {
    if (cap == 0) return 0;
    /* Single fake CPU entry until cpuid harvest lands. The flags
     * advertise only the baseline x86_64 set so userland that
     * dispatches on AVX/SSE features doesn't pick wrong codepaths. */
    out[0] = (struct linux_cpuinfo_entry){
        .processor_index = 0,
        .vendor_id       = "CapyOS",
        .cpu_family      = 6,
        .model           = 0,
        .model_name      = "CapyOS Generic CPU",
        .stepping        = 0,
        .cpu_mhz         = 0,
        .cache_size_kb   = 0,
        .flags           = LINUX_CPUINFO_FLAG_FPU |
                           LINUX_CPUINFO_FLAG_TSC |
                           LINUX_CPUINFO_FLAG_CMOV |
                           LINUX_CPUINFO_FLAG_LM,
    };
    return 1;
}

static size_t placeholder_maps(struct linux_proc_maps_entry *out, size_t cap) {
    (void)out; (void)cap;
    /* Empty maps until vmm_anon_region walker lands. */
    return 0;
}

static const char *placeholder_self_exe(void) {
    return "/unknown";
}

static const char *placeholder_cmdline_argv[] = { NULL };
static const char *const *placeholder_cmdline(void) {
    return placeholder_cmdline_argv;
}

static int placeholder_self_status(struct linux_proc_pid_status *out) {
    *out = (struct linux_proc_pid_status){
        .name  = "capy",
        .state = LINUX_PROC_STATE_RUNNING,
        .pid   = 1,
        .ppid  = 0,
    };
    return 0;
}

static const char *placeholder_version_release(void) {
    return "6.5.0-capyos";
}

/* Uptime is sourced from the same clock that timerfd uses, so all
 * userland paths agree on "boot time". CapyOS does not yet track
 * idle time at the scheduler, so idle == uptime (Linux behaviour
 * on a system with no idle accounting; userland tolerates it). */
static int placeholder_uptime(struct linux_proc_uptime *out) {
    struct linux_timespec ts;
    if (linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &ts) != 0) {
        *out = (struct linux_proc_uptime){0};
        return 0;
    }
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    out->uptime_ns = ns;
    out->idle_ns   = ns;
    return 0;
}

static int placeholder_loadavg(struct linux_proc_loadavg *out) {
    /* No real load metric yet; emit "0.00 0.00 0.00 1/1 0".
     * Userland (procps, glibc, libcap) parses zeros without
     * issue; the running/total fields are kept >= 1 so divisions
     * by zero are avoided. */
    *out = (struct linux_proc_loadavg){
        .running_tasks = 1,
        .total_tasks   = 1,
        .last_pid      = 0,
    };
    return 0;
}

void linux_procfs_init_boot(void) {
    static const struct linux_procfs_providers ops = {
        .meminfo         = placeholder_meminfo,
        .cpuinfo         = placeholder_cpuinfo,
        .maps            = placeholder_maps,
        .cmdline         = placeholder_cmdline,
        .self_exe        = placeholder_self_exe,
        .self_status     = placeholder_self_status,
        .version_release = placeholder_version_release,
        .uptime          = placeholder_uptime,
        .loadavg         = placeholder_loadavg,
    };
    linux_procfs_install_providers(&ops);
}

#endif /* !UNIT_TEST */
