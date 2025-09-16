; carrega GDTR, faz far jump para recarregar CS e ajusta segmentos de dados
; (far jump = troca de segmento de código; seletores: 0x08 code, 0x10 data)
[bits 32]
global gdt_flush

gdt_flush:
    ; entrada: [esp+4] = ponteiro para struct gdt_ptr
    mov     eax, [esp+4]
    lgdt    [eax]

    ; reabilita segmentos usando nossa GDT
    mov     ax, 0x10         ; data selector
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ; far jump para recarregar CS
    jmp     0x08:.flush_cs

.flush_cs:
    ret
