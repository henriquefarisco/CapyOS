; stage2.asm — Minimal BIOS loader for NoirOS
BITS 16
ORG 0x8000
    jmp start

    ; =================================================================
    ; Bootloader Header (Patchable Variables)
    ; =================================================================
    align 4
    ; 0x8004: kernel_sectors
    dd 0xBADC0FFE
    ; 0x8008: kernel_lba_low
    dd 0xFEEDFACE
    ; 0x800C: stage2_lba_low
    dd 0xDEADBEEF
    ; 0x8010: stage2_sectors
    dd 0xCAFEBABE
    
    ; Variable Address Map (Hardcoded to match layout above)
    %define kernel_sectors      0x8004
    %define kernel_lba_low      0x8008
    %define stage2_lba_low      0x800C
    %define stage2_sectors      0x8010

    ; Debug: Magic signature
    db 'N', 'O', 'I', 'R'

%define HANDOFF_STAGE2_BASE  0x0500
%define HANDOFF_SIG          0xA5
%define HANDOFF_OFF_S2_LBA     0
%define HANDOFF_OFF_S2_LBA_H   4
%define HANDOFF_OFF_S2_SEC     8   ; word
%define HANDOFF_OFF_SIG        10  ; byte
%define HANDOFF_OFF_KRNL_LBA   12
%define HANDOFF_OFF_KRNL_LBA_H 16
%define HANDOFF_OFF_KRNL_SEC   20
%define STAGE2_LBA_PLACEHOLDER      0xDEADBEEF
%define STAGE2_SECTORS_PLACEHOLDER  0xCAFEBABE
%define KERNEL_LBA_PLACEHOLDER      0xFEEDFACE
%define KERNEL_SECTORS_PLACEHOLDER  0xBADC0FFE
%define STAGE2_HDR_BUF        0x0600       ; temp buffer to reread stage2 header from disk
%define DEFAULT_STAGE2_LBA    0x800
%define DEFAULT_STAGE2_SEC    6
%define DEFAULT_KERNEL_LBA    0x807
%define DEFAULT_KERNEL_SEC    0xFE

%define KERNEL_BUF       0x10000         ; staging buffer (64KB, safe for INT 13h)
%define MANIFEST_BUF     0x7E00          ; manifest 512B
%define STACK_REAL       0x7C00
%define PM_STACK         0x9FC00
%define GDT_BASE         gdt
%define GDT_LIMIT        (gdt_end - gdt - 1)
%define CODE_SEL         0x08
%define DATA_SEL         0x10
%define MANIFEST_MAGIC   0x5442494E      ; "NIBT"
%define MANIFEST_VERSION 1
%define MANIFEST_ENTRY_SIZE 20
%define MANIFEST_OFFSET_ENTRIES 16
%define COLOR_RED_ERR    0x4F

%define ENTRY_TYPE_NORMAL    1
%define ENTRY_TYPE_RECOVERY  2

; ------------------
; Simple logging helpers (real mode, INT 10h teletype)
; Use short strings to control size. wait_space pauses until space is pressed.

print_str:
    pusha
.ps_loop:
    lodsb
    cmp al, 0
    je .ps_done
    mov ah, 0x0E
    int 0x10
    jmp .ps_loop
.ps_done:
    popa
    ret

print_crlf:
    mov ah, 0x0E
    mov al, 0x0D
    int 0x10
    mov al, 0x0A
    int 0x10
    ret

print_err_red:
    ; Prints AL with red background using INT10 AH=09
    push ax
    push bx
    push cx
    mov ah, 0x09
    mov bh, 0x00
    mov bl, 0x4F          ; white on red
    mov cx, 1
    int 0x10
    pop cx
    pop bx
    pop ax
    ret

wait_space_timeout:
    ; Waits for Space or times out after ~2s (BIOS tick based)
    pusha
    push ds
    xor ax, ax
    mov ds, ax
    mov bx, 0x046C
    mov eax, [bx]           ; BIOS ticks (18.2 Hz)
    add eax, 36             ; ~2s
.ws_loop:
    mov ah, 0x01
    int 0x16
    jz .ws_check_time
    mov ah, 0x00
    int 0x16
    cmp al, ' '
    je .ws_done
.ws_check_time:
    mov edx, [bx]
    cmp edx, eax
    jb .ws_loop
.ws_done:
    pop ds
    popa
    ret

