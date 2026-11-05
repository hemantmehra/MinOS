#include "kernel.h"
#include "filesystem.h"
#include "kernel_log.h"
#include "process.h"

static void shell_memory_report(void)
{
    const unsigned int test_virtual_address = 0xC1000000;
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

    result = filesystem_create_directory(name);
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
    case CREATE_DIRECTORY_FULL:
        terminal_write("mkdir: directory is full.\n");
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

void shell_run_service(
    unsigned int service,
    const char *arguments,
    unsigned int owner_process_id,
    unsigned int window_handle)
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
        video_start_graphics(owner_process_id, window_handle);
        break;
    case PROCESS_SERVICE_END_GRAPHICS:
        video_end_graphics(owner_process_id, window_handle);
        break;
    }
}

void __cdecl kernel_main(void)
{
    unsigned int process_id;
    enum ProcessStartResult result;

    kernel_log(KERNEL_LOG_INFO, "kernel", "booting");
    memory_initialize();
    video_initialize();
    processes_initialize();
    interrupts_initialize();
    kernel_log(KERNEL_LOG_INFO, "kernel", "initialization complete");

    for (;;)
    {
        while (process_has_foreground_process())
        {
            unsigned int request = video_take_launch_request();
            unsigned int close_id = video_take_close_request();

            if (close_id != 0)
            {
                process_kill_by_id(close_id);
            }
            if (request == 1)
            {
                process_start("A:/Programs/BALL.EXE", "", &process_id, 0);
            }
        }

        result = process_start("A:/Programs/SHELL.EXE", "", &process_id, 0);
        if (result != PROCESS_START_OK)
        {
            kernel_log_value(
                KERNEL_LOG_ERROR,
                "kernel",
                "shell start failed with result",
                (unsigned int)result);
            terminal_write("kernel: could not start SHELL.EXE.\n");
            for (;;)
            {
            }
        }
        kernel_log(KERNEL_LOG_INFO, "kernel", "shell started");
    }
}
