#include "stub_arch_sched_hooks.h"
#include "kernel/arch_sched_hooks.h"

#include <stddef.h>
#include <stdint.h>

static uint32_t g_call_count;
static const struct task *g_last_target;

void arch_sched_apply_kernel_stack(const struct task *next) {
    g_call_count++;
    g_last_target = next;
}

uint32_t stub_arch_sched_hooks_call_count(void) {
    return g_call_count;
}

const struct task *stub_arch_sched_hooks_last_target(void) {
    return g_last_target;
}

void stub_arch_sched_hooks_log_clear(void) {
    g_call_count = 0;
    g_last_target = NULL;
}
