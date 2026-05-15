#include "kernel/linux_compat/linux_process.h"

/* Boot wiring for `linux_process` against the real kernel task
 * system. Excluded from host tests (which inject their own fake
 * task views).
 *
 * Accessor trampolines erase the concrete `struct task *` into the
 * opaque `linux_task_t *` so the module never depends on the
 * scheduler headers.
 */

#if !defined(UNIT_TEST)

#include "kernel/task.h"

#include <stddef.h>
#include <stdint.h>

static linux_task_t *ops_current(void) {
    return (linux_task_t *)task_current();
}

static linux_task_t *ops_by_pid(uint32_t pid) {
    return (linux_task_t *)task_by_pid(pid);
}

static int ops_view(linux_task_t *t, struct linux_task_view *out) {
    if (!t || !out) return -1;
    struct task *k = (struct task *)t;
    out->pid      = k->pid;
    out->name     = k->name;
    out->name_cap = TASK_NAME_MAX;
    return 0;
}

static void ops_yield(void) {
    task_yield();
}

void linux_process_init_boot(void) {
    static const struct linux_process_ops ops = {
        .current = ops_current,
        .by_pid  = ops_by_pid,
        .view    = ops_view,
        .yield   = ops_yield,
    };
    linux_process_install_ops(&ops);
}

#endif /* !UNIT_TEST */
