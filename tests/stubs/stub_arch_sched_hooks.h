#ifndef TESTS_STUB_ARCH_SCHED_HOOKS_H
#define TESTS_STUB_ARCH_SCHED_HOOKS_H

/*
 * Host stub for `arch_sched_apply_kernel_stack` (M4 phase 8f.2).
 *
 * The real x86_64 implementation in
 * src/arch/x86_64/arch_sched_hooks.c uses cpu_local + TSS globals
 * that the host build cannot reach. The stub here records every
 * invocation in a tiny ring buffer so tests/test_context_switch.c
 * can lock the contract: arch_sched_apply_kernel_stack must be
 * called for every successful schedule()-driven swap, with the
 * about-to-run task as its argument.
 */

#include <stddef.h>
#include <stdint.h>

struct task;

/* How many invocations have happened since the last log_clear(). */
uint32_t stub_arch_sched_hooks_call_count(void);

/* Pointer to the most recent target task (NULL if no calls yet). */
const struct task *stub_arch_sched_hooks_last_target(void);

/* Reset the log to a clean state. */
void stub_arch_sched_hooks_log_clear(void);

#endif /* TESTS_STUB_ARCH_SCHED_HOOKS_H */
