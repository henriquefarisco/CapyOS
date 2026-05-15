#include "kernel/linux_compat/linux_signal.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

/* Module-local storage. When per-task signals land (S1.4 clone
 * + per-task table), this migrates to the task struct.
 *
 * Index 0 is unused (signals are 1..NSIG). */
static struct linux_sigaction g_actions[LINUX_NSIG + 1];
static uint64_t               g_signal_mask;
static struct linux_stack_t   g_altstack;

void linux_signal_reset_for_tests(void) {
    for (size_t i = 0; i <= LINUX_NSIG; i++) {
        g_actions[i].sa_handler = LINUX_SIG_DFL;
        g_actions[i].sa_flags   = 0;
        g_actions[i].sa_restorer = 0;
        g_actions[i].sa_mask    = 0;
    }
    g_signal_mask = 0;
    g_altstack    = (struct linux_stack_t){0};
}

/* ---------- rt_sigaction ---------- */

int64_t linux_rt_sigaction(int signum,
                           const struct linux_sigaction *act,
                           struct linux_sigaction *oact,
                           size_t sigsetsize) {
    /* Linux x86_64 sigsetsize is sizeof(sigset_t) == 8. */
    if (sigsetsize != 8) return -LINUX_EINVAL;
    if (signum <= 0 || signum > (int)LINUX_NSIG) return -LINUX_EINVAL;
    if (LINUX_SIGNAL_UNCATCHABLE(signum)) return -LINUX_EINVAL;

    if (oact) *oact = g_actions[signum];

    if (act) {
        /* Reject unknown flag bits. */
        if (act->sa_flags & ~(uint64_t)LINUX_SA_KNOWN_FLAGS) {
            return -LINUX_EINVAL;
        }
        g_actions[signum] = *act;
    }
    return 0;
}

/* ---------- rt_sigprocmask ---------- */

int64_t linux_rt_sigprocmask(int how, uint64_t set_ptr,
                             uint64_t oldset_ptr,
                             size_t sigsetsize) {
    if (sigsetsize != 8) return -LINUX_EINVAL;

    if (oldset_ptr != 0) {
        *(uint64_t *)(uintptr_t)oldset_ptr = g_signal_mask;
    }
    if (set_ptr == 0) return 0;  /* "query only" form */

    uint64_t set = *(const uint64_t *)(uintptr_t)set_ptr;

    /* SIGKILL/SIGSTOP can never be blocked; mask them out. */
    set &= ~((uint64_t)1 << (LINUX_SIGKILL - 1));
    set &= ~((uint64_t)1 << (LINUX_SIGSTOP - 1));

    switch (how) {
        case LINUX_SIG_BLOCK:   g_signal_mask |= set;  return 0;
        case LINUX_SIG_UNBLOCK: g_signal_mask &= ~set; return 0;
        case LINUX_SIG_SETMASK: g_signal_mask  = set;  return 0;
        default:                return -LINUX_EINVAL;
    }
}

/* ---------- rt_sigreturn ---------- */

int64_t linux_rt_sigreturn(void) {
    /* No signal delivery infra yet; rt_sigreturn cannot have been
     * legitimately invoked. Return -ENOSYS so musl panics if it
     * somehow lands here (would indicate a kernel bug). */
    return -LINUX_ENOSYS;
}

/* ---------- sigaltstack ---------- */

int64_t linux_sigaltstack(const struct linux_stack_t *ss,
                          struct linux_stack_t *old_ss) {
    if (old_ss) *old_ss = g_altstack;

    if (ss) {
        /* Linux requires ss_size >= MINSIGSTKSZ (2048 on x86_64) when
         * not disabling. */
        if (!(ss->ss_flags & LINUX_SS_DISABLE) && ss->ss_size < 2048) {
            return -LINUX_ENOMEM;
        }
        g_altstack = *ss;
    }
    return 0;
}

const struct linux_sigaction *linux_signal_test_get_action(int signum) {
    if (signum <= 0 || signum > (int)LINUX_NSIG) return NULL;
    return &g_actions[signum];
}
uint64_t linux_signal_test_get_mask(void) { return g_signal_mask; }
const struct linux_stack_t *linux_signal_test_get_altstack(void) {
    return &g_altstack;
}

/* ---------- Syscall adapters ---------- */

static int64_t sys_rt_sigaction(const struct linux_syscall_args *a) {
    return linux_rt_sigaction((int)a->a0,
                              (const struct linux_sigaction *)(uintptr_t)a->a1,
                              (struct linux_sigaction *)(uintptr_t)a->a2,
                              (size_t)a->a3);
}

static int64_t sys_rt_sigprocmask(const struct linux_syscall_args *a) {
    return linux_rt_sigprocmask((int)a->a0, a->a1, a->a2, (size_t)a->a3);
}

static int64_t sys_rt_sigreturn(const struct linux_syscall_args *a) {
    (void)a;
    return linux_rt_sigreturn();
}

static int64_t sys_sigaltstack(const struct linux_syscall_args *a) {
    return linux_sigaltstack((const struct linux_stack_t *)(uintptr_t)a->a0,
                             (struct linux_stack_t *)(uintptr_t)a->a1);
}

void linux_signal_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_rt_sigaction,    sys_rt_sigaction);
    (void)linux_syscall_register(LINUX_NR_rt_sigprocmask,  sys_rt_sigprocmask);
    (void)linux_syscall_register(LINUX_NR_rt_sigreturn,    sys_rt_sigreturn);
    (void)linux_syscall_register(LINUX_NR_sigaltstack,     sys_sigaltstack);
}
