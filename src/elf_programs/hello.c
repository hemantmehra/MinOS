#include <stdio.h>

static volatile int uninitialized;
static volatile int initialized = 42;

int main(int argc, char **argv)
{
    printf("Hello from a Clang-built ELF program!\n");
    printf("data=%d bss=%d argc=%d\n", initialized, uninitialized, argc);
    if (argc > 1)
    {
        printf("argument: %s\n", argv[1]);
    }
    return 0;
}
