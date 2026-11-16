#include <stdio.h>
#include <string.h>

static int syscall1(int number, int argument)
{
    int result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number), "b"(argument)
                      : "memory", "cc");
    return result;
}

int main(int argc, char **argv)
{
    unsigned int eax, ebx, ecx, edx;
    int first, second, fault;
    (void)argc;

    if (strstr(argv[0], "SMPFAIL") != 0)
    {
        __asm__ volatile ("ud2");
        return 1;
    }
    if (strstr(argv[0], "SMPC") != 0 || strstr(argv[0], "SMPLONG") != 0 ||
        strstr(argv[0], "smplong") != 0)
    {
        unsigned int iterations = 2000000000u;
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                          : "a"(1));
        printf("SMP worker started on APIC %u\n", ebx >> 24);
        if (strstr(argv[0], "SMPC") == 0)
            for (;;) __asm__ volatile ("pause" : : : "memory");
        __asm__ volatile (
            "movl $0x534d5057, %%edi\n"
            "1: decl %%ecx\n"
            "jnz 1b\n"
            : "+c"(iterations) : : "edi", "cc");
        return 0;
    }

    if (syscall1(30, (int)"A:/Programs/NOFILE.EXE") != -1)
    {
        printf("SMP test: missing executable unexpectedly started\n");
        return 1;
    }
    first = syscall1(30, (int)"A:/Programs/SMPC1.EXE");
    second = syscall1(30, (int)"A:/Programs/SMPC2.EXE");
    if (first < 0 || second < 0 || syscall1(31, first) != 0 ||
        syscall1(31, second) != 0)
    {
        printf("SMP test failed\n");
        return 1;
    }
    fault = syscall1(30, (int)"A:/Programs/SMPFAIL.EXE");
    if (fault < 0 || syscall1(31, fault) != 255)
    {
        printf("SMP fault test failed\n");
        return 1;
    }
    printf("SMP workers completed\n");
    return 0;
}
