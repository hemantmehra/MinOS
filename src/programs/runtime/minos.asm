; MinOS-specific runtime functions (declared in include/minos.h), included
; after crtl.asm. Same conventions: cdecl, result in EAX, EDI preserved.

; char *read_line(const char *prompt, int max)
_read_line:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    sub esp, 8                  ; [ebp - 12] = length, [ebp - 16] = shift
    mov dword [ebp - 12], 0
    mov dword [ebp - 16], 0
    mov byte [edi + __rl_line - image_start], 0
    mov ebx, [ebp + 8]
    mov eax, 1
    int 0x80
.key:
    mov eax, 2
    int 0x80
    test eax, eax
    jz .key
    cmp al, 0x2A
    je .shift_down
    cmp al, 0x36
    je .shift_down
    cmp al, 0xAA
    je .shift_up
    cmp al, 0xB6
    je .shift_up
    test al, 0x80
    jnz .key
    cmp al, 0x1C
    je .done
    cmp al, 0x0E
    je .backspace
    cmp al, 0x3A
    jae .key
    movzx eax, al
    lea ecx, [edi + __rl_keys - image_start]
    cmp dword [ebp - 16], 0
    je .lookup
    add ecx, 0x3A
.lookup:
    mov al, [ecx + eax]
    test al, al
    jz .key
    mov ecx, [ebp - 12]
    cmp ecx, [ebp + 12]
    jae .key
    mov [edi + __rl_line - image_start + ecx], al
    mov byte [edi + __rl_line - image_start + ecx + 1], 0
    mov [edi + __rl_echo - image_start], al
    inc dword [ebp - 12]
    lea ebx, [edi + __rl_echo - image_start]
    mov eax, 1
    int 0x80
    jmp .key
.shift_down:
    mov dword [ebp - 16], 1
    jmp .key
.shift_up:
    mov dword [ebp - 16], 0
    jmp .key
.backspace:
    cmp dword [ebp - 12], 0
    je .key
    dec dword [ebp - 12]
    mov ecx, [ebp - 12]
    mov byte [edi + __rl_line - image_start + ecx], 0
    lea ebx, [edi + __rl_back - image_start]
    mov eax, 1
    int 0x80
    jmp .key
.done:
    lea ebx, [edi + __rl_newline - image_start]
    mov eax, 1
    int 0x80
    lea eax, [edi + __rl_line - image_start]
    add esp, 8
    pop esi
    pop ebx
    pop ebp
    ret

; int parse_int(const char *text, int offset)
_parse_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov esi, [ebp + 8]
    add esi, [ebp + 12]
    xor ebx, ebx
    xor ecx, ecx
.loop:
    movzx eax, byte [esi]
    test al, al
    jz .end
    cmp al, '0'
    jb .invalid
    cmp al, '9'
    ja .invalid
    imul ebx, ebx, 10
    sub eax, '0'
    add ebx, eax
    inc esi
    inc ecx
    jmp .loop
.end:
    test ecx, ecx
    jz .invalid
    mov eax, ebx
    jmp .return
.invalid:
    mov eax, -1
.return:
    pop esi
    pop ebx
    pop ebp
    ret

; Scancode (set 1) to character tables: unshifted, then shifted.
__rl_keys:
    db 0, 0
    db "1234567890-=", 0, 0
    db "qwertyuiop[]", 0, 0
    db "asdfghjkl;'`", 0
    db "\zxcvbnm,./", 0, 0, 0
    db " "
    db 0, 0
    db "!@#$%^&*()_+", 0, 0
    db "QWERTYUIOP{}", 0, 0
    db "ASDFGHJKL:", 34, "~", 0
    db "|ZXCVBNM<>?", 0, 0, 0
    db " "
__rl_echo: db 0, 0
__rl_back: db 8, 0
__rl_newline: db 10, 0
__rl_line: times 129 db 0
