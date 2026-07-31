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

    # Enable EFER.NXE (bit 11) so PTE bit 63 (NX) is an honored permission
    # bit, not a reserved bit. The ELF loader marks non-executable user
    # segments (.rodata/.data) with NX (elf_loader.c: PF_X absent -> NX);
    # with NXE clear the CPU treats bit 63 as RESERVED and the first access
    # to such a page faults with a reserved-bit #PF (error code bit3=RSVD,
    # i.e. 0x9 for a supervisor read). VMware's UEFI firmware hands off with
    # NXE CLEAR while QEMU/OVMF leaves it SET -- which is exactly why the
    # graphical browser (capygfx, the first binary with a separate NX-marked
    # rodata segment) page-faulted in sys_window_create reading its title on
    # VMware but ran fine under every QEMU smoke. hello/exectarget have a
    # single executable PT_LOAD (NX=0), so they never exercised this. Enable
    # NXE unconditionally in early boot, gated on CPUID NX support (bit 20 of
    # CPUID.80000001h:EDX) -- present on every virtual/physical CPU that can
    # run this kernel. rdmsr/wrmsr clobber only EAX/ECX/EDX; the UEFI handoff
    # pointer lives in R15 (saved above) and is untouched.
    mov eax, 0x80000001
    cpuid
    test edx, (1 << 20)     # NX (execute-disable) supported?
    jz .Lno_nxe
    mov ecx, 0xC0000080     # IA32_EFER
    rdmsr
    or  eax, (1 << 11)      # set NXE
    wrmsr
.Lno_nxe:

    # CPUID and RDMSR/WRMSR clobber EDX; restore the debugcon port.
    mov dx, 0xE9
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
