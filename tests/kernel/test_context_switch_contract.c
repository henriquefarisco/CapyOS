/* C/assembly context layout and instruction-order contracts. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel/task.h"

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
    do {                                                                   \
        printf("FAIL: %s\n", msg);                                         \
    } while (0)

/* -------------------- 1. Layout invariants ----------------------------- */

static void test_layout_locks_asm_contract(void) {
    TEST("sizeof(struct task_context) == 0x250");
    if (sizeof(struct task_context) == 0x250u) PASS();
    else FAIL("size drift between C and asm");

    TEST("offsetof(rsp) == 0x00");
    if (offsetof(struct task_context, rsp) == 0x00u) PASS();
    else FAIL("rsp offset drift");

    TEST("offsetof(rbp) == 0x08");
    if (offsetof(struct task_context, rbp) == 0x08u) PASS();
    else FAIL("rbp offset drift");

    TEST("offsetof(rbx) == 0x10");
    if (offsetof(struct task_context, rbx) == 0x10u) PASS();
    else FAIL("rbx offset drift");

    TEST("offsetof(r12) == 0x18");
    if (offsetof(struct task_context, r12) == 0x18u) PASS();
    else FAIL("r12 offset drift");

    TEST("offsetof(r13) == 0x20");
    if (offsetof(struct task_context, r13) == 0x20u) PASS();
    else FAIL("r13 offset drift");

    TEST("offsetof(r14) == 0x28");
    if (offsetof(struct task_context, r14) == 0x28u) PASS();
    else FAIL("r14 offset drift");

    TEST("offsetof(r15) == 0x30");
    if (offsetof(struct task_context, r15) == 0x30u) PASS();
    else FAIL("r15 offset drift");

    TEST("offsetof(rip) == 0x38");
    if (offsetof(struct task_context, rip) == 0x38u) PASS();
    else FAIL("rip offset drift");

    TEST("offsetof(rflags) == 0x40");
    if (offsetof(struct task_context, rflags) == 0x40u) PASS();
    else FAIL("rflags offset drift");

    TEST("offsetof(cr3) == 0x48");
    if (offsetof(struct task_context, cr3) == 0x48u) PASS();
    else FAIL("cr3 offset drift");

    TEST("offsetof(fx_state) == 0x50 and is 16-byte aligned");
    if (offsetof(struct task_context, fx_state) == 0x50u &&
        _Alignof(struct task_context) >= 16u) PASS();
    else FAIL("FXSAVE area offset/alignment drift");
}

/* The host scheduler uses stub_context_switch.c, so runtime C tests alone
 * cannot catch an instruction-order regression in the real assembly.  Lock
 * the source contract that matters here: original RFLAGS must be pushed
 * before cli, then popped and committed to old->rflags before any swap. */
