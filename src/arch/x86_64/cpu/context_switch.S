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
#   - The function snapshots RFLAGS before disabling interrupts.  A
#     voluntary switch can arrive with IF=1, and saving flags after
#     `cli` would permanently resume that task with IF=0.  `pushfq;
#     cli; popq` keeps the original flags on the current stack while
#     closing the interrupt window before any context field is changed.
#     An IRQ between pushfq and cli is safe: the interrupt frame is
#     nested below that saved word and iretq returns to the cli.
#   - The new task's RIP, kernel CS, RFLAGS, RSP and SS are restored together
#     by iretq.  AMD64 IRETQ unconditionally pops RSP/SS even when CPL does
#     not change, so a three-slot frame would consume arbitrary task-stack
#     words and fault on the first kernel-task dispatch.  A popfq+jmp pair
#     would expose a narrow window with IF=1
#     after the new stack/registers were installed but before its RIP
#     was reached; a timer IRQ there could recursively overwrite the
#     selected task's context.  iretq makes that final transfer atomic.
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
    # Capture the caller's flags BEFORE cli.  In particular, preserve IF=1
    # across a cooperative yield.  Keep this three-instruction sequence at
    # the entry; tests/test_context_switch.c locks the ordering contract.
    pushfq
    cli
    popq %rax
    movq %rax, 0x40(%rdi)

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
    movq 0x00(%rsi), %rdx
    movq %rdx, %rsp

    # AMD64 IRETQ always pops [RIP, CS, RFLAGS, RSP, SS] in 64-bit mode,
    # including same-CPL returns.  Seed the full frame in reverse push order;
    # the target RSP is the value captured before these temporary pushes.
    pushq $0x10
    pushq %rdx
    pushq 0x40(%rsi)
    pushq $0x08
    pushq 0x38(%rsi)
    iretq

1:
    # Old task resumes here after being switched back in
    ret

.size context_switch, .-context_switch

# void context_switch_into_first(struct task_context *new_ctx) (M4 phase 8e)
#
# One-way jump into a freshly-created task: loads the task's saved
# regs and RSP, then restores RFLAGS/RIP atomically with `iretq`. The caller's
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
    movq 0x00(%rdi), %rdx
    movq %rdx, %rsp

    # Restore the full AMD64 IRETQ frame as one architectural transfer.
    # Abandoning the boot stack is intentional; RSP is explicitly restored to
    # the freshly-created task's saved stack after the five fields are popped.
    pushq $0x10
    pushq %rdx
    pushq 0x40(%rdi)
    pushq $0x08
    pushq 0x38(%rdi)
    iretq

.size context_switch_into_first, .-context_switch_into_first

.section .note.GNU-stack,"",@progbits
