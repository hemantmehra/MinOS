BITS 16
ORG 0x7C00

%define KERNEL_SECTOR_COUNT 32
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
    dw KERNEL_SECTOR_COUNT
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
