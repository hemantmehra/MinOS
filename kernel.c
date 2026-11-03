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

    if (*command == '\0')
    {
        return;
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
    else if (command_equals(command, "mem"))
    {
        const unsigned int test_virtual_address = 0x7F000000;
        const unsigned int test_value = 0x4D454D31;
        unsigned int physical_address = memory_allocate_page();
        unsigned int translated_address;
        unsigned int unmapped_address;
        volatile unsigned int *test_mapping;

        terminal_write("Free 4 KiB pages: ");
        terminal_write_unsigned(memory_available_pages());
        terminal_write("\n");

        if (physical_address == 0)
        {
            terminal_write("mem: physical page allocation failed.\n");
            return;
        }
        if (!memory_map_page(
            test_virtual_address, physical_address, 0x02))
        {
            memory_free_page(physical_address);
            terminal_write("mem: virtual page mapping failed.\n");
            return;
        }

        test_mapping = (volatile unsigned int *)test_virtual_address;
        *test_mapping = test_value;
        if (*test_mapping != test_value ||
            !memory_translate(test_virtual_address, &translated_address) ||
            translated_address != physical_address)
        {
            memory_unmap_page(test_virtual_address, &unmapped_address);
            memory_free_page(physical_address);
            terminal_write("mem: virtual memory test failed.\n");
            return;
        }

        if (!memory_unmap_page(test_virtual_address, &unmapped_address) ||
            unmapped_address != physical_address ||
            !memory_free_page(physical_address))
        {
            terminal_write("mem: page cleanup failed.\n");
            return;
        }
        terminal_write("Virtual memory test passed.\n");
    }
    else if (command_equals(command, "help"))
    {
        terminal_write(
            "Commands: echo <text>, hostname, time, mem, ps, ls, mkdir, clear, draw, basic, help\n");
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
    else if (command_equals(command, "ps"))
    {
        process_list();
    }
    else if (command_token_equals(command, argument, "mkdir"))
    {
        terminal_write("mkdir: ext2 filesystem is read-only.\n");
    }
    else
    {
        unsigned int process_id;
        enum ProcessStartResult result;
        char *name_end = argument;

        while (is_space(*argument))
        {
            ++argument;
        }
        if (*argument != '\0')
        {
            terminal_write("Unknown command or executable arguments: ");
            terminal_write(command);
            terminal_write("\n");
            return;
        }

        *name_end = '\0';
        result = process_start(command, &process_id);
        switch (result)
        {
        case PROCESS_START_OK:
            break;
        case PROCESS_START_INVALID_NAME:
            terminal_write("run: use an 8.3 executable name, such as HELLO.EXE.\n");
            break;
        case PROCESS_START_NOT_FOUND:
            terminal_write("run: executable not found: ");
            terminal_write(command);
            terminal_write("\n");
            break;
        case PROCESS_START_TOO_LARGE:
            terminal_write("run: executable exceeds the 64 KiB limit.\n");
            break;
        case PROCESS_START_DISK_ERROR:
            terminal_write("run: failed to read executable from disk.\n");
            break;
        case PROCESS_START_INVALID_EXECUTABLE:
            terminal_write("run: invalid MEXE executable.\n");
            break;
        case PROCESS_START_NO_MEMORY:
            terminal_write("run: could not allocate executable memory.\n");
            break;
        default:
            if (result == PROCESS_START_LIMIT_REACHED)
            {
                terminal_write("run: process limit reached (8 processes).\n");
            }
            else
            {
                terminal_write("run: failed to release process mappings.\n");
            }
            break;
        }
    }
}

void __cdecl kernel_main(void)
{
    char command[COMMAND_BUFFER_SIZE];
    unsigned char shift_pressed = 0;
    unsigned char caps_lock = 0;
    unsigned char extended = 0;

    memory_initialize();
    video_initialize();
    processes_initialize();
    interrupts_initialize();
    terminal_write("Hello, world!\n");
    terminal_write("Type help for available commands.\n");

    for (;;)
    {
        unsigned int length = 0;
        char character;

        terminal_write_prompt("minios> ");

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
