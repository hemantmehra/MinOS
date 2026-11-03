BITS 32
ORG 0x40000000

image_start:
    db "MEXE"
    dd image_end - image_start
    dd entry_point - image_start

entry_point:
    call get_runtime_base
get_runtime_base:
    pop ebp
    sub ebp, get_runtime_base - image_start

    lea ebx, [ebp + input_prompt - image_start]
    call print_string

read_input:
    mov eax, 2
    int 0x80
    test eax, eax
    jz read_input

    cmp al, 0x1C
    je input_complete
    cmp al, 0x0E
    je erase_input
    cmp al, 0x02
    jb read_input
    cmp al, 0x0B
    ja read_input

    mov ecx, [ebp + input_length - image_start]
    cmp ecx, 2
    jae input_too_long

    cmp al, 0x0B
    jne input_digit_one_to_nine
    mov al, '0'
    jmp store_input_digit

input_digit_one_to_nine:
    add al, 0x2F

store_input_digit:
    mov [ebp + input_buffer - image_start + ecx], al
    inc ecx
    mov [ebp + input_length - image_start], ecx
    mov [ebp + echo_character - image_start], al
    lea ebx, [ebp + echo_character - image_start]
    call print_string
    jmp read_input

erase_input:
    mov ecx, [ebp + input_length - image_start]
    test ecx, ecx
    jz read_input
    dec ecx
    mov [ebp + input_length - image_start], ecx
    mov byte [ebp + input_buffer - image_start + ecx], 0
    lea ebx, [ebp + backspace_character - image_start]
    call print_string
    jmp read_input

input_complete:
    lea ebx, [ebp + newline_character - image_start]
    call print_string
    cmp dword [ebp + input_length - image_start], 0
    je invalid_input

    xor esi, esi
    xor ecx, ecx
parse_input:
    cmp ecx, [ebp + input_length - image_start]
    jae input_parsed
    imul esi, esi, 10
    movzx eax, byte [ebp + input_buffer - image_start + ecx]
    sub eax, '0'
    add esi, eax
    inc ecx
    jmp parse_input

input_parsed:
    cmp esi, 12
    ja invalid_input

    mov eax, 1
    mov ecx, esi
calculate_factorial:
    cmp ecx, 1
    jbe factorial_complete
    imul eax, ecx
    dec ecx
    jmp calculate_factorial

factorial_complete:
    mov [ebp + factorial_result - image_start], eax
    lea ebx, [ebp + result_prefix - image_start]
    call print_string
    lea ebx, [ebp + input_buffer - image_start]
    call print_string
    lea ebx, [ebp + result_separator - image_start]
    call print_string
    call format_result
    call print_string
    lea ebx, [ebp + newline_character - image_start]
    call print_string
    jmp exit_program

input_too_long:
    lea ebx, [ebp + range_error - image_start]
    call print_string
    jmp exit_program

invalid_input:
    lea ebx, [ebp + range_error - image_start]
    call print_string

exit_program:
    mov eax, 0
    int 0x80

halt:
    jmp halt

print_string:
    mov eax, 1
    int 0x80
    ret

format_result:
    mov eax, [ebp + factorial_result - image_start]
    lea edi, [ebp + result_buffer - image_start + 10]
    mov byte [edi], 0

format_result_digit:
    dec edi
    xor edx, edx
    mov ebx, 10
    div ebx
    add dl, '0'
    mov [edi], dl
    test eax, eax
    jnz format_result_digit

    mov ebx, edi
    ret

image_data:
input_prompt:
    db "Enter n (0-12): ", 0
result_prefix:
    db "n! = ", 0
result_separator:
    db " = ", 0
range_error:
    db "Input out of range; use 0-12.", 10, 0
newline_character:
    db 10, 0
backspace_character:
    db 8, 0
echo_character:
    db 0, 0
input_length:
    dd 0
input_buffer:
    times 3 db 0
factorial_result:
    dd 0
result_buffer:
    times 11 db 0

image_end:
