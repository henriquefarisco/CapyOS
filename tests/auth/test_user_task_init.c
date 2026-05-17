/*
 * Host tests for the M4 phase 8f.4 synthetic IRET frame builder.
 *
 * `user_task_arm_for_first_dispatch` lays out a 22-slot frame on
 * the task's kernel stack matching what `x64_exception_common`
 * would have left behind after pushing GP regs + vector +
 * error_code, with the IRET frame on top. We exercise the builder
 * with known-distinct values for user RIP and user RSP and walk
 * the synthesized frame slot-by-slot, asserting each slot matches
 * the documented layout.
 *
 * The trampoline `x64_user_first_dispatch` is x86_64 asm and only
 * its ADDRESS is observable from the host (tests/stub_vmm.c
 * provides a no-op stub of the same name); we assert the address
 * lands in t->context.rip but do not invoke the trampoline.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernel/task.h"
#include "kernel/syscall.h"
#include "kernel/user_task_init.h"

extern void x64_user_first_dispatch(void);

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-58s ", name);                                          \
    } while (0)
#define PASS()                                                             \
    do {                                                                   \
        printf("OK\n");                                                    \
        tests_passed++;                                                    \
    } while (0)
#define FAIL(msg)                                                          \
    do { printf("FAIL: %s\n", msg); } while (0)

#define KSTACK_SIZE 4096u

/* Helper: drive the builder against a freshly zeroed task struct
 * + freshly zeroed kernel stack. Returns the resulting `p` (the
 * lowest address of the synthesized frame, which should be the
 * value the builder wrote into t->context.rsp). */
static const uint64_t *arm_fresh(struct task *t, uint8_t *stack,
                                 uint64_t user_rip, uint64_t user_rsp) {
    for (size_t i = 0; i < sizeof(*t); ++i)
        ((uint8_t *)t)[i] = 0;
    for (size_t i = 0; i < KSTACK_SIZE; ++i) stack[i] = 0;

    t->kernel_stack = stack;
    t->kernel_stack_size = KSTACK_SIZE;

    user_task_arm_for_first_dispatch(t, user_rip, user_rsp);
    return (const uint64_t *)(uintptr_t)t->context.rsp;
}

static void test_iret_frame_slot_values(void) {
    struct task t;
    uint8_t *stack = (uint8_t *)malloc(KSTACK_SIZE);

    const uint64_t user_rip = 0x0000400000ULL;
    const uint64_t user_rsp = 0x00007FFFFFF00000ULL;

    const uint64_t *p = arm_fresh(&t, stack, user_rip, user_rsp);

    /* Layout offsets from p (PUSH_REGS top):
     *   p[0..14]  = 15 zeroed GP-reg slots (r15 first, rax last).
     *   p[15]     = vector       (0)
     *   p[16]     = error_code   (0)
     *   p[17]     = user RIP
     *   p[18]     = user CS      (0x23)
     *   p[19]     = user RFLAGS  (0x202)
     *   p[20]     = user RSP
     *   p[21]     = user SS      (0x1B)
     */

    TEST("synth frame: 15 PUSH_REGS slots are zero");
    int ok = 1;
    for (int i = 0; i < 15; ++i) {
        if (p[i] != 0u) { ok = 0; break; }
    }
    if (ok) PASS();
    else FAIL("PUSH_REGS slot non-zero");

    TEST("synth frame: vector slot (p[15]) == 0");
    if (p[15] == 0u) PASS();
    else FAIL("vector slot non-zero");

    TEST("synth frame: error_code slot (p[16]) == 0");
    if (p[16] == 0u) PASS();
    else FAIL("error_code slot non-zero");

    TEST("synth frame: user RIP slot (p[17]) matches arg");
    if (p[17] == user_rip) PASS();
    else FAIL("RIP slot mismatch");

    TEST("synth frame: user CS slot (p[18]) == 0x23");
    if (p[18] == (uint64_t)USER_TASK_USER_CS) PASS();
    else FAIL("CS slot mismatch");

    TEST("synth frame: user RFLAGS slot (p[19]) == 0x202");
    if (p[19] == (uint64_t)USER_TASK_USER_RFLAGS) PASS();
    else FAIL("RFLAGS slot mismatch");

    TEST("synth frame: user RSP slot (p[20]) matches arg");
    if (p[20] == user_rsp) PASS();
    else FAIL("RSP slot mismatch");

    TEST("synth frame: user SS slot (p[21]) == 0x1B");
    if (p[21] == (uint64_t)USER_TASK_USER_SS) PASS();
    else FAIL("SS slot mismatch");

    free(stack);
}

