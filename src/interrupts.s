; interrupts.s — ISRs (0..31) e IRQs (32..47) chamando isr_dispatch(int_no, err_code)
; Monte com: nasm -f elf32 src/interrupts.s -o build/interrupts.o

[BITS 32]
SECTION .text

GLOBAL isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31
GLOBAL irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7,irq8,irq9,irq10,irq11,irq12,irq13,irq14,irq15

EXTERN isr_dispatch        ; void isr_dispatch(uint32_t int_no, uint32_t err_code);

%macro ISR_NOERR 1
isr%1:
    pusha
    push dword 0          ; err_code fake
    push dword %1         ; int_no
    call isr_dispatch
    add esp, 8
    popa
    iretd
%endmacro

; Exceções que empilham error code (CPU empilha antes do EIP)
; Após pusha, o error code fica em [esp + 32]
%macro ISR_ERR 1
isr%1:
    pusha
    push dword [esp + 32] ; err_code real
    push dword %1         ; int_no
    call isr_dispatch
    add esp, 8
    popa
    add esp, 4            ; descarta error code empilhado pela CPU
    iretd
%endmacro

; IRQs 0..15 (hardware). Não têm error code; usamos 0.
%macro IRQ 2
irq%1:
    pusha
    push dword 0          ; err_code fake
    push dword %2         ; int_no = 32 + %1
    call isr_dispatch
    add esp, 8
    popa
    iretd
%endmacro

; --- Exceções ---
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; --- IRQs (vetores 32..47) ---
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47