; load_header_dword:
; ESI = linear address (DS must be 0). Returns EAX assembled byte-wise.
; Avoids problematic dword reads on the patchable header fields.
load_header_dword:
    push edx
    xor eax, eax
    mov al, [esi]
    mov ah, [esi+1]
    xor edx, edx
    mov dl, [esi+2]
    mov dh, [esi+3]
    shl edx, 16
    or eax, edx
    pop edx
    ret

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_REAL
    sti
    
    ; Debug: Print '2' for Stage2 started
    mov ah, 0x0E
    mov al, '2'
    int 0x10
    mov si, msg_step_start
    call print_str
    call print_crlf
    call wait_space_timeout

    mov [boot_drive], dl
    ; Força boot_drive para o primeiro disco HDD (0x80) para evitar uso de unidade incorreta (CD/floppy).
    mov dl, 0x80
    mov [boot_drive], dl
    ; Force hard disk if BIOS reported <0x80 (e.g., CD/floppy), to avoid loading from wrong drive
    cmp dl, 0x80
    jae .keep_dl
    mov dl, 0x80
.keep_dl:
    mov [boot_drive], dl

    ; Set hardcoded defaults in case header/handoff are unreadable
    mov dword [stage2_lba_low_cached], DEFAULT_STAGE2_LBA
    mov dword [stage2_sectors_cached], DEFAULT_STAGE2_SEC
    mov dword [kernel_lba_low_cached], DEFAULT_KERNEL_LBA
    mov dword [kernel_sectors_cached], DEFAULT_KERNEL_SEC

    mov si, msg_dbg_start
    call print_str

    ; Print 'S'
    mov ah, 0x0E
    mov al, 'S'
    int 0x10

    ; Manual read kernel_sectors (0x8004) and keep if non-zero/non-placeholder
    mov si, msg_dbg_hdr
    call print_str
    mov esi, kernel_sectors
    call load_header_dword
    cmp eax, 0
    je .skip_hdr_ks
    cmp eax, KERNEL_SECTORS_PLACEHOLDER
    je .skip_hdr_ks
    mov [kernel_sectors_cached], eax
.skip_hdr_ks:
    mov eax, [kernel_sectors_cached]
    call print_eax_hex
    
    ; Print 'A'
    mov ah, 0x0E
    mov al, 'A'
    int 0x10

    ; Manual read kernel_lba_low (0x8008) and keep if non-zero/non-placeholder
    mov esi, kernel_lba_low
    call load_header_dword
    cmp eax, 0
    je .skip_hdr_kl
    cmp eax, KERNEL_LBA_PLACEHOLDER
    je .skip_hdr_kl
    mov [kernel_lba_low_cached], eax
.skip_hdr_kl:
    mov eax, [kernel_lba_low_cached]
    call print_eax_hex
    mov si, msg_dbg_hdr_done
    call print_str

    ; If Stage1 handoff exists, seed kernel caches immediately (manifest will override)
    mov al, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_SIG]
    cmp al, HANDOFF_SIG
    jne .after_handoff_seed
    mov si, msg_dbg_handoff
    call print_str
    mov eax, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_KRNL_SEC]
    cmp eax, 0
    je .skip_handoff_ks
    mov [kernel_sectors_cached], eax
.skip_handoff_ks:
    mov eax, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_KRNL_LBA]
    cmp eax, 0
    je .skip_handoff_kl
    mov [kernel_lba_low_cached], eax
.skip_handoff_kl:
    mov eax, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_KRNL_LBA_H]
    mov [kernel_lba_high], eax
.after_handoff_seed:

    ; Snapshot Stage2 header fields for manifest math
    mov esi, stage2_lba_low
    call load_header_dword
    cmp eax, 0
    je .skip_hdr_s2l
    cmp eax, STAGE2_LBA_PLACEHOLDER
    je .skip_hdr_s2l
    mov [stage2_lba_low_cached], eax
.skip_hdr_s2l:
    mov esi, stage2_sectors
    call load_header_dword
    cmp eax, 0
    je .skip_hdr_s2s
    cmp eax, STAGE2_SECTORS_PLACEHOLDER
    je .skip_hdr_s2s
    mov [stage2_sectors_cached], eax
.skip_hdr_s2s:

    ; Always prefer Stage1 handoff for stage2 location if present
    mov al, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_SIG]
    cmp al, HANDOFF_SIG
    jne .no_handoff_s2
    mov eax, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_S2_LBA]
    cmp eax, 0
    je .skip_handoff_s2l
    mov [stage2_lba_low_cached], eax
.skip_handoff_s2l:
    mov eax, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_S2_LBA_H]
    mov [stage2_lba_high], eax
    movzx eax, word [HANDOFF_STAGE2_BASE + HANDOFF_OFF_S2_SEC]
    cmp eax, 0
    je .skip_handoff_s2s
    mov [stage2_sectors_cached], eax
