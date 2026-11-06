#include <stdio.h>
#include <stdlib.h>

static int arguments[128];
static char argument_text[256];
static char *argument_vector[17];

static int system_call_1(int number, int argument)
{
    int result;
    __asm__ volatile ("int $0x80"
        : "=a"(result) : "a"(number), "b"(argument) : "memory", "cc");
    return result;
}

extern int main(int argc, char **argv);

void minos_runtime_start(void)
{
    int scratch[128];
    int status;
    int index;
    int argc = 1;
    int position = 1;

    __asm__ volatile ("int $0x80"
        : "=a"(status) : "a"(15), "c"(scratch), "d"(128) : "memory", "cc");
    if (status < 0)
    {
        fputs("ELF runtime: could not read arguments.\n", stderr);
        system_call_1(0, 0);
        for (;;) {}
    }
    for (index = 0; index <= status; ++index)
    {
        arguments[index] = scratch[index];
    }
    argument_vector[0] = argument_text;
    argument_text[0] = '\0';
    index = 0;
    while (arguments[index] != 0 && argc < 16)
    {
        while (arguments[index] == ' ')
        {
            ++index;
        }
        if (arguments[index] == 0)
        {
            break;
        }
        argument_vector[argc++] = &argument_text[position];
        while (arguments[index] != 0 && arguments[index] != ' ')
        {
            argument_text[position++] = (char)arguments[index++];
        }
        argument_text[position++] = '\0';
    }
    argument_vector[argc] = 0;
    exit(main(argc, argument_vector));
}
