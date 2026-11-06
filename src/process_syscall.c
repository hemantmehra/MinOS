#include <intrin.h>
#include "filesystem.h"
#include "kernel_log.h"
#include "process_internal.h"

unsigned int __cdecl process_system_call(unsigned int *saved_stack)
{
    Process *process;
    unsigned int task;
    unsigned int syscall_number;

    processes[current_task].saved_stack = saved_stack;
    if (current_task == 0 ||
        processes[current_task].state != TASK_ACTIVE)
    {
        return (unsigned int)saved_stack;
    }

    task = current_task;
    process = &processes[task];
    syscall_number = saved_stack[11];
    if (syscall_number == PROCESS_SYSCALL_PRINT)
    {
        if (!process_print_string(process, saved_stack[8]))
        {
            terminal_write("process: invalid print request; terminating.\n");
            process_terminate_current();
        }
    }
    else if (syscall_number == PROCESS_SYSCALL_KEYBOARD_READ)
    {
        unsigned char scancode;

        saved_stack[11] = 0;
        while (process_keyboard_try_read_scancode(process, &scancode))
        {
            saved_stack[11] = scancode;
            break;
        }
    }
    else if (syscall_number == PROCESS_SERVICE_OPENDIR)
    {
        char path[128];

        if (process_copy_user_path(process, saved_stack[8], path))
        {
            saved_stack[11] = (unsigned int)filesystem_open_directory(
                process->process_id, path);
        }
        else
        {
            saved_stack[11] = (unsigned int)-1;
        }
    }
    else if (syscall_number == PROCESS_SERVICE_CLOSEDIR)
    {
        saved_stack[11] = (unsigned int)filesystem_close_directory(
            process->process_id, saved_stack[8]);
    }
    else if (syscall_number == PROCESS_SERVICE_READ_DIR)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 256 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (!process_stack_buffer_is_valid(
                process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)filesystem_read_directory(
                    process->process_id,
                    saved_stack[8],
                    (unsigned int *)saved_stack[10],
                    capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_READ_ARGUMENTS ||
        syscall_number == PROCESS_SERVICE_PROGRAM_NAME ||
        syscall_number == PROCESS_SERVICE_GET_CURRENT_DIRECTORY)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 128 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (!process_stack_buffer_is_valid(
                process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else if (syscall_number == PROCESS_SERVICE_READ_ARGUMENTS ||
                syscall_number == PROCESS_SERVICE_PROGRAM_NAME)
            {
                saved_stack[11] = (unsigned int)
                    process_copy_string_to_array(
                        syscall_number == PROCESS_SERVICE_READ_ARGUMENTS
                            ? process->arguments : process->program_path,
                        (unsigned int *)saved_stack[10],
                        capacity);
            }
            else
            {
                char path[128];
                int length = filesystem_get_current_directory(
                    path, sizeof(path));

                saved_stack[11] = length < 0
                    ? (unsigned int)-1
                    : (unsigned int)process_copy_string_to_array(
                        path,
                        (unsigned int *)saved_stack[10],
                        capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_CHANGE_DIRECTORY)
    {
        saved_stack[11] = (unsigned int)filesystem_change_directory(
            process->arguments);
    }
    else if (syscall_number == PROCESS_SERVICE_OPEN_FILE)
    {
        char path[128];

        if (process_copy_user_path(process, saved_stack[8], path))
        {
            saved_stack[11] = (unsigned int)filesystem_open_file(
                process->process_id, path);
        }
        else
        {
            saved_stack[11] = (unsigned int)-1;
        }
    }
    else if (syscall_number == PROCESS_SERVICE_READ_FILE)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 256 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (!process_stack_buffer_is_valid(
                process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)filesystem_read_file(
                    process->process_id,
                    saved_stack[8],
                    (unsigned int *)saved_stack[10],
                    capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_CLOSE_FILE)
    {
        saved_stack[11] = (unsigned int)filesystem_close_file(
            process->process_id, saved_stack[8]);
    }
    else if (syscall_number == PROCESS_SERVICE_WRITE_FILE)
    {
        unsigned int length = saved_stack[9];
        unsigned int buffer_size;

        if (length > 256 ||
            length > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = length * sizeof(unsigned int);
            if (buffer_size != 0 &&
                !process_stack_buffer_is_valid(
                    process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)filesystem_write_file(
                    process->process_id,
                    saved_stack[8],
                    (const unsigned int *)saved_stack[10],
                    length);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_TRUNCATE_FILE)
    {
        saved_stack[11] = (unsigned int)filesystem_truncate_file(
            process->process_id, saved_stack[8]);
    }
    else if (syscall_number == PROCESS_SERVICE_KILL_PROCESS)
    {
        saved_stack[11] = (unsigned int)process_kill_by_name(
            process->process_id, process->arguments);
    }
    else if (syscall_number == PROCESS_SERVICE_CREATE_PROCESS)
    {
        char filename[128];
        unsigned int child_process_id;
        unsigned int child_slot;
        enum ProcessStartResult result;

        if (!process_copy_user_path(process, saved_stack[8], filename))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            unsigned int flags = disable_interrupts();
            result = process_start(
                filename,
                "",
                &child_process_id,
                process->background);
            if (result != PROCESS_START_OK)
            {
                kernel_log_value(
                    KERNEL_LOG_WARNING,
                    "process",
                    "create process failed with result",
                    (unsigned int)result);
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)-1;
                for (child_slot = 1;
                    child_slot < TASK_LIMIT;
                    ++child_slot)
                {
                    Process *child = &processes[child_slot];

                    if (child->state == TASK_ACTIVE &&
                        child->process_id == child_process_id)
                    {
                        child->parent_process_id = process->process_id;
                        process_inherit_environment(child, process);
                        saved_stack[11] = child_process_id;
                        break;
                    }
                }
                restore_interrupts(flags);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_SEEK_FILE)
    {
        saved_stack[11] = (unsigned int)filesystem_seek_file(
            process->process_id, saved_stack[8],
            (int)saved_stack[10], (int)saved_stack[9]);
    }
    else if (syscall_number == PROCESS_SERVICE_GET_ENVIRONMENT)
    {
        unsigned int entry = saved_stack[8];
        unsigned int capacity = saved_stack[9];

        if (entry >= PROCESS_ENV_COUNT || capacity == 0 || capacity > 128 ||
            !process_stack_buffer_is_valid(process, saved_stack[10],
                capacity * sizeof(unsigned int)))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            saved_stack[11] = (unsigned int)process_copy_string_to_array(
                process->environment[entry], (unsigned int *)saved_stack[10],
                capacity);
        }
    }
    else if (syscall_number == PROCESS_SERVICE_SET_ENVIRONMENT)
    {
        char variable[128];
        unsigned int length = 0, entry, free_entry = PROCESS_ENV_COUNT;

        saved_stack[11] = (unsigned int)-1;
        if (process_copy_user_path(process, saved_stack[8], variable))
        {
            while (variable[length] != '\0' && variable[length] != '=')
            {
                char c = variable[length];
                if (!((c >= 'A' && c <= 'Z') ||
                      (c >= 'a' && c <= 'z') || c == '_' ||
                      (length != 0 && c >= '0' && c <= '9')))
                    break;
                ++length;
            }
            if (length != 0 && variable[length] == '=')
            {
                for (entry = 0; entry < PROCESS_ENV_COUNT; ++entry)
                {
                    unsigned int i = 0;
                    if (process->environment[entry][0] == '\0')
                    {
                        if (free_entry == PROCESS_ENV_COUNT) free_entry = entry;
                        continue;
                    }
                    while (i < length &&
                        process->environment[entry][i] == variable[i]) ++i;
                    if (i == length && process->environment[entry][i] == '=')
                    {
                        free_entry = entry;
                        break;
                    }
                }
                if (free_entry != PROCESS_ENV_COUNT)
                {
                    for (entry = 0; variable[entry] != '\0'; ++entry)
                        process->environment[free_entry][entry] = variable[entry];
                    process->environment[free_entry][entry] = '\0';
                    saved_stack[11] = 0;
                }
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_INPUT_EOF)
    {
        saved_stack[11] = process->input_pipe == 1 &&
            process_pipe.active && !process_pipe.overflowed &&
            process_pipe.producer_closed && process_pipe.buffered == 0;
    }
    else if (syscall_number == PROCESS_SERVICE_GROW_HEAP)
    {
        unsigned int physical;
        unsigned int address;
        unsigned int index;
        volatile unsigned int *page;

        saved_stack[11] = 0;
        if (process->heap_pages < PROCESS_HEAP_PAGES)
        {
            physical = memory_allocate_page();
            if (physical != 0)
            {
                address = PROCESS_HEAP_BASE + process->heap_pages * 4096;
                page = (volatile unsigned int *)physical;
                for (index = 0; index < 1024; ++index)
                {
                    page[index] = 0;
                }
                if (memory_map_page_in(
                    process->page_directory, address, physical, USER_PAGE_FLAGS))
                {
                    ++process->heap_pages;
                    saved_stack[11] = address;
                }
                else if (!memory_free_page(physical))
                {
                    kernel_log(KERNEL_LOG_ERROR, "process", "heap page release failed");
                }
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_SLEEP)
    {
        unsigned int milliseconds = saved_stack[8];

        saved_stack[11] = 0;
        if (milliseconds != 0)
        {
            if (milliseconds > 60000)
            {
                milliseconds = 60000;
            }
            process->wake_tick = tick_count + milliseconds;
            process->state = TASK_SLEEPING;
            current_task = find_next_active_task();
        }
    }
    else if (syscall_number == PROCESS_SERVICE_TERMINAL_HOME)
    {
        if (!process->background)
        {
            terminal_home();
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SERVICE_START_PROCESS)
    {
        char filename[128];
        char arguments[128];

        if (!process_copy_user_path(process, saved_stack[8], filename) ||
            !process_copy_user_path(process, saved_stack[10], arguments))
        {
            saved_stack[11] = (unsigned int)-(int)PROCESS_START_INVALID_NAME;
        }
        else
        {
            saved_stack[11] = (unsigned int)process_spawn(
                process, filename, arguments, saved_stack[9]);
        }
    }
    else if (syscall_number == PROCESS_SERVICE_WAIT_PROCESS)
    {
        unsigned int child_process_id = saved_stack[8];
        unsigned int child_slot;

        saved_stack[11] = (unsigned int)-1;
        for (child_slot = 1; child_slot < TASK_LIMIT; ++child_slot)
        {
            Process *child = &processes[child_slot];

            if (child->process_id != child_process_id ||
                child->parent_process_id != process->process_id)
            {
                continue;
            }

            if (child->state == TASK_EXITED)
            {
                child->parent_process_id = 0;
                child->state = TASK_UNUSED;
                saved_stack[11] = child->exit_status;
            }
            else if (child->state == TASK_ACTIVE ||
                child->state == TASK_SLEEPING ||
                child->state == TASK_WAITING ||
                child->state == TASK_DYING)
            {
                process->wait_process_id = child_process_id;
                saved_stack[11] = 0;
                process->state = TASK_WAITING;
                current_task = find_next_active_task();
            }
            break;
        }
    }
    else if (syscall_number == PROCESS_SERVICE_READ_STREAM)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 256 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (capacity != 0 &&
                !process_stack_buffer_is_valid(
                    process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)process_stream_read(
                    process,
                    saved_stack[8],
                    (unsigned int *)saved_stack[10],
                    capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_WRITE_STREAM)
    {
        unsigned int length = saved_stack[9];
        unsigned int buffer_size;

        if (length > 256 ||
            length > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = length * sizeof(unsigned int);
            if (length != 0 &&
                !process_stack_buffer_is_valid(
                    process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                unsigned char buffer[256];
                const unsigned int *source =
                    (const unsigned int *)saved_stack[10];
                unsigned int index;
                int valid = 1;

                for (index = 0; index < length; ++index)
                {
                    if (source[index] > 255)
                    {
                        valid = 0;
                        break;
                    }
                    buffer[index] = (unsigned char)source[index];
                }
                saved_stack[11] = valid
                    ? (unsigned int)process_stream_write(
                        process,
                        saved_stack[8],
                        buffer,
                        length)
                    : (unsigned int)-1;
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_READ_PIPE)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 256 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (!process_stack_buffer_is_valid(
                process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)process_pipe_read(
                    process->input_pipe,
                    (unsigned int *)saved_stack[10],
                    capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_WRITE_CHARACTER)
    {
        unsigned char character = (unsigned char)saved_stack[8];

        if (process_stream_write(
            process,
            process->standard_output_handle,
            &character,
            1) < 0)
        {
            terminal_write(
                "process: pipe buffer full or reader closed; terminating.\n");
            process_terminate_current();
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SERVICE_CLEAR_GRAPHICS)
    {
        if (!process->background)
        {
            video_clear_graphics(
                process->process_id,
                process->window_handle,
                (unsigned char)saved_stack[8]);
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SERVICE_DRAW_PIXEL)
    {
        if (!process->background)
        {
            video_draw_pixel(
                process->process_id,
                process->window_handle,
                saved_stack[8],
                saved_stack[9],
                (unsigned char)saved_stack[10]);
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SERVICE_PRESENT_GRAPHICS)
    {
        if (!process->background)
        {
            video_present_graphics(
                process->process_id,
                process->window_handle);
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SERVICE_CREATE_WINDOW)
    {
        char title[32];

        if (process->background || process->window_handle != 0 ||
            !process_copy_user_title(process, saved_stack[8], title))
        {
            saved_stack[11] = 0;
        }
        else
        {
            process->window_handle = video_create_window(
                process->process_id,
                title,
                saved_stack[10],
                saved_stack[9]);
            saved_stack[11] = process->window_handle;
        }
    }
    else if (syscall_number == PROCESS_SERVICE_START_GRAPHICS &&
        process->window_handle == 0)
    {
        terminal_write(
            "process: graphics mode requires a window; terminating.\n");
        process_terminate_current();
    }
    else if (syscall_number >= PROCESS_SERVICE_CLOCK &&
        syscall_number < PROCESS_SERVICE_WRITE_CHARACTER)
    {
        if (!process->background ||
            (syscall_number != PROCESS_SERVICE_TERMINAL_CLEAR &&
                syscall_number != PROCESS_SERVICE_START_GRAPHICS &&
                syscall_number != PROCESS_SERVICE_END_GRAPHICS))
        {
            shell_run_service(
                syscall_number,
                process->arguments,
                process->process_id,
                process->window_handle);
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SYSCALL_EXIT)
    {
        process->exit_status = saved_stack[8] & 255;
        process_terminate_current();
    }
    else
    {
        terminal_write("process: unsupported system call; terminating.\n");
        process_terminate_current();
    }

    if (current_task == task && processes[task].state == TASK_ACTIVE)
    {
        /* Blocking services may be preempted; resume at the original syscall frame. */
        processes[task].saved_stack = saved_stack;
        return (unsigned int)saved_stack;
    }

    process_activate_current();
    return (unsigned int)processes[current_task].saved_stack;
}
