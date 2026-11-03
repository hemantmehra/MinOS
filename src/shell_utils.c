#include "kernel.h"

int command_equals(const char *command, const char *expected)
{
    while (*command != '\0' && *command == *expected)
    {
        ++command;
        ++expected;
    }

    return *command == '\0' && *expected == '\0';
}

int command_token_equals(
    const char *command,
    const char *end,
    const char *expected)
{
    while (command != end && *command == *expected)
    {
        ++command;
        ++expected;
    }

    return command == end && *expected == '\0';
}

int is_space(char character)
{
    return character == ' ' || character == '\t';
}
