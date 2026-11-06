#include <stdio.h>
#include <stdlib.h>

static int arguments[128];
static char argument_text[256];
static char *argument_vector[66];
static char environment_text[16][128];
static char *environment_vector[17];
char **environ = environment_vector;

static int system_call_1(int number, int argument)
{
    int result;
    __asm__ volatile ("int $0x80"
        : "=a"(result) : "a"(number), "b"(argument) : "memory", "cc");
    return result;
}

extern int main(int argc, char **argv, char **envp);

void minos_runtime_start(void)
{
    int scratch[128];
    int status;
    int index;
    int argc = 1;
    int position = 0;
    int entry, env_count = 0;

    for (entry = 0; entry < 16; ++entry)
    {
        __asm__ volatile ("int $0x80" : "=a"(status)
            : "a"(40), "b"(entry), "c"(scratch), "d"(128) : "memory", "cc");
        if (status < 0)
        {
            fputs("ELF runtime: could not read environment.\n", stderr);
            system_call_1(0, 255);
            for (;;) {}
        }
        if (status != 0)
        {
            for (index = 0; index <= status; ++index)
                environment_text[env_count][index] = (char)scratch[index];
            environment_vector[env_count] = environment_text[env_count];
            ++env_count;
        }
    }
    environment_vector[env_count] = 0;

    __asm__ volatile ("int $0x80"
        : "=a"(status) : "a"(39), "c"(scratch), "d"(128) : "memory", "cc");
    if (status < 0)
    {
        fputs("ELF runtime: could not read program name.\n", stderr);
        system_call_1(0, 255);
        for (;;) {}
    }
    argument_vector[0] = argument_text;
    for (index = 0; index <= status; ++index)
        argument_text[position++] = (char)scratch[index];
    __asm__ volatile ("int $0x80"
        : "=a"(status) : "a"(15), "c"(scratch), "d"(128) : "memory", "cc");
    if (status < 0)
    {
        fputs("ELF runtime: could not read arguments.\n", stderr);
        system_call_1(0, 255);
        for (;;) {}
    }
    for (index = 0; index <= status; ++index)
    {
        arguments[index] = scratch[index];
    }
    index = 0;
    while (arguments[index] != 0 && argc < 65)
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
    exit(main(argc, argument_vector, environ));
}
