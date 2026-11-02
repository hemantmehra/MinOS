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
    lea ebx, [esi + message - return_address]
    int 0x80

    mov eax, 0
    int 0x80

halt:
    jmp halt

message:
    db "Hello world from process!", 10, 0

image_end:
