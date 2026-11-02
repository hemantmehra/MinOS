#include "kernel.h"

static void run_command(
    char *command,
    unsigned char *shift_pressed,
    unsigned char *caps_lock,
    unsigned char *extended)
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
    else if (command_equals(command, "time"))
    {
        clock_write_time();
    }
    else if (command_equals(command, "help"))
    {
        terminal_write(
            "Commands: echo <text>, hostname, time, ls, mkdir, clear, draw, basic, help\n");
    }
    else if (command_equals(command, "clear"))
    {
        terminal_clear();
    }
    else if (command_equals(command, "draw"))
    {
        char character;

        video_draw_circle();
        do
        {
            character = keyboard_read_character(
                shift_pressed, caps_lock, extended);
        } while (character != '\x1B');

        terminal_clear();
    }
    else if (command_equals(command, "basic"))
    {
        terminal_write(
            "BASIC: enter numbered lines; RUN, LIST, NEW, or EXIT.\n");
        basic_repl(shift_pressed, caps_lock, extended);
    }
    else if (command_equals(command, "ls"))
    {
        filesystem_list_root();
    }
    else if (command_token_equals(command, argument, "mkdir"))
    {
        char *name;
        char *name_end;
        enum CreateDirectoryResult result;

        while (is_space(*argument))
        {
            ++argument;
        }

        if (*argument == '\0')
        {
            terminal_write("Usage: mkdir <name> (1-8 letters, digits, _ or -)\n");
            return;
        }

        name = argument;
        while (*argument != '\0' && !is_space(*argument))
        {
            ++argument;
        }
        name_end = argument;

        while (is_space(*argument))
        {
            ++argument;
        }

        if (*argument != '\0')
        {
            terminal_write("Usage: mkdir <name> (1-8 letters, digits, _ or -)\n");
            return;
        }

        *name_end = '\0';
        result = filesystem_create_root_directory(name);
        switch (result)
        {
        case CREATE_DIRECTORY_OK:
            terminal_write("Created directory ");
            terminal_write(name);
            terminal_write("\n");
            break;
        case CREATE_DIRECTORY_INVALID_NAME:
            terminal_write("mkdir: name must be 1-8 letters, digits, _ or -.\n");
            break;
        case CREATE_DIRECTORY_EXISTS:
            terminal_write("mkdir: directory already exists.\n");
            break;
        case CREATE_DIRECTORY_ROOT_FULL:
            terminal_write("mkdir: current directory is full.\n");
            break;
        case CREATE_DIRECTORY_DISK_FULL:
            terminal_write("mkdir: filesystem is full.\n");
            break;
        case CREATE_DIRECTORY_IO_ERROR:
            terminal_write("mkdir: disk read or write failed.\n");
            break;
        default:
            terminal_write("mkdir: could not mount the FAT16 filesystem.\n");
            break;
        }
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

    video_initialize();
    interrupts_initialize();
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
        run_command(
            command, &shift_pressed, &caps_lock, &extended);
    }
}