static void test_asm_captures_rflags_before_cli(void) {
    char source[16384];
    FILE *f = fopen("src/arch/x86_64/cpu/context_switch.S", "rb");
    if (!f) f = fopen("../../src/arch/x86_64/cpu/context_switch.S", "rb");

    TEST("context_switch asm source is available for contract check");
    if (!f) {
        FAIL("cannot open context_switch.S");
        return;
    }
    size_t n = fread(source, 1, sizeof(source) - 1u, f);
    fclose(f);
    source[n] = '\0';
    PASS();

    const char *entry = strstr(source, "\ncontext_switch:\n");
    const char *push = entry ? strstr(entry, "\n    pushfq\n") : NULL;
    const char *cli = entry ? strstr(entry, "\n    cli\n") : NULL;
    const char *pop = entry ? strstr(entry, "\n    popq %rax\n") : NULL;
    const char *save = entry ? strstr(entry,
        "\n    movq %rax, 0x40(%rdi)\n") : NULL;
    const char *first_register_save = entry ? strstr(entry,
        "\n    movq %rsp, 0x00(%rdi)\n") : NULL;

    TEST("context_switch captures RFLAGS before cli and before context swap");
    if (entry && push && cli && pop && save && first_register_save &&
        push < cli && cli < pop && pop < save && save < first_register_save) {
        PASS();
    } else {
        FAIL("expected pushfq -> cli -> popq -> old.rflags ordering");
    }

    const char *fxsave = entry ? strstr(entry,
        "\n    fxsave64 0x50(%rdi)\n") : NULL;
    const char *fxrestore = entry ? strstr(entry,
        "\n    fxrstor64 0x50(%rsi)\n") : NULL;
    TEST("context_switch saves old and restores new FP/SIMD state");
    if (fxsave && fxrestore && save < fxsave && fxsave < fxrestore)
        PASS();
    else FAIL("missing or misordered task-local FXSAVE/FXRSTOR");

    const char *frame_rsp_load = entry ?
        strstr(entry, "\n    movq 0x00(%rsi), %rdx\n") : NULL;
    const char *frame_ss = entry ? strstr(entry, "\n    pushq $0x10\n") : NULL;
    const char *frame_rsp = entry ? strstr(entry, "\n    pushq %rdx\n") : NULL;
    const char *frame_flags = entry ? strstr(entry,
        "\n    pushq 0x40(%rsi)\n") : NULL;
    const char *frame_cs = entry ? strstr(entry,
        "\n    pushq $0x08\n") : NULL;
    const char *frame_rip = entry ? strstr(entry,
        "\n    pushq 0x38(%rsi)\n") : NULL;
    const char *iret = entry ? strstr(entry, "\n    iretq\n") : NULL;
    const char *resume = entry ? strstr(entry, "\n1:\n") : NULL;
    const char *early_popfq = entry ? strstr(entry, "\n    popfq\n") : NULL;

    TEST("context_switch restores full AMD64 frame and IF atomically");
    if (frame_rsp_load && frame_ss && frame_rsp && frame_flags && frame_cs &&
        frame_rip && iret && resume && frame_rsp_load < frame_ss &&
        frame_ss < frame_rsp && frame_rsp < frame_flags &&
        frame_flags < frame_cs && frame_cs < frame_rip &&
        frame_rip < iret && iret < resume &&
        (!early_popfq || early_popfq > resume)) {
        PASS();
    } else {
        FAIL("IRETQ needs SS/RSP/RFLAGS/CS/RIP in reverse push order");
    }

    const char *first = strstr(source, "\ncontext_switch_into_first:\n");
    const char *first_rsp_load = first ?
        strstr(first, "\n    movq 0x00(%rdi), %rdx\n") : NULL;
    const char *first_ss = first ? strstr(first, "\n    pushq $0x10\n") : NULL;
    const char *first_rsp = first ? strstr(first, "\n    pushq %rdx\n") : NULL;
    const char *first_flags = first ? strstr(first,
        "\n    pushq 0x40(%rdi)\n") : NULL;
    const char *first_cs = first ? strstr(first,
        "\n    pushq $0x08\n") : NULL;
    const char *first_rip = first ? strstr(first,
        "\n    pushq 0x38(%rdi)\n") : NULL;
    const char *first_iret = first ? strstr(first, "\n    iretq\n") : NULL;
    const char *first_popfq = first ? strstr(first, "\n    popfq\n") : NULL;
    const char *first_fxrestore = first ? strstr(first,
        "\n    fxrstor64 0x50(%rdi)\n") : NULL;

    TEST("first task dispatch also restores the full AMD64 frame");
    if (first_rsp_load && first_ss && first_rsp && first_flags && first_cs &&
        first_rip && first_iret && first_rsp_load < first_ss &&
        first_ss < first_rsp && first_rsp < first_flags &&
        first_flags < first_cs && first_cs < first_rip &&
        first_rip < first_iret && first_popfq == NULL && first_fxrestore &&
        first_fxrestore < first_rsp_load) {
        PASS();
    } else {
        FAIL("first dispatch must build all five IRETQ fields");
    }
}

static void test_direct_user_entry_sets_rank_zero(void) {
    char source[4096];
    FILE *f = fopen("src/arch/x86_64/cpu/user_mode_entry.S", "rb");
    if (!f) f = fopen("../../src/arch/x86_64/cpu/user_mode_entry.S", "rb");

    TEST("direct user entry source is available for rank contract");
    if (!f) {
        FAIL("cannot open user_mode_entry.S");
        return;
    }
    size_t n = fread(source, 1, sizeof(source) - 1u, f);
    fclose(f);
    source[n] = '\0';
    PASS();

    const char *entry = strstr(source, "\nenter_user_mode:\n");
    const char *clear_rax = entry ? strstr(entry, "\n    xorl %eax, %eax\n") : NULL;
    const char *iret = entry ? strstr(entry, "\n    iretq\n") : NULL;
    TEST("direct user entry clears RAX before IRETQ (rank=0)");
    if (clear_rax && iret && clear_rax < iret) PASS();
    else FAIL("direct entry can leak a non-zero rank to crt0");
}

int test_context_switch_contract_run(void) {
    printf("[test_context_switch_contract]\n");
    tests_run = 0;
    tests_passed = 0;
    test_layout_locks_asm_contract();
    test_asm_captures_rflags_before_cli();
    test_direct_user_entry_sets_rank_zero();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
