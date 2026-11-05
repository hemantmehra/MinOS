; MinOS C runtime library, included at the top of every minc program.
; Provides the MEXE header, the program entry point and the library functions
; declared in include/minos.h. Functions use cdecl, return in EAX, and must
; preserve EDI, which holds the image's load address (the image is relocated
; by the loader, so all data is addressed relative to EDI).

image_start:
    db "MEXE"
    dd image_end - image_start
    dd __entry - image_start

__entry:
    cld
    call __get_image_base
__get_image_base:
    pop edi
    sub edi, __get_image_base - image_start
    call _main
    mov eax, 0
    int 0x80
__halt:
    jmp __halt

; int print(const char *str): writes a null-terminated string to standard
; output. Returns 0.
_print:
    push ebx
    mov ebx, [esp + 8]
    mov eax, 1
    int 0x80
    xor eax, eax
    pop ebx
    ret
