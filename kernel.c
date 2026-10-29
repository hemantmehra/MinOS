#include <intrin.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define COMMAND_BUFFER_SIZE 128

static unsigned short vga_cursor_position(void)
{
    unsigned short position;

    __outbyte(0x3D4, 14);
    position = (unsigned short)__inbyte(0x3D5) << 8;
    __outbyte(0x3D4, 15);
    position |= __inbyte(0x3D5);
    return position;
}

static void vga_set_cursor_position(unsigned short position)
{
    __outbyte(0x3D4, 14);
    __outbyte(0x3D5, (unsigned char)(position >> 8));
    __outbyte(0x3D4, 15);
    __outbyte(0x3D5, (unsigned char)position);
}

static void vga_scroll(void)
{
    volatile unsigned short *video_memory =
        (volatile unsigned short *)0xB8000;
    unsigned int index;

    for (index = 0; index < VGA_WIDTH * (VGA_HEIGHT - 1); ++index)
    {
        video_memory[index] = video_memory[index + VGA_WIDTH];
    }

    for (; index < VGA_WIDTH * VGA_HEIGHT; ++index)
    {
        video_memory[index] = 0x0F20;
    }
}

static void terminal_write_character(char character)
{
    volatile unsigned short *video_memory =
        (volatile unsigned short *)0xB8000;
    unsigned short position = vga_cursor_position();

    if (character == '\r')
    {
        position -= position % VGA_WIDTH;
    }
    else if (character == '\n')
    {
        position += VGA_WIDTH - position % VGA_WIDTH;
    }
    else if (character == '\b')
    {
        if (position != 0)
        {
            --position;
            video_memory[position] = 0x0F20;
        }
    }
    else
    {
        video_memory[position] = (unsigned short)(0x0F00 | (unsigned char)character);
        ++position;
        __outbyte(0xE9, (unsigned char)character);
    }

    if (position >= VGA_WIDTH * VGA_HEIGHT)
    {
        vga_scroll();
        position -= VGA_WIDTH;
    }

    vga_set_cursor_position(position);

    if (character == '\n')
    {
        __outbyte(0xE9, '\n');
    }
    else if (character == '\b')
    {
        __outbyte(0xE9, '\b');
    }
}

static void terminal_write(const char *text)
{
    while (*text != '\0')
    {
        terminal_write_character(*text);
        ++text;
    }
}

static void terminal_clear(void)
{
    volatile unsigned short *video_memory =
        (volatile unsigned short *)0xB8000;
    unsigned int index;

    for (index = 0; index < VGA_WIDTH * VGA_HEIGHT; ++index)
    {
        video_memory[index] = 0x0F20;
    }

    vga_set_cursor_position(0);
}

static char translate_scancode(
    unsigned char scancode,
    unsigned char shift_pressed,
    unsigned char caps_lock)
{
    char character = '\0';

    if (scancode >= 0x02 && scancode <= 0x0B)
    {
        const char *digits = shift_pressed ? "!@#$%^&*()" : "1234567890";
        character = digits[scancode - 0x02];
    }
    else if (scancode >= 0x10 && scancode <= 0x19)
    {
        character = "qwertyuiop"[scancode - 0x10];
    }
    else if (scancode >= 0x1E && scancode <= 0x26)
    {
        character = "asdfghjkl"[scancode - 0x1E];
    }
    else if (scancode >= 0x2C && scancode <= 0x32)
    {
        character = "zxcvbnm"[scancode - 0x2C];
    }
    else if (scancode == 0x39)
    {
        character = ' ';
    }
    else
    {
        switch (scancode)
        {
        case 0x0C: character = shift_pressed ? '_' : '-'; break;
        case 0x0D: character = shift_pressed ? '+' : '='; break;
        case 0x1A: character = shift_pressed ? '{' : '['; break;
        case 0x1B: character = shift_pressed ? '}' : ']'; break;
        case 0x27: character = shift_pressed ? ':' : ';'; break;
        case 0x28: character = shift_pressed ? '"' : '\''; break;
        case 0x29: character = shift_pressed ? '~' : '`'; break;
        case 0x2B: character = shift_pressed ? '|' : '\\'; break;
        case 0x33: character = shift_pressed ? '<' : ','; break;
        case 0x34: character = shift_pressed ? '>' : '.'; break;
        case 0x35: character = shift_pressed ? '?' : '/'; break;
        default: break;
        }
    }

    if (character >= 'a' && character <= 'z' &&
        (shift_pressed != caps_lock))
    {
        character = (char)(character - 'a' + 'A');
    }

    return character;
}

