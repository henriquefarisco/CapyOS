# x86_64 syscall entry point for CapyOS
# Uses SYSCALL/SYSRET convention:
#   RAX = syscall number
#   RDI, RSI, RDX, R10, R8, R9 = arguments
#   RCX = saved RIP (by CPU)
#   R11 = saved RFLAGS (by CPU)

.global syscall_entry
.global syscall_init_msr

# MSR addresses
.set MSR_EFER,   0xC0000080
.set MSR_STAR,   0xC0000081
.set MSR_LSTAR,  0xC0000082
.set MSR_SFMASK, 0xC0000084
.set EFER_SCE,   0x01

# Kernel CS=0x08, Kernel SS=0x10, User CS=0x1B, User SS=0x23
# STAR: bits 47:32 = kernel CS, bits 63:48 = user CS base (minus 16)
.set STAR_VALUE_LOW,  0x00000000
.set STAR_VALUE_HIGH, 0x00100008

syscall_init_msr:
    # Enable SCE (System Call Extensions) in EFER
    movl $MSR_EFER, %ecx
    rdmsr
    orl $EFER_SCE, %eax
    wrmsr

    # Set STAR: kernel segments and user segments
    movl $MSR_STAR, %ecx
    movl $STAR_VALUE_LOW, %eax
    movl $STAR_VALUE_HIGH, %edx
    wrmsr

    # Set LSTAR: syscall entry point
    movl $MSR_LSTAR, %ecx
    leaq syscall_entry(%rip), %rax
    movq %rax, %rdx
    shrq $32, %rdx
    wrmsr

    # Set SFMASK: clear IF and DF on syscall entry
    movl $MSR_SFMASK, %ecx
    movl $0x0600, %eax   # IF=0x200, DF=0x400
    xorl %edx, %edx
    wrmsr

    ret

.align 16
syscall_entry:
    # We enter with:
    #   RCX = user RIP, R11 = user RFLAGS
    #   RSP = user RSP (must switch to kernel stack)

    # Save user RSP and switch to kernel stack
    # For now, use a per-CPU kernel stack area
    movq %rsp, %gs:0x08       # Save user RSP to per-CPU area
    movq %gs:0x00, %rsp       # Load kernel RSP from per-CPU area

    # Build syscall_frame on kernel stack
    pushq %r11                 # rflags
    pushq %rcx                 # rip (return address)
    pushq %gs:0x08             # user rsp
    pushq %r11                 # r11 (saved rflags duplicate)
    pushq %rcx                 # rcx (saved rip duplicate)
    pushq %r9
    pushq %r8
    pushq %r10                 # arg4 (r10 replaces rcx in syscall ABI)
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rax                 # syscall number

    # Call C dispatcher: syscall_dispatch(frame)
    movq %rsp, %rdi
    call syscall_dispatch

    # Return value is in RAX, it stays there for userspace

    # Restore registers
    addq $8, %rsp              # skip saved rax (return value already in rax)
    popq %rdi
    popq %rsi
    popq %rdx
    popq %r10
    popq %r8
    popq %r9
    popq %rcx                  # rcx duplicate
    popq %r11                  # r11 duplicate
    popq %rsp                  # restore user RSP directly
    # RCX and R11 already have user RIP and RFLAGS from above pops

    sysretq

.size syscall_entry, .-syscall_entry
