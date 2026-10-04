.text
# uint64_t fp_boundary_probe(seed, before, after, frame, boundary)
# Execute the real production boundary with all architectural FP registers
# occupied. Preserve the host test runner's state, including its FP controls.
.global fp_boundary_probe
.type fp_boundary_probe, @function
fp_boundary_probe:
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $512, %rsp
    fxsave64 (%rsp)
    movq %rsi, %r12
    movq %rdx, %r13
    movq %rcx, %r14
    movq %r8, %r15
    fxrstor64 (%rdi)
    fxsave64 (%r12)
    movq %r14, %rdi
    call *%r15
    fxsave64 (%r13)
    fxrstor64 (%rsp)
    addq $512, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    ret
.size fp_boundary_probe, .-fp_boundary_probe

# Host replacement for the C dispatcher: record its incoming controls, then
# deliberately overwrite every FP register, status and control field. RDI is
# the original opaque frame pointer and RAX must survive the boundary exit.
.global syscall_dispatch
.global syscall_dispatch_with_fp
.global x64_exception_dispatch
.type syscall_dispatch_with_fp, @function
syscall_dispatch_with_fp:
    movq %rdi, %r8
    leaq 1024(%rdi), %rdi
    movq $64, %rcx
    rep movsq
    movq %r8, %rdi
    jmp syscall_dispatch
.size syscall_dispatch_with_fp, .-syscall_dispatch_with_fp
.type syscall_dispatch, @function
syscall_dispatch:
x64_exception_dispatch:
    fxsave64 (%rdi)
    fxrstor64 512(%rdi)
    movabsq $0x465053595343414c, %rax
    ret
.size syscall_dispatch, .-syscall_dispatch
.section .note.GNU-stack,"",@progbits
