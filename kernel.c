#include <intrin.h>

void __cdecl kernel_main(void)
{
    const char *message = "Hello, world!";
    volatile unsigned short *video_memory =
        (volatile unsigned short *)0xB8000;

    for (unsigned int index = 0; message[index] != '\0'; ++index)
    {
        unsigned char character = (unsigned char)message[index];

        video_memory[index] = (unsigned short)(0x0F00 | character);
        __outbyte(0xE9, character);
    }

    __outbyte(0xE9, '\n');

    __asm
    {
        cli
    halt:
        hlt
        jmp halt
    }
}
