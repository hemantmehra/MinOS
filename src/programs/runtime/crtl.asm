; MinOS C runtime library, included at the top of every minc program.
; Provides the MEXE header, the program entry point and the library functions
; declared in include/stdio.h. Functions use cdecl, return in EAX, and must
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
    fninit
    fnstcw [edi + __rt_cw - image_start]
    or word [edi + __rt_cw - image_start], 0x0C00
    fldcw [edi + __rt_cw - image_start]
    call _main
    mov eax, 0
    int 0x80
__halt:
    jmp __halt

; int printf(const char *format, ...): writes formatted text to standard
; output. Supports %d, %u, %x, %c, %s, %f (%.Nf) and %%; every argument is a 32-bit
; value on the stack (cdecl). Returns 0. The print system call only accepts
; pointers inside the process image, so text is staged in image buffers.
_printf:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    lea eax, [ebp + 12]
    push eax                    ; [ebp - 12] = next argument
    push dword 6                ; [ebp - 16] = precision
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
    mov dword [ebp - 16], 6
    movzx eax, byte [esi]
    cmp al, '.'
    jne .spec
    inc esi
    xor ecx, ecx
.prec:
    movzx eax, byte [esi]
    sub eax, '0'
    cmp eax, 9
    ja .precdone
    imul ecx, 10
    add ecx, eax
    inc esi
    jmp .prec
.precdone:
    mov [ebp - 16], ecx
.spec:
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
    cmp al, 'f'
    je .float
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
.float:
    mov ecx, [ebp - 12]
    fld qword [ecx]
    add dword [ebp - 12], 8
    test dword [ecx + 4], 0x80000000
    jz .fpos
    fchs
    mov al, '-'
    call __rt_putc
.fpos:
    mov ecx, [ebp - 16]
    cmp ecx, 9
    jbe .fprec
    mov ecx, 9
    mov [ebp - 16], ecx
.fprec:
    mov ebx, 1
.fscale:
    test ecx, ecx
    jz .fsplit
    imul ebx, 10
    dec ecx
    jmp .fscale
.fsplit:
    sub esp, 12                 ; [esp] integer, [esp+4] scale, [esp+8] fraction
    mov [esp + 4], ebx
    fld st0
    fistp dword [esp]
    fild dword [esp]
    fsubp st1, st0
    fild dword [esp + 4]
    fmulp st1, st0
    fld dword [edi + __rt_half - image_start]
    faddp st1, st0
    fistp dword [esp + 8]
    mov eax, [esp + 8]
    cmp eax, [esp + 4]
    jb .nocarry
    sub eax, [esp + 4]
    mov [esp + 8], eax
    inc dword [esp]
.nocarry:
    mov eax, [esp]
    mov ecx, 10
    call __rt_putnum
    cmp dword [ebp - 16], 0
    je .fdone
    mov al, '.'
    call __rt_putc
.fdigit:
    mov eax, [esp + 4]
    xor edx, edx
    mov ecx, 10
    div ecx
    mov [esp + 4], eax
    mov eax, [esp + 8]
    xor edx, edx
    div dword [esp + 4]
    mov [esp + 8], edx
    add al, '0'
    call __rt_putc
    dec dword [ebp - 16]
    jnz .fdigit
.fdone:
    add esp, 12
    jmp .next
.done:
    xor eax, eax
    add esp, 8
    pop esi
    pop ebx
    pop ebp
    ret

; int strcmp(const char *a, const char *b): 0 when equal, otherwise the
; difference of the first differing bytes.
_strcmp:
    push ebp
    mov ebp, esp
    push esi
    push ebx
    mov esi, [ebp + 8]
    mov ebx, [ebp + 12]
.loop:
    movzx eax, byte [esi]
    movzx ecx, byte [ebx]
    sub eax, ecx
    jnz .done
    test ecx, ecx
    jz .done
    inc esi
    inc ebx
    jmp .loop
.done:
    pop ebx
    pop esi
    pop ebp
    ret

; int starts_with(const char *text, const char *prefix): 1 or 0.
_starts_with:
    push ebp
    mov ebp, esp
    push esi
    push ebx
    mov esi, [ebp + 8]
    mov ebx, [ebp + 12]
.loop:
    movzx eax, byte [ebx]
    test eax, eax
    jz .yes
    cmp al, [esi]
    jne .no
    inc esi
    inc ebx
    jmp .loop
.yes:
    mov eax, 1
    jmp .done
.no:
    xor eax, eax
.done:
    pop ebx
    pop esi
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

__rt_cw: dw 0
__rt_half: dd 0x3F000000
__rt_char: db 0, 0
__rt_digits: times 11 db 0
    db 0
