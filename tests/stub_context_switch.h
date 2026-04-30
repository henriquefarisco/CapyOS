/*
 * Public surface of the host stub for the x86_64 context_switch seam.
 * Used by tests/test_context_switch.c to assert that the real
 * scheduler performs the expected swaps.
 */
#ifndef TESTS_STUB_CONTEXT_SWITCH_H
#define TESTS_STUB_CONTEXT_SWITCH_H

#include <stdint.h>

struct task_context;

struct stub_context_switch_entry {
    struct task_context *old_ctx;
    struct task_context *new_ctx;
};

uint32_t stub_context_switch_invocations(void);
uint32_t stub_context_switch_log_count(void);
const struct stub_context_switch_entry *stub_context_switch_log_at(
    uint32_t index);
void stub_context_switch_log_clear(void);

#endif /* TESTS_STUB_CONTEXT_SWITCH_H */
