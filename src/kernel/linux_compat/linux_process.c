#include "kernel/linux_compat/linux_process.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

/* Accessor ops installed at boot. Zero-initialised by default so
 * pre-install calls degrade gracefully (Linux never has no-current-
 * task in production; this is host-test safety). */
static struct linux_process_ops g_ops;

/* S1.10 storage. Module-local because there is no per-task slot
 * yet (single-thread model). When S1.4 clone with thread groups
 * lands, these migrate to the task struct. */
static uint64_t g_tid_address;
static uint64_t g_robust_list_head;
static size_t   g_robust_list_len;

void linux_process_install_ops(const struct linux_process_ops *ops) {
    if (ops) g_ops = *ops;
    else g_ops = (struct linux_process_ops){0};
}

void linux_process_reset_for_tests(void) {
    g_ops = (struct linux_process_ops){0};
    g_tid_address = 0;
    g_robust_list_head = 0;
    g_robust_list_len = 0;
}

/* -------- internal helpers ---------------------------------------- */

static int resolve_current_view(struct linux_task_view *out) {
    if (!g_ops.current || !g_ops.view) return -1;
    linux_task_t *t = g_ops.current();
    if (!t) return -1;
    return g_ops.view(t, out);
}

static int resolve_pid_view(uint32_t pid, struct linux_task_view *out) {
    if (pid == 0) return resolve_current_view(out);
    if (!g_ops.by_pid || !g_ops.view) return -1;
    linux_task_t *t = g_ops.by_pid(pid);
    if (!t) return -1;
    return g_ops.view(t, out);
}

/* -------- S1.18 gettid -------------------------------------------- */

int64_t linux_gettid(void) {
    struct linux_task_view v = {0};
    if (resolve_current_view(&v) != 0) return 0;
    return (int64_t)v.pid;
}

/* -------- S1.11 sched_yield + sched_*affinity --------------------- */

int64_t linux_sched_yield(void) {
    if (g_ops.yield) g_ops.yield();
    return 0;
}

/* Linux requires cpusetsize to be a multiple of sizeof(long). We use
 * 8 bytes (our target arch is x86_64 where long == 8). */
#define LINUX_CPUSETSIZE_ALIGN 8

int64_t linux_sched_getaffinity(uint32_t pid, size_t cpusetsize,
                                uint8_t *mask_out) {
    if (cpusetsize == 0) return -LINUX_EINVAL;
    if ((cpusetsize % LINUX_CPUSETSIZE_ALIGN) != 0) return -LINUX_EINVAL;
    if (!mask_out) return -LINUX_EFAULT;

    struct linux_task_view v = {0};
    if (resolve_pid_view(pid, &v) != 0) return -LINUX_ESRCH;

    /* Single-CPU model: bit 0 set, rest zero. */
    mask_out[0] = 0x01;
    for (size_t i = 1; i < cpusetsize; i++) mask_out[i] = 0x00;

    /* Linux returns the number of bytes placed. We report the
     * mask size that our kernel "tracks" -- one long. */
    return (int64_t)LINUX_CPUSETSIZE_ALIGN;
}

int64_t linux_sched_setaffinity(uint32_t pid, size_t cpusetsize,
                                const uint8_t *mask) {
    if (cpusetsize == 0) return -LINUX_EINVAL;
    if ((cpusetsize % LINUX_CPUSETSIZE_ALIGN) != 0) return -LINUX_EINVAL;
    if (!mask) return -LINUX_EFAULT;

    struct linux_task_view v = {0};
    if (resolve_pid_view(pid, &v) != 0) return -LINUX_ESRCH;

    /* Linux rejects an all-zero mask with -EINVAL ("empty set"). */
    int any_bit_set = 0;
    for (size_t i = 0; i < cpusetsize; i++) {
        if (mask[i] != 0) { any_bit_set = 1; break; }
    }
    if (!any_bit_set) return -LINUX_EINVAL;

    /* Single CPU today: we ignore the mask value and always pin to
     * CPU 0. This is what `sched_setaffinity(0x01)` would do on
     * Linux, and masks requesting CPUs we do not have are silently
     * narrowed to what is available (again Linux semantics). */
    return 0;
}

/* -------- S1.9 prctl ---------------------------------------------- */

/* Copy up to `max-1` chars from `src` into `dst`, NUL-terminate at
 * position `max-1`. Returns number of non-NUL bytes copied. `src`
 * is assumed to be either NUL-terminated or at least `max` bytes
 * (Linux uses 16). */