.skip_handoff_s2s:
.no_handoff_s2:
    ; Hard fallback for known layout if still zero (Stage2 at LBA 0x800, 4 sectors)
    mov eax, [stage2_lba_low_cached]
    cmp eax, 0
    jne .s2_fallback_done
    mov dword [stage2_lba_low_cached], DEFAULT_STAGE2_LBA
    mov dword [stage2_sectors_cached], DEFAULT_STAGE2_SEC
.s2_fallback_done:
    
    ; Debug: Check for Magic 'NOIR' at 0x8014
    mov esi, 0x8014
    mov eax, [esi]
    cmp eax, 0x52494F4E
    je .magic_ok
    
    ; Magic Fail
    mov ah, 0x0E
    mov al, '!'
    int 0x10
    call print_eax_hex
    jmp .end_magic
    
.magic_ok:
    mov ah, 0x0E
    mov al, 'M'
    int 0x10

.end_magic:
    mov si, msg_step_manifest
    call print_str
    call print_crlf

    ; Try to load manifest (located right after stage2) to get kernel LBA/size
    mov si, msg_dbg_manifest_read
    call print_str
    mov eax, [stage2_sectors_cached]
    add eax, [stage2_lba_low_cached]   ; manifest = stage2 start + size
    mov edx, [stage2_lba_high]
    adc edx, 0
    mov bx, MANIFEST_BUF
    xor ax, ax
    mov es, ax
    call read_sector_lba
    jc .manifest_done

    mov esi, MANIFEST_BUF
    mov eax, [esi]              ; magic
    cmp eax, MANIFEST_MAGIC
    jne .manifest_done
    cmp dword [esi+4], MANIFEST_VERSION
    jne .manifest_done
    mov ecx, [esi+8]            ; entry_count
    cmp ecx, 0
    je .manifest_done

    mov edi, esi
    add edi, MANIFEST_OFFSET_ENTRIES
    mov si, msg_dbg_manifest_ok
    call print_str

    ; Find NORMAL entry first
.find_normal:
    cmp ecx, 0
    je .check_recovery
    cmp dword [edi], ENTRY_TYPE_NORMAL
    je .use_entry
    add edi, MANIFEST_ENTRY_SIZE
    dec ecx
    jmp .find_normal

.check_recovery:
    mov ecx, [esi+8]
    mov edi, esi
    add edi, MANIFEST_OFFSET_ENTRIES
.find_recovery:
    cmp ecx, 0
    je .manifest_done
    cmp dword [edi], ENTRY_TYPE_RECOVERY
    je .use_entry
    add edi, MANIFEST_ENTRY_SIZE
    dec ecx
    jmp .find_recovery

.use_entry:
    mov eax, [edi+4]            ; lba_start
    mov [kernel_lba_low_cached], eax
    xor eax, eax
    mov [kernel_lba_high], eax
    mov eax, [edi+8]            ; sector_count
    mov [kernel_sectors_cached], eax
    ; Debug: 'M' for manifest applied
    mov ah, 0x0E
    mov al, 'M'
    int 0x10
    mov si, msg_manifest_ok
    call print_str
    call print_crlf
    mov si, msg_dbg_manifest_entry
    call print_str

.manifest_done:
    ; Treat placeholders as unset
    mov eax, [kernel_sectors_cached]
    cmp eax, 0xBADC0FFE
    jne .chk_lba
    mov dword [kernel_sectors_cached], 0
.chk_lba:
    mov eax, [kernel_lba_low_cached]
    cmp eax, 0xFEEDFACE
    jne .after_manifest
    mov dword [kernel_lba_low_cached], 0
.after_manifest:
    ; If still unset, try handoff for kernel fields too
    mov al, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_SIG]
    cmp al, HANDOFF_SIG
    jne .after_handoff_kernel
    mov eax, [kernel_sectors_cached]
    cmp eax, 0
    jne .check_kernel_placeholder
    jmp .load_kernel_handoff
.check_kernel_placeholder:
    cmp eax, KERNEL_SECTORS_PLACEHOLDER
    jne .check_kernel_lba
.load_kernel_handoff:
    mov eax, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_KRNL_SEC]
    mov [kernel_sectors_cached], eax
.check_kernel_lba:
    mov eax, [kernel_lba_low_cached]
    cmp eax, 0
    jne .check_kernel_lba_placeholder
    jmp .load_kernel_lba_handoff
.check_kernel_lba_placeholder:
    cmp eax, KERNEL_LBA_PLACEHOLDER
    jne .after_handoff_kernel
.load_kernel_lba_handoff:
    mov eax, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_KRNL_LBA]
    mov [kernel_lba_low_cached], eax
    mov eax, [HANDOFF_STAGE2_BASE + HANDOFF_OFF_KRNL_LBA_H]
    mov [kernel_lba_high], eax
