; MinOS-specific runtime functions (declared in include/minos.h), included
; after crtl.asm. Same conventions: cdecl, result in EAX, EDI preserved.

; System call wrappers. Kernel registers: EBX, ECX and EDX carry up to three
; arguments, EAX is the service number and the result.
%macro SYS0 2                   ; no arguments
_%1:
    mov eax, %2
    int 0x80
    ret
%endmacro
%macro SYS1 2                   ; EBX = arg0
_%1:
    push ebx
    mov ebx, [esp + 8]
    mov eax, %2
    int 0x80
    pop ebx
    ret
%endmacro
%macro SYS3 2                   ; EBX = arg0, ECX = arg1, EDX = arg2
_%1:
    push ebx
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    mov eax, %2
    int 0x80
    pop ebx
    ret
%endmacro
%macro SYSBUF 3                 ; EBX = arg0, ECX = arg1 buffer, EDX = capacity
_%1:
    push ebx
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    mov edx, %3
    mov eax, %2
    int 0x80
    pop ebx
    ret
%endmacro
%macro SYSOUT 3                 ; ECX = arg0 buffer, EDX = capacity
_%1:
    mov ecx, [esp + 4]
    mov edx, %3
    mov eax, %2
    int 0x80
    ret
%endmacro

SYS0 clock, 4
SYS0 memory_report, 5
SYS0 process_list, 6
SYS0 make_directory, 7
SYS0 terminal_clear, 8
SYS0 start_graphics_mode, 9
SYS0 end_graphics_mode, 10
SYS1 opendir, 12
SYS1 closedir, 13
SYSBUF read_dir, 14, 256
SYSOUT get_current_directory, 16, 128
SYS0 change_directory, 17
SYS1 open_file, 18
SYSBUF read_file, 19, 256
SYS1 close_file, 20
SYSOUT read_pipe, 21, 256
SYS3 write_file, 22
SYS1 truncate_file, 23
SYSBUF read_stream, 24, 256
SYS3 write_stream, 25
SYS0 kill_process, 26
SYS1 clear_graphics, 27
SYS0 present, 29
SYS1 create_process, 30
SYS1 wait_process, 31
SYS3 create_window, 32
SYS3 start_process, 33
SYS0 terminal_home, 34
SYS1 sleep, 35
SYSBUF get_environment, 40, 128
SYS1 set_environment, 41
SYS0 read_key, 2

; void draw_pixel(int x, int y, int color): EBX = x, EDX = y, ECX = color
_draw_pixel:
    push ebx
    mov ebx, [esp + 8]
    mov edx, [esp + 12]
    mov ecx, [esp + 16]
    mov eax, 28
    int 0x80
    pop ebx
    ret

; int random(int limit): a pseudo-random value in [0, limit).
_random:
    mov ecx, [esp + 4]
    rdtsc
    xor eax, edx
    xor edx, edx
    div ecx
    mov eax, edx
    ret
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

; int key_char(int scancode, int shift): the character for a set 1 make code,
; or 0 when the key has none. Uses the same tables as read_line.
_key_char:
    mov eax, [esp + 4]
    cmp eax, 0x3A
    jae .none
    lea ecx, [edi + __rl_keys - image_start]
    cmp dword [esp + 8], 0
    je .lookup
    add ecx, 0x3A
.lookup:
    movzx eax, byte [ecx + eax]
    ret
.none:
    xor eax, eax
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
