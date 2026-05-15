#include "kernel/linux_compat/linux_memfd.h"

/* Boot wiring for `linux_memfd`. Excluded from host tests.
 *
 * Only callback is `pid_exists`, used by pidfd_open to validate
 * the target pid before allocating a slot. Maps to
 * `task_by_pid != NULL`.
 */

#if !defined(UNIT_TEST)

#include "kernel/task.h"

#include <stdint.h>

static int wrap_pid_exists(uint32_t pid) {
    return task_by_pid(pid) != NULL ? 1 : 0;
}

void linux_memfd_init_boot(void) {
    static const struct linux_memfd_ops ops = {
        .pid_exists = wrap_pid_exists,
    };
    linux_memfd_install_ops(&ops);
}

#endif /* !UNIT_TEST */