static void test_context_pointers(void) {
    struct task t;
    uint8_t *stack = (uint8_t *)malloc(KSTACK_SIZE);
    const uint64_t *p = arm_fresh(&t, stack, 0x400000ULL,
                                  0x7FFFFFFF00000ULL);

    /* The builder must write t->context.rsp to point at p (the
     * lowest address of the PUSH_REGS area). */
    TEST("context.rsp points at top of PUSH_REGS frame");
    if ((uint64_t)(uintptr_t)p == t.context.rsp) PASS();
    else FAIL("rsp not at frame top");

    TEST("context.rip == &x64_user_first_dispatch");
    if (t.context.rip ==
        (uint64_t)(uintptr_t)x64_user_first_dispatch) PASS();
    else FAIL("rip does not point at trampoline");

    TEST("context.rflags == USER_TASK_USER_RFLAGS (0x202)");
    if (t.context.rflags == (uint64_t)USER_TASK_USER_RFLAGS) PASS();
    else FAIL("rflags wrong");

    TEST("context.rbp == context.rsp");
    if (t.context.rbp == t.context.rsp) PASS();
    else FAIL("rbp not aligned with rsp");

    free(stack);
}

static void test_frame_total_size(void) {
    /* 22 slots * 8 bytes = 176 bytes. The frame top = stack top.
     * The frame bottom = top - 176. */
    struct task t;
    uint8_t *stack = (uint8_t *)malloc(KSTACK_SIZE);
    const uint64_t *p = arm_fresh(&t, stack, 0x1000ULL, 0x2000ULL);

    uint64_t expected_bottom =
        (uint64_t)(uintptr_t)(stack + KSTACK_SIZE) - 176u;

    TEST("synth frame bottom is exactly 176 bytes below stack top");
    if ((uint64_t)(uintptr_t)p == expected_bottom) PASS();
    else FAIL("frame size != 176 bytes");

    free(stack);
}

static void test_null_task_is_safe(void) {
    /* The builder must reject NULL gracefully: callers can sometimes
     * pass a NULL after a process_create failure and we never want
     * a NULL deref in this hot path. */
    user_task_arm_for_first_dispatch(NULL, 0x1000ULL, 0x2000ULL);
    TEST("NULL task argument is a no-op (no crash)");
    PASS();
}

static void test_too_small_stack_is_safe(void) {
    /* Frame is 176 bytes; a 100-byte stack should be rejected. */
    struct task t;
    uint8_t stack[100];
    for (size_t i = 0; i < sizeof(t); ++i) ((uint8_t *)&t)[i] = 0;
    t.kernel_stack = stack;
    t.kernel_stack_size = sizeof(stack);
    user_task_arm_for_first_dispatch(&t, 0x1000ULL, 0x2000ULL);

    TEST("undersized stack: builder leaves context untouched");
    if (t.context.rsp == 0u && t.context.rip == 0u) PASS();
    else FAIL("builder wrote to context on undersized stack");
}

/* M4 phase 8f.5: rank-passing variant. */

static void test_with_rax_writes_rank_into_rax_slot(void) {
    struct task t;
    uint8_t *stack = (uint8_t *)malloc(KSTACK_SIZE);

    for (size_t i = 0; i < sizeof(t); ++i) ((uint8_t *)&t)[i] = 0;
    for (size_t i = 0; i < KSTACK_SIZE; ++i) stack[i] = 0;
    t.kernel_stack = stack;
    t.kernel_stack_size = KSTACK_SIZE;

    user_task_arm_for_first_dispatch_with_rax(&t, 0xCAFE, 0xBEEF,
                                              0xA5A5);

    const uint64_t *p = (const uint64_t *)(uintptr_t)t.context.rsp;

    /* The RAX slot is at the HIGHEST address of the PUSH_REGS area
     * because PUSH_REGS pushes rax first (lowest stack address ==
     * top of the area is still the FIRST push, which after stack
     * growth-down ends up at the highest-numbered offset of the
     * PUSH_REGS region from the rsp). p[14] is rax. */
    TEST("with_rax: p[14] (rax slot) == initial_rax");
    if (p[14] == 0xA5A5u) PASS();
    else FAIL("rax slot did not receive initial_rax");

    /* All other PUSH_REGS slots remain zero. */
    int ok = 1;
    for (int i = 0; i < 14; ++i) {
        if (p[i] != 0u) { ok = 0; break; }
    }
    TEST("with_rax: all other 14 PUSH_REGS slots are zero");
    if (ok) PASS();
    else FAIL("non-rax PUSH_REGS slot leaked non-zero");

    /* IRET frame slots are unchanged from the base API. */
    TEST("with_rax: user RIP slot still matches arg");
    if (p[17] == 0xCAFEu) PASS();
    else FAIL("RIP slot regressed");

    TEST("with_rax: user RSP slot still matches arg");
    if (p[20] == 0xBEEFu) PASS();
    else FAIL("RSP slot regressed");

    free(stack);
}

