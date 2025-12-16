; stage1.s — MBR boot code that loads stage2 via INT 13h Extensions
; - Expects LBA of stage2 and sector count to be patched in the placeholders below.
; - Relies on BIOS EDD; falls back to hang on error.

BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl          ; BIOS sets DL to boot drive

    ; Prepare DAP (Disk Address Packet)
    mov si, disk_packet
    mov word [si], 0x0010         ; size=16, reserved=0
    mov word [si+2], stage2_sectors
    mov word [si+4], stage2_offset
    mov word [si+6], stage2_segment
    mov dword [si+8], stage2_lba_low
    mov dword [si+12], stage2_lba_high

    ; Call INT 13h Extensions - Read
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc boot_fail

    ; Jump to stage2
    jmp stage2_segment:stage2_offset

boot_fail:
    cli
    hlt
    jmp boot_fail

; Data
boot_drive      db 0

; DAP structure (16 bytes)
disk_packet:
    dw 0x0010            ; size + reserved
    dw 0                 ; sector count (patched above)
    dw 0                 ; buffer offset
    dw 0                 ; buffer segment
    dd 0                 ; LBA low
    dd 0                 ; LBA high

; Stage2 load location (patched)
stage2_offset   dw 0x8000
stage2_segment  dw 0x0000
stage2_sectors  dw 0x0001          ; placeholder
stage2_lba_low  dd 0xDEADBEEF      ; placeholder patched by installer
stage2_lba_high dd 0

times 440-($-$$) db 0

; Partition table placeholder (installer copies actual entries here)
times 64 db 0

dw 0xAA55