static char keyboard_read_character(
    unsigned char *shift_pressed,
    unsigned char *caps_lock,
    unsigned char *extended)
{
    for (;;)
    {
        unsigned char scancode;
        unsigned char status = __inbyte(0x64);

        if ((status & 0x01) == 0)
        {
            continue;
        }

        scancode = __inbyte(0x60);

        if (*extended)
        {
            *extended = 0;
            continue;
        }

        if (scancode == 0xE0)
        {
            *extended = 1;
            continue;
        }

        if (scancode == 0x2A || scancode == 0x36)
        {
            *shift_pressed = 1;
            continue;
        }

        if (scancode == 0xAA || scancode == 0xB6)
        {
            *shift_pressed = 0;
            continue;
        }

        if ((scancode & 0x80) != 0)
        {
            continue;
        }

        if (scancode == 0x3A)
        {
            *caps_lock = (unsigned char)!*caps_lock;
            continue;
        }

        if (scancode == 0x1C)
        {
            return '\n';
        }

        if (scancode == 0x0E)
        {
            return '\b';
        }

        return translate_scancode(scancode, *shift_pressed, *caps_lock);
    }
}

static int command_equals(const char *command, const char *expected)
{
    while (*command != '\0' && *command == *expected)
    {
        ++command;
        ++expected;
    }

    return *command == '\0' && *expected == '\0';
}

static int is_space(char character)
{
    return character == ' ' || character == '\t';
}

static void run_command(char *command)
{
    char *argument;

    while (is_space(*command))
    {
        ++command;
    }

    argument = command;
    while (*argument != '\0' && !is_space(*argument))
    {
        ++argument;
    }

    if (argument - command == 4 &&
        command[0] == 'e' && command[1] == 'c' &&
        command[2] == 'h' && command[3] == 'o')
    {
        while (is_space(*argument))
        {
            ++argument;
        }
        terminal_write(argument);
        terminal_write("\n");
    }
    else if (command_equals(command, "hostname"))
    {
        terminal_write("minios\n");
    }
    else if (command_equals(command, "help"))
    {
        terminal_write("Commands: echo <text>, hostname, clear, help\n");
    }
    else if (command_equals(command, "clear"))
    {
        terminal_clear();
    }
    else
    {
        terminal_write("Unknown command: ");
        terminal_write(command);
        terminal_write("\n");
    }
}

void __cdecl kernel_main(void)
{
    char command[COMMAND_BUFFER_SIZE];
    unsigned char shift_pressed = 0;
    unsigned char caps_lock = 0;
    unsigned char extended = 0;

    terminal_clear();
    terminal_write("Hello, world!\n");
    terminal_write("Type help for available commands.\n");

    for (;;)
    {
        unsigned int length = 0;
        char character;

        terminal_write("minios> ");

        for (;;)
        {
            character = keyboard_read_character(
                &shift_pressed, &caps_lock, &extended);

            if (character == '\n')
            {
                terminal_write("\n");
                break;
            }

            if (character == '\b')
            {
                if (length != 0)
                {
                    --length;
                    terminal_write_character('\b');
                }
                continue;
            }

            if (character >= ' ' && character <= '~' &&
                length < COMMAND_BUFFER_SIZE - 1)
            {
                command[length++] = character;
                terminal_write_character(character);
            }
        }

        command[length] = '\0';
        run_command(command);
    }
}