.after_handoff_kernel:
    ; Hard fallback for known kernel layout if still zero (LBA 0x805, sectors 0xFE)
    mov eax, [kernel_sectors_cached]
    cmp eax, 0
    jne .kf_done
    mov dword [kernel_sectors_cached], DEFAULT_KERNEL_SEC
    mov dword [kernel_lba_low_cached], DEFAULT_KERNEL_LBA
    xor eax, eax
    mov [kernel_lba_high], eax
.kf_done:

    mov si, msg_step_load
    call print_str
    call print_crlf
    call wait_space_timeout

    mov si, msg_dbg_preload
    call print_str

    ; If still zero, reread stage2 header directly from disk into temp buffer
    mov eax, [kernel_sectors_cached]
    cmp eax, 0
    jne .after_header_reload
    mov eax, [kernel_lba_low_cached]
    cmp eax, 0
    jne .after_header_reload
    ; read stage2 sector 0
    mov eax, [stage2_lba_low_cached]
    mov edx, [stage2_lba_high]
    mov bx, STAGE2_HDR_BUF
    xor ax, ax
    mov es, ax
    call read_sector_lba
    jc .after_header_reload
    ; parse header in temp buffer
    mov esi, STAGE2_HDR_BUF + 4      ; kernel_sectors offset 4
    call load_header_dword
    mov [kernel_sectors_cached], eax
    mov esi, STAGE2_HDR_BUF + 8      ; kernel_lba_low offset 8
    call load_header_dword
    mov [kernel_lba_low_cached], eax
    xor eax, eax
    mov [kernel_lba_high], eax
.after_header_reload:

    ; If still zero, force defaults again
    mov eax, [kernel_sectors_cached]
    cmp eax, 0
    jne .after_force_defaults
    mov dword [kernel_sectors_cached], DEFAULT_KERNEL_SEC
    mov dword [kernel_lba_low_cached], DEFAULT_KERNEL_LBA
    xor eax, eax
    mov [kernel_lba_high], eax
.after_force_defaults:

    ; Debug: show caches just before reading kernel
    mov ah, 0x0E
    mov al, 'C'
    int 0x10
    mov eax, [kernel_sectors_cached]
    call print_eax_hex
    mov eax, [kernel_lba_low_cached]
    call print_eax_hex
    mov si, msg_dbg_readstart
    call print_str

    ; Setup stack
    mov esp, STACK_REAL
    cmp dword [kernel_sectors_cached], 0
    je .not_patched
    
    ; Debug: Print 'K' for Kernel Start
    mov ah, 0x0E
    mov al, 'K'
    int 0x10

    ; Debug: print boot drive (from memory) and LBA/sector picked
    mov ah, 0x0E
    mov al, 'B'
    int 0x10
    mov dl, [boot_drive]
    call print_dl_hex
    mov ah, 0x0E
    mov al, 'L'
    int 0x10
    mov eax, [kernel_lba_low_cached]
    call print_eax_hex
    mov ah, 0x0E
    mov al, 'S'
    int 0x10
    mov eax, [kernel_sectors_cached]
    call print_eax_hex

    ; Backup original kernel params for retry attempts
    mov eax, [kernel_sectors_cached]
    mov [orig_kernel_sectors], eax
    mov eax, [kernel_lba_low_cached]
    mov [orig_kernel_lba], eax
    mov eax, [kernel_lba_high]
    mov [orig_kernel_lba_high], eax

    xor bx, bx              ; attempt index 0..2 (dl, 0x80, 0x81)
.retry_load:
    ; Select drive for this attempt
    cmp bx, 0
    jne .chk80
    mov dl, [boot_drive]
    jmp .drive_set
.chk80:
    cmp bx, 1
    jne .use81
    mov dl, 0x80
    jmp .drive_set
.use81:
    mov dl, 0x81
.drive_set:
    mov [boot_drive], dl

    ; Restore caches
    mov eax, [orig_kernel_sectors]
    mov [kernel_sectors_cached], eax
    mov eax, [orig_kernel_lba]
    mov [kernel_lba_low_cached], eax
    mov eax, [orig_kernel_lba_high]
    mov [kernel_lba_high], eax

    ; Setup for read_file_chunked
    mov ecx, [kernel_sectors_cached]
    mov eax, [kernel_lba_low_cached]
    ; Final override if caches look bogus
    cmp ecx, 0
    jne .use_cached_lba
    mov ecx, DEFAULT_KERNEL_SEC
    mov dword [kernel_sectors_cached], ecx
.use_cached_lba:
    cmp eax, 0
    jne .use_cached
    mov eax, DEFAULT_KERNEL_LBA
    mov dword [kernel_lba_low_cached], eax
