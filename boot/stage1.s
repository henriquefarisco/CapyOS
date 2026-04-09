; stage1.s — MBR boot code that loads stage2 via INT 13h Extensions
; - Expects LBA of stage2 and sector count to be patched in the placeholders below.
; - Relies on BIOS EDD; falls back to hang on error.

BITS 16
ORG 0x7C00

%define HANDOFF_STAGE2_BASE 0x0500
%define HANDOFF_SIG        0xA5
%define HANDOFF_OFF_S2_LBA     0
%define HANDOFF_OFF_S2_LBA_H   4
%define HANDOFF_OFF_S2_SEC     8   ; word
%define HANDOFF_OFF_SIG        10  ; byte
%define HANDOFF_OFF_KRNL_LBA   12
%define HANDOFF_OFF_KRNL_LBA_H 16
%define HANDOFF_OFF_KRNL_SEC   20

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Debug: Print 'S' for Stage1 started
    mov ah, 0x0E
    mov al, 'S'
    int 0x10

    mov [boot_drive], dl          ; BIOS sets DL to boot drive

    ; Prepare DAP (Disk Address Packet) - carrega valores das variaveis patchadas
    mov si, disk_packet
    mov word [si], 0x0010         ; size=16, reserved=0
    mov ax, [stage2_sectors]
    mov word [si+2], ax           ; sector count (patched)
    mov ax, [stage2_offset]
    mov word [si+4], ax           ; buffer offset
    mov ax, [stage2_segment]
    mov word [si+6], ax           ; buffer segment
    mov eax, [stage2_lba_low]
    mov dword [si+8], eax         ; LBA low (patched)
    mov eax, [stage2_lba_high]
    mov dword [si+12], eax        ; LBA high

    ; Debug: Print 'L' for Loading stage2
    mov ah, 0x0E
    mov al, 'L'
    int 0x10

    ; Call INT 13h Extensions - Read
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc boot_fail

    ; Stash stage2 location so stage2 can recover even if its header is unreadable
    mov eax, [stage2_lba_low]
    mov dword [HANDOFF_STAGE2_BASE + HANDOFF_OFF_S2_LBA], eax
    mov eax, [stage2_lba_high]
    mov dword [HANDOFF_STAGE2_BASE + HANDOFF_OFF_S2_LBA_H], eax
    mov ax, [stage2_sectors]
    mov word [HANDOFF_STAGE2_BASE + HANDOFF_OFF_S2_SEC], ax

    ; Also copy patched kernel header values from the loaded stage2 image
    ; Do byte-wise to avoid operand-size quirks
    mov si, 0x8004                ; kernel_sectors in loaded stage2
    xor eax, eax
    mov al, [si]
    mov ah, [si+1]
    xor ebx, ebx
    mov bl, [si+2]
    mov bh, [si+3]
    shl ebx, 16
    or eax, ebx
    mov dword [HANDOFF_STAGE2_BASE + HANDOFF_OFF_KRNL_SEC], eax

    mov si, 0x8008                ; kernel_lba_low in loaded stage2
    xor eax, eax
    mov al, [si]
    mov ah, [si+1]
    xor ebx, ebx
    mov bl, [si+2]
    mov bh, [si+3]
    shl ebx, 16
    or eax, ebx
    mov dword [HANDOFF_STAGE2_BASE + HANDOFF_OFF_KRNL_LBA], eax
    xor eax, eax
    mov dword [HANDOFF_STAGE2_BASE + HANDOFF_OFF_KRNL_LBA_H], eax

    mov byte [HANDOFF_STAGE2_BASE + HANDOFF_OFF_SIG], HANDOFF_SIG

    ; Debug: Print 'J' for Jumping to stage2
    mov ah, 0x0E
    mov al, 'J'
    int 0x10

    ; Jump to stage2
    jmp 0x0000:0x8000

boot_fail:
    ; Debug: Print 'E' for Error
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
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
stage2_sectors  dw 0xBEEF          ; placeholder (patched by installer)
stage2_lba_low  dd 0xDEADBEEF      ; placeholder (patched by installer)
stage2_lba_high dd 0

times 446-($-$$) db 0

; Partition table placeholder (installer copies actual entries here)
times 64 db 0

dw 0xAA55
