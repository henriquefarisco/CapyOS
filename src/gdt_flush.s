[bits 32]
global gdt_flush

; void gdt_flush(struct gdt_ptr* gp);
gdt_flush:
    mov     eax, [esp+4]
    lgdt    [eax]            ; (LGDT = carrega registrador GDTR)

    mov     ax, 0x10         ; (0x10 = seletor do segmento de dados)
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    jmp     0x08:.flush_cs   ; (far jump recarrega CS com seletor 0x08)
.flush_cs:
    ret