.use_cached:
    mov edx, [kernel_lba_high]
    mov edi, KERNEL_BUF            ; unused by read_file_chunked? calls sets it internally

    ; Sanity check: max 512KB (1024 sectors) to avoid EBDA collision
    cmp ecx, 1024
    ja .too_big

    call read_file_chunked
    jc .next_attempt

    ; Validate kernel buffer magic before PM
    mov si, msg_dbg_check_kernel
    call print_str
    mov esi, KERNEL_BUF
    mov eax, [esi]
    cmp eax, 0x464C457F          ; ELF magic
    je .kernel_ok
    mov ah, 0x0E
    mov al, 'X'                  ; bad kernel magic
    int 0x10
    call print_eax_hex           ; show what was read

.next_attempt:
    inc bx
    cmp bx, 3
    jl .retry_load
    jmp boot_halt
.kernel_ok:

    ; success
    mov si, msg_step_pm
    call print_str
    call print_crlf
    call wait_space_timeout
    mov si, msg_dbg_pm
    call print_str
    jmp .boot

.too_big:
    ; Debug: 'B' for Big
    mov ah, 0x0E
    mov al, 'B'
    int 0x10
    jmp boot_halt

.not_patched:
    ; Debug: 'N' for Not Patched
    mov ah, 0x0E
    mov al, 'N'
    int 0x10
    cli
    hlt

.a20_fail:
    ; Debug: 'F' for A20 Failed
    mov ah, 0x0E
    mov al, 'F'
    int 0x10
    cli
    hlt

.boot:

    ; Debug: Print 'K' for Kernel Loaded
    mov ah, 0x0E
    mov al, 'K'
    int 0x10

    ; success path: kernel staged at KERNEL_BUF
    ; enable A20
    call enable_a20
    
    ; Verify A20 is enabled
    call verify_a20
    jc .a20_fail
    
    ; Debug: Print 'A' for A20 OK
    mov ah, 0x0E
    mov al, 'A'
    int 0x10
    
    ; load GDT
    lgdt [gdt_descriptor]
    
    ; Debug: Print 'G' for GDT loaded
    mov ah, 0x0E
    mov al, 'G'
    int 0x10
    
    ; Debug: Print 'P' for Protected Mode switch
    mov ah, 0x0E
    mov al, 'P'
    int 0x10

    ; enter protected mode
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEL:pm_entry

boot_halt:
    ; Debug: Print 'E' for Error
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
    mov si, msg_err_load
    call print_str
    mov al, '!'
    call print_err_red
    call print_crlf
    call wait_space_timeout
    cli
    hlt
    jmp boot_halt

; ------------------
; BIOS helpers (16-bit)

; read_sector_lba:
; IN: eax = lba low, edx = lba high, ES:BX = buffer
;     [boot_drive] contains drive
; OUT: CF clear on success, set on error
read_sector_lba:
    push ax
    push dx
    push si
    mov si, disk_packet
    mov word [si], 0x0010
    mov word [si+2], 1               ; 1 sector
    mov word [si+4], bx              ; offset
    mov word [si+6], es              ; segment
    mov dword [si+8], eax            ; lba low
    mov dword [si+12], edx           ; lba high
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop si
    pop dx
    pop ax
    ret

; read_multiple_lba:
; IN: eax=lba low, edx=lba high, cx=sector_count, di=dest (offset), ds=0
; OUT: CF set on error
read_multiple_lba:
    push ax
    push dx
    push cx
    push di
    push si
    mov si, disk_packet
    mov word [si], 0x0010
.next_chunk:
    cmp cx, 0
    je .done
    mov ax, cx
    cmp ax, 32               ; limit chunk to 32 sectors to keep packet small
    jbe .use_count
    mov ax, 32
.use_count:
    mov word [si+2], ax
    mov word [si+4], di
    mov word [si+6], 0x0000
    mov dword [si+8], eax    ; lba low
    mov dword [si+12], edx   ; lba high
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .error
    ; advance pointers
    mov bx, ax               ; chunk count
    shl bx, 9                ; chunk * 512 -> bytes
    add di, bx
    mov bx, ax               ; chunk count again
    add eax, ebx             ; advance lba by chunk sectors
    adc edx, 0
    sub cx, ax               ; consumed sectors (ax holds chunk)
    jmp .next_chunk
.done:
    pop si
    pop di
    pop cx
    pop dx
    pop ax
    clc
    ret
.error:
    pop si
    pop di
    pop cx
    pop dx
    pop ax
    stc
    ret



