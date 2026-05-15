#ifndef KERNEL_LINUX_COMPAT_LINUX_UMASK_H
#define KERNEL_LINUX_COMPAT_LINUX_UMASK_H

/* Linux ABI `umask(2)` -- file mode creation mask.
 *
 *   mode_t umask(mode_t mask);
 *
 * Atomically sets the calling process's umask to `mask & 0777`
 * and returns the previous value. Always succeeds.
 *
 * Marco M1 stores the mask module-locally (not per-process).
 * When per-process state lands, this becomes a `task->umask`
 * field; the API stays the same.
 *
 * Default Linux umask is 0022 (deny group/other write). musl's
 * `__init_libc` reads the current umask via the syscall to
 * record it before any open(O_CREAT) calls, so the value matters
 * even though we don't honour it during file creation yet (the
 * tmpfs/devfs backends ignore mode bits in Marco M1). */

#include <stdint.h>

#define LINUX_UMASK_MASK_BITS 0777u

uint32_t linux_umask(uint32_t mask);

void linux_umask_register_syscalls(void);
void linux_umask_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_UMASK_H */
