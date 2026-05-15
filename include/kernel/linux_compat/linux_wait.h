#ifndef KERNEL_LINUX_COMPAT_LINUX_WAIT_H
#define KERNEL_LINUX_COMPAT_LINUX_WAIT_H

/* Linux ABI `wait4(2)` and `waitid(2)` -- block-and-collect on
 * a child process.
 *
 *   pid_t wait4(pid_t pid, int *wstatus,
 *               int options, struct rusage *rusage);
 *   int   waitid(idtype_t idtype, id_t id,
 *               siginfo_t *infop, int options);
 *
 * Marco M1 has no `clone()` thread groups and no fork() with a
 * separate child process visible to userland (capybrowser
 * spawns a kernel-managed engine but it isn't a child of the
 * caller in POSIX terms). Per Linux semantics, when a process
 * calls `wait4` and has no children to reap, the syscall must
 * return -ECHILD. Userland (musl, glibc, busybox, popen()) all
 * handle ECHILD gracefully -- it's the documented "no children
 * to wait for" answer, not an error condition.
 *
 * Returning -ECHILD instead of -ENOSYS lets shells and
 * `system()` callers detect that there are no children without
 * aborting. The alternative (-ENOSYS) makes musl `popen()`
 * fail-fast, which we don't want.
 *
 * When task_clone_thread + child-process tracking land, this
 * module hooks into the actual wait queue. */

#include <stdint.h>
#include <stddef.h>

/* options bits we recognise (Linux). */
#define LINUX_WNOHANG     0x00000001
#define LINUX_WUNTRACED   0x00000002
#define LINUX_WSTOPPED    0x00000002 /* alias of WUNTRACED */
#define LINUX_WEXITED     0x00000004
#define LINUX_WCONTINUED  0x00000008
#define LINUX_WNOWAIT     0x01000000
#define LINUX_WAIT_KNOWN_FLAGS \
    (LINUX_WNOHANG | LINUX_WUNTRACED | LINUX_WEXITED | \
     LINUX_WCONTINUED | LINUX_WNOWAIT)

/* idtype values for waitid */
#define LINUX_P_ALL    0
#define LINUX_P_PID    1
#define LINUX_P_PGID   2
#define LINUX_P_PIDFD  3

int64_t linux_wait4(int32_t pid, int *wstatus,
                    int options, void *rusage);
int64_t linux_waitid(int idtype, int32_t id,
                     void *infop, int options);

void linux_wait_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_WAIT_H */
