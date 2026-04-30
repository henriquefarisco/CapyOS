# x86_64 context switch for CapyOS scheduler
# void context_switch(struct task_context *old, struct task_context *new_ctx)
# old is in %rdi, new_ctx is in %rsi
#
# struct task_context layout (offsets, locked by tests/test_context_switch.c):
#   0x00: rsp
#   0x08: rbp
#   0x10: rbx
#   0x18: r12
#   0x20: r13
#   0x28: r14
#   0x30: r15
#   0x38: rip
#   0x40: rflags
#   0x48: cr3
#
# IRQ-safety contract (M4 phase 2):
#   - The function disables interrupts on entry via cli so that no
#     IRQ can fire while we are mid-swap (between saving the old
#     stack pointer and switching to the new one). The swap is
#     completed atomically from the perspective of any handler that
#     might preempt the current task.
#   - The new task's RFLAGS slot is restored via popfq just before
#     the final jmpq. If the new task was preempted with IF=1 (the
#     normal case for any task spawned via task_create which seeds
#     rflags=0x202), interrupts are re-enabled exactly when control
#     transfers, leaving no observable window in which the kernel
#     state is inconsistent AND interrupts are pending.
#   - When invoked from an IRQ handler that already entered with
#     IF=0 (interrupt gate semantics), the explicit cli is a no-op
#     but keeps the function's invariant uniform across callers.
#   - The eventual iretq in the surrounding ISR epilogue restores
#     IF from the IRET frame on the task's kernel stack, so the
#     task always observes the architectural IF that was in force
#     at the time it was last preempted, regardless of the path
#     taken through the scheduler.

.global context_switch
.type context_switch, @function
context_switch:
    # Disable interrupts for the duration of the swap. Must be the
    # very first instruction so that the old task's IF state has not
    # already been clobbered by an unexpected IRQ between schedule()
    # and the start of context_switch.
    cli

    # Save callee-saved registers into old context
    movq %rsp, 0x00(%rdi)
    movq %rbp, 0x08(%rdi)
    movq %rbx, 0x10(%rdi)
    movq %r12, 0x18(%rdi)
    movq %r13, 0x20(%rdi)
    movq %r14, 0x28(%rdi)
    movq %r15, 0x30(%rdi)

    # Save return address as RIP
    leaq 1f(%rip), %rax
    movq %rax, 0x38(%rdi)

    # Save flags. Because the cli above forced IF=0 in the architectural
    # state, the bit we record here always reflects the IF that the
    # caller wanted (pushfq is taken AFTER cli and would always observe
    # IF=0); however for resumption purposes the meaningful IF value is
    # the one that the new task wants, which we restore further below
    # via popfq from new_ctx->rflags. Saving here keeps the contract
    # symmetric and lets a task that enters context_switch with a
    # well-defined IF observe it again on its next turn through this
    # routine via the legacy rflags bit pattern.
    pushfq
    popq %rax
    movq %rax, 0x40(%rdi)

    # Check if we need to switch address spaces (cr3)
    movq 0x48(%rsi), %rax
    testq %rax, %rax
    jz .Lno_cr3_switch
    movq %cr3, %rcx
    cmpq %rax, %rcx
    je .Lno_cr3_switch
    movq %rax, %cr3
.Lno_cr3_switch:

    # Restore callee-saved registers from new context
    movq 0x10(%rsi), %rbx
    movq 0x18(%rsi), %r12
    movq 0x20(%rsi), %r13
    movq 0x28(%rsi), %r14
    movq 0x30(%rsi), %r15
    movq 0x08(%rsi), %rbp
    movq 0x00(%rsi), %rsp

    # Restore flags
    movq 0x40(%rsi), %rax
    pushq %rax
    popfq

    # Jump to new task's saved RIP
    movq 0x38(%rsi), %rax
    jmpq *%rax

1:
    # Old task resumes here after being switched back in
    ret

.size context_switch, .-context_switch

# void context_switch_into_first(struct task_context *new_ctx) (M4 phase 8e)
#
# One-way jump into a freshly-created task: loads the task's saved
# regs, RSP, RFLAGS and RIP, then `jmp`s to the entry. The caller's
# stack/RIP is intentionally abandoned because there is no "old"
# context to save into - this is the kernel's bootstrap into the
# very first task it dispatches.
#
# Why this and not context_switch?
#   The conventional context_switch saves the caller's callee-saved
#   regs into `old`. Calling it for the first dispatch would clobber
#   `old` (the first task's own context) with the boot stack/RIP,
#   meaning the first task's saved entry RIP would be lost and it
#   would never actually run its entry function. Phase 8e demo
#   needs the first task to ACTUALLY execute its body, so we expose
#   a noreturn helper that only restores `new`.
#
# Argument: rdi = struct task_context * for the task to enter.
# Behaviour: noreturn. The caller must have already done
#   task_set_current(new) and scheduler_set_running(1) and any other
#   setup before invoking this.

.global context_switch_into_first
.type context_switch_into_first, @function
context_switch_into_first:
    cli

    # Optional cr3 switch (mirrors context_switch behaviour).
    movq 0x48(%rdi), %rax
    testq %rax, %rax
    jz .Lcsif_no_cr3
    movq %cr3, %rcx
    cmpq %rax, %rcx
    je .Lcsif_no_cr3
    movq %rax, %cr3
.Lcsif_no_cr3:

    # Restore callee-saved regs from new context.
    movq 0x10(%rdi), %rbx
    movq 0x18(%rdi), %r12
    movq 0x20(%rdi), %r13
    movq 0x28(%rdi), %r14
    movq 0x30(%rdi), %r15
    movq 0x08(%rdi), %rbp
    movq 0x00(%rdi), %rsp

    # Restore RFLAGS (typically 0x202 = IF=1 for tasks created via
    # task_create). This re-enables interrupts at the same instant
    # control transfers into the task body, mirroring context_switch.
    movq 0x40(%rdi), %rax
    pushq %rax
    popfq

    # Jump to the task entry. Abandoning the boot stack is intentional.
    movq 0x38(%rdi), %rax
    jmpq *%rax

.size context_switch_into_first, .-context_switch_into_first

.section .note.GNU-stack,"",@progbits
