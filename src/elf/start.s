.section .text.start,"ax",@progbits
.global _start
.type _start,@function
_start:
    cld
    andl $-16, %esp
    call minos_runtime_start
1:
    hlt
    jmp 1b
