#ifndef KERNEL_LINUX_COMPAT_LINUX_PROCESS_H
#define KERNEL_LINUX_COMPAT_LINUX_PROCESS_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI process/thread/scheduler shims (S1.9, S1.11, S1.17, S1.18).
 *
 * Grouped in a single module because these syscalls share the same
 * domain (task accessors) and have trivial bodies once the task
 * accessors are injected. The module:
 *
 *   S1.18 gettid()                -> task->pid
 *   S1.11 sched_yield()           -> yield_fn callback
 *   S1.11 sched_getaffinity()     -> reports single-CPU bitmap
 *   S1.11 sched_setaffinity()     -> accepts any mask, no-op
 *                                    (single-CPU semantics today)
 *   S1.9  prctl(PR_SET_NAME)      -> copies up to 15 chars + NUL
 *                                    into task->name (Linux cap = 16)
 *   S1.9  prctl(PR_GET_NAME)      -> reads task->name into user buf
 *   S1.9  prctl(other)            -> -LINUX_EINVAL
 *   S1.17 prlimit64(RLIMIT_NOFILE/AS/STACK) -> static caps matching
 *                                              CapyOS policy
 *
 * Design:
 *   - Same layering strategy as `linux_clock` / `linux_random`: the
 *     core logic takes function pointers for task accessors so host
 *     tests can inject fakes. The kernel boot wiring installs the
 *     real accessors (`task_current`, `task_by_pid`, `task_yield`).
 *
 *   - Fake task view: the module does not import `struct task`. It
 *     operates on a minimal `linux_task_view` (pid + name) returned
 *     by the accessor. This keeps the module testable without the
 *     scheduler and isolates the ABI from kernel-internal changes.
 */

/* -------- Linux-ABI constants (mirror uapi headers upstream) ------ */

/* prctl operations (include/uapi/linux/prctl.h). We expose only
 * PR_SET_NAME / PR_GET_NAME now; the rest return -EINVAL. */
#define LINUX_PR_SET_NAME 15
#define LINUX_PR_GET_NAME 16

/* prctl name cap in Linux: 16 bytes including terminator. */
#define LINUX_PR_NAME_MAX 16

/* rlimit resources (include/uapi/asm-generic/resource.h). We expose
 * the three Firefox/musl cares about at startup. */
#define LINUX_RLIMIT_AS     9   /* address space */
#define LINUX_RLIMIT_NOFILE 7   /* max open files */
#define LINUX_RLIMIT_STACK  3   /* max stack size */
#define LINUX_RLIMIT_NLIMITS 16 /* Linux mainline has 16 */

/* Linux RLIM_INFINITY on 64-bit. */
#define LINUX_RLIM_INFINITY ((uint64_t)0xFFFFFFFFFFFFFFFFull)

