/*
 * Host stub for the x86_64 context_switch assembly seam.
 *
 * The real implementation lives in src/arch/x86_64/cpu/context_switch.S
 * and performs an actual register/RSP/CR3 swap. Host tests link against
 * this stub to exercise the C-side scheduler logic without running any
 * machine-specific instructions.
 *
 * The stub records every invocation in a small log so tests can assert
 * which task pairs were swapped, and in what order, without depending
 * on the actual swap semantics. The recorded entries are kept in a
 * fixed-size circular buffer; tests that need more entries should
 * call stub_context_switch_log_clear() between scenarios.
 */
#include <stddef.h>
#include <stdint.h>

#include "kernel/task.h"
#include "stub_context_switch.h"

#define STUB_CTX_LOG_MAX 64

static struct stub_context_switch_entry g_log[STUB_CTX_LOG_MAX];
static uint32_t g_log_count = 0u;
static uint32_t g_invocations = 0u;

void context_switch(struct task_context *old, struct task_context *new_ctx) {
    g_invocations++;
    if (g_log_count < STUB_CTX_LOG_MAX) {
        g_log[g_log_count].old_ctx = old;
        g_log[g_log_count].new_ctx = new_ctx;
        g_log_count++;
    }
}

uint32_t stub_context_switch_invocations(void) {
    return g_invocations;
}

uint32_t stub_context_switch_log_count(void) {
    return g_log_count;
}

const struct stub_context_switch_entry *stub_context_switch_log_at(
    uint32_t index) {
    if (index >= g_log_count) {
        return (const struct stub_context_switch_entry *)0;
    }
    return &g_log[index];
}

void stub_context_switch_log_clear(void) {
    g_log_count = 0u;
    g_invocations = 0u;
}
