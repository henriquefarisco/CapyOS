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
    mov [boot_drive], dl

    ; manifest LBA = stage2_lba + stage2_sectors
    mov eax, [stage2_lba_low]
    mov edx, [stage2_lba_high]
    mov [manifest_lba_high], edx
    add eax, [stage2_sectors]        ; sectors is 32-bit placeholder
    adc dword [manifest_lba_high], 0
    mov [manifest_lba_low], eax

    ; read manifest (1 sector)
    mov bx, MANIFEST_BUF
    mov eax, [manifest_lba_low]
    mov edx, [manifest_lba_high]
    call read_sector_lba
    jc boot_halt

    ; validate magic "NIBT"
    mov si, MANIFEST_BUF
    mov eax, [si]
    cmp eax, 0x5442494E
    jne boot_halt
    mov ecx, [si+8]                  ; entry_count
    cmp ecx, 4
    jbe .store_count
    mov ecx, 4
.store_count:
    mov byte [entry_max], cl
    mov si, MANIFEST_BUF + 16        ; first entry
    call try_entries
    jc boot_halt                     ; failure

    ; success path: kernel staged at KERNEL_BUF, length in kernel_bytes
    ; enable A20
    call enable_a20
    ; load GDT
    lgdt [gdt_descriptor]
    ; enter protected mode
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEL:pm_entry

boot_halt:
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
    mov [si+8], eax                  ; lba low
    mov [si+12], edx                 ; lba high
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
    mov [si+8], eax          ; lba low
    mov [si+12], edx         ; lba high
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .error
    ; advance pointers
    add di, ax
    shl ax, 9                ; ax * 512 -> bytes
    add di, 0                ; offset already advanced
    add eax, ax              ; advance lba by chunk
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

; try_entries:
; si -> first entry, entry_max holds count
; returns CF set on failure, cleared on success
try_entries:
    xor bx, bx
    mov cl, [entry_max]
    cmp cl, 0
    je .fail
    mov di, MANIFEST_BUF + 16
.loop_entries:
    mov eax, [di]            ; type
    cmp eax, ENTRY_TYPE_NORMAL
    je .try
    cmp eax, ENTRY_TYPE_RECOVERY
    jne .next
.try:
    push di
    mov eax, [di+4]          ; lba
    mov edx, [di+8]          ; lba high (unused)
    mov ecx, [di+12]         ; sectors
    mov [kernel_sectors], ecx
    mov di, KERNEL_BUF
    call read_file_chunked
    pop di
    jc .next
    ; verify checksum
    mov esi, KERNEL_BUF
    mov ecx, [kernel_bytes]
    call checksum32
    cmp eax, [di+16]
    jne .next
    clc
    ret
.next:
    add di, 20
    loop .loop_entries
.fail:
    stc
    ret

; read_file_chunked: uses CX=sectors, EAX=lba low, EDX=lba high, DI=dest offset
; sets kernel_bytes
read_file_chunked:
    mov [kernel_sectors], ecx
    mov dword [kernel_bytes], 0
    mov [current_lba_low], eax
    mov [current_lba_high], edx
    mov dword [buf_ptr], KERNEL_BUF
.rf_loop:
    cmp ecx, 0
    je .done
    ; set ES:BX from buf_ptr
    mov eax, [buf_ptr]
    mov bx, ax
    and bx, 0xF
    shr eax, 4
    mov es, ax

    mov eax, [current_lba_low]
    mov edx, [current_lba_high]
    call read_sector_lba
    jc .rf_err
    ; advance buffer pointer
    mov eax, [buf_ptr]
    add eax, 512
    mov [buf_ptr], eax
    ; advance LBA
    mov eax, [current_lba_low]
    add eax, 1
    mov [current_lba_low], eax
    adc dword [current_lba_high], 0
    dec ecx
    jmp .rf_loop
.rf_err:
    stc
    ret
.done:
    mov eax, [kernel_sectors]
    mov ebx, 512
    mul ebx               ; edx:eax = sectors*512
    mov [kernel_bytes], eax
    clc
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

    mov ebx, [kernel_bytes]
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
kernel_sectors      dd 0
kernel_bytes        dd 0
kernel_entry        dd 0
manifest_lba_low    dd 0
manifest_lba_high   dd 0
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

times 510-($-$$) db 0
dw 0xAA55
