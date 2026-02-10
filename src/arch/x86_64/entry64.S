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
    
    # HYBRID BOOT: Do NOT disable interrupts (cli) or reload IDT.
    # We rely on UEFI Boot Services for input, which needs UEFI IDT/ISRs.
    
    mov dx, 0xE9
    mov al, 'S'
    out dx, al

    # Stack (128 KiB for UEFI safety)
    lea rsp, [rip + stack_top]
    
    mov al, 'T'
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

.bss
.align 16
stack_space:
    .skip 131072    # 128 KiB Stack
stack_top:

.section .note.GNU-stack,"",@progbits

