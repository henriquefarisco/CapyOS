; stage2.asm — Minimal BIOS loader for NoirOS
; - Loaded by stage1 to 0x0000:0x8000 (phys 0x8000).
; - Reads manifest from disk (absolute LBA), loads kernel ELF into buffer,
;   switches to protected mode and jumps to kernel entry with Multiboot magic.
; - Fallback: tries recovery entry if checksum falha.

BITS 16
ORG 0x8000

%define KERNEL_BUF       0x20000         ; staging buffer (real mode accessible)
%define MANIFEST_BUF     0x7E00          ; manifest 512B
%define STACK_REAL       0x7C00
%define PM_STACK         0x9FC00
%define GDT_BASE         gdt
%define GDT_LIMIT        (gdt_end - gdt - 1)
%define CODE_SEL         0x08
%define DATA_SEL         0x10

%define ENTRY_TYPE_NORMAL    1
%define ENTRY_TYPE_RECOVERY  2

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
    
    mov [boot_drive], dl

    ; Direct Kernel Boot (Embedded Info)
    ; Validate we have patched values (check for placeholders)
    cmp dword [kernel_sectors], 0xBADC0FFE
    je .not_patched
    cmp dword [kernel_sectors], 0
    je .not_patched
    
    ; Debug: Print 'K' for Kernel Start
    mov ah, 0x0E
    mov al, 'K'
    int 0x10

    ; Setup for read_file_chunked
    mov ecx, [kernel_sectors]
    mov eax, [kernel_lba_low]
    mov edx, [kernel_lba_high]
    mov edi, KERNEL_BUF            ; unused by read_file_chunked? calls sets it internally
    
    ; Sanity check: max 512KB (1024 sectors) to avoid EBDA collision
    cmp ecx, 1024
    ja .too_big
    
    call read_file_chunked
    jc boot_halt
    
    ; success
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

.boot:

    ; Debug: Print 'K' for Kernel Loaded
    mov ah, 0x0E
    mov al, 'K'
    int 0x10

    ; success path: kernel staged at KERNEL_BUF
    ; enable A20
    call enable_a20
    ; load GDT
    lgdt [gdt_descriptor]
    
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

    mov dword [kernel_sectors], ecx
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
    mov ecx, [kernel_sectors]
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
    mov ecx, [kernel_sectors]
    sub ecx, ebx
    mov [kernel_sectors], ecx

    ; Advance LBA
    add dword [current_lba_low], ebx
    adc dword [current_lba_high], 0
    
    ; Advance Buffer Pointer (sectors * 512)
    shl ebx, 9          ; * 512
    add dword [buf_ptr], ebx
    
    jmp .rf_loop

.rf_err:
    ; Debug: 'E' on error
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
    stc
    ret
.done:
    ; Calculate total bytes (original sectors * 512 is lost, we need to save original count or recalc)
    ; Actually kernel_bytes is used for checksum.
    ; We decremented kernel_sectors to 0. 
    ; Let's retrieve original sector count? 
    ; Wait, the original code recalculated it at the end.
    ; We can't rely on [kernel_sectors] being original count anymore.
    ; But we printed it at the start.
    ; Let's just fix the caller or save it.
    
    ; Caller saved ECX in [di+12] probably? No.
    ; We can infer it from (buf_ptr - KERNEL_BUF).
    mov eax, [buf_ptr]
    sub eax, KERNEL_BUF
    mov [kernel_bytes], eax
    
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
enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
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

    mov esi, KERNEL_BUF
    mov eax, [esi]              ; ELF magic
    cmp eax, 0x464C457F
    jne pm_halt

    mov ebx, dword [kernel_bytes]
    ; ELF header fields
    mov eax, [esi+24]           ; e_entry
    mov [kernel_entry], eax
    mov ebx, [esi+28]           ; e_phoff
    mov cx, [esi+44]            ; e_phnum (word)
    mov dx, [esi+42]            ; e_phentsize
    add ebx, esi                ; ph base pointer
    cmp cx, 0
    je pm_halt
ph_loop:
    cmp dword [ebx+0], 1        ; PT_LOAD
    jne ph_next
    mov eax, [ebx+4]            ; p_offset
    add eax, esi                ; src = buf + offset
    mov edi, [ebx+8]            ; p_paddr
    mov ecx, [ebx+16]           ; p_filesz
    push ebx
    push ecx
    push esi
    mov esi, eax
    call pm_memcpy
    pop esi
    pop ecx
    pop ebx
    mov eax, [ebx+20]           ; p_memsz
    cmp eax, ecx
    jbe ph_next
    sub eax, ecx
    add edi, ecx
    mov ecx, eax
    call pm_memzero
ph_next:
    add ebx, edx
    loop ph_loop

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
kernel_sectors      dd 0xBADC0FFE  ; Patched by installer
kernel_bytes        dd 0
kernel_entry        dd 0
kernel_lba_low      dd 0xFEEDFACE  ; Patched by installer
kernel_lba_high     dd 0
current_lba_low     dd 0
current_lba_high    dd 0
buf_ptr             dd 0

; DAP buffer
disk_packet:
    dw 0x0010
    dw 0
    dw 0
    dw 0
    dd 0
    dd 0

; Stage2 location (patched by installer)
stage2_lba_low  dd 0xDEADBEEF
stage2_lba_high dd 0
stage2_sectors  dd 0xCAFEBABE

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