static size_t copy_bounded(char *dst, const char *src, size_t max) {
    if (max == 0) return 0;
    size_t i = 0;
    while (i + 1 < max && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

static int64_t prctl_set_name(uint64_t a1) {
    if (a1 == 0) return -LINUX_EFAULT;
    struct linux_task_view v = {0};
    if (resolve_current_view(&v) != 0) return -LINUX_ESRCH;
    if (!v.name || v.name_cap == 0) return -LINUX_EFAULT;

    const char *src = (const char *)(uintptr_t)a1;
    /* Linux caps the name at 16 bytes including terminator. Use the
     * smaller of the task's capacity and LINUX_PR_NAME_MAX. */
    size_t cap = v.name_cap < LINUX_PR_NAME_MAX ? v.name_cap
                                                : LINUX_PR_NAME_MAX;
    (void)copy_bounded(v.name, src, cap);
    return 0;
}

static int64_t prctl_get_name(uint64_t a1) {
    if (a1 == 0) return -LINUX_EFAULT;
    struct linux_task_view v = {0};
    if (resolve_current_view(&v) != 0) return -LINUX_ESRCH;
    if (!v.name) return -LINUX_EFAULT;

    char *dst = (char *)(uintptr_t)a1;
    /* Linux always writes exactly 16 bytes, NUL-terminated. */
    size_t src_len = 0;
    while (src_len + 1 < LINUX_PR_NAME_MAX && src_len < v.name_cap &&
           v.name[src_len] != '\0') {
        dst[src_len] = v.name[src_len];
        src_len++;
    }
    for (size_t i = src_len; i < LINUX_PR_NAME_MAX; i++) dst[i] = '\0';
    return 0;
}

int64_t linux_prctl(int32_t op, uint64_t a1, uint64_t a2,
                    uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    switch (op) {
        case LINUX_PR_SET_NAME: return prctl_set_name(a1);
        case LINUX_PR_GET_NAME: return prctl_get_name(a1);
        default:                return -LINUX_EINVAL;
    }
}

/* -------- S1.17 prlimit64 ----------------------------------------- */

static void fill_rlimit(uint32_t resource, struct linux_rlimit64 *out) {
    switch (resource) {
        case LINUX_RLIMIT_AS:
            out->rlim_cur = LINUX_CAPYOS_RLIMIT_AS_MAX;
            out->rlim_max = LINUX_CAPYOS_RLIMIT_AS_MAX;
            break;
        case LINUX_RLIMIT_NOFILE:
            out->rlim_cur = LINUX_CAPYOS_RLIMIT_NOFILE_MAX;
            out->rlim_max = LINUX_CAPYOS_RLIMIT_NOFILE_MAX;
            break;
        case LINUX_RLIMIT_STACK:
            out->rlim_cur = LINUX_CAPYOS_RLIMIT_STACK_SOFT;
            out->rlim_max = LINUX_CAPYOS_RLIMIT_STACK_HARD;
            break;
        default:
            /* All other resources: report RLIM_INFINITY, same as
             * Linux default for untracked resources. */
            out->rlim_cur = LINUX_RLIM_INFINITY;
            out->rlim_max = LINUX_RLIM_INFINITY;
            break;
    }
}

int64_t linux_prlimit64(uint32_t pid, uint32_t resource,
                        const struct linux_rlimit64 *new_limit,
                        struct linux_rlimit64 *old_limit) {
    if (resource >= LINUX_RLIMIT_NLIMITS) return -LINUX_EINVAL;

    /* pid == 0 means self; any other pid is unsupported (Linux allows
     * querying other pids with CAP_SYS_RESOURCE; we do not grant it). */
    if (pid != 0) {
        struct linux_task_view v = {0};
        if (resolve_current_view(&v) != 0) return -LINUX_ESRCH;
        if (v.pid != pid) return -LINUX_EPERM;
    }

    if (old_limit) fill_rlimit(resource, old_limit);

    /* We do not permit raising or lowering limits at runtime; they
     * are kernel policy. Refuse any write attempt with -EPERM so
     * userland uses the returned `old_limit` as the effective value. */
    if (new_limit != NULL) return -LINUX_EPERM;

    return 0;
}

/* -------- S1.10 set_tid_address + set_robust_list ----------------- */

int64_t linux_set_tid_address(uint64_t tidptr) {
    /* Linux returns the current tid even if tidptr is NULL (the
     * NULL case effectively clears the stored pointer). */
    g_tid_address = tidptr;
    return linux_gettid();
}

int64_t linux_set_robust_list(uint64_t head, size_t len) {
    /* Linux validates len strictly: must equal sizeof(struct
     * robust_list_head). Any deviation -> -EINVAL. This is the
     * single way to detect an ABI/userland mismatch. */
    if (len != LINUX_ROBUST_LIST_HEAD_SIZE) return -LINUX_EINVAL;
    g_robust_list_head = head;
    g_robust_list_len  = len;
    return 0;
}

uint64_t linux_process_test_get_tid_address(void)      { return g_tid_address; }
uint64_t linux_process_test_get_robust_list(void)      { return g_robust_list_head; }
size_t   linux_process_test_get_robust_list_len(void)  { return g_robust_list_len; }

/* -------- musl bring-up syscalls (sessao 20) --------------------- */

int64_t linux_getpid(void) {
    /* CapyOS today: 1 thread per process so getpid == gettid.
     * When S1.4 thread groups land, this returns the thread group
     * leader pid (which is what Linux semantics require). */
    return linux_gettid();
}

int64_t linux_getppid(void) {
    /* CapyOS does not yet track parent task id. Return 1 (init's
     * pid) so userland that checks `getppid() == 1` to detect
     * orphaned-by-init still works. Refines later. */
    return 1;
}

int64_t linux_getuid(void)  { return 0; }
int64_t linux_geteuid(void) { return 0; }
int64_t linux_getgid(void)  { return 0; }
int64_t linux_getegid(void) { return 0; }

/* Local strncpy without pulling string.h. Caller guarantees `dst`
 * has at least `cap` bytes. NUL-terminates within `cap`. */
static void uts_set(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (cap == 0) return;
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    /* Pad the rest with NULs (Linux always zeroes the field tail). */
    while (i < cap) dst[i++] = '\0';
}

int64_t linux_uname(struct linux_utsname *uts) {
    if (!uts) return -LINUX_EFAULT;
    uts_set(uts->sysname,    LINUX_UTSNAME_FIELD, "Linux");
    uts_set(uts->nodename,   LINUX_UTSNAME_FIELD, "capyos");
    uts_set(uts->release,    LINUX_UTSNAME_FIELD, "6.5.0-capyos");
    uts_set(uts->version,    LINUX_UTSNAME_FIELD, "#1 SMP CapyOS");
    uts_set(uts->machine,    LINUX_UTSNAME_FIELD, "x86_64");
    uts_set(uts->domainname, LINUX_UTSNAME_FIELD, "(none)");
    return 0;
}

/* -------- Syscall adapters --------------------------------------- */

static int64_t sys_gettid(const struct linux_syscall_args *a) {
    (void)a;
    return linux_gettid();
}

static int64_t sys_getpid(const struct linux_syscall_args *a) {
    (void)a; return linux_getpid();
}
static int64_t sys_getppid(const struct linux_syscall_args *a) {
    (void)a; return linux_getppid();
}
static int64_t sys_getuid(const struct linux_syscall_args *a) {
    (void)a; return linux_getuid();
}
static int64_t sys_geteuid(const struct linux_syscall_args *a) {
    (void)a; return linux_geteuid();
}
static int64_t sys_getgid(const struct linux_syscall_args *a) {
    (void)a; return linux_getgid();
}
static int64_t sys_getegid(const struct linux_syscall_args *a) {
    (void)a; return linux_getegid();
}
static int64_t sys_uname(const struct linux_syscall_args *a) {
    return linux_uname((struct linux_utsname *)(uintptr_t)a->a0);
}

static int64_t sys_sched_yield(const struct linux_syscall_args *a) {
    (void)a;
    return linux_sched_yield();
}

static int64_t sys_sched_getaffinity(const struct linux_syscall_args *a) {
    return linux_sched_getaffinity((uint32_t)a->a0, (size_t)a->a1,
                                   (uint8_t *)(uintptr_t)a->a2);
}

static int64_t sys_sched_setaffinity(const struct linux_syscall_args *a) {
    return linux_sched_setaffinity((uint32_t)a->a0, (size_t)a->a1,
                                   (const uint8_t *)(uintptr_t)a->a2);
}

static int64_t sys_prctl(const struct linux_syscall_args *a) {
    return linux_prctl((int32_t)a->a0, a->a1, a->a2, a->a3, a->a4);
}

static int64_t sys_prlimit64(const struct linux_syscall_args *a) {
    return linux_prlimit64((uint32_t)a->a0, (uint32_t)a->a1,
                           (const struct linux_rlimit64 *)(uintptr_t)a->a2,
                           (struct linux_rlimit64 *)(uintptr_t)a->a3);
}

static int64_t sys_set_tid_address(const struct linux_syscall_args *a) {
    return linux_set_tid_address(a->a0);
}

static int64_t sys_set_robust_list(const struct linux_syscall_args *a) {
    return linux_set_robust_list(a->a0, (size_t)a->a1);
}

void linux_process_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_gettid,             sys_gettid);
    (void)linux_syscall_register(LINUX_NR_sched_yield,        sys_sched_yield);
    (void)linux_syscall_register(LINUX_NR_sched_getaffinity,  sys_sched_getaffinity);
    (void)linux_syscall_register(LINUX_NR_sched_setaffinity,  sys_sched_setaffinity);
    (void)linux_syscall_register(LINUX_NR_prctl,              sys_prctl);
    (void)linux_syscall_register(LINUX_NR_prlimit64,          sys_prlimit64);
    (void)linux_syscall_register(LINUX_NR_set_tid_address,    sys_set_tid_address);
    (void)linux_syscall_register(LINUX_NR_set_robust_list,    sys_set_robust_list);
    /* musl bring-up: cred + identity (sessao 20). */
    (void)linux_syscall_register(LINUX_NR_getpid,   sys_getpid);
    (void)linux_syscall_register(LINUX_NR_getppid,  sys_getppid);
    (void)linux_syscall_register(LINUX_NR_getuid,   sys_getuid);
    (void)linux_syscall_register(LINUX_NR_geteuid,  sys_geteuid);
    (void)linux_syscall_register(LINUX_NR_getgid,   sys_getgid);
    (void)linux_syscall_register(LINUX_NR_getegid,  sys_getegid);
    (void)linux_syscall_register(LINUX_NR_uname,    sys_uname);
}
