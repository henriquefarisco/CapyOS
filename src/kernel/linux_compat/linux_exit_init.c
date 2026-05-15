#include "kernel/linux_compat/linux_exit.h"

/* Boot wiring for `linux_exit` against the real CapyOS task
 * subsystem. Excluded from host tests via UNIT_TEST.
 *
 * `task_exit(code)` is declared `noreturn` in `kernel/task.h`;
 * it removes the calling task from the scheduler queue, marks
 * it ZOMBIE/DEAD, and yields. From userland's perspective the
 * syscall never returns. */

#if !defined(UNIT_TEST)

#include "kernel/task.h"

static void wrap_exit_task(int code) {
    task_exit(code);
}

void linux_exit_init_boot(void) {
    static const struct linux_exit_ops ops = {
        .exit_task = wrap_exit_task,
    };
    linux_exit_install_ops(&ops);
}

#else /* UNIT_TEST */

void linux_exit_init_boot(void) {}

#endif
