# AP (Application Processor) bootstrap trampoline for CapyOS SMP
#
# This code is copied to physical address 0x8000 by the BSP before
# sending STARTUP IPIs. Each AP starts in 16-bit real mode at 0x8000,
# transitions to 32-bit protected mode, then to 64-bit long mode,
# and finally jumps to ap_main() in C.
#
# The BSP writes the AP's stack pointer and cr3 into known locations
# at the end of this trampoline page before sending the IPI.

.code16
.section .text.ap_trampoline, "ax"
.global ap_trampoline_start
.global ap_trampoline_end

ap_trampoline_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    # Load the GDT pointer placed by BSP at trampoline + 0xF00
    lgdt [0x8F00]

    # Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    # Far jump to 32-bit code
    .byte 0x66, 0xEA        # ljmp
    .long ap_pm32 - ap_trampoline_start + 0x8000
    .word 0x08              # code32 selector

.code32
ap_pm32:
    mov ax, 0x10            # data32 selector
    mov ds, ax
    mov es, ax
    mov ss, ax

    # Enable PAE
    mov eax, cr4
    or eax, (1 << 5)        # PAE
    mov cr4, eax

    # Load CR3 from trampoline data area (BSP wrote it at 0x8FF0)
    mov eax, [0x8FF0]
    mov cr3, eax

    # Enable long mode via EFER MSR
    mov ecx, 0xC0000080      # IA32_EFER
    rdmsr
    or eax, (1 << 8)         # LME
    wrmsr

    # Enable paging
    mov eax, cr0
    or eax, (1 << 31)        # PG
    mov cr0, eax

    # Far jump to 64-bit code
    .byte 0xEA
    .long ap_lm64 - ap_trampoline_start + 0x8000
    .word 0x08               # code64 selector

.code64
ap_lm64:
    # Load 64-bit stack from trampoline data (BSP wrote at 0x8FF8)
    mov rsp, [0x8FF8]

    # Signal that we're alive by writing to debugcon
    mov dx, 0xE9
    mov al, 'A'
    out dx, al
    mov al, 'P'
    out dx, al

    # Call ap_main (defined in smp.c)
    .extern ap_entry_c
    call ap_entry_c

    # Should never return
.ap_halt:
    hlt
    jmp .ap_halt

ap_trampoline_end:

.section .note.GNU-stack,"",@progbits
