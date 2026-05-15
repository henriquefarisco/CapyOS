#ifndef KERNEL_LINUX_COMPAT_LINUX_SCHED_PRIO_H
#define KERNEL_LINUX_COMPAT_LINUX_SCHED_PRIO_H

/* Linux ABI scheduler-policy + priority syscalls.
 *
 *   int sched_setscheduler(pid_t pid, int policy,
 *                          const struct sched_param *param);
 *   int sched_getscheduler(pid_t pid);
 *   int sched_setparam    (pid_t pid, const struct sched_param *param);
 *   int sched_getparam    (pid_t pid, struct sched_param *param);
 *   int sched_get_priority_max(int policy);
 *   int sched_get_priority_min(int policy);
 *
 * Why this matters for the Firefox port:
 *   - Firefox audio thread queries
 *     `sched_get_priority_max(SCHED_FIFO)` to know the priority
 *     ceiling before bumping itself; -ENOSYS makes the audio
 *     thread stay at default priority and leads to glitches
 *     under load.
 *   - Firefox compositor uses `sched_setscheduler(SCHED_FIFO)`
 *     to elevate its own priority on Linux desktops; the call
 *     is best-effort and we accept-but-no-op on Marco M1.
 *   - musl `pthread_getschedparam` reads via sched_getscheduler
 *     and sched_getparam.
 *
 * Linux x86_64 layout:
 *
 *   struct sched_param { int sched_priority; };
 *
 * Linux scheduling policies:
 *   SCHED_OTHER    0   normal time-sharing
 *   SCHED_FIFO     1   real-time first-in-first-out
 *   SCHED_RR       2   real-time round-robin
 *   SCHED_BATCH    3   batch (low priority)
 *   SCHED_IDLE     5   idle (very low priority)
 *   SCHED_DEADLINE 6   EDF/CBS deadline scheduler
 *
 * Priority ranges (Linux defaults):
 *   SCHED_OTHER/BATCH/IDLE: priority must be 0
 *   SCHED_FIFO/RR:          priority [1, 99]
 *   SCHED_DEADLINE:         priority must be 0 (uses runtime/deadline)
 *
 * Marco M1 has a single cooperative scheduler that doesn't
 * model priority bands. We faithfully report Linux constants
 * and accept setscheduler/setparam as no-ops (returning 0)
 * when the policy and priority are well-formed. This lets
 * Firefox / pthread continue along its happy path. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_SCHED_OTHER     0
#define LINUX_SCHED_FIFO      1
#define LINUX_SCHED_RR        2
#define LINUX_SCHED_BATCH     3
#define LINUX_SCHED_IDLE      5
#define LINUX_SCHED_DEADLINE  6

#define LINUX_SCHED_RT_MIN_PRIO  1
#define LINUX_SCHED_RT_MAX_PRIO  99

struct linux_sched_param {
    int32_t sched_priority;
};

int linux_sched_policy_known(int policy);
int linux_sched_priority_valid(int policy, int prio);

int64_t linux_sched_get_priority_max(int policy);
int64_t linux_sched_get_priority_min(int policy);
int64_t linux_sched_setscheduler(int pid, int policy,
                                 const struct linux_sched_param *param);
int64_t linux_sched_getscheduler(int pid);
int64_t linux_sched_setparam(int pid, const struct linux_sched_param *param);
int64_t linux_sched_getparam(int pid, struct linux_sched_param *param);

void linux_sched_prio_register_syscalls(void);
void linux_sched_prio_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_SCHED_PRIO_H */
