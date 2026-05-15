#ifndef KERNEL_LINUX_COMPAT_LINUX_PRIORITY_H
#define KERNEL_LINUX_COMPAT_LINUX_PRIORITY_H

/* Linux ABI `getpriority(2)` and `setpriority(2)` -- nice value
 * inspection and adjustment.
 *
 *   int getpriority(int which, int who);
 *   int setpriority(int which, int who, int prio);
 *
 * Linux quirk: getpriority returns the nice value encoded as
 * (20 - nice). A nice value of 0 (default) returns 20; nice
 * +19 (lowest priority) returns 1; nice -20 (highest) returns
 * 40. -1 is a valid encoding so callers must clear errno before
 * the call to distinguish error from a nice of +20 (return -1).
 *
 * Marco M1 has no per-task scheduling priority yet. We accept
 * `which` in {PRIO_PROCESS, PRIO_PGRP, PRIO_USER} and
 * `who == 0` (= self / own group / own user) and answer with a
 * stored nice value (default 0 = encoded 20). setpriority
 * updates the stored value after clamping to [-20, +19].
 *
 * Permission semantics: Linux requires CAP_SYS_NICE to lower
 * the nice value (raise priority). Marco M1 runs as effective
 * root, so all setpriority calls succeed. */

#include <stdint.h>

#define LINUX_PRIO_PROCESS  0
#define LINUX_PRIO_PGRP     1
#define LINUX_PRIO_USER     2

#define LINUX_NICE_MIN  (-20)
#define LINUX_NICE_MAX  (19)

int64_t linux_getpriority(int which, int who);
int64_t linux_setpriority(int which, int who, int prio);

void linux_priority_register_syscalls(void);
void linux_priority_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_PRIORITY_H */
