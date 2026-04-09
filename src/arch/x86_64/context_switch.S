# x86_64 context switch for CapyOS scheduler
# void context_switch(struct task_context *old, struct task_context *new_ctx)
# old is in %rdi, new_ctx is in %rsi
#
# struct task_context layout (offsets):
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

.global context_switch
.type context_switch, @function
context_switch:
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

    # Save flags
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
