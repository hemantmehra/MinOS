BITS 16
ORG 0x7C00

%define KERNEL_SECTOR_COUNT 320
%define KERNEL_FIRST_READ_SECTOR_COUNT 127
%define CODE_SELECTOR 0x08
%define DATA_SELECTOR 0x10

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    mov si, disk_address_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_read_error

    mov word [disk_address_packet + 2], 127
    mov word [disk_address_packet + 6], 0x1FE0
    mov dword [disk_address_packet + 8], KERNEL_FIRST_READ_SECTOR_COUNT + 1
    mov dword [disk_address_packet + 12], 0
    mov si, disk_address_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_read_error

    mov word [disk_address_packet + 2], KERNEL_SECTOR_COUNT - 254
    mov word [disk_address_packet + 6], 0x2FC0
    mov dword [disk_address_packet + 8], 255
    mov si, disk_address_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_read_error

    mov ax, 0x1130
    mov bh, 0x06
    int 0x10
    push ds
    push es
    pop ds
    mov si, bp
    mov ax, 0x0500
    mov es, ax
    xor di, di
    mov cx, 128 * 16
    cld
    rep movsb
    pop ds
    xor ax, ax
    mov es, ax

    cli
    in al, 0x92
    or al, 0x02
    out 0x92, al

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SELECTOR:protected_mode

disk_read_error:
    mov si, error_message
    mov ah, 0x0E
    mov bx, 0x0007
.print:
    lodsb
    test al, al
    jz .halt
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

boot_drive:
    db 0

align 4
disk_address_packet:
    db 0x10, 0
    dw KERNEL_FIRST_READ_SECTOR_COUNT
    dw 0
    dw 0x1000
    dq 1

error_message:
    db "Disk read error.", 0

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BITS 32
protected_mode:
    mov ax, DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    cld
    mov eax, [kernel_entry]
    jmp eax

kernel_entry:
    dd 0xC0DEF00D

times 510 - ($ - $$) db 0
dw 0xAA55