; read_file_chunked: uses CX=sectors, EAX=lba low, EDX=lba high, DI=dest offset
; sets kernel_bytes
; read_file_chunked: uses CX=sectors, EAX=lba low, EDX=lba high, DI=dest offset
; sets kernel_bytes
read_file_chunked:
    ; Debug: 'r' for read start
    mov ah, 0x0E
    mov al, 'r'
    int 0x10
    mov si, msg_dbg_read_chunk
    call print_str

    mov dword [kernel_sectors_cached], ecx
    mov dword [kernel_bytes], 0
    mov dword [current_lba_low], eax
    mov dword [current_lba_high], edx
    mov dword [buf_ptr], KERNEL_BUF

    ; Debug: print kernel sectors in hex
    mov eax, ecx
    call print_eax_hex
    
    ; Sanity check: max 8MB (16384 sectors)
    cmp ecx, 16384
    ja .rf_err

.rf_loop:
    mov ecx, [kernel_sectors_cached]
    cmp ecx, 0
    je .done

    ; Calculate chunk size (max 64 sectors = 32KB)
    mov ax, 64
    cmp cx, ax
    ja .use_Max
    mov ax, cx
.use_Max:
    ; AX = sectors to read in this chunk (max 64)
    push ax             ; save sector count
    
    ; Setup DAP
    mov si, disk_packet
    mov word [si], 0x0010
    mov word [si+2], ax ; sector count
    
    ; Calculate Seg:Off from buf_ptr (linear)
    ; Seg = (buf_ptr >> 4), Off = (buf_ptr & 0xF)
    mov eax, dword [buf_ptr]
    mov bx, ax
    and bx, 0xF         ; Offset (0-15)
    mov word [si+4], bx ; Buffer Offset
    
    shr eax, 4          ; Segment
    mov word [si+6], ax ; Buffer Segment
    
    mov eax, dword [current_lba_low]
    mov dword [si+8], eax
    mov eax, dword [current_lba_high]
    mov dword [si+12], eax

    ; Debug: print current LBA before read
    mov ah, 0x0E
    mov al, 'L'
    int 0x10
    mov eax, [current_lba_low]
    call print_eax_hex

    ; INT 13h Read
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .rf_err

    ; Debug: '.' per chunk
    mov ah, 0x0E
    mov al, '.'
    int 0x10

    pop ax              ; restore chunk sector count

    ; Update state
    movzx ebx, ax       ; ebx = sectors read

    ; Subtract from remaining sectors (kernel_sectors)
    mov ecx, [kernel_sectors_cached]
    sub ecx, ebx
    mov [kernel_sectors_cached], ecx

    ; Advance LBA
    add dword [current_lba_low], ebx
    adc dword [current_lba_high], 0

    ; Debug: show remaining sectors each loop
    mov ah, 0x0E
    mov al, '-'
    int 0x10
    mov eax, [kernel_sectors_cached]
    call print_eax_hex

    ; Advance Buffer Pointer (sectors * 512)
    shl ebx, 9          ; * 512
    add dword [buf_ptr], ebx
    
    jmp .rf_loop

.rf_err:
    ; Debug: 'E' on error
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
    mov eax, [current_lba_low]
    call print_eax_hex
    stc
    ret
.done:
    ; Calculate total bytes (original sectors * 512 is lost, we need to save original count or recalc)
    ; Actually kernel_bytes is used for checksum.
    ; We decremented kernel_sectors_cached to 0.
    ; Let's retrieve original sector count?
    ; Wait, the original code recalculated it at the end.
    ; We can't rely on the cached copy holding the original count anymore.
    ; But we printed it at the start.
    ; Let's just fix the caller or save it.

    ; Caller saved ECX in [di+12] probably? No.
    ; We can infer it from (buf_ptr - KERNEL_BUF).
    mov eax, [buf_ptr]
    sub eax, KERNEL_BUF
    mov [kernel_bytes], eax

    ; Debug: show first dword of kernel buffer
    mov eax, [KERNEL_BUF]
    call print_eax_hex

    clc
    ret

; Helper: Print EAX in Hex
print_eax_hex:
    pusha
    mov cx, 8           ; 8 hex digits
.hex_loop:
    rol eax, 4          ; rotate left 4 bits
    mov ebx, eax
    and ebx, 0x0F       ; mask execution nibble
    add bl, '0'
    cmp bl, '9'
    jbe .print_digit
    add bl, 7           ; 'A' - '9' - 1
.print_digit:
    mov ah, 0x0E
    mov al, bl
    int 0x10
    loop .hex_loop
    popa
    ret

; print_dl_hex: prints DL as 2-digit hex using print_eax_hex
print_dl_hex:
    push eax
    xor eax, eax
    mov al, dl
    call print_eax_hex
    pop eax
    ret