/* Linux `struct rlimit64`. */
struct linux_rlimit64 {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

/* -------- Task view (host-testable) ------------------------------- */

/* Opaque pointer to a task; the accessors interpret it. Host tests
 * pass pointers into their own fake task structs. */
typedef void linux_task_t;

/* Minimal projection of a task used by this module. */
struct linux_task_view {
    uint32_t pid;
    /* Pointer into the task's name storage; the module writes here
     * directly during PR_SET_NAME to propagate the rename into the
     * kernel task struct. At most LINUX_PR_NAME_MAX bytes including
     * terminator. */
    char    *name;
    size_t   name_cap;
};

/* Accessor bundle installed at boot. */
struct linux_process_ops {
    /* Return the currently running task, or NULL if no task exists
     * (early boot or host test with no install). */
    linux_task_t *(*current)(void);
    /* Return task by pid, or NULL. */
    linux_task_t *(*by_pid)(uint32_t pid);
    /* Project a task pointer into a view. Fills *out; returns 0 on
     * success, -1 on NULL task. */
    int (*view)(linux_task_t *t, struct linux_task_view *out);
    /* Yield the current task. May be NULL on freestanding early
     * boot; sched_yield then returns 0 (Linux always returns 0). */
    void (*yield)(void);
};

void linux_process_install_ops(const struct linux_process_ops *ops);
void linux_process_reset_for_tests(void);

/* -------- Individual entry points (host-testable pure logic) ------ */

/* Returns current task pid. No errors possible: if no task is
 * installed, returns 0 (kernel has no current thread -- Linux would
 * never be in this situation, but we defend). */
int64_t linux_gettid(void);

/* Returns 0 (Linux semantics). Calls ops->yield if installed. */
int64_t linux_sched_yield(void);

/* Fills `*mask_out` with a bitmap of allowed CPUs for `pid` (or
 * current if pid==0). `cpusetsize` is in bytes; must be >=
 * sizeof(unsigned long) aligned to 8 like Linux. On success returns
 * the size of the mask in bytes (Linux 2.6.9+ semantics: returns the
 * number of bytes placed in the mask, which is the kernel internal
 * cpumask size). Writes `0x01` in byte 0 (CPU 0 only), zeroes others.
 *
 * Errors:
 *   NULL mask, len > 0    -> -LINUX_EFAULT
 *   cpusetsize == 0       -> -LINUX_EINVAL
 *   cpusetsize not 8-align-> -LINUX_EINVAL (Linux checks this too)
 *   pid != 0 && unknown   -> -LINUX_ESRCH
 */
int64_t linux_sched_getaffinity(uint32_t pid, size_t cpusetsize,
                                uint8_t *mask_out);

/* No-op: we have a single scheduling domain for now. Accepts any
 * non-zero mask as success (Linux mask is 0x01 minimum for useful
 * calls). Errors mirror getaffinity. */
int64_t linux_sched_setaffinity(uint32_t pid, size_t cpusetsize,
                                const uint8_t *mask);

/* prctl dispatcher. Only PR_SET_NAME / PR_GET_NAME are handled.
 *   PR_SET_NAME: a1 points to a NUL-terminated (or 16-char) name.
 *     Copies up to 15 chars + NUL into task->name (via the view).
 *     Returns 0 on success, -EFAULT if a1 == NULL.
 *   PR_GET_NAME: a1 points to a buffer >= 16 bytes. Writes exactly
 *     16 bytes (always NUL-terminated). Returns 0.
 *   other op: -LINUX_EINVAL (we do not pretend to support what we
 *     do not, per the ENOSYS/EINVAL split).
 */
int64_t linux_prctl(int32_t op, uint64_t a1, uint64_t a2,
                    uint64_t a3, uint64_t a4);

/* prlimit64(pid, resource, new_limit, old_limit).
 *   pid == 0 or pid == current      -> self (only self supported today)
 *   pid != 0 && pid != current      -> -LINUX_EPERM (Linux default)
 *   resource >= LINUX_RLIMIT_NLIMITS-> -LINUX_EINVAL
 *   new_limit != NULL               -> -LINUX_EPERM (we refuse to
 *                                       raise/lower limits: the kernel
 *                                       sets them and they are fixed
 *                                       for Marco M1)
 *   Supported resources: AS, NOFILE, STACK (others return
 *   LINUX_RLIM_INFINITY to match Linux defaults for "no enforced
 *   limit" and let userland check the sentinel).
 *
 * Limit values (from CapyOS policy):
 *   RLIMIT_AS     = 8 GiB (soft+hard)
 *   RLIMIT_NOFILE = 1024 (soft+hard) -- matches most Linux distros
 *   RLIMIT_STACK  = 8 MiB soft, 64 MiB hard -- Linux defaults
 */
int64_t linux_prlimit64(uint32_t pid, uint32_t resource,
                        const struct linux_rlimit64 *new_limit,
                        struct linux_rlimit64 *old_limit);

/* Exposed so the defaults can be asserted in tests. */
#define LINUX_CAPYOS_RLIMIT_AS_MAX     (8ull * 1024 * 1024 * 1024) /* 8 GiB */
#define LINUX_CAPYOS_RLIMIT_NOFILE_MAX 1024u
#define LINUX_CAPYOS_RLIMIT_STACK_SOFT (8ull  * 1024 * 1024)       /* 8 MiB */
#define LINUX_CAPYOS_RLIMIT_STACK_HARD (64ull * 1024 * 1024)       /* 64 MiB */

/* S1.10 -- pthread / musl thread housekeeping.
 *
 * `set_tid_address(int *tidptr)` -- Linux stores `tidptr` so it
 * can clear the futex at thread exit (CLONE_CHILD_CLEARTID). For
 * Marco M1 we have a single thread per process, so the stored
 * pointer is unused (no clear-on-exit), but musl calls this on
 * thread startup and the kernel must return the current tid.
 *
 * `set_robust_list(struct robust_list_head *head, size_t len)` --
 * registers a per-thread robust-futex list. Linux validates
 * `len == sizeof(struct robust_list_head)` (24 bytes on x86_64).
 * Returns 0 on success, -EINVAL on bad len.
 *
 * Both are stored in module-local state today (single thread).
 * When S1.4 clone with thread groups lands, this graduates to a
 * per-task slot. */

/* Linux struct robust_list_head is 24 bytes on x86_64
 * (3 * sizeof(void*) == 24). */
#define LINUX_ROBUST_LIST_HEAD_SIZE 24u

int64_t linux_set_tid_address(uint64_t tidptr);
int64_t linux_set_robust_list(uint64_t head, size_t len);

/* Test-only observation of the stored values. */
uint64_t linux_process_test_get_tid_address(void);
uint64_t linux_process_test_get_robust_list(void);
size_t   linux_process_test_get_robust_list_len(void);

/* -------- musl bring-up syscalls (sessao 20) ------------------- */

/* `getpid()` -- returns current task->pid. Linux semantics: never
 * fails. CapyOS today has 1 thread per process, so getpid==gettid;
 * when S1.4 thread groups land, this returns the *thread group
 * leader* tid (which is what Linux does). NULL ops -> 0. */
int64_t linux_getpid(void);

/* `getppid()` -- parent process id. CapyOS does not yet track
 * parent relationships; we return 1 (init) so userland code that
 * checks `getppid() == 1` to detect orphaned-by-init still works.
 * Future task-tree work will refine this. */
int64_t linux_getppid(void);

/* `getuid()` / `geteuid()` -- real / effective user id.
 * `getgid()` / `getegid()` -- real / effective group id.
 * CapyOS runs userland as root for now (no multi-user model); all
 * four return 0. Single-user OSes commonly do this. When we get a
 * cred model, these graduate to per-task storage. */
int64_t linux_getuid(void);
int64_t linux_geteuid(void);
int64_t linux_getgid(void);
int64_t linux_getegid(void);

/* `uname(struct utsname *)` -- system identification.
 *
 * Linux x86_64 utsname is 6 fields of 65 bytes each (390 bytes
 * total, NUL-terminated each). Userland (musl, glibc, JS shell,
 * Chromium) inspects:
 *   sysname  = "Linux"  (mandatory: programs key off this)
 *   nodename = "capyos" (host name)
 *   release  = "6.5.0-capyos"
 *   version  = "#1 SMP CapyOS"
 *   machine  = "x86_64"
 *   domainname = "(none)" (POSIX)
 *
 * Returns 0 on success, -LINUX_EFAULT if `uts` is NULL. */
#define LINUX_UTSNAME_FIELD 65

struct linux_utsname {
    char sysname   [LINUX_UTSNAME_FIELD];
    char nodename  [LINUX_UTSNAME_FIELD];
    char release   [LINUX_UTSNAME_FIELD];
    char version   [LINUX_UTSNAME_FIELD];
    char machine   [LINUX_UTSNAME_FIELD];
    char domainname[LINUX_UTSNAME_FIELD];
};

int64_t linux_uname(struct linux_utsname *uts);

/* Register the syscalls in the linux_syscall dispatcher. */
void linux_process_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_PROCESS_H */
