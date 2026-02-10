; kernel_entry.s — Entrada do kernel com cabeçalho Multiboot (NASM, 32-bit)
; Montar com: nasm -f elf32 src/kernel_entry.s -o build/kernel_entry.o

[BITS 32]
SECTION .multiboot
align 4
MULTIBOOT_MAGIC  equ 0x1BADB002
MULTIBOOT_FLAGS  equ 0x00000000        ; flags mínimas
MULTIBOOT_CHECK  equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; Header Multiboot (precisa ficar nos primeiros ~8KB do binário)
dd MULTIBOOT_MAGIC
dd MULTIBOOT_FLAGS
dd MULTIBOOT_CHECK

SECTION .text
global _start
extern kernel_main

_start:
    ; zera os registradores “essenciais” e sobe uma pilha simples
    cli
    mov esp, stack_top

    ; GRUB/Multiboot passam: EAX = magic, EBX = pointer para multiboot_info
    ; Passa como argumentos (cdecl): kernel_main(uint32_t magic, uint32_t info_ptr)
    push ebx
    push eax
    call kernel_main
    add esp, 8

.hang:
    cli
    hlt
    jmp .hang

SECTION .bss
align 16
stack_bottom:
    resb 16384
stack_top:

SECTION .note.GNU-stack noalloc