; checksum32: ESI=buffer, ECX=length bytes -> EAX=sum32
BITS 16
checksum32:
    push ebx
    push edx
    xor eax, eax
.cs_loop_dword:
    cmp ecx, 4
    jb .cs_tail
    mov edx, [esi]
    add eax, edx
    add esi, 4
    sub ecx, 4
    jmp .cs_loop_dword
.cs_tail:
    cmp ecx, 0
    je .cs_done
.cs_tail_loop:
    mov dl, [esi]
    movzx edx, dl
    add eax, edx
    inc esi
    dec ecx
    jnz .cs_tail_loop
.cs_done:
    pop edx
    pop ebx
    ret

; enable A20 via port 0x92
; enable_a20: Tries BIOS INT 15h, then Fast A20 (Port 92)
enable_a20:
    push ax
    ; Method 1: BIOS INT 15h, Function 2401h (Enable A20)
    mov ax, 0x2401
    int 0x15
    jnc .done       ; If CF=0, success
    cmp ah, 0
    je .done        ; If AH=0, success

    ; Method 2: Fast A20 (Port 92)
    in al, 0x92
    or al, 0x02
    out 0x92, al
    
.done:
    pop ax
    ret

; verify_a20: Check if A20 is actually enabled
; Returns: CF=0 if A20 enabled, CF=1 if A20 disabled
verify_a20:
    push ax
    push es
    push di
    
    ; Set ES to 0xFFFF
    mov ax, 0xFFFF
    mov es, ax
    
    ; Write 0x00 to FFFF:0010 (physical 0x100000)
    mov di, 0x0010
    mov byte [es:di], 0x00
    
    ; Write 0xFF to 0000:0000 (physical 0x00000)
    xor ax, ax
    mov es, ax
    mov byte [es:0x0000], 0xFF
    
    ; Read back FFFF:0010 - if A20 is disabled, this wraps to 0x00000 and reads 0xFF
    mov ax, 0xFFFF
    mov es, ax
    cmp byte [es:di], 0xFF
    
    pop di
    pop es
    pop ax
    
    je .a20_off      ; If values match, A20 is OFF (memory wrapped)
    clc              ; A20 is ON, clear carry
    ret
.a20_off:
    stc              ; A20 is OFF, set carry
    ret

; ------------------
; Protected-mode loader

BITS 32
pm_entry:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, PM_STACK

    ; DEBUG: 'M' - PM entry successful
    mov byte [0xB8000], 'M'
    mov byte [0xB8001], 0x1F  ; White on Blue

    mov esi, KERNEL_BUF
    
    ; DEBUG: 'R' - About to read from KERNEL_BUF
    mov byte [0xB8002], 'R'
    mov byte [0xB8003], 0x2F  ; White on Green
    
    mov eax, [esi]              ; ELF magic
    
    ; DEBUG: 'D' - Read done, show first byte as hex
    mov byte [0xB8004], 'D'
    mov byte [0xB8005], 0x4F  ; White on Red
    
    ; Show first byte (AL) as character at position 3
    mov byte [0xB8006], al      ; First byte of buffer
    mov byte [0xB8007], 0x0F    ; White on Black
    
    cmp eax, 0x464C457F
    jne pm_halt

    ; DEBUG: 'E' - ELF magic valid
    mov byte [0xB8002], 'E'
    mov byte [0xB8003], 0x2F  ; White on Green

    mov ecx, dword [kernel_bytes]
    ; ELF header fields
    mov eax, [esi+24]           ; e_entry
    mov [kernel_entry], eax
    mov ebx, [esi+28]           ; e_phoff
    movzx ecx, word [esi+44]    ; e_phnum (word) -> full ECX
    mov [ph_count], ecx         ; Store count in memory
    movzx edx, word [esi+42]    ; e_phentsize
    mov [ph_entsize], edx       ; Store entsize in memory
    add ebx, esi                ; ph base pointer = buf + e_phoff
    cmp ecx, 0
    je pm_halt
    
    ; DEBUG: 'L' - About to enter loop
    mov byte [0xB8004], 'L'
    mov byte [0xB8005], 0x4F  ; White on Red
    
