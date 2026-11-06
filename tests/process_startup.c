#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile unsigned char image_data[3 * 1024 * 1024];
static volatile unsigned char initialized_data[256 * 1024] = { 1 };

static int syscall1(int number, int argument)
{
    int result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number), "b"(argument)
                      : "memory", "cc");
    return result;
}

static int spawn(const char *name, const char *arg)
{
    int path[128], args[128], i, result;
    for (i = 0; name[i]; ++i) path[i] = name[i];
    path[i] = 0;
    for (i = 0; arg[i]; ++i) args[i] = arg[i];
    args[i] = 0;
    __asm__ volatile ("int $0x80" : "=a"(result)
        : "a"(33), "b"(path), "c"(args), "d"(0) : "memory", "cc");
    return result;
}

static int set_variable(const char *value)
{
    int data[128], i;
    for (i = 0; value[i]; ++i) data[i] = value[i];
    data[i] = 0;
    return syscall1(41, (int)data);
}

static int check_kernel_variable(int index, const char *value)
{
    int data[128], result, i;
    __asm__ volatile ("int $0x80" : "=a"(result)
        : "a"(40), "b"(index), "c"(data), "d"(128) : "memory", "cc");
    if (result < 0) return 0;
    for (i = 0; value[i] && data[i] == value[i]; ++i) {}
    return data[i] == 0 && value[i] == 0;
}

int main(int argc, char **argv, char **envp)
{
    volatile unsigned char stack_data[900 * 1024];
    int child, status, failures = 0;
    stack_data[0] = 7;
    stack_data[sizeof(stack_data) - 1] = 9;
    image_data[sizeof(image_data) - 1] = 3;
    if (stack_data[0] != 7 || stack_data[sizeof(stack_data) - 1] != 9 ||
        image_data[sizeof(image_data) - 1] != 3 ||
        initialized_data[0] != 1 || initialized_data[sizeof(initialized_data) - 1] != 0 ||
        argv[0][0] == 0 || argv[argc] != 0 || !envp || !envp[0] ||
        !getenv("TEST") || strcmp(getenv("TEST"), "hello") != 0 ||
        !getenv("PATH") || getenv("NOT_SET"))
        ++failures;
    if (argc > 1)
    {
        if (strcmp(argv[0], "A:/Programs/STARTUP.EXE") != 0) ++failures;
        if (set_variable("TEST=child") != 0) ++failures;
        if (strcmp(argv[1], "slow") == 0) syscall1(35, 100);
        return failures ? 99 : 42;
    }
    child = spawn("A:/Programs/STARTUP.EXE", "fast");
    if (child < 0) ++failures;
    else
    {
        syscall1(35, 500);
        status = syscall1(31, child);
        if (status != 42 || syscall1(31, child) != -1) ++failures;
    }
    child = spawn("A:/Programs/STARTUP.EXE", "slow");
    if (child < 0 || syscall1(31, child) != 42) ++failures;
    child = spawn("A:/Programs/MINTST.EXE", "arg");
    if (child < 0 || syscall1(31, child) != 77) ++failures;
    if (!check_kernel_variable(1, "TEST=hello") ||
        set_variable("9INVALID=value") != -1) ++failures;
    printf("STARTUP TEST: %d failures\n", failures);
    return failures != 0;
}
