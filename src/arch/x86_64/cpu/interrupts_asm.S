.intel_syntax noprefix
.section .text
.code64

.extern x64_exception_dispatch

.macro PUSH_REGS
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
.endm

.macro POP_REGS
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
.endm

.macro VECTOR_NOERR num
    .global x64_vector_\num
x64_vector_\num:
    push 0
    push \num
    jmp x64_exception_common
.endm

.macro VECTOR_ERR num
    .global x64_vector_\num
x64_vector_\num:
    push \num
    jmp x64_exception_common
.endm

.global x64_unhandled_vector_stub
x64_unhandled_vector_stub:
    push 0
    push 255
    jmp x64_exception_common

x64_exception_common:
    cld
    PUSH_REGS
    mov rdi, rsp
    mov r12, rsp
    and rsp, -16
    call x64_exception_dispatch
    mov rsp, r12
    POP_REGS
    add rsp, 16
    iretq

# void x64_user_first_dispatch(void) (M4 phase 8f.4)
#
# One-way trampoline used when context_switch lands on a fresh user
# task for the very first time. The task's kernel stack must already
# have been seeded by user_task_arm_for_first_dispatch() with the
# exact byte layout that x64_exception_common would have left behind
# AFTER `PUSH_REGS` plus `vector` plus `error_code` plus the IRET
# frame (RIP, CS, RFLAGS, RSP, SS).
#
# When context_switch jumps here, RSP already points at the bottom
# of that synthesized frame (matching the post-PUSH_REGS state).
# The trampoline therefore mirrors the tail of x64_exception_common
# starting at the POP_REGS sequence.
#
# Result: the CPU pops 15 GP regs + skips vector/error_code +
# iretqs into ring 3 with the user RIP/RSP/CS/SS/RFLAGS the builder
# wrote into the IRET slots. From the user task's point of view it
# is indistinguishable from "I was preempted just before main()
# started running, and now the scheduler restored me".

.global x64_user_first_dispatch
.type x64_user_first_dispatch, @function
x64_user_first_dispatch:
    POP_REGS
    add rsp, 16
    iretq

.size x64_user_first_dispatch, .-x64_user_first_dispatch

VECTOR_NOERR 0
VECTOR_NOERR 1
VECTOR_NOERR 2
VECTOR_NOERR 3
VECTOR_NOERR 4
VECTOR_NOERR 5
VECTOR_NOERR 6
VECTOR_NOERR 7
VECTOR_ERR 8
VECTOR_NOERR 9
VECTOR_ERR 10
VECTOR_ERR 11
VECTOR_ERR 12
VECTOR_ERR 13
VECTOR_ERR 14
VECTOR_NOERR 15
VECTOR_NOERR 16
VECTOR_ERR 17
VECTOR_NOERR 18
VECTOR_NOERR 19
VECTOR_NOERR 20
VECTOR_ERR 21
VECTOR_NOERR 22
VECTOR_NOERR 23
VECTOR_NOERR 24
VECTOR_NOERR 25
VECTOR_NOERR 26
VECTOR_NOERR 27
VECTOR_NOERR 28
VECTOR_ERR 29
VECTOR_ERR 30
VECTOR_NOERR 31

VECTOR_NOERR 32
VECTOR_NOERR 33
VECTOR_NOERR 34
VECTOR_NOERR 35
VECTOR_NOERR 36
VECTOR_NOERR 37
VECTOR_NOERR 38
VECTOR_NOERR 39
VECTOR_NOERR 40
VECTOR_NOERR 41
VECTOR_NOERR 42
VECTOR_NOERR 43
VECTOR_NOERR 44
VECTOR_NOERR 45
VECTOR_NOERR 46
VECTOR_NOERR 47

.section .rodata
.align 8
.global x64_exception_stub_table
x64_exception_stub_table:
    .quad x64_vector_0
    .quad x64_vector_1
    .quad x64_vector_2
    .quad x64_vector_3
    .quad x64_vector_4
    .quad x64_vector_5
    .quad x64_vector_6
    .quad x64_vector_7
    .quad x64_vector_8
    .quad x64_vector_9
    .quad x64_vector_10
    .quad x64_vector_11
    .quad x64_vector_12
    .quad x64_vector_13
    .quad x64_vector_14
    .quad x64_vector_15
    .quad x64_vector_16
    .quad x64_vector_17
    .quad x64_vector_18
    .quad x64_vector_19
    .quad x64_vector_20
    .quad x64_vector_21
    .quad x64_vector_22
    .quad x64_vector_23
    .quad x64_vector_24
    .quad x64_vector_25
    .quad x64_vector_26
    .quad x64_vector_27
    .quad x64_vector_28
    .quad x64_vector_29
    .quad x64_vector_30
    .quad x64_vector_31

.global x64_irq_stub_table
x64_irq_stub_table:
    .quad x64_vector_32
    .quad x64_vector_33
    .quad x64_vector_34
    .quad x64_vector_35
    .quad x64_vector_36
    .quad x64_vector_37
    .quad x64_vector_38
    .quad x64_vector_39
    .quad x64_vector_40
    .quad x64_vector_41
    .quad x64_vector_42
    .quad x64_vector_43
    .quad x64_vector_44
    .quad x64_vector_45
    .quad x64_vector_46
    .quad x64_vector_47

.section .note.GNU-stack,"",@progbits
