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

; int printf(const char *format, ...): writes formatted text to standard
; output. Supports %d, %u, %x, %c, %s and %%; every argument is a 32-bit
; value on the stack (cdecl). Returns 0. The print system call only accepts
; pointers inside the process image, so text is staged in image buffers.
_printf:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    lea eax, [ebp + 12]
    push eax                    ; [ebp - 12] = next argument
    mov esi, [ebp + 8]
.next:
    movzx eax, byte [esi]
    test eax, eax
    jz .done
    inc esi
    cmp al, '%'
    je .format
.literal:
    call __rt_putc
    jmp .next
.format:
    movzx eax, byte [esi]
    test eax, eax
    jz .done
    inc esi
    cmp al, 'd'
    je .signed
    cmp al, 'u'
    je .unsigned
    cmp al, 'x'
    je .hex
    cmp al, 's'
    je .string
    cmp al, 'c'
    je .char
    cmp al, '%'
    je .literal
    push eax
    mov al, '%'
    call __rt_putc
    pop eax
    jmp .literal
.signed:
    mov ecx, [ebp - 12]
    mov eax, [ecx]
    add dword [ebp - 12], 4
    test eax, eax
    jns .signed_digits
    push eax
    mov al, '-'
    call __rt_putc
    pop eax
    neg eax
.signed_digits:
    mov ecx, 10
    call __rt_putnum
    jmp .next
.unsigned:
    mov ecx, [ebp - 12]
    mov eax, [ecx]
    add dword [ebp - 12], 4
    mov ecx, 10
    call __rt_putnum
    jmp .next
.hex:
    mov ecx, [ebp - 12]
    mov eax, [ecx]
    add dword [ebp - 12], 4
    mov ecx, 16
    call __rt_putnum
    jmp .next
.char:
    mov ecx, [ebp - 12]
    mov eax, [ecx]
    add dword [ebp - 12], 4
    jmp .literal
.string:
    mov ecx, [ebp - 12]
    mov ebx, [ecx]
    add dword [ebp - 12], 4
    test ebx, ebx
    jz .next
    mov eax, 1
    int 0x80
    jmp .next
.done:
    xor eax, eax
    add esp, 4
    pop esi
    pop ebx
    pop ebp
    ret

; Writes the character in AL. Preserves all registers except EAX.
__rt_putc:
    push ebx
    mov [edi + __rt_char - image_start], al
    lea ebx, [edi + __rt_char - image_start]
    mov eax, 1
    int 0x80
    pop ebx
    ret

; Writes the unsigned value in EAX in base ECX (10 or 16). Preserves ESI.
__rt_putnum:
    push ebx
    push esi
    lea esi, [edi + __rt_digits - image_start + 11]
    mov ebx, ecx
.digit:
    xor edx, edx
    div ebx
    add dl, '0'
    cmp dl, '9'
    jbe .store
    add dl, 39
.store:
    dec esi
    mov [esi], dl
    test eax, eax
    jnz .digit
    mov ebx, esi
    mov eax, 1
    int 0x80
    pop esi
    pop ebx
    ret

__rt_char: db 0, 0
__rt_digits: times 11 db 0
    db 0
