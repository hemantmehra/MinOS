#include <stdarg.h>
#include <stdio.h>

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

int putchar(int character)
{
    system_call_1(11, character);
    return (unsigned char)character;
}

static int write_string(const char *text)
{
    int count = 0;
    while (*text)
    {
        putchar(*text++);
        ++count;
    }
    return count;
}

static int write_number(unsigned int value, unsigned int base)
{
    char digits[32];
    int count = 0;
    int index;

    do
    {
        unsigned int digit = value % base;
        digits[count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value != 0);
    for (index = count; index != 0; --index)
    {
        putchar(digits[index - 1]);
    }
    return count;
}

int printf(const char *format, ...)
{
    va_list arguments_list;
    int count = 0;

    va_start(arguments_list, format);
    while (*format)
    {
        char specifier = *format++;
        if (specifier != '%')
        {
            putchar(specifier);
            ++count;
            continue;
        }
        specifier = *format++;
        if (specifier == '\0')
        {
            va_end(arguments_list);
            return -1;
        }
        if (specifier == 's')
        {
            const char *text = va_arg(arguments_list, const char *);
            count += write_string(text != 0 ? text : "(null)");
        }
        else if (specifier == 'd' || specifier == 'i')
        {
            int value = va_arg(arguments_list, int);
            unsigned int magnitude = (unsigned int)value;
            if (value < 0)
            {
                putchar('-');
                ++count;
                magnitude = 0U - magnitude;
            }
            count += write_number(magnitude, 10);
        }
        else if (specifier == 'u' || specifier == 'x')
        {
            count += write_number(
                va_arg(arguments_list, unsigned int), specifier == 'u' ? 10 : 16);
        }
        else if (specifier == 'c' || specifier == '%')
        {
            putchar(specifier == 'c' ? va_arg(arguments_list, int) : '%');
            ++count;
        }
        else
        {
            va_end(arguments_list);
            return -1;
        }
    }
    va_end(arguments_list);
    return count;
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
        write_string("ELF runtime: could not read arguments.\n");
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
    (void)main(argc, argument_vector);
    system_call_1(0, 0);
    for (;;) {}
}
