BITS 16
ORG 0x7C00

start:
    mov ah, 0x0E
    mov si, msg
.print:
    lodsb
    or al, al
    jz boot_grub
    int 0x10
    jmp .print

boot_grub:
    jmp 0x0000:0x7C00   ; transfere controle para GRUB (caso esteja em cadeia)

halt:
    cli
    hlt

msg db "Hello from Bootloader!", 0

times 510 - ($ - $$) db 0
dw 0xAA55
