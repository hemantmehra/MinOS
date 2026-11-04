#include <intrin.h>
#include "kernel_log.h"

#define QEMU_DEBUG_PORT 0xE9

static void kernel_log_write(const char *text)
{
    while (*text != '\0')
    {
        __outbyte(QEMU_DEBUG_PORT, (unsigned char)*text);
        ++text;
    }
}

static void kernel_log_write_unsigned(unsigned int value)
{
    char digits[10];
    unsigned int length = 0;

    do
    {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    while (length != 0)
    {
        __outbyte(QEMU_DEBUG_PORT, (unsigned char)digits[--length]);
    }
}

static const char *kernel_log_level_name(enum KernelLogLevel level)
{
    switch (level)
    {
    case KERNEL_LOG_INFO:
        return "INFO";
    case KERNEL_LOG_WARNING:
        return "WARN";
    case KERNEL_LOG_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

void kernel_log(
    enum KernelLogLevel level,
    const char *subsystem,
    const char *message)
{
    unsigned int flags;

    __asm
    {
        pushfd
        pop eax
        mov flags, eax
        cli
    }

    kernel_log_write("[");
    kernel_log_write(kernel_log_level_name(level));
    kernel_log_write("] ");
    kernel_log_write(subsystem);
    kernel_log_write(": ");
    kernel_log_write(message);
    __outbyte(QEMU_DEBUG_PORT, '\n');

    __asm
    {
        push flags
        popfd
    }
}

void kernel_log_value(
    enum KernelLogLevel level,
    const char *subsystem,
    const char *message,
    unsigned int value)
{
    unsigned int flags;

    __asm
    {
        pushfd
        pop eax
        mov flags, eax
        cli
    }

    kernel_log_write("[");
    kernel_log_write(kernel_log_level_name(level));
    kernel_log_write("] ");
    kernel_log_write(subsystem);
    kernel_log_write(": ");
    kernel_log_write(message);
    kernel_log_write("=");
    kernel_log_write_unsigned(value);
    __outbyte(QEMU_DEBUG_PORT, '\n');

    __asm
    {
        push flags
        popfd
    }
}
