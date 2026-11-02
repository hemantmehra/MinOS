BITS 32
ORG 0x40000000

%define FRAMEBUFFER 0xD0000000
%define SCREEN_WIDTH 1280
%define SCREEN_HEIGHT 720

image_start:
    db "MEXE"
    dd image_end - image_start
    dd entry_point - image_start

entry_point:
    cld
    call get_runtime_base
get_runtime_base:
    pop ebp
    sub ebp, get_runtime_base - image_start

    mov edi, FRAMEBUFFER
    mov ecx, SCREEN_WIDTH * SCREEN_HEIGHT
    mov al, 15
    rep stosb

    call draw_circle
    call draw_triangle
    call draw_rectangle

wait_for_escape:
    mov eax, 2
    int 0x80
    cmp eax, 1
    jne wait_for_escape

    mov eax, 0
    int 0x80

halt:
    jmp halt

draw_circle:
    mov dword [ebp + circle_x - image_start], 130
    mov dword [ebp + circle_y - image_start], 0
    mov dword [ebp + circle_error - image_start], -129

circle_loop:
    mov eax, [ebp + circle_x - image_start]
    cmp eax, [ebp + circle_y - image_start]
    jl circle_done

    mov eax, 260
    add eax, [ebp + circle_x - image_start]
    mov ebx, 360
    add ebx, [ebp + circle_y - image_start]
    mov ecx, 12
    call draw_pixel

    mov eax, 260
    sub eax, [ebp + circle_x - image_start]
    mov ebx, 360
    add ebx, [ebp + circle_y - image_start]
    mov ecx, 12
    call draw_pixel

    mov eax, 260
    add eax, [ebp + circle_x - image_start]
    mov ebx, 360
    sub ebx, [ebp + circle_y - image_start]
    mov ecx, 12
    call draw_pixel

    mov eax, 260
    sub eax, [ebp + circle_x - image_start]
    mov ebx, 360
    sub ebx, [ebp + circle_y - image_start]
    mov ecx, 12
    call draw_pixel

    mov eax, 260
    add eax, [ebp + circle_y - image_start]
    mov ebx, 360
    add ebx, [ebp + circle_x - image_start]
    mov ecx, 12
    call draw_pixel

    mov eax, 260
    sub eax, [ebp + circle_y - image_start]
    mov ebx, 360
    add ebx, [ebp + circle_x - image_start]
    mov ecx, 12
    call draw_pixel

    mov eax, 260
    add eax, [ebp + circle_y - image_start]
    mov ebx, 360
    sub ebx, [ebp + circle_x - image_start]
    mov ecx, 12
    call draw_pixel

    mov eax, 260
    sub eax, [ebp + circle_y - image_start]
    mov ebx, 360
    sub ebx, [ebp + circle_x - image_start]
    mov ecx, 12
    call draw_pixel

    inc dword [ebp + circle_y - image_start]
    cmp dword [ebp + circle_error - image_start], 0
    jl circle_error_negative

    dec dword [ebp + circle_x - image_start]
    mov eax, [ebp + circle_x - image_start]
    sub eax, [ebp + circle_y - image_start]
    shl eax, 1
    inc eax
    add [ebp + circle_error - image_start], eax
    jmp circle_loop

circle_error_negative:
    mov eax, [ebp + circle_y - image_start]
    shl eax, 1
    inc eax
    add [ebp + circle_error - image_start], eax
    jmp circle_loop

circle_done:
    ret

draw_triangle:
    mov dword [ebp + triangle_y - image_start], 200

triangle_row:
    mov eax, [ebp + triangle_y - image_start]
    sub eax, 200
    shr eax, 1
    mov [ebp + triangle_left - image_start], eax
    mov [ebp + triangle_right - image_start], eax
    neg dword [ebp + triangle_left - image_start]
    add dword [ebp + triangle_left - image_start], 640
    add dword [ebp + triangle_right - image_start], 640
    mov eax, [ebp + triangle_left - image_start]
    mov [ebp + triangle_x - image_start], eax

triangle_pixel:
    mov eax, [ebp + triangle_x - image_start]
    mov ebx, [ebp + triangle_y - image_start]
    mov ecx, 10
    call draw_pixel
    inc dword [ebp + triangle_x - image_start]
    mov eax, [ebp + triangle_x - image_start]
    cmp eax, [ebp + triangle_right - image_start]
    jle triangle_pixel

    inc dword [ebp + triangle_y - image_start]
    cmp dword [ebp + triangle_y - image_start], 480
    jle triangle_row
    ret

draw_rectangle:
    mov dword [ebp + rectangle_y - image_start], 230

rectangle_row:
    mov dword [ebp + rectangle_x - image_start], 930

rectangle_pixel:
    mov eax, [ebp + rectangle_x - image_start]
    mov ebx, [ebp + rectangle_y - image_start]
    mov ecx, 9
    call draw_pixel
    inc dword [ebp + rectangle_x - image_start]
    cmp dword [ebp + rectangle_x - image_start], 1170
    jle rectangle_pixel

    inc dword [ebp + rectangle_y - image_start]
    cmp dword [ebp + rectangle_y - image_start], 490
    jle rectangle_row
    ret

draw_pixel:
    pushad
    mov edi, ebx
    imul edi, SCREEN_WIDTH
    add edi, eax
    add edi, FRAMEBUFFER
    mov [edi], cl
    popad
    ret

circle_x: dd 0
circle_y: dd 0
circle_error: dd 0
triangle_y: dd 0
triangle_left: dd 0
triangle_right: dd 0
triangle_x: dd 0
rectangle_y: dd 0
rectangle_x: dd 0

image_end:
