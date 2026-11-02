BITS 32
ORG 0x40000000

image_start:
    db "MEXE"
    dd image_end - image_start
    dd entry_point - image_start

entry_point:
    mov eax, 1
    call $ + 5
return_address:
    pop esi
    lea ebx, [esi + countdown_message - return_address]
    int 0x80

    mov eax, 0
    int 0x80

halt:
    jmp halt

countdown_message:
    db "10", 10
    db "9", 10
    db "8", 10
    db "7", 10
    db "6", 10
    db "5", 10
    db "4", 10
    db "3", 10
    db "2", 10
    db "1", 10, 0

image_end:
