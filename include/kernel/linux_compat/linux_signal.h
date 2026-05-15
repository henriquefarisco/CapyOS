#ifndef KERNEL_LINUX_COMPAT_LINUX_SIGNAL_H
#define KERNEL_LINUX_COMPAT_LINUX_SIGNAL_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI rt_sig* / sigaltstack shim (S1.12).
 *
 * Marco M1 surface (no actual signal *delivery* yet, just
 * storage):
 *
 *   rt_sigaction(signum, act, oact, sigsetsize)   -- install handler
 *   rt_sigprocmask(how, set, oldset, sigsetsize)  -- thread sigmask
 *   rt_sigreturn()                                -- IRET from handler
 *   sigaltstack(ss, old_ss)                       -- alt stack
 *
 * Why "storage only" is enough for Marco M1:
 *   - musl/glibc install SIGSEGV/SIGABRT/SIGCHLD/SIGPIPE handlers
 *     during startup. Syscalls that fail to store -> abort().
 *   - SpiderMonkey installs handlers for crash reporter +
 *     write-watch GC (alloc-on-write). Without storage the JS
 *     shell cannot start.
 *   - Actual delivery requires hardware fault paths (#PF, #UD)
 *     to unwind to user-mode handler frames -- that's S5
 *     territory. Until then, kernel exceptions panic; userland
 *     sees no SIGSEGV but no SpiderMonkey assert either.
 *
 * Layering: pure logic + injected per-task callback. Production
 * stores the action in a per-task table. Host tests use module-
 * local storage for a single fake task.
 */

/* Linux signal numbers (asm-generic/signal.h). Subset we name. */
#define LINUX_SIGHUP   1
#define LINUX_SIGINT   2
#define LINUX_SIGQUIT  3
#define LINUX_SIGILL   4
#define LINUX_SIGABRT  6
#define LINUX_SIGFPE   8
#define LINUX_SIGKILL  9
#define LINUX_SIGUSR1  10
#define LINUX_SIGSEGV  11
#define LINUX_SIGUSR2  12
#define LINUX_SIGPIPE  13
#define LINUX_SIGALRM  14
#define LINUX_SIGTERM  15
#define LINUX_SIGCHLD  17
#define LINUX_SIGCONT  18
#define LINUX_SIGSTOP  19

#define LINUX_NSIG 64u  /* Linux supports 1..64; sigset_t is 64 bits */

/* Signals that cannot be caught/blocked. */
#define LINUX_SIGNAL_UNCATCHABLE(s) \
    ((s) == LINUX_SIGKILL || (s) == LINUX_SIGSTOP)

/* sigaction.sa_flags bits we recognise. */
#define LINUX_SA_NOCLDSTOP 0x00000001u
#define LINUX_SA_NOCLDWAIT 0x00000002u
#define LINUX_SA_SIGINFO   0x00000004u
#define LINUX_SA_RESTART   0x10000000u
#define LINUX_SA_NODEFER   0x40000000u
#define LINUX_SA_RESETHAND 0x80000000u
#define LINUX_SA_RESTORER  0x04000000u
#define LINUX_SA_ONSTACK   0x08000000u

#define LINUX_SA_KNOWN_FLAGS \
    (LINUX_SA_NOCLDSTOP | LINUX_SA_NOCLDWAIT | LINUX_SA_SIGINFO | \
     LINUX_SA_RESTART | LINUX_SA_NODEFER | LINUX_SA_RESETHAND | \
     LINUX_SA_RESTORER | LINUX_SA_ONSTACK)

/* Linux struct sigaction (asm-generic/signal.h on x86_64). The
 * layout differs by arch; this matches the Linux 6.x x86_64
 * uapi. */
struct linux_sigaction {
    uint64_t sa_handler;   /* function pointer or SIG_DFL/SIG_IGN */
    uint64_t sa_flags;
    uint64_t sa_restorer;
    uint64_t sa_mask;      /* 64-bit signal mask */
};

/* Linux struct sigaltstack. */
struct linux_stack_t {
    uint64_t ss_sp;
    int32_t  ss_flags;
    uint64_t ss_size;
};

/* Special handlers (from asm-generic/signal-defs.h). */
#define LINUX_SIG_DFL ((uint64_t)0)
#define LINUX_SIG_IGN ((uint64_t)1)

/* sigprocmask `how` constants. */
#define LINUX_SIG_BLOCK   0
#define LINUX_SIG_UNBLOCK 1
#define LINUX_SIG_SETMASK 2

/* sigaltstack ss_flags. */
#define LINUX_SS_ONSTACK   1
#define LINUX_SS_DISABLE   2
#define LINUX_SS_AUTODISARM 0x80000000

void linux_signal_reset_for_tests(void);

/* Core entries. Returns 0 on success or -LINUX_E*. */
int64_t linux_rt_sigaction(int signum,
                           const struct linux_sigaction *act,
                           struct linux_sigaction *oact,
                           size_t sigsetsize);

int64_t linux_rt_sigprocmask(int how, uint64_t set_ptr,
                             uint64_t oldset_ptr,
                             size_t sigsetsize);

/* rt_sigreturn is invoked by userland to return from a signal
 * handler. Without delivery infra it never legitimately fires
 * during Marco M1; we surface -ENOSYS. */
int64_t linux_rt_sigreturn(void);

int64_t linux_sigaltstack(const struct linux_stack_t *ss,
                          struct linux_stack_t *old_ss);

/* Test-only observation. */
const struct linux_sigaction *
linux_signal_test_get_action(int signum);
uint64_t linux_signal_test_get_mask(void);
const struct linux_stack_t *linux_signal_test_get_altstack(void);

void linux_signal_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_SIGNAL_H */
