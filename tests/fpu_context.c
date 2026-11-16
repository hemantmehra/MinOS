#include <stdio.h>
#include <string.h>

static int syscall1(int number, int argument)
{
    int result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number), "b"(argument) : "memory", "cc");
    return result;
}

int main(int argc, char **argv)
{
    unsigned int value[2] = { 0, 0x3ff40000 };
    unsigned int result[2] = { 0, 0 };
    unsigned int flags;
    int sleep_result = 35;
    unsigned short control;
    volatile unsigned int spin;
    int child, round, i;
    int is_child = strcmp(argv[0], "A:/Programs/FPUCHILD.EXE") == 0;
    (void)argc;
    __asm__ volatile ("std; int $0x80; pushfl; popl %0; cld"
                      : "=r"(flags), "+a"(sleep_result)
                      : "b"(1) : "memory", "cc");
    if ((flags & 0x400) == 0 || sleep_result != 0)
    {
        printf("fpu test: direction flag not restored\n");
        return 1;
    }
    __asm__ volatile ("fnstcw %0" : "=m"(control));
    if (control != 0x037f)
    {
        printf("fpu test: incorrect initial control word\n");
        return 1;
    }
    if (is_child)
        value[1] = 0x400c0000;

    for (round = 0; round < (is_child ? 1 : 2); ++round)
    {
        if (!is_child)
        {
            child = syscall1(30, (int)"A:/Programs/FPUCHILD.EXE");
            if (child < 0)
            {
                printf("fpu test: could not start child\n");
                return 1;
            }
        }

        __asm__ volatile ("fldl %0" : : "m"(value));
        for (i = 0; i < 100; ++i)
        {
            for (spin = 0; spin < 10000; ++spin) {}
            syscall1(35, 1);
        }
        __asm__ volatile ("fstpl %0" : "=m"(result));
        if (result[0] != value[0] || result[1] != value[1])
        {
            printf("fpu test: state corrupted\n");
            return 1;
        }
        if (!is_child && syscall1(31, child) != 0)
        {
            printf("fpu test: child failed\n");
            return 1;
        }
    }
    if (!is_child)
        printf("fpu context test passed\n");
    return 0;
}
