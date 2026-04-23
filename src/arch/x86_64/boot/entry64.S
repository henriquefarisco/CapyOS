# x86_64 early entry: Hybrid Boot compatible (preserves UEFI state)
.intel_syntax noprefix
.section .text
.code64
.global _start
.extern kernel_main64

.section .text
_start:
    # Preserve UEFI handoff pointer (kentry(handoff) passes it in RDI)
    mov r15, rdi

    # Hybrid boots keep Boot Services alive, but the kernel only arms its
    # native bridge descriptors a few instructions later in C. Keep IRQs
    # masked from the first instruction so firmware handlers cannot interrupt
    # us in the unstable window before x64_platform_tables_prepare_bridge().
    cli

    mov dx, 0xE9
    mov al, 'S'
    out dx, al

    # The kernel image now contains compiler-generated XMM moves very early in
    # boot. Firmware does not guarantee CR0/CR4 are left in an OS-owned SIMD
    # state, so claim the FPU/SSE context before any C code executes.
    mov rax, cr0
    and rax, -13         # clear TS (bit 3) and EM (bit 2)
    or  rax, 2           # set MP so WAIT/FWAIT faults follow TS correctly
    mov cr0, rax

    mov rax, cr4
    or  rax, (1 << 9) | (1 << 10)    # OSFXSR | OSXMMEXCPT
    mov cr4, rax
    fninit

    mov al, 'F'
    out dx, al

    # Bootstrap stack in a dedicated section so early return addresses never
    # alias regular .bss globals as the image layout grows.
    lea rsp, [rip + bootstrap_stack_top]

    mov al, 'T'
    out dx, al

    # Zero .bss now (UEFI loader skipped the bulk zero because writing several
    # MB through firmware page tables triggered VMware UEFI exceptions).
    # By the time we get here the firmware is no longer executing in the
    # window we're about to clear, so a direct rep stosb is safe.
    lea rdi, [rip + __bss_start]
    lea rcx, [rip + __bss_end]
    sub rcx, rdi
    xor eax, eax
    cld
    rep stosb

    mov al, 'B'
    out dx, al

    # Restore handoff pointer for SysV ABI first argument
    mov rdi, r15

    # Align stack to 16 bytes for ABI compliance before call
    and rsp, -16

    mov al, 'K'
    out dx, al
    call kernel_main64
.hang:
    hlt
    jmp .hang

.section .bootstrap_stack,"aw",@nobits
.align 4096
.global bootstrap_stack_bottom
bootstrap_stack_bottom:
    .skip 262144    # 256 KiB bootstrap stack
.global bootstrap_stack_top
bootstrap_stack_top:

.section .note.GNU-stack,"",@progbits
