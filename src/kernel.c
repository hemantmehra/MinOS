#include "kernel.h"

static int parse_pipeline_stage(char **command, char **arguments)
{
    char *name_end;

    while (is_space(**command))
    {
        ++*command;
    }
    if (**command == '\0')
    {
        return 0;
    }

    name_end = *command;
    while (*name_end != '\0' && !is_space(*name_end))
    {
        ++name_end;
    }
    *arguments = name_end;
    while (is_space(**arguments))
    {
        ++*arguments;
    }
    *name_end = '\0';
    return 1;
}

static void run_pipeline(char *first_command, char *second_command)
{
    char *first_arguments;
    char *second_arguments;
    enum ProcessStartResult result;

    if (!parse_pipeline_stage(&first_command, &first_arguments) ||
        !parse_pipeline_stage(&second_command, &second_arguments))
    {
        terminal_write("Usage: <command> | <command>\n");
        return;
    }

    result = process_start_pipeline(
        first_command,
        first_arguments,
        second_command,
        second_arguments);
    if (result != PROCESS_START_OK)
    {
        if (result == PROCESS_START_NOT_FOUND)
        {
            terminal_write("pipe: executable not found.\n");
        }
        else if (result == PROCESS_START_LIMIT_REACHED)
        {
            terminal_write("pipe: process limit reached.\n");
        }
        else
        {
            terminal_write("pipe: could not start both processes.\n");
        }
    }
}

static void run_command(char *command)
{
    char *argument;
    char *name_end;
    char *pipe_separator = 0;
    char *scan;
    unsigned int process_id;
    enum ProcessStartResult result;

    while (is_space(*command))
    {
        ++command;
    }

    if (*command == '\0')
    {
        return;
    }

    for (scan = command; *scan != '\0'; ++scan)
    {
        if (*scan == '|')
        {
            if (pipe_separator != 0)
            {
                terminal_write("Usage: <command> | <command>\n");
                return;
            }
            pipe_separator = scan;
        }
    }
    if (pipe_separator != 0)
    {
        *pipe_separator = '\0';
        run_pipeline(command, pipe_separator + 1);
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
        return;
    }

    name_end = argument;
    while (is_space(*argument))
    {
        ++argument;
    }
    if (*argument != '\0' &&
        !command_token_equals(command, name_end, "mkdir") &&
        !command_token_equals(command, name_end, "ls") &&
        !command_token_equals(command, name_end, "cd") &&
        !command_token_equals(command, name_end, "cat"))
    {
        terminal_write("Unknown command or executable arguments: ");
        terminal_write(command);
        terminal_write("\n");
        return;
    }

    *name_end = '\0';
    result = process_start(command, argument, &process_id);
    switch (result)
    {
    case PROCESS_START_OK:
        break;
    case PROCESS_START_INVALID_NAME:
        terminal_write("run: use an 8.3 executable name, such as HELLO.EXE.\n");
        break;
    case PROCESS_START_ARGUMENT_TOO_LONG:
        terminal_write("run: command arguments are too long.\n");
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

static void shell_memory_report(void)
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
    if (!memory_map_page(test_virtual_address, physical_address, 0x02))
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

static void shell_make_directory(const char *arguments)
{
    char name[COMMAND_BUFFER_SIZE];
    unsigned int length = 0;
    enum CreateDirectoryResult result;

    while (is_space(*arguments))
    {
        ++arguments;
    }
    if (*arguments == '\0')
    {
        terminal_write("Usage: mkdir <name> (1-8 letters, digits, _ or -)\n");
        return;
    }

    while (*arguments != '\0' && !is_space(*arguments))
    {
        if (length == COMMAND_BUFFER_SIZE - 1)
        {
            terminal_write("mkdir: name must be 1-8 letters, digits, _ or -.\n");
            return;
        }
        name[length++] = *arguments++;
    }
    name[length] = '\0';

    while (is_space(*arguments))
    {
        ++arguments;
    }
    if (*arguments != '\0')
    {
        terminal_write("Usage: mkdir <name> (1-8 letters, digits, _ or -)\n");
        return;
    }

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
        terminal_write("mkdir: root directory is full.\n");
        break;
    case CREATE_DIRECTORY_DISK_FULL:
        terminal_write("mkdir: ext2 filesystem has no free space.\n");
        break;
    case CREATE_DIRECTORY_IO_ERROR:
        terminal_write("mkdir: disk read or write failed.\n");
        break;
    default:
        terminal_write("mkdir: could not mount or update the ext2 filesystem.\n");
        break;
    }
}

void shell_run_service(unsigned int service, const char *arguments)
{
    switch (service)
    {
    case PROCESS_SERVICE_CLOCK:
        clock_write_time();
        break;
    case PROCESS_SERVICE_MEMORY_REPORT:
        shell_memory_report();
        break;
    case PROCESS_SERVICE_LIST_PROCESSES:
        process_list();
        break;
    case PROCESS_SERVICE_MAKE_DIRECTORY:
        shell_make_directory(arguments);
        break;
    case PROCESS_SERVICE_TERMINAL_CLEAR:
        terminal_clear();
        break;
    case PROCESS_SERVICE_START_GRAPHICS:
        video_start_graphics();
        break;
    case PROCESS_SERVICE_END_GRAPHICS:
        video_end_graphics();
        break;
    }
}

void process_service_write_character(char character)
{
    terminal_write_character(character);
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
    terminal_write("Enter a command or executable name.\n");

    for (;;)
    {
        unsigned int length = 0;
        char character;

        while (process_has_active_process())
        {
        }

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
        run_command(command);
    }
}