ph_loop:
    ; Debug: print 'L' for each PT_LOAD iteration
    ; (Commented out to reduce overhead - uncomment for debug)
    ; push eax
    ; mov al, 'L'
    ; mov ah, 0x0E
    ; int 0x10  ; Note: Can't use INT in PM! Remove.
    ; pop eax
    
    cmp dword [ebx+0], 1        ; PT_LOAD?
    jne ph_next
    
    mov eax, [ebx+4]            ; p_offset
    add eax, esi                ; src = buf + offset
    mov edi, [ebx+8]            ; p_paddr (destination)
    mov ecx, [ebx+16]           ; p_filesz
    
    ; Save our state before calling memcpy
    push ebx                    ; Save PH pointer
    push esi                    ; Save kernel buffer base
    mov esi, eax                ; src for memcpy
    call pm_memcpy
    pop esi                     ; Restore kernel buffer base
    pop ebx                     ; Restore PH pointer
    
    ; Handle BSS (p_memsz > p_filesz)
    mov eax, [ebx+20]           ; p_memsz
    mov ecx, [ebx+16]           ; p_filesz (reload, was clobbered)
    cmp eax, ecx
    jbe ph_next
    
    ; Zero BSS section
    sub eax, ecx                ; bytes to zero = p_memsz - p_filesz
    mov edi, [ebx+8]            ; p_paddr
    add edi, ecx                ; dest = paddr + filesz
    mov ecx, eax
    call pm_memzero
    
ph_next:
    mov edx, [ph_entsize]       ; Reload entsize (may have been clobbered)
    add ebx, edx                ; Move to next program header
    
    ; Decrement counter
    mov ecx, [ph_count]
    dec ecx
    mov [ph_count], ecx
    jnz ph_loop

    ; Debug: Write 'X' to VGA to confirm ELF parsing done
    mov byte [0xB8000], 'X'
    mov byte [0xB8001], 0x4F  ; White on Red
    
    mov eax, 0x2BADB002
    mov ebx, 0
    jmp dword [kernel_entry]

pm_halt:
    cli
    hlt
    jmp pm_halt

; pm_memcpy: EDI=dst, ESI=src, ECX=len
pm_memcpy:
    cmp ecx, 0
    je .ret
.cpy:
    mov al, [esi]
    mov [edi], al
    inc esi
    inc edi
    dec ecx
    jnz .cpy
.ret:
    ret

; pm_memzero: EDI=dst, ECX=len
pm_memzero:
    cmp ecx, 0
    je .retz
.zloop:
    mov byte [edi], 0
    inc edi
    dec ecx
    jnz .zloop
.retz:
    ret

; ------------------
; Data
BITS 16
boot_drive          db 0
entry_max           db 0
; byte-wise reconstructed copies of header fields
kernel_sectors_cached    dd 0
kernel_lba_low_cached    dd 0
stage2_lba_low_cached    dd 0
stage2_sectors_cached    dd 0
; kernel_sectors etc moved to header
kernel_bytes        dd 0
kernel_entry        dd 0
kernel_lba_high     dd 0
orig_kernel_sectors dd 0
orig_kernel_lba     dd 0
orig_kernel_lba_high dd 0
current_lba_low     dd 0
current_lba_high    dd 0
buf_ptr             dd 0
ph_count            dd 0            ; Program header counter (for PM ELF loop)
ph_entsize          dd 0            ; Program header entry size
msg_dump_hdr      db 'Hdr: ',0
msg_wait_space      db ' [SPACE]',0
msg_step_start      db 's2: startP',0
msg_step_manifest   db 's2: manifest',0
msg_manifest_ok     db 's2: manifest OK',0
msg_step_load       db 's2: load kernel',0
msg_step_pm         db 's2: enter PM',0
msg_err_load        db 's2: ERR load',0
msg_dbg_start       db '[dbg] start defaults set',0
msg_dbg_hdr         db '[dbg] read header...',0
msg_dbg_hdr_done    db '[dbg] header done',0
msg_dbg_handoff     db '[dbg] handoff applied',0
msg_dbg_manifest_read db '[dbg] read manifest...',0
msg_dbg_manifest_ok db '[dbg] manifest ok',0
msg_dbg_manifest_entry db '[dbg] manifest entry applied',0
msg_dbg_preload     db '[dbg] preload checks',0
msg_dbg_readstart   db '[dbg] reading kernel',0
msg_dbg_read_chunk  db '[dbg] chunk loop',0
msg_dbg_pm          db '[dbg] jumping to PM',0
msg_dbg_check_kernel db '[dbg] checking kernel magic',0

; DAP buffer
disk_packet:
    dw 0x0010
    dw 0
    dw 0
    dw 0
    dd 0
    dd 0

; Stage2 location
; stage2_lba_low moved to header
stage2_lba_high dd 0
; stage2_sectors moved to header

; GDT (flat 32-bit)
gdt:
    dq 0x0000000000000000      ; null
    dq 0x00CF9A000000FFFF      ; code segment
    dq 0x00CF92000000FFFF      ; data segment
gdt_end:

gdt_descriptor:
    dw GDT_LIMIT
    dd GDT_BASE

%define CODE_SEL 0x08
%define DATA_SEL 0x10