static void test_base_api_writes_zero_into_rax_slot(void) {
    /* Locks the contract that the no-rank API still leaves RAX at
     * zero (capylibc's crt0 will forward zero to RDI, so main(0)
     * sees a default rank). */
    struct task t;
    uint8_t *stack = (uint8_t *)malloc(KSTACK_SIZE);

    for (size_t i = 0; i < sizeof(t); ++i) ((uint8_t *)&t)[i] = 0;
    for (size_t i = 0; i < KSTACK_SIZE; ++i) stack[i] = 0;
    t.kernel_stack = stack;
    t.kernel_stack_size = KSTACK_SIZE;

    user_task_arm_for_first_dispatch(&t, 0x1000ULL, 0x2000ULL);

    const uint64_t *p = (const uint64_t *)(uintptr_t)t.context.rsp;
    TEST("base API leaves RAX slot at zero (rank=0)");
    if (p[14] == 0u) PASS();
    else FAIL("base API leaked non-zero RAX");

    free(stack);
}

/* M5 phase A.5: fork-frame builder tests.
 *
 * `user_task_arm_for_fork(child, parent_frame)` is a thin wrapper
 * over the with_rax builder that sources user RIP/RSP from the
 * parent's kernel-side `struct syscall_frame` and pins RAX=0 so the
 * child sees the canonical "child branch" return value of fork().
 * These tests lock the wiring without depending on any asm path. */

static void fork_arm_fresh(struct task *t, uint8_t *stack,
                           const struct syscall_frame *parent) {
    for (size_t i = 0; i < sizeof(*t); ++i) ((uint8_t *)t)[i] = 0;
    for (size_t i = 0; i < KSTACK_SIZE; ++i) stack[i] = 0;
    t->kernel_stack = stack;
    t->kernel_stack_size = KSTACK_SIZE;
    user_task_arm_for_fork(t, parent);
}

static void test_fork_frame_inherits_parent_rip_rsp(void) {
    struct task t;
    uint8_t *stack = (uint8_t *)malloc(KSTACK_SIZE);
    struct syscall_frame parent = {0};
    parent.rip = 0x0000400ABCDULL;
    parent.rsp = 0x00007FFFFEED1000ULL;
    parent.r11 = 0x202;
    parent.rcx = parent.rip;

    fork_arm_fresh(&t, stack, &parent);
    const uint64_t *p = (const uint64_t *)(uintptr_t)t.context.rsp;

    TEST("fork frame: inherits parent RIP into IRET slot");
    if (p[17] == parent.rip) PASS();
    else FAIL("RIP slot did not match parent->rip");

    TEST("fork frame: inherits parent RSP into IRET slot");
    if (p[20] == parent.rsp) PASS();
    else FAIL("RSP slot did not match parent->rsp");

    TEST("fork frame: child RAX slot pinned to 0 (fork() == 0 in child)");
    if (p[14] == 0u) PASS();
    else FAIL("RAX slot should be 0 in the child");

    TEST("fork frame: non-RAX PUSH_REGS slots are zero");
    int ok = 1;
    for (int i = 0; i < 14; ++i) {
        if (p[i] != 0u) { ok = 0; break; }
    }
    if (ok) PASS();
    else FAIL("non-RAX PUSH_REGS slot leaked non-zero");

    TEST("fork frame: trampoline pointer stored in t.context.rip");
    if (t.context.rip == (uint64_t)(uintptr_t)x64_user_first_dispatch) PASS();
    else FAIL("context.rip not pointing at trampoline");

    free(stack);
}

static void test_fork_arm_null_inputs_are_safe(void) {
    struct task t;
    uint8_t *stack = (uint8_t *)malloc(KSTACK_SIZE);
    struct syscall_frame parent = {0};
    parent.rip = 0xAAAA;
    parent.rsp = 0xBBBB;

    /* NULL task: no crash, no writes. */
    user_task_arm_for_fork(NULL, &parent);
    TEST("fork builder: NULL task is a no-op");
    PASS();

    /* NULL parent_frame: no crash, no writes. */
    for (size_t i = 0; i < sizeof(t); ++i) ((uint8_t *)&t)[i] = 0;
    for (size_t i = 0; i < KSTACK_SIZE; ++i) stack[i] = 0;
    t.kernel_stack = stack;
    t.kernel_stack_size = KSTACK_SIZE;
    user_task_arm_for_fork(&t, NULL);
    TEST("fork builder: NULL parent frame is a no-op");
    if (t.context.rip == 0u && t.context.rsp == 0u) PASS();
    else FAIL("builder mutated context with NULL parent frame");

    free(stack);
}

int test_user_task_init_run(void) {
    printf("[test_user_task_init]\n");
    tests_run = 0;
    tests_passed = 0;
    test_iret_frame_slot_values();
    test_context_pointers();
    test_frame_total_size();
    test_null_task_is_safe();
    test_too_small_stack_is_safe();
    test_with_rax_writes_rank_into_rax_slot();
    test_base_api_writes_zero_into_rax_slot();
    test_fork_frame_inherits_parent_rip_rsp();
    test_fork_arm_null_inputs_are_safe();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
