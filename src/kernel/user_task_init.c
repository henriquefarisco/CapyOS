/*
 * User task synthetic IRET frame builder (M4 phase 8f.4).
 *
 * See include/kernel/user_task_init.h for the public contract and
 * tests/test_user_task_init.c for the byte-layout regression
 * guard. The builder is pure C: no inline asm, no globals.
 *
 * Frame layout (built top-down from the kernel stack top):
 *
 *     +---- highest addr (kernel_stack + kernel_stack_size)
 *     | user SS         (0x1B)
 *     | user RSP        (caller-supplied)
 *     | user RFLAGS     (0x202 = IF=1, IOPL=0)
 *     | user CS         (0x23)
 *     | user RIP        (caller-supplied)
 *     | error_code      (0)
 *     | vector          (0)
 *     | rax = 0
 *     | rbx = 0
 *     | rcx = 0
 *     | rdx = 0
 *     | rbp = 0
 *     | rsi = 0
 *     | rdi = 0
 *     | r8  = 0
 *     | r9  = 0
 *     | r10 = 0
 *     | r11 = 0
 *     | r12 = 0
 *     | r13 = 0
 *     | r14 = 0
 *     | r15 = 0  <- t->context.rsp points here (top of PUSH_REGS frame)
 *     +----
 *
 * The 15-register PUSH_REGS area mirrors the order written by the
 * macro in src/arch/x86_64/cpu/interrupts_asm.S. POP_REGS in the
 * trampoline pops r15 first, so r15 must be the LOWEST address in
 * the PUSH_REGS area (i.e. the slot t->context.rsp points at).
 */

#include "kernel/user_task_init.h"
#include "kernel/task.h"

#include <stddef.h>
#include <stdint.h>

extern void x64_user_first_dispatch(void);

#define USER_TASK_PUSH_REGS_SLOTS 15u
#define USER_TASK_VECTOR_ERR_SLOTS 2u  /* vector + error_code */
#define USER_TASK_IRET_SLOTS 5u        /* RIP, CS, RFLAGS, RSP, SS */

void user_task_arm_for_first_dispatch_with_rax(struct task *t,
                                               uint64_t user_rip,
                                               uint64_t user_rsp,
                                               uint64_t initial_rax) {
    if (!t || !t->kernel_stack || t->kernel_stack_size < (uint64_t)
            (USER_TASK_PUSH_REGS_SLOTS + USER_TASK_VECTOR_ERR_SLOTS +
             USER_TASK_IRET_SLOTS) * sizeof(uint64_t)) {
        return;
    }

    /* Top of the stack is one past the last byte; the first slot we
     * write lives at top-8 (highest valid 8-byte slot). */
    uint64_t *p = (uint64_t *)(void *)(t->kernel_stack +
                                       t->kernel_stack_size);

    /* IRET frame, top-down (the CPU pops bottom-up: RIP first). */
    *--p = (uint64_t)USER_TASK_USER_SS;
    *--p = user_rsp;
    *--p = (uint64_t)USER_TASK_USER_RFLAGS;
    *--p = (uint64_t)USER_TASK_USER_CS;
    *--p = user_rip;

    /* Vector + error code that the IRQ stub would have pushed.
     * Both are zero for a synthetic dispatch (no real exception). */
    *--p = 0u; /* error_code */
    *--p = 0u; /* vector */

    /* PUSH_REGS area, written in PUSH order (rax first, r15 last).
     * The macro in interrupts_asm.S pushes rax first which means
     * its slot lives at the HIGHEST address of the PUSH_REGS area
     * (stack grows down). We decrement-then-write, so the FIRST
     * write below targets the rax slot. POP_REGS pops r15 first
     * (lowest address), rax last (highest address), so on entry to
     * ring 3 RAX equals `initial_rax` and every other GP reg is
     * zero. capylibc's crt0 forwards RAX to RDI, giving main() a
     * one-arg "rank" channel without a full argv frame. */
    *--p = initial_rax; /* rax slot (PUSH_REGS pushes rax FIRST -> highest addr) */
    *--p = 0u;          /* rbx */
    *--p = 0u;          /* rcx */
    *--p = 0u;          /* rdx */
    *--p = 0u;          /* rbp */
    *--p = 0u;          /* rsi */
    *--p = 0u;          /* rdi */
    *--p = 0u;          /* r8  */
    *--p = 0u;          /* r9  */
    *--p = 0u;          /* r10 */
    *--p = 0u;          /* r11 */
    *--p = 0u;          /* r12 */
    *--p = 0u;          /* r13 */
    *--p = 0u;          /* r14 */
    *--p = 0u;          /* r15 (PUSH_REGS pushes r15 LAST -> lowest addr) */

    /* `p` now points at the lowest address of the PUSH_REGS area
     * (the r15 slot). The trampoline expects RSP to be exactly here
     * when control reaches its first POP_REGS instruction. */
    t->context.rsp = (uint64_t)(uintptr_t)p;
    t->context.rip = (uint64_t)(uintptr_t)x64_user_first_dispatch;
    t->context.rflags = (uint64_t)USER_TASK_USER_RFLAGS;
    t->context.rbp = (uint64_t)(uintptr_t)p;
}

void user_task_arm_for_first_dispatch(struct task *t, uint64_t user_rip,
                                      uint64_t user_rsp) {
    /* Backward-compatible thin shim. The base API leaves RAX at
     * zero so the existing test_user_task_init contract (15 zeroed
     * PUSH_REGS slots) still holds. */
    user_task_arm_for_first_dispatch_with_rax(t, user_rip, user_rsp,
                                              0u);
}
